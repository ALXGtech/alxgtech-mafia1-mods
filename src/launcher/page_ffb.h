/* page_ffb.h - the Force Feedback tab: the tuner, ported out of src/spike/ffb_gui.c.
 *
 * NOT a rewrite. The twelve sliders, their travel and their live hints, the eight-stop rotation
 * range selector, the two-column wheel-weight table, the profile slots and the status lamp are
 * the same code they were in mafia-ffb-setup.exe. Dropped: its WinMain, its frame, its copy of
 * the skin, and three things the launcher now owns -
 *
 *   - Apply / Apply & Launch. Saving IS applying (the mod re-reads this file about once a
 *     second), so the shared save row does it, and the shared banner says it is live.
 *   - its own banner. One save model on every tab - see mod_state.h.
 *   - ModIsOn / ToggleMod, which renamed mafia_ffb.asi to .asi.off. The toggle at the top of the
 *     page installs and uninstalls through the journal, and two mechanisms for "is this mod on"
 *     is exactly the drift the journal exists to end. A self-test asserts no .asi.off is created.
 *
 * Everything is prefixed ffb_.
 *
 * THE PATH TRAP, same as the shifter's: mafia-ffb-setup.exe lived INSIDE "mafia ffb setup\" and
 * built its paths from the folder its exe sat in. This program sits in the GAME ROOT.
 *
 * The INI contract is src/ffb/ffb_settings.c: section [ffb], integer percents. These key names
 * ARE the contract - changing one silently disconnects a slider.
 */
#ifndef ALXG_PAGE_FFB_H
#define ALXG_PAGE_FFB_H

#include "ffb_devices.h"        /* which wheel, and a force you can feel - needs page_shifter.h's
                                   DirectInput declarations, which launcher.c includes first */

/* Under "ALXG mods\" since 2026-08-07 - see install_core.h's ALXG_DIR for why. Spelled here
   as a literal rather than built from ALXG_DIR so the two-line grep for "mafia ffb setup"
   finds every place it is written; ffb_settings.c inside the mod is the third. */
#define FFBDIR "ALXG mods\\mafia ffb setup"

/* The order IS the layout order. Everything below S_SPRING is a full-width strength row; from
   S_SPRING on it is the two-column wheel-weight table, where the two truck rows are drawn on the
   SAME LINE as the car row each one multiplies. */
enum { S_MASTER,S_CRASH,S_OBJ,S_PED,S_GUN,S_ROAD,S_SAT,
       S_SPRING,S_DAMPSTAT,S_DAMPMOVE,S_TRKSTAT,S_TRKMOVE,NSLIDER };

static const char *SKEY[NSLIDER] = {
    "master","crash","objects","ped","gun","road","sat",
    "spring","damper_static","damper_moving","truck_static","truck_moving" };
static const char *SNAME[NSLIDER] = {
    "Overall strength","Crashes and rams","Hitting objects","Pedestrians","Gunfire",
    "Road surface","Slide feel",
    /* "Parking damper" / "Driving damper" rather than standing/moving, per the naming decided
       2026-07-27b. The INI keys stay damper_static / damper_moving - they are the contract with
       ffb_settings.c and they match the constants they scale. A label is for the person; a key
       is for the two programs that have to agree. */
    "Centering spring","Parking damper","Driving damper","","" };
/* Slider travel. The crash slider stops at 100 because the reference formula caps at MAX_MAG -
   above it the slider would be a knob with nothing behind it - but the text box goes to 150.
   The two truck rows stop at 400 for the opposite reason: the standstill damper saturates past
   that, so the slider would stop changing the response. Their boxes go to 800, because there is
   no harm in a value typed by hand and real harm in a slider whose top half is dead. */
static const int SMAX[NSLIDER]    = { 100,100,300,300,600,300,200,200,200,200,400,400 };
static const int SBOXMAX[NSLIDER] = { 100,150,300,300,600,300,200,200,200,200,800,800 };
/* Drag and arrow-key step. 1 everywhere except the truck pair, set to 0.05 of a
   multiplier - 5 percent. */
static const int SSTEP[NSLIDER]   = { 1,1,1,1,1,1,1,1,1,1,5,5 };
/* THE RECOMMENDED VALUE PER ROW, which is 100 everywhere except gunfire.
   Reported 2026-08-12: force feedback fires when ANYBODY shoots from the player's car, and a jolt the player did
   not cause reads as the wheel going wrong rather than as a gunshot. Until the mod can tell
   whose shot it is, the recommended value for that row is ZERO - the effect is off unless
   somebody deliberately asks for it. 100 keeps a mark of its own on that slider so the old
   behaviour is visibly still there, one step away; it is simply not what we recommend. */
static const int SREF[NSLIDER]    = { 100,100,100,100,0,100,100,100,100,100,100,100 };
/* a SECOND mark, or -1. Only gunfire has one, and only because its recommended value moved
   away from 100 and 100 still means something on that row. */
static const int SALT[NSLIDER]    = { -1,-1,-1,-1,100,-1,-1,-1,-1,-1,-1,-1 };
/* Kept short on purpose: the column is narrow and the first set of these ran off the edge on
   screen while every self-test passed. A hint nobody can read is worse than no hint. */
static const char *SHINT[NSLIDER] = {
    "less of everything",
    "the reference IS the ceiling",
    "crates, bins, booths, hydrants",
    "",
    "OFF by default - see the note below",
    "curbs, tram rails, offroad",
    "the slip-angle effect",
    "",
    "",                                   /* filled in live - see ffb_DamperWord() */
    "",
    "", "" };                             /* truck rows carry no hint - no room on a shared line */

/* Which car row a truck row multiplies. -1 = not a truck row. */
static int TruckPartner(int i){
    return i==S_TRKSTAT ? S_DAMPSTAT : (i==S_TRKMOVE ? S_DAMPMOVE : -1);
}

static int ffb_val[NSLIDER];

/* The rotation range. NOT a setting we impose - a DECLARATION of what the wheel's own driver is
   set to, so the mod knows what to serve. Settled 2026-07-25: the range is selected, not set.
   The UI must say SELECT, never SET. Anything outside this list is refused by the mod, so no
   control here may produce it. Sorted for display; the mod's own table is in a different order
   because F1..F5 addressed it by index. */
#define NDOR 8
static const int DORDEG[NDOR] = { 90,360,540,600,720,900,1080,1440 };
#define DOR_DEFAULT 600
static int ffb_range = DOR_DEFAULT;
/* Ranges nobody has driven. 600 is the reference; 90 and 360 are his own measured choices; the
   rest are the cube-root law, and a law is not a drive. */
static int RangeDriven(int deg){ return deg==90||deg==360||deg==600||deg==900||deg==1080; }

/* Read from the INI, written back untouched, and deliberately NOT given a control. It scales the
   SURPLUS of heavy-vehicle steering weight, and that feature was closed outright on 2026-07-23,
   settled as: no additional weight for the trucks. A slider here would ship a feature that was refused; dropping
   the key on save would silently un-refuse it for anyone who set it by hand. */
static int ffb_truck = 0;
/* The instance GUID of the wheel the .asi should take, preserved by the same rule: a key this
   file does not know about is a key this file DESTROYS, and that has happened twice. */
static char ffb_device[80] = "";

static HWND ffb_slider[NSLIDER], ffb_box[NSLIDER], ffb_hint[NSLIDER], ffb_reset[NSLIDER];
static HWND ffb_dorBtn[NDOR], ffb_dorText, ffb_lamp, ffb_lampText;
static int  ffb_dorCx[NDOR];
static HWND ffb_slot[3], ffb_saveSlot;
static HWND ffb_devDrop, ffb_devTest, ffb_devRefresh, ffb_devHeld;
static HWND ffb_ingame;
static int  ffb_SavApplied(void);      /* defined with the rest of the profile code below */
static void ffb_SetDevice(int idx);    /* the picker, further down */
static void ffb_RefreshIngame(void){
    if(!ffb_ingame) return;
    /* The face says the STATE and nothing else. The paragraph that used to sit beside it was
       judged too strong - a wall of explanation next to a control that can simply say what
       it is. Note the OFF wording: "your own", not "default", because that is what it restores -
       a player who set their linearity for a gamepad gets THAT back, not GOG's number. */
    SetWindowTextA(ffb_ingame, ffb_SavApplied()
        ? "ON: recommended in-game FFB settings"
        : "OFF: your own in-game settings");
    InvalidateRect(ffb_ingame,NULL,TRUE);
}
static int  ffb_quiet=0;      /* set while WE are changing a text box, so EN_CHANGE knows */
static int  ffb_slotSel=0;
static int  ffb_pageH;
/* which rows changed since the last flush, and what they held before it - so a drag logs one
   line saying 100 -> 60 rather than forty lines counting down */
static int  ffb_logPend[NSLIDER];
static int  ffb_logWas[NSLIDER];

/* ---- TWO COLUMNS ----------------------------------------------------------------------------
 * Measured 2026-08-01: in one column this page came to 971 px, most of the way down a 1400 px work
 * area, and judged too large even on a big screen. Trimming words got it under 800 and no
 * further - eleven slider rows, eight range buttons and a preset row are simply that tall
 * stacked. So they are not stacked any more: the strength rows take the left half and everything
 * else takes the right, and the page is about half as tall.
 *
 * The two halves are not arbitrary. LEFT is what you drag while judging a drive; RIGHT is what
 * you set once - the range your wheel is on, the weight table, and which preset you are in. */
#define DORTXT_W 260
#define COL_LABEL 26
#define COL_SLIDER 186
#define W_SLIDER 250
#define COL_BOX 446
#define COL_HINT 522
#define W_HINT 200
#define COL_MID 750                    /* the right column starts here */
#define W_FULL (WINW-2*COL_LABEL)
#define W_LEFT (COL_MID-COL_LABEL-24)
#define W_RIGHT (WINW-COL_MID-COL_LABEL)

/* ---- the WHEEL WEIGHT table: two columns, cars left, trucks right ---------------------------
 * A truck value is a MULTIPLIER on the car value in the same condition, so putting them on one
 * line puts the multiplier physically beside the number it multiplies. Every gap here is checked
 * against the control to its left, not eyeballed. */
