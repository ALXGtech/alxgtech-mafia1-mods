/* fp_wmatrix.h - ROUTE 2: the engine's own seam, and it needs no Direct3D at all.
 *
 * Route 1 hooks `IDirect3DDevice8::SetTransform` and rewrites the VIEW matrix on its way past.
 * That seam works - it fires, it is version-independent, and at a zero delta it reproduces the
 * engine's picture byte for byte - but it sits DOWNSTREAM of everything, so it inherits every
 * consequence of the engine having already decided what the camera is:
 *
 *   - VIEW is sent by more than one pass in a frame (one rotation-only, one live), and the two
 *     are told apart only by return address. Writing the wrong one puts the eye at the world
 *     origin with the city two kilometres away. That cost three runs.
 *   - PROJECTION, `camera+0x1E4` (VIEW x PROJECTION) and `camera+0x224` (inverse VIEW) are all
 *     derived from the camera BEFORE our hook sees anything, so they stay consistent with the
 *     old eye while VIEW carries the new one. Nothing downstream of D3D can fix that.
 *
 * Route 2 moves the write UPSTREAM of all of it. `?UpdateWMatrixProc@I3D_frame@@AAEXXZ` refreshes
 * a frame's WORLD matrix, and the engine's VIEW builder calls it and then transposes WORLD into
 * VIEW immediately afterwards. Write the eye into WORLD's translation row ON THE RETURN of that
 * call and the engine derives VIEW, PROJECTION and every cache itself, consistently, for every
 * pass. The whole two-passes class of bug stops existing.
 *
 * What makes the route portable is not an address but the EXPORT: LS3DF.dll exports that function
 * by name on every build we have seen, so it is found with GetProcAddress and never sought at a
 * hard-coded offset.
 *
 * WHY A PROLOGUE GUARD AND NOT A VERSION CHECK. The first six bytes of that function are checked
 * before a single byte is written, and a mismatch REFUSES rather than patching anyway. A detour
 * written over an instruction boundary we guessed wrong is not a camera bug, it is a crash in
 * someone else's game - and the version resource on this build cannot be trusted to decide.
 *
 * THE PROLOGUE IS RELOCATABLE AND THAT IS NOT LUCK - it is checked. The six bytes hold a stack
 * adjustment, a push and a register move: no relative branch, nothing position-dependent (this is
 * 32-bit, so there is no rip-relative addressing either). They can be copied into a trampoline
 * verbatim. A prologue that began with a `call` or a `jmp` could not, which is exactly why the
 * guard covers all six rather than counting to five and hoping.
 *
 * WHAT IS STORED IS THE DIGEST OF THOSE SIX BYTES, NOT THE BYTES, since 2026-08-14 - the same
 * change, for the same reason, as the build signature in `src\ffb\mafia_ffb_v6.c`. Both uses were
 * always pure equality tests, so the safety property is identical and none of the game's own code
 * is carried in this file. A refusal now logs the six bytes it FOUND, which is the half that
 * helps whoever reports the fault; it is their file, not ours.
 */

#ifndef FP_WMATRIX_H
#define FP_WMATRIX_H

#include "../shared/md5_core.h"

/* Defined further down fp_camera.c. Declared here so this header can say why it refused - a
   silent refusal is the failure mode this project treats as a defect regardless of cause. */
static void FpLog(const char *msg, DWORD a, DWORD b);

/* thiscall, no arguments, void return: `this` arrives in ECX and the callee pops nothing.
   clang's fastcall is the same register pair (ECX, EDX) for the first two integer arguments, so
   a two-argument fastcall function IS a no-argument thiscall function as far as the ABI is
   concerned. `edx` is never read; it is present only to keep `this` in ECX. */
typedef void (__attribute__((fastcall)) *FpUpdW_t)(void *self, void *edx);

#define FP_UPDW_NAME  "?UpdateWMatrixProc@I3D_frame@@AAEXXZ"
#define FP_UPDW_STEAL 6      /* bytes copied to the trampoline; see the prologue note above */

/* md5 of the six prologue bytes this route was built against. Compared, never written: the
   trampoline is always built from the bytes actually found at the site. */
#define FP_UPDW_PROLOGUE_MD5  "03f4ebe77545d5e858988f854fe2fb93"

/* 1 when the six bytes at `p` are the prologue we know. */
static int FpUpdWPrologueOk(const BYTE *p)
{
    char here[33];
    md5_bytes(p, FP_UPDW_STEAL, here);
    return md5_hex_same(here, FP_UPDW_PROLOGUE_MD5);
}

static FpUpdW_t  g_updwTramp = NULL;   /* the six stolen bytes, then a jump back to site+6 */
static BYTE     *g_updwSite  = NULL;   /* where the export lives, so the patch can be re-checked */
static BYTE     *g_updwTrampMem = NULL;

