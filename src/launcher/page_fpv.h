/* page_fpv.h - the First person tab: where the driver's eye sits.
 *
 * PLAN 3, spec section 5. The seat settled at the wheel is the REFERENCE on every row and the
 * heavy detent on each slider marks it, which is why the slider class stopped hard-coding 100 as
 * its reference on 2026-08-03. **That reference is 150 / -3 / -31 near 42 as of 2026-08-06** - it
 * moved when he re-settled the seat on the GOG bench, and this file carried the old 150 / 3 / -25
 * for a day after.
 *
 * THE PAGE IS LIVE AS OF 2026-08-07. `LTABS[LTAB_FPV].live` is 1, so the banner says every change
 * here reaches a running game. That was checked against the SHIPPED BINARY, not the source:
 * `strings payload\mafia_fp.asi` lists eleven `mafia_fp.ini changed: <key>` lines, which is
 * FpLoadLive's per-key report, and the payload carries f8acb811. The rule that kept the flag at 0
 * still stands and is worth restating - a green `EVERY CHANGE HERE IS LIVE` over a mod that reads
 * its file once is the loudest possible lie this window can tell - it is simply no longer a lie.
 *
 * `pitch_deg` is on the page since 2026-08-07 - a key fp_camera.c re-reads, and the trim he was
 * otherwise adjusting by keyboard.
 *
 * `view_mode` HAS NO CONTROL, and briefly did. As of 2026-08-07, this setting (Which view)
 * needed to be removed from here - users were not going to get the ability to change cameras,
 * camera views. The slot is settled at 13 and stays there; the key remains in the ini for whoever
 * edits files by hand, exactly like `fov_deg` below. A page that shows a control for something
 * nobody may change is a page inviting a support question.
 *
 * THE HOME KEY IS RETIRED. `key_take_view` picked which slot the camera took over, from inside the
 * car. As of 2026-08-07, Home can be unbound for now - no further rebinding is planned, it stays as
 * it is. The slot is settled at 13, so a live re-bind is a key that can only fire by accident.
 * It is 0 in the shipped ini, which fp_camera.c reads as "not a key" by construction - the nudge
 * loop tests `nvk[i] &&` before polling, precisely so a retired binding cannot come back as a
 * phantom press. The page must not advertise a key that is unbound.
 *
 * ONE FILE, ONE WRITER: `mafia_fp.ini`, `[fp]`. The mod writes the same keys when the numpad nudges
 * the seat, so this page reads the file every time the tab is shown rather than trusting what it
 * last wrote - the H-shifter page's lost bindings, on 2026-08-03, were exactly the cost of a page
 * writing a value it had not loaded.
 *
 * WHAT IS DELIBERATELY ABSENT:
 *   `fov_deg`     - the launcher already has a FOV mod that patches Game.exe 70 -> 86, and this key
 *                   overrides the projection at runtime. Two knobs on one quantity is how a
 *                   settings dialog starts lying (the same ruling that left g_dorTrim pinned). The
 *                   key stays in the ini for whoever wants it; it gets no control.
 *   `route`, `anchor`, `basis_swap`, `trace_arm` - not preferences. The shipped ini says so in a
 *                   fenced block and fp_camera.c will not re-read them anyway.
 *   `height_stop` - the numpad's own cycle through the fixed stops. A second way to set it here
 *                   would fight the key.
 */
#ifndef ALXG_PAGE_FPV_H
#define ALXG_PAGE_FPV_H

#define FP_ID_SLIDER0 5600
#define FP_ID_BOX0    5620
#define FP_ID_HINT0   5640
#define FP_ID_SECT    5660
#define FP_ID_NOTE    5661
#define FP_ID_LEVEL   5662     /* the horizon pair: held level */
#define FP_ID_TILT    5663     /* ...or rolling with the car   */
#define FP_ID_SEATREF 5664
#define FP_ID_UIFIX   5665     /* the wide-screen interface fix - ONE button, it toggles */
/* 5666 was its OFF half for one build. There is no second button: a control that says which state
   it is in does not need a partner to say the opposite. The id is left named so nothing reuses it
   by accident while this file still has FP_ID_ constants counting upward. */
/* 5665..5667 were an interface pair and a measurement toggle, added and removed the same day:
   those switches belong on KEYS IN THE GAME, not in a client that ships to strangers - see the
   note where they used to be drawn. The ids stay unused rather than being recycled; a reused
   control id is how a page ends up drawing one thing and acting on another. */
#define FP_ID_KEY0    5670     /* six bindings, one per direction, as on the shifter page */
#define FP_ID_KEYCLR0 5680     /* and a clear beside each */
#define FP_ID_SLOT0   5690     /* presets 1..3, the row the FFB page has */
#define FP_TIMER      3        /* GB_TIMER is 1, FFB_TIMER is 2 */

/* ---- THE SIX SEAT KEYS ----------------------------------------------------------------------
 * As of 2026-08-07, the request was for two keys per axis so a wheel rotary could nudge the seat mid-drive
 * instead of alt-tabbing out to a slider. The mod has had these keys all along; what was missing
 * was a way to change them without editing the ini by hand.
 *
 * VIRTUAL KEYS, not scancodes. The H-shifter page next door stores SCANCODES, because the game's
 * own key table is in scancodes; fp_camera.c polls GetAsyncKeyState, which takes a VK. The two
 * pages look identical from the outside, which is exactly how storing the wrong one would happen
 * - and a binding in the wrong alphabet is a key that simply never fires.
 *
 * 0 = unbound, and clearing is a real operation: the mod's nudge loop tests `nvk[i] &&` before
 * polling, so an axis can be left with no keys on purpose.
 *
 * A re-bind reaches a RUNNING game as of 2026-08-07 - the six keys were added to FpLoadLive that
 * day, which is what makes binding them from here worth doing at all. */
