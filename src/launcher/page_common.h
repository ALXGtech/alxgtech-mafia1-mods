/* page_common.h - the row every tab has at the top: a lamp, a switch, and one line about the
 * build. Spec section 3.1 - THE TOGGLE IS THE INSTALL. Settled 2026-08-01: let there be a toggle:
 * this will be the install. No separate installer step and no fifth exe.
 *
 * The two actions call install_core.h. Nothing here copies a file, patches a byte or writes a
 * journal line: a second implementation of install is how a GUI ends up leaving a folder in a
 * state the console tool cannot undo.
 */
#ifndef ALXG_PAGE_COMMON_H
#define ALXG_PAGE_COMMON_H

/* ---- the header strip -----------------------------------------------------------------------
 * ONE LINE across the top of the window, under the tabs, and it belongs to the FRAME rather than
 * to any page. Settled 2026-08-01: the switch on the left, the state beside it, the game folder on
 * the same line, all of it in as little height as it can take.
 *
 * On the frame, not on the four pages, for the same reason the folder row was: the game folder
 * is one fact about the whole program, and four copies of a control are four ways to disagree.
 * The switch and the lamp follow the SELECTED TAB - one set of controls showing that tab's mod. */
#define ID_LAMP      3300
#define ID_TOGGLE    3310
#define ID_BUILDLINE 3320
#define HDR_CONTROLS 1       /* the build line is the only header control ON a page now */

#define HDR_Y      10
#define HDR_H      28
#define HDR_BTN_W  180
#define HDR_LAMP_W 190

static HWND g_lamp, g_switch, g_buildline[LNTAB];
static int  g_state[LNTAB];      /* JMOD_*, re-read after every action */
static int  g_curTab;            /* ui_tabs.h owns it; declared here because the header reads it */

/* ---- writing, which happens by itself -------------------------------------------------------
 * There is no Save button. A change IS the change: move a slider, bind a gear, and the file the
 * mod reads is written before your finger is off the mouse. Settled 2026-08-01.
 *
 * The reason is not convenience. A button that confirms what you already did is a button that
 * can be forgotten, and the failure it leaves behind is silent - the game plays with settings
 * the window is not showing, and the window looks right.
 *
 * What is left on a page is the red banner, and it says only what a page cannot fix by writing:
 * a start-time mod cannot reach the game that is already running.
 */
#define ID_BANNER0 3350

static HWND g_banner[LNTAB];
/* changed since the running game started - see BannerText. Cleared when a new game comes up,
   because that process read the files as they are now. */
static int  g_touched[LNTAB];
static void (*g_saveFn[LNTAB])(void);
static void (*g_loadFn[LNTAB])(void);
static int  g_writing[LNTAB];     /* set while a page is loading, so a load is not a change */

static int GameIsRunning(void);   /* launcher.c - it owns the window lookup */

static int g_bannerKind[LNTAB];   /* BAN_*, so WM_CTLCOLORSTATIC knows the colour */

static void RefreshSaved(int tab){
    char warn[300];
    if(!g_banner[tab]) return;
    g_bannerKind[tab]=BannerText(warn,sizeof warn,tab,g_state[tab]==JMOD_ON,
                                 GameIsRunning(),g_touched[tab]);
    SetWindowTextA(g_banner[tab],warn);
    InvalidateRect(g_banner[tab],NULL,TRUE);
}

/* AT THE TOP OF THE PAGE, above everything the mod configures. Settled 2026-08-01: at the bottom
   it needed scrolling to, and the one thing this line exists to do is be seen without looking
   for it. Its space is reserved whether or not it has anything to say today, so the page does
   not jump when the game starts. */
/* Where a page's ink starts and how wide it runs. Not every page is the frame's width: the FFB
   page needs all 1320 for two columns, the H-shifter page keeps the 714 it was designed at and is
   centred (page_shifter.h, settled 2026-08-01). The build line and the banner belong to the PAGE, so
   they line up with its content and not with the window's edge - a header at x=26 over centred
   rows reads as one of the two being in the wrong place. A page sets these BEFORE it calls
   PageHeader; 0 means "the default margin". */
static int g_pageX[LNTAB];
static int g_pageW[LNTAB];
static int PageX(int tab){ return g_pageX[tab] ? g_pageX[tab] : 26; }
static int PageW(int tab){ return g_pageW[tab] ? g_pageW[tab] : WINW-52; }

