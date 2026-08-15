/*
 * mafia-gog-patch.exe - installs this project's work into a stock GOG Mafia.
 *
 * Phase 4 of docs/APPROACH.md. What it installs:
 *   - Ultimate ASI Loader as dinput8.dll   (GOG's LS3DF.dll imports DINPUT8.dll)
 *   - mafia_ffb.asi                        (the ported force feedback)
 *   - mafia_fp.asi + mafia_fp.ini          (the first-person driving camera)
 *   - gearbox_hook.asi + its setup folder  (the H-shifter gearbox module, shipped OFF)
 *   - the FOV 70 -> 86 patch in Game.exe   (three sites, BY SIGNATURE - see docs/FOV-PORT.md)
 *   - the resolution the engine will ask D3D for (registry blob LS3D_setup)
 *
 * What it deliberately does NOT touch: a1..aa.dta, tables\, sounds\, any localized asset.
 * Nothing it writes goes outside the target folder except the one registry value.
 *
 * Design rules this file obeys, each one paid for elsewhere in the project:
 *   - RECORD BEFORE WRITE. Every change goes into <game>\ALXG mods\install.log with whatever
 *     it displaced, so any of it can be undone on a build we have never seen. This replaced
 *     the old rule that an unrecognised Game.exe meant zero writes and a refusal, in effect until 2026-08-01:
 *     a person on an odd build may know exactly what they are doing, and the journal is what
 *     makes saying yes to them safe. --refuse-unknown restores the old gate.
 *   - BACKUP BEFORE WRITE, and never overwrite an existing .bak - the first one is the only
 *     one that holds the original.
 *   - SAME LENGTH ONLY. Every byte patch here replaces 4 bytes with 4 bytes.
 *   - PATCH BY SIGNATURE, refuse unless a signature matches EXACTLY ONCE. A second hit means
 *     this is not the build we think it is.
 *   - NEVER DELETE A FILE WE DID NOT WRITE. Uninstall removes dinput8.dll only if its md5 is
 *     the loader we shipped; somebody else's dinput8.dll stays.
 *   - Say what happened, to the console and to a log next to the exe.
 *
 * Build: tools\build-patcher.ps1
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* Everything this program does. Argument parsing and the console lifetime stay here; the
   launcher window will include the same header and call the same functions. */
#include "install_core.h"

/* ------------------------------------------------------------------ main */

static void own_dir(char *out, size_t n)
{
    char self[MAX_PATH];
    char *slash;
    GetModuleFileNameA(NULL, self, MAX_PATH);
    slash = strrchr(self, '\\');
    if (slash) *slash = 0;
    snprintf(out, n, "%s", self);
}

static void usage(void)
{
    printf("mafia-gog-patch - force feedback, a first-person driving camera, an H-shifter\n"
           "                  gearbox and the FOV fix, installed into a stock GOG Mafia\n\n"
           "  mafia-gog-patch.exe [folder] [options]\n\n"
           "  folder            the Mafia install to patch. Default: the folder this exe is in.\n"
           "  --check           report what is installed and change nothing.\n"
           "  --uninstall       undo everything we recorded doing to this folder.\n"
           "  --uninstall-mod M undo one mod only: loader, ffb, fpv, shifter or fov.\n"
           "  --install-mod M   install one mod only, plus the loader if it needs one.\n"
           "  --status          one machine-readable line per mod: tag, state, name.\n"
           "  --fov N           field of view to write into Game.exe, 60..120. Default 86,\n"
           "                    which is what fits 16:9; the game ships 70.\n"
           "  --res WxH         resolution to write (default: your current desktop mode).\n"
           "  --no-res          do not touch the resolution at all.\n"
           "  --no-camera       skip the first-person camera; install the rest.\n"
           "  --no-gearbox      skip the H-shifter gearbox module; install the rest.\n"
           "  --refuse-unknown  do nothing at all unless Game.exe is a build we know.\n"
           "  --help\n\n"
           "It installs onto any build and warns when it is not the one everything was tested\n"
           "on. Every change is recorded in \"ALXG mods\\install.log\" and can be undone, on that\n"
           "build or any other. It never modifies game data, localization or sound.\n");
}