#define WW_LBL_W   124
#define WW_CAR_SL  (COL_MID+130)
#define WW_SL_W    130
#define WW_CAR_BX  (COL_MID+266)
#define WW_TRK_SL  (COL_MID+346)
#define WW_TRK_BX  (COL_MID+482)
/* No hint column on the truck rows. In one column it fitted at the far right; in half a window
   it clipped to two letters, and a hint nobody can read is worse than no hint - the same rule
   that shortened these strings in the first place. The static note under the table carries the
   fact instead, at every value rather than only while dragging. */
#define WW_HINT    0
#define WW_HINT_W  0
/* How wide the table actually is: label + slider + box + the `%`. The Cars/Trucks divider is drawn
   to exactly this and no further - run to the column's full width it reads as the end of the
   section rather than a seam inside it. */
#define WW_TABLE_W 344
/* The two truck rows have no entry in SNAME on purpose - it is "" so the change LOG can call them
   Trucks, parking damper and not be confused with the car row of the same name. Stacked under
   their own heading they still need something on screen, so the table draws these. */
static const char *WWTRK[2] = { "Parking damper", "Driving damper" };

/* ---- live hints ---------------------------------------------------------------------------
 * The damper is a DI condition COEFFICIENT, not a force: the output saturates, so halving it is
 * felt and doubling it is not. Rather than let a percent scale lie about that, the row says so
 * in words as the user drags. */
static const char *ffb_DamperWord(int v){
    if(v<=25)  return "loose";
    if(v<100)  return "lighter - IS felt";
    if(v==100) return "reference";
    return "heavier - barely felt";
}
static const char *ffb_CrashWord(int v){ return v>100 ? "above the approved feel" : ""; }
static const char *ffb_GunWord(int v){
    if(v==0)   return "off - the tap is silenced";
    if(v>200)  return "clusters in a shootout - a buzzer";
    return "0 silences the tap";
}
/* The hint column of the wheel-weight table sits next to the TRUCK controls, so it says what the
   TRUCK value means - the car halves are covered by the static note under the table. */
static const char *ffb_TruckWord(int v){
    if(v<100)  return "lighter than a car";
    if(v==100) return "same as a car";
    if(v<=400) return "heavier than a car";
    return "past the useful range";
}
static const char *ffb_HintFor(int i,int v){
    if(i==S_TRKSTAT||i==S_TRKMOVE) return ffb_TruckWord(v);
    if(i==S_DAMPSTAT||i==S_DAMPMOVE) return ffb_DamperWord(v);
    if(i==S_CRASH && v>100) return ffb_CrashWord(v);
    if(i==S_GUN) return ffb_GunWord(v);
    if(i==S_PED) return v==0 ? "off - pedestrian contacts go silent" : "";
    return SHINT[i];
}

/* ---- paths: the game root, plus the mod's own folder ---------------------------------------- */
static void ffb_SetupPath(char *out,const char *name){
    SCpy(out,g_gameDir); SCat(out,FFBDIR); SCat(out,"\\"); SCat(out,name);
}
static void ffb_IniPath(char *out){ ffb_SetupPath(out,"mafia_ffb.ini"); }
static void ffb_StatusPath(char *out){ ffb_SetupPath(out,"mafia_ffb_status.ini"); }

/* ---- THE GAME'S OWN SETTINGS ----------------------------------------------------------------
 * The whole force feedback was built and judged against a particular set of the GAME's settings
 * - its car handling, its own built-in force feedback, two sound levels. A player who installs
 * this and leaves the game on its factory values is not feeling what was tuned.
 *
 * So the mod applies them, and it is a TOGGLE showing STATE rather than a button that does
 * something: green means the recommended values are in the profile right now. Pressing it again
 * puts back exactly what that player had before we touched it - not the factory values, THEIRS,
 * because those are the only ones we have any right to restore.
 *
 * What was there before lives in ALXG mods\ingame-settings.bak, one section per profile. The
 * file EXISTING is the state - there is no second place to disagree with it.
 *
 * Format and cipher: ..\shared\profile_sav.h. Every write is validated first: a profile that
 * does not decrypt to 'forP' version 1 is left alone and said out loud.
 */
static void ffb_SavBakPath(char *out){
    SCpy(out,g_gameDir); SCat(out,"ALXG mods\\ingame-settings.bak");
}
/* decimal string -> dword, no CRT. The backup stores the raw 32-bit float BITS as a decimal
   number: that is exact, and a printed float is not - a value that came back 0.15624999 would
   be a different setting from the one we took away. */
static unsigned long ffb_SavNum(const char *s){
    unsigned long v=0;
    while(*s>='0'&&*s<='9'){ v=v*10u+(unsigned long)(*s-'0'); s++; }
    return v;
}
/* THREE states, not two, and the third is the one that matters. The backup file being absent
   means never touched, and that is NOT the same as the player switching it off - if those two
   were one state, switching it off would be undone by the next launch. So the file survives a
   revert with applied=0 in it, and only its absence means nobody has decided yet. */
static int ffb_SavIntent(void){
    char p[MAX_PATH]; ffb_SavBakPath(p);
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES) return -1;   /* undecided */
    return GetPrivateProfileIntA("state","applied",0,p)?1:0;
}
static int ffb_SavApplied(void){ return ffb_SavIntent()==1; }
/* read a whole profile; 0 = not a profile we understand */
static int ffb_SavRead(const char *path,unsigned char *raw,unsigned long *dw){
    HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    DWORD got=0; int ok;
    if(h==INVALID_HANDLE_VALUE) return 0;
    ok=ReadFile(h,raw,PSAV_BYTES,&got,NULL)&&got==PSAV_BYTES;
    CloseHandle(h);
    if(!ok) return 0;
    return PSavDecrypt(raw,PSAV_BYTES,dw);
}
static int ffb_SavWrite(const char *path,const unsigned long *dw,const unsigned char *raw){
    unsigned char out[PSAV_BYTES]; DWORD put=0; int ok;
    HANDLE h;
    PSavEncrypt(dw,raw,out);
    h=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if(h==INVALID_HANDLE_VALUE) return 0;
    ok=WriteFile(h,out,PSAV_BYTES,&put,NULL)&&put==PSAV_BYTES;
    CloseHandle(h);
    return ok;
}
/* apply=1 writes the recommended values and records what was there; apply=0 puts back what was
   recorded. Returns how many profiles were touched. */
static int ffb_SavToggle(int apply){
    char dir[MAX_PATH],pat[MAX_PATH],path[MAX_PATH],bak[MAX_PATH],key[32],val[32],m[400];
    WIN32_FIND_DATAA fd; HANDLE fh; int touched=0,bad=0,kept=0,fresh=0;
    ffb_SavBakPath(bak);
    SCpy(dir,g_gameDir); SCat(dir,"savegame");
    SCpy(pat,dir); SCat(pat,"\\*.sav");
    fh=FindFirstFileA(pat,&fd);
    if(fh==INVALID_HANDLE_VALUE){
        LogLine("no player profile in savegame\\ yet - start the game once, make a profile, then "
                "press this again");
        return 0;
    }
    do{
        unsigned char raw[PSAV_BYTES]; unsigned long dw[PSAV_DWORDS]; int i,wrote=0;
        SCpy(path,dir); SCat(path,"\\"); SCat(path,fd.cFileName);
        if(!ffb_SavRead(path,raw,dw)){
            wsprintfA(m,"%s is not a profile this build understands - left alone",fd.cFileName);
            LogLine(m); bad++; continue;
        }
        for(i=0;i<PSAV_NRECOMMENDED;i++){
            int idx=PSAV_RECOMMENDED[i].idx;
            unsigned long ours=PSavFromFloat(PSAV_RECOMMENDED[i].val);
            char have[32]; have[0]=0;
            wsprintfA(key,"d%d",idx);
            GetPrivateProfileStringA(fd.cFileName,key,"",have,sizeof(have),bak);
            if(apply){
                /* RECORD ONCE, and record what is in THIS player's file - not GOG's factory
                   value. Reported 2026-08-12: somebody may have set their linearity for a gamepad,
                   and handing them the factory number back would quietly ruin the game for them
                   without their ever noticing what did it.
                   Once, because a second apply that re-recorded would write OUR value into the
                   backup and turn the revert into a no-op. */
                if(!have[0]){
                    wsprintfA(val,"%lu",dw[idx]);
                    WritePrivateProfileStringA(fd.cFileName,key,val,bak);
                }
                dw[idx]=ours; wrote++;
            } else if(have[0]){
                /* DO NOT CLOBBER A LATER EDIT. If the field no longer holds our value, the player
                   changed it themselves while ours was applied, and their newer choice outranks a
                   value we wrote down before they made it. Restore only what is still ours. */
                if(dw[idx]==ours){ dw[idx]=ffb_SavNum(have); wrote++; }
                else kept++;
            }
        }
        if(apply&&!GetPrivateProfileIntA(fd.cFileName,"seen",0,bak)){
            WritePrivateProfileStringA(fd.cFileName,"seen","1",bak); fresh++;
        }
        if(wrote&&ffb_SavWrite(path,dw,raw)) touched++;
    } while(FindNextFileA(fh,&fd));
    FindClose(fh);
    /* the file SURVIVES a revert, carrying applied=0 - see ffb_SavIntent for why its
       absence has to keep meaning that nobody has decided yet. */
    WritePrivateProfileStringA("state","applied",apply?"1":"0",bak);
    wsprintfA(m,apply
        ? "recommended in-game settings applied to %d profile(s) - the game's own handling and "
          "force feedback are now what this mod was tuned against"
        : "the game's own settings put back to what they were in %d profile(s) - not to GOG's "
          "factory values, to yours",touched);
    LogLine(m);
    if(bad){ wsprintfA(m,"  %d file(s) in savegame\\ were skipped",bad); LogLine(m); }
    if(kept){
        wsprintfA(m,"  %d value(s) you changed yourself since are left exactly as you set them",kept);
        LogLine(m);
    }
    if(fresh&&apply){
        wsprintfA(m,"  %d profile(s) had not been given these settings before",fresh);
        LogLine(m);
    }
    return touched;
}
static void ffb_ProfilePath(char *out,int slot){
    char n[40]; wsprintfA(n,"profiles\\p%d.ini",slot+1);
    ffb_SetupPath(out,n);
}