static int PageBanner(HWND canvas,int tab,int y){
    g_banner[tab]=MkTextF(canvas,"",PageX(tab),y,PageW(tab),32,ID_BANNER0+tab,g_fBig,0);
    return y+36;
}

/* Something on this page changed. It WILL be written - the only question is when.
 *
 * Not on this call: dragging a slider produces a WM_MOUSEMOVE per frame, so writing here would
 * rewrite the whole file sixty times a second while a finger is down. The change is marked and a
 * 300 ms timer does the write, which is still faster than the mod's own one-second re-read, so
 * nobody can perceive the difference.
 *
 * `g_writing` keeps a LOAD from counting as a change: loading a preset moves every control, and
 * without the guard the load would queue a write of what it just read. */
static int g_pending[LNTAB];

static void PageDirty(int tab){
    if(g_writing[tab]) return;
    g_touched[tab]=1;
    g_pending[tab]=1;
    RefreshSaved(tab);
}

/* called by the frame's flush timer */
static void PageFlush(void){
    int i;
    /* NOT WHILE A KNOB IS HELD (ui_slider.h). A drag produces a WM_MOUSEMOVE per frame, so the
       timer would write the file several times on the way to the value the user actually chose -
       and each write logs the step it landed on, turning one decision into a record of the
       finger's journey. The release does the write. */
    if(g_sliderDrag) return;
    for(i=0;i<LNTAB;i++){
        if(!g_pending[i]) continue;
        g_pending[i]=0;
        if(g_saveFn[i]){ g_writing[i]=1; g_saveFn[i](); g_writing[i]=0; }
    }
}

/* The knob was let go. One write, immediately, rather than up to 300 ms later - the value under
   the finger at the moment it lifted is the answer, and waiting only invites the window to be
   closed in between. */
static void SliderDragEnded(void){ PageFlush(); }
static void PageSaved(int tab){ RefreshSaved(tab); }

/* A game just came up. Whatever it read is what the files say now. */
static void GameStarted(void){
    int i;
    for(i=0;i<LNTAB;i++){ g_touched[i]=0; RefreshSaved(i); }
}

/* Registers what this page writes and reads, and reserves the banner's line. No buttons: the
   writing is automatic and the reading is the preset row's job. */
static int PageSaveRow(HWND canvas,int tab,int y,void (*save)(void),void (*load)(void)){
    g_saveFn[tab]=save;
    g_loadFn[tab]=load;
    RefreshSaved(tab);
    return y;
}

static void PageGrey(int tab,int on);

/* ---- the two dialogs, and the flag that can skip one of them --------------------------------
 * Spec 3.1: neither direction is one click. Enable shows a warning, disable asks.
 *
 * The do-not-show-again flag lives in HKCU, per application, not in the game folder: a
 * file there would have to be created before anything is installed, kept out of the journal, and
 * then survive an uninstall that promises the folder back the way it was. */
static int SkipWarning(void){
    HKEY k; DWORD v=0,n=sizeof v,type=0;
    if(RegOpenKeyExA(HKEY_CURRENT_USER,ALXG_REGKEY,0,KEY_READ,&k)!=ERROR_SUCCESS) return 0;
    if(RegQueryValueExA(k,"skip_enable_warning",NULL,&type,(BYTE*)&v,&n)!=ERROR_SUCCESS) v=0;
    RegCloseKey(k);
    return v?1:0;
}
static void SetSkipWarning(int on){
    HKEY k; DWORD v=on?1:0;
    if(RegCreateKeyExA(HKEY_CURRENT_USER,ALXG_REGKEY,0,NULL,0,KEY_WRITE,NULL,&k,NULL)
       !=ERROR_SUCCESS) return;
    RegSetValueExA(k,"skip_enable_warning",0,REG_DWORD,(const BYTE*)&v,sizeof v);
    RegCloseKey(k);
}

