/* fp_iat.h - find the IAT slot a loaded module uses for one imported function.
 *
 * This is the whole foundation of the first-person camera's injection, so it is its own file
 * with its own offline test (tests/offline/fp_iat_verify.c) rather than a helper buried in the
 * hook.
 *
 * WHY A PARSER AND NOT A CONSTANT OR A SIGNATURE
 * ----------------------------------------------
 * The seam is `LS3DF.dll`'s import of `d3d8.dll!Direct3DCreate8`. On the deployed GOG build that
 * slot happens to sit at RVA 0x9B254 - and that number appears nowhere below. This project's
 * own camera rule asks that any surviving address be found by byte signature and
 * refuse loudly on a mismatch. Walking the module's own import directory is strictly better than
 * a signature: it is not an address at all, it is a lookup in a structure the loader itself uses,
 * so it is correct on every build of every version by construction. The same code finds the slot
 * on their `LS3DF.dll` as on ours, which is what the offline test demonstrates.
 *
 * WHAT IT DOES NOT DO
 * -------------------
 * It returns the ADDRESS OF THE SLOT, never the value in it. The caller decides what to write
 * and is responsible for VirtualProtect. That split is deliberate: locating is pure and testable
 * against a file on disk, patching is not.
 *
 * Nothing here allocates, calls the CRT, or touches anything outside the module's own headers,
 * so it compiles under the .asi's `-nostdlib` build unchanged.
 */

#ifndef FP_IAT_H
#define FP_IAT_H

#include <windows.h>

/* ASCII, case-insensitive, no CRT. Module names in an import directory are ASCII by
   specification, so a byte-wise fold is not an approximation here. */
static int FpEqI(const char *a, const char *b)
{
    for (;;) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static int FpEq(const char *a, const char *b)
{
    for (;;) {
        if (*a != *b) return 0;
        if (!*a) return 1;
        a++; b++;
    }
}

/* Guarded read of one dword-sized field inside the mapped image. A truncated or hostile PE must
   make this function return NULL, not fault: this runs inside the game's process at load time,
   and a fault here is a crash with no log. */
static int FpInImage(const BYTE *base, const void *p, SIZE_T need)
{
    MEMORY_BASIC_INFORMATION mbi;
    (void)base;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const BYTE *)p + need <= (const BYTE *)mbi.BaseAddress + mbi.RegionSize;
}

/* Returns the address of the IAT slot `mod` uses for `dllName!funcName`, or NULL.
 *
 * `mod` must be a MAPPED IMAGE - either a normally loaded module, or one mapped with
 * LoadLibraryEx(..., DONT_RESOLVE_DLL_REFERENCES), which is what the offline test uses. It must
 * not be a raw file read into memory: on disk the sections are file-aligned and every RVA below
 * would land in the wrong place.
 *
 * Both thunk arrays are handled. `OriginalFirstThunk` is the one that still holds names after
 * the loader has run, so it is preferred; a module built without it falls back to `FirstThunk`,
 * which only carries names while the imports are still unresolved. Ordinal-only imports are
 * skipped rather than guessed at - `Direct3DCreate8` is imported by name on every build seen.
 */
static void **FpFindIatSlot(HMODULE mod, const char *dllName, const char *funcName)
{
    BYTE *base = (BYTE *)mod;
    if (!base) return NULL;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (!FpInImage(base, dos, sizeof(*dos))) return NULL;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (!FpInImage(base, nt, sizeof(*nt))) return NULL;
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) return NULL;

    DWORD impRva = nt->OptionalHeader
                     .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!impRva) return NULL;

    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + impRva);
    for (; ; imp++) {
        if (!FpInImage(base, imp, sizeof(*imp))) return NULL;
        if (!imp->Name || !imp->FirstThunk) break;      /* the terminating zero descriptor */

        const char *name = (const char *)(base + imp->Name);
        if (!FpInImage(base, name, 1)) return NULL;
        if (!FpEqI(name, dllName)) continue;

        DWORD nameThunkRva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
        IMAGE_THUNK_DATA *nameThunk = (IMAGE_THUNK_DATA *)(base + nameThunkRva);
        IMAGE_THUNK_DATA *iatThunk  = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);

        for (; ; nameThunk++, iatThunk++) {
            if (!FpInImage(base, nameThunk, sizeof(*nameThunk))) return NULL;
            if (!FpInImage(base, iatThunk, sizeof(*iatThunk))) return NULL;
            if (!nameThunk->u1.AddressOfData) break;    /* end of this module's imports */
            if (IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal)) continue;

            IMAGE_IMPORT_BY_NAME *ibn =
                (IMAGE_IMPORT_BY_NAME *)(base + nameThunk->u1.AddressOfData);
            if (!FpInImage(base, ibn, sizeof(WORD) + 1)) return NULL;
            if (FpEq((const char *)ibn->Name, funcName))
                return (void **)&iatThunk->u1.Function;
        }
        /* the module imports that DLL but not that function - keep looking, a PE may legally
           carry two descriptors for the same name */
    }
    return NULL;
}

#endif /* FP_IAT_H */
