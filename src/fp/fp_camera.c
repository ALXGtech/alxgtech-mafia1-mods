/* fp_camera.c - mafia_fp.asi, the first-person driving camera.
 *
 * Spec: docs\superpowers\specs\2026-07-28-fp-camera-driving-design.md
 * Camera contract with a VR build is on file, and the
 * reply to it, dated 2026-07-27c, corrects it in two places that matter
 * here (the rotation source, and NOT scaling the projection on this build).
 *
 * THIS FILE IS THE HOOK CHAIN ONLY. It reaches SetTransform and proves it did, in the log. The
 * pose pipeline, the car anchor and the keys are the next increment; until they exist the
 * substitution is deliberately absent rather than stubbed to something plausible, because a
 * camera that moves the picture by a wrong amount is harder to diagnose than one that does not
 * move it at all.
 *
 * Built -nostdlib like the FFB module, so there is no CRT here: no memset, no strcmp, no
 * printf. Anything that looks like one is written out below.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "fp_iat.h"
#include "fp_pose.h"
#include "fp_wmatrix.h"
#include "../shared/car_anchor.h"
#include "../shared/scene_walk.h"
#include "../shared/hud_aspect.h"

/* ---------------------------------------------------------------- log -----------------------
 * The standing rule in this repository is that a guard whose failure mode is silence is worse
 * than no guard - it was paid for twice, once by a gearbox module that ran inert for eighteen
 * minutes saying nothing. So every step of the chain writes a line, including the steps that
 * succeed: a log that only speaks on failure cannot tell "it worked" from "it never ran".
 *
 * Plain text, not the FFB module's binary record format, and deliberately: this file has a
 * handful of events for a whole session, not thousands per second, and a text line needs no
 * decoder to read at 2 a.m.
 */
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static int g_logReady = 0;

static int FpLen(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* -nostdlib, so there is no memcpy. `-fno-builtin` in build-fp.ps1 is what stops -O2 turning
   this loop back into a call to the one that does not exist. */
static void FpMemCpy(void *dst, const void *src, DWORD n)
{
    BYTE *d = (BYTE *)dst; const BYTE *s = (const BYTE *)src;
    while (n--) *d++ = *s++;
}

/* Declared here rather than beside the draw hooks that use them, because the ini reader below
   is the other half of this feature and it comes first in the file. */
static LONG g_hudAspectPct = -1;    /* -1 auto, 0 off, 25..200 explicit */
/* 1 = squeeze each element about the SCREEN EDGE it sits against, so the speedometer stays in its
   corner and the slack lands in the middle of the screen, where there is nothing in play. 0 = the
   old centre squeeze, which fixed every shape and pulled every corner inward. Settled on the
   first on 2026-08-12; the second stays reachable so the two can be compared in one session
   without a rebuild. See ..\shared\hud_aspect.h. */
static LONG g_hudAspectAnchored = 1;
/* hud_probe: log the span of every screen-space draw, so the seam can be CHOSEN from a log rather
   than guessed. Needs diag = 1 to write anything, and it is armed by hand for one short session -
   see the note at the probe itself. */
static LONG g_hudProbe     = 0;
/* His decision of 2026-08-14, in two switches. hud_zones: correct ONLY the radar's corner and the
   speedometer's, because correcting every screen-space draw moved the mission text and laid the
   menu out wrong. hud_incar_only: do it only while sitting in a car, which is the only place
   those two elements exist. Both default ON; 0 on either restores the old whole-interface
   behaviour for comparison. Declared here, with the other ini-backed values, because the live
   re-reader runs long before the drawing code is defined. */
static LONG g_hudZones     = 1;
static LONG g_hudInCarOnly = 1;
static LONG g_hudProbeRows = 0;
/* dev_keys: the bench switches - Insert and Page Down, see the block in the frame loop. 0 in the
   shipped ini and 1 on our own installs, so nothing a stranger presses can reach them. */
static LONG g_devKeys      = 0;
static int  g_hudLogged    = 0;

/* A SIGNED ini read, and it is not a nicety. GetPrivateProfileIntA is documented to return ZERO
   when the value in the file is negative - so a key whose documented default is -1 reads back as
   0, and for hud_aspect_pct 0 means OFF. The fix would have shipped switched off by the very
   line the ini tells the player to write. Read the string and parse it here instead. */
static LONG FpAtoiS(const char *s)
{
    int neg = 0; LONG v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

/* -1 auto, 0 off, 25..200 explicit. Anything else is a typo, and a typo means AUTO rather than
   a silent off - the same rule `route` already follows, and for the same reason: a value that
   quietly disables a feature reads as the feature not working. */
static LONG FpHudPctFromIni(const char *ini)
{
    char buf[32];
    GetPrivateProfileStringA("fp", "hud_aspect_pct", "-1", buf, sizeof(buf), ini);
    LONG v = FpAtoiS(buf);
    if (v == 0 || v == -1) return v;
    if (v >= 25 && v <= 200) return v;
    return -1;
}

static void FpCat(char *dst, int cap, const char *src)
{
    int n = FpLen(dst);
    while (n < cap - 1 && *src) dst[n++] = *src++;
    dst[n] = 0;
}

/* unsigned hex, because every value this file logs is an address, an HRESULT or a flag */
static void FpCatHex(char *dst, int cap, DWORD v)
{
    char t[16]; int i = 0;
    const char *d = "0123456789ABCDEF";
    if (!v) { FpCat(dst, cap, "0"); return; }
    while (v) { t[i++] = d[v & 0xF]; v >>= 4; }
    char out[20]; int j = 0;
    while (i) out[j++] = t[--i];
    out[j] = 0;
    FpCat(dst, cap, out);
}

static void FpLog(const char *msg, DWORD a, DWORD b)
{
    if (!g_logReady) return;
    char line[512]; line[0] = 0;
    FpCat(line, sizeof(line), msg);
    FpCat(line, sizeof(line), "  a=0x");
    FpCatHex(line, sizeof(line), a);
    FpCat(line, sizeof(line), " b=0x");
    FpCatHex(line, sizeof(line), b);
    FpCat(line, sizeof(line), "\r\n");

    EnterCriticalSection(&g_logLock);
    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD w;
        SetFilePointer(g_log, 0, NULL, FILE_END);
        WriteFile(g_log, line, (DWORD)FpLen(line), &w, NULL);
        FlushFileBuffers(g_log);   /* a crash must not eat the line that explains it */
    }
    LeaveCriticalSection(&g_logLock);
}

/* The same line with a STRING instead of two numbers. Added 2026-08-12 for one question a number
   could not answer: which module owns a vtable slot we refused. A base address identifies nobody;
   a path names the overlay, shim or wrapper in one line. */
static void FpLogStr(const char *msg, const char *s)
{
    if (!g_logReady) return;
    char line[512]; line[0] = 0;
    FpCat(line, sizeof(line), msg);
    FpCat(line, sizeof(line), " ");
    FpCat(line, sizeof(line), s);
    FpCat(line, sizeof(line), "\r\n");

    EnterCriticalSection(&g_logLock);
    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD w;
        SetFilePointer(g_log, 0, NULL, FILE_END);
        WriteFile(g_log, line, (DWORD)FpLen(line), &w, NULL);
        FlushFileBuffers(g_log);
    }
    LeaveCriticalSection(&g_logLock);
}

/* <game>\mafia_fp.log, beside the exe. One file per launch would be tidier, but the gearbox
   module's CREATE_ALWAYS destroyed the previous evidence on every launch and cost a whole
   evening, so this one APPENDS and carries a launch banner instead. */
/* SHIPPED OFF since 2026-08-11, the same fix the FFB mod got in v7.69 and for the same reason:
   this handle was opened unconditionally, appended forever and was never rotated, so a player
   who installed the mod would find a file growing in the game folder with nothing in the
   documentation explaining it and no way to switch it off. `diag = 1` in mafia_fp.ini turns it
   back on for a bench. Gating the OPEN is the whole fix - FpLog() already returns on
   !g_logReady, so all 182 call sites become no-ops with nothing else to change.
   FpIniPath() therefore has to run BEFORE this; it does no logging of its own, so the order
   swap loses nothing. */