static int AskEnable(int tab){
    char body[1400];
    int dontAsk=0,r;
    if(SkipWarning()) return 1;
    EnableText(body,sizeof body,&g_target,tab);
    r=AlxgBox(g_frame,"Before switching this on",body,"Install it","Cancel",
              "Do not show me this again",&dontAsk);
    /* Remembered only when they went ahead: ticking the box and then cancelling means "stop
       asking", not install the next thing without asking. */
    if(r==IDOK&&dontAsk) SetSkipWarning(1);
    return r==IDOK;
}
static int AskDisable(int tab){
    char body[700];
    DisableText(body,sizeof body,tab);
    /* no checkbox on purpose: undoing an install is never made skippable */
    return AlxgBox(g_frame,"Switch this mod off",body,"Switch it off","Cancel",NULL,NULL)==IDOK;
}

/* Where a page's own content starts: under the build line, which is the only header control
   left on a page now that the switch and the lamp are on the frame. */
#define PAGE_TOP 40

/* the build line stays ON the page: it is about that mod, and it appears only once the mod is on */
static void PageHeader(HWND canvas,int tab){
    /* not at y=0: the canvas's top edge is the paper's torn edge, and a line set into it is
       read as clipped rather than as decorated */
    g_buildline[tab]=MkTextF(canvas,"",PageX(tab),10,PageW(tab),22,ID_BUILDLINE,g_fField,0);
}

static void RefreshPage(int tab){
    char root[MAX_PATH],line[420];
    GameRoot(root);
    g_state[tab]=TabModState(root,tab);

    /* the one switch and the one lamp show the tab you are looking at */
    if(tab==g_curTab&&g_lamp){
        SetWindowTextA(g_lamp,LTABS[tab].tag?ToggleLabel(g_state[tab]):"Not installed");
        SetWindowTextA(g_switch,
            !LTABS[tab].tag           ? "Not available here"
          : g_state[tab]==JMOD_ON     ? "Disable this mod"
          : g_state[tab]==JMOD_BROKEN ? "Reinstall this mod"
                                      : "Enable this mod");
        EnableWindow(g_switch,LTABS[tab].tag!=NULL);
        InvalidateRect(g_lamp,NULL,TRUE);
        InvalidateRect(g_switch,NULL,TRUE);
    }
    /* The green line is the confirmation of an ACTION, not a status band (spec 3.3): it appears
       once the mod is on, and a tab whose mod is off says nothing about the build at all. */
    if(g_buildline[tab]){
        if(g_state[tab]==JMOD_ON){ BuildLine(line,sizeof line,&g_target);
                                   SetWindowTextA(g_buildline[tab],line); }
        else SetWindowTextA(g_buildline[tab],"");
        InvalidateRect(g_buildline[tab],NULL,TRUE);
    }
    PageGrey(tab,g_state[tab]==JMOD_ON);
}

/* page_fpv.h owns the field-of-view slider and therefore the angle a fresh install writes. It is
   defined there and declared here for the same reason SliderDragEnded is: the page is included
   after this file, and the toggle cannot ask a page that does not exist yet. */
static int FpvWantedFov(void);

static int ToggleOn(int tab){
    char root[MAX_PATH]; int mods,ok;
    const mod_row *r=LTABS[tab].tag?mod_by_tag(LTABS[tab].tag):NULL;
    if(!r) return 0;
    GameRoot(root);
    if(!locate(root,&g_target)) return 0;
    mods=r->bit;
    /* Exactly this one, plus the loader when the mod needs one. A camera installed into a folder
       where nothing is ever loaded is the worst kind of success: every file in place and no
       symptom to search for. */
    if(mods&M_NEEDS_LOADER) mods|=M_LOADER;
    /* THE CAMERA BRINGS THE FIELD OF VIEW WITH IT. Noted 2026-08-07, on being handed a release
       archive that left the game at 70: it is strange that it ships without the patch. There
       was a built-in patch to 86, even without any sliders, by default. That was correct, and it was a loss in transit -
       the 2026-08-01 plan lists `fov` among this window's toggles and no tab ever got one, so
       every bench here has run 86 while the product shipped 70. It rides with the camera because
       70 is at its worst from the driver's seat, and the slider on that page is what chooses the
       angle. */
    if(tab==LTAB_FPV) mods|=M_FOV;
    g_errors=0;
    /* res_w/res_h 0: a toggle is not a first-time setup and has no business changing the
       resolution somebody chose. Same reason --install-mod forces --no-res. */
    ok=do_install(&g_target,0,0,mods,0,FpvWantedFov());
    /* THE PAGE NOW READS THE FILES THE INSTALL JUST WROTE.
     *
     * Without this the page keeps whatever it decided while the folder was still bare, and on a
     * fresh copy that is a guess: the settings files did not exist a moment ago. Alex hit it on
     * 2026-08-15 - he enabled the camera and the wide-screen button still read OFF, over a
     * mafia_fp.ini the install had just written with hud_aspect_pct = -1.
     *
     * It is not only a wrong caption. The page writes ONE KEY AT A TIME from its own variables,
     * so the next save would have put that stale 0 back over the shipped -1 - a control writing a
     * value it never read, which is the rule [[unplugged-device-clobber]] exists to state.
     *
     * g_loadFn has been registered by every page since the save row was written and called by
     * nothing until now. RefreshPage after it, not before: the load moves the values and repaints
     * the page's own controls, RefreshPage repaints the frame's lamp, switch and build line. */
    /* Not conditional on `ok`. A HALF-DONE install is the case where the page most needs to
       describe the folder rather than its own intentions. */
    if(g_loadFn[tab]) g_loadFn[tab]();
    RefreshPage(tab);
    return ok&&g_errors==0;
}