enum { FK_UP, FK_DOWN, FK_FWD, FK_BACK, FK_LEFT, FK_RIGHT, FK_N };
static const char *FKKEY[FK_N]  = { "key_seat_up","key_seat_down","key_seat_fwd",
                                    "key_seat_back","key_seat_left","key_seat_right" };
static const char *FKNAME[FK_N] = { "Eye up","Eye down","Seat forward",
                                    "Seat back","Seat left","Seat right" };
static const int   FKDEF[FK_N]  = { 0x70,0x71,0x72,0x73,0x74,0x75 };   /* F1..F6 */
static int  fp_key[FK_N];
static HWND fp_keyBtn[FK_N];
static int  fp_capture = -1;        /* row waiting for a keypress, -1 = none */

/* ---- PRESETS, modelled on the Force Feedback page -------------------------------------------
 * Same rules as that page, deliberately: clicking a preset SWITCHES to it, an empty preset takes
 * whatever is on screen rather than being a dead button, and there is no save - every change goes
 * into the live file AND into the selected preset, so a session's work is never sitting only
 * where the next click would overwrite it.
 *
 * "Real-time" is not loose here: the seat values, and since 2026-08-07 the six keys, are all in
 * FpLoadLive, which the mod polls once a second. A preset click reaches a running game in about
 * a second, exactly like the FFB page. */
static HWND fp_slotBtn[3];
static int  fp_slotSel = 0;

/* The rows, and every number in this table came from a drive rather than from taste.
 *
 * `lo` is what the key's own value is at slider 0, so a row that goes negative simply starts below
 * zero - the slider class works in 0..travel and the page translates, which is what the ops
 * indirection is for. `ref` is the settled value in KEY space. */
enum { FP_UP, FP_FWD, FP_SIDE, FP_NEAR, FP_PITCH, FP_FOV, FP_NROW };

/* FP_FOV's key is NULL, and that is the whole of its special case: it is not a line in
   mafia_fp.ini but three floats inside Game.exe, so it is neither saved to the ini nor to a
   preset nor loaded from either. fp_LoadFrom reads it from the exe and fp_ApplyFov writes it
   back there. Everything else on this page - the slider, the box, the reference mark, the
   clamp - works on it unchanged, which is why it is a row here rather than a control of its
   own somewhere else. */
static const char *FPKEY[FP_NROW]  = { "seat_up_cm","seat_forward_cm","seat_side_cm","near_cm",
                                       "pitch_deg", NULL };
static const char *FPNAME[FP_NROW] = { "Eye height","Forward / back","Left / right",
                                       "Near clipping plane","Look up / down","Field of view" };
static const int   FPLO[FP_NROW]   = {   0, -100, -100,   0, -30, FOV_MIN };
static const int   FPHI[FP_NROW]   = { 200,  100,  100, 100,  30, FOV_MAX };
/* THE REFERENCE ROW IS WHAT HE LAST APPROVED, and it moved. Until 2026-08-07 this table still
   said 150 / 3 / -25 with a 36 cm near plane - the seat settled on 2026-08-03 - while the bench
   had been running 150 / -3 / -31 near 42 since he re-settled it on the GOG bench on the 6th.
   A "reference" mark on a slider that points at a superseded value is worse than no mark: it
   invites him to return to a seat he has already rejected. See [[fp-seat-position-settled]].
   pitch_deg's reference is 0 - level - because that is what he drove and approved; the control
   exists so a trim is reachable without the keyboard. */
static const int   FPREF[FP_NROW]  = { 150,   -3,  -31,  42,   0, FOV_RECOMMENDED };
static const char *FPHINT[FP_NROW] = {
    "how high the eye sits in the cabin",
    "+ towards the windscreen, - towards the seat back",
    "- towards the driver's door, + towards the passenger",
    "how close a thing can get before it stops being drawn",
    "+ tips the view up, - tips it down. 0 is level",
    /* The one row on this page that is not live, and it has to say so itself: the banner above
       is green because everything else here reaches a running game in a second. */
    "86 fits a 16:9 screen; the game ships 70. Patches Game.exe - restart Mafia" };

static int  fp_val[FP_NROW];
static int  fp_level = 1;           /* lock_roll */
/* THE WIDE-SCREEN INTERFACE FIX, as a client setting. Asked for by Alex on 2026-08-14 once the
   correction was working in the picture: a button in the utility, ON applies it, OFF does not.
   It writes `hud_aspect_pct`: -1 means "work it out from the resolution" and 0 means off.
   THIS INITIALISER IS NOT THE DEFAULT ANYBODY SEES - `fp_LoadFrom` sets the value on every load,
   from the file when there is one and from the shipped default when there is not. It used to read
   "DEFAULT OFF, matching the shipped ini", which was true until 1.4.0 shipped the correction ON
   and false the moment it did. A comment that names another file's contents goes stale silently;
   the load function is where the answer actually lives. */
static int  fp_uifix = 0;           /* hud_aspect_pct != 0 */
static HWND fp_slider[FP_NROW], fp_box[FP_NROW], fp_hint[FP_NROW];
static HWND fp_level_btn, fp_tilt_btn;
static HWND fp_uifix_btn;
/* THE SAME WORDING AS THE FORCE FEEDBACK PAGE, on Alex's instruction of 2026-08-14 - see
   `ffb_RefreshIngame` in page_ffb.h, whose faces are "ON: recommended in-game FFB settings" and
   "OFF: your own in-game settings".
   The shape is: the STATE first, then what that state gives you. One button, its caption is the
   state, and the live state is green. Two pages doing the same job must not read differently. */
#define FP_UIFIX_ON_TEXT  "ON: radar and speedometer un-stretched"
#define FP_UIFIX_OFF_TEXT "OFF: interface as the game draws it"
static int  fp_quiet = 0;           /* set while WE are writing a box, so EN_CHANGE knows */
static int  fp_pageH = 0;