static void FpLogOpen(void)
{
    char path[MAX_PATH], ini[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    while (n && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    path[n] = 0;

    /* the same directory, so the ini is resolved here rather than depending on g_iniPath,
       which is declared further down this file */
    ini[0] = 0;
    FpCat(ini, MAX_PATH, path);
    FpCat(ini, MAX_PATH, "mafia_fp.ini");
    if (GetPrivateProfileIntA("fp", "diag", 0, ini) == 0) return;

    FpCat(path, MAX_PATH, "mafia_fp.log");

    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE) return;
    InitializeCriticalSection(&g_logLock);
    g_logReady = 1;
    FpLog("---- mafia_fp.asi launch, diag=1, pid", GetCurrentProcessId(), 0);
}

/* ---------------------------------------------------------------- D3D8 ----------------------
 * Only the vtable slots this file touches, so there is no d3d8.h dependency and no COM headers
 * in a -nostdlib build. The indices are fixed by the COM ABI - a vtable cannot be reordered
 * without breaking every program that uses it - but they are still CHECKED at runtime below,
 * because ABI invariance and runtime correctness are different claims.
 */
#define IDIRECT3D8_CREATEDEVICE        15
#define IDIRECT3DDEVICE8_PRESENT       15
#define IDIRECT3DDEVICE8_SETTRANSFORM  37
/* The probe set, 2026-07-28. Measured: 1000 frames presented, SetTransform called ZERO times -
   so this engine does not put its view matrix through the fixed-function transform pipeline at
   all, and the seam the whole camera contract is built on is not on the path. These three say
   where it goes instead. Indices are the D3D8 COM ABI, and Present at 15 firing per frame has
   already confirmed the counting base. */
#define IDIRECT3DDEVICE8_BEGINSCENE        34
#define IDIRECT3DDEVICE8_DRAWPRIM          70
#define IDIRECT3DDEVICE8_DRAWINDEXEDPRIM   71
#define IDIRECT3DDEVICE8_DRAWPRIMUP        72
#define IDIRECT3DDEVICE8_DRAWINDEXEDPRIMUP 73
#define IDIRECT3DDEVICE8_SETVERTEXSHADER   76
#define IDIRECT3DDEVICE8_GETVERTEXSHADER   77
#define IDIRECT3DDEVICE8_SETVSCONSTANT     79
/* GetViewport, for the HUD squeeze - it is the only honest source of the surface the interface
   is being spread across. The index is the same COM ABI as every other one above, and it is not
   taken on faith: the values it returns are range-checked before anything is done with them, so
   a wrong index gives up rather than scaling by garbage. */
#define IDIRECT3DDEVICE8_GETVIEWPORT       41
#define D3DTS_VIEW        2
#define D3DTS_PROJECTION  3

/* FVF bits. The position field is a 3-bit enum in the low bits, and D3DFVF_XYZRHW means the
   vertex is ALREADY in screen space - which is exactly and only what the interface uses. */
#define D3DFVF_POSITION_MASK  0x00Eu
#define D3DFVF_XYZRHW         0x004u

typedef void *(WINAPI *Direct3DCreate8_t)(UINT sdkVersion);
typedef HRESULT (WINAPI *CreateDevice_t)(void *self, UINT adapter, DWORD devType, HWND focus,
                                         DWORD behaviour, void *presentParams, void **outDev);
typedef HRESULT (WINAPI *SetTransform_t)(void *self, DWORD state, const float *matrix);
typedef HRESULT (WINAPI *Present_t)(void *self, const void *a, const void *b, HWND c,
                                    const void *d);
typedef HRESULT (WINAPI *DrawIndexedPrim_t)(void *self, DWORD type, UINT minIdx, UINT numV,
                                            UINT startIdx, UINT primCount);
typedef HRESULT (WINAPI *SetVertexShader_t)(void *self, DWORD handle);
typedef HRESULT (WINAPI *SetVSConstant_t)(void *self, DWORD reg, const void *data, DWORD count);

/* Forward declarations, so the frame-1000 report can compare what is CURRENTLY in the device's
   vtable against our own functions. FpPresent is defined above both of them and does the
   comparison; without these it would be reading identifiers that do not exist yet. */
static HRESULT WINAPI FpSetTransform(void *self, DWORD state, const float *matrix);
static HRESULT WINAPI FpDraw(void *self, DWORD type, UINT minIdx, UINT numV,
                             UINT startIdx, UINT primCount);
/* The per-frame slot watch below runs inside Present, which is defined before the hook writer.
   It has to put slot 37 BACK, not merely report it, so the writer is declared here too. */
static void *FpHookVTable(void *obj, int index, void *replacement);
static int   FpSameModuleAsD3D(void *entry);

/* ---------------------------------------------------------------- settings ------------------
 * `mafia_fp.ini`, `[fp]`, beside the exe. `enabled` is jointly owned with the VR project by
 * their agreement; every other key is ours, and the file is NEVER re-emitted from a built-in
 * table - the gearbox binder destroyed a hand-maintained config twice by doing exactly that,
 * so a write here touches one key and leaves the rest of the file alone.
 *
 * Centimetres in the file, metres in the maths. A settings file a human edits should carry the
 * unit a human thinks in, and 5 is easier to type and to reason about than 0.05.
 *
 * OFF BY DEFAULT. An installed mod that silently changes what the game looks like is worse than
 * one that has to be switched on.
 */
static volatile LONG g_enabled  = 0;
static volatile LONG g_seatUpCm = 0;     /* + raises the eye point   */
/* LATERAL, added 2026-07-31. `seat[0]` - the "right" component - was hard-coded to 0.0f, so the
   eye sat on the car's centre line and there was no way to move it to the driver's side at all.
   Once the basis convention was fixed and the camera finally held still, the request was to move it
   back some distance, down about 15 centimeters, and further left about 25 centimeters. The first two had
   parameters; the third did not. NEGATIVE is left, on the assumption that row 0 is "right" as
   CamSeat's own comment says - if the eye goes the other way, flip the sign, not the code. */
static volatile LONG g_seatSideCm = 0;   /* + right, - left          */
static volatile LONG g_deltaPct = 0;     /* the control scale on the substitution; see FpLoadIni */
/* THE A/B, 2026-07-28. "Only skybox" is a human verdict on a picture, and the two explanations
   for it - the geometry is CULLED, or the geometry is drawn and flies off screen - look
   identical to the eye. They do not look identical to the draw counters. Alternating the
   substitution on and off inside one run, and reporting how much geometry each window drew,
   separates them without anybody watching the screen. */
static volatile LONG g_abOn     = 1;
static volatile LONG g_cityFrame = 0;    /* the frame the car anchor first resolved */
static volatile LONG g_abFrames = 0;     /* 0 = never flip the substitution; see the A/B block */
static volatile LONG g_passOriginal = 0; /* compute everything, hand the engine its own matrix */
static volatile LONG g_winW     = 0;     /* 0 = leave the game fullscreen; see FpWindowTheGame */
static volatile LONG g_winH     = 0;
static volatile LONG g_seatFwCm = 0;     /* + moves it toward the nose - this is what clears
                                            the cap in the first mission, not height */
/* WHICH SEAM. 1 = the D3D8 SetTransform hook (route 1, everything above), 2 = the engine's own
   UpdateWMatrixProc detour (route 2, fp_wmatrix.h). Default 1, and deliberately: route 1 is the
   deployed, measured path and the run that judges it has not been taken yet. At route 1 not one
   byte of route 2's install ever executes, so this key changes the binary's BEHAVIOUR not at all
   until it is set - which is what makes it safe to ship the two side by side and settle the
   question with two launches in one bench session instead of two sessions. */
static volatile LONG g_route    = 1;
/* HOW the jitter trace opens. 1 = the car has moved 5 m, which suits an automated replay.
   2 = the UP ARROW HELD, which is what a manual run needs: the driver skips the intros
   and loads the city at his own pace, so there is no replay clock to anchor to and holding
   accelerate is his own statement that the drive has begun. Default 2, because a manual run
   is the one that cannot be re-taken cheaply. */
static volatile LONG g_traceArm    = 2;
/* WHICH ANCHOR. 0 = car+0x2A10, the field this camera has always used. 1 = the car's render
   frame at [[car+0xE4]+0x68]+0x40, which is what car+0x2A10 is a once-a-tick COPY of. Default
   1: the copy is by construction a tick old, and a tick at speed is the half metre that was
   measured between our eye and the drawn body. 0 is kept so a run can A/B it. */
static volatile LONG g_anchorMode  = 1;
/* Swap components 0 and 2 of each basis row so the basis and the position share one convention -
   see the long comment at the substitution site. 1 = fixed, 0 = the old mismatched behaviour. */
static volatile LONG g_basisSwap   = 1;
/* Defined below, beside the disassembly that justifies it; used above in the detour. */
static int FpCarRenderPos(float *out3);
static int FpCarRenderBasis(float *out9);   /* diagnostic only - see its definition */

/* tan(), without a C library. We link -nostdlib -fno-builtin, so `tanf` does not exist and
   -O2 would happily emit a call to it. `fptan` is an x87 instruction: it replaces st(0) with
   the tangent and then pushes 1.0, so the 1.0 has to be popped to leave the result on top.
   Only ever called with a half-FOV in radians, which is far inside fptan's domain. */
static float FpTan(float x)
{
    double r;
    __asm__ volatile ("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"((double)x));
    return (float)r;
}

/* Same reason as FpTan: no C library, so no sqrtf. `fsqrt` is one x87 instruction. */
static float FpSqrt(float x)
{
    double r;
    __asm__ volatile ("fsqrt" : "=t"(r) : "0"((double)x));
    return (float)r;
}

/* `FpSinCos` and `CamPitch` are in fp_pose.h, beside the rest of the pose maths and inside the
   reach of tests\offline\fp_pose_verify.c. The `fsincos` operand order is the reason. */

/* ---- the two keys, and why these two ------------------------------------------------------
 * As of 2026-07-28: one key TOGGLES the camera, a second CYCLES the height through a few fixed
 * stops. Not a pair of up/down keys - kept simple for smooth play - and above all
 * not the game's own Shift, which the VR and FFB mods use and which is the walk key.
 *
 * F1 to F8 ARE ALL TAKEN by the FFB module: F1-F5 are the wheel rotation ranges, F6 marks the
 * start of a run, F7 and F8 are the "this is right" / "this is wrong" markers he presses while
 * driving. F9 is left alone because quick-load lives there by convention on most games and a
 * camera key that loads a save is not a bug anyone forgives. That leaves F10, F11, F12.
 *
 * Both are INI keys, because the VR project has not settled its own map and asked that nothing
 * be treated as final.
 */
static volatile LONG g_keyToggle = VK_F11;
static volatile LONG g_keyHeight = VK_F12;

/* ---- LIVE SEAT NUDGE KEYS, requested 2026-07-31 -----------------------------------------
 * F row - it belongs to the FFB mod, the gearbox and a separate shifter tool, and
 *  forward-back and left-right... on F1 through F6... and on F7, F8 confirm whether I like this
 *  camera or not."
 *
 * THE COLLISION, stated rather than silently worked around: this file's own header forbids the
 * F row - it belongs to the FFB mod, the gearbox and a separate shifter tool, and
 * `mafia_ffb.asi` (deployed in this very sandbox) addresses its profile table by F1..F5. He
 * asked for the F row anyway, so the F row is the DEFAULT - but every code is read from the ini,
 * so moving them off costs one line and no rebuild. If a nudge also flips an FFB profile, that
 * is the collision biting, not the camera misbehaving.
 *
 * Each press is written straight back to the ini, so the position he settles on survives the
 * next launch and I can read it out of the file instead of asking him for numbers.
 */
static volatile LONG g_keySeatUp    = 0x70;  /* F1  up      */
static volatile LONG g_keySeatDown  = 0x71;  /* F2  down    */
static volatile LONG g_keySeatFwd   = 0x72;  /* F3  forward */
static volatile LONG g_keySeatBack  = 0x73;  /* F4  back    */
static volatile LONG g_keySeatLeft  = 0x74;  /* F5  left    */
static volatile LONG g_keySeatRight = 0x75;  /* F6  right   */
static volatile LONG g_keyVerdictY  = 0x76;  /* F7  "I like this camera"     */
static volatile LONG g_keyVerdictN  = 0x77;  /* F8  "I do not like it"       */
static volatile LONG g_nudgeCm      = 5;

/* ---- NEAR PLANE AND FOV, 2026-07-31 ----------------------------------------------------------
 * After the first tuning drive, at the position found preferable, the head was not
 * rendered, but some edges were: either a piece of the costume, or a piece of the hat,
 * or maybe the collar - so the camera had to be moved too close forward, and, apparently,
 * the field of view was too small.
 * Slivers of geometry a few centimetres from the eye are exactly what a near plane removes, and
 * unlike the visibility-bit route it does not care that Tommy is one SNGMRPH mesh with no head
 * node ([[head-is-one-morph-mesh]]) - it clips by DISTANCE, so it works on every costume.
 *
 * Both live in the same 64-byte PROJECTION at camera+0x1A4. Two facts make writing there sane:
 *   - our own reading of the engine says PROJECTION is written only ~300 times a session, all
 *     before this detour runs, so a write here is not overwritten next frame;
 *   - changing the ANGLE FIELD was proved to do nothing on another build (the engine has
 *     already consumed it) and that scaling this MATRIX does work. So the matrix it is.
 *
 * 0 means "do not touch it" for both, so the shipped default is exactly today's behaviour.
 */
static volatile LONG g_keyNearUp   = 0x78;  /* F9  push the near plane out  */
static volatile LONG g_keyNearDown = 0x79;  /* F10 pull it back in          */
static volatile LONG g_keyLockRoll = 0x7A;  /* F11 toggle the level horizon */

/* The pitch trim, on INSERT and DELETE rather than the F row. The whole F row is spoken for on
   this bench: F1..F6 are the seat, F9..F11 are the near plane and the horizon, and F7/F8 belong
   to the FFB module's "this is right" / "this is wrong" markers - the two keys whose meaning a
   whole drive's analysis rests on. Insert and Delete sit above the arrow block, are findable
   without looking, and no Mafia action uses them. */
static volatile LONG g_keyPitchUp   = 0x2D;  /* INSERT - aim higher */
static volatile LONG g_keyPitchDown = 0x2E;  /* DELETE - aim lower  */
/* HOME - marks the current on-screen view as the one to replace.
   NOT the numpad. THIS KEYBOARD HAS NO NUMPAD - noted repeatedly, and
   mafia_ffb_v6.c has known it since July that top-row digits are used because this keyboard has no numpad.
   This file never got the message, so four of its controls sat on keys that do not physically
   exist on his machine: the camera toggle, the height cycle, the scene dump and this one.
   They were not available either way; they were unreachable, and the scene dump in
   particular was asked for out loud before anyone noticed.
   The nav cluster is the right home: INSERT and DELETE already trim the pitch, so HOME, END,
   PAGE UP and PAGE DOWN sit under the same hand and nothing else in this project or the FFB
   module claims them. */
static volatile LONG g_keyTakeView  = 0x24;   /* HOME */

/* The scene probe's settings. Declared up here with the other keys rather than beside the walk
   itself, because FpLoadIni reads them 1600 lines before that code exists. */
static LONG g_dumpFrames = 0;         /* ini: 0 off, 1 dump once when the scene appears        */
static LONG g_keyDump    = 0x23;      /* END - see g_keyTakeView: there is no numpad here      */
static char g_hideName[64];           /* ini: exact node name to hide, empty = hide nothing    */
static int  g_dumped = 0;
static int  g_hideSaid = 0;

/* ---- LEVEL HORIZON, requested 2026-07-31 ------------------------------------------------
 * "can the horizon be locked on roll? That is, if I am pitch-tilting or it is
 *  turning, let it stay as it is now. But I want my camera locked on roll.
 *  And meanwhile the car will be rotating around, and I will see how hard it is
 *  being tossed."
 * His hypothesis is that the FFB and VR builds had this and that it is what made hitting a kerb
 * FEEL like the car being thrown - the car rotating around a level head reads as violence, the
 * whole world tipping reads as nothing. He also thinks it may suit VR; that is theirs to test.
 *
 * Roll is the only one of the three that gets removed: keep the forward axis exactly as the
 * engine aimed it (so pitch and yaw are untouched), lay "right" flat by crossing world up with
 * it, then rebuild "up" from those two. Standard, and it cannot drift, because it is recomputed
 * from the engine's own forward every frame rather than accumulated.
 * World up is +Y - confirmed three independent ways on another build.
 *
 * SETTLED 2026-07-31, and by an A/B he ran himself: three F11 presses - on, off, on - and he
 * finished with it ON. "Yes, this is much better. Write down that this is what we do. Roll exactly like
 * this, as it is now." So this is the DEFAULT now, not an experiment: `lock_roll = 0` in the ini turns
 * it off, nothing else has to change.
 */
static volatile LONG g_lockRoll = 1;
static volatile LONG g_nearCm  = 0;   /* near plane in cm; 0 = leave the engine's */
static volatile LONG g_fovDeg  = 0;   /* horizontal FOV in degrees; 0 = leave it   */

/* ---- THE PITCH TRIM, 2026-08-06 -----------------------------------------------------------
 * After the first drive with the camera on this install, the request was for a Pitch slider
 * for the first-person camera from the cab, because when the camera was used, where the camera
 * looks down a little, the horizon was too high, which felt completely unnatural.
 *
 * The complaint is measurable and was measured: the one orientation sample that drive left in
 * `mafia_fp.log` has the camera's forward.y at -0.2510, i.e. aimed 14.5 degrees BELOW the
 * horizon. In a chase camera that is right - it looks down at the car. From the driver's seat
 * it puts the horizon near the top of the screen and the road fills the frame.
 *
 * + is UP, in degrees. Clamped to +-45: past that the trim stops being a trim, and a typo must
 * not be able to aim the camera at the sky.
 *
 * Applied like the level horizon and for the same reason - recomputed every frame from the
 * engine's OWN forward, never accumulated - so it cannot drift, applying it twice is applying
 * it once, and setting it back to 0 restores the engine's aim exactly.
 *
 * WHY THIS COULD NOT SIMPLY BE SET TO "THE ANGLE HE LIKED": he asked for exactly that, and the
 * log cannot answer it. `mafia_fp.log` writes the orientation ONCE per launch (a `rollLogged`
 * latch), so the drive that carried 23 of his F7 presses contains two identical samples and no
 * time series. That is an instrument defect, not missing data, and it is fixed below: every
 * verdict key now records the pitch that was live when he pressed it. */
static volatile LONG g_pitchDeg = 0;   /* + is up, degrees; 0 = the engine's own aim */
#define FP_PITCH_MAX 45

/* ---- WHICH VIEW WE ARE, 2026-08-06 ---------------------------------------------------------
 * After the first drive, right now you have replaced literally all the positions of the
 * original camera and made them the view from inside the car, and that will not do. You could replace one of the exterior views with a view from
 * inside the car, or just add one. That assessment is right, and the log agrees - `C` was pressed three
 * times at t+205706 ms of that drive looking for the stock views, and every one of them was ours.
 *
 * The game does not count views, it walks a static ring of descriptors at `0x638700` (17 entries,
 * stride 0x44, `+0x00` mode id and `+0x04` the next mode). The vehicle ring is
 *
 *     7 C Beh. Hellboy -> 8 C Behind Emeth -> 9 C Behind3 -> 12 C Chase -> 13 C In -> 14 C Over -> 7
 *
 * and **13 `C In` is already an inside-the-car view**: its handler at `0x5EFC91` averages the
 * car's own camera dummies and writes the result into the camera frame's local translation at
 * `frame+0x80`, then dirties `frame+0xAC` - which is exactly the state this detour sees on the
 * return. So nothing has to be added or taken away. We sit on the slot the game already reserves
 * for the view from inside, and the other five stay exactly as the game built them.
 *
 * The live mode is one chain off the global this file already reads:
 *
 *     camctl = [[0x63788C] + 0x24] + 0x4C
 *     camctl + 0x04 = the camera FRAME  (so the mod can prove it is looking at its own camera)
 *     camctl + 0x10 = the mode
 *
 * Two agents reached that independently, one from the input path and one from the render path.
 * `view_mode = 0` restores the old behaviour - every view is ours - because that is what every
 * measurement before today was taken on, and an A/B has to stay possible. */
static volatile LONG g_viewMode = 9;
#define FP_CAMCTL_GLOBAL 0x63788Cu

/* THE SLOT IS HIS TO PICK AT THE WHEEL, 2026-08-07. Binding to 13 was wrong and it cost him a
 * whole session: 13 "C In" is the game's BONNET view, so taking it over deleted the view he
 * actually wanted kept - "we still do not have the bonnet camera... the bonnet camera needs to come
 * back, and this camera, the one in the screenshot right now, needs to be removed". Which rear view he
 * meant cannot be read back out of the recording with any confidence, and guessing it again
 * would spend another drive on the same question.
 * So the mod stops guessing: `key_take_view` makes the view CURRENTLY on screen the mod's slot
 * and writes it into the ini immediately, exactly like every seat nudge. Press C to the view
 * you want replaced, press the key, and it is first person from that press onward. The slot it
 * previously held goes straight back to being the game's own.
 * The live mode is published here rather than read in the key loop, because the key loop has no
 * safe way to walk the camera chain - the render detour has already proved the frame identity. */
static volatile LONG g_liveMode = -1;

/* The camera's forward.y as raw float bits, refreshed every frame we touch WORLD. Read by the
   verdict keys so a "this is the one" press records where the camera was AIMED, not only where
   the seat was. -0.2510 was the whole of what the previous drive could say about aim. */
static volatile DWORD g_lastFwdY = 0;
#define CAM_PROJ_OFF 0x1A4u

/* The height cycle: five stops, four centimetres apart, added ON TOP of seat_up_cm. The base
   stays in the file because it is the guess that needs to be right first - a 16 cm cycle cannot
   rescue a base that is half a metre out, and pretending otherwise would make the key look
   broken when the base is what is wrong. */
#define FP_STOPS 5
#define FP_STOP_CM 4
static volatile LONG g_stopIdx = 0;
/* The clamp exists so a typo cannot put the camera in orbit. A refused value is logged, never
   silently corrected - the difference between a refused 900 and a camera that quietly
   sits at 150 is a support question nobody can answer. */
#define SEAT_MIN_CM (-100)
#define SEAT_MAX_CM  200

static char g_iniPath[MAX_PATH];

static void FpIniPath(void)
{
    DWORD n = GetModuleFileNameA(NULL, g_iniPath, MAX_PATH);
    if (!n || n >= MAX_PATH) { g_iniPath[0] = 0; return; }
    while (n && g_iniPath[n - 1] != '\\' && g_iniPath[n - 1] != '/') n--;
    g_iniPath[n] = 0;
    FpCat(g_iniPath, MAX_PATH, "mafia_fp.ini");
}

static LONG FpClampSeat(LONG v, const char *what)
{
    if (v < SEAT_MIN_CM || v > SEAT_MAX_CM) {
        FpLog(what, (DWORD)v, 0);
        FpLog("  ...is outside the accepted range and was REFUSED; the previous value stands",
              (DWORD)SEAT_MIN_CM, (DWORD)SEAT_MAX_CM);
        return 0x7FFFFFFF;                    /* sentinel: caller keeps what it had */
    }
    return v;
}

/* ---- THE LIVE SUBSET - the only keys re-read while the game is running ----------------------
 *
 * Added 2026-08-01 for the launcher's First person tab. `LTABS[LTAB_FPV].live` in
 * src/launcher/mod_state.h is 0 until this exists, because a tab that says "EVERY CHANGE HERE IS
 * LIVE" over a mod that read its file once at startup is a lie the window tells confidently.
 *
 * WHY A SUBSET AND NOT THE WHOLE FILE. Three of the keys in this file cannot be re-read by
 * construction, and re-reading them would be a bug rather than a feature:
 *   `route`        chooses which hooks got INSTALLED twenty seconds ago. Nothing at runtime can
 *                  un-install them, so a new value would only make the log disagree with reality.
 *   `window_w/h`   are consumed inside CreateDevice. The device exists.
 *   `anchor`, `basis_swap`, `trace_arm`, `ab_frames`, `pass_original`, `delta_pct` are not
 *                  preferences at all - the shipped ini says so in a fenced block - and one of
 *                  them alternates the substitution, which would make a live edit look like the
 *                  camera breaking.
 * So the live set is exactly the keys a PERSON tunes, which is also - and this is the useful part
 * - exactly the keys this mod already writes back itself with FpSaveKey, plus `fov_deg` and
 * `nudge_cm`, which have no key of their own. That gives one invariant worth keeping:
 *
 *   THE FILE AND THE RUNTIME ARE ALREADY IN SYNC, so a re-read is a no-op unless somebody else
 *   changed the file. Which is why the change reporting below is per key and per change: a nudge
 *   on the numpad writes the file, the next tick reads back what it just wrote, finds nothing
 *   different and says nothing. Without that, this would put a line a second in the log.
 *
 * `announce` is 0 for the load at startup, which prints its own summary a few lines down.
 */
static void FpLoadLive(int announce)
{
    LONG v;

    /* `enabled` is jointly owned with the VR project AND with the numpad toggle. Re-reading it is
       still right: the toggle writes the file on every press, so the file is what the player last
       asked for from either direction. */
    v = (LONG)GetPrivateProfileIntA("fp", "enabled", 0, g_iniPath);
    if (v != g_enabled) {
        if (announce) FpLog("mafia_fp.ini changed: enabled", (DWORD)g_enabled, (DWORD)v);
        g_enabled = v;
    }

    /* LIVE on purpose. The arithmetic is a very good first guess and his eye is the authority on
       the final look, so this is a key he tunes with the game running - and it is also the
       one-line revert (0) that lets an A/B fit inside a single run. */
    v = FpHudPctFromIni(g_iniPath);
    if (v != g_hudAspectPct) {
        if (announce) FpLog("mafia_fp.ini changed: hud_aspect_pct",
                            (DWORD)g_hudAspectPct, (DWORD)v);
        g_hudAspectPct = v;
        g_hudLogged = 0;        /* say the new factor once, the next time one is applied */
    }

    /* LIVE for the same reason, and this is the one that decides WHERE the squeezed pixels go -
       against the screen edges (1) or all of it about the centre (0). Both are one keystroke
       apart in the file, so the comparison is a pause rather than a second drive. */
    v = (LONG)GetPrivateProfileIntA("fp", "hud_aspect_anchored", 1, g_iniPath) ? 1 : 0;
    if (v != g_hudAspectAnchored) {
        if (announce) FpLog("mafia_fp.ini changed: hud_aspect_anchored",
                            (DWORD)g_hudAspectAnchored, (DWORD)v);
        g_hudAspectAnchored = v;
        g_hudLogged = 0;
    }

    /* LIVE, both of them, and they are the two halves of Alex's decision of 2026-08-14: correct
       the radar and the speedometer, nothing else, and only while sitting in a car. Live because
       the comparison is one keystroke and a pause rather than a second drive. */
    v = (LONG)GetPrivateProfileIntA("fp", "hud_zones", 1, g_iniPath) ? 1 : 0;
    if (v != g_hudZones) {
        if (announce) FpLog("mafia_fp.ini changed: hud_zones", (DWORD)g_hudZones, (DWORD)v);
        g_hudZones = v;
        g_hudLogged = 0;
    }
    v = (LONG)GetPrivateProfileIntA("fp", "hud_incar_only", 1, g_iniPath) ? 1 : 0;
    if (v != g_hudInCarOnly) {
        if (announce) FpLog("mafia_fp.ini changed: hud_incar_only",
                            (DWORD)g_hudInCarOnly, (DWORD)v);
        g_hudInCarOnly = v;
        g_hudLogged = 0;
    }

    /* LIVE so it can be armed at the moment that matters - in a car, with the map open - and
       disarmed again without ending the session. The row budget is reset on every ARM, so a second
       arm in the same run measures the second thing rather than finding the budget spent. */
    v = (LONG)GetPrivateProfileIntA("fp", "hud_probe", 0, g_iniPath) ? 1 : 0;
    if (v != g_hudProbe) {
        if (announce) FpLog("mafia_fp.ini changed: hud_probe", (DWORD)g_hudProbe, (DWORD)v);
        g_hudProbe = v;
        if (v) g_hudProbeRows = 0;
    }

    /* NOT live, and deliberately so. Whether this install has bench keys at all is a property of
       the install, not a setting to be flipped mid-session - and a key that appears halfway
       through a run is a key nobody can account for afterwards. Read once, at startup. */

    /* The clamp returns a sentinel rather than a value when the file is out of range, and the
       previous value stands - the same rule as at startup, so a typo in a slider's file cannot
       throw the camera across the map mid-drive. */
    v = FpClampSeat((LONG)GetPrivateProfileIntA("fp", "seat_up_cm", 0, g_iniPath), "seat_up_cm");
    if (v != 0x7FFFFFFF && v != g_seatUpCm) {
        if (announce) FpLog("mafia_fp.ini changed: seat_up_cm", (DWORD)g_seatUpCm, (DWORD)v);
        g_seatUpCm = v;
    }
    v = FpClampSeat((LONG)GetPrivateProfileIntA("fp", "seat_forward_cm", 0, g_iniPath),
                    "seat_forward_cm");
    if (v != 0x7FFFFFFF && v != g_seatFwCm) {
        if (announce) FpLog("mafia_fp.ini changed: seat_forward_cm", (DWORD)g_seatFwCm, (DWORD)v);
        g_seatFwCm = v;
    }
    v = FpClampSeat((LONG)GetPrivateProfileIntA("fp", "seat_side_cm", 0, g_iniPath),
                    "seat_side_cm");
    if (v != 0x7FFFFFFF && v != g_seatSideCm) {
        if (announce) FpLog("mafia_fp.ini changed: seat_side_cm", (DWORD)g_seatSideCm, (DWORD)v);
        g_seatSideCm = v;
    }

    v = (LONG)GetPrivateProfileIntA("fp", "height_stop", 0, g_iniPath);
    v = (v >= 0 && v < FP_STOPS) ? v : 0;
    if (v != g_stopIdx) {
        if (announce) FpLog("mafia_fp.ini changed: height_stop", (DWORD)g_stopIdx, (DWORD)v);
        g_stopIdx = v;
    }

    v = (LONG)GetPrivateProfileIntA("fp", "lock_roll", 1, g_iniPath) ? 1 : 0;
    if (v != g_lockRoll) {
        if (announce) FpLog("mafia_fp.ini changed: lock_roll", (DWORD)g_lockRoll, (DWORD)v);
        g_lockRoll = v;
    }

    /* GetPrivateProfileInt returns UINT - a negative default comes back as 0xFFFFFFFF, and this
       project has been bitten by that return type before. Every one of these is range-checked
       into a known-good value rather than trusted. */
    v = (LONG)GetPrivateProfileIntA("fp", "nudge_cm", 5, g_iniPath);
    v = (v >= 1 && v <= 50) ? v : 5;
    if (v != g_nudgeCm) {
        if (announce) FpLog("mafia_fp.ini changed: nudge_cm", (DWORD)g_nudgeCm, (DWORD)v);
        g_nudgeCm = v;
    }
    v = (LONG)GetPrivateProfileIntA("fp", "near_cm", 0, g_iniPath);
    v = (v >= 0 && v <= 500) ? v : 0;
    if (v != g_nearCm) {
        if (announce) FpLog("mafia_fp.ini changed: near_cm", (DWORD)g_nearCm, (DWORD)v);
        g_nearCm = v;
    }
    v = (LONG)GetPrivateProfileIntA("fp", "fov_deg", 0, g_iniPath);
    v = (v == 0 || (v >= 40 && v <= 150)) ? v : 0;
    if (v != g_fovDeg) {
        if (announce) FpLog("mafia_fp.ini changed: fov_deg", (DWORD)g_fovDeg, (DWORD)v);
        g_fovDeg = v;
    }
    /* Negative values DO survive this call, which is worth stating because the API returns UINT
       and the project has a memory about that. Proven on this very code path: `seat_side_cm`
       has been negative for a week and the launch banner logs it as 0xFFFFFFE7 = -25. */
    v = (LONG)GetPrivateProfileIntA("fp", "pitch_deg", 0, g_iniPath);
    v = (v >= -FP_PITCH_MAX && v <= FP_PITCH_MAX) ? v : 0;
    if (v != g_pitchDeg) {
        if (announce) FpLog("mafia_fp.ini changed: pitch_deg (+ is up)", (DWORD)g_pitchDeg, (DWORD)v);
        g_pitchDeg = v;
    }
    /* Only the six vehicle ring modes, or 0 for "every view". A number outside the ring would
       mean a camera that never appears however long he holds C, which looks exactly like a mod
       that failed to load. */
    /* The default moved 13 -> 9 on 2026-08-07. 13 "C In" is the game's BONNET view and taking
       it over deleted the view he wanted kept; 9 "C Behind3" is the third of three rear cameras
       that SHARE one handler (0x5EDE25) and differ only in ten tuning floats, so it is the
       cheapest of the six to give up. He can move it at the wheel with key_take_view. */
    v = (LONG)GetPrivateProfileIntA("fp", "view_mode", 9, g_iniPath);
    v = (v == 0 || v == 7 || v == 8 || v == 9 || v == 12 || v == 13 || v == 14) ? v : 9;
    if (v != g_viewMode) {
        if (announce) FpLog("mafia_fp.ini changed: view_mode (0 = every view)",
                            (DWORD)g_viewMode, (DWORD)v);
        g_viewMode = v;
    }

    /* ---- THE SIX SEAT KEYS, live as of 2026-08-07 --------------------------------------------
     * The request was for them to be bindable from the window: good to bind, for each
     * axis, two keys - on the wheel, bound to one knob and adjusted
     * conveniently during play.
     *
     * They were absent from this function, so a re-bind only took effect after a restart - which
     * defeats the point of binding them at all. The seat VALUES have been live since 2026-08-03;
     * the keys that move them were not, and nothing said so.
     *
     * 0 IS A VALID VALUE HERE and means "no key", which the nudge loop already honours by testing
     * `nvk[i] &&` before polling. So this deliberately does NOT reject 0 the way the seat clamps
     * reject an out-of-range centimetre: clearing a binding in the window has to reach the game
     * the same way setting one does. */
    {
        static const struct { const char *key; volatile LONG *var; LONG def; const char *msg; }
        SEATKEYS[6] = {
            { "key_seat_up",    &g_keySeatUp,    0x70, "mafia_fp.ini changed: key_seat_up"    },
            { "key_seat_down",  &g_keySeatDown,  0x71, "mafia_fp.ini changed: key_seat_down"  },
            { "key_seat_fwd",   &g_keySeatFwd,   0x72, "mafia_fp.ini changed: key_seat_fwd"   },
            { "key_seat_back",  &g_keySeatBack,  0x73, "mafia_fp.ini changed: key_seat_back"  },
            { "key_seat_left",  &g_keySeatLeft,  0x74, "mafia_fp.ini changed: key_seat_left"  },
            { "key_seat_right", &g_keySeatRight, 0x75, "mafia_fp.ini changed: key_seat_right" },
        };
        int k;
        for (k = 0; k < 6; k++) {
            LONG nv = (LONG)GetPrivateProfileIntA("fp", SEATKEYS[k].key,
                                                  SEATKEYS[k].def, g_iniPath);
            if (nv < 0 || nv > 0xFF) continue;      /* not a virtual key - leave the binding be */
            if (nv != *SEATKEYS[k].var) {
                if (announce) FpLog(SEATKEYS[k].msg, (DWORD)*SEATKEYS[k].var, (DWORD)nv);
                *SEATKEYS[k].var = nv;
            }
        }
    }
}

/* The file's last write time and size, as one number to compare against. Cheaper than reading
   thirty keys a second, and it is what keeps the 1 Hz tick a stat() rather than a parse. */
static DWORDLONG FpIniStamp(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!g_iniPath[0]) return 0;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &fa)) return 0;
    return (((DWORDLONG)fa.ftLastWriteTime.dwHighDateTime << 32)
            | (DWORDLONG)fa.ftLastWriteTime.dwLowDateTime)
           ^ (DWORDLONG)fa.nFileSizeLow;
}

static void FpLoadIni(void)
{
    if (!g_iniPath[0]) return;
    FpLoadLive(0);                 /* everything a person tunes; announces nothing at startup */
    /* THE NAV CLUSTER, and NOT the F row - and, since 2026-08-07, not the numpad either.
       These read VK_F11 / VK_F12 until 2026-07-28 and the run that broke the D3D8 seam proved
       the cost: an unattended replay that presses ENTER and the arrows toggled first person off,
       then cycled the height stop three times, then toggled it back on. Nothing in the replay
       touches F11 or F12, so something else in the process does. Moving them to the numpad
       fixed that and introduced a worse one: HIS KEYBOARD HAS NO NUMPAD, which he has said many
       times and which mafia_ffb_v6.c has honoured since July while this file did not. A control
       on a key that does not exist is not a default, it is a control nobody has.
       PAGE UP and PAGE DOWN, next to the INSERT/DELETE pair that already trims the pitch.
       Spelled as hex because this file builds -nostdlib and has no VK_ table. */
    /* BOTH DEFAULT TO 0 = NO KEY as of 2026-08-07, and that is a removal, not a regression.
       PAGE UP / PAGE DOWN / END belong to mafia_ffb.asi, which tunes the DETECTION gate live
       on them (g_gateFrac, and END resets it). Pressing END to dump the scene graph would have
       silently reset the detector's sensitivity in the middle of the drive that is measuring
       that detector. Found by auditing every binding in both modules after checking whether
       anything collides - it did, three ways.
       Rather than hunt for two more free keys, both controls go, because neither earns one:
         key_toggle  - `enabled` is re-read from this ini once a second, so the camera can be
                       switched off without a key at all;
         key_height  - the height cycle is 5 stops of 4 cm = 16 cm, all of it upward, on the
                       same axis F1/F2 already move by 2 cm a press across a 300 cm clamp. It
                       was a shortcut, and it is 5% of the travel the shortcut duplicates.
       Set either to a VK code here to bring it back. */
    g_keyToggle = (LONG)GetPrivateProfileIntA("fp", "key_toggle", 0, g_iniPath);
    g_keyHeight = (LONG)GetPrivateProfileIntA("fp", "key_height", 0, g_iniPath);
    g_keySeatUp    = (LONG)GetPrivateProfileIntA("fp", "key_seat_up",    0x70, g_iniPath);
    g_keySeatDown  = (LONG)GetPrivateProfileIntA("fp", "key_seat_down",  0x71, g_iniPath);
    g_keySeatFwd   = (LONG)GetPrivateProfileIntA("fp", "key_seat_fwd",   0x72, g_iniPath);
    g_keySeatBack  = (LONG)GetPrivateProfileIntA("fp", "key_seat_back",  0x73, g_iniPath);
    g_keySeatLeft  = (LONG)GetPrivateProfileIntA("fp", "key_seat_left",  0x74, g_iniPath);
    g_keySeatRight = (LONG)GetPrivateProfileIntA("fp", "key_seat_right", 0x75, g_iniPath);
    g_keyVerdictY  = (LONG)GetPrivateProfileIntA("fp", "key_verdict_yes", 0x76, g_iniPath);
    g_keyVerdictN  = (LONG)GetPrivateProfileIntA("fp", "key_verdict_no",  0x77, g_iniPath);
    g_keyNearUp    = (LONG)GetPrivateProfileIntA("fp", "key_near_out",    0x78, g_iniPath);
    g_keyNearDown  = (LONG)GetPrivateProfileIntA("fp", "key_near_in",     0x79, g_iniPath);
    /* v7.65: F11 FREED - the on/off toggle is no longer needed, not the horizon feature. A level
       horizon is what he wants everywhere ("when you are walking on foot the horizon should be
       perfect"), so it stays ON and stops being a thing to press. lock_roll in the ini still
       switches it for an A/B. */
    g_keyLockRoll  = (LONG)GetPrivateProfileIntA("fp", "key_lock_roll",   0, g_iniPath);
    /* v7.65: INSERT/DELETE RETIRED. Reported 2026-08-07: it is not entirely clear
       what tilt the camera has there, because we have already set the horizon anyway. You set
       the horizon to 0, and that is it.
       He is right that the two controls read as one thing and are not. `lock_roll` removes
       ROLL - the car tipping sideways no longer tips the picture. `pitch_deg` aims the camera
       up or down, which roll has nothing to do with. The trim existed because the engine aims
       the vehicle camera 14.5 degrees below the horizon; binding to a proper in-car view slot
       removed most of that, so the trim now earns neither a key nor a default. Both keys read
       0 = no key, and pitch_deg goes to 0. A VK code here brings the pair back. */
    g_keyPitchUp   = (LONG)GetPrivateProfileIntA("fp", "key_pitch_up",    0, g_iniPath);
    g_keyPitchDown = (LONG)GetPrivateProfileIntA("fp", "key_pitch_down",  0, g_iniPath);
    g_keyTakeView  = (LONG)GetPrivateProfileIntA("fp", "key_take_view",   0x24, g_iniPath);
    /* THE SCENE PROBE, 2026-08-04. Startup-only on purpose: it is an instrument, not a setting,
       and a dump that could start mid-drive from a file watcher is a dump nobody asked for.
       `key_dump` is on the NUMPAD (VK_SUBTRACT) - the F row is spoken for twice over, by this
       file's own seat keys and by the FFB module's F7/F8 markers. */
    g_dumpFrames = (LONG)GetPrivateProfileIntA("fp", "dump_frames", 0, g_iniPath) ? 1 : 0;
    /* F12, and it is the LAST free key on this keyboard. END was wrong twice over: no numpad
       is why it left the numpad, and mafia_ffb.asi's detection-gate reset is why it cannot be
       END either. The full audit is in docs\KEY-MAP.md - nothing else is unclaimed. */
    g_keyDump    = (LONG)GetPrivateProfileIntA("fp", "key_dump", 0x7B, g_iniPath);
    GetPrivateProfileStringA("fp", "hide_frame", "", g_hideName, sizeof(g_hideName), g_iniPath);
    if (g_dumpFrames || g_hideName[0])
        FpLog("scene probe armed - dump_frames, hide_frame set",
              (DWORD)g_dumpFrames, (DWORD)(g_hideName[0] ? 1 : 0));
    /* `lock_roll`, `nudge_cm`, `near_cm` and `fov_deg` are read by FpLoadLive above - they are
       part of the live subset, and reading them twice with two copies of their range checks is how
       the two copies stop agreeing. The KEY BINDINGS stay here, startup only: they are read once
       into the poll loop's own variables and a live rebind is not something anything asks for. */
    /* See the control at the substitution. 0 = write the row but change nothing, which is the
       only setting that can tell a bad write apart from bad arithmetic. Clamped rather than
       trusted: a typo here is a camera thrown across the map. */
    {
        LONG dp = (LONG)GetPrivateProfileIntA("fp", "delta_pct", 0, g_iniPath);
        g_deltaPct = (dp < -200 || dp > 200) ? 0 : dp;
    }
    /* Diagnostic only, off by default: a windowed game can be photographed, a fullscreen
       exclusive one cannot. Never shipped on - it changes how the game presents. */
    /* 0 = off, which is the shipping state. The alternation exists to compare a substituted
       picture against an unsubstituted one inside a single run; while it is on, nothing else
       about the picture can be judged, because it changes every couple of seconds. */
    g_abFrames = (LONG)GetPrivateProfileIntA("fp", "ab_frames", 0, g_iniPath);
    g_passOriginal = (LONG)GetPrivateProfileIntA("fp", "pass_original", 0, g_iniPath);
    g_winW = (LONG)GetPrivateProfileIntA("fp", "window_w", 0, g_iniPath);
    g_winH = (LONG)GetPrivateProfileIntA("fp", "window_h", 0, g_iniPath);
    {
        LONG rt = (LONG)GetPrivateProfileIntA("fp", "route", 1, g_iniPath);
        /* Anything that is not a route we have is route 1, and it is LOGGED as a correction
           rather than accepted quietly - a typo that silently selects the old path would be read
           as a false report that route 2 does not work. */
        if (rt != 1 && rt != 2) {
            FpLog("route is not 1 or 2 - falling back to route 1. what the file said", (DWORD)rt, 0);
            rt = 1;
        }
        g_route = rt;
    }
    {
        LONG ta = (LONG)GetPrivateProfileIntA("fp", "trace_arm", 2, g_iniPath);
        g_traceArm = (ta == 1 || ta == 2) ? ta : 2;
        LONG am = (LONG)GetPrivateProfileIntA("fp", "anchor", 1, g_iniPath);
        g_anchorMode = (am == 0 || am == 1) ? am : 1;
LONG bs = (LONG)GetPrivateProfileIntA("fp", "basis_swap", 1, g_iniPath);
g_basisSwap = (bs == 0) ? 0 : 1;
    }
    FpLog("settings loaded: enabled, seat up cm", (DWORD)g_enabled, (DWORD)g_seatUpCm);
    FpLog("  route - 1 is the D3D8 SetTransform seam, 2 is UpdateWMatrixProc", (DWORD)g_route, 0);
    FpLog("  trace_arm - 1 opens the trace on 5 m driven, 2 on the up arrow HELD",
          (DWORD)g_traceArm, 0);
    FpLog("  anchor - 0 is car+0x2A10, 1 is the car's RENDER frame", (DWORD)g_anchorMode, 0);
FpLog("  basis_swap - 1 puts the basis in the position's convention", (DWORD)g_basisSwap, 0);
    FpLog("  window_w, window_h - 0,0 means leave it fullscreen", (DWORD)g_winW, (DWORD)g_winH);
    FpLog("  seat forward cm, height stop", (DWORD)g_seatFwCm, (DWORD)g_stopIdx);
    FpLog("  seat side cm (+ right, - left)", (DWORD)g_seatSideCm, 0);
    FpLog("  near_cm, fov_deg - 0 means leave the engine's", (DWORD)g_nearCm, (DWORD)g_fovDeg);
    FpLog("  delta_pct - 0 means substitute but change nothing", (DWORD)g_deltaPct, 0);
    FpLog("  keys toggle/height (virtual-key codes)", (DWORD)g_keyToggle, (DWORD)g_keyHeight);
    FpLog("  hud_aspect_pct - -1 auto, 0 off, 25..200 explicit", (DWORD)g_hudAspectPct, 0);
    FpLog("  hud_aspect_anchored - 1 to the screen edges, 0 about the centre",
          (DWORD)g_hudAspectAnchored, 0);
    g_devKeys = (LONG)GetPrivateProfileIntA("fp", "dev_keys", 0, g_iniPath) ? 1 : 0;
    FpLog("  dev_keys - INSERT flips the anchoring, PAGE DOWN measures", (DWORD)g_devKeys, 0);
    if (g_devKeys)
        FpLog("  bench keys are LIVE on this install. PAGE DOWN needs diag = 1 to write anything, "
              "and diag is", 1, 0);   /* this line only exists at all when the log is open */
}