/* Is anything left that the ASI loader is there for? Asked of the JOURNAL, like everything else
   a toggle decides from. */
static int LoaderStillNeeded(const char *root){
    int i;
    for(i=0;i<N_MODS;i++)
        if((MODS[i].bit & M_NEEDS_LOADER) && JModState(root,MODS[i].tag)!=JMOD_ABSENT)
            return 1;
    return 0;
}

static int ToggleOff(int tab){
    char root[MAX_PATH]; int ok=1;
    const mod_row *r=LTABS[tab].tag?mod_by_tag(LTABS[tab].tag):NULL;
    if(!r) return 0;
    GameRoot(root);
    if(!locate(root,&g_target)) return 0;
    g_errors=0;
    /* The FOV patch went in with the camera, so it comes out with it - BEFORE the camera's own
       files, because undo works newest-first and Game.exe.bak is journalled under `fov` too.
       Left behind, it would be a Game.exe still at 86 with every tab reading "not installed",
       which is exactly the state the journal exists to make impossible. */
    if(SEq(r->tag,J_MOD_FPV)&&JModState(root,J_MOD_FOV)!=JMOD_ABSENT){
        ok=do_uninstall(&g_target,J_MOD_FOV);
        if(ok&&!locate(root,&g_target)) return 0;
    }
    /* The reload after the uninstall is at the bottom of this function, with the same reasoning as
       ToggleOn's: a page must describe the folder as it is now. Milder here only because the page
       is greyed afterwards, so a stale value cannot be saved - but it can still be READ, and a
       switched-off mod whose page shows the settings it had while it was on is the same lie in the
       other direction. */
    if(ok) ok=do_uninstall(&g_target,r->tag);

    /* THE LAST MOD OUT TAKES THE LOADER WITH IT.
       ToggleOn installs the loader silently along with the mod, so ToggleOff has to be able to
       take it away again, or a player who switches everything off is left with a dinput8.dll
       that loads nothing and an "ALXG mods" folder recording it. The toggle promises the folder
       back the way it was; a leftover file is that promise broken.
       Measured first as a self-test failure, not reasoned about: the round trip left both. */
    if(ok&&!LoaderStillNeeded(root)&&JModState(root,J_MOD_LOADER)!=JMOD_ABSENT){
        LogLine("nothing left that needs the ASI loader - taking it out too");
        if(locate(root,&g_target)) ok=do_uninstall(&g_target,J_MOD_LOADER);
    }
    /* The trap that cost two runs elsewhere, and it is still true: off does not mean a quiet
       wheel. The stock game HAS force feedback of its own. */
    if(ok&&LTABS[tab].tag&&SEq(LTABS[tab].tag,J_MOD_FFB)){
        LogLine("NOTE: the game's OWN force feedback comes back. It has collision effects and");
        LogLine("      it saws the wheel left and right. Quiet is not one of the options.");
    }
    if(g_loadFn[tab]) g_loadFn[tab]();
    RefreshPage(tab);
    return ok&&g_errors==0;
}

#endif /* ALXG_PAGE_COMMON_H */