/* WHAT THE SHIPPED INI SAYS, READ OUT OF THE SHIPPED INI.
 *
 * The page needs a value for `hud_aspect_pct` before there is a file to read - a fresh game folder
 * has no mafia_fp.ini until a mod is switched on. It used to answer that with a constant `0`
 * written next to a comment claiming it matched the payload. It did match, until 1.4.0 shipped the
 * correction ON, and then the comment was false and the page told a fresh install the opposite of
 * what enabling it would do. Restating another file's contents in C is a fact with no owner.
 *
 * So ask the file. It is compiled into this exe as RT_RCDATA - the same bytes install_settings
 * drops into the game folder - so this cannot disagree with what we install, by construction.
 *
 * Comment lines are skipped: `;` before a key would otherwise match a key inside prose. The name
 * is compared whole, so `hud_aspect_anchored` cannot answer for `hud_aspect_pct`.
 */
static int fp_ShippedIniInt(const char *key,int fallback)
{
    size_t len=0,i=0,k;
    const unsigned char *d=payload(RES_FP_INI,&len);
    if(!d) return fallback;
    while(i<len){
        size_t bol=i, eq, ke;
        while(i<len && d[i]!='\n') i++;              /* i is now the end of this line */
        {
            size_t p=bol, e=i;
            while(p<e && (d[p]==' '||d[p]=='\t')) p++;
            if(p>=e || d[p]==';' || d[p]=='#' || d[p]=='[' || d[p]=='\r'){ i++; continue; }
            eq=p; while(eq<e && d[eq]!='=') eq++;
            if(eq>=e){ i++; continue; }
            ke=eq; while(ke>p && (d[ke-1]==' '||d[ke-1]=='\t')) ke--;   /* key end, spaces trimmed */
            for(k=0;key[k];k++)
                if(p+k>=ke || d[p+k]!=(unsigned char)key[k]) break;
            if(key[k]==0 && p+k==ke){
                int sign=1,val=0,any=0;
                size_t v=eq+1;
                while(v<e && (d[v]==' '||d[v]=='\t')) v++;
                if(v<e && (d[v]=='-'||d[v]=='+')){ if(d[v]=='-') sign=-1; v++; }
                while(v<e && d[v]>='0' && d[v]<='9'){ val=val*10+(d[v]-'0'); v++; any=1; }
                return any?sign*val:fallback;
            }
        }
        i++;
    }
    return fallback;
}

static void fp_IniPath(char *out){ SCpy(out,g_gameDir); SCat(out,"mafia_fp.ini"); }
/* Beside the mod's own file rather than in a folder of our own: mafia_fp.ini lives in the game
   root, so its presets do too, and the name says which mod they belong to. */
static void fp_ProfilePath(char *out,int slot){
    char n[40]; wsprintfA(n,"mafia_fp_preset%d.ini",slot+1);
    SCpy(out,g_gameDir); SCat(out,n);
}

/* ---- the file --------------------------------------------------------------------------------
 * ONE KEY AT A TIME with WritePrivateProfileString, never a re-emitted file. That is fp_camera.c's
 * own rule (`FpSaveKey`) and it exists because the gearbox binder destroyed a hand-maintained
 * config twice by rewriting its whole table. Here it matters twice over: the ini also carries
 * route / anchor / basis_swap and a fenced block of comments explaining them, and re-emitting the
 * file from this page's four values would delete all of it. */
/* A PRESET IS A CAMERA POSITION, NOT A SET OF BINDINGS.
 * As of 2026-08-07, presets were to be by camera positions, not by key bindings for these
 * eye up, eye down. That is right, and the first version had it wrong: switching preset would
 * have silently re-bound the keys under him, so a preset made on one rig could take the controls
 * away on another.
 * So `withKeys` is 1 only for the live ini - the file the mod reads and the one this page is the
 * editor of - and 0 for a preset or an export. The bindings are a property of the RIG; the seat
 * is a property of the CAR you are sitting in. */
static void fp_SaveTo(const char *p,int withKeys){
    char v[16];
    int i;
    for(i=0;i<FP_NROW;i++){
        if(!FPKEY[i]) continue;            /* the field of view lives in Game.exe, not here */
        wsprintfA(v,"%d",fp_val[i]);
        WritePrivateProfileStringA("fp",FPKEY[i],v,p);
    }
    WritePrivateProfileStringA("fp","lock_roll",fp_level?"1":"0",p);
    /* -1, not 100: the factor is derived from the surface, so it stays right at 1440p and 4K.
       A percentage typed here would be a number that is only correct at one resolution. */
    WritePrivateProfileStringA("fp","hud_aspect_pct",fp_uifix?"-1":"0",p);
    /* hud_aspect_anchored and hud_probe are NOT written from this page. The mod owns them: the
       first is flipped from INSERT on a bench install, and writing it from here would fight that
       key by rewriting the file a second later. */
    if(withKeys) for(i=0;i<FK_N;i++){
        wsprintfA(v,"%d",fp_key[i]);
        WritePrivateProfileStringA("fp",FKKEY[i],v,p);
    }
}

/* TWO FILES, ALWAYS BOTH - the one the mod reads and the preset being edited. Straight from the
   FFB page: the active preset IS what you are editing, so a session's work is never sitting only
   in a working file that the next preset click would overwrite. */
/* ---- writing the angle back ------------------------------------------------------------------
 *
 * Through do_install with M_FOV alone, not through a second copy of the patch logic: the backup,
 * the journal line per site and the refusal on a mixed exe all come free, and there stays exactly
 * one implementation of what patching Game.exe means. Two exes, one engine - the same rule the
 * toggle follows.
 *
 * It runs only when the camera is INSTALLED. With the mod off there is nothing of ours in the
 * folder, and quietly patching an exe from a page whose switch says "Enable this mod" would be
 * the worst kind of surprise; the value is remembered and ToggleOn writes it.
 */
static int fp_FovInExe(void);   /* defined with the loader below, used here */

