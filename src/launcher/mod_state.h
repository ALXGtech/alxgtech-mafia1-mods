/* mod_state.h - which tab switches which mod, and every word a toggle or a dialog says.
 *
 * NO HWND IN THIS FILE, on purpose. Everything here is a rule from
 * a spec fixed on 2026-08-01, rather than a drawing
 * decision, so all of it can be tested without a window on screen - which matters because the
 * four defects the FFB tuner shipped with were all found by LOOKING, and looking does not scale
 * to wording.
 *
 * The tab table carries a MOD TAG, not a copy of what a mod is. install_core.h's MODS[] is the
 * one place that says what exists; a tab points into it. Two tables is how a launcher ends up
 * offering to install something the engine has never heard of.
 */
#ifndef ALXG_MOD_STATE_H
#define ALXG_MOD_STATE_H

#include "../patcher/install_core.h"

/* Task 3 moves these into ui_skin.h; until it exists mod_state.h defines them, so the state
   test could be written before there was a window to paint. */
#ifndef MAFIA_GREEN
#define MAFIA_GREEN RGB(32,94,42)
#define MAFIA_RED   RGB(150,22,22)
#endif

/* Spec section 3, in the order it names them. */
enum { LTAB_SHIFTER, LTAB_FFB, LTAB_FPV, LTAB_VR, LNTAB };

/* Resource ids of the icons, matching src\launcher\launcher.rc. IDI_APP is the application's own
   and must stay the lowest icon id in the file - see the note there. A tab with 0 has no icon and
   centres its label alone; nothing has 0 today.
   The VR helmet was withheld on his instruction until 2026-08-12, when he asked for the whole set
   redrawn in the application icon's language and named VR among them. */
#define IDI_APP          10
#define IDI_TAB_SHIFTER  200
#define IDI_TAB_FFB      201
#define IDI_TAB_FPV      202
#define IDI_TAB_VR       203

typedef struct {
    const char *tag;      /* a J_MOD_* tag, or NULL for a tab that installs nothing */
    const char *name;     /* the tab's own label */
    const char *writes;   /* what enabling it puts in the folder, for the enable dialog */
    const char *blurb;    /* one line under the toggle */
    int   live;           /* 1 = the mod re-reads its settings while the game runs */
    int   icon;           /* IDI_TAB_*, or 0 for a tab that has none */
} launcher_tab;

static const launcher_tab LTABS[LNTAB] = {
    { J_MOD_SHIFTER, "H-shifter",
      "gearbox_hook.asi, dinput8.dll (the ASI loader), "
      "and a \"gearbox hshifter setup\" folder with gearbox.ini in it",
      "An H-shifter drives Mafia's own gear keys, so a real gate shifts real gears.",
      0, IDI_TAB_SHIFTER },   /* gearbox_hook.asi reads gearbox.ini once, at game start */
    { J_MOD_FFB, "Force Feedback",
      "mafia_ffb.asi and dinput8.dll (the ASI loader)",
      /* NOT the game itself sends none - that claim was refuted here on 2026-07-27: the stock
         GOG build knocks the wheel on a collision with none of our code loaded. The settled
         pitch is what this mod IS, not what the game is missing. */
      "Force feedback built from scratch for modern direct drive wheels.",
      1, IDI_TAB_FFB },   /* ffb_settings.c: CheckFFBSettingsReload, polled 1 Hz from FFBThread */
    { J_MOD_FPV, "First person",
      "mafia_fp.asi, mafia_fp.ini and dinput8.dll (the ASI loader)",
      "The camera sits in the driver's seat instead of behind the car.",
      /* Flipped to 1 on 2026-08-07, and only after checking the SHIPPED binary rather than the
         source: `strings payload\mafia_fp.asi` lists eleven lines of the form mafia_fp.ini changed: <key>,
         which is FpLoadLive's per-key change report. The payload carries f8acb811, which has it.
         The old comment here said "NO re-read today" and had been false since 2026-08-03 - the
         re-read landed and this flag was never flipped, so the window kept telling him a restart
         was needed when it was not. */
      1, IDI_TAB_FPV },   /* fp_camera.c: FpLoadLive, polled 1 Hz - verified in the payload binary */
    { NULL, "VR",
      "",
      /* SAYS WHAT THE TAB IS, NOT WHAT VR WILL DO. It used to read "that mod and head
         tracking, in a headset" - a capability sentence on a tab that installs nothing, which is
         a promise the archive does not keep. github-project's gate refused to stage a release
         over exactly this string, 2026-08-12, and it was the only error left. */
      "This tab installs nothing yet - the VR mod is still being built.",
      0, IDI_TAB_VR },
};