/* ---- the settings file ----------------------------------------------------------------------
 * The write preserves every key it does not own, harvested out of the FILE it is about to
 * overwrite rather than remembered from load. Not theoretical: the gearbox binder wrote its own
 * built-in table back on every save and destroyed a hand-maintained config twice. Preserved state
 * belongs to the FILE, because a tool routinely writes a file it never opened. */
/* Defined below, next to ffb_LoadFrom - declared here because ffb_SaveTo needs it to decide
   which non-slider keys are present and must be carried through. */
static int ffb_KeyPresent(const char *path,const char *key);

static void ffb_SaveTo(const char *path,int announce){
    char buf[4096]; buf[0]=0; char line[512];

    ffb_truck = (int)GetPrivateProfileIntA("ffb","truck",ffb_truck,path);
    /* `device` is a STRING, so it needs the other reader. Read into a SEPARATE buffer and copy
       only on a hit: passing ffb_device as both the default and the destination would have the
       API writing into the string it is reading its default from. */
    {
        char dev[80];
        GetPrivateProfileStringA("ffb","device","",dev,sizeof(dev),path);
        if(dev[0]){ int i=0; for(;dev[i]&&i<(int)sizeof(ffb_device)-1;i++) ffb_device[i]=dev[i];
                    ffb_device[i]=0; }
    }

    /* MEASURED before changing it, 2026-08-12: nothing reads this line back. The file is rewritten
       whole, so the header is a caption for a human and not a marker anything matches on - which
       is what the migration note feared and was wrong about. */
    SCat(buf,"; Mafia force feedback - written by " BRAND_NAME ", Force Feedback tab\r\n"
             "; Every value is a PERCENT of the reference build. 100 = the shipped feel,\r\n"
             "; unchanged to the byte. The mod re-reads this file about once a second, so a\r\n"
             "; change here takes effect mid-drive.\r\n\r\n[ffb]\r\n");
    for(int i=0;i<S_TRKSTAT;i++){
        wsprintfA(line,"%s=%d\r\n",SKEY[i],ffb_val[i]); SCat(buf,line);
    }
    SCat(buf,"\r\n; TRUCKS. A MULTIPLIER on the two damper lines above, applied only in a heavy\r\n"
             "; vehicle - so 100 = a truck damps exactly like a car, and neither line does\r\n"
             "; anything in a car at all. There is no truck spring: a truck's centering is the\r\n"
             "; car's, by design.\r\n"
             "; These are arithmetic off the shipped constants - nobody has driven them.\r\n");
    for(int i=S_TRKSTAT;i<NSLIDER;i++){
        wsprintfA(line,"%s=%d\r\n",SKEY[i],ffb_val[i]); SCat(buf,line);
    }
    SCat(buf,"\r\n; Heavy-vehicle steering surplus. 0 = a truck steers like a car, which is the\r\n"
             "; shipped ruling. No control in the utility - this line is kept as you left it.\r\n");
    wsprintfA(line,"truck=%d\r\n",ffb_truck); SCat(buf,line);
    SCat(buf,"\r\n; The rotation range your WHEEL DRIVER is set to, in degrees. This does not\r\n"
             "; change the wheel - nothing can, from here - it tells the mod which range to\r\n"
             "; serve. Accepted: 90, 360, 540, 600, 720, 900, 1080, 1440.\r\n"
             ";\r\n"
             "; 600 is the reference: every constant in this mod was chosen by hand on a\r\n"
             "; SIMAGIC 12 Nm wheelbase set to 600 degrees and 6.6 Nm. That is what 100% means.\r\n");
    wsprintfA(line,"range=%d\r\n",ffb_range); SCat(buf,line);

    SCat(buf,"\r\n; WHICH WHEEL, if you have more than one force-feedback device. Empty or\r\n"
             "; absent, the mod takes the first one Windows offers. The mod LOGS every device\r\n"
             "; it is offered, with its GUID - that log is where you read the string to paste.\r\n");
    if(ffb_device[0]){ wsprintfA(line,"device=%s\r\n",ffb_device); SCat(buf,line); }
    else               SCat(buf,"; device={XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}\r\n");

    /* ================= KEYS THIS PAGE DOES NOT EDIT, CARRIED THROUGH VERBATIM =================
     * This function builds the whole file in a buffer and writes it with CREATE_ALWAYS, so ANY
     * key it does not print is destroyed. That is fine for the sliders it owns and fatal for
     * everything else, because the mod reads more keys than this dialog shows.
     *
     * Found 2026-08-07, before it could bite: `impulse_kick` defaults to 0 in the .asi, so one
     * drag of any slider would have silently turned the impulse channel OFF - the channel just
     * labeled the reference - and the file would have looked perfectly normal afterwards.
     * The risk was summarized as: a utility that must not spoil everything.
     *
     * Only keys that are ACTUALLY PRESENT are written back. Printing them unconditionally would
     * bake this build's defaults into every file the utility touches, which is the same fault
     * pointing the other way: a later change to a default in the .asi would stop reaching
     * anyone whose file had been saved once.
     *
     * When a new cfg key is added to ffb_settings.c and it is not a slider on this page, it
     * belongs in this list. */
    {
        static const char *PASS[] = {
            "impulse_kick",       /* the impulse channel - DEFAULTS TO 0, see above */
            "impulse_confirm",    /* the speedometer veto that killed the phantoms */
            "tap_needs_impulse",  /* the manifold tap without its 450 ms wait */
            "car_snap",           /* the 1.2 GB dump switch */
            "tick_log",           /* the per-tick input log */
            "tear_fix",           /* torn-sample guard, a DETECTOR input */
            "ceil_agree",         /* ceiling-kick agreement, a DETECTOR input */
            NULL
        };
        int wrote=0, i;
        for(i=0;PASS[i];i++){
            if(!ffb_KeyPresent(path,PASS[i])) continue;
            if(!wrote){
                SCat(buf,"\r\n; ---- carried over from the file as it was ----\r\n"
                         "; These are not controls on this page. The utility preserves them so\r\n"
                         "; that saving a slider cannot change how the mod DETECTS anything.\r\n");
                wrote=1;
            }
            wsprintfA(line,"%s=%d\r\n",PASS[i],
                      (int)GetPrivateProfileIntA("ffb",PASS[i],0,path));
            SCat(buf,line);
        }
    }

    HANDLE h=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){ char m[400]; wsprintfA(m,"could NOT write %s",path); LogLine(m); return; }
    DWORD w; WriteFile(h,buf,(DWORD)SLen(buf),&w,NULL); CloseHandle(h);
    if(announce){ char m[400]; wsprintfA(m,"saved %s",path); LogLine(m); }
}

static void ffb_RefreshRow(int i);
static void ffb_RefreshDor(void);
static void ffb_RefreshDev(void);
static void ffb_SyncTestBtn(void);

/* Is a key actually present, as opposed to absent-and-defaulted? Asked with TWO different
   defaults, because GetPrivateProfileInt returns UINT: a negative sentinel comes back as
   0xFFFFFFFF and `result < 0` is a comparison that can never be true. */
static int ffb_KeyPresent(const char *path,const char *key){
    return (int)GetPrivateProfileIntA("ffb",key,1,path)
        == (int)GetPrivateProfileIntA("ffb",key,2,path);
}

static void ffb_LoadFrom(const char *path){
    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) return;
    /* MIGRATION, and it has to match the .asi's exactly (ffb_settings.c) or the dialog and the
       mod disagree about a file neither of them wrote. `damper=` was one control until
       2026-07-27b; a file that still carries it loads that number into BOTH halves. */
    int dampLegacy=(int)GetPrivateProfileIntA("ffb","damper",100,path);
    for(int i=0;i<NSLIDER;i++){
        int dflt = (i==S_DAMPSTAT||i==S_DAMPMOVE) ? dampLegacy : SREF[i];
        ffb_val[i]=Clamp((int)GetPrivateProfileIntA("ffb",SKEY[i],dflt,path),0,SBOXMAX[i]);
    }
    if(ffb_KeyPresent(path,"damper") && !ffb_KeyPresent(path,"damper_static")){
        char m[220];
        wsprintfA(m,"this file predates the damper split - damper=%d carried into both lines",
                  dampLegacy);
        LogLine(m);
    }
    ffb_truck=(int)GetPrivateProfileIntA("ffb","truck",0,path);
    int deg=(int)GetPrivateProfileIntA("ffb","range",DOR_DEFAULT,path);
    int ok=0; for(int i=0;i<NDOR;i++) if(DORDEG[i]==deg) ok=1;
    if(ok) ffb_range=deg;
    else { char m[200]; wsprintfA(m,"range=%d in that file is not one the mod accepts - keeping %d",
                                 deg,ffb_range); LogLine(m); }
    /* THE CHOSEN DEVICE IS READ HERE TOO, not only harvested in ffb_SaveTo. Without this the
       selector would light "First one offered" over a file that names a wheel - the same shape of
       lie the H-shifter page paid for on 2026-08-01, where a row read "click to set" over a real
       binding. Into a separate buffer: passing ffb_device as both the default and the destination
       has the API writing into the string it is reading its default from. */
    {
        char dev[80];
        GetPrivateProfileStringA("ffb","device","",dev,sizeof(dev),path);
        SCpy(ffb_device,dev);
    }
    for(int i=0;i<NSLIDER;i++){ ffb_RefreshRow(i); ffb_logWas[i]=ffb_val[i]; ffb_logPend[i]=0; }
    ffb_RefreshDor();
    ffb_RefreshDev();
}

/* what the shared save row calls */
/* Called by the flush timer whenever anything on this page moved. TWO files, always both: the one
   the mod reads, and the preset that is selected. The active preset IS what you are editing
   (settled 2026-08-01), so a session's work is never sitting only in a working file that the next
   preset click would overwrite.

   It also drains the change log. A drag reports ONE line - the value it landed on - because
   reporting every mouse-move would bury the session in noise, and the log exists to be read. */