static void fp_ApplyFov(void){
    char root[MAX_PATH];
    if(TabModState(g_gameDir,LTAB_FPV)!=JMOD_ON) return;
    if(fp_FovInExe()==FpvWantedFov()) return;      /* nothing to say and nothing to write */
    GameRoot(root);
    if(!locate(root,&g_target)) return;
    g_errors=0;
    do_install(&g_target,0,0,M_FOV,0,FpvWantedFov());
}

static void FpvSaveIni(void){
    char p[MAX_PATH];
    fp_IniPath(p);                fp_SaveTo(p,1);   /* the live file carries the bindings */
    fp_ProfilePath(p,fp_slotSel); fp_SaveTo(p,0);   /* a preset is the seat only */
    fp_ApplyFov();
    PageSaved(LTAB_FPV);
}

static void fp_RefreshRow(int i);
static void fp_RefreshLevel(void);
static void fp_RefreshUiFix(void);
static void fp_RefreshKey(int i);
static void fp_LoadFrom(const char *p,int withKeys);

/* ---- the field of view: the exe is the file, and it is also the truth ------------------------
 *
 * The row is not stored anywhere of ours. It is read out of Game.exe and written back into it,
 * so the slider shows what the game will actually do rather than what we last intended - the
 * same rule as every other state on this window ([[gui-mafia-skin]]: show STATE, not an action).
 * When the exe cannot be read, or holds three angles that disagree, the row falls back to the
 * recommended 86 - which is also what a fresh install would write.
 */
static int fp_FovInExe(void){
    char exe[MAX_PATH];
    unsigned char *d; size_t n; int deg=FOV_RECOMMENDED;
    SCpy(exe,g_gameDir); SCat(exe,"Game.exe");
    d=read_file(exe,&n);
    if(!d) return FOV_RECOMMENDED;
    if(!fov_read(d,n,&deg)) deg=FOV_RECOMMENDED;
    free(d);
    return deg;
}

/* What a fresh ToggleOn should write. Declared in page_common.h, which is included first. */
static int FpvWantedFov(void){
    int v=fp_val[FP_FOV];
    return (v>=FOV_MIN&&v<=FOV_MAX)?v:FOV_RECOMMENDED;
}

/* `withKeys` mirrors fp_SaveTo: loading the live ini adopts its bindings, loading a PRESET must
   leave them alone or switching preset would re-bind the controls under the driver. */
static void fp_LoadFrom(const char *p,int withKeys){
    int i;
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES){
        /* Not an error and not a blank page: the defaults ARE the settled seat, so a fresh install
           shows what he approved rather than zeros. */
        for(i=0;i<FP_NROW;i++) fp_val[i]=FPREF[i];
        if(withKeys) for(i=0;i<FK_N;i++) fp_key[i]=FKDEF[i];
        fp_level=1;
        /* From the ini we would INSTALL, not from a constant repeating it - so with no file yet
           the button shows what switching the mod on will actually write. Same rule the field of
           view row follows twenty lines below, and for the same reason. */
        fp_uifix=(fp_ShippedIniInt("hud_aspect_pct",0)!=0)?1:0;
    } else {
        for(i=0;i<FP_NROW;i++)
            if(FPKEY[i])
                fp_val[i]=Clamp((int)GetPrivateProfileIntA("fp",FPKEY[i],FPREF[i],p),FPLO[i],FPHI[i]);
        fp_level=GetPrivateProfileIntA("fp","lock_roll",1,p)?1:0;
        /* Any non-zero value means the correction is on, whether it is -1 or an explicit percent
           somebody set by hand - the button reports what the mod will DO, not which spelling is
           in the file. GetPrivateProfileInt returns UINT, so -1 comes back as 0xFFFFFFFF; that is
           non-zero either way, which is why this reads as an int and only tests against 0. */
        fp_uifix=((int)GetPrivateProfileIntA("fp","hud_aspect_pct",0,p)!=0)?1:0;
        if(withKeys) for(i=0;i<FK_N;i++){
            /* Anything outside a virtual key is a typo, not an intention - fall back to the
               default rather than binding something that can never fire. 0 IS allowed: it is
               how a deliberately unbound axis is spelled. */
            int v=(int)GetPrivateProfileIntA("fp",FKKEY[i],FKDEF[i],p);
            fp_key[i]=(v>=0&&v<=0xFF)?v:FKDEF[i];
        }
    }
    /* The field of view comes from the exe whichever branch ran above, and it overwrites the
       FPREF default the first branch just put there: a preset does not carry an angle, and a
       missing mafia_fp.ini says nothing about an exe that may already be patched.
       ONE EXCEPTION, and without it the whole row is useless: an UNTOUCHED exe reads 70, so a
       slider that simply mirrored it would show 70, and switching the mod on would then write
       70 - a patch that patches nothing. While the mod is off and the exe is stock, the row
       shows what enabling it WILL write, which is the recommended 86. Once the mod is on, the
       exe is the truth again and 70 there means somebody chose 70. */
    {
        int inExe=fp_FovInExe();
        if(inExe==FOV_STOCK&&TabModState(g_gameDir,LTAB_FPV)!=JMOD_ON) inExe=FOV_RECOMMENDED;
        fp_val[FP_FOV]=Clamp(inExe,FPLO[FP_FOV],FPHI[FP_FOV]);
    }
    for(i=0;i<FP_NROW;i++) fp_RefreshRow(i);
    for(i=0;i<FK_N;i++)    fp_RefreshKey(i);
    fp_RefreshLevel();
}

/* A LOAD THAT DOES NOT REPAINT IS NOT A LOAD. Every refresh helper below returns immediately when
   its control does not exist yet, so this is safe at page-build time (where FpvLoadIni runs before
   a single window is created) and correct afterwards.
   It was not always so, and that is exactly why `g_loadFn` sat registered and never called by
   anything: wiring it would have moved the values and left the window showing the old ones. Found
   2026-08-15, when Alex enabled the mod on a fresh copy and the wide-screen button kept saying OFF
   over an ini that said -1. */