/*
 * NO SAVE BUTTON, ANYWHERE. Settled 2026-08-01: no need to make a save-setting at all. All
 * changes are written into the active preset right away.
 *
 * The history is worth keeping, because "add Apply" and "add Save" are both reasonable things to
 * ask for twice. Apply in the old gearbox-setup.exe meant COPY the ini from the tool's own folder
 * into the game; this program lives in the game, so saving and applying collapsed into one act.
 * Then the one act collapsed into none: moving a slider IS the change, and a button that confirms
 * what you already did is a button that can be forgotten - after which the game plays with
 * settings the window is not showing, and the window looks right.
 *
 * What survives is the honest difference between the mods: WHEN THE MOD NOTICES. The force
 * feedback re-reads its file about once a second; the gearbox and the camera read theirs when the
 * game starts. That is what `live` is for, and it is the only thing the banner says.
 *
 * Files stay explicit in exactly one place - import and export, which is how a preset is handed
 * to somebody else.
 */
enum { BAN_NONE = 0, BAN_GOOD = 1, BAN_WARN = 2, BAN_INFO = 3 };

/*
 * The banner along the bottom of a tab, and it is BIG on purpose. Settled 2026-08-01: an
 * obvious big sign was needed that all the changes are live and go into the launched game - the small line
 * that used to say it was, put plainly, something nobody will notice.
 *
 * Three things it can say. The first two depend on the MOD rather than on the moment, and every
 * installed mod says one of them - a tab that stays silent about when its settings arrive leaves
 * the player to assume, and the two mods behave differently:
 *
 *   BAN_GOOD  a mod that re-reads its settings. Green, and shown WHENEVER you are on that tab,
 *             because never needing a restart for this is a standing fact about the mod, not
 *             an event. With the game up it says so about the running game.
 *   BAN_INFO  a START-TIME mod: the same standing fact, the other way round. Settled 2026-08-01:
 *             "for the shifter we then need a notice up top saying that changes apply only after
 *             the game restarts, so it's obvious in comparison with the next force feedback
 *             tab". Ordinary ink, not red and not amber: it is neither a fault nor a state
 *             that changes, and the whole point is that it reads as the SAME KIND of sign as the
 *             green one, differing in what it says. Two tabs, one slot, one glance.
 *   BAN_WARN  a start-time mod whose settings were changed since this game started. Red, and it
 *             replaces the standing line while it applies. The wording leads with the mod
 *             working, because it is - only the change is waiting.
 *
 * BAN_WARN needs all three of: start-time mod, game running, settings touched. As before: a
 * warning shown merely because the game is up would sit there through a session where nothing
 * changed, and a player who reads "does not apply" while the mod works perfectly concludes the
 * mod is broken. It clears itself when a new game starts, falling back to BAN_INFO.
 */