/* Decimal into `v`, no CRT. */
static void FpNum(char *v, int cap, LONG n)
{
    v[0] = 0;
    if (n < 0) { FpCat(v, cap, "-"); n = -n; }
    char d[12]; int i = 0;
    if (!n) d[i++] = '0';
    while (n) { d[i++] = (char)('0' + (n % 10)); n /= 10; }
    char out[16]; int j = 0;
    while (i) out[j++] = d[--i];
    out[j] = 0;
    FpCat(v, cap, out);
}

/* ONE key at a time, in place, leaving every other line of the file exactly as the user left
   it. That rule is the gearbox binder's scar: it rewrote its whole built-in table on every save
   and destroyed a hand-maintained config twice. */
static void FpSaveKey(const char *key, LONG value)
{
    if (!g_iniPath[0]) return;
    char v[16];
    FpNum(v, sizeof(v), value);
    WritePrivateProfileStringA("fp", key, v, g_iniPath);
}

static Direct3DCreate8_t g_realCreate8   = NULL;
static CreateDevice_t    g_realCreateDev = NULL;
static SetTransform_t    g_realSetXform  = NULL;
static Present_t         g_realPresent   = NULL;
static DrawIndexedPrim_t g_realDraw      = NULL;
static void             *g_realBeginScene = NULL;
static void             *g_device         = NULL;   /* kept only to re-read its vtable */
static void             *g_realDrawAlt[3];
static SetVertexShader_t g_realSetVS     = NULL;
static SetVSConstant_t   g_realSetVSC    = NULL;

/* Route 2's three counters live up here with the other per-frame ones rather than beside the
   detour that increments them, because the frame report in FpWatchSlots reads them and sits
   above it. Three and not one: never fired, fired but never on the camera frame, and fired
   on the camera and refused to write are three different bugs with three different owners. */
static volatile LONG     g_updwCalls     = 0;   /* how often the detour ran at all */
static volatile LONG     g_updwCam       = 0;   /* ...of those, how often it was the CAMERA */
static volatile LONG     g_updwWrites    = 0;   /* ...of those, how often we actually wrote */

static volatile LONG     g_drawCalls     = 0;
static volatile LONG     g_sceneCalls    = 0;
static volatile LONG     g_drawAlt[3];      /* DrawPrimitive, DrawPrimitiveUP, DrawIndexedPrimUP */
static volatile LONG     g_vsCalls       = 0;
static volatile LONG     g_vscCalls      = 0;
static volatile LONG     g_vscFirstReg   = 0xFFFFFFFF;
static volatile LONG     g_vscMaxReg     = 0;

/* THE SLOT-INTEGRITY WATCH, 2026-07-28. The run before this one proved the write STICKS at
   install (the read-back passed and logged "SetTransform hooked") and is GONE by frame 1000:
   slot 37 held d3d8's original again while slot 71 still held ours, on the same object and the
   same vtable. So the question is no longer whether the engine calls it - it is WHO PUTS THE
   ORIGINAL BACK, and WHEN. A count at frame 1000 cannot answer that; only the frame NUMBER of
   the first reversion can, because a single reversion during the mission load and one every
   frame from something that re-asserts itself are different bugs with different owners. */
static void            **g_vtable        = NULL;  /* the vtable as it was when we patched it */
static void             *g_engineDevPrev = NULL;  /* LS3DF's device global, to catch a swap */
static volatile LONG     g_rehooks       = 0;     /* how often slot 37 had to be put back */
/* 1 only when slot 37 was ours to take. When another module owns it we leave it alone AND stop
   the watch below from re-asserting ours: doing that with g_realSetXform still NULL would put a
   detour in the vtable with nothing to chain to, i.e. turn a clean refusal into a crash. */
static int               g_xformOwned    = 0;
static LONG              g_xformAtRehook = -1;    /* SetTransform count at the first re-hook */
static int               g_vtSwapLogged  = 0;

/* BeginScene takes no arguments beyond `this`, so a wrong index here is harmless: worst case we
   pass a call through to something that ignores its stack. It is the decisive probe - a 3D frame
   that never calls BeginScene is not a 3D frame, so a zero here means the index BASE is wrong
   rather than the engine being unusual. */
static HRESULT WINAPI FpBeginScene(void *self)
{
    g_sceneCalls++;
    return ((HRESULT (WINAPI *)(void *))g_realBeginScene)(self);
}

/* ---- THE HUD SQUEEZE ------------------------------------------------------------------------
 *
 * Why it lives on the draw hooks and not on a matrix: the interface is PRE-TRANSFORMED. Its
 * vertices are already in screen space, so no matrix the engine sets can reach them - the only
 * place the coordinates exist is the buffer handed to DrawPrimitiveUP. See ..\shared\hud_aspect.h
 * for the defect and the factor.
 *
 * NOT gated on `enabled`. The stretch is there in third person and on foot exactly as it is in
 * the driver's seat - the requirement is at any time, in a car and outside one alike - and it
 * is `enabled` alone that chooses whether the CAMERA is substituted. The two are
 * independent and must stay independent, or switching the camera off would silently bring the
 * ellipse back.
 *
 * Single-threaded by construction: these are D3D8 device methods, called from the thread that
 * owns the device, which is why one static scratch buffer is enough.
 */
static volatile DWORD    g_curVS        = 0;     /* the last SetVertexShader handle */
/* WHY EVERY REJECTION HAS ITS OWN COUNTER, 2026-08-14. The run of that morning armed
   `hud_probe = 1`, reached the city, and wrote NOT ONE probe row - and there was no way to tell
   from the log whether the hook was never called, whether every draw was rejected as not
   screen-space, or whether the factor came out as 1. Three different faults, one silence.
   An instrument that cannot say "0 hits, and here is where they were lost" measures nothing;
   these are reported by the city-anchored block in Present below. */
static volatile LONG     g_hudCalls     = 0;     /* entries into FpHudSqueeze, whatever happened */
static volatile LONG     g_hudNotSS     = 0;     /* ...rejected: the FVF is not pre-transformed  */
static volatile LONG     g_hudNoVS      = 0;     /* ...rejected: GetVertexShader itself failed   */
static volatile LONG     g_hudNotInCar  = 0;     /* ...rejected: on foot or in a menu            */
static volatile LONG     g_hudOutOfZone = 0;     /* ...rejected: not the radar and not the dial  */
static volatile LONG     g_hudOffScreen = 0;     /* ...rejected: drawn into a render target      */
static volatile LONG     g_hudNoDial    = 0;     /* ...rejected: the driving HUD is not on screen */
static volatile LONG     g_dialFrame    = -1000; /* the frame the speedometer ring was last seen */
static volatile LONG     g_hudMaxW = 0, g_hudMaxH = 0;  /* the widest surface seen = the screen  */
static volatile LONG     g_inCar        = 0;     /* 1 while the car anchor resolves every frame  */
static volatile LONG     g_inCarFrame   = -1000; /* the frame it was last CONFIRMED on           */
/* Declared here rather than used from further down: the frame counter lives with the Present hook,
   which is defined after the drawing code that has to ask how old the confirmation is. */
static volatile LONG     g_frames;
static volatile LONG     g_hudZoneRadar = 0;     /* corrected as the radar's corner              */
static volatile LONG     g_hudZoneDial  = 0;     /* corrected as the speedometer's corner        */
static float             g_radarCx = 0.0f, g_radarSpan = 0.0f;  /* the ring's centre and width  */
static float             g_dialCx  = 0.0f, g_dialSpan  = 0.0f;  /* the dial's centre and width  */
static volatile LONG     g_ssIndexed    = 0;     /* screen-space draws coming from a vertex
                                                    BUFFER - the blips we cannot reach. This
                                                    counter is the measurement behind the
                                                    "the traffic dots are not ours to move"
                                                    claim, rather than an inference from a
                                                    picture. */
static volatile LONG     g_hudNoSurf    = 0;     /* ...rejected: GetViewport gave no plausible screen */
static volatile LONG     g_hudUnity     = 0;     /* ...rejected: the factor is 1, nothing to do  */
static volatile LONG     g_hudBadArgs   = 0;     /* ...rejected: no verts, or a stride under 16  */
static volatile LONG     g_hudDraws     = 0;     /* screen-space draws we rewrote */
static volatile LONG     g_hudTooBig    = 0;     /* ...and ones that did not fit the scratch */
static volatile LONG     g_hudFullScreen= 0;     /* ...and ones deliberately left alone */
static volatile LONG     g_hudLeft      = 0;     /* rewritten about the LEFT edge of the screen */
static volatile LONG     g_hudRight     = 0;     /* ...the right edge */
static volatile LONG     g_hudCentre    = 0;     /* ...and ones that cross the middle */
static float             g_hudK         = 1.0f;  /* last factor used, for the log line */
static volatile LONG     g_hudW = 0, g_hudH = 0; /* the surface the last factor was worked out on */
static BYTE              g_hudScratch[64 * 1024];

/* The surface the interface is being spread across. GetViewport rather than the backbuffer
   description because a viewport is what screen-space coordinates are actually relative to, and
   because it is one call with no COM object to release. Range-checked: if slot 41 is not
   GetViewport on some build, the numbers will not be a plausible screen and we do nothing. */
static int FpHudSurface(void *self, int *outW, int *outH)
{
    struct { DWORD X, Y, Width, Height; float MinZ, MaxZ; } vp;
    void **vt = *(void ***)self;
    HRESULT hr = ((HRESULT (WINAPI *)(void *, void *))vt[IDIRECT3DDEVICE8_GETVIEWPORT])(self, &vp);
    if (hr != 0) return 0;
    if (vp.Width < 16u || vp.Width > 16384u || vp.Height < 16u || vp.Height > 16384u) return 0;
    *outW = (int)vp.Width;
    *outH = (int)vp.Height;
    return 1;
}

/* A pre-transformed draw is one whose FVF carries the RHW bit. A handle above 0xFFFF is a real
   vertex shader, not an FVF, and deciding it from the low bits instead classified the same
   handle differently from run to run - that mistake cost 801011 draws of 801011. */
/* ASK THE DEVICE, DO NOT CACHE THE LAST SetVertexShader. Measured on the reference drive of
   2026-08-14, in the city, over 1200 frames: 58433 draws reached this test, ALL 58433 were
   rejected as not screen-space, and `SetVertexShader` had been called exactly ZERO times. The
   hook on slot 76 installed without error - it is simply never used. Mafia sets the FVF some
   other way (a captured state block applies device state without going through the public
   method), so the cached handle was 0 for the whole run and every draw looked three-dimensional.
   GetVertexShader returns whatever is actually current, whoever set it. One call per candidate
   draw, ~50 per frame, against a hook that was answering a question it could not see. */