static void FpvLoadIni(void){
    char p[MAX_PATH];
    int i;
    fp_IniPath(p);
    fp_LoadFrom(p,1);
    for(i=0;i<FP_NROW;i++) fp_RefreshRow(i);
    fp_RefreshLevel();
    fp_RefreshUiFix();
    for(i=0;i<FK_N;i++) fp_RefreshKey(i);
    PageSaved(LTAB_FPV);
}

/* A VK to a readable name. GetKeyNameText wants a SCANCODE in bits 16..23, so the VK has to be
   mapped first - the shifter page hands it a scancode directly because that is what it stores.
   Same call, one extra step, and skipping the step prints the name of a different key. */
static const char *fp_KeyName(int vk,char *buf){
    UINT sc=MapVirtualKeyA((UINT)vk,0);
    if(sc && GetKeyNameTextA((LONG)(sc<<16),buf,32)>0) return buf;
    wsprintfA(buf,"key 0x%02X",vk); return buf;
}

static void fp_RefreshKey(int i){
    char t[320],kb[40];
    if(!fp_keyBtn[i]) return;
    if(fp_capture==i)   SCpy(t,"press a KEY now...");
    else if(!fp_key[i]) SCpy(t,"click to set");
    else                wsprintfA(t,"%s   (0x%02X)",fp_KeyName(fp_key[i],kb),fp_key[i]);
    SetWindowTextA(fp_keyBtn[i],t);
}

static void fp_RefreshSlots(void){
    int i; for(i=0;i<3;i++) if(fp_slotBtn[i]) InvalidateRect(fp_slotBtn[i],NULL,TRUE);
}

/* Clicking a preset SWITCHES to it, always - that is what makes the row a switch rather than
   three separate commands. An empty preset takes what is on screen, so a fresh one is never a
   dead button. Straight out of ffb_SelectSlot, including the reasoning. */
static void fp_SelectSlot(int i){
    char p[MAX_PATH],m[200];
    fp_ProfilePath(p,i);
    fp_slotSel=i;
    fp_RefreshSlots();
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES){
        wsprintfA(m,"CAMERA PRESET %d WAS EMPTY - it now holds these settings, and is the one "
                    "being edited",i+1);
        LogLine(m);
        FpvSaveIni();
        return;
    }
    fp_LoadFrom(p,0);             /* seat only - the bindings stay as the rig has them */
    FpvSaveIni();                 /* the mod uses what you are looking at */
    wsprintfA(m,"CAMERA PRESET %d is now in force - the mod picks it up within a second",i+1);
    LogLine(m);
}

/* ---- the controls ---------------------------------------------------------------------------- */
static void fp_RefreshRow(int i){
    char t[32];
    if(!fp_box[i]) return;
    wsprintfA(t,"%d",fp_val[i]);
    fp_quiet=1; SetWindowTextA(fp_box[i],t); fp_quiet=0;
    if(fp_slider[i]) InvalidateRect(fp_slider[i],NULL,TRUE);
}

static void fp_RefreshLevel(void){
    /* one choice, two controls, so neither is ever repainted alone - the same shape as the
       gearbox page's A/M pair, and for the same reason: a tick box said neither what the two
       options were nor which one was live */
    if(fp_level_btn) InvalidateRect(fp_level_btn,NULL,TRUE);
    if(fp_tilt_btn)  InvalidateRect(fp_tilt_btn,NULL,TRUE);
}

static void fp_RefreshUiFix(void){
    /* The TEXT is the state, so it is rewritten here rather than being a fixed label with a
       colour bolted on. A control whose caption stays "ON" while it is off is the exact defect
       this window's skin rule exists to prevent. */
    if(!fp_uifix_btn) return;
    SetWindowTextA(fp_uifix_btn,fp_uifix?FP_UIFIX_ON_TEXT:FP_UIFIX_OFF_TEXT);
    InvalidateRect(fp_uifix_btn,NULL,TRUE);
}


static void fp_SetRow(int row,int v){
    int nv=Clamp(v,FPLO[row],FPHI[row]);
    if(fp_val[row]==nv) return;
    fp_val[row]=nv;
    fp_RefreshRow(row);
    PageDirty(LTAB_FPV);
}

/* Slider space is 0..(hi-lo); key space is what the mod reads. The translation lives here and
   nowhere else, so a row that goes negative costs one line rather than a special case in the
   shared class. */
static int  fp_SlGet(int row){ return fp_val[row]-FPLO[row]; }
static void fp_SlSet(int row,int v){ fp_SetRow(row,v+FPLO[row]); }
static int  fp_SlMax(int row){ return FPHI[row]-FPLO[row]; }
static int  fp_SlStep(int row){ (void)row; return 1; }
static int  fp_SlRef(int row){ return FPREF[row]-FPLO[row]; }
/* no second mark on this page - every row here has exactly one value worth marking */
static int  fp_SlAlt(int row){ (void)row; return -1; }
static const slider_ops FP_SLIDER_OPS = {
    fp_SlGet, fp_SlSet, fp_SlMax, fp_SlStep, fp_SlRef, fp_SlAlt, FP_ID_SLIDER0 };

/* ---- ONE CENTRED COLUMN, like the H-shifter page --------------------------------------------
 * As of 2026-08-07, the mod was centred as a whole, the way the H-shifter is centred too. The
 * page used to start at x=26 and run to the window edge, so it read as a full-width sheet beside
 * a centred one, and its controls stretched to whatever the window happened to be.
 *
 * FP_W is wider than the shifter's 714 because this page carries a hint column the shifter does
 * not have. Everything is measured from FP_X so the block moves as one, and nothing is written
 * against WINW any more - a control sized off the window is a control that changes shape when the
 * window does, which is what made the bindings absurdly long. */
#define FP_W       860
#define FP_X       ((WINW-FP_W)/2)
#define FP_R       (FP_X+FP_W)              /* right edge of the block */

#define FPC_LABEL  FP_X
#define FPC_SLIDER (FP_X+184)
#define FPW_SLIDER 260
#define FPC_BOX    (FP_X+460)
/* the "cm" label ends 86 px after the box, so the hint starts clear of it: at a smaller gap the
   photograph showed 150 cmhow high the eye sits - the touching-strings defect the FFB tuner
   shipped with, found the same way. */
