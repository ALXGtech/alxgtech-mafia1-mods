/* ffb_settings.c - gog-patcher only, milestone M1.
 *
 * INI-driven percent multipliers for the FFB tuner utility. Included from mafia_ffb_v6.c
 * after Log, the K_ codes and GamePath are defined, so it can call them directly without
 * duplicating them - and it owns no line of any other module, per the merge-discipline
 * rule in
 * the merge discipline for this milestone: new files only, with zero lines of any other module in them.
 *
 * The invariant (handover section 1): every slider is a MULTIPLIER on a reference constant,
 * default 100 = bit-identical to the shipped v7.52 build. Nothing here may change what a
 * default install feels like.
 *
 * The seven open decisions in section 5 were accepted as recommended, 2026-07-24
 * (recorded in the milestone plan). This file implements #2 (spring and
 * damper as two separate percents) and carries the multiplier state for #1 (crash slider).
 * #3/#4 (DOR floor interpolation, wallow clamp) already live in mafia_ffb_v6.c's g_kickFloor
 * / g_wallowK tables; #5/#6 (mod-off meaning, mod-discovery file) are M2 (the GUI) work.
 */

#define K_SETTINGS 6u   /* new diag record: loaded settings. Free - K_WHEEL=5 was the last one. */

/* <gamedir>\ALXG mods\mafia ffb setup\mafia_ffb.ini - the layout in handover section 2, moved
   under "ALXG mods\" on 2026-08-07 so a game folder carries one directory of ours rather than
   three. The window writes this file (page_ffb.h FFBDIR) and this module reads it; the two
   spellings live in different programs and have to move together. */
static void FFBIniPath(char *out)
{
    GamePath(out, "ALXG mods\\mafia ffb setup\\mafia_ffb.ini");
}

/* Percent multipliers. All default 100 = the reference build, untouched. Numbered to match
   the slider table recorded for this milestone, section 2.3. */
static volatile LONG g_pctMaster  = 100;  /* #1 every constant force, at the SetMag choke   */
static volatile LONG g_pctCrash   = 100;  /* #2 crashes and rams                            */
static volatile LONG g_pctObjects = 100;  /* #3 hitting objects (F3 taps)                   */
static volatile LONG g_pctPed     = 100;  /* #4 pedestrians                                 */
static volatile LONG g_pctGun     =   0;  /* #5 gunfire - SHIPPED OFF, see the read below    */
static volatile LONG g_pctRoad    = 100;  /* #6 road surface (curbs, tram rails, offroad)   */
static volatile LONG g_pctSpring  = 100;  /* #7a wheel weight - spring                      */
/* #7b split in two on 2026-07-27b: passenger cars were to have three sliders, spring, damper at
   standstill and damper in motion. They were one control while the truck pair did not exist; now the
   truck multipliers act on the standing and moving terms separately, and one car number in front
   of two separately-multiplied quantities is a dialog that cannot be read. Legacy `damper=`
   still loads into BOTH, so an existing file keeps meaning what it meant. */
static volatile LONG g_pctDampStatic = 100;  /* #7b wheel weight - damper, standing         */
static volatile LONG g_pctDampMoving = 100;  /* #7c wheel weight - damper, moving           */
static volatile LONG g_pctSat     = 100;  /* #8 slide feel (SAT / spin)                     */
/* #9/#10, added 2026-07-27 by decision (recorded 2026-07-27 and
   2026-07-27b). NOT percents of a reference force like the eight above - percents of the DAMPER
   COEFFICIENT A CAR GETS, applied only when the vehicle-class vote says heavy. 100 = a truck
   damps exactly like a car, which is what Recommended must produce; his x2 / x1.75 spec of
   2026-07-22 was never judged by feel and is a starting position, not a default. Recommended
   ceiling 400 - see TruckDampX in mafia_ffb_v6.c for why, and for why the two do not behave
   alike (the standstill coefficient is already half saturated at 100%, the moving one is not
   saturated at all). Consumed at the coefficient source, not at SetDamper. */