static int BannerText(char *out, size_t n, int tab, int on, int running, int touched)
{
    out[0] = 0;
    /* A tab that installs nothing, or a mod that is switched off, has nothing to be live about.
       `EVERY CHANGE HERE IS LIVE` over a mod that is not installed is the worst kind of wrong:
       loud, green, and false. The same goes for the restart line: nothing is waiting to apply. */
    if (!LTABS[tab].tag || !on) return BAN_NONE;
    if (LTABS[tab].live) {
        snprintf(out, n, running
            ? "EVERY CHANGE HERE IS ALREADY IN THE RUNNING GAME - no restart needed."
            : "EVERY CHANGE HERE IS LIVE - it reaches the game as you make it, no restart needed.");
        return BAN_GOOD;
    }
    if (running && touched) {
        snprintf(out, n, "%s IS WORKING - but what you just changed only applies after you restart "
                         "Mafia.", LTABS[tab].name);
        return BAN_WARN;
    }
    /* Short on purpose. This line is set in the big face and a start-time page can be narrower
       than the window (the H-shifter page keeps its own 714 px and is centred), so the sentence
       has to fit the PAGE rather than the frame. It is the green line's mirror image, word for
       word where it can be: `no restart needed` against `after you restart Mafia`. */
    snprintf(out, n, "CHANGES HERE APPLY AFTER YOU RESTART MAFIA.");
    return BAN_INFO;
}

/* What a toggle renders. From the JOURNAL, never from the folder: asking the filesystem is how
   a switch reads ON for a file we never wrote, and then does nothing when it is clicked. */
static int TabModState(const char *gamedir, int tab)
{
    if (tab < 0 || tab >= LNTAB || !LTABS[tab].tag) return JMOD_ABSENT;
    return JModState(gamedir, LTABS[tab].tag);
}

/* A control that HAS a state shows the state, not the verb. */
static const char *ToggleLabel(int state)
{
    return state == JMOD_ON     ? "Mod is ENABLED"
         : state == JMOD_BROKEN ? "Files changed - reinstall"
                                : "Mod is DISABLED";
}

static COLORREF ToggleColour(int state)
{
    /* green = in force right now, red = a fault. BROKEN is a fault; plain OFF is not, but red
       on "Mod is DISABLED" is the palette's settled meaning and stays as it is. */
    return state == JMOD_ON ? MAFIA_GREEN : MAFIA_RED;
}

/*
 * The line under the toggle. Spec 3.3: the claim is BOUNDED on purpose - one md5 against one
 * known build. The wording says this is the build everything was tuned and tested on, never
 * compatibility verified, because nothing has been verified on anybody's machine.
 *
 * The caller decides the colour and when to show it; this decides the words. On an unknown
 * build the caller must use ordinary ink, NOT red: red is a fault, and an unknown build is a
 * caveat.
 */
static void BuildLine(char *out, size_t n, const target *t)
{
    if (t->build)
        snprintf(out, n, "Game.exe matches %s - the build everything here was tuned and "
                         "tested on.", t->build->shortname);
    else
        snprintf(out, n, "This Game.exe is not the build these mods were made on (md5 %s), "
                         "so they may do nothing at all. Everything is still recorded and can "
                         "be undone.", t->md5);
}

/* The enable dialog, spec 3.2, in the spec's order. */
static void EnableText(char *out, size_t n, const target *t, int tab)
{
    char unknown[420];
    unknown[0] = 0;
    if (!t->build)
        snprintf(unknown, sizeof unknown,
                 "This Game.exe does not look like %s, so this mod may simply do nothing. "
                 "Everything it writes is still recorded and can be undone.\r\n\r\n",
                 BUILDS[PRIMARY_BUILD].shortname);

    snprintf(out, n,
             "Back up your game folder and your saves before installing anything.\r\n\r\n"
             "%s"
             "Switching %s on writes:\r\n    %s\r\n\r\n"
             "Every file it places is recorded in \"ALXG mods\\install.log\", and switching the "
             "mod off puts the folder back the way it was.",
             unknown, LTABS[tab].name, LTABS[tab].writes);
}

static void DisableText(char *out, size_t n, int tab)
{
    snprintf(out, n,
             "Switch %s off completely and put the folder back the way it was?\r\n\r\n"
             "Everything we recorded writing for this mod is undone. A file you have edited "
             "since is reported and left alone, not overwritten.",
             LTABS[tab].name);
}

#endif /* ALXG_MOD_STATE_H */