static void ffb_MkDirs(void){
    char dir[MAX_PATH];
    /* Three levels now, one CreateDirectory each: the API makes exactly one, so skipping the
       parent leaves the other two silently uncreated and the ini is written nowhere. */
    SCpy(dir,g_gameDir); SCat(dir,ALXG_DIR); CreateDirectoryA(dir,NULL);
    SCpy(dir,g_gameDir); SCat(dir,FFBDIR);   CreateDirectoryA(dir,NULL);
    SCat(dir,"\\profiles");                  CreateDirectoryA(dir,NULL);
}
static void FfbSaveIni(void){
    char p[MAX_PATH];
    int i;
    for(i=0;i<NSLIDER;i++) if(ffb_logPend[i]){
        char m[200];
        const char *nm=SNAME[i][0]?SNAME[i]
                     :(i==S_TRKSTAT?"Trucks, parking damper":"Trucks, driving damper");
        wsprintfA(m,"%s: %d%% -> %d%%",nm,ffb_logWas[i],ffb_val[i]);
        LogLine(m);
        ffb_logWas[i]=ffb_val[i];
        ffb_logPend[i]=0;
    }
    ffb_MkDirs();
    ffb_IniPath(p);                 ffb_SaveTo(p,0);   /* the file the mod re-reads */
    ffb_ProfilePath(p,ffb_slotSel); ffb_SaveTo(p,0);   /* and the preset being edited */
    PageSaved(LTAB_FFB);
}
static void FfbLoadIni(void){
    char p[MAX_PATH]; ffb_IniPath(p);
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES){
        /* NO FILE IS A STATE, NOT A REASON TO SHOW NOTHING IN PARTICULAR.
         *
         * This used to return here, leaving every slider holding whatever ffb_val[] happened to
         * contain - zeros on a fresh process, and on any later call the values of the folder we
         * were pointed at BEFORE. A page that has not loaded is showing numbers it invented, and
         * the one thing this window promises is that it shows the state.
         *
         * The recommended set is what "no file" means: it is exactly what switching the mod on
         * will write, and it is what Reset to Default writes - so the two can no longer disagree,
         * which is what Alex reported on 2026-08-15 for the gunfire row.
         * The damper pair follows SREF here too; there is no legacy `damper` key to inherit from
         * in a file that does not exist. */
        int i;
        for(i=0;i<NSLIDER;i++) ffb_val[i]=SREF[i];
        LogLine("no mafia_ffb.ini yet - showing the recommended settings, which is what switching "
                "the mod on will write");
        return;
    }
    ffb_LoadFrom(p);
    PageSaved(LTAB_FFB);
    LogLine("settings reloaded from mafia_ffb.ini");
}

/* ---- the mod's own status file ---------------------------------------------------------------
 * Written by the .asi (src/ffb/ffb_status.c). Two words, not one: `found` says we hold the wheel,
 * `effective` says the last force actually reached it - so the lamp can never read "connected"
 * while the wheel is silent. `generation` is the heartbeat. */
static DWORD ffb_lastGen=0, ffb_lastGenSeen=0;
static int   ffb_lampState=-1;      /* -1 unknown, 0 red, 1 green, 2 amber, 3 never run */
static char  ffb_devName[128]="";

static void PageFfbPoll(void){
    char path[MAX_PATH]; ffb_StatusPath(path);
    int was=ffb_lampState;
    char wasDev[128]; SCpy(wasDev,ffb_devName);

    /* after PageGrey, which enables every child it walks - see ffb_SyncTestBtn */
    ffb_SyncTestBtn();

    /* ONCE per session, and only while the mod is on. Undecided means apply - that is the whole
       point, the forces were tuned against these values and the toggle can undo it. Already
       applied means apply again, which is how a profile CREATED SINCE gets them too. An explicit
       0 means the player said no, and nothing here is allowed to argue. */
    {
        static int synced=0;
        if(!synced && g_state[LTAB_FFB]==JMOD_ON){
            int intent=ffb_SavIntent();
            synced=1;
            if(intent!=0){ ffb_SavToggle(1); ffb_RefreshIngame(); }
            /* AND PICK A WHEEL. As of 2026-08-12, switching the mod on should put the first
               device in the box by itself. With nothing chosen the mod already takes the first
               one offered, so this changes no behaviour - it makes the choice VISIBLE and
               nameable, which is the half that was missing. Only when something is attached and
               nothing has been chosen: a device the user picked is never overridden. */
            if(FfbDevChosen(ffb_device)==FFBDEV_ANY && ffbdev_n>0){
                LogLine("no wheel was chosen - taking the first force-feedback device offered");
                ffb_SetDevice(0);
            }
        }
    }

    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES){
        /* its own state, not "no wheel": the mod has simply never started here, and gluing that
           onto the red "No wheel driven" line read as a fault when nothing is wrong yet */
        ffb_lampState=3; ffb_devName[0]=0;
    } else {
        DWORD gen=(DWORD)GetPrivateProfileIntA("status","generation",0,path);
        int eff=(int)GetPrivateProfileIntA("status","effective",0,path);
        int found=(int)GetPrivateProfileIntA("status","found",0,path);
        GetPrivateProfileStringA("status","device","<none enumerated>",ffb_devName,
                                 sizeof(ffb_devName),path);
        DWORD now=GetTickCount();
        if(gen!=ffb_lastGen){ ffb_lastGen=gen; ffb_lastGenSeen=now; }
        int live=(ffb_lastGenSeen!=0)&&((now-ffb_lastGenSeen)<20000u);
        if(!live)      ffb_lampState=2;   /* the file is old: the game is not running */
        else if(eff)   ffb_lampState=1;
        else           ffb_lampState=0;
        (void)found;
    }
    if((ffb_lampState!=was||!SEq(wasDev,ffb_devName))&&ffb_lampText){
        char t[300];
        if(ffb_lampState==1)      wsprintfA(t,"Driving effects on %s",ffb_devName);
        else if(ffb_lampState==2) wsprintfA(t,"Game not running - last seen on %s",ffb_devName);
        /* Reported 2026-08-05: the lamp did not make clear whether to start the game once with the mod
           installed, or disable it first. The old line said
           what had not happened and left the instruction to be guessed - and the wrong guess
           (switch it off first) is the one that guarantees the line never changes. So the text now
           states the ACTION, and it states a different action depending on whether the mod is on,
           because "start the game once" is advice that cannot work while the mod is off. */
        else if(ffb_lampState==3) SCpy(t, g_state[LTAB_FFB]==JMOD_ON
            ? "Not run here yet - leave it switched ON and start Mafia once, then this line "
              "says which wheel it took"
            : "Not run here yet - switch the mod on, then start Mafia once");
        else                      wsprintfA(t,"No force is reaching the wheel - %s",ffb_devName);
        SetWindowTextA(ffb_lampText,t);
        if(ffb_lamp) InvalidateRect(ffb_lamp,NULL,TRUE);
        InvalidateRect(ffb_lampText,NULL,TRUE);
    }

    /* WHAT THE MOD TOOK, next to what was chosen - the two are different questions and the whole
       point of the picker is the case where they differ. The mod already announces a substitution
       in its own log (diag 0x15); this is the same fact where somebody will actually see it.
       Compared by NAME because that is all the status file carries: two identical wheels cannot be
       told apart here, which is what the Test button is for. */
    if(ffb_devHeld){
        char t[400]; int chosen=FfbDevChosen(ffb_device);
        t[0]=0;
        /* The two facts this line can carry, in priority order. A chosen device that is not here
           outranks anything the status file says: it is about to become a substitution, and saying
           so BEFORE the game runs is the only warning that arrives in time. */
        if(chosen==FFBDEV_GONE)
            wsprintfA(t,"the wheel you chose is not plugged in - %s. The mod will fall back to the "
                        "first device offered.",ffb_device);
        /* Nothing when there is no device: the dropdown's own face already says so, and the same
           sentence twice on one line reads as two different problems. */
        else if(ffbdev_n==0)
            SCpy(t,"");
        else if(ffb_lampState==3||!ffb_devName[0])
            SCpy(t,"");
        else if(chosen==FFBDEV_ANY)     wsprintfA(t,"the mod took %s",ffb_devName);
        else if(SEq(ffbdev_name[chosen],ffb_devName))
                                        wsprintfA(t,"the mod is on %s, as chosen",ffb_devName);
        else                            wsprintfA(t,"NOT your choice - the mod is on %s",ffb_devName);
        SetWindowTextA(ffb_devHeld,t);
        InvalidateRect(ffb_devHeld,NULL,TRUE);
    }
}

/* The dropdown's FACE says what is chosen, in words, so it reads the same greyed as it does live -
   which is the other half of his complaint about the first version: "the button labels are unclear
   in the disabled state". A row of identical grey slabs says nothing when disabled; one line of
   text says the same thing in either state. */
/* The Test button is dead while there is nothing for it to test. Per the 2026-08-12 request, make it
   unavailable when no device is there.
   THE CONDITION IS "the list is empty", not "nothing is chosen", and the difference matters:
   the normal, shipped state of this page is "first one offered", i.e. nothing chosen by GUID,
   and that is exactly the case where the button earns its keep - it is the only way to find out
   WHICH wheel "the first one" is. Disabling it there would take the button away in the state
   almost every user is in.
   The page-wide greying (PageGrey, keyed on the mod being switched on) enables every child it
   walks, so this has to be re-applied after it - which is why the poll calls it too. */
static void ffb_SyncTestBtn(void){
    if(!ffb_devTest) return;
    int pageOn = (g_state[LTAB_FFB]==JMOD_ON);
    int want   = (pageOn && ffbdev_n>0);
    if(!IsWindowEnabled(ffb_devTest) != !want){
        EnableWindow(ffb_devTest,want);
        InvalidateRect(ffb_devTest,NULL,TRUE);
    }
}

static void ffb_RefreshDev(void){
    char t[320];
    int chosen=FfbDevChosen(ffb_device);
    ffb_SyncTestBtn();
    if(!ffb_devDrop) return;
    if(chosen>=0)                    wsprintfA(t,"%s",ffbdev_name[chosen]);
    else if(chosen==FFBDEV_GONE)     SCpy(t,"the wheel you chose is not plugged in");
    else if(ffbdev_n==0)             SCpy(t,"no device found - press Refresh list");
    else if(ffbdev_n==1)             wsprintfA(t,"first one offered - %s",ffbdev_name[0]);
    /* NAME THE DEVICE even when there are several. Whichever Windows names first is true and
       useless: the one thing a person wants from this line is WHICH wheel, and we know it. */
    else                             wsprintfA(t,"first one offered - %s",ffbdev_name[0]);
    SetWindowTextA(ffb_devDrop,t);
    InvalidateRect(ffb_devDrop,NULL,TRUE);
}