#define FPC_HINT   (FP_X+560)
#define FPW_HINT   (FP_R-FPC_HINT)

static void PageFpvCreate(HWND h){
    int y,i;
    PageHeader(h,LTAB_FPV);
    y=PageBanner(h,LTAB_FPV,PAGE_TOP);

    /* ---- THE WIDE-SCREEN INTERFACE FIX, ABOVE EVERYTHING ELSE ON THIS PAGE -------------------
     * Alex's placement, 2026-08-14: above the camera settings, above even the "where the driver's
     * eye sits" heading. It is the first thing a player decides on this tab and it has nothing to
     * do with the seat.
     *
     * ONE button, not a pair - also his: *"you do not even need two buttons, ON and OFF, because with
     * us, if it is pressed then ON is green"*. The button IS the state: its own text says which state
     * it is in, and the live one is green, which is this window's rule everywhere
     * (see ui_skin.h - show STATE, not an action). A pair would be two controls for a question
     * with one answer.
     */
    MkText(h,"UI wide-screen fix",FPC_LABEL,y+6,170,20,0);
    fp_uifix_btn=MkBtn(h,fp_uifix?FP_UIFIX_ON_TEXT:FP_UIFIX_OFF_TEXT,
                       FPC_SLIDER,y,300,28,FP_ID_UIFIX);
    /* One line beside it, not a paragraph - the button already says what the state does. The FFB
       page dropped its paragraph for the same reason: "too strong" next to a control that can
       simply say what it is. */
    MkTextF(h,"Mafia's interface was drawn for a 4:3 screen. Only the radar and the speedometer "
              "are corrected, and only while you are in a car.",
            FPC_SLIDER+312,y+6,FP_R-FPC_SLIDER-312,16,FP_ID_NOTE,g_fSmall,0);
    y+=36;
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,FPC_LABEL,y,FP_W,2,h,
                    NULL,NULL,NULL);
    y+=12;

    MkTextF(h,"WHERE THE DRIVER'S EYE SITS",FPC_LABEL,y+3,330,20,FP_ID_SECT,g_fSection,0);
    MkText(h,"centimetres from the car's own centre - the marked value on each slider is the seat "
             "set at the wheel",FPC_LABEL+340,y+5,FP_R-FPC_LABEL-340,18,0);
    y+=26;
    MkBtn(h,"Back to the default seat",FPC_LABEL,y,220,28,FP_ID_SEATREF);
    /* The numbers here MUST match FPREF above. They did not for a day: the button said
       150 up, 3 forward, 25 to the left, near plane 36, while the sliders it resets sat at
       150 / -3 / -31 / 42 - the numbers were read off a screenshot. A label describing a control is a
       second copy of that control's truth, and it goes stale the moment nobody checks it. */
    MkTextF(h,"150 up, 3 back, 31 to the left, near plane 42, level",FPC_LABEL+232,y+6,520,16,
            FP_ID_NOTE,g_fSmall,0);
    y+=34;

    for(i=0;i<FP_NROW;i++){
        MkText(h,FPNAME[i],FPC_LABEL,y+5,170,20,0);
        fp_slider[i]=MkSliderOps(h,FPC_SLIDER,y,FPW_SLIDER,26,FP_ID_SLIDER0+i,&FP_SLIDER_OPS);
        fp_box[i]=CreateWindowExA(0,"EDIT","",WS_CHILD|WS_VISIBLE|ES_RIGHT|ES_AUTOHSCROLL,
                                  FPC_BOX,y+3,58,20,h,(HMENU)(INT_PTR)(FP_ID_BOX0+i),NULL,NULL);
        SendMessageA(fp_box[i],WM_SETFONT,(WPARAM)g_fField,TRUE);
        MkTextF(h,"cm",FPC_BOX+62,y+7,24,16,FP_ID_NOTE,g_fSmall,0);
        fp_hint[i]=MkTextF(h,FPHINT[i],FPC_HINT,y+7,FPW_HINT,16,FP_ID_HINT0+i,
                           g_fSmall,0);
        y+=30;
    }
    y+=6;

    /* ---- HORIZON: named after what it LOCKS TO, and the recommendation is written down -------
     * Reported 2026-08-05: the naming was disliked. Locks to horizon or
     * locks to the car were considered; a label was wanted stating the recommended setting for immersion.
     * "Held level" describes the picture; "Locks to horizon" describes what the camera is
     * attached to, which is the choice being made and reads the same way as its opposite.
     * The recommendation sits UNDER the pair rather than inside a button, because it is a
     * statement about one of the two options and a button that recommends itself is a button
     * arguing with the other one. It is on Locks to horizon by his own instruction - that is
     * what he drove and confirmed. */
    MkText(h,"Horizon",FPC_LABEL,y+6,170,20,0);
    fp_level_btn=MkBtn(h,"Locks to horizon",FPC_SLIDER,y,150,28,FP_ID_LEVEL);
    fp_tilt_btn =MkBtn(h,"Rolls with the car",FPC_SLIDER+160,y,190,28,FP_ID_TILT);
    y+=30;
    MkTextF(h,"Locks to horizon is the recommended setting for immersion - it is what he drove. "
              "Rolling with the car is closer to a real head and harder to watch.",
            FPC_SLIDER,y,FP_R-FPC_SLIDER,16,FP_ID_NOTE,g_fSmall,0);
    y+=26;

    /* The interface fix used to sit here, under the horizon. It moved to the TOP of the page on
       2026-08-14 at Alex's instruction - see the block in PageFpvCreate. */

    /* NO INTERFACE CONTROLS HERE, and the paragraph is the point.
     *
     * A "Corners stay put / Squeeze from centre" pair and a "Measure the interface" toggle sat
     * here for exactly one build. Alex had asked for buttons, and I built the wrong kind: he meant
     * KEYS IN THE GAME. The utility ships to end users on GitHub, and ad-hoc measurement
     * controls do not belong in its interface.
     *
     * Two reasons, and the first is the one that makes a window impossible rather than merely
     * untidy: reaching one means alt-tabbing, and Mafia PAUSES the instant it loses focus - so the
     * picture being compared stops being drawn at the moment you go to change it, and on a
     * recorded run the capture is interrupted too. The second: this utility ships to strangers,
     * and a measurement aimed at our own bench is clutter in a client.
     *
     * The switches live in the mod, on INSERT and PAGE DOWN, behind `dev_keys` - see the block in
     * `src\fp\fp_camera.c`. The shipped ini has `dev_keys = 0`; our benches have 1. */

    /* ---- THE BINDINGS, laid out like the H-shifter page's ------------------------------------
     * Same shape on purpose: a wide field saying what is bound, which turns into "press a KEY
     * now..." while it waits, and a narrow clear beside it. Two pages doing the same job should
     * not have to be learned twice. */
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    FPC_LABEL,y,FP_W,2,h,NULL,NULL,NULL);
    y+=12;
    /* TWO LINES, NOT FOUR. The first version had a heading, a bold warning and a hint, and it
       was reported as too much text, needing to be made simpler. The one fact worth the space is that a
       wheel button cannot be bound - he tried, and fp_camera.c opens no DirectInput at all - and
       it fits in the same line that says how to use the row. */
    MkTextF(h,"OPTIONAL: KEYS TO ADJUST THE CAMERA WHILE DRIVING",
            FPC_LABEL,y,FP_W,22,FP_ID_SECT,g_fSection,0);
    y+=26;
    MkTextF(h,"Keyboard only in this version - F1-F6 for example. Click a binding, then press a key.",
            FPC_LABEL,y,FP_W,18,FP_ID_NOTE,g_fSmall,0);
    y+=24;
    for(i=0;i<FK_N;i++){
        MkText(h,FKNAME[i],FPC_LABEL,y+4,170,20,0);
        fp_keyBtn[i]=MkBtn(h,"click to set",FPC_SLIDER,y,240,24,FP_ID_KEY0+i);
        MkBtn(h,"clear",FPC_SLIDER+250,y,80,24,FP_ID_KEYCLR0+i);
        y+=28;
    }
    y+=8;

    /* ---- PRESETS - the row the Force Feedback page has, under everything it covers ---------- */
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    FPC_LABEL,y,FP_W,2,h,NULL,NULL,NULL);
    y+=12;
    /* NO IMPORT / EXPORT. The FFB page has them because a force profile is worth sending to
       someone else; a seat is three numbers set in ten seconds, so it was decided: remove
       the save-preset and load-preset buttons - no need to load these presets into folders.
       The label is short because the long one was clipped - it was sized
       against FP_W-560 to leave room for buttons that are now gone. */
    /* The buttons sit just past the end of the label, not out at the right margin. They were at
       FP_R-170 because they inherited the FFB page's layout, where the row also carries import
       and export and the whole group is pushed right to line up with them. With those gone the
       three numbers were stranded a screen away from the word they belong to. */
    MkTextF(h,"PRESETS - the green one is being edited",
            FPC_LABEL,y+6,300,20,FP_ID_SECT,g_fSub,0);
    for(i=0;i<3;i++){
        char t[8]; wsprintfA(t,"%d",i+1);
        fp_slotBtn[i]=MkBtn(h,t,FPC_LABEL+310+i*54,y,48,28,FP_ID_SLOT0+i);
    }
    y+=40;

    FpvLoadIni();
    fp_RefreshSlots();
    PageSaveRow(h,LTAB_FPV,y,FpvSaveIni,FpvLoadIni);
    fp_pageH=y+8;
    g_contentH[LTAB_FPV]=fp_pageH;
}