static int FpHudIsScreenSpace(void *self)
{
    DWORD h = 0;
    void **vt = *(void ***)self;
    HRESULT hr = ((HRESULT (WINAPI *)(void *, DWORD *))vt[IDIRECT3DDEVICE8_GETVERTEXSHADER])(self, &h);
    if (hr != 0) { g_hudNoVS++; return 0; }
    g_curVS = h;                      /* still recorded, so the log can show what was current */
    if (h > 0xFFFFu) return 0;        /* a real vertex shader handle, not an FVF */
    return (h & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
}

/* Rewrite X about the centre of the surface, in place in the scratch copy. Y, Z, RHW and every
   byte after them are untouched: a uniform scale cannot fix a shape, it only makes a smaller
   ellipse. Returns the scratch pointer, or NULL to mean the original should pass through. */
static const void *FpHudSqueeze(void *self, const void *verts, UINT count, UINT stride)
{
    int w = 0, h = 0;
    if (g_hudAspectPct == 0) return NULL;
    /* ONLY WHILE DRIVING. Everything drawn on foot, in a cutscene or in a menu is left exactly as
       the game drew it - which is what Alex asked for after the first working run, and it is also
       the honest scope: the two elements this correction is FOR only exist in a car. */
    /* CONFIRMED WITHIN THE LAST FEW FRAMES, not "confirmed once". Eight frames is under a sixth
       of a second at 60 fps and several frames at the menu's much higher rate, so a real drive
       never flickers off, while leaving the car turns the correction off almost immediately. */
    if (g_hudInCarOnly && (g_frames - g_inCarFrame) > 8) { g_hudNotInCar++; return NULL; }

    /* AND THE DRIVING HUD MUST ACTUALLY BE ON SCREEN.
     *
     * Alex, 2026-08-14, twice: pressing Escape squeezed the pause-menu entries - "nearly every
     * caption is squeezed, but the bottom one is not". Being in a menu does not remove the car, so the
     * in-car test above stays true and correctly so. Filtering by SIZE does not work either,
     * which is what that sentence describes: the narrow entries pass a width limit and the wide
     * one does not, so the menu comes out ragged - worse than either extreme.
     *
     * What separates the two states cleanly is the instrument itself. The speedometer's outer
     * ring is drawn every frame the driving HUD is up, and it is not drawn when a menu replaces
     * it. So: nothing is corrected unless that ring was seen within the last few frames. The menu
     * then needs no detection of its own - it is simply a place where the dial is absent.
     *
     * Three frames of slack, because the ring and the elements around it are separate draws
     * within one frame and the ring is not necessarily first.
     *
     * The test itself is BELOW, after the zone is known - the ring has to be able to announce
     * itself, and a test placed before the classification would refuse the very draw that proves
     * the HUD is up. */
    g_hudCalls++;
    if (!verts || stride < 16u || count == 0u) { g_hudBadArgs++; return NULL; }
    if (!FpHudIsScreenSpace(self)) { g_hudNotSS++; return NULL; }
    if (!FpHudSurface(self, &w, &h)) { g_hudNoSurf++; return NULL; }

    /* ONLY THE SCREEN, NEVER A RENDER TARGET. Found in the probe on 2026-08-14: a two-pixel draw
       at x=241, y=179 - the TOP LEFT of the screen - was being classified as the speedometer.
       It could only happen because the viewport at that moment was 341x255, not 1920x1080: the
       engine draws part of the interface into a small texture first, and inside it our fractions
       of "the screen" point somewhere else entirely. That small surface is also almost exactly
       4:3, so it needs no correction at all - and whatever is drawn into it (the radar's contents
       being the obvious candidate) is composited onto the screen afterwards by a draw we already
       handle.
       The main surface is the widest one this session has seen; anything narrower is a render
       target and is passed through untouched. */
    if (w > g_hudMaxW) { g_hudMaxW = w; g_hudMaxH = h; }
    if (w * 10 < g_hudMaxW * 9) { g_hudOffScreen++; return NULL; }

    float k = hud_aspect_factor((int)g_hudAspectPct, w, h);
    g_hudK = k;
    g_hudW = w; g_hudH = h;
    if (k > 0.9995f && k < 1.0005f) { g_hudUnity++; return NULL; }   /* 4:3, or pct 100 */

    DWORD bytes = (DWORD)count * (DWORD)stride;
    if (bytes > sizeof(g_hudScratch)) { g_hudTooBig++; return NULL; }

    /* LEAVE FULL-SCREEN DRAWS ALONE - unlike the approach used on another build, where the application
       paints the interface onto a quad, so the question does not arise there.
       On a monitor it does: the menu background, a loading screen and a fade-to-black are all
       screen-space draws that span the whole width, and squeezing one of those does not
       un-stretch a shape - it pillarboxes the picture, or leaves the edges of a fade unfaded.
       Both read as the mod breaking the game. What was reported is round things coming out
       oval, and those are all SMALL elements. So: measure the span first, and if this draw
       covers essentially the whole surface, pass it through untouched. */
    int anchor, zone = HUD_ZONE_NONE;
    float zoneCx = 0.0f;
    {
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
        for (DWORD i = 0; i < count; i++) {
            const BYTE *v = (const BYTE *)verts + (DWORD)i * stride;
            float x = *(const float *)v;
            float y = *(const float *)(v + 4u);
            if (i == 0) { minX = maxX = x; minY = maxY = y; }
            else {
                if (x < minX) minX = x; else if (x > maxX) maxX = x;
                if (y < minY) minY = y; else if (y > maxY) maxY = y;
            }
        }
        if ((maxX - minX) >= 0.95f * (float)w) { g_hudFullScreen++; return NULL; }

        /* ---- hud_probe: WHERE THE INTERFACE ACTUALLY IS -------------------------------------
         * Asked for on 2026-08-12, about the anchoring: find an X axis on screen where the
         * interface does not cross, and inject the pixels there. The seam should be
         * found rather than assumed. The thing that decides it is whether mission text is drawn
         * as ONE primitive per line or one per GLYPH: with one per line, a seam at the middle is
         * free; with one per glyph, a seam anywhere inside a sentence tears it.
         *
         * That is a measurement, not an opinion, so this logs the span of every screen-space draw
         * and the counts fall out of the log: a line of text is one wide row, per-glyph text is a
         * run of narrow rows at the same Y.
         *
         * Bounded at 600 rows. An instrument that fills the disk during the drive it was armed
         * for measures nothing, and this one is armed by hand for one short session.
         */
        if (g_hudProbe && g_hudProbeRows < 600) {
            g_hudProbeRows++;
            FpLog("hud probe: x from, to", (DWORD)(LONG)minX, (DWORD)(LONG)maxX);
            FpLog("           y from, to", (DWORD)(LONG)minY, (DWORD)(LONG)maxY);
            FpLog("           verts, stride", (DWORD)count, (DWORD)stride);
            FpLog("           zone (1 radar, 2 dial, 0 neither), in car",
                  (DWORD)hud_zone_of(minX, maxX, minY, maxY, w, h), (DWORD)g_inCar);
        }

        /* THE ZONE TEST - his decision of 2026-08-14, and the reason the rest of the interface
           is left alone. A draw is corrected only if it sits ENTIRELY in the radar's corner or
           the speedometer's. `hud_zones = 0` restores the old behaviour of correcting every
           screen-space draw, which is what broke the mission text and the menu. */
        if (g_hudZones) {
            zone = hud_zone_of(minX, maxX, minY, maxY, w, h);
            if (zone == HUD_ZONE_NONE) { g_hudOutOfZone++; return NULL; }
            /* THE RING ANNOUNCES THAT THE DRIVING HUD IS UP - see the note further above. Only
               the wide outer ring counts, not every small part of the dial, so that a stray draw
               in that corner cannot switch the correction on for a menu. */
            if (zone == HUD_ZONE_DIAL && (maxX - minX) > 0.25f * (float)w)
                g_dialFrame = g_frames;
            /* ...and nothing is corrected in a frame where it was not drawn. This is what keeps
               the pause menu out: the menu replaces the HUD, so the ring stops appearing.
               ONE frame of slack, not three. Alex, 2026-08-14: pressing Escape showed the entries
               squeezed for a fraction of a second before they snapped back. That delay was this
               window - three frames of "the HUD was up recently" at ~60 fps is ~50 ms of wrong
               picture. One frame is the least that can work: the ring and the radar are separate
               draws within a frame and the ring is not necessarily first, so demanding it in THIS
               frame would drop the radar on every frame the order flips. */
            if ((g_frames - g_dialFrame) > 1) { g_hudNoDial++; return NULL; }
            if (zone == HUD_ZONE_RADAR) g_hudZoneRadar++; else g_hudZoneDial++;
        }

        /* WHICH EDGE THIS DRAW BELONGS TO. The span is already measured for the guard above, so
           the classification is free - and it is the span of THIS draw, never a cached flag.
           Classifying a screen-space draw from one remembered bit is what cost 801011
           draws of 801011. */
        anchor = hud_aspect_anchor(minX, maxX, w);
        if      (anchor == HUD_ANCHOR_LEFT)  g_hudLeft++;
        else if (anchor == HUD_ANCHOR_RIGHT) g_hudRight++;
        else                                 g_hudCentre++;
        /* EACH ZONE GETS THE ANCHOR ITS CONTENTS NEED, and the two are different - which is not
           a compromise, it is what the two elements are.

           THE RADAR is squeezed about its OWN CENTRE. The traffic blips inside it are drawn from
           a vertex buffer and cannot be moved by this module, so the ring has to stay where they
           are; pulling it to the screen edge is what put them off to the right.

           THE SPEEDOMETER is squeezed to the screen CORNER, which is what it did before and what
           Alex confirmed looked right: *"you squeezed the speedometer well when you just squeezed
           it into the corner"*. It is four separate draws - ring, face, gear box, small gauge - and a
           common anchor at the screen edge keeps their positions relative to each other exactly
           as the game drew them. Nothing is drawn inside it by anyone else, so there is nothing
           for it to stay aligned with. */
        if (g_hudZones && zone == HUD_ZONE_RADAR) {
            float span = maxX - minX, mid = (minX + maxX) * 0.5f;
            if (span > g_radarSpan) { g_radarSpan = span; g_radarCx = mid; }
            zoneCx = g_radarCx;
        }
    }

    FpMemCpy(g_hudScratch, verts, bytes);
    for (DWORD i = 0; i < count; i++) {
        float *x = (float *)(g_hudScratch + (DWORD)i * stride);
        /* In zone mode the element keeps its place and only changes width: x is mapped about the
           zone's own centre. The screen-edge mapping is what pulled the radar away from the blips
           the engine draws inside it, and those blips are not ours to move. */
        /* The radar keeps its own centre; everything else - the speedometer included - is pulled
           to the screen edge it belongs to, which is what it did before and what looked right.
           THE BUG THIS REPLACES: `g_hudZones ? about(zoneCx) : edge()` sent the dial through the
           centre path with zoneCx left at 0.0, i.e. squeezed it toward x=0. Alex saw it sitting
           near the middle of the screen instead of in its corner. A default of zero that means
           "the left edge of the screen" is not a safe default for a value that may not be set. */
        *x = (g_hudZones && zone == HUD_ZONE_RADAR)
                 ? hud_map_x_about(*x, zoneCx, k)
                 : hud_aspect_map_x(*x, anchor, (int)g_hudAspectAnchored, w, k);
    }
    g_hudDraws++;
    if (!g_hudLogged) {
        g_hudLogged = 1;
        FpLog("HUD squeeze active - surface w, h", (DWORD)w, (DWORD)h);
        FpLog("  factor x1000, pct setting", (DWORD)(LONG)(k * 1000.0f), (DWORD)g_hudAspectPct);
        FpLog("  edge anchored (1) or centred (0)", (DWORD)g_hudAspectAnchored, 0);
    }
    return g_hudScratch;
}

/* How many vertices a non-indexed primitive count means. Anything unrecognised returns 0, which
   makes the caller pass the draw through untouched rather than guess a length and walk off the
   end of the engine's buffer. */
static UINT FpHudVertCount(DWORD primType, UINT primCount)
{
    switch (primType) {
        case 1: return primCount;          /* POINTLIST     */
        case 2: return primCount * 2u;     /* LINELIST      */
        case 3: return primCount + 1u;     /* LINESTRIP     */
        case 4: return primCount * 3u;     /* TRIANGLELIST  */
        case 5: return primCount + 2u;     /* TRIANGLESTRIP */
        case 6: return primCount + 2u;     /* TRIANGLEFAN   */
        default: return 0u;
    }
}

static HRESULT WINAPI FpDrawPrim(void *self, DWORD a, UINT b, UINT c)
{
    g_drawAlt[0]++;
    return ((HRESULT (WINAPI *)(void *, DWORD, UINT, UINT))g_realDrawAlt[0])(self, a, b, c);
}
static HRESULT WINAPI FpDrawPrimUP(void *self, DWORD a, UINT b, const void *c, UINT d)
{
    g_drawAlt[1]++;
    UINT n = FpHudVertCount(a, b);
    if (n) {
        const void *fixed = FpHudSqueeze(self, c, n, d);
        if (fixed) c = fixed;
    }
    return ((HRESULT (WINAPI *)(void *, DWORD, UINT, const void *, UINT))
            g_realDrawAlt[1])(self, a, b, c, d);
}
static HRESULT WINAPI FpDrawIdxPrimUP(void *self, DWORD a, UINT b, UINT c, UINT d,
                                      const void *e, DWORD f, const void *g, UINT h)
{
    g_drawAlt[2]++;
    /* `b` is MinVertexIndex and `c` is NumVertices, but the pointer is the base of vertex 0 and
       the indices are relative to it - so the copy has to start at 0 and cover b + c vertices,
       or the indices would address the wrong rows. Only the used range is rewritten. */
    if (h >= 16u && g) {
        UINT total = b + c;
        const void *fixed = FpHudSqueeze(self, g, total, h);
        if (fixed) {
            /* undo the rewrite on the vertices below MinVertexIndex - they were copied for the
               indices' sake and this draw does not claim to use them */
            if (b) FpMemCpy(g_hudScratch, g, (DWORD)b * (DWORD)h);
            g = fixed;
        }
    }
    return ((HRESULT (WINAPI *)(void *, DWORD, UINT, UINT, UINT, const void *, DWORD,
                                const void *, UINT))
            g_realDrawAlt[2])(self, a, b, c, d, e, f, g, h);
}

static HRESULT WINAPI FpDraw(void *self, DWORD type, UINT minIdx, UINT numV,
                             UINT startIdx, UINT primCount)
{
    g_drawCalls++;
    /* COUNT the screen-space ones. This path draws from a vertex BUFFER, so the vertices are on
       the D3D side and this module cannot rewrite them the way it rewrites the ...UP calls.
       Everything flat that comes through here is therefore an interface element the correction
       physically cannot touch - the traffic blips inside the radar being the one that shows.
       Counting them turns that from an explanation into a number. */
    if (g_hudAspectPct != 0 && (g_frames - g_inCarFrame) <= 8 && FpHudIsScreenSpace(self))
        g_ssIndexed++;
    return g_realDraw(self, type, minIdx, numV, startIdx, primCount);
}

/* handle 0 means the FIXED FUNCTION pipeline with an FVF; anything else is a real vertex
   shader. Which of those this engine uses decides where a view matrix can possibly live. */
static HRESULT WINAPI FpSetVS(void *self, DWORD handle)
{
    g_vsCalls++;
    g_curVS = handle;           /* the HUD squeeze needs to know if the next draw is screen space */
    if (g_vsCalls <= 3) FpLog("SetVertexShader - handle, call", handle, (DWORD)g_vsCalls);
    return g_realSetVS(self, handle);
}

/* If the view matrix travels as shader constants, it is four consecutive registers written
   every frame. The register NUMBERS are the lead worth capturing. */
static HRESULT WINAPI FpSetVSC(void *self, DWORD reg, const void *data, DWORD count)
{
    g_vscCalls++;
    if ((LONG)reg < g_vscFirstReg) g_vscFirstReg = (LONG)reg;
    if ((LONG)(reg + count) > g_vscMaxReg) g_vscMaxReg = (LONG)(reg + count);
    return g_realSetVSC(self, reg, data, count);
}
static void            **g_iatSlot       = NULL;
static int               g_viewSeen      = 0;
static int               g_subLogged     = 0;
static int               g_farLogged     = 0;
static volatile LONG     g_xformCalls    = 0;
static BYTE              g_stateLogged[32];
static volatile LONG     g_frames        = 0;   /* tentative definition above; this one initialises */

/* A LIVENESS CONTROL, not a feature. Present is called once per rendered frame by every D3D8
   program that exists, so if this fires and SetTransform does not, the vtable patch is good and
   the engine is setting the view somewhere we are not looking. If neither fires, the patch is
   the problem. One log line at the first frame and one at the thousandth is enough to tell
   those apart, and it costs an increment per frame. */
/* Runs once per frame, four pointer compares. Reports the FIRST frame on which each watched slot
   stops being ours, and puts slot 37 back so the run also answers the follow-up question - if
   SetTransform starts firing after a re-hook, the seam was always on the path and a re-assert is
   the whole fix; if it still never fires with our pointer verifiably in the slot, the engine is
   not drawing the world through this device and the seam is the wrong one.
   The vtable POINTER is watched as well as its contents: an object handed a different vtable and
   an object whose vtable was edited look identical from the call counts and are not the same
   thing at all. */
static void FpWatchSlots(LONG frame)
{
    static int logged[4];
    static const struct { int idx; const char *name; } watch[4] = {
        { IDIRECT3DDEVICE8_SETTRANSFORM,      "37 SetTransform" },
        { IDIRECT3DDEVICE8_BEGINSCENE,        "34 BeginScene" },
        { IDIRECT3DDEVICE8_DRAWINDEXEDPRIM,   "71 DrawIndexedPrimitive" },
        { IDIRECT3DDEVICE8_DRAWPRIMUP,        "72 DrawPrimitiveUP" },
    };
    if (!g_device) return;

    void **vt = *(void ***)g_device;
    if (vt != g_vtable && !g_vtSwapLogged) {
        g_vtSwapLogged = 1;
        FpLog("THE DEVICE WAS HANDED A DIFFERENT VTABLE - frame, the new vtable",
              (DWORD)frame, (DWORD)(DWORD_PTR)vt);
    }

    void *mine[4];
    mine[0] = (void *)FpSetTransform;
    mine[1] = (void *)FpBeginScene;
    mine[2] = (void *)FpDraw;
    mine[3] = (void *)FpDrawPrimUP;

    for (int i = 0; i < 4; i++) {
        if (vt[watch[i].idx] == mine[i]) continue;
        if (!logged[i]) {
            logged[i] = 1;
            FpLog(watch[i].name, 0, 0);
            FpLog("  ...IS NO LONGER OURS - frame, what is in the slot now",
                  (DWORD)frame, (DWORD)(DWORD_PTR)vt[watch[i].idx]);
        }
        /* Only slot 37 is put back. The other three are evidence and must stay evidence: a slot
           we keep re-asserting can no longer tell us whether anything reverts it. */
        if (watch[i].idx == IDIRECT3DDEVICE8_SETTRANSFORM && g_xformOwned) {
            if (g_xformAtRehook < 0) g_xformAtRehook = g_xformCalls;
            void *prev = FpHookVTable(g_device, IDIRECT3DDEVICE8_SETTRANSFORM,
                                      (void *)FpSetTransform);
            /* Never adopt our OWN function as "the original" - that is an infinite recursion
               waiting for the next reversion. Keep the d3d8 entry we took at install. */
            if (prev && prev != (void *)FpSetTransform && FpSameModuleAsD3D(prev))
                g_realSetXform = (SetTransform_t)prev;
            g_rehooks++;
        }
    }

    /* ---- ROUTE 2: MAKE THE ENGINE REFRESH THE CAMERA, EVERY FRAME ----------------------------
     * MEASURED 2026-07-30, and it is the whole of the jitter: over one run the detour fired
     * 93680 times and only 286 of those were the camera frame. So our position was written 286
     * times while the game drew thousands of frames, and on every other frame the engine's own
     * chase camera stood - a picture alternating between two places, which reads as
     * jitter, now forward, now back, and as the camera running ahead during acceleration.
     *
     * The cause is a flag the engine keeps. Both call sites in the VIEW builder are guarded by a
     * test of bit 0x20 in the flags byte at camera+0xAC: when the bit is SET the engine treats
     * the world matrix as current and skips the refresh entirely.
     * The engine sets that bit once and leaves it set while the camera is not moving in ITS
     * model of the world. Clearing it marks this frame's world matrix as stale, which is true -
     * we are about to make it stale - and the refresh runs, and our detour lands.
     *
     * WHY HERE AND NOT IN THE DETOUR: clearing it from inside the detour would be asking the
     * engine to re-enter the function it is already running. Present is once a frame, before the
     * next frame's VIEW is built, which is exactly the moment the bit needs to be down.
     *
     * This is a WRITE INTO THE ENGINE'S STATE, so it is gated on route 2 AND on the mod being
     * enabled: with first person off, this file must leave the game untouched. */
    if (g_route == 2 && g_enabled) {
        HMODULE lsd = GetModuleHandleA("LS3DF.dll");
        if (lsd) {
            void *camd = *(void **)((BYTE *)lsd + 0x1C4CF8u);
            if (camd) {
                BYTE *flags = (BYTE *)camd + 0xACu;
                static int dirtyLogged = 0;
                if (!dirtyLogged) {
                    dirtyLogged = 1;
                    FpLog("route 2: clearing the camera's world-matrix-current bit every frame "
                          "so the refresh runs. flags before, frame", (DWORD)*flags, (DWORD)frame);
                }
                *flags = (BYTE)(*flags & ~0x20u);
            }
        }
    }

    /* ROUTE 2's patch gets the same treatment, and for the same reason. Slot 37 read back
       correctly at install and was gone by the first frame; a detour in a DLL's own .text is no
       more sacred than a vtable entry, and a mod whose patch was quietly undone looks exactly
       like a mod whose seam is wrong. The FIRST frame it is missing is what distinguishes them,
       so the frame number is what gets logged. Not re-asserted: unlike slot 37, this one has
       never been seen to revert, and a patch we keep putting back can no longer tell us whether
       anything takes it away. */
    if (g_route == 2 && g_updwSite) {
        static int updwGoneLogged = 0;
        if (!updwGoneLogged && !FpWMatrixVerify()) {
            updwGoneLogged = 1;
            FpLog("THE UpdateWMatrixProc DETOUR IS GONE - frame, the byte now at the site",
                  (DWORD)frame, (DWORD)g_updwSite[0]);
        }
    }

    /* The engine's own device global. If it ever stops being the object we hooked, everything
       above is being measured on an orphan. */
    if (g_engineDevPrev) {
        HMODULE ls = GetModuleHandleA("LS3DF.dll");
        if (ls) {
            void *now = *(void **)((BYTE *)ls + 0x1C597Cu);
            if (now != g_engineDevPrev) {
                FpLog("THE ENGINE SWAPPED ITS DEVICE - frame, the new device",
                      (DWORD)frame, (DWORD)(DWORD_PTR)now);
                g_engineDevPrev = now;
            }
        }
    }
}

static HRESULT WINAPI FpPresent(void *self, const void *a, const void *b, HWND c, const void *d)
{
    LONG n = ++g_frames;
    FpWatchSlots(n);

    /* THE A/B WINDOWS. Anchored to the frame the car anchor FIRST resolved, not to a frame
       number: the menu runs at ~300 fps and the city at ~60, so any fixed count lands wherever
       the load time put it - and the replay only stays in the city for about eight seconds, so
       a window chosen by counting frames misses it entirely. Two-second windows give four of
       them inside that.
       Each window reports the geometry drawn while the substitution was in one state, then
       flips it. Culling and bad arithmetic look identical on screen and disagree here: culled
       geometry makes the indexed-primitive count COLLAPSE in the "on" windows, while geometry
       that is drawn and flies off screen leaves it unchanged. */
    if (g_abFrames > 0 && g_cityFrame && n > g_cityFrame &&
        ((n - g_cityFrame) % g_abFrames) == 0) {
        static LONG prevDraw, prevUP, prevScene;
        LONG d  = g_drawCalls    - prevDraw;
        LONG du = g_drawAlt[1]   - prevUP;
        LONG sc = g_sceneCalls   - prevScene;
        prevDraw = g_drawCalls; prevUP = g_drawAlt[1]; prevScene = g_sceneCalls;
        FpLog("A/B - was the substitution ON, and DrawIndexedPrimitive in those 600 frames",
              (DWORD)g_abOn, (DWORD)d);
        FpLog("  ...DrawPrimitiveUP and BeginScene in the same window", (DWORD)du, (DWORD)sc);
        g_abOn = g_abOn ? 0 : 1;
    }
    /* ---- THE CITY REPORT, and it exists because the other one reported the MENU ---------------
     * 2026-08-14: the run armed to measure the interface reported at frames 1000, 3000, 6000,
     * 12000 and 20000 - and the car anchor did not resolve until frame 23801. So every number in
     * that log described the main menu, where the engine draws one DrawPrimitiveUP per frame and
     * nothing else, and the city - the only place the question lives - was never sampled at all.
     * A fixed frame number cannot find the mission: the menu runs at ~300 fps and the city at
     * ~60, so where 20000 lands depends entirely on how long somebody sat in the menu.
     *
     * This block is anchored to `g_cityFrame` instead, reports every 1200 frames (~20 s of play)
     * and prints DELTAS, so two runs of the same replayed drive can be compared line for line -
     * which is how the draw distance gets measured: same route, same frames, more geometry.
     * Every counter says its number even when the number is zero, on purpose. */
    if (g_cityFrame && n > g_cityFrame && ((n - g_cityFrame) % 1200) == 0) {
        static LONG pDraw, pUP, pScene, pVS, pXf;
        LONG d = g_drawCalls - pDraw, du = g_drawAlt[1] - pUP, sc = g_sceneCalls - pScene;
        LONG vs = g_vsCalls - pVS,    xf = g_xformCalls - pXf;
        pDraw = g_drawCalls; pUP = g_drawAlt[1]; pScene = g_sceneCalls;
        pVS = g_vsCalls; pXf = g_xformCalls;
        FpLog("CITY 1200 frames - frames since the city, DrawIndexedPrimitive",
              (DWORD)(n - g_cityFrame), (DWORD)d);
        FpLog("  ...DrawPrimitiveUP, BeginScene", (DWORD)du, (DWORD)sc);
        FpLog("  ...SetVertexShader, SetTransform", (DWORD)vs, (DWORD)xf);
        FpLog("  HUD: entries into the squeeze, and draws actually rewritten",
              (DWORD)g_hudCalls, (DWORD)g_hudDraws);
        FpLog("  HUD rejected: not screen space, no viewport",
              (DWORD)g_hudNotSS, (DWORD)g_hudNoSurf);
        FpLog("  ...GetVertexShader refused, and the handle it last returned",
              (DWORD)g_hudNoVS, (DWORD)g_curVS);
        FpLog("  HUD rejected: factor is 1, full-screen span, too big for the scratch",
              (DWORD)g_hudUnity, (DWORD)g_hudFullScreen);
        FpLog("  HUD anchoring: left, right", (DWORD)g_hudLeft, (DWORD)g_hudRight);
        FpLog("  HUD zones: corrected as radar, as speedometer",
              (DWORD)g_hudZoneRadar, (DWORD)g_hudZoneDial);
        FpLog("  HUD skipped: outside both zones, not in a car",
              (DWORD)g_hudOutOfZone, (DWORD)g_hudNotInCar);
        FpLog("  FLAT draws from a vertex BUFFER - we cannot rewrite these (the radar blips)",
              (DWORD)g_ssIndexed, 0);
        FpLog("  HUD skipped: drawn into a render target, not the screen; widest surface seen",
              (DWORD)g_hudOffScreen, (DWORD)((g_hudMaxW << 16) | (g_hudMaxH & 0xFFFF)));
        FpLog("  the radar's centre and width, as x1000",
              (DWORD)(LONG)(g_radarCx * 1000.0f), (DWORD)(LONG)(g_radarSpan * 1000.0f));
        FpLog("  HUD surface w,h and factor x1000",
              (DWORD)((g_hudW << 16) | (g_hudH & 0xFFFF)), (DWORD)(LONG)(g_hudK * 1000.0f));
        FpLog("  probe rows written, and the pct setting in force",
              (DWORD)g_hudProbeRows, (DWORD)g_hudAspectPct);
    }

    if (n == 1)
        FpLog("first frame presented - the device vtable IS live", 0, 0);
    /* 1000 frames is ~17 s at 60 Hz and the mission has NOT loaded by then - the replay's own
       timing puts the city at ~21 s. So the first report samples the MENU, which is why it read
       one BeginScene and one DrawPrimitiveUP per frame and no geometry: that is a 2D screen,
       not an impossible city. Report again well inside the mission. */
    else if (n == 1000 || n == 3000 || n == 6000 || n == 12000 || n == 20000) {
        FpLog("1000 frames: SetTransform calls, DrawIndexedPrimitive calls",
              (DWORD)g_xformCalls, (DWORD)g_drawCalls);
        FpLog("1000 frames: SetVertexShader calls, SetVertexShaderConstant calls",
              (DWORD)g_vsCalls, (DWORD)g_vscCalls);
        FpLog("  shader-constant registers touched: lowest, highest+count",
              (DWORD)g_vscFirstReg, (DWORD)g_vscMaxReg);
        FpLog("1000 frames: BeginScene calls, DrawPrimitive calls",
              (DWORD)g_sceneCalls, (DWORD)g_drawAlt[0]);
        FpLog("  DrawPrimitiveUP, DrawIndexedPrimitiveUP",
              (DWORD)g_drawAlt[1], (DWORD)g_drawAlt[2]);
        /* ---- ROUTE 2's OWN VITAL SIGNS, and they exist because of one instruction ------------
         * The VIEW builder does NOT call UpdateWMatrixProc unconditionally. Both of its call
         * sites are guarded by the same test on the camera's flags byte at +0xAC: bit 0x20 SET
         * skips the refresh entirely, bit 0x20 CLEAR performs it.
         *
         * (The second call site is the no-parent branch - the two calls are ALTERNATIVES, not a
         * sequence.) So there is a live state of the world in
         * which route 2 is installed, correct, verified in place, and never runs: the engine
         * decides the matrix is already current and reads camera+0x40 without asking us.
         *
         * That is precisely the failure this project refuses to ship - installed, running, and
         * quietly hooked to nothing - so it gets a counter and a name rather than a silence. The
         * three counts separate the three ways it can go wrong: the detour never fired at all
         * (the flag), it fired but never on the camera frame (the object identity), or it fired
         * on the camera and refused to write (the anchor or the 50 m guard). The flags byte is
         * logged with them so the next run does not have to guess which.
         *
         * What the same disassembly settles in route 2's FAVOUR, at 1000bae5:
         *     flds 0x40(%ebp) / fchs      - VIEW's translation is the NEGATED camera+0x40,
         * rotated by the transpose of WORLD's 3x3 that the twelve movl pairs above it copy. The
         * write reaches VIEW by the engine's own arithmetic. That is measured here, not carried
         * over from an earlier build. */
        if (g_route == 2) {
            HMODULE lsr = GetModuleHandleA("LS3DF.dll");
            DWORD flags = 0xFFFFFFFFu;
            if (lsr) {
                void *camr = *(void **)((BYTE *)lsr + 0x1C4CF8u);
                if (camr) flags = *(BYTE *)((BYTE *)camr + 0xACu);
            }
            FpLog(g_updwCalls == 0
                      ? "ROUTE 2: the detour has NEVER FIRED - the engine is skipping the "
                        "refresh (camera+0xAC bit 0x20). calls, of which the camera frame"
                      : "route 2: detour calls, of which the camera frame",
                  (DWORD)g_updwCalls, (DWORD)g_updwCam);
            FpLog("  ...of those, writes; and the camera's flags byte at +0xAC "
                  "(bit 0x20 set = the engine considers WORLD current and skips us)",
                  (DWORD)g_updwWrites, flags);
        }
        /* THE LAST AMBIGUITY. Index 37 is SetTransform - proven from LS3DF's own call sites,
           which push D3DTS_VIEW and D3DTS_PROJECTION before calling *0x94 - and the engine has
           49 such sites. So either it never runs them, or our pointer is no longer in the slot.
           Re-read it and say which. A hook that was quietly reverted looks exactly like a
           function that is never called, and only one of those is our fault. */
        /* THE DECIDING COMPARISON. Disassembly (private reference material at 10062f29) shows
           the engine calling vtable slot 37 with D3DTS_VIEW on an object held in a global at
           LS3DF+0x1C597C - and slot 50 with two args and slot 63 with three, which are
           SetRenderState and SetTextureStageState exactly. So the index table is right and the
           engine really does call SetTransform. If that global is NOT the device CreateDevice
           handed us, we hooked the wrong object; if it IS, then its vtable pointer was swapped
           after we patched. Those are the only two possibilities left, and this tells them
           apart. LS3DF RELOCATES, so the address is computed from the module base, never used
           as the absolute it appears as in the listing. */
        {
            HMODULE ls = GetModuleHandleA("LS3DF.dll");
            void *engineDev = NULL;
            /* LS3DF+0x1C597C. Derived twice, independently, and both readings checked against
               the listing by hand: the `pushl $0x101c597c` that CreateDevice receives as its
               ppReturnedDeviceInterface out-parameter (at 1006f6a9), and the `movl 0x101c597c,
               %eax` the camera code dereferences as a live COM pointer right before calling
               slot 37 with D3DTS_VIEW (at 10062f18). */
            if (ls) engineDev = *(void **)((BYTE *)ls + 0x1C597Cu);
            FpLog("  the device the ENGINE uses / the one we hooked",
                  (DWORD)(DWORD_PTR)engineDev, (DWORD)(DWORD_PTR)g_device);
            if (engineDev)
                FpLog("  ...their vtables", (DWORD)(DWORD_PTR)(*(void ***)engineDev),
                      g_device ? (DWORD)(DWORD_PTR)(*(void ***)g_device) : 0);
        }
        if (g_device) {
            void **vt = *(void ***)g_device;
            FpLog("  slot 37 now holds / ours is",
                  (DWORD)(DWORD_PTR)vt[IDIRECT3DDEVICE8_SETTRANSFORM],
                  (DWORD)(DWORD_PTR)FpSetTransform);
            FpLog("  slot 71 now holds / ours is",
                  (DWORD)(DWORD_PTR)vt[IDIRECT3DDEVICE8_DRAWINDEXEDPRIM],
                  (DWORD)(DWORD_PTR)FpDraw);
            FpLog("  the device and its vtable", (DWORD)(DWORD_PTR)g_device,
                  (DWORD)(DWORD_PTR)vt);
            /* THE ANSWER LINE. re-hooks says how often slot 37 was taken from us; the second
               field is what SetTransform had counted when the FIRST one happened. Zero re-hooks
               with zero calls means nothing ever reverted it and the engine simply does not use
               this device for the world. Many re-hooks means something re-asserts the original
               every frame, and then the count of calls says whether we ever win the race. */
            FpLog("  slot 37 re-hooks, and SetTransform's count at the first one",
                  (DWORD)g_rehooks, (DWORD)g_xformAtRehook);
        }
    }
    return g_realPresent(self, a, b, c, d);
}

/* Replace one vtable entry. The vtable of a COM object lives in read-only pages of the module
   that defined it, so this needs VirtualProtect and must restore the protection - leaving a
   writable code page behind is exactly the sort of thing that makes an injected DLL look like
   what an antivirus is scanning for. */
static void *FpHookVTable(void *obj, int index, void *replacement)
{
    void ***vt = (void ***)obj;
    if (!obj || !*vt) return NULL;
    void **slot = &(*vt)[index];

    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return NULL;
    void *prev = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(void *), old, &old);

    /* READ IT BACK. A write that did not land looks exactly like a function that is never
       called, and this project has already lost a run to that pair being confused - the first
       in-game session reported six hooks "installed" and zero calls on three of them, which is
       a sentence that means nothing until you know whether the bytes changed. VirtualProtect
       succeeding does not prove the store stuck: a copy-on-write page, a guard, or a second
       writer can all swallow it silently. */
    if (*slot != replacement) {
        FpLog("VTABLE WRITE DID NOT STICK - slot, what is in it now",
              (DWORD)index, (DWORD)(DWORD_PTR)*slot);
        return NULL;
    }
    return prev;
}