static volatile LONG g_pctTruckStatic = 100;  /* #9  truck damper, standing                 */
static volatile LONG g_pctTruckMoving = 100;  /* #10 truck damper, moving                   */

/* The arithmetic settled offline (handover section 3):
   exact at every percentage including 100, and the pct==100 branch makes a default install
   bit-identical rather than merely arithmetically equal. Never rounds a tiny kick to zero. */
static LONG Mul(LONG mag, LONG pct)
{
    if (pct == 100) return mag;
    return (mag * pct + 50) / 100;
}

/* Bumped by every successful load. M1b (ffb_status.c) echoes it into the status file, which
   is how the utility confirms whether the .asi actually picked up the INI written to it, without
   guessing from a timestamp. 0 = the .asi is running on built-in defaults. */
static DWORD g_settingsGen = 0;

/* ---- the rotation range, in DEGREES ----
   Settled 2026-07-25: the mod asks for the range and offers the selector choice; the range is
   not set directly - the chosen value determines what the game is told to serve.
   That sentence is the whole design of this key, and it is worth restating because it is the
   opposite of what a settings file usually does. We do NOT set the wheel's lock - DirectInput
   has no property for it and it lives in the CMagic driver on his other monitor. He sets it
   there by hand; here he only TELLS us which one he chose, so the mod knows what to serve.
   The UI must read that way too: "select your range", never "set your range".

   DEGREES, not a slot index, because that is what a human writes and what the selector shows.
   Only the EIGHT table values are accepted - 540 and 720 joined the original six on 2026-07-27,
   and with them the free-degrees box was DROPPED rather than deferred. That was the whole point
   of adding them: eight detents from 90 to 1440 cover what a real wheelbase offers, so no
   arbitrary angle needs the runtime cube root this file cannot compute (-nostdlib: no powf, no
   cbrtf). An unknown value must not silently become something else, so it is refused loudly
   into the log and the value already in force stands.

   DEFAULT 600, on his instruction. Numerically that is a no-op against the old default of
   slot -1 ("none declared"): 600 is the reference, so g_dorK = 1.000, g_wallowK = 1.000 and
   g_kickFloor = 0 - the same three values -1 produced. What changes is honesty, not force:
   the log and the status file can now say 600 instead of "nothing declared". */
static void ReadRange(const char *ini)
{
    LONG deg = (LONG)GetPrivateProfileIntA("ffb", "range", DOR_REF, ini);
    for (int i = 0; i < N_DOR_SLOTS; i++) {
        if (g_dorDegTab[i] == deg) {
            g_dorSlot = i;
            g_dorNow  = deg;
            return;
        }
    }
    /* Not one of the six. Keep whatever is in force and say so - c = the value refused. */
    Log(K_SETTINGS, 0xBADu, (DWORD)deg, (DWORD)g_dorNow, (DWORD)g_dorSlot);
}

static void LogFFBSettings(void)
{
    /* K_SETTINGS: a..d hold four percents per record - split over four records so all twelve
       load-bearing values are on the record without inventing a wider REC. */
    Log(K_SETTINGS, (DWORD)g_pctMaster, (DWORD)g_pctCrash, (DWORD)g_pctObjects, (DWORD)g_pctPed);
    Log(K_SETTINGS, (DWORD)g_pctGun, (DWORD)g_pctRoad, (DWORD)g_pctSpring, (DWORD)g_pctSat);
    /* the four damper numbers on ONE record, in the order the dialog draws them: the two car
       halves and the truck multiplier that sits beside each. Reading the log should not require
       joining two records to determine what the damper was doing. */
    Log(K_SETTINGS, (DWORD)g_pctDampStatic, (DWORD)g_pctTruckStatic,
                    (DWORD)g_pctDampMoving, (DWORD)g_pctTruckMoving);
    Log(K_SETTINGS, (DWORD)g_truckTrim, 0, 0, 0);
}