static int PageFpvDraw(DRAWITEMSTRUCT *d,int id){
    char txt[320]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
    RECT r=d->rcItem;
    int pressed=(d->itemState&ODS_SELECTED)!=0;
    int off=!IsWindowEnabled(d->hwndItem);
    SetBkMode(d->hDC,TRANSPARENT);

    if(id==FP_ID_LEVEL||id==FP_ID_TILT||id==FP_ID_UIFIX){
        /* a SELECTOR: the live one green, exactly like the range, the presets and the wheel.
           The interface fix is a single toggle rather than one of a pair, so "self" is simply
           whether it is on - green when the fix is applied, plain when it is not. */
        int self=(id==FP_ID_LEVEL) ? fp_level  :
                 (id==FP_ID_TILT)  ? !fp_level : fp_uifix;
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,self?g_fAction:g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,self&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    /* THE BINDING FIELDS AND THEIR CLEARS.
       These were MISSING until 2026-08-07 and the page shipped without them for one build: the
       buttons are owner-drawn, so a page that does not draw an id leaves it blank. Reported:
       no Clear label was visible and there was no way to click it. The controls were
       there and working; nothing was painting them.
       Copied from PageShifterDraw so the two pages look identical, including the red arrow and
       the red text while a row is waiting for a key. */
    if(id>=FP_ID_KEY0&&id<FP_ID_KEY0+FK_N){
        HBRUSH bf=CreateSolidBrush(off?RGB(238,236,230):FIELD);
        FillRect(d->hDC,&r,bf); DeleteObject(bf);
        {
            HPEN pen=CreatePen(PS_SOLID,1,off?RGB(180,174,164):RGB(60,54,48));
            HGDIOBJ op=SelectObject(d->hDC,pen);
            HGDIOBJ ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
            Rectangle(d->hDC,r.left,r.top,r.right,r.bottom);
            SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
        }
        {
            int capturing=(fp_capture==id-FP_ID_KEY0);
            HGDIOBJ of;
            RECT t=r;
            if(!off&&(capturing||(d->itemState&ODS_FOCUS)))
                RedArrow(d->hDC,r.left-12,(r.top+r.bottom)/2);
            of=SelectObject(d->hDC,g_fField);
            SetTextColor(d->hDC,off?INK_OFF:capturing?MAFIA_RED:INK);
            t.left+=10;
            DrawTextA(d->hDC,txt,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            SelectObject(d->hDC,of);
        }
        return 1;
    }
    if(id>=FP_ID_KEYCLR0&&id<FP_ID_KEYCLR0+FK_N){
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,0);
        SelectObject(d->hDC,save);
        return 1;
    }
    if(id>=FP_ID_SLOT0&&id<FP_ID_SLOT0+3){
        /* the selected preset green, same as the FFB row */
        int self=((id-FP_ID_SLOT0)==fp_slotSel);
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,self?g_fAction:g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,self&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    if(id==FP_ID_SEATREF){
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fAction);
        DrawPressable(d->hDC,&r,txt,pressed,off,1,0);
        SelectObject(d->hDC,save);
        return 1;
    }
    return 0;
}

/* Polled from the frame's WM_TIMER while this page is up, and it returns immediately unless a row
   is actually waiting - so it costs nothing the rest of the time.
   THE BASELINE MATTERS: without it the mouse click that started the capture is still physically
   down on the first tick and would bind itself. The shifter page snapshots every key for the same
   reason. ESCAPE cancels rather than binding, so a row clicked by accident has a way out that is
   not a matter of binding something and then clearing it. */
static void PageFpvTimer(void){
    static int prevKey[256]; static int haveBase=0;
    int vk;
    if(fp_capture<0){ haveBase=0; return; }
    if(!haveBase){
        for(vk=0;vk<256;vk++) prevKey[vk]=(GetAsyncKeyState(vk)&0x8000)!=0;
        haveBase=1; return;
    }
    for(vk=0x08;vk<=0xFE;vk++){
        int now;
        if(vk==VK_LBUTTON||vk==VK_RBUTTON||vk==VK_MBUTTON) continue;
        now=(GetAsyncKeyState(vk)&0x8000)!=0;
        if(now&&!prevKey[vk]){
            int r=fp_capture, v2;
            char m[200],kb[40];
            fp_capture=-1; haveBase=0;
            if(vk!=VK_ESCAPE){
                fp_key[r]=vk;
                wsprintfA(m,"%s = %s (0x%02X)",FKNAME[r],fp_KeyName(vk,kb),vk);
                LogLine(m);
                PageDirty(LTAB_FPV);
            }
            fp_RefreshKey(r);
            for(v2=0;v2<256;v2++) prevKey[v2]=0;
            return;
        }
        prevKey[vk]=now;
    }
}

static int PageFpvCommand(int id,int code,HWND ctl){
    if(id>=FP_ID_SLOT0&&id<FP_ID_SLOT0+3){ fp_SelectSlot(id-FP_ID_SLOT0); return 1; }
    if(id>=FP_ID_KEY0&&id<FP_ID_KEY0+FK_N){
        int r=id-FP_ID_KEY0, prev=fp_capture;
        fp_capture=(prev==r)?-1:r;          /* clicking the waiting row again cancels it */
        if(prev>=0&&prev!=r) fp_RefreshKey(prev);
        fp_RefreshKey(r);
        return 1;
    }
    if(id>=FP_ID_KEYCLR0&&id<FP_ID_KEYCLR0+FK_N){
        int r=id-FP_ID_KEYCLR0;
        if(fp_capture==r) fp_capture=-1;
        if(fp_key[r]){
            char m[120]; wsprintfA(m,"%s = unbound",FKNAME[r]); LogLine(m);
            fp_key[r]=0; PageDirty(LTAB_FPV);
        }
        fp_RefreshKey(r);
        return 1;
    }
    if(id==FP_ID_LEVEL){
        if(!fp_level){ fp_level=1; fp_RefreshLevel(); PageDirty(LTAB_FPV);
                       LogLine("horizon: held level"); }
        return 1;
    }
    if(id==FP_ID_TILT){
        if(fp_level){ fp_level=0; fp_RefreshLevel(); PageDirty(LTAB_FPV);
                      LogLine("horizon: rolls with the car"); }
        return 1;
    }
    if(id==FP_ID_UIFIX){
        /* One button, so it TOGGLES. */
        fp_uifix=!fp_uifix;
        fp_RefreshUiFix();
        PageDirty(LTAB_FPV);
        LogLine(fp_uifix
                ? "wide-screen interface fix: ON - the radar and the speedometer are un-stretched,"
                  " nothing else is touched"
                : "wide-screen interface fix: OFF - the interface is drawn exactly as the game"
                  " draws it");
        return 1;
    }
    if(id==FP_ID_SEATREF){
        int i;
        for(i=0;i<FP_NROW;i++){ fp_val[i]=FPREF[i]; fp_RefreshRow(i); }
        fp_level=1; fp_RefreshLevel();
        PageDirty(LTAB_FPV);
        LogLine("back to the default seat - 150 up, 3 back, 31 to the left, near plane 42, level");
        return 1;
    }
    if(id>=FP_ID_BOX0&&id<FP_ID_BOX0+FP_NROW&&code==EN_CHANGE&&!fp_quiet){
        char t[32]; int v,row=id-FP_ID_BOX0;
        GetWindowTextA(ctl,t,sizeof(t));
        /* an empty box, or a lone "-", is somebody mid-edit rather than a value */
        if(t[0]&&!(t[0]=='-'&&!t[1])&&ParseInt(t,&v)){
            int nv=Clamp(v,FPLO[row],FPHI[row]);
            if(fp_val[row]!=nv){
                fp_val[row]=nv;
                PageDirty(LTAB_FPV);
                /* NOT fp_RefreshRow: it rewrites the box, which would fight the typing */
                if(fp_slider[row]) InvalidateRect(fp_slider[row],NULL,TRUE);
            }
        }
        return 1;
    }
    return 0;
}

#endif /* ALXG_PAGE_FPV_H */