/* Which module does `p` live in? Returns its allocation base, or NULL. */
static void *FpModuleOf(void *p)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return NULL;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return NULL;
    if (mbi.State != MEM_COMMIT) return NULL;
    return mbi.AllocationBase;
}

/* The sanity check on every vtable entry we take as "the original": it must live in the SAME
   MODULE the real Direct3DCreate8 came from. If it does not, either the index is wrong or
   something else already owns the slot, and calling it blind is how two mods become one crash
   with no author.
 *
 * IT USED TO COMPARE AGAINST GetModuleHandleA("d3d8.dll") AND THAT WAS WRONG - found by running
 * it, 2026-07-28, first launch. The refusal fired on a perfectly good vtable whose entry sat
 * 0x2A0 bytes from the Direct3DCreate8 we had just saved, i.e. unmistakably the same module. On
 * this machine the D3D provider that actually answers is not the module that name resolves to.
 * The invariant was never that the name is d3d8.dll; it was always that the same place the entry
 * point came from, which is also what makes this correct on a machine with a d3d8-to-9 shim, on
 * Wine, or on any build in the wild. Comparing against a NAME encoded an assumption
 * about the host; comparing against the pointer we already hold encodes the actual requirement.
 *
 * AND COMPARING AGAINST Direct3DCreate8 WAS WRONG TOO - found on Alex's own install, 2026-08-15,
 * on a shipped 1.4.1. The camera worked and the interface fix did nothing, because EVERY draw slot
 * was refused: slot 37 was owned by `C:\Windows\SYSTEM32\d3d8.dll` while the `Direct3DCreate8` we
 * had captured from LS3DF's import table lived in a different module entirely. Something answers
 * that import before the real runtime does, so "the module the entry point came from" named the
 * thing IN FRONT of D3D rather than D3D.
 *
 * So both single references are wrong, each in the other's situation, and flipping between them
 * is how a guard oscillates for ever. The invariant neither of them captured is this: a foreign
 * hook replaces a FEW slots of a vtable, never all of them. The genuine implementation is
 * therefore whichever module owns MOST of the device's own vtable - which is true when the
 * provider is the system runtime, when it is a d3d8-to-d3d9 wrapper in the game folder, and on
 * Wine, without naming any of them.
 *
 * Decided ONCE, from the vtable as it is before we touch it, and logged by name with its count so
 * a refusal can be read afterwards instead of guessed at.
 */
static void *g_d3dRef = NULL;

static void FpPickD3DRef(void *dev)
{
    void **vt;
    void *cand[8]; int cnt[8], n = 0, i, j, best = -1;
    if (!dev) return;
    vt = *(void ***)dev;
    if (!vt) return;
    /* 80 of IDirect3DDevice8's 89 slots - enough for a majority, short of the end. */
    for (i = 0; i < 80; i++) {
        void *m = FpModuleOf(vt[i]);
        if (!m) continue;
        for (j = 0; j < n; j++) if (cand[j] == m) { cnt[j]++; break; }
        if (j == n && n < 8) { cand[n] = m; cnt[n] = 1; n++; }
    }
    for (j = 0; j < n; j++) if (best < 0 || cnt[j] > cnt[best]) best = j;
    g_d3dRef = (best >= 0) ? cand[best] : FpModuleOf((void *)g_realCreate8);
    {
        char who[MAX_PATH];
        HMODULE m = (HMODULE)g_d3dRef;
        FpLog("the device's vtable comes from this module, in this many of 80 slots",
              (DWORD)(DWORD_PTR)g_d3dRef, (DWORD)(best >= 0 ? cnt[best] : 0));
        if (m && GetModuleFileNameA(m, who, MAX_PATH))
            FpLogStr("  ...which is", who);
        /* Named too, because when the two DISAGREE that difference is the whole diagnosis. */
        m = (HMODULE)FpModuleOf((void *)g_realCreate8);
        if (m && GetModuleFileNameA(m, who, MAX_PATH))
            FpLogStr("  ...and Direct3DCreate8 came from", who);
        if (n > 1) FpLog("  ...NOTE: the vtable spans more than one module. modules seen",
                         (DWORD)n, 0);
    }
}

static int FpSameModuleAsD3D(void *p)
{
    void *want = g_d3dRef ? g_d3dRef : FpModuleOf((void *)g_realCreate8);
    void *got  = FpModuleOf(p);
    return want && got && want == got;
}

/* Where the engine's own camera is, in world space. NOT recoverable from the VIEW matrix in a
 * loaded scene - that matrix has a zero translation row - so it is read from the camera frame
 * the engine itself is about to send, at `*(LS3DF+0x1C4CF8) + 0x40`.
 *
 * Derived from the disassembly of the very function that sends VIEW (10062e1f..10062e5d): it
 * refreshes the camera frame's world matrix and then copies three consecutive floats from
 * +0x40 into the globals at LS3DF+0x1C4C60. A 4x4 matrix at frame+0x10 puts its translation row
 * on +0x40 exactly. Confirmed against a completely independent path: in the menu, where VIEW
 * DOES carry a translation, the eye recovered from the matrix equalled this field bit for bit.
 *
 * The GLOBAL is read rather than the object, because the global is what the engine leaves
 * behind after its own null check and needs no pointer chase - but the object is checked too,
 * since a global that is merely stale would be indistinguishable from a correct one.
 */
static int FpEngineEye(float *out3)
{
    HMODULE ls = GetModuleHandleA("LS3DF.dll");
    if (!ls) return 0;
    void *cam = *(void **)((BYTE *)ls + 0x1C4CF8u);
    if (!cam) return 0;
    const float *p = (const float *)((BYTE *)cam + 0x40u);
    out3[0] = p[0]; out3[1] = p[1]; out3[2] = p[2];
    return 1;
}

/* ---- ROUTE 2: the write, on the return of the engine's own world-matrix refresh --------------
 *
 * Everything structural about this seam is in fp_wmatrix.h. What lives here is the one thing that
 * needs the pose pipeline: WHAT to write.
 *
 * The operation is a MOVE TOWARDS, not an add:
 *
 *     row += (want - row) * k
 *
 * which at k = 1 is `row = want` and at k = 0 is a no-op. That shape is chosen over `row += delta`
 * for one specific reason: `UpdateWMatrixProc` is a method on `I3D_frame`, the base class of every
 * frame in the scene, and while the `self == camera` test below means we only ever act on the
 * camera, nothing guarantees the VIEW builder is the ONLY caller that passes it. An accumulating
 * offset called twice a frame walks the camera off the map in seconds; a move-towards called
 * twice a frame simply arrives at the same place twice. Idempotence is the property that makes an
 * unknown call count survivable, and the call count is genuinely unknown ([[camera-object-gog-addresses]]
 * records the builder calling it twice at 1000ba50 and 1000ba87).
 *
 * At k between 0 and 1 it is NOT idempotent - repeated calls converge on `want` - so a
 * fractional `delta_pct` is a diagnostic here and not a setting. Said out loud because route 1's
 * `delta_pct` behaves exactly the opposite way and the same key now means two things.
 *
 * The 50 m guard is the same guard and for the same reason as route 1: a refusal is diagnosable,
 * a teleported picture is reported as "the mod is broken".
 */
static int           g_updwLogged  = 0;
static int           g_updwFarLog  = 0;
static volatile LONG g_traceN      = 0;   /* traced WRITES, not calls - see the trace */
static volatile LONG g_traceArmed  = 0;   /* opened by the car moving, not by a counter    */
/* The last eye we wrote, published so the SetTransform side can compare the CAR's drawn
   position against it in the same frame. Written by the detour, read by the D3D hook. */
static float         g_lastWant[3];
static volatile LONG g_haveLastWant = 0;

static void __attribute__((fastcall)) FpUpdateWMatrix(void *self, void *edx)
{
    /* The engine's work happens FIRST and unconditionally. Whatever we do or refuse to do below,
       the frame's world matrix must be refreshed exactly as it would have been - this detour is
       an addition to the engine's behaviour, never a replacement for it. */
    g_updwTramp(self, edx);
    g_updwCalls++;

    if (!g_enabled || g_route != 2) return;

    HMODULE ls = GetModuleHandleA("LS3DF.dll");
    if (!ls) return;
    void *cam = *(void **)((BYTE *)ls + 0x1C4CF8u);
    /* EVERY frame in the scene comes through here. Acting on any frame but the camera would move
       a lamp post. The camera is identified by the engine's OWN global, not by a signature. */
    if (!cam || cam != self) return;
    g_updwCam++;

    /* ---- IS HE IN A CAR? ASKED BEFORE THE VIEW GATE, AND THAT ORDER IS THE POINT -------------
     *
     * Alex, 2026-08-14, after drive 014: *"the correction has to follow the car."*
     *
     * The interface correction used to sit BELOW the view gate, so it inherited the camera's
     * condition: not the inside view, no substitution, no confirmation, and the squeeze switched
     * itself off. The drive proves it - sixteen presses of C in the key recording, sixteen
     * `view mode changed` lines here, and the radar went round at 0:20, elliptical at 0:24, round
     * again at 1:00 and elliptical at 1:07, every switch landing on a press. He blamed Escape;
     * the pause menu opened and closed with the correction working on both sides of it.
     *
     * The radar and the speedometer are stretched in EVERY driving view, so the correction
     * belongs to the car, not to the camera. The camera keeps its own gate below - the two
     * questions are separate and are now asked separately. */
    CamPose p;
    if (!CarAnchor(p.pos, p.basis)) { g_inCar = 0; return; }  /* on foot, in a menu, or no car */
    g_inCar = 1;
    /* AND THE FRAME IT WAS CONFIRMED ON, which is the half that actually matters. Alex, 2026-08-14:
       the menu was fine when the game started and BROKEN in its top left corner after he left the
       mission. That is this flag latching: when the player leaves the car this function stops
       being called at all, so the line above never runs, the 1 stands for ever, and the squeeze
       carries on into the menu - into the radar's corner, which is where the menu buttons are.
       A state that can only be cleared by code that stops running is not a state, it is a memory
       of one. The consumer below therefore requires a RECENT confirmation instead. */
    g_inCarFrame = g_frames;
    p.haveBasis = 1;
    if (!g_cityFrame) {
        g_cityFrame = g_frames;
        FpLog("route 2: the car anchor resolved for the first time, frame", (DWORD)g_cityFrame, 0);
        /* START THE PROBE HERE, not at launch. Its 600-row budget was spent on the main menu on
           2026-08-14 - every row in that log was a menu panel, and the two elements the probe
           exists to locate had not been drawn yet. */
        g_hudProbeRows = 0;
    }

    /* ---- ARE WE THE VIEW HE ASKED FOR? 2026-08-06 -------------------------------------------
     * Everything above proves this is the camera. This proves it is the camera IN THE MODE we
     * were given, so the other five stock views stay the game's own.
     *
     * Every dereference is guarded, and the frame identity is CHECKED rather than assumed:
     * `camctl+0x04` is set from `scene+0x17C` at 0x5A1E89, so if it does not equal the frame we
     * were handed, the chain resolved to something else and the safe answer is to leave the
     * picture alone. Refusing costs a stock camera for a frame; guessing moves a lamp post. */
    if (g_viewMode != 0) {
        DWORD glob = *(volatile DWORD *)FP_CAMCTL_GLOBAL;
        int ok = 0;
        if (CarReadable(glob, 0x28)) {
            DWORD game = *(volatile DWORD *)(glob + 0x24u);
            if (CarReadable(game, 0x60)) {
                DWORD camctl = game + 0x4Cu;
                if (*(volatile DWORD *)(camctl + 0x04u) == (DWORD)(BYTE *)self) {
                    LONG mode = *(volatile LONG *)(camctl + 0x10u);
                    static volatile LONG s_lastMode = -1;
                    /* Published EVERY frame, not only on a change: `key_take_view` reads it and
                       a value that is only refreshed on transitions would hand the key a stale
                       mode for as long as he sits in one view - which is the whole time he is
                       deciding. */
                    g_liveMode = mode;
                    if (mode != s_lastMode) {
                        s_lastMode = mode;
                        FpLog("view mode changed - the game's mode, and the one we take",
                              (DWORD)mode, (DWORD)g_viewMode);
                    }
                    ok = (mode == g_viewMode);
                }
            }
        }
        if (!ok) return;
    }

    /* THE JITTER TRACE, 2026-07-30. Reported: the camera shaking forward and
       back, and asked - fairly - whether my sampling could even see it. It could not: the VIEW
       samples fire every 900th call, which is a reading every fifteen seconds.
       Every camera-frame call is logged instead, capped, WITH THE FRAME NUMBER. That is the
       measurement that matters, because the defect turned out to be the RATE and not the value:
       93680 detour calls over the run, of which 286 were the camera. A position written 286
       times cannot hold a camera through thousands of frames, and what fills the gaps is the
       engine's own chase camera - which is exactly a picture that jerks between two places. */

    /* The pose was taken above, before the view gate - `p` is filled and `p.haveBasis` is set. */

    /* ---- THE FIX: read the SOURCE, not last tick's copy of it ------------------------------
     * `car+0x2A10` is written straight from `[[car+0xE4]+0x68]+0x40`, component for component,
     * once a tick (private reference material 5a54f8 -> 5a55c4). So this substitution changes ONE thing and one
     * only: staleness. Every downstream line - the seat offset, the axis order, the guard -
     * stays exactly as it was, because the two values are the same quantity in the same space
     * and the only difference between them is a tick of age.
     * Off by `anchor = 0` in the ini, so the old behaviour is one key away and a run can A/B. */
    if (g_anchorMode == 1) {
        float rp[3];
        if (FpCarRenderPos(rp)) {
            static volatile LONG cmpN = 0;
            if (++cmpN <= 40) {
                /* THE FALSIFIABLE CHECK, and it is owed: if these two are the same quantity,
                   they must agree exactly while the car is STILL, and differ only while it
                   moves. A constant offset would mean they are not the same field after all,
                   and would also mean the recorded axis swap needs re-deriving. */
                FpLog("  ANCHORCMP render x,z bits", *(DWORD *)&rp[0], *(DWORD *)&rp[2]);
                FpLog("   ...car+0x2A10 x,z bits at the same moment",
                      *(DWORD *)&p.pos[0], *(DWORD *)&p.pos[2]);
                FpLog("   ...render height bits, and car+0x2A14",
                      *(DWORD *)&rp[1], *(DWORD *)&p.pos[1]);
            }
            /* ---- BASISCMP, 2026-07-31: the cabin-slide measurement -------------------------
             * Both bases at the same instant. If they are equal the diagnosis above is wrong
             * and the slide is something else; if the UP rows diverge under braking and the
             * others under cornering, it is confirmed AND the printout gives the row and
             * component mapping needed to write the fix without a second guess.
             * The dot product is the cheap running indicator - 1.0 means the two UP axes agree,
             * anything less is the pitch/roll the eye is inheriting from only one of them. */
            float rb[9];
            if (FpCarRenderBasis(rb)) {
                static volatile LONG bN = 0;
                LONG n = ++bN;
                if (n <= 8) {
                    FpLog("  BASISCMP anchor r0 x,y bits", *(DWORD *)&p.basis[0], *(DWORD *)&p.basis[1]);
                    FpLog("   ...anchor r0 z, r1 x",       *(DWORD *)&p.basis[2], *(DWORD *)&p.basis[3]);
                    FpLog("   ...anchor r1 y,z",           *(DWORD *)&p.basis[4], *(DWORD *)&p.basis[5]);
                    FpLog("   ...anchor r2 x,y",           *(DWORD *)&p.basis[6], *(DWORD *)&p.basis[7]);
                    FpLog("   ...anchor r2 z, render r0 x", *(DWORD *)&p.basis[8], *(DWORD *)&rb[0]);
                    FpLog("   ...render r0 y,z",           *(DWORD *)&rb[1], *(DWORD *)&rb[2]);
                    FpLog("   ...render r1 x,y",           *(DWORD *)&rb[3], *(DWORD *)&rb[4]);
                    FpLog("   ...render r1 z, r2 x",       *(DWORD *)&rb[5], *(DWORD *)&rb[6]);
                    FpLog("   ...render r2 y,z",           *(DWORD *)&rb[7], *(DWORD *)&rb[8]);
                }
                /* ---- AN OUTLIER DETECTOR, NOT A WINDOW, 2026-07-31 ------------------------
                 * First version logged the first 400 samples and every one read dot = 1.000000
                 * exactly - because the replay hit a table in the first seconds and the car
                 * then stood still for the rest of the run. 400 readings of a parked car is
                 * the same mistake this file already records against the jitter trace, made
                 * again on a different channel.
                 * So: watch the WHOLE drive and log only where the two bases actually part,
                 * plus a sparse heartbeat so "nothing logged" can be told from "never ran". */
                float d = p.basis[3] * rb[3] + p.basis[4] * rb[4] + p.basis[5] * rb[5];
                float dev = d < 1.0f ? (1.0f - d) : (d - 1.0f);
                static volatile LONG devN = 0;
                static float worst = 0.0f;
                if (dev > worst) worst = dev;
                if (dev > 0.0005f && devN < 200) {
                    devN++;
                    /* Both r1 vectors at the moment they disagree - that is the mapping AND
                       the magnitude in one line pair. */
                    FpLog("  BASISDEV dot bits, sample", *(DWORD *)&d, (DWORD)n);
                    FpLog("   ...anchor r1 x,y", *(DWORD *)&p.basis[3], *(DWORD *)&p.basis[4]);
                    FpLog("   ...anchor r1 z, render r1 x", *(DWORD *)&p.basis[5], *(DWORD *)&rb[3]);
                    FpLog("   ...render r1 y,z", *(DWORD *)&rb[4], *(DWORD *)&rb[5]);
                }
                if ((n % 600) == 0) {
                    FpLog("  BASISCMP heartbeat - worst deviation so far bits, sample",
                          *(DWORD *)&worst, (DWORD)n);
                }

                /* ---- AIMCMP, 2026-07-31: the surviving candidate -----------------------------
                 * The basis question is closed - 6000 samples over a 1200 m drive with two
                 * handbrake turns, worst deviation 0.000107. So the car's orientation is NOT
                 * what the eye is inheriting wrongly.
                 * What we DO inherit is the engine's own camera aim: route 2 replaces the
                 * translation row at cam+0x40 and leaves the 3x3 at cam+0x10 alone. That 3x3
                 * belongs to the game's chase camera, which hangs off a spring behind the car.
                 * A rigid eye with a swinging aim is exactly a picture that reads as the eye
                 * sliding around the cabin - and it is the one thing this file has suspected
                 * from the start without ever measuring it (see the comment at the jitter
                 * trace: position frozen, aim still swinging, reads as shake).
                 * So: compare the CAMERA's forward axis against the CAR's forward axis. If the
                 * two stay locked, this candidate dies too; if the angle between them breathes
                 * while the car corners, it is the defect, in one number.
                 * Row 2 of a frame's 4x4 lives at +0x30 - same stride as the rows read above. */
                {
                    const volatile float *cr2 = (const volatile float *)((BYTE *)cam + 0x30u);
                    float cx = cr2[0], cy = cr2[1], cz = cr2[2];
                    if (CarFinite(cx) && CarFinite(cy) && CarFinite(cz)) {
                        float aim = cx * rb[6] + cy * rb[7] + cz * rb[8];
                        static volatile LONG aimN = 0;
                        static float aimMin = 2.0f, aimMax = -2.0f;
                        if (aim < aimMin) aimMin = aim;
                        if (aim > aimMax) aimMax = aim;
                        LONG an = ++aimN;
                        if ((an % 600) == 0) {
                            FpLog("  AIMCMP cam-vs-car forward: min bits, max bits",
                                  *(DWORD *)&aimMin, *(DWORD *)&aimMax);
                        }
                    }
                }
            }
            /* CarAnchor does NOT return memory order, and the first run of this fix proved it
               the hard way: it fills `pos[0]` from `car+0x2A18` and `pos[2]` from `car+0x2A10`
               (see car_anchor.h), i.e. components 0 and 2 reversed. Downstream then reverses
               them again, and the two cancelled - which is why the old path was correct and why
               nothing in the code says so.
               Handing it the frame in raw memory order broke that cancellation: the eye landed
               2839 m away, the 50 m guard refused all 60 substitutions, the mod went inert and
               the game's own bonnet camera stood. Nothing was damaged, and the guard is why.
               So the render frame is converted INTO CarAnchor's convention here, at the one
               place the two meet. */
            p.pos[0] = rp[2]; p.pos[1] = rp[1]; p.pos[2] = rp[0];

            /* ---- THE CONVENTION MISMATCH, found 2026-07-31 ------------------------------------
             * `CamSeat` computes  pos[i] += basis[row][i] * seat[row].  For that sum to mean
             * anything, `basis` and `pos` have to be in the SAME component space. They are not:
             *
             *   pos    - CarAnchor's convention, components 0 and 2 deliberately REVERSED
             *            (pos[0] from car+0x2A18, pos[2] from car+0x2A10), and the render-frame
             *            substitution above reverses them again to match it.
             *   basis  - read as nine contiguous floats straight out of `frame + mtxOff`, i.e.
             *            RAW memory order. Proven raw today: it matches the render node's own
             *            raw 4x4 rows to 1.07e-4 over 6000 samples across two drives.
             *
             * So the seat offset is being rotated by a matrix expressed in the other handedness
             * of the position it is added to. Straight ahead the error is small and reads as the
             * eye sliding forward; under yaw the offset swings the WRONG WAY and the error grows
             * with the turn rate. Measured 2026-07-31, driving with the offset at +35:
             *   "when it starts moving it slides forward, but stays inside the car...
             *    during sharp turns the camera sometimes flew out through the door window."
             * A lever arm swinging the wrong way is exactly an eye that leaves through the door
             * in a handbrake turn and merely creeps forward in a straight line.
             *
             * The fix is one permutation, applied at the same place and for the same reason the
             * position already gets one: swap components 0 and 2 of each basis ROW. The row
             * ORDER is untouched - rows are named by the seat vector (right/up/forward), not by
             * a world axis, so a component permutation does not renumber them.
             * `basis_swap = 0` in the ini restores the old behaviour for an A/B. */
            if (g_basisSwap) {
                for (int r = 0; r < 3; r++) {
                    float t = p.basis[r * 3 + 0];
                    p.basis[r * 3 + 0] = p.basis[r * 3 + 2];
                    p.basis[r * 3 + 2] = t;
                }
            }
        }
    }

    float seat[3];
    seat[0] = (float)g_seatSideCm * 0.01f;   /* + right, - left */
    seat[1] = (float)(g_seatUpCm + g_stopIdx * FP_STOP_CM) * 0.01f;
    seat[2] = (float)g_seatFwCm * 0.01f;
    CamSeat(&p, seat);

    /* WORLD's translation row. A 4x4 at frame+0x10 puts row 3 on +0x40 exactly, and that field is
       confirmed on GOG by an independent path: in the menu the eye recovered from VIEW's own
       arithmetic equalled it bit for bit. */
    float *row = (float *)((BYTE *)cam + 0x40u);

    /* Components 0 and 2 are swapped between the car anchor and the engine camera - measured, at
       a thousand-to-one separation, and OWED a confirmation on a second map. Same swap as route 1
       and read from the same place, so if it is wrong both routes are wrong together, which is
       the honest coupling. */
    float want[3];
    want[0] = p.pos[2];
    want[1] = p.pos[1];
    want[2] = p.pos[0];

    /* ---- THE NEAR PLANE AND THE FOV, both in the PROJECTION at camera+0x1A4, 2026-07-31 -------
      * After the first tuning drive, at the position found preferable, the head
      * was not rendered, but some edges were: either it is a piece of the costume, or a piece
      * of the hat, or maybe the collar - so the camera had to be moved too close forward, and,
      * it seems, the field of view was too small.
     * Slivers of geometry a few centimetres from the eye are what a near plane removes, and
     * unlike the visibility-bit route it does not care that Tommy is one SNGMRPH mesh with no
     * head node - it clips by DISTANCE, so it works on every costume at once.
     *
     * Two facts make writing here sane: our own reading of the engine says PROJECTION is written
     * only ~300 times a session, all before this detour runs; confirmed on another build
     * that writing the ANGLE FIELD does nothing (the engine has already consumed it) while
     * changing this MATRIX works.
     *
     * A D3D perspective carries the depth range as
     *     m[10] = zf / (zf - zn)      m[14] = -zn * m[10]
     * so the engine's own planes are RECOVERED rather than guessed:
     *     zn = -m[14] / m[10]         zf = m[10] * zn / (m[10] - 1)
     * kerb, which is the intended result.
     * once, so a build whose convention is not the standard one shows up as an absurd zn/zf in
     * the log rather than as a quietly wrong picture.
     *
     * FOV is written from the WANTED angle, never scaled relatively: measurement on another build found that a
     * relative scale compounds every frame if the engine does not rebuild the matrix, and
     * whether it rebuilds is exactly what nobody knows. Their measurement also says the engine's
     * angle is the HORIZONTAL one, i.e. the m[0] term, so the aspect ratio m[5]/m[0] is carried
     * across unchanged instead of being assumed.
     *
     * Both default to 0 = "leave the engine's", so the shipped behaviour is unchanged. */
    if (g_nearCm > 0 || g_fovDeg > 0) {
        float *pr = (float *)((BYTE *)cam + CAM_PROJ_OFF);
        if (CarReadable((DWORD)(BYTE *)pr, 64) && CarFinite(pr[10]) && CarFinite(pr[14]) &&
            pr[0] > 0.0001f && pr[5] > 0.0001f && pr[10] > 1.0001f) {
            static volatile LONG projLogged = 0;
            float zn = -pr[14] / pr[10];
            float zf = pr[10] * zn / (pr[10] - 1.0f);
            if (!projLogged) {
                projLogged = 1;
                FpLog("PROJECTION recovered - near bits, far bits",
                      *(DWORD *)&zn, *(DWORD *)&zf);
                FpLog("  ...diag m0,m5 bits", *(DWORD *)&pr[0], *(DWORD *)&pr[5]);
                FpLog("  ...asked for near cm, fov deg", (DWORD)g_nearCm, (DWORD)g_fovDeg);
            }
            if (g_nearCm > 0 && CarFinite(zf) && zf > 1.0f) {
                float wn = (float)g_nearCm * 0.01f;
                if (wn < zf * 0.5f) {              /* a near plane past far is a black screen */
                    pr[10] = zf / (zf - wn);
                    pr[14] = -wn * pr[10];
                }
            }
            if (g_fovDeg > 0) {
                float half = (float)g_fovDeg * 0.008726646f;   /* deg -> rad, halved */
                float t = FpTan(half);
                if (t > 0.0001f && CarFinite(t)) {
                    float ratio = pr[5] / pr[0];               /* the engine's own aspect */
                    pr[0] = 1.0f / t;
                    pr[5] = pr[0] * ratio;
                }
            }
        }
    }

    /* ---- THE LEVEL HORIZON, 2026-07-31 -------------------------------------------------------
     * Roll only. The engine's forward axis is kept exactly as it aimed it, so pitch and yaw are
     * untouched and looking around still works; "right" is laid flat against world up (+Y), and
     * "up" is rebuilt from the two. The car then rotates around a level head when it hits a
     * kerb, which is the desired outcome.
     *
     * Recomputed from the engine's own forward every frame, never accumulated, so it cannot
     * drift and applying it twice is applying it once - the same discipline the FOV write uses.
     *
     * The one degenerate case is looking straight up or down, where forward is parallel to world
     * up and the cross product collapses. Guarded on the length: below the threshold the frame
     * is left exactly as the engine built it, which is a frame of roll rather than a frame of
     * garbage. The rows are WORLD's 4x4 at cam+0x10, stride 16 - the same layout whose
     * translation row at +0x40 this function already writes.
     *
     * Written on the RETURN from UpdateWMatrixProc, which is the seam confirmed in a
     * headset for composing head rotation into WORLD - so the engine derives VIEW from it. */
    /* WHERE THE CAMERA IS ACTUALLY AIMED, every frame, kept as raw bits for the verdict keys.
       Cheap - one guarded read of a matrix this function already touches - and it is the whole
       fix for the defect the 2026-08-06 drive exposed: the orientation was written to the log
       ONCE per launch behind a latch, so a drive carrying 23 "I like this" presses could not say
       what he liked. forward.y is the sine of the pitch, so asin of it is the angle. */
    {
        float *w0 = (float *)((BYTE *)cam + 0x10u);
        if (CarReadable((DWORD)(BYTE *)w0, 64) && CarFinite(w0[9])) {
            g_lastFwdY = *(volatile DWORD *)&w0[9];
        }
    }

    if (g_lockRoll) {
        float *w = (float *)((BYTE *)cam + 0x10u);
        if (CarReadable((DWORD)(BYTE *)w, 64)) {
            float fx = w[8], fy = w[9], fz = w[10];          /* row 2: forward */
            if (CarFinite(fx) && CarFinite(fy) && CarFinite(fz)) {
                /* right = normalize(worldUp x forward), worldUp = (0,1,0)
                   => (1*fz - 0*fy, 0*fx - 0*fz, 0*fy - 1*fx) = (fz, 0, -fx) */
                float rx = fz, ry = 0.0f, rz = -fx;
                float rl = FpSqrt(rx * rx + rz * rz);
                if (rl > 0.001f && CarFinite(rl)) {
                    rx /= rl; rz /= rl;
                    /* up = forward x right, with right = (rx, 0, rz) */
                    float ux = fy * rz - fz * ry;
                    float uy = fz * rx - fx * rz;
                    float uz = fx * ry - fy * rx;
                    if (CarFinite(ux) && CarFinite(uy) && CarFinite(uz)) {
                        w[0] = rx; w[1] = ry; w[2] = rz;
                        w[4] = ux; w[5] = uy; w[6] = uz;
                        static volatile LONG rollLogged = 0;
                        if (!rollLogged) {
                            rollLogged = 1;
                            FpLog("LEVEL HORIZON active - right x,z bits", *(DWORD *)&rx, *(DWORD *)&rz);
                            FpLog("  ...rebuilt up y bits, forward y bits", *(DWORD *)&uy, *(DWORD *)&fy);
                        }
                    }
                }
            }
        }
    }

    /* ---- THE PITCH TRIM, 2026-08-06 ----------------------------------------------------------
     * Rotate the camera's forward and up about its own RIGHT axis. Right is untouched, so this
     * composes cleanly with the level horizon above whichever way round they run: that block
     * rebuilds right and up from the engine's forward, and this one then tilts forward and up
     * within the plane right is normal to.
     *
     * Read fresh from WORLD every frame and written back the same frame, exactly like the roll
     * levelling - so nothing accumulates, `pitch_deg = 0` reproduces the engine's aim byte for
     * byte, and a value the ini watcher changes mid-drive takes effect on the next frame.
     *
     * `+` is up, which is the direction he asked for: the camera was aimed 14.5 degrees BELOW
     * the horizon and the horizon sat too high in the frame. */
    if (g_pitchDeg != 0) {
        float *w = (float *)((BYTE *)cam + 0x10u);
        if (CarReadable((DWORD)(BYTE *)w, 64)) {
            float fx = w[8], fy = w[9], fz = w[10];
            if (CarFinite(fx) && CarFinite(fy) && CarFinite(fz) &&
                CarFinite(w[4]) && CarFinite(w[5]) && CarFinite(w[6])) {
                if (CamPitch(w, (int)g_pitchDeg)) {
                    /* Once per launch, like the roll line, and for the same cost reason. What
                       makes the pitch ANSWERABLE afterwards is not this line - it is the pitch
                       now carried by every verdict key press. */
                    static volatile LONG pitchLogged = 0;
                    if (!pitchLogged) {
                        pitchLogged = 1;
                        FpLog("PITCH TRIM active - degrees, forward y bits BEFORE",
                              (DWORD)g_pitchDeg, *(DWORD *)&fy);
                        FpLog("  ...forward y bits AFTER (+ is up)", *(DWORD *)&w[9], 0);
                    }
                }
            }
        }
    }

    /* ---- THE JITTER MEASUREMENT, 2026-07-30 --------------------------------------------------
     * Reported: it still shakes after the write-rate fix. So the rate was necessary and not
     * sufficient, and the next step is a measurement rather than a second guess. Four readings
     * per camera write, which between them name every candidate:
     *
     *   want[]        - our anchor. If THIS shakes, `car+0x2A10` is a physics position sampled
     *                   out of phase with rendering, and the fix is where we read it.
     *   row[] before  - the engine's own translation. If this jumps while `want` is smooth, the
     *                   engine is writing the camera behind our back between our writes.
     *   WORLD m20/m22 - the camera's FORWARD axis. This is the candidate I most expect: we
     *                   replace the POSITION and inherit the ORIENTATION, and that orientation
     *                   belongs to a chase camera which aims at the car from a springy point
     *                   behind it. Position frozen, aim still swinging, reads as shake.
     *
     * WORLD is a 4x4 at cam+0x10, so its third basis row sits at +0x30..+0x38.
     */
    /* THE WINDOW IS COUNTED HERE, not at the top of the function, and the first attempt got that
       wrong: gating on the camera-frame call number spent all 400 slots before a car existed,
       because the detour starts firing the moment the dirty bit is cleared and `CarAnchor`
       returns early until the mission is loaded. The result was 400 frame-number lines and not
       one position - a trace that ran perfectly and measured nothing. Count what you actually
       want to sample. */
    /* ...AND THE WINDOW ONLY OPENS ONCE THE CAR IS ACTUALLY MOVING. Second correction to the
       same trace: counting writes from the first one spent all 400 slots on a PARKED car - 163
       frames of identical numbers, taken in the seconds between the anchor resolving and the
       replay pressing accelerate. Every series read as perfectly still, which was true and
       useless.
       The trigger is the car's own displacement from where it first resolved, not a frame count
       and not a timer: it cannot be thrown off by load time, frame rate, or the replay's timing
       drifting. Same reasoning as anchoring the A/B windows to the anchor frame. */
    if (g_traceArm == 1) {
        static float first[3]; static int haveFirst = 0;
        if (!haveFirst) { haveFirst = 1; first[0]=want[0]; first[1]=want[1]; first[2]=want[2]; }
        else if (!g_traceArmed) {
            float ax = want[0]-first[0], az = want[2]-first[2];
            if (ax*ax + az*az > 25.0f) {          /* 5 m, comfortably past any parked wobble */
                g_traceArmed = 1;
                FpLog("route 2: the car has moved 5 m - opening the jitter trace window, frame",
                      (DWORD)g_frames, 0);
            }
        }
    }
    /* mode 2 arms from the key thread instead - see FpThread */
    if (g_traceArmed && ++g_traceN <= 400) {
        const float *w = (const float *)((BYTE *)cam + 0x10u);
        FpLog("  route 2 trace: frame, traced write number", (DWORD)g_frames, (DWORD)g_traceN);
        FpLog("   ...trace want x,z bits",   *(DWORD *)&want[0], *(DWORD *)&want[2]);
        FpLog("   ...trace want y, engine row x bits", *(DWORD *)&want[1], *(DWORD *)&row[0]);
        FpLog("   ...trace engine row y,z bits", *(DWORD *)&row[1], *(DWORD *)&row[2]);
        FpLog("   ...trace WORLD forward m20,m22 bits",
              *(const DWORD *)&w[8], *(const DWORD *)&w[10]);
    }

    g_lastWant[0] = want[0]; g_lastWant[1] = want[1]; g_lastWant[2] = want[2];
    g_haveLastWant = 1;

    /* ---- THE CANDIDATE ANCHOR: the car's own FRAME, 2026-07-30 -------------------------------
     * Everything measured says the camera path is clean and the drawn body is what moves. If
     * that is right, the anchor should not be the car's PHYSICS position at car+0x2A10 but its
     * RENDER position - and the car already has a frame pointer at car+0x58, the same one the
     * gearbox module walks for the gear. If that is an I3D_frame like the camera, its world
     * translation sits at +0x40, the identical offset.
     *
     * This only READS it and logs it beside the physics position. Switching the anchor over is
     * a change to the picture and it waits for this measurement, because the whole point of the
     * exercise is to stop proposing fixes for causes that have not been shown.
     * What to look for in the log: if `frame` tracks the drawn body while `phys` does not, the
     * anchor is the bug and the fix is one line. If they are the same number, car+0x58 is not
     * the render transform and the search moves on. */
    {
        DWORD carp = CarPtr();
        static volatile LONG frameTrace = 0;
        if (carp && CarReadable(carp + CAR_FRAME_OFF, 4)) {
            DWORD fr = *(volatile DWORD *)(carp + CAR_FRAME_OFF);
            if (CarSane(fr) && CarReadable(fr + 0x40u, 12) && g_traceArmed && ++frameTrace <= 300) {
                const float *fp = (const float *)(fr + 0x40u);
                const float *ph = (const float *)(carp + CAR_POS_Y_OFF);
                FpLog("  FRAMETRACE frame, sample", (DWORD)g_frames, (DWORD)frameTrace);
                FpLog("   ...car frame +0x40 x,z bits",
                      *(const DWORD *)&fp[0], *(const DWORD *)&fp[2]);
                FpLog("   ...car frame +0x40 height bits, and the frame pointer",
                      *(const DWORD *)&fp[1], fr);
                FpLog("   ...physics car x,z bits",
                      *(const DWORD *)&ph[0], *(const DWORD *)&ph[2]);
            }
        }
    }

    float d[3];
    d[0] = want[0] - row[0];
    d[1] = want[1] - row[1];
    d[2] = want[2] - row[2];

    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 50.0f * 50.0f) {
        if (!g_updwFarLog) {
            g_updwFarLog = 1;
            FpLog("route 2: the eye we want is more than 50 m from the engine's - NOT writing. "
                  "d x,z bits", *(DWORD *)&d[0], *(DWORD *)&d[2]);
            FpLog("  ...d height bits, and the engine's own row x bits",
                  *(DWORD *)&d[1], *(DWORD *)&row[0]);
        }
        return;
    }
    g_updwFarLog = 0;

    if (g_passOriginal) return;      /* every line above ran; the engine keeps its own matrix */

    float k = (float)g_deltaPct * 0.01f;
    row[0] += d[0] * k;
    row[1] += d[1] * k;
    row[2] += d[2] * k;
    g_updwWrites++;

    if (!g_updwLogged) {
        g_updwLogged = 1;
        FpLog("route 2 is LIVE - wrote WORLD's translation row. d x,z bits",
              *(DWORD *)&d[0], *(DWORD *)&d[2]);
        FpLog("  d height bits, and the row we left behind, x bits",
              *(DWORD *)&d[1], *(DWORD *)&row[0]);
        FpLog("  the row's height and z bits", *(DWORD *)&row[1], *(DWORD *)&row[2]);
        FpLog("  detour calls so far, of which the camera frame",
              (DWORD)g_updwCalls, (DWORD)g_updwCam);
    }
}