static void ffb_SetDevice(int idx){
    char s[48];
    char m[400];
    if(idx<0){
        if(!ffb_device[0]) return;
        ffb_device[0]=0;
        LogLine("wheel: whichever force-feedback device Windows offers first");
    } else {
        if(idx>=ffbdev_n) return;
        FfbGuidStr(&ffbdev_guid[idx],s);
        if(SEq(s,ffb_device)) return;
        SCpy(ffb_device,s);
        wsprintfA(m,"wheel: %s  %s",ffbdev_name[idx],s);
        LogLine(m);
    }
    PageDirty(LTAB_FFB);
    ffb_RefreshDev();
}

/* ---- the rows ---- */
static void ffb_RefreshRow(int i){
    char t[16]; wsprintfA(t,"%d",ffb_val[i]);
    if(!ffb_box[i]) return;
    ffb_quiet=1; SetWindowTextA(ffb_box[i],t); ffb_quiet=0;
    /* GUARDED, and not defensively for its own sake: the truck rows have no hint window at all,
       and InvalidateRect(NULL,...) does not fail quietly - it invalidates EVERY window on the
       desktop. */
    if(ffb_hint[i]){
        SetWindowTextA(ffb_hint[i],ffb_HintFor(i,ffb_val[i]));
        InvalidateRect(ffb_hint[i],NULL,TRUE);
    }
    if(ffb_slider[i]) InvalidateRect(ffb_slider[i],NULL,TRUE);
    /* the undo arrow exists only while there is something to undo */
    if(ffb_reset[i]) ShowWindow(ffb_reset[i],ffb_val[i]==SREF[i]?SW_HIDE:SW_SHOW);
}