/* Install the detour. Returns 1 on success, -1 on a refusal that must not be retried.
 *
 * The ORDER matters and is the opposite of the obvious one: the trampoline is built and proven
 * FIRST, and only then is the live function overwritten. Patch first and the process spends the
 * window between the two writes with a jump to a trampoline that does not exist yet.
 */
/* Split from FpWMatrixInstall so the mechanics can be tested WITHOUT LS3DF and without the game:
   the offline harness builds a synthetic function with the same prologue, detours it, calls it,
   and checks that both the original body and the detour ran. That is the only way to prove the
   thiscall/fastcall assumption and the two rel32 computations by execution rather than by
   reading them - and a trampoline whose jump is one byte off does not misbehave, it crashes. */
static int FpWMatrixInstallAt(BYTE *fn, void *detour)
{
    /* THE VERSION GUARD. All six bytes, before anything is written. What gets logged on a
       refusal is what was FOUND - three lines of two bytes - because that is what tells us
       which build somebody has, and it is their build's code, not ours to withhold. */
    if (!FpUpdWPrologueOk(fn)) {
        FpLog("  its prologue is NOT the one this route was built against; route 2 REFUSED, "
              "nothing was written. Bytes 0 and 1:", (DWORD)fn[0], (DWORD)fn[1]);
        FpLog("  ...bytes 2 and 3:", (DWORD)fn[2], (DWORD)fn[3]);
        FpLog("  ...bytes 4 and 5:", (DWORD)fn[4], (DWORD)fn[5]);
        return -1;
    }

    BYTE *tramp = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        FpLog("could not allocate a trampoline - route 2 REFUSED", GetLastError(), 0);
        return -1;
    }
    for (int i = 0; i < FP_UPDW_STEAL; i++) tramp[i] = fn[i];
    tramp[FP_UPDW_STEAL] = 0xE9;                                  /* jmp rel32 back to site+6 */
    *(LONG *)(tramp + FP_UPDW_STEAL + 1) =
        (LONG)((BYTE *)(fn + FP_UPDW_STEAL) - (tramp + FP_UPDW_STEAL + 5));

    DWORD old;
    if (!VirtualProtect(fn, FP_UPDW_STEAL, PAGE_EXECUTE_READWRITE, &old)) {
        FpLog("could not make UpdateWMatrixProc writable - route 2 REFUSED", GetLastError(), 0);
        VirtualFree(tramp, 0, MEM_RELEASE);
        return -1;
    }
    fn[0] = 0xE9;
    *(LONG *)(fn + 1) = (LONG)((BYTE *)detour - (fn + 5));
    /* The sixth byte is a NOP rather than left as it was. `8b e9`'s tail would otherwise sit
       there as a lone `e9` - a jump opcode - and anything that ever disassembles or single-steps
       this function would read a garbage instruction. It is never executed either way; this is
       for whoever reads the bytes next, which has already been me twice. */
    fn[5] = 0x90;
    VirtualProtect(fn, FP_UPDW_STEAL, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, FP_UPDW_STEAL);

    g_updwTramp = (FpUpdW_t)tramp;
    g_updwTrampMem = tramp;
    g_updwSite  = fn;
    FpLog("UpdateWMatrixProc DETOURED - trampoline, detour target",
          (DWORD)(DWORD_PTR)tramp, (DWORD)(DWORD_PTR)detour);
    return 1;
}

static int FpWMatrixInstall(HMODULE ls, void *detour)
{
    BYTE *fn = (BYTE *)GetProcAddress(ls, FP_UPDW_NAME);
    if (!fn) {
        FpLog("LS3DF.dll does not export " FP_UPDW_NAME " - this is not a build this route "
              "understands, route 2 REFUSED", 0, 0);
        return -1;
    }
    FpLog("UpdateWMatrixProc found at LS3DF+", (DWORD)(fn - (BYTE *)ls), (DWORD)(DWORD_PTR)fn);
    return FpWMatrixInstallAt(fn, detour);
}

/* Did it stick, and is it still there? Slot 37 of the D3D vtable taught this project that a
   write which reads back correctly at install time can be gone by the first frame, and that the
   difference between "never took" and "was put back" is the whole diagnosis. Called once a
   second after install, and again from the frame watch. Returns 1 if the site and the trampoline
   both still hold what we wrote. */
static int FpWMatrixVerify(void)
{
    if (!g_updwSite || !g_updwTrampMem) return 0;
    if (g_updwSite[0] != 0xE9) return 0;
    /* The trampoline's own bytes matter as much as the site's: if something restored the
       original function over our copy, the detour would call the ORIGINAL prologue and then jump
       back into the middle of the original - an infinite loop rather than a wrong picture. */
    if (!FpUpdWPrologueOk(g_updwTrampMem)) return 0;
    if (g_updwTrampMem[FP_UPDW_STEAL] != 0xE9) return 0;
    return 1;
}

#endif /* FP_WMATRIX_H */