int main(int argc, char **argv)
{
    char dir[MAX_PATH] = "";
    char logpath[MAX_PATH];
    char selfdir[MAX_PATH];
    int  mode_check = 0, mode_uninstall = 0, mode_status = 0;
    int  res_w = 0, res_h = 0, no_res = 0, mods = M_ALL;
    int  fov_deg = FOV_RECOMMENDED;
    /* NULL = undo everything. --uninstall-mod <tag> undoes one, which is what the launcher's
       per-tab toggle needs and is what makes turning a single mod off a supported operation
       rather than something a GUI would have to reimplement. */
    const char *uninstall_mod_tag = NULL;
    /* The old behaviour, kept for a caller that wants a gate rather than a warning. */
    int  refuse_unknown = 0;
    int  i, ok;
    target t;
    DWORD pids[4];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0)          mode_check = 1;
        else if (strcmp(argv[i], "--uninstall") == 0) mode_uninstall = 1;
        else if (strcmp(argv[i], "--no-res") == 0)    no_res = 1;
        else if (strcmp(argv[i], "--no-camera") == 0)  mods &= ~M_FPV;
        else if (strcmp(argv[i], "--no-gearbox") == 0) mods &= ~M_SHIFTER;
        else if (strcmp(argv[i], "--refuse-unknown") == 0) refuse_unknown = 1;
        else if (strcmp(argv[i], "--status") == 0) mode_status = 1;
        else if (strcmp(argv[i], "--install-mod") == 0 && i + 1 < argc) {
            const mod_row *r = mod_by_tag(argv[++i]);
            int one;
            if (!r) {
                printf("unknown mod '%s'. Known: loader ffb fpv shifter fov\n", argv[i]);
                return 2;
            }
            one = r->bit;
            /* Exactly this one, plus the loader when the mod needs it. Asking for the camera
               alone and getting a folder where nothing is ever loaded would be the worst kind
               of success: every file in place and no symptom to search for. */
            mods = one;
            if (one & M_NEEDS_LOADER) mods |= M_LOADER;
            no_res = 1;   /* a per-mod install is a toggle, not a first-time setup */
        }
        else if (strcmp(argv[i], "--uninstall-mod") == 0 && i + 1 < argc) {
            const mod_row *r = mod_by_tag(argv[++i]);
            if (!r) {
                printf("unknown mod '%s'. Known: loader ffb fpv shifter fov\n", argv[i]);
                return 2;
            }
            mode_uninstall = 1;
            uninstall_mod_tag = r->tag;
        }
        else if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &res_w, &res_h) != 2) { printf("bad --res value\n"); return 2; }
        }
        else if (strcmp(argv[i], "--fov") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%d", &fov_deg) != 1 ||
                fov_deg < FOV_MIN || fov_deg > FOV_MAX) {
                printf("bad --fov value: whole degrees, %d..%d\n", FOV_MIN, FOV_MAX);
                return 2;
            }
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(); return 0; }
        else if (argv[i][0] == '-') { printf("unknown option: %s\n", argv[i]); usage(); return 2; }
        else snprintf(dir, sizeof dir, "%s", argv[i]);
    }

    own_dir(selfdir, sizeof selfdir);
    if (!dir[0]) snprintf(dir, sizeof dir, "%s", selfdir);

    /* --status is for a program, not a person: it prints its lines and nothing else, and
       writes no log. A banner in front of them would have to be skipped by every caller, and
       the first caller to forget would parse the banner as a mod. */
    if (mode_status) {
        if (!locate(dir, &t)) return 1;
        do_status(&t);
        return 0;
    }

    snprintf(logpath, sizeof logpath, "%s\\mafia-gog-patch.log", selfdir);
    g_log = fopen(logpath, "a");

    logf_("=== mafia-gog-patch  (%s) ===", mode_check ? "check" : mode_uninstall ? "uninstall" : "install");

    if (!locate(dir, &t)) { ok = 0; goto done; }
    report(&t);

    if (mode_check)          ok = 1;
    else if (mode_uninstall) ok = do_uninstall(&t, uninstall_mod_tag);
    else {
        if (!no_res && res_w <= 0) {
            res_w = GetSystemMetrics(SM_CXSCREEN);
            res_h = GetSystemMetrics(SM_CYSCREEN);
        }
        if (no_res) { res_w = res_h = 0; }
        ok = do_install(&t, res_w, res_h, mods, refuse_unknown, fov_deg);
    }

    /* Re-read from disk and report again: the state AFTER the writes, measured rather than
     * assumed. Everything above says what we intended to do; this says what is there. */
    if (!mode_check) {
        logf_("");
        logf_("--- state after ---");
        if (locate(dir, &t)) report(&t);
    }

done:
    logf_("");
    logf_(ok && g_errors == 0 ? "DONE - no errors." : "FINISHED WITH ERRORS - see above.");
    logf_("log: %s", logpath);
    if (g_log) fclose(g_log);

    /* Double-clicked: we own the console and it would vanish with the process. */
    if (GetConsoleProcessList(pids, 4) == 1) { printf("\nPress Enter to close..."); getchar(); }
    return (ok && g_errors == 0) ? 0 : 1;
}