static void ffb_RefreshDor(void){
    /* Three states, not two. 600 is the range everything was tuned on and it says so; the two he
       measured by hand are simply the range; everything else is the cube-root law continued,
       never driven. NO NUMBER in any of them: the line is parked under the button it describes. */
    const char *t = (ffb_range==DOR_DEFAULT) ? "everything was tuned here"
                  : RangeDriven(ffb_range)   ? "driven and confirmed"
                                             : "calculated, never driven";
    if(!ffb_dorText) return;
    SetWindowTextA(ffb_dorText,t);
    SendMessageA(ffb_dorText,WM_SETFONT,
                 (WPARAM)(ffb_range==DOR_DEFAULT?g_fAction:g_fField),TRUE);

    int sel=-1;
    for(int i=0;i<NDOR;i++) if(DORDEG[i]==ffb_range) sel=i;
    if(sel>=0){
        int x=ffb_dorCx[sel]-DORTXT_W/2;
        if(x<COL_LABEL)               x=COL_LABEL;
        if(x+DORTXT_W>WINW-COL_LABEL) x=WINW-COL_LABEL-DORTXT_W;
        RECT rc; GetWindowRect(ffb_dorText,&rc);
        HWND par=GetParent(ffb_dorText);
        POINT p={rc.left,rc.top}; ScreenToClient(par,&p);
        if(p.x!=x){
            /* the old position has to be repainted too - the control is moving off it and
               nothing else owns those pixels */
            RECT old={p.x,p.y,p.x+DORTXT_W,p.y+18};
            InvalidateRect(par,&old,TRUE);
            SetWindowPos(ffb_dorText,NULL,x,p.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
    }
    InvalidateRect(ffb_dorText,NULL,TRUE);
    for(int i=0;i<NDOR;i++) if(ffb_dorBtn[i]) InvalidateRect(ffb_dorBtn[i],NULL,TRUE);
}

/* Every change says what it was and what it became - see FfbSaveIni for why the slider rows are
   queued rather than logged here. */
static void ffb_SetRange(int idx){
    int deg=DORDEG[Clamp(idx,0,NDOR-1)];
    if(ffb_range!=deg){
        char m[160];
        wsprintfA(m,"wheel rotation range: %d -> %d degrees",ffb_range,deg);
        LogLine(m);
        PageDirty(LTAB_FFB);
    }
    ffb_range=deg;
    ffb_RefreshDor();
}
static void ffb_SetRow(int row,int v){
    int nv=Clamp(v,0,SBOXMAX[row]);
    if(ffb_val[row]!=nv){ ffb_logPend[row]=1; PageDirty(LTAB_FFB); }
    ffb_val[row]=nv;
    ffb_RefreshRow(row);
}

/* the callback block the shared slider class drives this page through */
static int  ffb_SlGet(int row){ return ffb_val[row]; }
static void ffb_SlSet(int row,int v){ ffb_SetRow(row,v); }
static int  ffb_SlMax(int row){ return SMAX[row]; }
static int  ffb_SlStep(int row){ return SSTEP[row]; }
/* Every row on this page is a PERCENT of the shipped feel, so 100 is the reference on all of them -
   which is what the slider class used to assume for the whole program. */
static int  ffb_SlRef(int row){ return SREF[row]; }
static int  ffb_SlAlt(int row){ return SALT[row]; }
/* The id base is a MACRO shared with the id block further down rather than a literal repeated in
   two places: a slider whose idBase disagrees with its control ids addresses the wrong rows and
   nothing says a word. (A static assert was tried first and is not available here - a member read
   from a static const struct is not an integer constant expression, and clang folding it anyway
   would have made this file depend on that.) */
#define FF_SLIDER_ID_BASE 5000
static const slider_ops FFB_SLIDER_OPS = {
    ffb_SlGet, ffb_SlSet, ffb_SlMax, ffb_SlStep, ffb_SlRef, ffb_SlAlt,
    FF_SLIDER_ID_BASE };

static void ffb_Recommended(void){
    for(int i=0;i<NSLIDER;i++){ ffb_val[i]=SREF[i]; ffb_RefreshRow(i); }
    PageDirty(LTAB_FFB);
    LogLine("back to the mod's default settings - every slider is 100%, except gunfire, which is recommended OFF");
}

static void ffb_RefreshSlots(void){
    for(int i=0;i<3;i++) if(ffb_slot[i]) InvalidateRect(ffb_slot[i],NULL,TRUE);
}
/* Clicking a preset SWITCHES to it, always - that is what makes the row a switch rather than
   three separate commands. There is no save button any more, so an empty preset takes whatever
   is on screen: the first thing that happens to a fresh preset is that it becomes the live one,
   which is also the only behaviour that leaves no dead control on the row. */
static void ffb_SelectSlot(int i){
    char p[MAX_PATH],m[200];
    ffb_ProfilePath(p,i);
    ffb_slotSel=i;
    ffb_RefreshSlots();
    if(GetFileAttributesA(p)==INVALID_FILE_ATTRIBUTES){
        wsprintfA(m,"PRESET %d WAS EMPTY - it now holds these settings, and is the one being edited",
                  i+1);
        LogLine(m);
        FfbSaveIni();
        return;
    }
    ffb_LoadFrom(p);
    FfbSaveIni();                 /* the mod plays what you are looking at, immediately */
    wsprintfA(m,"PRESET %d is now in force - the mod picks it up within a second",i+1);
    LogLine(m);
}
/* IMPORT AND EXPORT - the only place a file is chosen by hand, and the way a preset is handed to
 * somebody else.
 *
 * An import ASKS FIRST and names the preset it is about to replace, because that preset is
 * somebody's settings and a file dialog gives no hint that anything is about to be overwritten.
 * Settled 2026-08-01. */
static void ffb_FileDialog(int save){
    char buf[MAX_PATH]; buf[0]=0;
    /* OPEN WHERE THE MOD LIVES, not in Documents. Decided 2026-08-03: the Export Preset dialog opens in
       the game's own folder, so the user understands where the mod is and where the presets
       might be, keeping everything in one place. Windows had been offering Documents, which is
       where nothing of this mod is. `mafia ffb setup` is the folder inside the game that already
       holds the ini the mod reads and the `profiles\` the slots write - so an exported preset lands
       beside them instead of somewhere the next person has to go looking for.
       The buffer must outlive the call, hence static: OPENFILENAME keeps the pointer. */
    static char initDir[MAX_PATH];
    SCpy(initDir,g_gameDir); SCat(initDir,FFBDIR);
    ffb_MkDirs();                 /* it may not exist yet on a fresh install */
    OPENFILENAMEA ofn; for(int i=0;i<(int)sizeof(ofn);i++) ((BYTE*)&ofn)[i]=0;
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_frame;
    ofn.lpstrFilter="Force feedback preset (*.ini)\0*.ini\0All files\0*.*\0";
    ofn.lpstrInitialDir=initDir;
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH; ofn.lpstrDefExt="ini";
    ofn.lpstrTitle=save?"Export this preset to a file":"Import a preset from a file";
    ofn.Flags=save?OFN_OVERWRITEPROMPT:(OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST);
    int ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if(!ok) return;
    if(save){
        char m[MAX_PATH+80];
        ffb_SaveTo(buf,0);
        wsprintfA(m,"EXPORTED preset %d to %s",ffb_slotSel+1,buf);
        LogLine(m);
        return;
    }
    {
        char q[800],m[MAX_PATH+80];
        wsprintfA(q,"Replace preset %d with the settings in this file?\r\n\r\n%s\r\n\r\n"
                    "Preset %d is the one being edited, so it becomes what the game plays with "
                    "as well. The other presets are untouched.",
                  ffb_slotSel+1,buf,ffb_slotSel+1);
        if(AlxgBox(g_frame,"Import a preset",q,"Replace it","Cancel",NULL,NULL)!=IDOK) return;
        ffb_LoadFrom(buf);
        FfbSaveIni();          /* into preset N and into the file the mod reads */
        wsprintfA(m,"IMPORTED into preset %d - it is in force within a second",ffb_slotSel+1);
        LogLine(m);
    }
}

/* ---- the page ---- */
#define FF_ID_SLIDER0 FF_SLIDER_ID_BASE
#define FF_ID_BOX0    5100
#define FF_ID_HINT0   5200
#define FF_ID_RESET0  5250
#define FF_ID_DOR0    5300
#define FF_ID_DORTXT  5350
#define FF_ID_RECOMM  5360
#define FF_ID_LAMP    5361
#define FF_ID_LAMPTXT 5362
#define FF_ID_SLOT0   5370
#define FF_ID_SAVESLOT 5380
#define FF_ID_LOADF   5381
#define FF_ID_SAVEF   5382
#define FF_ID_SECT0   5390
#define FF_ID_RECLBL  5395
#define FF_ID_NOTE    5396      /* the small grey asides */
/* 5400.. are also the POPUP MENU ids: TrackPopupMenu returns one of these, so the dropdown and the
   list cannot drift about which item means which device. */
#define FF_ID_DEV0    5400      /* one per force-feedback device */
#define FF_ID_DEVANY  (FF_ID_DEV0+FFBDEV_MAX)
#define FF_ID_DEVDROP 5419
#define FF_ID_DEVTEST 5420
#define FF_ID_DEVHELD 5421
/* Reported 2026-08-12: the utility was opened with the wheel switched OFF, the wheel was switched on
   afterwards, and it never appeared - closing and reopening the window was the only way to see it.
   The list was enumerated once, when the page was built. DirectInput has no obligation to tell us
   about a device that arrives later, so the honest fix is to let the person say "look again". */
#define FF_ID_DEVREFRESH 5422
/* The empty-list item. Not a device and not selectable - it exists because an empty menu says
   "this is broken" and this sentence says what to do about it. */
#define FF_ID_DEVNONE 5423
/* The game's own settings. A TOGGLE SHOWING STATE, not a button that does something - green
   means the recommended values are in the player's profile right now. It is applied by default
   the moment the mod is switched on, because it is reversible and because the forces were tuned
   against those values; un-pressing it puts back what the player had. */
#define FF_ID_INGAME  5424

/* The list. TPM_RETURNCMD, so the choice comes back here instead of arriving later as a WM_COMMAND
   the page would have to tell apart from a button press. Down here rather than beside the other
   device code because it is the first thing in this file that needs the ids above. */
static void ffb_DevDropDown(void){
    HMENU m=CreatePopupMenu();
    RECT r;
    int chosen=FfbDevChosen(ffb_device),i,pick;
    if(!m) return;
    for(i=0;i<ffbdev_n;i++)
        AppendMenuA(m,MF_STRING|(i==chosen?MF_CHECKED:0),(UINT_PTR)(FF_ID_DEV0+i),ffbdev_name[i]);
    /* An empty list is the case that sent him looking for a bug in the mod. Say what happened and
       what to do, in the list itself, where he is already looking - greyed, because it is a
       sentence rather than something to pick. */
    if(!ffbdev_n)
        AppendMenuA(m,MF_STRING|MF_GRAYED|MF_DISABLED,(UINT_PTR)FF_ID_DEVNONE,
                    "No device found - if your wheel is plugged in now, press Refresh list");
    AppendMenuA(m,MF_SEPARATOR,0,NULL);
    /* First one offered is LAST and named as what it does, not as none: it is an empty device=
       and a real choice with a real consequence, not the absence of one. */
    AppendMenuA(m,MF_STRING|(chosen==FFBDEV_ANY?MF_CHECKED:0),(UINT_PTR)FF_ID_DEVANY,
                "First force-feedback device Windows offers");
    GetWindowRect(ffb_devDrop,&r);
    pick=(int)TrackPopupMenu(m,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|TPM_NONOTIFY,
                             r.left,r.bottom,0,g_frame,NULL);
    DestroyMenu(m);
    if(pick==FF_ID_DEVANY) ffb_SetDevice(-1);
    else if(pick>=FF_ID_DEV0&&pick<FF_ID_DEV0+ffbdev_n) ffb_SetDevice(pick-FF_ID_DEV0);
}
#define FFB_TIMER     2

static void ffb_MkRow(HWND h,int i,int y,int lblX,int lblW,int slX,int slW,int bxX,
                      int hintX,int hintW){
    if(lblW) MkText(h,SNAME[i],lblX,y+4,lblW,18,0);
    /* the per-row undo loop, left of the slider, shown only when the row has been moved off
       100 - at the reference there is nothing to undo and a permanent row of arrows is noise */
    ffb_reset[i]=MkBtn(h,"",slX-26,y+3,20,20,FF_ID_RESET0+i);
    ShowWindow(ffb_reset[i],SW_HIDE);
    ffb_slider[i]=MkSliderOps(h,slX,y,slW,26,FF_ID_SLIDER0+i,&FFB_SLIDER_OPS);
    ffb_box[i]=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","100",
        WS_CHILD|WS_VISIBLE|ES_RIGHT|ES_AUTOHSCROLL,bxX,y+1,56,22,h,
        (HMENU)(INT_PTR)(FF_ID_BOX0+i),NULL,NULL);
    SendMessageA(ffb_box[i],WM_SETFONT,(WPARAM)g_fField,TRUE);
    MkTextF(h,"%",bxX+62,y+5,12,18,0,g_fField,0);
    if(hintW){
        ffb_hint[i]=MkTextF(h,SHINT[i],hintX,y+5,hintW,18,FF_ID_HINT0+i,g_fSmall,0);
    }
}

static void PageFfbCreate(HWND h){
    int y,ry,colTop;
    PageHeader(h,LTAB_FFB);
    y=PageBanner(h,LTAB_FFB,PAGE_TOP);

    ffb_lamp=MkBtn(h,"",COL_LABEL,y,22,22,FF_ID_LAMP);
    EnableWindow(ffb_lamp,FALSE);
    ffb_lampText=MkText(h,"waiting for the mod...",COL_LABEL+28,y+3,W_FULL-28,18,FF_ID_LAMPTXT);
    y+=30;

    /* ================= FIRST OF ALL: WHICH WHEEL, IN ONE LINE =================
     * Above the range, per the instruction to put it above the angles, and the order is the order the two
     * settings depend on each other in: which device, THEN how many degrees that device is set to.
     * A range declared for a wheel the mod is not driving is a number about nothing.
     *
     * ONE LINE, corrected from the first attempt: the menu for choosing the
     * device was too big, stretched out, unclear - the wheel choice needed to be literally in one line.
     * Since the window is wide, everything fits there: a combo box, a dropdown and
     * a button. The first version was one button per device across the full width - three grey
     * slabs when the page is greyed, and no way to tell which was a device and which was an
     * option.
     *
     * The dropdown is an owner-drawn button plus TrackPopupMenu, not a comctl32 combo: a combo
     * cannot be skinned past its border, and one grey Windows control on Mafia paper reads as a
     * defect - the same reason the sliders are a custom class.
     *
     * NO "Confirm device" BUTTON, though he offered the idea. Nothing else in this program has one:
     * a change writes itself on a 300 ms timer, and a button that confirms what you already did is
     * a button that can be forgotten. The button on this line is the one that answers a question
     * text cannot - which wheel is THIS one. */
    /* ---- the game's own settings, above the wheel row ----
       Its own line because it is not about the wheel: it changes what the GAME does, and it is
       the first thing that has to be true before any force on this page means what it says. */
    ffb_ingame=MkBtn(h,"",COL_LABEL,y,330,26,FF_ID_INGAME);
    MkText(h,"the game's own handling and force feedback, as this mod was tuned",
           COL_LABEL+340,y+5,W_FULL-340,18,0);
    y+=32;

    FfbDevEnum();
    /* 88, not 64: at 64 the section face drew "WHEE". Measured off a photograph, which is the only
       instrument that finds a clipped string - no test can see it. */
    MkTextF(h,"WHEEL",COL_LABEL,y+6,88,20,FF_ID_SECT0+4,g_fSection,0);
    ffb_devDrop=MkBtn(h,"",COL_LABEL+96,y,420,30,FF_ID_DEVDROP);
    /* Refresh sits BETWEEN the list and Test, on his instruction of 2026-08-12 - and the order is
       the order you use them in: this is the list, this is how you rebuild the list, this is how
       you test what is in it.
       Bounds are written down rather than eyeballed, because an overlapping static painting paper
       over a button's edge has cost this page four times:
         drop 96..516   refresh 526..676   test 686..886   held 896..1268 (W_FULL is 1268). */
    ffb_devRefresh=MkBtn(h,"Refresh list",COL_LABEL+526,y,150,30,FF_ID_DEVREFRESH);
    ffb_devTest=MkBtn(h,"Test - push the wheel",COL_LABEL+686,y,200,30,FF_ID_DEVTEST);
    /* What the MOD says it took, from mafia_ffb_status.ini - a different question from what is
       chosen here, and the point of the row is the case where the two differ. On the same line,
       because the line is 1268 px wide and this is the half of it nothing else needs. */
    /* Clear of the Test button, which ends at COL_LABEL+726. At 714 this static's own rectangle
       overlapped it by 12 px and painted paper over the button's right edge - visible on a
       photograph as a doubled border, and the fourth time this project has paid for a control
       whose bounds nobody checked against its neighbour. */
    ffb_devHeld=MkTextF(h,"",COL_LABEL+896,y+7,W_FULL-896,32,FF_ID_DEVHELD,g_fSmall,0);
    y+=36;
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    COL_LABEL,y,W_FULL,2,h,NULL,NULL,NULL);
    y+=12;

    /* ================= ABOVE BOTH COLUMNS: the range =================
     * Decided 2026-08-01: the angle choice needed to go at the top, above everything, and only then
     * the two different columns. It is the first thing on the page because every other number here is a function
     * of it - the kick floor, the wallow gain and every recommended value are chosen per range -
     * so a page that opens with sliders invites tuning strength against a range nobody declared.
     * At full width the eight detents go back to ONE LINE, which is the layout the design pass of
     * 2026-07-27 settled on; the 2x4 grid was only ever the two-column compromise. */
    /* The header and its sentence share a line. On the page growing back: it still
       needed squeezing, or squeezing continued and then stretched it right back out again. A section title with a
       full-width line of prose under it costs 46 px to say what fits on one. */
    MkTextF(h,"WHEEL ROTATION RANGE",COL_LABEL,y+3,270,20,FF_ID_SECT0,g_fSection,0);
    MkText(h,"what your wheel's own driver is set to - it changes nothing on the wheel, it tells "
             "the mod what to serve",COL_LABEL+280,y+5,W_FULL-280,18,0);
    y+=26;
    {   /* 600 keeps a wider face and the larger figure: it is the range every constant in this
           mod was chosen on, and bold-with-no-colour is this skin's word for "recommended". */
        int gap=10,x=COL_LABEL;
        for(int i=0;i<NDOR;i++){
            int wB=(DORDEG[i]==DOR_DEFAULT)?170:140;
            char t[16]; wsprintfA(t,"%d",DORDEG[i]);
            ffb_dorBtn[i]=MkBtn(h,t,x,y,wB,30,FF_ID_DOR0+i);
            ffb_dorCx[i]=x+wB/2;
            x+=wB+gap;
        }
    }
    y+=34;
    /* Parked under the button it describes - ffb_RefreshDor moves the BOX as the selection
       changes, and SS_CENTER centres the TEXT inside it. Both halves are needed and only the
       first one existed: the box was centred on the button while the words sat at its left
       edge, so a 260 px box holding a 150 px caption read as ~55 px off to the left. Reported
       2026-08-05: the text was not centered and needed to be centered under the button. */
    ffb_dorText=MkTextF(h,"everything was tuned here",COL_LABEL,y,DORTXT_W,18,FF_ID_DORTXT,
                        g_fField,SS_CENTER);
    y+=22;
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    COL_LABEL,y,W_FULL,2,h,NULL,NULL,NULL);
    y+=12;

    ry=y;                      /* the right column starts level with the left */
    colTop=y;                  /* and the divider between them starts there too, not at the page top */

    /* ================= LEFT: the sliders you drag while judging a drive =================
     * The two column headings are CENTRED over their own columns. Decided 2026-08-03: each needs to be
     * centred on its own category, so it is immediately clear what starts where and where the category
     * ends - left alignment reads as wrong. Left-aligned, a heading sits above
     * the first control of its column and says nothing about where the column ends - which on a
     * two-column page is the one thing a heading is for. */
    MkTextF(h,"FORCE FEEDBACK STRENGTH",COL_LABEL,y,W_LEFT,22,FF_ID_SECT0+1,g_fSection,SS_CENTER);
    y+=24;
    MkText(h,"100% is the shipped feel, unchanged.",COL_LABEL,y,W_LEFT,18,0); y+=22;
    for(int i=0;i<S_SPRING;i++){
        ffb_MkRow(h,i,y,COL_LABEL,150,COL_SLIDER,W_SLIDER,COL_BOX,COL_HINT,W_HINT);
        y+=27;
    }
    MkTextF(h,"The tall mark on each slider is what we recommend - 100 everywhere except gunfire, which is 0. Gunfire shakes the wheel for EVERY shot fired from your car, including your allies' and enemies', not just yours.",
            COL_LABEL,y+4,W_LEFT,16,FF_ID_NOTE,g_fSmall,0);
    y+=24;

    /* ================= RIGHT: what you set once =================
     * CARS AND TRUCKS ARE STACKED, not side by side. Decided 2026-08-03: a visual divider is needed
     * between Cars and Trucks, since it is otherwise unclear where the percentages start and end -
     * Cars and Trucks could be fitted into one column by height, making the whole table narrower. Right now
     * it is too wide.
     *
     * He is right about what the wide version cost. Side by side, one line carried slider, box, `%`,
     * slider, box, `%` - six controls and two identical `100 %` pairs with nothing between them, so
     * which percent belonged to which vehicle was a matter of counting. Stacked, there is one column
     * of numbers under one heading, a rule, then another. The table is ~350 px instead of ~550, and
     * the height is free: the left column is the taller of the two either way.
     *
     * What the old layout HAD that this gives up: a truck value physically beside the car value it
     * multiplies. The Trucks heading carries that instead: multiplies the value above. */
    MkTextF(h,"WHEEL WEIGHT",COL_MID,ry,W_RIGHT,22,FF_ID_SECT0+2,g_fSection,SS_CENTER); ry+=26;

    MkTextF(h,"Cars",COL_MID,ry,WW_LBL_W+WW_SL_W+80,18,0,g_fField,0); ry+=20;
    for(int i=S_SPRING;i<=S_DAMPMOVE;i++){
        ffb_MkRow(h,i,ry,COL_MID,WW_LBL_W,WW_CAR_SL,WW_SL_W,WW_CAR_BX,0,0);
        ry+=27;
    }
    /* The divider he asked for, and only as wide as the table it divides - run to the column's full
       width it would read as the end of the section rather than a seam inside it. */
    ry+=4;
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    COL_MID,ry,WW_TABLE_W,2,h,NULL,NULL,NULL);
    ry+=10;

    MkTextF(h,"Trucks - multiplies the value above",COL_MID,ry,WW_TABLE_W,18,0,g_fField,0); ry+=20;
    for(int i=S_SPRING;i<=S_DAMPMOVE;i++){
        int t=-1;
        for(int k=S_TRKSTAT;k<NSLIDER;k++) if(TruckPartner(k)==i) t=k;
        /* The spring has no truck row on purpose - a truck's centering is the car's - and the row
           SAYS so, because a gap in a table reads as a missing control. */
        if(t>=0){
            /* lblW 0, and the name drawn here: SNAME is empty for these rows so the log can name
               them apart from the car rows they multiply. */
            MkText(h,WWTRK[t-S_TRKSTAT],COL_MID,ry+4,WW_LBL_W,18,0);
            ffb_MkRow(h,t,ry,COL_MID,0,WW_CAR_SL,WW_SL_W,WW_CAR_BX,0,0);
        } else {
            MkText(h,SNAME[i],COL_MID,ry+5,WW_LBL_W,20,0);
            MkTextF(h,"same as cars - by design",WW_CAR_SL,ry+6,WW_SL_W+80,16,FF_ID_NOTE,
                    g_fSmall,0);
        }
        ry+=27;
    }
    MkTextF(h,"The parking damper saturates near half a turn, so turning it DOWN is the half you "
              "feel. Truck values are arithmetic - nobody has driven them.",
            COL_MID,ry+2,W_RIGHT,32,FF_ID_NOTE,g_fSmall,0);
    ry+=38;


    /* a hairline between the columns, so the page reads as two panels rather than as one that
       lost its alignment. It starts where the COLUMNS start, not at the top of the page: the
       range above them spans both, and a divider run up through it would cut that row in half. */
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDVERT,
                    COL_MID-14,colTop,2,(y>ry?y:ry)-colTop,h,NULL,NULL,NULL);

    /* ================= UNDER BOTH COLUMNS: the presets =================
     * Moved out of the right column, per instruction: the preset choice needed to be shared across both
     * columns, so there was no feeling that presets belong only to the right column and not the left. That
     * is describing what the layout actually said - a preset carries every value on the page, and
     * sitting inside one column claimed it carried half of them.
     *
     * Below the divider rather than above it, so the divider is exactly as tall as the two things
     * it separates and the preset row belongs to neither. */
    y=(y>ry?y:ry)+10;

    /* ---- BACK TO DEFAULT SETTINGS - full width, centred, UNDER both columns and ABOVE the
     * presets. Two of his instructions, a few minutes apart on 2026-08-05, and the second
     * refines the first:
     *   The recommendation was on the left, but it also affects the sliders on the right, which was not
     *   obvious - so it cannot live inside the left column: it sets EVERY slider on the
     *    page back to 100%, the wheel-weight table included.
     *   The instruction was to put it not at the top but below, under Force Feedback and Wheel Weight, above the presets.
     * Under both columns is where the reader arrives having seen everything it resets, and above
     * the presets because it acts on the sliders, not on which preset is selected. */
    { int bw=220, bx=(WINW-bw)/2;
      MkBtn(h,"Back to default settings",bx,y,bw,28,FF_ID_RECOMM);
      MkTextF(h,"every slider on this page back to 100%",COL_LABEL,y+32,W_FULL,16,
              FF_ID_NOTE,g_fSmall,SS_CENTER);
      y+=56; }

    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    COL_LABEL,y,W_FULL,2,h,NULL,NULL,NULL);
    y+=12;
    /* Shortened to fit the 708 px left of the buttons - the longer sentence drew "goes into it as"
       and stopped. A line that ends mid-clause is worse than the shorter line that says it. */
    MkTextF(h,"PRESETS - the one lit green is being edited, and every value on this page goes "
              "into it",COL_LABEL,y+6,W_FULL-560,20,FF_ID_SECT0+3,g_fSub,0);
    for(int i=0;i<3;i++){
        char t[16]; wsprintfA(t,"%d",i+1);
        ffb_slot[i]=MkBtn(h,t,COL_LABEL+W_FULL-540+i*54,y,48,28,FF_ID_SLOT0+i);
    }
    MkBtn(h,"Import preset...",COL_LABEL+W_FULL-350,y,168,28,FF_ID_LOADF);
    MkBtn(h,"Export preset...",COL_LABEL+W_FULL-172,y,172,28,FF_ID_SAVEF);
    y+=34;

    y=PageSaveRow(h,LTAB_FFB,y,FfbSaveIni,FfbLoadIni);
    ffb_pageH=y+8;

    /* PAINT THE FACES NOW THAT THE CONTROLS EXIST. FfbLoadIni runs earlier in this function and
       ends with ffb_RefreshDev(), but at that moment ffb_devDrop is still NULL, so it returns
       having painted nothing - and the wheel row sat BLANK until something else happened to call
       it. Reported 2026-08-12: the wheel was switched on, the list still looked empty, and pressing
       Refresh list "fixed" it. Nothing was wrong with the enumeration; the face had simply never
       been drawn. */
    ffb_RefreshDev();
    ffb_RefreshIngame();
}