/* ---- THE CAR'S RENDER POSITION, and why the old anchor was always going to lag -------------
 * Found in the GOG disassembly and re-read by hand at `private reference material
 *
 *   (instruction listing removed for publication - the addresses, offsets and the conclusion are stated in prose)
 *   ...
 *   (instruction listing removed for publication - the addresses, offsets and the conclusion are stated in prose)
 *
 * So `car+0x2A10` - the anchor this camera has used all along - is not a physics position at
 * all. It is a COPY of the drawn body's position, written once per tick, and the engine keeps
 * it only so the next tick can difference the two and derive speed into `car+0x2A0C`.
 *
 * That is the whole defect. Reading the copy instead of the source gives a camera that is one
 * tick behind the body it is supposed to be sitting inside. At 100 km/h one 60 Hz frame is
 * 0.46 m, which is the 0.5 m of relative movement measured between our anchor and the drawn
 * body - a number that never made sense while the anchor was believed to be physics.
 *
 * Reading the SOURCE also gives, for free, the thing `car+0x2A10` never could: `frame+0x10` is
 * a full 4x4, so the drawn body's ORIENTATION is available at the same pointer.
 *
 * The `0x20` guard is reproduced exactly as Game.exe does it, because that is what makes the
 * read current rather than merely fresher.
 */
typedef void (__attribute__((fastcall)) *FpUpdWCall_t)(void *self, void *edx);
static FpUpdWCall_t FpUpdWEntry(void)
{
    /* Prefer the trampoline: under route 2 the export itself is detoured, and calling through
       it would re-enter our own hook. The hook returns early for anything that is not the
       camera, so it would be harmless - but "harmless re-entry" is a thing to avoid rather
       than to rely on. */
    if (g_updwTramp) return g_updwTramp;
    static FpUpdWCall_t direct = NULL;
    if (!direct) {
        HMODULE ls = GetModuleHandleA("LS3DF.dll");
        if (ls) direct = (FpUpdWCall_t)GetProcAddress(ls, FP_UPDW_NAME);
    }
    return direct;
}

/* ---- THE RENDER NODE'S OWN BASIS, 2026-07-31 -------------------------------------------------
 * Why this exists: the position now comes from the render body (`[[car+0xE4]+0x68]+0x40`) but
 * the BASIS still comes from `CarAnchor`, which reads `[car+0x58]+mtxOff` - the car's root
 * frame. Those are two different nodes. The body pitches and rolls on its suspension; the root
 * does not. `CamSeat` then lays a 1.62 m up-offset along the un-pitched axes starting from the
 * pitching point, so a nose dive under braking slides the eye forward and body roll in a turn
 * slides it sideways. That is exactly what was reported on 2026-07-31:
 *   "while driving, our camera moves around inside the cabin: now left, now right, depending on the
 *    turn... under braking it shifts forward."
 *
 * This reader is DIAGNOSTIC ONLY for now and nothing downstream uses it. It is deliberately not
 * wired into `CamSeat` yet, because two things are unknown and guessing either one is how the
 * eye landed 2839 m away last time (see the comment at the substitution site):
 *   1. the ROW order - whether the 4x4's rows are right/up/forward in the same order as the
 *      packed 3x3 at `frame+mtxOff` that `CarAnchor` returns;
 *   2. the COMPONENT order - the position needed components 0 and 2 reversed to enter
 *      CarAnchor's convention, and a basis under that permutation transforms on BOTH indices,
 *      not one.
 * The trace below prints the two matrices side by side so one drive answers both.
 *
 * Layout: the node's 4x4 starts at +0x10 - that is what puts its translation row on +0x40
 * exactly, which is independently confirmed. Rows are 4 floats; only the first 3 are basis. */
static int FpCarRenderBasis(float *out9)
{
    DWORD car = CarPtr();
    if (!car || !CarReadable(car + 0xE4u, 4)) return 0;
    DWORD vis = *(volatile DWORD *)(car + 0xE4u);
    if (!CarSane(vis) || !CarReadable(vis + 0x68u, 4)) return 0;
    DWORD fr = *(volatile DWORD *)(vis + 0x68u);
    if (!CarSane(fr) || !CarReadable(fr + 0x10u, 48)) return 0;
    /* No UpdateWMatrix forcing here on purpose: FpCarRenderPos has already done it for this
       node this frame, and calling it twice would be a second write we have not reasoned about. */
    for (int r = 0; r < 3; r++) {
        const volatile float *row = (const volatile float *)(fr + 0x10u + (DWORD)r * 16u);
        for (int c = 0; c < 3; c++) {
            float v = row[c];
            if (!CarFinite(v)) return 0;
            out9[r * 3 + c] = v;
        }
    }
    return 1;
}

static int FpCarRenderPos(float *out3)
{
    DWORD car = CarPtr();
    if (!car || !CarReadable(car + 0xE4u, 4)) return 0;
    DWORD vis = *(volatile DWORD *)(car + 0xE4u);
    if (!CarSane(vis) || !CarReadable(vis + 0x68u, 4)) return 0;
    DWORD fr = *(volatile DWORD *)(vis + 0x68u);
    if (!CarSane(fr) || !CarReadable(fr + 0xACu, 1) || !CarReadable(fr + 0x40u, 12)) return 0;

    if (!(*(volatile BYTE *)(fr + 0xACu) & 0x20u)) {
        FpUpdWCall_t f = FpUpdWEntry();
        if (f) f((void *)fr, NULL);
    }
    const float *p = (const float *)(fr + 0x40u);
    if (!CarFinite(p[0]) || !CarFinite(p[1]) || !CarFinite(p[2])) return 0;
    out3[0] = p[0]; out3[1] = p[1]; out3[2] = p[2];
    return 1;
}

