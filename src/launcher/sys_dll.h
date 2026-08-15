/* sys_dll.h - load a system DLL from SYSTEM32, never from the folder we are standing in.
 *
 * THE DEFECT THIS EXISTS FOR, found by clicking a toggle on 2026-08-05.
 *
 * He pressed "Disable this mod" on the H-shifter tab and got `could not remove
 * gearbox_hook.asi` / `0 operations undone`. The file was locked - as a loaded MODULE of the
 * launcher's own process.
 *
 * `ALXGtech Mafia 1 Mods.exe` is meant to live in the game folder. The game folder also holds
 * `dinput8.dll`, which is not Microsoft's - it is the Ultimate ASI Loader we install. Windows
 * searches the APPLICATION DIRECTORY before System32 for any DLL that is not a KnownDLL, and
 * `dinput8.dll` is not one. So `LoadLibraryA("dinput8.dll")` from the shifter or the wheel
 * picker loaded the ASI loader, which did its job faithfully and loaded every `*.asi` beside
 * it INTO THE LAUNCHER: the force feedback mod grabbed the wheel, the camera hooked Direct3D,
 * and all three wrote their logs into the game folder. Timestamps proved it - `ffb_diag_*.bin`,
 * `car_snap.bin` and `ALXGtech Mafia 1 Mods.log` all carry the launcher's own start second.
 *
 * Two layers, because one of them is not enough on its own:
 *
 *   1. Every DLL this program loads by hand goes through `LoadSystemDll`, which builds an
 *      ABSOLUTE path from GetSystemDirectory. A full path is not searched, so nothing in the
 *      game folder can answer.
 *   2. `HardenDllSearch()` at start-up asks Windows to search System32 only for anything loaded
 *      later - by us or by a system DLL on our behalf (comdlg32 pulling in a shell extension is
 *      the classic case). Resolved through GetProcAddress rather than imported, so the exe
 *      still starts on a Windows without that API.
 *
 * NEITHER helps a STATIC import: those are resolved before WinMain runs, so a program that
 * links `-ldinput8` is hijacked before its first instruction. That is why this launcher must
 * keep reaching DirectInput through LoadLibrary - the dynamic call is not an inconvenience
 * here, it is the fix.
 */

#ifndef ALXG_SYS_DLL_H
#define ALXG_SYS_DLL_H

#include <windows.h>

/* No dependency on ui_skin.h's helpers on purpose: this header is included before it, and a
   load order that has to be remembered is a load order that gets broken. */
static HMODULE LoadSystemDll(const char *name)
{
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    int i = 0;
    if (!n || n >= MAX_PATH - 2) return NULL;
    if (path[n - 1] != '\\') { path[n++] = '\\'; }
    while (name[i] && (UINT)(n + i) < MAX_PATH - 1) { path[n + i] = name[i]; i++; }
    path[n + i] = 0;
    return LoadLibraryA(path);
}

typedef BOOL (WINAPI *SetDefaultDllDirectories_t)(DWORD);
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

static void HardenDllSearch(void)
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    SetDefaultDllDirectories_t set = k32
        ? (SetDefaultDllDirectories_t)GetProcAddress(k32, "SetDefaultDllDirectories")
        : NULL;
    if (set) set(LOAD_LIBRARY_SEARCH_SYSTEM32);
}

#endif /* ALXG_SYS_DLL_H */