static int PageFfbDraw(DRAWITEMSTRUCT *d,int id){
    char txt[320]; GetWindowTextA(d->hwndItem,txt,sizeof(txt));
    RECT r=d->rcItem;
    int pressed=(d->itemState&ODS_SELECTED)!=0;
    int off=!IsWindowEnabled(d->hwndItem);
    SetBkMode(d->hDC,TRANSPARENT);

    if(id>=FF_ID_DOR0&&id<FF_ID_DOR0+NDOR){
        /* The range selector: one button per legal value, ALL ON ONE LINE, 600 wider and set in
           a larger face because it is the range every constant in this mod was chosen on.
           GREEN = in force now, BOLD = the recommended one, and never red. */
        int deg=DORDEG[id-FF_ID_DOR0];
        int chosen=(deg==ffb_range), isRef=(deg==DOR_DEFAULT);
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,isRef?g_fRange:(chosen?g_fAction:g_fField));
        DrawPressable(d->hDC,&r,txt,pressed,off,isRef,chosen&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    if(id>=FF_ID_RESET0&&id<FF_ID_RESET0+NSLIDER){
        /* A real loop - most of a circle, coming back on itself, with the head at the opening.
           The first version was a plain back-arrow, which reads as "previous", not as "undo". */
        PaperBlit(d->hDC,d->hwndItem,&r);
        int cx=(r.left+r.right)/2, cy=(r.top+r.bottom)/2, rad=7;
        COLORREF c = off ? INK_OFF : pressed ? INK : INK_SOFT;
        HPEN pen=CreatePen(PS_SOLID,2,c);
        HGDIOBJ op=SelectObject(d->hDC,pen), ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
        Arc(d->hDC,cx-rad,cy-rad,cx+rad,cy+rad, cx,cy-rad, cx+(rad*7)/10,cy-(rad*7)/10);
        SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(pen);
        HBRUSH b=CreateSolidBrush(c); HGDIOBJ ob2=SelectObject(d->hDC,b);
        HPEN p2=CreatePen(PS_SOLID,1,c); HGDIOBJ op2=SelectObject(d->hDC,p2);
        POINT ah[3]={{cx+1,cy-rad-4},{cx+1,cy-rad+4},{cx-5,cy-rad}};
        Polygon(d->hDC,ah,3);
        SelectObject(d->hDC,op2); DeleteObject(p2); SelectObject(d->hDC,ob2); DeleteObject(b);
        return 1;
    }
    if(id==FF_ID_LAMP){
        /* a state, never clicked: green means the mod told us it put a force on the wheel, amber
           that the game is not running, red that it is and the wheel is not being driven - which
           is the one place red belongs, because it IS a fault */
        PaperBlit(d->hDC,d->hwndItem,&r);
        COLORREF c = ffb_lampState==1?MAFIA_GREEN :
                     (ffb_lampState==2||ffb_lampState==3)?MAFIA_AMBER : MAFIA_RED;
        HBRUSH b=CreateSolidBrush(c); HGDIOBJ ob=SelectObject(d->hDC,b);
        HPEN pen=CreatePen(PS_SOLID,1,INK); HGDIOBJ op=SelectObject(d->hDC,pen);
        Ellipse(d->hDC,r.left+2,(r.top+r.bottom)/2-7,r.left+18,(r.top+r.bottom)/2+9);
        SelectObject(d->hDC,op); DeleteObject(pen); SelectObject(d->hDC,ob); DeleteObject(b);
        return 1;
    }
    if(id>=FF_ID_SLOT0&&id<FF_ID_SLOT0+3){
        /* the profile slots are a SELECTOR, like the range: the live one is green */
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,((id-FF_ID_SLOT0)==ffb_slotSel)&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    if(id==FF_ID_DEVDROP){
        /* GREEN when a real device is chosen and attached - the palette's one meaning, "in force
           now". Plain when it is "first one offered", because that is a choice about a device
           nobody has named; and plain, with the words saying so, when the choice is not here. */
        int chosen=FfbDevChosen(ffb_device);
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,(chosen>=0)&&!off);
        SelectObject(d->hDC,save);
        /* the little arrow, so the line reads as something that opens */
        if(!off){
            int cx=r.right-16, cy=(r.top+r.bottom)/2;
            POINT tri[3]={{cx-5,cy-2},{cx+5,cy-2},{cx,cy+4}};
            HBRUSH b=CreateSolidBrush(INK_SOFT); HGDIOBJ ob=SelectObject(d->hDC,b);
            HPEN pen=CreatePen(PS_SOLID,1,INK_SOFT); HGDIOBJ op=SelectObject(d->hDC,pen);
            Polygon(d->hDC,tri,3);
            SelectObject(d->hDC,op); DeleteObject(pen);
            SelectObject(d->hDC,ob); DeleteObject(b);
        }
        return 1;
    }
    if(id==FF_ID_DEVTEST||id==FF_ID_DEVREFRESH){
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,0,0);
        SelectObject(d->hDC,save);
        return 1;
    }
    /* drawn CHOSEN when the settings are applied - the same green the range detents use, because
       it is the same kind of fact: a state that is true right now, not an action offered */
    if(id==FF_ID_INGAME){
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        /* the LAST argument is the green one - `primary` is only an emphasised face. Passing the
           state as primary drew an ordinary button whose text claimed to be ON, which is exactly
           the kind of control this page is not allowed to have. Same shape as the range detents:
           chosen && !off, so a greyed page greys it too. */
        DrawPressable(d->hDC,&r,txt,pressed,off,0,ffb_SavApplied()&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    if(id==FF_ID_RECOMM||id==FF_ID_SAVESLOT||id==FF_ID_LOADF||id==FF_ID_SAVEF){
        int primary=(id==FF_ID_RECOMM);
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,primary?g_fAction:g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,primary,0);
        SelectObject(d->hDC,save);
        return 1;
    }
    return 0;
}

static int PageFfbCommand(int id,int code,HWND ctl){
    if(id==FF_ID_INGAME){
        ffb_SavToggle(ffb_SavApplied()?0:1);
        ffb_RefreshIngame();
        return 1;
    }
    if(id==FF_ID_DEVDROP){ ffb_DevDropDown(); return 1; }
    /* Ask DirectInput again. The list is built once when the page is created, and a wheel switched
       on after that never appeared until the window was closed and reopened - which was first
       hit on 2026-08-12 and what this button removes.
       It reports the COUNT out loud in both directions. "0 -> 0" is the answer to "I pressed it and
       nothing happened", and without it a refresh that found nothing is indistinguishable from a
       button that does nothing - the same silence this project keeps paying for. */
    if(id==FF_ID_DEVREFRESH){
        char m[200];
        int before=ffbdev_n,i;
        FfbDevEnum();
        wsprintfA(m,"refresh: %d force-feedback device(s) before, %d now",before,ffbdev_n);
        LogLine(m);
        for(i=0;i<ffbdev_n;i++){ wsprintfA(m,"  [%d] %s",i,ffbdev_name[i]); LogLine(m); }
        if(!ffbdev_n)
            LogLine("  none. Switch the wheel on, wait for Windows to finish with it, press it again");
        /* The face reads from the list, so it has to be told now. The "what the mod took" line
           beside it is rebuilt by the page's own timer, which is already running. */
        ffb_RefreshDev();
        return 1;
    }
    if(id==FF_ID_DEVTEST){
        char why[200],m[400];
        int idx=FfbDevChosen(ffb_device);
        /* WITH NOTHING CHOSEN, test the one the mod would take - the first offered. Testing
           nothing at all would be the one case where the button is useless, and it is also the
           case where a user most needs to know what "the first one" actually is. Same for a chosen
           device that is not attached, and there the log says WHICH question is being answered. */
        if(idx==FFBDEV_GONE)
            LogLine("the wheel you chose is not attached - testing the device the mod would take");
        if(idx<0) idx=(ffbdev_n>0)?0:-1;
        if(idx<0){ LogLine("no force-feedback device is attached - nothing to test"); return 1; }
        /* The mod holds the wheel EXCLUSIVE while the game runs. Two exclusive owners is a fight,
           and its loser is whoever asked second - so refuse and say why, rather than produce a
           silence indistinguishable from broken hardware. */
        if(GameIsRunning()){
            LogLine("Mafia is running and the mod is holding the wheel - close the game to test");
            return 1;
        }
        wsprintfA(m,"testing %s - left, right, centre",ffbdev_name[idx]); LogLine(m);
        if(FfbTestForce(idx,g_frame,why,sizeof why)) LogLine("  the force was sent");
        else { wsprintfA(m,"  no force: %s",why); LogLine(m); }
        return 1;
    }
    if(id>=FF_ID_DOR0&&id<FF_ID_DOR0+NDOR){ ffb_SetRange(id-FF_ID_DOR0); return 1; }
    if(id>=FF_ID_RESET0&&id<FF_ID_RESET0+NSLIDER){ ffb_SetRow(id-FF_ID_RESET0,100); return 1; }
    if(id>=FF_ID_SLOT0&&id<FF_ID_SLOT0+3){ ffb_SelectSlot(id-FF_ID_SLOT0); return 1; }
    if(id==FF_ID_RECOMM){ ffb_Recommended(); return 1; }
    if(id==FF_ID_LOADF){ ffb_FileDialog(0); return 1; }
    if(id==FF_ID_SAVEF){ ffb_FileDialog(1); return 1; }
    if(id>=FF_ID_BOX0&&id<FF_ID_BOX0+NSLIDER&&code==EN_CHANGE&&!ffb_quiet){
        char t[32]; int v;
        GetWindowTextA(ctl,t,sizeof(t));
        /* an empty box is somebody mid-edit, not a zero */
        if(t[0]&&ParseInt(t,&v)){
            int row=id-FF_ID_BOX0;
            int nv=Clamp(v,0,SBOXMAX[row]);
            if(ffb_val[row]!=nv){
                ffb_val[row]=nv;
                PageDirty(LTAB_FFB);
                /* NOT ffb_RefreshRow: it rewrites the box, which would fight the typing. Only
                   the things that render the value elsewhere. */
                if(ffb_hint[row]){ SetWindowTextA(ffb_hint[row],ffb_HintFor(row,nv));
                                   InvalidateRect(ffb_hint[row],NULL,TRUE); }
                if(ffb_slider[row]) InvalidateRect(ffb_slider[row],NULL,TRUE);
                if(ffb_reset[row])  ShowWindow(ffb_reset[row],nv==100?SW_HIDE:SW_SHOW);
            }
        }
        return 1;
    }
    return 0;
}

/* Read the settings this install already has, and start the status poll. */
static void PageFfbStart(void){
    char p[MAX_PATH];
    for(int i=0;i<NSLIDER;i++) ffb_val[i]=100;
    ffb_IniPath(p);
    ffb_LoadFrom(p);
    for(int i=0;i<NSLIDER;i++){ ffb_RefreshRow(i); ffb_logWas[i]=ffb_val[i]; ffb_logPend[i]=0; }
    ffb_RefreshDor();
    ffb_RefreshSlots();
    /* What is on screen came straight out of the file, so the page is NOT dirty. Without this
       the tab opened saying "not saved yet" about settings nobody had touched - which is the
       same false alarm the red banner exists to avoid. */
    PageSaved(LTAB_FFB);
    PageFfbPoll();
}

#endif /* ALXG_PAGE_FFB_H */