static void ReadFFBSettings(void)
{
    char ini[MAX_PATH];
    FFBIniPath(ini);
    g_pctMaster  = (LONG)GetPrivateProfileIntA("ffb", "master",  100, ini);
    g_pctCrash   = (LONG)GetPrivateProfileIntA("ffb", "crash",   100, ini);
    g_pctObjects = (LONG)GetPrivateProfileIntA("ffb", "objects", 100, ini);
    g_pctPed     = (LONG)GetPrivateProfileIntA("ffb", "ped",     100, ini);
    /* GUNFIRE SHIPS AT ZERO since 2026-08-12. The tap fires for EVERY shot leaving the car,
       and with an armed passenger aboard: a jolt not caused by the driver reads as the wheel
       going wrong rather than as a gunshot, and it muddles everything else the wheel is
       saying. The channel cannot yet tell whose shot it is - that is the real fix and it is
       written down as one. Until then the honest default is off, and 100 is one step away on
       the slider for anybody who wants it back. */
    g_pctGun     = (LONG)GetPrivateProfileIntA("ffb", "gun",       0, ini);
    g_pctRoad    = (LONG)GetPrivateProfileIntA("ffb", "road",    100, ini);
    g_pctSpring  = (LONG)GetPrivateProfileIntA("ffb", "spring",  100, ini);
    /* MIGRATION, and it must stay for as long as any file written before 2026-07-27b can exist:
       the old single `damper=` becomes the DEFAULT for both halves, so a user who had set 50
       gets 50 standing and 50 moving - exactly what that number used to mean - instead of
       silently snapping back to the reference. The utility writes only the two new keys. */
    LONG dampLegacy = (LONG)GetPrivateProfileIntA("ffb", "damper", 100, ini);
    g_pctDampStatic = (LONG)GetPrivateProfileIntA("ffb", "damper_static", dampLegacy, ini);
    g_pctDampMoving = (LONG)GetPrivateProfileIntA("ffb", "damper_moving", dampLegacy, ini);
    g_pctSat     = (LONG)GetPrivateProfileIntA("ffb", "sat",     100, ini);
    /* v7.63: NOT A SLIDER - a DETECTOR threshold, and the distinction is the one
       memory\slider-vs-detector.md was written about. It does not scale any force; it decides
       whether a maximum-force kick is allowed to fire at all, so it must never appear in the
       tuner beside the percentages. Stored x100 because GetPrivateProfileInt has no floats.
         0   = the guard is OFF and every ceiling kick fires, i.e. exactly v7.62
         73  = the shipped threshold, priced on eight labelled ceiling fires
       Anything outside 0..200 is a typo rather than an intention, so it falls back to 73
       instead of silently disabling the guard or vetoing every crash in the game.
       GetPrivateProfileInt returns UINT - see memory\truck-damper-sliders.md - so this is
       read into a LONG before any comparison. */
    {
        LONG ca = (LONG)GetPrivateProfileIntA("ffb", "ceil_agree", 73, ini);
        if (ca != 0 && (ca < 10 || ca > 200)) ca = 73;
        g_ceilAgree = (float)ca / 100.0f;
    }
    /* v7.64, and it is a DETECTOR input rather than a force, so it never goes in the tuner.
       Stored x1000 because the useful range is a few percent: 50 = 0.05 = the shipped value,
       0 = the guard is off and the raw speed field is used exactly as v7.63 used it.
       Anything outside 0..500 is a typo; fall back to 50 rather than silently disabling a
       guard that exists to stop a maximum-force kick firing on a read artefact. */
    {
        LONG tf = (LONG)GetPrivateProfileIntA("ffb", "tear_fix", 50, ini);
        if (tf != 0 && (tf < 5 || tf > 500)) tf = 50;
        g_tearFix = (float)tf / 1000.0f;
    }
    /* v7.65. Not a detector input and not a force - it decides whether the per-tick INPUT log
       runs. Default 1 because the whole point of it is that the signal has never been recorded
       for a whole drive, and a diagnostic that has to be switched on is a diagnostic that is
       off when the interesting thing happens. 0 costs nothing to set if a drive ever needs the
       smaller file. See g_tickLog in mafia_ffb_v6.c. */
    g_tickLog = (LONG)GetPrivateProfileIntA("ffb", "tick_log", 1, ini);
    /* v7.66. A FORCE, so default 0 - it must never appear without being asked for. 1 turns on
       the impulse kick, the first channel that can feel a ram from behind. Everything else is
       untouched when it is on, so `impulse_kick` alone is a clean A/B. See IMP_KICK_MIN. */
    g_impulseKick = (LONG)GetPrivateProfileIntA("ffb", "impulse_kick", 0, ini);
    /* v7.66. Default 1 because it FIXES a defect he named twice - the manifold tap's built-in
       125-1062 ms delay - rather than adding anything. 0 restores the v7.65 wait-for-the-second-
       contact rule for an A/B. Measured trade at PED_IMP_CONFIRM. */
    g_tapNeedsImpulse = (LONG)GetPrivateProfileIntA("ffb", "tap_needs_impulse", 1, ini);
    /* v7.67. Default 1: it removes 25 of the 42 kicks drive 10 produced, keeps every one he
       approved, and leaves none of his 17 complaints with a kick behind it. 0 = fire the
       impulse kick immediately, i.e. exactly v7.66, for an A/B. See IMP_CONFIRM_MS. */
    g_impConfirm = (LONG)GetPrivateProfileIntA("ffb", "impulse_confirm", 1, ini);

    /* Not a slider and not a percent multiplier on an output - this one scales the SURPLUS of
       the heavy-vehicle steering weight above 1.0, and its default is 0 rather than 100.
       0 = a truck steers exactly like a car, settled 2026-07-23; 100 = the old
       v7.31 weighting. Reading it here rather than leaving it a compile-time constant is what
       makes changing it later possible without a rebuild. See mafia_ffb_v6.c at g_truckTrim. */
    g_truckTrim  = (LONG)GetPrivateProfileIntA("ffb", "truck",     0, ini);
    /* The two truck damper sliders. Default 100 = a truck damps like a car - his correction of
       2026-07-27b, and the reason it is 100 here and 0 above is that these two multiply the
       whole coefficient while `truck` scales only the surplus above it. No upper clamp: the
       utility caps its SLIDER at 400 because past that the standstill damper saturates inside
       the first eighth of the wheel's travel, but a hand-typed value is his to make. */
    g_pctTruckStatic = (LONG)GetPrivateProfileIntA("ffb", "truck_static", 100, ini);
    g_pctTruckMoving = (LONG)GetPrivateProfileIntA("ffb", "truck_moving", 100, ini);
    ReadRange(ini);
    g_settingsGen++;
    LogFFBSettings();
}

/* Hot reload (handover section 3): the utility writes the INI, this thread notices within
   ~1s. Call once a second from FFBThread - never from anywhere that can spin faster, the
   file-attributes call is a real syscall. A missing INI is not an error: it means "installed,
   utility never run yet", and every percent above is already the safe default. */
static FILETIME g_settingsMtime;
static int      g_settingsHaveMtime = 0;

static void CheckFFBSettingsReload(void)
{
    char ini[MAX_PATH];
    FFBIniPath(ini);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(ini, GetFileExInfoStandard, &fad)) return;
    if (!g_settingsHaveMtime ||
        fad.ftLastWriteTime.dwLowDateTime  != g_settingsMtime.dwLowDateTime ||
        fad.ftLastWriteTime.dwHighDateTime != g_settingsMtime.dwHighDateTime) {
        g_settingsMtime = fad.ftLastWriteTime;
        g_settingsHaveMtime = 1;
        ReadFFBSettings();
    }
}