static HRESULT WINAPI FpSetTransform(void *self, DWORD state, const float *matrix)
{
    /* ---- DIAGNOSTIC, 2026-07-28 -----------------------------------------------------------
     * First run in the game: the chain hooked all the way to SetTransform; confirmed: the
     * scene rendered - car, city, pedestrians - and D3DTS_VIEW never arrived. Rather than
     * theorise about why, measure it. If SetTransform is never called AT ALL, the hook is not
     * live and the fault is upstream; if it is called with other states but never VIEW, the
     * engine sets the view somewhere else. Those are different bugs and the log now tells them
     * apart on the next launch instead of on the next guess.
     * Logged once per distinct state, capped, so a per-frame call cannot flood the file. */
    if (!g_stateLogged[state & 31u]) {
        g_stateLogged[state & 31u] = 1;
        FpLog("SetTransform called - state, call count so far", state, g_xformCalls);
    }
    g_xformCalls++;

    if (state == D3DTS_VIEW && !g_viewSeen) {
        g_viewSeen = 1;
        /* The proof the whole chain works, logged once rather than 60 times a second. The two
           values are the first and last floats of the matrix - a view matrix's [0][0] is a
           basis component near +-1 and its [3][3] is exactly 1.0, so a wrong pointer shows up
           here immediately as nonsense. */
        FpLog("SetTransform(D3DTS_VIEW) reached - the seam is live. m00,m33 bits",
              *(const DWORD *)&matrix[0], *(const DWORD *)&matrix[15]);
    }
    /* THE VIEW SAMPLE, 2026-07-28. The 50 m guard logs only its FIRST refusal, so a run that
       refuses in the menu and would have agreed in the city is indistinguishable from one that
       never agrees at all - and the first run past the seam refused with the engine's eye at
       (-0,-0), which is what an identity matrix looks like, i.e. exactly the menu. Sample the
       pair across the whole run instead of trusting one reading taken at the worst moment.
       Placed BEFORE the `g_enabled` test on purpose: a stray key press that disables the mod
       must not also blind the measurement. */
    /* ---- ROUTE 2's BLIND SPOT, closed here, 2026-07-30 ---------------------------------------
     * The detour's own trace samples only the frames where we WROTE, so it cannot see the
     * frames under suspicion. This is the other end of the pipe: the matrix that actually
     * reaches the device. Recover the eye from it and log it against the frame number, and the
     * question answers itself - if a frame carries two eyes about 1.37 m apart, the picture is
     * alternating between our seat and the engine's chase camera, which is the measured gap
     * between `want` and the engine's own row and exactly the shake being described.
     * Same window as the detour's trace, so the two are directly comparable. */
    /* ---- THE OUTLIER DETECTOR, 2026-07-30 ----------------------------------------------------
     * Single popped frames were reported: for one frame the steering wheel appears, then it is
     * gone again. A fixed 134-frame window is the wrong instrument for that - it samples 2.2
     * seconds and reports everything, when what is wanted is the whole drive and only the
     * anomalies. Three windows in a row measured the frames where everything works.
     *
     * So: every live VIEW pass, for the whole session, compared against the eye we last wrote.
     * Only departures are logged, capped. If the eye on a popped frame sits ~1.37 m back, that
     * is the engine's chase camera reaching the screen - and 1.37 m back is exactly where the
     * steering wheel comes into view.
     *
     * The rotation gets the same treatment: a frame-to-frame jump in the forward axis far
     * larger than the recent one is a popped AIM rather than a popped position, and the two are
     * different bugs. Both are counted, so "no anomalies at all" is also a reportable answer.
     */
    if (g_route == 2 && g_haveLastWant && state == D3DTS_VIEW && matrix
        && !(matrix[12] == 0.0f && matrix[14] == 0.0f)) {
        static volatile LONG liveN = 0, popN = 0, spinN = 0;
        static float prevFwd[3]; static int havePrev = 0;
        float eye[3];
        CamEyeFromView(matrix, eye);
        liveN++;
        float ex = eye[0] - g_lastWant[0], ey = eye[1] - g_lastWant[1], ez = eye[2] - g_lastWant[2];
        float d2 = ex*ex + ey*ey + ez*ez;
        if (d2 > 0.09f && popN < 60) {          /* 30 cm - far past any rounding */
            popN++;
            FpLog("POP: the eye at the device is NOT where we wrote it. frame, pop number",
                  (DWORD)g_frames, (DWORD)popN);
            FpLog("  ...eye x,z bits", *(DWORD *)&eye[0], *(DWORD *)&eye[2]);
            FpLog("  ...we wrote x,z bits", *(DWORD *)&g_lastWant[0], *(DWORD *)&g_lastWant[2]);
            FpLog("  ...miss distance squared bits, live VIEW passes so far",
                  *(DWORD *)&d2, (DWORD)liveN);
        }
        /* ---- AND THE JUMP THE POP DETECTOR CANNOT SEE ----------------------------------------
         * The test above asks whether the screen shows what was written. It answers yes, always -
         * and it is therefore blind to a jump WE caused, because then the screen agrees with us
         * perfectly and the camera still teleported. Three measurements in a row have now been
         * consistency checks between our own numbers.
         * This one asks the only question the eye actually cares about: did the camera move
         * further in one frame than it has been moving? A running mean of the step, and a flag
         * when a step is far past it. It does not care who caused the jump, which is the point.
         */
        {
            static float prevEye[3]; static int havePrevEye = 0;
            static float meanStep = 0.0f; static LONG stepN = 0;
            static volatile LONG jumpN = 0;
            if (havePrevEye) {
                float jx = eye[0]-prevEye[0], jy = eye[1]-prevEye[1], jz = eye[2]-prevEye[2];
                float j2 = jx*jx + jy*jy + jz*jz;
                float thr = meanStep * 3.0f;
                if (thr < 0.25f) thr = 0.25f;            /* never flag ordinary slow driving */
                if (stepN > 30 && j2 > thr*thr && jumpN < 60) {
                    jumpN++;
                    FpLog("JUMP: the eye moved much further in one frame than it had been. "
                          "frame, jump number", (DWORD)g_frames, (DWORD)jumpN);
                    FpLog("  ...this step squared, the running mean step bits",
                          *(DWORD *)&j2, *(DWORD *)&meanStep);
                    FpLog("  ...eye now x,z bits", *(DWORD *)&eye[0], *(DWORD *)&eye[2]);
                    FpLog("  ...eye before x,z bits", *(DWORD *)&prevEye[0], *(DWORD *)&prevEye[2]);
                } else {
                    /* The mean is only fed by NON-flagged steps, so one teleport cannot raise
                       the bar and hide the next one. */
                    float s = j2 > 0.0f ? j2 : 0.0f;
                    /* cheap sqrt-free running mean of the squared step, then compared squared */
                    float st = s;
                    for (int i = 0; i < 8; i++) st = 0.5f * (st + (s / (st > 0.0001f ? st : 1.0f)));
                    meanStep += (st - meanStep) * 0.05f;
                    stepN++;
                }
            }
            prevEye[0]=eye[0]; prevEye[1]=eye[1]; prevEye[2]=eye[2]; havePrevEye = 1;
        }

        /* VIEW's third COLUMN is the forward axis of a row-vector view matrix. */
        float fwd[3] = { matrix[2], matrix[6], matrix[10] };
        if (havePrev) {
            float a = fwd[0]-prevFwd[0], b = fwd[1]-prevFwd[1], c = fwd[2]-prevFwd[2];
            float s2 = a*a + b*b + c*c;
            if (s2 > 0.01f && spinN < 40) {     /* ~5.7 degrees in one frame */
                spinN++;
                FpLog("SPIN: the view direction jumped in one frame. frame, spin number",
                      (DWORD)g_frames, (DWORD)spinN);
                FpLog("  ...jump squared bits, live VIEW passes so far", *(DWORD *)&s2, (DWORD)liveN);
            }
        }
        prevFwd[0]=fwd[0]; prevFwd[1]=fwd[1]; prevFwd[2]=fwd[2]; havePrev = 1;
        if ((liveN % 1000) == 0)
            FpLog("live VIEW passes, pops, spins so far", (DWORD)liveN,
                  (DWORD)((popN << 16) | spinN));
    }

    if (g_route == 2 && g_traceArmed && state == D3DTS_VIEW && matrix) {
        static volatile LONG vtrace = 0;
        if (++vtrace <= 900) {
            float eye[3];
            CamEyeFromView(matrix, eye);
            FpLog("  VIEWTRACE frame, sample", (DWORD)g_frames, (DWORD)vtrace);
            FpLog("   ...eye at the device, x,z bits", *(DWORD *)&eye[0], *(DWORD *)&eye[2]);
            /* The raw row too: a rotation-only pass has an all-zero translation, and a
               recovered eye of (0,0) from such a pass is not a camera at the world origin -
               it is a pass that carries no camera at all. Telling those apart in the log
               rather than in my head is the whole lesson of the first three camera runs. */
            FpLog("   ...raw VIEW row m30,m32 bits",
                  *(const DWORD *)&matrix[12], *(const DWORD *)&matrix[14]);
        }
    }

    if (state == D3DTS_VIEW && matrix) {
        static volatile LONG viewN = 0;
        LONG v = viewN++;
        if (v < 3 || (v % 900) == 0) {
            float eye[3]; CamPose s;
            CamEyeFromView(matrix, eye);
            FpLog("  VIEW sample - the engine's eye x,z bits",
                  *(DWORD *)&eye[0], *(DWORD *)&eye[2]);
            /* THE RAW TRANSLATION ROW, 2026-07-28. The recovered eye came back as (+0,-0) in the
               city, and a mix of positive and negative zero is what negating structural zeros
               looks like - so the reading to trust is the row itself, not the arithmetic done on
               it. If m30..m32 are zero the engine does not put the camera position in VIEW at
               all, and substituting an absolute eye here would translate the whole world by two
               kilometres rather than move the camera. */
            FpLog("   ...VIEW translation row m30,m31 bits",
                  *(const DWORD *)&matrix[12], *(const DWORD *)&matrix[13]);
            FpLog("   ...VIEW m32 bits, and m00 to prove the matrix is real",
                  *(const DWORD *)&matrix[14], *(const DWORD *)&matrix[0]);
            /* THE ENGINE'S OWN CAMERA POSITION, and it is needed because the world arrives
               camera-relative: the substitution can only be a DELTA from where the engine's eye
               already is, and VIEW does not carry it. From the disassembly at 10062e1f, which is
               the same function that sends this matrix, twenty instructions earlier:
                   camera = *(LS3DF+0x1C4CF8); if (camera->flags & 0x20) UpdateWMatrix(camera);
                   0x101c4c60/64/68 = *(float[3])(camera + 0x40)
               Three consecutive floats copied as a unit right after the frame's world matrix is
               refreshed - a 4x4 at camera+0x10 puts its translation row exactly on +0x40. Both
               the global and the object are read here, because agreeing readings from two places
               is what turns a plausible offset into a measured one. The test is cheap and
               external: a chase camera must sit METRES from the car, not kilometres. */
            {
                HMODULE ls = GetModuleHandleA("LS3DF.dll");
                if (ls) {
                    const float *g = (const float *)((BYTE *)ls + 0x1C4C60u);
                    void *camObj = *(void **)((BYTE *)ls + 0x1C4CF8u);
                    FpLog("   ...engine camera via the GLOBAL, x,z bits",
                          *(const DWORD *)&g[0], *(const DWORD *)&g[2]);
                    if (camObj) {
                        const float *c = (const float *)((BYTE *)camObj + 0x40u);
                        FpLog("   ...engine camera via camera+0x40, x,z bits",
                              *(const DWORD *)&c[0], *(const DWORD *)&c[2]);
                        FpLog("   ...and its height y bits, from global and from the object",
                              *(const DWORD *)&g[1], *(const DWORD *)&c[1]);
                    }
                }
            }
            if (CarAnchor(s.pos, s.basis)) {
                FpLog("   ...and the car anchor x,z bits", *(DWORD *)&s.pos[0],
                      *(DWORD *)&s.pos[2]);
                FpLog("   ...car anchor height y bits, sample number",
                      *(DWORD *)&s.pos[1], (DWORD)v);
            } else {
                FpLog("   ...and there was NO car anchor at all, sample number", 0, (DWORD)v);
            }
        }
    }

    /* THE OTHER HALF OF THE SAME QUESTION. If VIEW carries no camera position, the eye has been
       folded into the WORLD matrices instead - the engine sets state 0x100, D3DTS_WORLDMATRIX(0),
       in the same breath. Its translation row against the car anchor says which convention this
       engine uses, and that decides whether the substitution is an absolute eye or a delta. */
    /* ---- IS IT THE CAMERA SHAKING, OR THE CAR SHAKING IN FRONT OF IT? -----------------------
     * The eye reaching the device is ours to a millimetre, so the position is not what moves.
     * From inside a cabin, a dashboard sliding back and forth in front of a still eye looks
     * exactly like a camera sliding back and forth - and we anchor to the car's PHYSICS
     * position while the body is drawn from whatever the render path uses.
     *
     * Every object's WORLD matrix passes through here on its way to being drawn. The car's is
     * the one whose translation sits within a few metres of our anchor, so that is the filter -
     * crude, and it does not need to be better: a nearby lamp post does not move, so anything
     * in this window that MOVES WITH US is the car. If its distance from our anchor is constant
     * the two are rigid and this candidate is dead; if it breathes, that breathing IS the shake.
     */
    if (g_route == 2 && g_traceArmed && state == 0x100u && matrix && g_haveLastWant) {
        static volatile LONG cartrace = 0;
        float dx = matrix[12] - g_lastWant[0];
        float dy = matrix[13] - g_lastWant[1];
        float dz = matrix[14] - g_lastWant[2];
        if (dx*dx + dy*dy + dz*dz < 25.0f && ++cartrace <= 900) {
            FpLog("  CARTRACE frame, sample", (DWORD)g_frames, (DWORD)cartrace);
            FpLog("   ...drawn body x,z bits",
                  *(const DWORD *)&matrix[12], *(const DWORD *)&matrix[14]);
            FpLog("   ...our anchor x,z bits at the same moment",
                  *(DWORD *)&g_lastWant[0], *(DWORD *)&g_lastWant[2]);
        }
    }

    if (state == 0x100u && matrix) {
        static volatile LONG worldN = 0;
        LONG w = worldN++;
        if (w < 2 || (w % 3000) == 0) {
            FpLog("  WORLD(0) sample - translation row m30,m31 bits",
                  *(const DWORD *)&matrix[12], *(const DWORD *)&matrix[13]);
            FpLog("   ...m32 bits, sample number",
                  *(const DWORD *)&matrix[14], (DWORD)w);
        }
    }

    /* `g_route != 1` keeps the two seams from both writing. The diagnostics above still run under
       route 2 on purpose - the VIEW samples are how route 2's effect gets MEASURED rather than
       looked at, since a row written upstream must show up in the matrix that arrives here. */
    if (state != D3DTS_VIEW || !g_enabled || !matrix || !g_abOn || g_route != 1)
        return g_realSetXform(self, state, matrix);

    /* Stage 1, the anchor. Position only: engine rotation is kept, so `haveBasis`
       feeds CamSeat and nothing else - see fp_pose.h for why that is a flag rather than a
       second code path. A failed anchor means on foot, a menu, or a dead node; the engine's
       own matrix goes through untouched, which is the stock picture. */
    CamPose p;
    if (!CarAnchor(p.pos, p.basis))
        return g_realSetXform(self, state, matrix);
    p.haveBasis = 1;
    /* The first frame a car exists is the first frame we are demonstrably in a mission, and it
       is what the A/B windows are anchored to. Set once. */
    if (!g_cityFrame) {
        g_cityFrame = g_frames;
        FpLog("the car anchor resolved for the first time - A/B windows start here, frame",
              (DWORD)g_cityFrame, 0);
    }

    float seat[3];
    seat[0] = (float)g_seatSideCm * 0.01f;   /* + right, - left */
    seat[1] = (float)(g_seatUpCm + g_stopIdx * FP_STOP_CM) * 0.01f;
    seat[2] = (float)g_seatFwCm * 0.01f;
    CamSeat(&p, seat);

    /* THE SUBSTITUTION IS A DELTA, NOT AN ABSOLUTE EYE - measured 2026-07-28, and it is the
       whole difference between a first-person camera and a picture thrown two kilometres.
       In a loaded scene this engine sends VIEW with an ALL-ZERO translation row and subtracts
       the camera position itself when it builds each object's WORLD matrix, so the geometry
       arrives already camera-relative. Writing an absolute eye into VIEW would therefore
       translate the entire world by the car's world position. What actually moves the camera is
       the offset FROM the engine's own eye - and at delta zero this reduces to exactly the
       matrix the engine sent, which is the property that makes it safe to ship.
       The engine's eye is not in the matrix; it is at camera+0x40, proven in the same run by
       the menu, where VIEW still carries a translation and the recovered eye matched that field
       BIT FOR BIT. */
    float engineEye[3];
    if (!FpEngineEye(engineEye))
        return g_realSetXform(self, state, matrix);

    /* THE TWO SOURCES DO NOT AGREE ON AXIS ORDER, and the numbers are not close enough for that
       to be a matter of taste: the same instant read the camera at (-1984.6, -3.37, 22.53) and
       the car anchor at (23.40, -4.83, -1984.54). Components 0 and 2 are swapped between them,
       which is unambiguous at a thousand-to-one separation. The delta must be computed in the
       ENGINE's order, because the rotation it is about to be multiplied by is the engine's.
       Owed: a confirmation on a second map - one reading cannot tell a swap from a coincidence
       of one particular spot, even an implausible one. */
    float want[3];
    want[0] = p.pos[2];
    want[1] = p.pos[1];
    want[2] = p.pos[0];

    float delta[3];
    delta[0] = want[0] - engineEye[0];
    delta[1] = want[1] - engineEye[1];
    delta[2] = want[2] - engineEye[2];

    /* THE CONTROL, 2026-07-28. The first substituted run moved the eye by 0.8 m and produced
       nothing but skybox - no floor, no ceiling, no other cars. A 0.8 m step cannot remove the
       world, so the fault is not the size of the delta, and the two candidates - that writing this
       row at all is wrong, and that the row is right but the arithmetic is not - are not
       distinguishable by looking harder at the picture.
       `delta_pct` scales the delta. At 0 the substitution still happens, still writes the row,
       still goes through every line of this path, and is numerically IDENTICAL to the matrix the
       engine sent. A run at 0 therefore separates the two candidates outright: a clean picture
       blames the arithmetic, an empty one blames the write. Default 0 so no ini gets a surprise
       camera; the value is a percentage because GetPrivateProfileInt cannot read a float. */
    {
        float k = (float)g_deltaPct * 0.01f;
        delta[0] *= k; delta[1] *= k; delta[2] *= k;
    }

    /* THE GUARD. Unchanged in purpose - if the point we computed is not in the same postcode as
       the engine's own eye, something is wrong (the wrong car, a cutscene camera, a bad offset)
       and substituting would teleport the picture. Refusing shows up as first person doing
       nothing, teleporting is reported as the mod being broken, and only one of those is
       diagnosable. It is now applied to the delta, which is the same test stated directly. */
    if (delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2] > 50.0f * 50.0f) {
        if (!g_farLogged) {
            g_farLogged = 1;
            FpLog("the eye we want is more than 50 m from the engine's - NOT substituting. "
                  "delta x,z bits", *(DWORD *)&delta[0], *(DWORD *)&delta[2]);
            FpLog("  delta height bits, and the engine's eye x bits",
                  *(DWORD *)&delta[1], *(DWORD *)&engineEye[0]);
        }
        return g_realSetXform(self, state, matrix);
    }
    g_farLogged = 0;

    /* NOT ON THE STACK - THIS IS THE BUG, and it took three runs to corner because the matrix
       was never wrong. Measured 2026-07-28, in order:
         - `delta_pct = 0` builds a matrix NUMERICALLY IDENTICAL to the engine's, and the picture
           was still nothing but sky. So the contents are innocent.
         - 16-byte alignment changed nothing, so it is not an aligned SSE load either.
         - `pass_original = 1` runs every line of this path and hands the engine back its OWN
           pointer: the city returns, intact.
       Three readings, one conclusion: what breaks is handing on a pointer to memory that DIES
       when this function returns. Something downstream keeps the pointer and reads the matrix
       later, so by the time it is used the stack frame has been overwritten by whatever ran
       next - which is exactly why the result looked random rather than shifted.
       Static, therefore, and aligned because it costs nothing to keep. Rendering is single
       threaded here; if that ever stops being true this becomes per-thread, not per-call. */
    static __attribute__((aligned(16))) float out[16];
    CamViewOffset(matrix, delta, out);

    /* THE POINTER TEST. `delta_pct = 0` already showed the picture breaks while the matrix is
       numerically identical to the engine's, and 16-byte alignment did not save it - so the
       remaining suspects are handing on a different pointer and running this path at all
       (CarAnchor walks the engine's pointer chain every frame; reading is supposed to be free,
       and supposed-to-be is what measurements are for). With pass_original = 1 everything above
       still runs and the engine gets ITS OWN matrix back, which separates the two outright. */
    if (g_passOriginal)
        return g_realSetXform(self, state, matrix);
    if (!g_subLogged) {
        g_subLogged = 1;
        FpLog("substituting the view - first person is LIVE. delta x,z bits",
              *(DWORD *)&delta[0], *(DWORD *)&delta[2]);
        FpLog("  delta height bits, and the translation row we wrote, m30 bits",
              *(DWORD *)&delta[1], *(DWORD *)&out[12]);
        /* THE FULL DUMP, once. Nothing but skybox was seen at a delta of 0.8 m, and the A/B
           windows then showed the geometry still being drawn in the same quantity - so the world
           is submitted and not culled, and the remaining question is purely whether this matrix
           is sane. That question can be answered away from the bench: both matrices, in full,
           are enough to project a known point through each and compare. Sixteen floats is eight
           lines and it is written exactly once, which is cheaper than another launch. */
        for (int i = 0; i < 16; i += 2)
            FpLog("  VIEW in  - two floats", *(const DWORD *)&matrix[i],
                  *(const DWORD *)&matrix[i + 1]);
        for (int i = 0; i < 16; i += 2)
            FpLog("  VIEW out - two floats", *(DWORD *)&out[i], *(DWORD *)&out[i + 1]);
    }
    return g_realSetXform(self, state, out);
}

/* WINDOW THE GAME SO A FRAME CAN BE CAPTURED - the technique is borrowed from work done on another build, which has been
 * screenshotting this engine successfully for weeks (`ApplyForceRes` in their `src\mafia_vr.c`).
 * A GDI grab of a FULLSCREEN EXCLUSIVE Direct3D app returns black or the desktop behind it, so
 * `playtest.ps1` here has capture switched off with a comment saying exactly that - correct, but
 * it reads as if capture is impossible when the real statement is that capture needs a window.
 *
 * D3DPRESENT_PARAMETERS by OFFSET, because this file builds -nostdlib with no d3d8.h. The D3D8
 * layout is fixed by the COM ABI:
 *   0x00 BackBufferWidth   0x04 BackBufferHeight   0x1C Windowed
 *   0x2C FullScreen_RefreshRateInHz                0x30 FullScreen_PresentationInterval
 *
 * THREE TRAPS COME WITH THE TECHNIQUE, and all three are theirs, already paid for:
 *   1. a windowed swap chain only accepts DEFAULT/ONE/IMMEDIATE as the presentation interval.
 *      The fullscreen-only divisors (TWO/THREE/FOUR) make the real CreateDevice return
 *      D3DERR_INVALIDCALL and the whole override fails SILENTLY. DEFAULT (0) is written here.
 *   2. the refresh rate must be 0 when windowed.
 *   3. a window LARGER THAN THE DESKTOP gives a black or corner-cropped frame, and every picture
 *      measured under it is suspect. Monitors are swapped between projects, so this is checked
 *      and logged rather than assumed.
 * Their fourth - Reset() hands the game's own unforced parameters back - is not handled here:
 * this is a diagnostic switch that is off by default, and a run that alt-tabs is void anyway.
 */
static void FpWindowTheGame(void *pp)
{
    if (!pp || g_winW <= 0 || g_winH <= 0) return;
    UINT *u = (UINT *)pp;
    FpLog("force window: the size the game asked for", u[0], u[1]);

    DEVMODEA dm;
    for (int i = 0; i < (int)sizeof(dm); i++) ((BYTE *)&dm)[i] = 0;
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        if ((DWORD)g_winW > dm.dmPelsWidth || (DWORD)g_winH > dm.dmPelsHeight)
            FpLog("force window: WARNING it EXCEEDS the desktop - the frame will be cropped or "
                  "black and any picture from this run is suspect. desktop w,h",
                  dm.dmPelsWidth, dm.dmPelsHeight);
        else
            FpLog("force window: it fits the desktop w,h", dm.dmPelsWidth, dm.dmPelsHeight);
    }

    u[0]  = (UINT)g_winW;          /* BackBufferWidth  */
    u[1]  = (UINT)g_winH;          /* BackBufferHeight */
    u[7]  = 1u;                    /* Windowed = TRUE  */
    u[11] = 0u;                    /* FullScreen_RefreshRateInHz must be 0 when windowed */
    u[12] = 0u;                    /* D3DPRESENT_INTERVAL_DEFAULT - the only safe legal value */
    FpLog("force window: now windowed at w,h", u[0], u[1]);
}

static HRESULT WINAPI FpCreateDevice(void *self, UINT adapter, DWORD devType, HWND focus,
                                     DWORD behaviour, void *presentParams, void **outDev)
{
    FpWindowTheGame(presentParams);
    HRESULT hr = g_realCreateDev(self, adapter, devType, focus, behaviour,
                                 presentParams, outDev);
    if (hr < 0 || !outDev || !*outDev) {
        FpLog("CreateDevice failed or returned no device - not hooking", (DWORD)hr, 0);
        return hr;
    }
    if (g_realSetXform) {          /* Reset() and multi-device paths must not double-hook */
        FpLog("CreateDevice again - SetTransform already hooked, leaving it", 0, 0);
        return hr;
    }

    /* BEFORE the first hook, or our own detours would be counted as part of the vtable we are
       trying to identify. */
    FpPickD3DRef(*outDev);

    void *prev = FpHookVTable(*outDev, IDIRECT3DDEVICE8_SETTRANSFORM, (void *)FpSetTransform);
    if (!prev) {
        FpLog("could NOT write the device vtable - camera is inert this session", 0, 0);
        return hr;
    }
    if (!FpSameModuleAsD3D(prev)) {
        /* Refuse THIS SLOT rather than proceed with it. Something else owns it, or the index is
           wrong; either way calling `prev` blind would be calling an unknown function. The log
           carries BOTH modules, because "refused" without the two numbers being compared is a
           dead end for whoever reads it - which is exactly how the first version of this guard
           cost a launch. */
        FpHookVTable(*outDev, IDIRECT3DDEVICE8_SETTRANSFORM, prev);
        FpLog("vtable slot 37 is not in the D3D module - REFUSED. entry module, d3d module",
              (DWORD)(DWORD_PTR)FpModuleOf(prev),
              (DWORD)(DWORD_PTR)FpModuleOf((void *)g_realCreate8));
        /* WHO IT IS, by name. The two bases alone identified nothing on the 2026-08-12 run and
           cost the whole question a day: an overlay, a shim or a wrapper all look like a number. */
        {
            char who[MAX_PATH];
            HMODULE m = (HMODULE)FpModuleOf(prev);
            if (m && GetModuleFileNameA(m, who, MAX_PATH))
                FpLogStr("  ...the module that owns slot 37 is", who);
        }
        /* AND THAT IS ALL IT COSTS US. Until 2026-08-12 this returned, which took the draw hooks
           and Present down with it - slots that have nothing to do with SetTransform and carry
           their own per-slot check three lines below. The result was measured on run 013: the
           interface squeeze never ran, the hud probe wrote nothing, and Alex pressed INSERT
           twenty-two times at a mod that had no draw hook installed. A guard may refuse the thing
           it guards; it may not refuse the things it was never asked about.
           SetTransform stays the foreign module's - g_realSetXform is left NULL and the per-frame
           watch is told not to take the slot back (g_xformOwned). */
        g_device = *outDev;
        g_vtable = *(void ***)g_device;
        goto probe_slots;
    }
    g_device = *outDev;
    g_realSetXform = (SetTransform_t)prev;
    g_xformOwned = 1;
    g_vtable = *(void ***)g_device;   /* the baseline the per-frame watch compares against */
    {
        HMODULE ls = GetModuleHandleA("LS3DF.dll");
        if (ls) g_engineDevPrev = *(void **)((BYTE *)ls + 0x1C597Cu);
    }
    FpLog("SetTransform hooked, original at", (DWORD)(DWORD_PTR)prev, 0);
    FpLog("  the vtable being watched, and the engine's device global now",
          (DWORD)(DWORD_PTR)g_vtable, (DWORD)(DWORD_PTR)g_engineDevPrev);

probe_slots:
    ;
    struct { int idx; void *fn; void **orig; const char *name; } probes[] = {
        { IDIRECT3DDEVICE8_BEGINSCENE,        (void *)FpBeginScene,   &g_realBeginScene,  "BeginScene" },
        { IDIRECT3DDEVICE8_DRAWPRIM,          (void *)FpDrawPrim,     &g_realDrawAlt[0],  "DrawPrimitive" },
        { IDIRECT3DDEVICE8_DRAWPRIMUP,        (void *)FpDrawPrimUP,   &g_realDrawAlt[1],  "DrawPrimitiveUP" },
        { IDIRECT3DDEVICE8_DRAWINDEXEDPRIMUP, (void *)FpDrawIdxPrimUP,&g_realDrawAlt[2],  "DrawIndexedPrimitiveUP" },
        { IDIRECT3DDEVICE8_DRAWINDEXEDPRIM, (void *)FpDraw,   (void **)&g_realDraw,   "DrawIndexedPrimitive" },
        { IDIRECT3DDEVICE8_SETVERTEXSHADER, (void *)FpSetVS,  (void **)&g_realSetVS,  "SetVertexShader" },
        { IDIRECT3DDEVICE8_SETVSCONSTANT,   (void *)FpSetVSC, (void **)&g_realSetVSC, "SetVertexShaderConstant" },
    };
    for (int i = 0; i < (int)(sizeof(probes)/sizeof(probes[0])); i++) {
        void *o = FpHookVTable(*outDev, probes[i].idx, probes[i].fn);
        if (o && FpSameModuleAsD3D(o)) *probes[i].orig = o;
        else { if (o) FpHookVTable(*outDev, probes[i].idx, o);
               /* WITH THE NAME. Seven bare "could not probe" lines were the whole record of the
                  2026-08-15 failure, and they said nothing about who owned the slot - so the one
                  install that was broken produced a log that could not explain it. */
               FpLog("could not probe a vtable slot - index, and the module that owns it",
                     (DWORD)probes[i].idx, (DWORD)(DWORD_PTR)FpModuleOf(o));
               { char who[MAX_PATH]; HMODULE m = (HMODULE)FpModuleOf(o);
                 if (m && GetModuleFileNameA(m, who, MAX_PATH))
                     FpLogStr("  ...which is", who); } }
    }

    void *pp = FpHookVTable(*outDev, IDIRECT3DDEVICE8_PRESENT, (void *)FpPresent);
    if (pp && FpSameModuleAsD3D(pp)) {
        g_realPresent = (Present_t)pp;
        FpLog("Present hooked as a liveness control, original at", (DWORD)(DWORD_PTR)pp, 0);
    } else {
        if (pp) FpHookVTable(*outDev, IDIRECT3DDEVICE8_PRESENT, pp);
        FpLog("could not hook Present - no liveness control this session", 0, 0);
    }
    return hr;
}

static void *WINAPI FpDirect3DCreate8(UINT sdkVersion)
{
    void *d3d = g_realCreate8 ? g_realCreate8(sdkVersion) : NULL;
    if (!d3d) {
        FpLog("Direct3DCreate8 returned NULL - nothing to hook", sdkVersion, 0);
        return d3d;
    }
    void *prev = FpHookVTable(d3d, IDIRECT3D8_CREATEDEVICE, (void *)FpCreateDevice);
    if (!prev) {
        FpLog("could NOT write the IDirect3D8 vtable - camera inert this session", 0, 0);
        return d3d;
    }
    if (!FpSameModuleAsD3D(prev)) {
        FpHookVTable(d3d, IDIRECT3D8_CREATEDEVICE, prev);
        FpLog("vtable slot 15 is not in the D3D module - REFUSED. entry module, d3d module",
              (DWORD)(DWORD_PTR)FpModuleOf(prev),
              (DWORD)(DWORD_PTR)FpModuleOf((void *)g_realCreate8));
        return d3d;
    }
    g_realCreateDev = (CreateDevice_t)prev;
    FpLog("IDirect3D8 created and CreateDevice hooked, original at",
          (DWORD)(DWORD_PTR)prev, (DWORD)(DWORD_PTR)d3d);
    return d3d;
}

/* ---------------------------------------------------------------- install -------------------
 * `LS3DF.dll` statically imports d3d8.dll!Direct3DCreate8, and our .asi is loaded by
 * dinput8.dll, which the loader is pulling in to satisfy LS3DF's OWN imports. So at this point
 * LS3DF is mapped but none of its code has run, and Direct3DCreate8 cannot have been called
 * yet. The slot is found by walking LS3DF's import directory - never by the RVA, which is
 * 0x9B254 on GOG and 0x9721C on the custom build (tests\offline\fp_iat_verify.c).
 */
static int FpInstall(void)
{
    HMODULE ls = GetModuleHandleA("LS3DF.dll");
    if (!ls) return 0;                         /* not mapped yet - the caller retries */

    void **slot = FpFindIatSlot(ls, "d3d8.dll", "Direct3DCreate8");
    if (!slot) {
        FpLog("LS3DF.dll does NOT import d3d8.dll!Direct3DCreate8 - this is not a build this "
              "mod understands, camera inert", 0, 0);
        return -1;                             /* fatal, do not retry */
    }

    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
        FpLog("could not make the IAT slot writable - camera inert",
              (DWORD)(DWORD_PTR)slot, 0);
        return -1;
    }
    g_realCreate8 = (Direct3DCreate8_t)*slot;
    *slot = (void *)FpDirect3DCreate8;
    VirtualProtect(slot, sizeof(void *), old, &old);
    g_iatSlot = slot;

    FpLog("IAT patched at LS3DF+", (DWORD)((BYTE *)slot - (BYTE *)ls),
          (DWORD)(DWORD_PTR)g_realCreate8);
    return 1;
}

/* ---------------------------------------------------------------- the scene graph -----------
 * THE HEAD. Confirmed 2026-07-30 that Tommy's head is in shot from the driver's eye, and
 * the route that was going to fix it died: the character is ONE `SNGMRPH` mesh, so there is no
 * head node whose visibility bit could be cleared. What was never established is what the STOCK
 * GOG data actually looks like - every node name we have came from the first-person mod's own
 * replacement models, on the OTHER install.
 *
 * This is the instrument for that question, and it is read-only. It walks the four scene roots
 * and writes one line per node: address, flags, type enum, four-character tag and name. If the
 * player's subtree turns out to hold more than `base` / `SNGMRPH` / `no_shadow`, the visibility
 * bit is back on the table; if it does not, the answer is settled with data instead of an
 * assumption, and the remaining routes are the near plane and the eye anchor.
 *
 * The chain it walks and every offset in it were read out of the listing - see the block at the
 * end of `src/shared/car_anchor.h`. NONE OF IT HAS RUN. The first drive is the measurement, and
 * the dump is written so that a wrong offset shows up as obvious rubbish (an unreadable name, a
 * type outside 1..15, a tag that is not four printable characters) rather than as a plausible
 * tree that is not the scene.
 */
#define FP_DUMP_MAX_NODES 20000
#define FP_DUMP_MAX_DEPTH 40

static void FpDumpNode(DWORD node, int depth, void *user)
{
    (void)user;
    if (!g_logReady) return;
    char line[320]; line[0] = 0;
    char nm[64];
    FrameName(node, nm, sizeof(nm));

    FpCat(line, sizeof(line), "F d=");
    FpCatHex(line, sizeof(line), (DWORD)depth);
    FpCat(line, sizeof(line), " 0x");
    FpCatHex(line, sizeof(line), node);
    FpCat(line, sizeof(line), " flg=0x");
    FpCatHex(line, sizeof(line), FrameField(node, FRAME_FLAGS_OFF, 0));
    FpCat(line, sizeof(line), " t=");
    FpCatHex(line, sizeof(line), FrameField(node, FRAME_TYPE_OFF, 0));
    FpCat(line, sizeof(line), " tag=");
    {
        DWORD tag = FrameField(node, FRAME_TAG_OFF, 0);
        char t[5];
        for (int i = 0; i < 4; i++) {
            char c = (char)((tag >> (i * 8)) & 0xFF);
            t[i] = (c >= 32 && c < 127) ? c : '.';
        }
        t[4] = 0;
        FpCat(line, sizeof(line), t);
    }
    FpCat(line, sizeof(line), " ");
    FpCat(line, sizeof(line), nm[0] ? nm : "-");
    FpCat(line, sizeof(line), "\r\n");

    EnterCriticalSection(&g_logLock);
    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD w;
        SetFilePointer(g_log, 0, NULL, FILE_END);
        WriteFile(g_log, line, (DWORD)FpLen(line), &w, NULL);
    }
    LeaveCriticalSection(&g_logLock);
}

/* The walk itself is `SceneWalkAll` in src/shared/scene_walk.h, where an offline test can reach
   it. What is here is only what to do with each node. */
static void FpDumpScene(void)
{
    DWORD scene = SceneObj();
    if (!scene) { FpLog("frame dump: no scene object - nothing walked", 0, 0); return; }
    FpLog("frame dump BEGINS, scene at", scene, (DWORD)g_frames);
    for (int r = 0; r < SCENE_NROOTS; r++) FpLog("  root", (DWORD)r, SceneRoot(scene, r));

    int total = SceneWalkAll(FpDumpNode, NULL, FP_DUMP_MAX_NODES, FP_DUMP_MAX_DEPTH);

    FlushFileBuffers(g_log);
    /* b=1 means the cap stopped it, so the tree is INCOMPLETE - the difference between nothing
       more existing and nothing more having been written has to be in the log, not in someone's memory. */
    FpLog("frame dump ENDS, nodes written", (DWORD)total,
          total >= FP_DUMP_MAX_NODES ? 1 : 0);
}

/* Clearing the bit is the whole of the hide, and re-asserting it is not optional: the engine
   sets these flags itself, so a single write is undone the next time the node is touched. Off
   unless `hide_frame` names a node, and it must match EXACTLY - a prefix match here would hide
   a subtree nobody asked for and the symptom would be missing scenery, not a missing head. */
static void FpHideVisit(DWORD node, int depth, void *user)
{
    (void)depth; (void)user;
    char nm[64];
    FrameName(node, nm, sizeof(nm));
    if (!nm[0]) return;
    for (int i = 0; ; i++) {
        if (nm[i] != g_hideName[i]) return;
        if (!nm[i]) break;
    }
    DWORD f = FrameField(node, FRAME_FLAGS_OFF, 0);
    if (!(f & FRAME_VISIBLE_BIT)) return;
    if (!CarReadable(node + FRAME_FLAGS_OFF, 4)) return;
    *(volatile DWORD *)(node + FRAME_FLAGS_OFF) = f & ~FRAME_VISIBLE_BIT;
    if (!g_hideSaid) {
        FpLog("hide_frame: cleared the drawn bit on the named node", node, f);
        g_hideSaid = 1;
    }
}

static void FpHideTick(void)
{
    if (!g_hideName[0]) return;
    SceneWalkAll(FpHideVisit, NULL, FP_DUMP_MAX_NODES, FP_DUMP_MAX_DEPTH);
}

/* The loader may still be writing that IAT while we patch it, so the patch is VERIFIED rather
   than assumed, and re-applied once if it was overwritten. Assuming here is how a mod ends up
   installed, silent, and doing nothing - the exact failure this project has already paid for. */
static DWORD WINAPI FpThread(LPVOID p)
{
    (void)p;
    FpIniPath();      /* must precede FpLogOpen - the log is gated on a key in that file */
    FpLogOpen();
    FpLoadIni();

    int rc = 0;
    for (int i = 0; i < 200 && rc == 0; i++) {     /* up to ~20 s for LS3DF to appear */
        rc = FpInstall();
        if (rc == 0) Sleep(100);
    }
    if (rc == 0) {
        FpLog("LS3DF.dll never appeared - camera inert this session", 0, 0);
        return 0;
    }
    if (rc < 0) return 0;

    Sleep(1000);
    if (g_iatSlot && *g_iatSlot != (void *)FpDirect3DCreate8) {
        FpLog("the IAT slot was overwritten after we patched it - re-applying",
              (DWORD)(DWORD_PTR)*g_iatSlot, 0);
        DWORD old;
        if (VirtualProtect(g_iatSlot, sizeof(void *), PAGE_READWRITE, &old)) {
            *g_iatSlot = (void *)FpDirect3DCreate8;
            VirtualProtect(g_iatSlot, sizeof(void *), old, &old);
        }
    } else if (g_iatSlot) {
        FpLog("IAT patch still in place one second later", 0, 0);
    }

    /* ---- route 2, and only if it was asked for ------------------------------------------
     * Installed AFTER the IAT work rather than instead of it: route 1's hooks also carry every
     * diagnostic this project has (the VIEW samples, the draw counters, the A/B windows), and
     * under route 2 those become the MEASUREMENT of route 2 rather than dead weight. Only the
     * substitution is exclusive, and that is settled by `g_route` at the one line in
     * FpSetTransform that writes.
     *
     * A refusal here is fatal to route 2 and to nothing else: the camera falls back to being a
     * mod that is installed and does nothing, which is the state it ships in anyway. */
    if (g_route == 2) {
        HMODULE ls2 = GetModuleHandleA("LS3DF.dll");
        if (ls2 && FpWMatrixInstall(ls2, (void *)FpUpdateWMatrix) > 0) {
            Sleep(1000);
            FpLog(FpWMatrixVerify()
                      ? "the UpdateWMatrixProc detour is still in place a second later"
                      : "the UpdateWMatrixProc detour was UNDONE within a second - route 2 is "
                        "not writing anything, and this is the line that says so",
                  (DWORD)(DWORD_PTR)g_updwSite, (DWORD)(DWORD_PTR)g_updwTrampMem);
        } else {
            FpLog("route 2 was requested and could NOT be installed - the camera is inert this "
                  "session. Nothing was written to LS3DF.", 0, 0);
        }
    }

    /* ---- the seat keys ----------------------------------------------------------------
     * Polled rather than hooked. A keyboard hook would put this mod in the same input path as
     * the gearbox module and the FFB mod, and three mods in one DirectInput chain is how the
     * gearbox spent eighteen minutes inert with nothing in its log. Polling costs a syscall
     * every 40 ms and cannot break anyone else's input.
     *
     * NOT F1-F12: the whole function row belongs to the FFB mod, the gearbox and an
     * other tools. Numpad by default, which is where a driver's free hand is, and both
     * keys are configurable because the VR project has not settled its own map yet and asked
     * that no key be treated as final.
     *
     * Edge-triggered, so holding a key steps once rather than sliding the seat out of the car.
     */
    int wasT = 0, wasH = 0;
    int upHeld = 0;
    int iniTick = 0;
    DWORDLONG iniStamp = FpIniStamp();      /* what FpLoadIni above has already applied */
    for (;;) {
        Sleep(40);

        /* ---- the settings file, once a second ----------------------------------------------
         * 25 polls at 40 ms. Sized to match the force feedback mod's own 1 Hz reload, so the two
         * mods notice a change at the same rate and the launcher can make one promise about both.
         *
         * A STAMP, not a re-parse: the timestamp and size say whether anything COULD have changed,
         * and thirty GetPrivateProfileInt calls a second on a file nobody has touched is work for
         * nothing. The mod's own FpSaveKey bumps that stamp too, so a numpad nudge costs one extra
         * read of the file it just wrote - and reports nothing, because FpLoadLive compares values
         * rather than trusting the stamp.
         *
         * Two writers on one file is a known shape here and this is not the place it is solved: if
         * the player nudges the seat while a slider is being dragged, the last write wins, and
         * both writes are the player's own. What must not happen is a WRITE from a value nobody
         * loaded, which is the defect the H-shifter page paid for on the same day. */
        if (++iniTick >= 25) {
            DWORDLONG now = FpIniStamp();
            iniTick = 0;
            if (now != iniStamp) {
                iniStamp = now;
                FpLoadLive(1);
            }
        }

        /* ---- the scene probe ---------------------------------------------------------------
         * `dump_frames=1` dumps ONCE, as soon as a scene exists - which is the moment a map is
         * loaded, not the moment a car is entered, because the player is on foot first and what
         * his own model is made of is the question. The key repeats it on demand: the same walk
         * standing on the street and sitting in the car answers whether the driver is parented
         * under the vehicle, and that is one keypress rather than a second run. */
        {
            static int wasDump = 0;
            int dk = (GetAsyncKeyState((int)g_keyDump) & 0x8000) != 0;
            if (g_dumpFrames && !g_dumped && SceneObj()) { g_dumped = 1; FpDumpScene(); }
            if (dk && !wasDump) FpDumpScene();
            wasDump = dk;
        }
        FpHideTick();

        /* ---- THE BENCH KEYS, and they exist ONLY on our bench --------------------------------
         * Requested 2026-08-12, after these two switches had been put on a tab in the utility:
         * the keys were to be bound to an F key, Insert or Page Up/Page Down instead, rather
         * than to controls in the interface. The utility ships to end users on GitHub, and
         * ad-hoc measurement switches do not belong in it.
         *
         * Two reasons, and the second is the one I keep having to relearn:
         *   - reaching a WINDOW means alt-tabbing, and Mafia pauses the moment it loses focus. The
         *     picture you are comparing stops being drawn exactly when you go to change it, and on
         *     a recorded run the capture is interrupted too.
         *   - the utility ships to strangers. A measurement aimed at our own bench is clutter in a
         *     client and one more switch for somebody to leave on.
         *
         * So: keys, and behind `dev_keys`. The shipped ini has 0; our benches have 1. With it off
         * this block cannot fire at all - the keys are not even polled, so no stray Insert in a
         * client's game can change what he sees.
         *
         *   INSERT     - the interface: corners stay put  <->  squeezed from the centre
         *   PAGE DOWN  - the interface measurement, on and off, taking the log with it
         *
         * Not F-keys: F1..F6 are the seat nudges and are bindable by the player. Insert and Page
         * Down are on his keyboard (he has no numpad - see the standing note) and Mafia binds
         * neither while driving.
         */
        if (g_devKeys) {
            static int wasIns = 0, wasPgDn = 0;
            int ins  = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
            int pgdn = (GetAsyncKeyState(VK_NEXT)   & 0x8000) != 0;
            if (ins && !wasIns) {
                g_hudAspectAnchored = !g_hudAspectAnchored;
                g_hudLogged = 0;    /* say the new mode once, next time a draw is rewritten */
                FpSaveKey("hud_aspect_anchored", g_hudAspectAnchored);
                FpLog("INSERT - interface anchoring is now (1 = corners stay put)",
                      (DWORD)g_hudAspectAnchored, 0);
            }
            wasIns = ins;
            if (pgdn && !wasPgDn) {
                g_hudProbe = !g_hudProbe;
                /* The row budget resets on every ARM, so a second measurement in one session
                   measures the second thing rather than finding the budget spent.
                   THE LOG CANNOT BE SWITCHED ON FROM HERE, and it would be a lie to pretend
                   otherwise: the file is opened once at startup and only when diag = 1, so a key
                   press cannot create it. That is why dev_keys and diag travel together in the
                   bench ini, and why the startup summary says so out loud when they disagree. */
                if (g_hudProbe) g_hudProbeRows = 0;
                FpLog("PAGE DOWN - interface measurement (1 = recording), rows so far",
                      (DWORD)g_hudProbe, (DWORD)g_hudProbeRows);
            }
            wasPgDn = pgdn;
        }

        /* A key of 0 means "no key" and must not reach GetAsyncKeyState - VK 0 is not a key,
           and polling it is how a retired control comes back as a phantom press. */
        int t = g_keyToggle && (GetAsyncKeyState((int)g_keyToggle) & 0x8000) != 0;
        int h = g_keyHeight && (GetAsyncKeyState((int)g_keyHeight) & 0x8000) != 0;

        /* ---- ARMING THE TRACE FROM THE WHEEL, design note 2026-07-30 ---------------------
         * For a run HE drives, there is no replay to anchor to and the 5 m displacement trigger
         * is the wrong instrument: he skips the intros and loads the city himself, at his own
         * pace, and the car may roll before he means to start. His rule instead: holding the up
         * arrow IS the statement "I am driving now". So that is the trigger.
         *
         * HELD, not pressed. A tap is menu navigation - the same arrow walks the load dialogs he
         * is about to click through - and arming on a tap would open the window inside a menu
         * and spend it there, which is the mistake three earlier trace windows already made in
         * three different ways. Six polls at 40 ms is ~240 ms: longer than any menu keypress,
         * shorter than the shortest deliberate hold.
         *
         * Latched: once armed it stays armed. Lifting off to change gear must not close it. */
        if (g_traceArm == 2 && !g_traceArmed) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                if (++upHeld >= 6) {
                    g_traceArmed = 1;
                    FpLog("TRACE ARMED - the up arrow was held, so the drive has started. "
                          "polls held, frame", (DWORD)upHeld, (DWORD)g_frames);
                }
            } else {
                upHeld = 0;
            }
        }

        /* ---- THE LIVE SEAT NUDGE, 2026-07-31 -----------------------------------------------
         * Edge-triggered like everything else in this loop: held down, a level trigger would
         * step 25 times a second and put the eye outside the car before he let go.
         * Every change is saved immediately, one key at a time, through FpSaveKey - so the
         * position he stops on is readable out of the ini afterwards and nobody has to
         * transcribe numbers from a spoken description again. */
        {
            const LONG nvk[14] = { g_keySeatUp, g_keySeatDown, g_keySeatFwd, g_keySeatBack,
                                   g_keySeatLeft, g_keySeatRight, g_keyVerdictY, g_keyVerdictN,
                                   g_keyNearUp, g_keyNearDown, g_keyLockRoll,
                                   g_keyPitchUp, g_keyPitchDown, g_keyTakeView };
            static int wasN[14];
            for (int i = 0; i < 14; i++) {
                /* A retired control reads 0 here, and VK 0 is not a key - polling it is how a
                   removed binding comes back as a phantom press. */
                int down = nvk[i] && (GetAsyncKeyState((int)nvk[i]) & 0x8000) != 0;
                if (down && !wasN[i]) {
                    LONG s = g_nudgeCm;
                    switch (i) {
                    case 0: g_seatUpCm   += s; FpSaveKey("seat_up_cm",      g_seatUpCm);   break;
                    case 1: g_seatUpCm   -= s; FpSaveKey("seat_up_cm",      g_seatUpCm);   break;
                    case 2: g_seatFwCm   += s; FpSaveKey("seat_forward_cm", g_seatFwCm);   break;
                    case 3: g_seatFwCm   -= s; FpSaveKey("seat_forward_cm", g_seatFwCm);   break;
                    case 4: g_seatSideCm -= s; FpSaveKey("seat_side_cm",    g_seatSideCm); break;
                    case 5: g_seatSideCm += s; FpSaveKey("seat_side_cm",    g_seatSideCm); break;
                    case 6: FpLog("VERDICT: he LIKES this camera - up cm, forward cm",
                                  (DWORD)g_seatUpCm, (DWORD)g_seatFwCm);
                            FpLog("   ...side cm, and this is the position to keep",
                                  (DWORD)g_seatSideCm, 0);
                            /* AND WHERE IT WAS AIMED. Without this line the previous drive's 23
                               likes could say what the seat was and nothing about the pitch,
                               which is the half he actually complained about. */
                            FpLog("   ...pitch_deg trim, and forward y bits AS DRAWN",
                                  (DWORD)g_pitchDeg, (DWORD)g_lastFwdY);
                            break;
                    case 7: FpLog("VERDICT: he does NOT like this camera - up cm, forward cm",
                                  (DWORD)g_seatUpCm, (DWORD)g_seatFwCm);
                            FpLog("   ...side cm, rejected", (DWORD)g_seatSideCm, 0);
                            FpLog("   ...pitch_deg trim, and forward y bits AS DRAWN",
                                  (DWORD)g_pitchDeg, (DWORD)g_lastFwdY);
                            break;
                    /* The near plane, live. Starts from 10 cm rather than 0 on the first press,
                       because 0 means "leave the engine's" and stepping up from it would take
                       five presses to reach anything visible. Capped at 100 cm - past that the
                       dashboard and the bonnet start disappearing, which is not the goal. */
                    case 8: g_nearCm = (g_nearCm <= 0) ? 10 : g_nearCm + 2;
                            if (g_nearCm > 100) g_nearCm = 100;
                            FpSaveKey("near_cm", g_nearCm);
                            FpLog("near plane pushed OUT - cm", (DWORD)g_nearCm, 0);
                            break;
                    case 9: g_nearCm = (g_nearCm <= 10) ? 0 : g_nearCm - 2;
                            FpSaveKey("near_cm", g_nearCm);
                            FpLog("near plane pulled IN - cm (0 = the engine's own)",
                                  (DWORD)g_nearCm, 0);
                            break;
                    case 10: g_lockRoll = g_lockRoll ? 0 : 1;
                            FpSaveKey("lock_roll", g_lockRoll);
                            FpLog("LEVEL HORIZON toggled - 1 means roll is locked out",
                                  (DWORD)g_lockRoll, 0);
                            break;
                    /* The pitch trim, one degree a press. A degree rather than nudge_cm's step:
                       the seat moves in centimetres and the aim in degrees, and at this FOV one
                       degree is already a visible slice of horizon. Saved immediately like every
                       other live control, so the angle he stops on is in the file afterwards -
                       which is precisely what the last drive could not tell us. */
                    case 11: if (g_pitchDeg < FP_PITCH_MAX) g_pitchDeg++;
                            FpSaveKey("pitch_deg", g_pitchDeg);
                            FpLog("PITCH aimed UP - degrees, forward y bits as drawn",
                                  (DWORD)g_pitchDeg, (DWORD)g_lastFwdY);
                            break;
                    case 12: if (g_pitchDeg > -FP_PITCH_MAX) g_pitchDeg--;
                            FpSaveKey("pitch_deg", g_pitchDeg);
                            FpLog("PITCH aimed DOWN - degrees, forward y bits as drawn",
                                  (DWORD)g_pitchDeg, (DWORD)g_lastFwdY);
                            break;
                    /* TAKE THE VIEW THAT IS ON SCREEN. The one control in this loop that changes
                       WHICH of the game's six vehicle views is ours, and it exists because the
                       choice was made for him twice and was wrong both times.
                       Refused, loudly, in three cases rather than silently corrected - a key that
                       looks dead is a support question nobody can answer:
                         - the render detour has not published a mode yet (not in a car, or the
                           camera chain did not resolve), so there is nothing to take;
                         - the mode is outside the C ring, i.e. a cutscene or sniper camera that
                           the C key can never return to;
                         - view_mode is 0, "every view is ours", where taking one is meaningless. */
                    case 13: {
                            LONG m = g_liveMode;
                            if (g_viewMode == 0) {
                                FpLog("TAKE VIEW refused: view_mode is 0, every view is already "
                                      "ours - live mode", (DWORD)m, 0);
                            } else if (m != 7 && m != 8 && m != 9 && m != 12 && m != 13
                                       && m != 14) {
                                FpLog("TAKE VIEW refused: not one of the six vehicle views "
                                      "(7/8/9/12/13/14) - live mode, kept", (DWORD)m,
                                      (DWORD)g_viewMode);
                            } else {
                                LONG was = g_viewMode;
                                g_viewMode = m;
                                FpSaveKey("view_mode", m);
                                FpLog("TAKE VIEW: the mod now owns this slot - was, now",
                                      (DWORD)was, (DWORD)m);
                            }
                            break; }
                    }
                    if (i < 6) {
                        FpLog("seat nudged - up cm, forward cm",
                              (DWORD)g_seatUpCm, (DWORD)g_seatFwCm);
                        FpLog("   ...side cm (+ right, - left)", (DWORD)g_seatSideCm, 0);
                    }
                }
                wasN[i] = down;
            }
        }

        /* Edge-triggered, both of them. Held down, a level-triggered toggle flips 25 times a
           second and lands on whichever state the release happened to catch. */
        if (t && !wasT) {
            g_enabled = g_enabled ? 0 : 1;
            g_subLogged = 0;                 /* say "live" again on the next substitution */
            FpSaveKey("enabled", g_enabled);
            FpLog("FIRST PERSON toggled - enabled, height stop",
                  (DWORD)g_enabled, (DWORD)g_stopIdx);
        }
        if (h && !wasH) {
            g_stopIdx = (g_stopIdx + 1) % FP_STOPS;
            FpSaveKey("height_stop", g_stopIdx);
            FpLog("height stop now, total cm above the car",
                  (DWORD)g_stopIdx, (DWORD)(g_seatUpCm + g_stopIdx * FP_STOP_CM));
        }
        wasT = t; wasH = h;
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(NULL, 0, FpThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
