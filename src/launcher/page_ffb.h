/* page_ffb.h - the Force Feedback tab: the tuner, ported out of src/spike/ffb_gui.c.
 *
 * The sliders and their live hints, the eight-stop rotation range selector, the profile slots and
 * the status lamp came over from mafia-ffb-setup.exe. Dropped: its WinMain, its frame, its copy of
 * the skin, and three things the launcher now owns -
 *
 *   - Apply / Apply & Launch. Saving IS applying (the mod re-reads this file about once a
 *     second), so the shared save row does it, and the shared banner says it is live.
 *   - its own banner. One save model on every tab - see mod_state.h.
 *   - ModIsOn / ToggleMod, which renamed mafia_ffb.asi to .asi.off. The toggle at the top of the
 *     page installs and uninstalls through the journal, and two mechanisms for "is this mod on"
 *     is exactly the drift the journal exists to end. A self-test asserts no .asi.off is created.
 *
 * 2026-09-30, THE UTILITY OF THE 2026 ROAD MODEL - the owner's decisions on
 * docs\UTILITY-CHANGES-v821.md (B1-B5), and his corrections the same evening:
 *   - ONE FORCE FEEDBACK: the road model, labelled as the new 2026 model with a NEW plate. A
 *     selector of three was built and he took it back to one, in two steps. Legacy (the 1.4.3
 *     feel) went first, translated: "the old feedback should not be offered - I don't want to
 *     complicate my utility with old profiles; whoever needs it will install the old 1.4.3 by
 *     hand". The game developers' own feedback went next. So the page writes neither `ffb_mode`
 *     nor `ground` - the module's own handling of those keys is left alone - and a file that still
 *     switches the road model off is SAID, in red, because the sliders would then move nothing.
 *   - The rows that belonged to the old feedback are gone (spring, slide feel, the old damper pair,
 *     the truck pair). Their keys are carried through a save untouched; a value in them that the
 *     road module still applies is named on the page. A folder that still holds the 1.4.3 module
 *     (or any earlier build of ours) is detected and said - an upgrade, not a model.
 *   - The road model's own sliders, grouped by channel - steering, damper, road, hits, overall.
 *     PROPORTIONAL, never a ceiling cut: a slider multiplies its channel's whole curve including
 *     that channel's ceiling, and 100% (the tall mark) is his reference.
 *   - HEADROOM TO 400% on every force slider, his words: people on weaker bases must be able to
 *     crank the force 3-4x; he will not tune for weak hardware.
 *   - The file this page writes switches the developers' in-game hotkeys OFF, unless it says
 *     dev_keys = 1 (the bench).
 *
 * 2026-10-01, HIS FIRST LOOK AT THE 2.0 PAGE: THE 1.4.3 PAGE COMES BACK, plus the new rows.
 *   "I don't understand why you redesigned the application so much compared to 1.4. With all my
 *   new corrections the difference should be almost none - a few sliders added, and that's it",
 *   and "the fonts differ, and the colour, and the layout of the page itself" (translated). So the
 *   layout is 1.4.3's - see THE 1.4.3 PAGE, AGAIN below - and his corrections of the same look:
 *   - THE MODEL BLOCK IS GONE - name, NEW plate, the line under them, and the heading that named
 *     the model over the sliders: "there are no other options, and it only makes the utility
 *     taller. We worked hard to make it smaller, and now it has stretched vertically again." What
 *     the block's line also did - say in red why the road model is NOT driving the wheel - stays,
 *     as a line above the columns that takes room only while it is true.
 *   - THE RANGE BUTTONS as in 1.4.3, one line each, and no MEASURED or BY THE LAW: "by the law
 *     looks as silly as it gets... remove measured and by the law; label 600 as recommended and
 *     leave the others as they are."
 *   - OVERALL STRENGTH on a row of its own above both columns, like the range: "why is it at the
 *     bottom - should it not be above everything? And which values does it regulate?", then "the
 *     steering has to sit under it - now it looks as if it only acts on the left column". Renamed
 *     by him to TOTAL EFFECTS (WITHOUT DAMPERS) and centred, "so it is clear it belongs to both
 *     columns". The Damper half says in its heading that Total effects does not scale it.
 *   - SHORTER: "too tall - why so much empty space under the presets?" The window is as tall as
 *     its tallest tab, so this page and First person both lost height; see FFB_PAGE_MAX.
 *   - THE GUNFIRE NOTE under the gunfire row: "it does not read as anything to do with gunfire".
 *
 * Everything is prefixed ffb_.
 *
 * THE PATH TRAP, same as the shifter's: mafia-ffb-setup.exe lived INSIDE "mafia ffb setup\" and
 * built its paths from the folder its exe sat in. This program sits in the GAME ROOT.
 *
 * The INI contract is src/ffb/ffb_settings.c and GroundReadIni in src/ffb/mafia_ffb_v6.c: section
 * [ffb]. These key names ARE the contract - changing one silently disconnects a slider.
 */
#ifndef ALXG_PAGE_FFB_H
#define ALXG_PAGE_FFB_H

#include "ffb_devices.h"        /* which wheel, and a force you can feel - needs page_shifter.h's
                                   DirectInput declarations, which launcher.c includes first */
#include "ini_carry.h"          /* keep every key of [ffb] this page does not own */

/* Under "ALXG mods\" since 2026-08-07 - see install_core.h's ALXG_DIR for why. Spelled here
   as a literal rather than built from ALXG_DIR so the two-line grep for "mafia ffb setup"
   finds every place it is written; ffb_settings.c inside the mod is the third. */
#define FFBDIR "ALXG mods\\mafia ffb setup"

/* ---- THE ROWS ------------------------------------------------------------------------------
 * The first NPLAIN are one ini key each, a percent of the reference build - the keys the tuner has
 * always written. From S_GWEIGHT on they are the road model's own, and each writes the module's
 * numbers as a PROPORTION of his reference (GREF, further down). */
enum { S_MASTER,S_CRASH,S_OBJ,S_PED,S_GUN,S_ROAD,
       S_GWEIGHT,S_GBREAK,S_GPARK,S_GDRIVE,S_GSLIP,S_GTEXT,NSLIDER };
#define NPLAIN S_GWEIGHT

static const char *SKEY[NSLIDER] = {
    "master","crash","objects","ped","gun","road",
    /* a road-model row writes more than one key; this is the one that names it */
    "ground_sat_k","ground_opt_deg","ground_damp_stand","ground_damp_move_pct",
    "ground_damp_slip","ground_detail_k" };
/* What a row is called, on screen and in the change log. The road row is one key, `road`, and in
   the road model it scales roll and pitch only - the curbs - while the fine texture has a row of
   its own. */
static const char *SNAME[NSLIDER] = {
    /* "Overall strength" until 2026-10-01 - his rename, so the name says the dampers are not in it;
       "without dampers" the same evening, then "not affecting dampers", his wording for release */
    "Total effects (not affecting dampers)","Crashes and rams","Hitting objects","Pedestrians",
    "Gunfire",
    "Roll and curbs",
    "Steering weight","Breakaway","Parking damper","Driving damper","Lightness in a slide",
    "Road texture" };
/* Slider travel, in the row's own unit. HEADROOM TO 400% on every force row - the owner's decision
   of 2026-09-30: people on weaker wheelbases must be able to crank the force three or four times;
   he will not tune for weak hardware. 100% stays his reference and the tall mark. Gunfire keeps the
   600 it already had. Two rows are not forces and keep their own range: the breakaway is an angle,
   5..15 degrees, and the lightness in a slide is a share of the damper, 0..100.
   Above 100 some channels meet a ceiling INSIDE the module (the crash cap, the 25000 sum clamp, the
   texture and parking-damper clamps) and the device's own limit after that; the module changes that
   make 400% the approved shape x4 rather than a flat wall are listed in
   docs\UTILITY-MODULE-NEEDS.md, deliberately not faked here. */
static const int SMIN[NSLIDER]    = { 0,0,0,0,0,0,       0,5,0,0,0,0 };
static const int SMAX[NSLIDER]    = { 400,400,400,400,600,400, 400,15,400,400,100,400 };
static const int SBOXMAX[NSLIDER] = { 400,400,400,400,600,400, 400,15,400,400,100,400 };
/* Drag and arrow-key step. The road model's driving damper steps 2, because its key is a whole
   percent of HALF the profile's damper: an odd value on the slider has no key that writes it, and
   would come back one higher on reload. */
static const int SSTEP[NSLIDER]   = { 1,1,1,1,1,1, 1,1,1,2,1,1 };
/* THE REFERENCE PER ROW - the tall mark. 100 on every percent row except gunfire; on the two rows
   whose unit is not a percent of the reference it is the reference itself: 9 degrees of breakaway,
   78% of the damper gone at a full slide.
   Gunfire, Alex 2026-08-12: force feedback fires when ANYBODY shoots from his car, and a jolt he
   did not cause reads as the wheel going wrong rather than as a gunshot. Until the mod can tell
   whose shot it is, the recommended value for that row is ZERO. 100 keeps a mark of its own on
   that slider (SALT) so the old behaviour is visibly still there, one step away. */
static const int SREF[NSLIDER]    = { 100,100,100,100,0,100, 100,9,100,100,78,100 };
static const int SALT[NSLIDER]    = { -1,-1,-1,-1,100,-1, -1,-1,-1,-1,-1,-1 };
static const char *SUNIT[NSLIDER] = { "%","%","%","%","%","%", "%","deg","%","%","%","%" };
/* Which marks a row draws and snaps to - ui_slider.h's `marks`. The breakaway travels ten degrees
   and snaps to nothing (0); every other row has its reference and its round numbers (2). */
static const int SMARKS[NSLIDER]  = { 2,2,2,2,2,2, 2,0,2,2,2,2 };
/* Kept short on purpose: the left column's hints have 200 px and the right column's 134 (GEO_L,
   GEO_R) - only the full-width Overall row has more (GEO_TOP) - and the first set of these ran off
   the edge on screen while every self-test passed. A hint nobody can read is worse than no hint -
   and the self-test MEASURES every one of them in the font it is drawn in.
   Total effects answers his question of 2026-10-01, "which values does it regulate?", in its own
   name - "(not affecting dampers)" - and its hint lists the rest: it is the module's SetMag choke, every
   constant force on the page, while the dampers are a separate DirectInput effect it never touches
   (mafia_ffb_v6.c, SetMag). */
static const char *SHINT[NSLIDER] = {
    "steering, road and hits",
    "",
    "crates, bins, hydrants",
    "",
    "",                                   /* gunfire speaks through ffb_GunWord */
    "body rock over curbs",
    "how heavy overall",
    "where the grip goes",
    "at a standstill",
    "while driving",
    "gone in a full slide",
    "cobbles and fine grain" };

static int ffb_val[NSLIDER];

/* ---- THE ROAD MODEL'S OWN KEYS --------------------------------------------------------------
 * What the module reads (GroundReadIni), at HIS REFERENCE - memory\the-approved-driving-feel.md,
 * the table he confirmed on 2026-09-30, and the module's own defaults. These two must agree: a
 * silent ini and a file written at 100% have to be the same wheel.
 *
 * The page keeps the RAW values it read and writes them back untouched until a row is moved. Two
 * rows drive two keys each (weight = SAT + caster, texture = gain + ceiling), and a file whose
 * pair is not in the reference proportion - a bench experiment - must not have its second key
 * rewritten by a save that never touched the row: a page may write only what it read. */
enum { GK_SATK, GK_CASTER, GK_OPT, GK_STAND, GK_MOVE, GK_SLIP, GK_DETK, GK_DETLIM, NGK };
static const char *GKEY[NGK] = {
    "ground_sat_k","ground_caster_k","ground_opt_deg","ground_damp_stand",
    "ground_damp_move_pct","ground_damp_slip","ground_detail_k","ground_detail_lim" };
static const int GREF[NGK] = { 7875, 2400, 9, 6000, 50, 78, 900, 5000 };
/* Read-back sanity only. High enough for 400% of every reference, so the page reads its own 400%
   back as 400% - the MODULE clamps some of these lower today (ground_damp_stand at 20000,
   ground_detail_lim at 10000), which is one of the module needs, not a reason for the page to
   show a different number from the one it wrote. */
static const int GCAP[NGK] = { 40000, 10000, 45, 24000, 200, 100, 20000, 20000 };
static int ffb_gk[NGK];

static int ffb_Pct(int raw,int ref){ return raw<=0 ? 0 : (raw*100+ref/2)/ref; }
static int ffb_OfPct(int ref,int pct){ return (ref*pct+50)/100; }

/* The value a road-model row shows, from the keys it drives. */
static int ffb_RowFromRaw(int row){
    switch(row){
    case S_GWEIGHT: return ffb_Pct(ffb_gk[GK_SATK],GREF[GK_SATK]);
    case S_GBREAK:  return ffb_gk[GK_OPT];
    case S_GPARK:   return ffb_Pct(ffb_gk[GK_STAND],GREF[GK_STAND]);
    case S_GDRIVE:  return ffb_Pct(ffb_gk[GK_MOVE],GREF[GK_MOVE]);
    case S_GSLIP:   return ffb_gk[GK_SLIP];
    case S_GTEXT:   return ffb_Pct(ffb_gk[GK_DETK],GREF[GK_DETK]);
    }
    return ffb_val[row];
}
/* ...and the keys a moved row writes. PROPORTIONAL: both keys of a pair scale together from his
   reference, so the weight's SAT and caster keep his ratio and the texture's gain keeps its
   ceiling in step. */
static void ffb_RawFromRow(int row,int v){
    switch(row){
    case S_GWEIGHT:
        ffb_gk[GK_SATK]  =ffb_OfPct(GREF[GK_SATK],v);
        ffb_gk[GK_CASTER]=ffb_OfPct(GREF[GK_CASTER],v);
        break;
    case S_GBREAK: ffb_gk[GK_OPT]=v; break;
    /* 0% IS WRITTEN AS 1, and it is not a rounding. The module reads ground_damp_stand = 0 as
       "do not intervene", which puts back the OLD standstill damper - 20000, the heaviest parked
       wheel this mod has ever had - so a slider pulled to zero would do the exact opposite of
       what it shows. 1 is no damper worth feeling. Listed as a module need. */
    case S_GPARK:  ffb_gk[GK_STAND]= v>0 ? ffb_OfPct(GREF[GK_STAND],v) : 1; break;
    case S_GDRIVE: ffb_gk[GK_MOVE]=ffb_OfPct(GREF[GK_MOVE],v); break;
    case S_GSLIP:  ffb_gk[GK_SLIP]=v; break;
    case S_GTEXT:
        ffb_gk[GK_DETK]  =ffb_OfPct(GREF[GK_DETK],v);
        ffb_gk[GK_DETLIM]=ffb_OfPct(GREF[GK_DETLIM],v);
        break;
    }
}

/* ---- WEIGHT BUILD-UP WITH SPEED: three curves he DROVE, not a free slider -------------------
 * The shape is his, specified band by band and then judged: O ("the half") is the reference
 * since ours-037, I is the lighter curve that was the base from v810 to v816, U the heavier v803
 * one he drove on ours-032. The numbers are tools\ground-keys.ps1's preset8 / preset7 / preset6
 * rows, which run_all 3g already holds equal to the module's own tables. A free slider here would
 * let anybody draw a curve nobody has driven; three buttons cannot. */
#define NSPD 8
static const int SPD_KMH[NSPD] = { 10, 20, 30, 50, 60, 70, 80, 110 };
enum { CRV_LIGHT, CRV_REF, CRV_HEAVY, NCRV };
static const int CRV_PCT[NCRV][NSPD] = {
    { 22, 34, 39, 49, 65,  84, 105, 173 },     /* I - lighter  */
    { 22, 34, 42, 60, 80, 100, 122, 190 },     /* O - the half, his reference */
    { 22, 34, 46, 70, 95, 115, 138, 208 } };   /* U - heavier  */
static const char *CRV_NAME[NCRV] = { "Light", "Reference", "Heavy" };
static int ffb_spdKmh[NSPD], ffb_spdPct[NSPD];

/* Which of the three the file holds, or -1 for a curve that is none of them - a bench file, or
   one edited by hand. That curve is KEPT as read and written back as read; it only changes when
   one of the three buttons is pressed. */
static int ffb_CurveNow(void){
    int c,k;
    for(k=0;k<NSPD;k++) if(ffb_spdKmh[k]!=SPD_KMH[k]) return -1;
    for(c=0;c<NCRV;c++){
        for(k=0;k<NSPD;k++) if(ffb_spdPct[k]!=CRV_PCT[c][k]) break;
        if(k==NSPD) return c;
    }
    return -1;
}

/* ---- THE DEVELOPERS' HOTKEYS - OFF in the file this page writes ----------------------------
 * The module carries in-game keys for the bench: the roll and crash banks on F1..F6, the range
 * keys (v822), the nine presets (F1..F5 and U I O P), G/N for the road model, K/L for the impulse
 * channel. His decision against them in a shipping build is from 2026-08-01 ("no F-keys in a
 * shipping build") and was restated for this release: the ini the utility writes for players
 * switches every one OFF, 0 = no key.
 * THE BENCH KEEPS ITS KEYS with dev_keys = 1 in [ffb]: then this page never writes them and they
 * are carried through as the file holds them. Nothing in the module reads dev_keys.
 * CHECKED IN THE MODULE BEFORE WRITING (v821 source): road_roll_keys, crash_keys, range_keys and
 * impulse_key_on/off read 0 as off. ground_key_on/off and presetN_key DO NOT - the module falls
 * back to the default key on 0 (GroundReadIni) - and with both banks off F1..F5 go back to the
 * presets. Written anyway, as he decided; making 0 mean off there is the first module need in
 * docs\UTILITY-MODULE-NEEDS.md, and this release is blocked on it. */
static const char *FFB_DEVKEYS[] = {
    "road_roll_keys","crash_keys","range_keys","ground_key_on","ground_key_off",
    "impulse_key_on","impulse_key_off",
    "preset1_key","preset2_key","preset3_key","preset4_key","preset5_key",
    "preset6_key","preset7_key","preset8_key","preset9_key", NULL };
#define FFB_NDEVKEYS ((int)(sizeof FFB_DEVKEYS/sizeof FFB_DEVKEYS[0])-1)

/* ---- WHAT AN EARLIER VERSION LEFT IN THE FILE, and the road model still applies ---------------
 * The old feedback's rows are gone from this page, and their keys are carried through a save
 * untouched - the page has no control for them and no business rewriting them. But the road
 * module still multiplies its damper by the old damper and truck percents, and still runs the old
 * slip channel (`sat`) above 15 degrees of body slip. A 1.4.3 player who had set one of them would
 * carry it into the road model unseen - so the page reads them and NAMES any that is off its
 * reference. Making the module ignore them is a module need. */
#define NOLD 5
static const char *OLDKEY[NOLD]  = { "sat","damper_static","damper_moving","truck_static",
                                     "truck_moving" };
static const char *OLDNAME[NOLD] = { "slide feel","old parking damper","old driving damper",
                                     "truck parking damper","truck driving damper" };
static int ffb_old[NOLD] = { 100,100,100,100,100 };

/* ---- A FILE THAT SWITCHES THE ROAD MODEL OFF ---------------------------------------------------
 * The page no longer writes `ffb_mode` or `ground`: the module's own handling of them is left
 * alone, and a file that has neither runs the road model (the module's default). But a file can
 * still carry one that turns the road model off - `ffb_mode = 1` (the old feedback), `ffb_mode =
 * 2` (the developers'), or `ground = 0` with no `ffb_mode` - from the bench or from the unreleased
 * selector of 2026-09-14. Then every slider on this page moves nothing, so the page says so. */
static int ffb_fileMode = -1;      /* `ffb_mode` as the live file holds it, -1 = absent */
static int ffb_fileGround = 1;
static int ffb_KeyPresent(const char *path,const char *key);    /* with the settings file below */
static void ffb_ReadModelKeys(const char *path){
    ffb_fileMode = ffb_KeyPresent(path,"ffb_mode")
                 ? (int)GetPrivateProfileIntA("ffb","ffb_mode",0,path) : -1;
    ffb_fileGround = (int)GetPrivateProfileIntA("ffb","ground",1,path);
}
/* 0 = the road model runs; 1 = the file switches it off; 2 = the file asks for the developers'.
   Exactly the module's reading (GroundReadIni): ffb_mode 0 turns the road model on, 1 or 2 off,
   and any other value - or none - leaves it to `ground`, whose default is on. */
static int ffb_FileModel(void){
    if(ffb_fileMode==2) return 2;
    if(ffb_fileMode==1) return 1;
    if(ffb_fileMode==0) return 0;
    return ffb_fileGround==0 ? 1 : 0;
}

/* The rotation range. NOT a setting we impose - a DECLARATION of what the wheel's own driver is
   set to, so the mod knows what to serve. Alex, 2026-07-25 (translated): "we do not set it, we
   select it".
   The UI must say SELECT, never SET. Anything outside this list is refused by the mod, so no
   control here may produce it. Sorted for display; the mod's own table is in a different order
   because F1..F5 addressed it by index. */
#define NDOR 8
static const int DORDEG[NDOR] = { 90,360,540,600,720,900,1080,1440 };
#define DOR_DEFAULT 600
static int ffb_range = DOR_DEFAULT;
/* WHICH RANGES WERE DRIVEN ON THE 2.0 MODEL is a fact for the release notes, not for the page:
   600 is the reference everything was tuned on; 90 and 360 were driven at full force on ours-040
   and ours-042 ("F3 and F4 better"); 900 with the grown centring on ours-040, ours-043 and ours-044
   ("the new F4 feels right at 900, keep it"); 540, 720, 1080 and 1440 follow the law (R/600)^(1/3)
   carried on from those points and were never driven. The buttons said so as MEASURED and BY THE
   LAW until 2026-10-01, when he asked to have both removed - see FFB_DOR_600. */

/* Read from the INI, written back untouched, and deliberately NOT given a control. It scales the
   SURPLUS of heavy-vehicle steering weight, and that feature was closed outright on 2026-07-23,
   (translated: "I don't want trucks made heavier either"). A slider here would ship a feature he
   refused; dropping
   the key on save would silently un-refuse it for anyone who set it by hand. */
static int ffb_truck = 0;
/* The instance GUID of the wheel the .asi should take, preserved by the same rule: a key this
   file does not know about is a key this file DESTROYS, and that has happened twice. */
static char ffb_device[80] = "";

/* ---- WHY THE ROAD MODEL IS NOT DRIVING THE WHEEL, said in red, and only then ----------------
 * The model used to have a row of its own at the top - a name, a NEW plate, a line under them -
 * and he took it off on 2026-10-01: one model needs no picking and the row only made the page
 * taller (see the top of this file). What that line ALSO carried is kept: an earlier module still
 * in the folder, or a file that switches the model off, means every slider moves nothing, and the
 * page says so in red straight above them. ffb_Layout gives the line room only while it has
 * something to say. */
static HWND ffb_modeText;
static HWND ffb_presetLbl, ffb_impBtn, ffb_expBtn, ffb_recommBtn;

/* ---- THE 1.4.3 PAGE, AGAIN - 2026-10-01 -------------------------------------------------------
 * His verdict on the 2.0 page, translated: "I don't understand why you redesigned the application
 * so much compared to 1.4. With all my new corrections the difference should be almost none - a
 * few sliders added, and that's it." And: "the fonts differ, and the colour, and the layout of the
 * page itself." The channel groups of 2026-09-30 - item B4 of docs\UTILITY-CHANGES-v821.md, which
 * he had read as a sentence and never seen as a page - are gone, and the page is 1.4.3's again:
 * the range across the top, then FORCE FEEDBACK STRENGTH on the left, what you drag while judging a
 * drive, and WHEEL WEIGHT on the right, what you set once, each under its own heading centred in
 * the section face. The road model's rows go where the rows it retired were: Road texture joins the
 * strength list, and the steering and damper rows take the place of the Cars / Trucks table, in
 * that table's own style - a small heading per half and a seam between them.
 * The same tables decide what is shown and where, so "which rows does the page have" and "where
 * are they" cannot disagree. */
enum { LI_END=0, LI_HEAD, LI_LINE, LI_SUB, LI_ROW, LI_CURVE, LI_GUNNOTE, LI_SEAM };
typedef struct { int kind, arg; } ffb_lay;
/* the words the layout places, by the `arg` of a HEAD, LINE or SUB item */
enum { T_HEAD_L, T_HEAD_R, T_LINE_L, T_SUB_STEER, T_SUB_DAMP, NTEXT };
/* The gunfire note sits right under the gunfire row: full width under both columns it "does not
   read as anything to do with gunfire", his words of 2026-10-01. */
static const ffb_lay LAY_L[] = {
    {LI_HEAD,T_HEAD_L},{LI_LINE,T_LINE_L},
    {LI_ROW,S_CRASH},{LI_ROW,S_OBJ},{LI_ROW,S_PED},{LI_ROW,S_GUN},
    {LI_GUNNOTE,0},{LI_ROW,S_ROAD},{LI_ROW,S_GTEXT},{LI_END,0} };
static const ffb_lay LAY_R[] = {
    {LI_HEAD,T_HEAD_R},
    {LI_SUB,T_SUB_STEER},{LI_ROW,S_GWEIGHT},{LI_CURVE,0},{LI_ROW,S_GBREAK},{LI_SEAM,0},
    {LI_SUB,T_SUB_DAMP},{LI_ROW,S_GPARK},{LI_ROW,S_GDRIVE},{LI_ROW,S_GSLIP},{LI_END,0} };
/* TOTAL EFFECTS (WITHOUT DAMPERS), called Overall strength until his rename of 2026-10-01, ON ITS
   OWN ROW ABOVE BOTH COLUMNS and centred on the page (GEO_TOP) - like the range, the other setting that
   acts on everything under it. His two questions of 2026-10-01 settle it: "should it not be above
   everything?", and then, translated, "if overall strength also affects the steering, the steering
   has to sit under it - now it looks as if it only acts on the left column". It does act on the
   right one: the steering weight is a constant force, through the same SetMag choke as every hit.
   What it does NOT touch is the dampers, and the Damper half says so in its own heading. */
static const ffb_lay LAY_TOP[] = { {LI_ROW,S_MASTER},{LI_END,0} };
#define FFB_NLAY 3                     /* 0 left column, 1 right column, 2 the full-width row */
static const ffb_lay *ffb_Lay(int side){ return side==2 ? LAY_TOP : side ? LAY_R : LAY_L; }
static int ffb_LayHas(const ffb_lay *L,int kind,int arg){
    for(;L->kind!=LI_END;L++) if(L->kind==kind&&L->arg==arg) return 1;
    return 0;
}
static int ffb_RowShown(int row){
    int s;
    for(s=0;s<FFB_NLAY;s++) if(ffb_LayHas(ffb_Lay(s),LI_ROW,row)) return 1;
    return 0;
}

static HWND ffb_slider[NSLIDER], ffb_box[NSLIDER], ffb_hint[NSLIDER], ffb_reset[NSLIDER],
            ffb_label[NSLIDER], ffb_unit[NSLIDER];
static HWND ffb_text[NTEXT], ffb_seam, ffb_colDiv, ffb_noteGun, ffb_noteOld;
static HWND ffb_recommNote, ffb_presetDiv;
static HWND ffb_curveLbl, ffb_curveBtn[NCRV], ffb_curveHint, ffb_curveReset;
static int  ffb_layTop, ffb_built;    /* where ffb_Layout starts: under the range's separator */
/* What the last layout gave room to - the red line and the earlier-version note each take space
   only while they have text, so a change in WHETHER they have any means laying the page out again */
static int  ffb_laidWarn=-1, ffb_laidOld=-1;
static HWND ffb_dorBtn[NDOR], ffb_lamp, ffb_lampText;
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

/* ---- WHICH MODULE THE GAME FOLDER HOLDS - an upgrade check ---------------------------------
 * A player who installed 1.4.3 has its module in the folder, recorded in the journal as ours, so
 * the header reads "Mod is ENABLED" while the wheel is driven by the old feedback. Nothing replaces
 * it until the mod is switched off and on - so the page says so, in red, above the sliders.
 * By md5: this version's module, AN EARLIER BUILD OF OURS (the 1.4.3 one named - c56669c4, the
 * build 1.2.0 through 1.4.3 shipped), or a file we do not know - a bench build, left alone and not
 * called anything. */
#define ASI_RELEASE_143_MD5 "c56669c45adf1b58ef06f8b666f33586"
enum { FFBV_NONE=0, FFBV_NEW=1, FFBV_OLD=2, FFBV_OTHER=3 };
static int  ffb_variant = FFBV_NONE;
static int  ffb_variant143 = 0;
static int ffb_VariantOfMd5(const char *m){
    if(SEq(m,PAYLOAD_ASI_MD5)) return FFBV_NEW;
    if(is_our_asi(m))          return FFBV_OLD;
    return FFBV_OTHER;
}
static void ffb_RefreshVariant(void){
    char path[MAX_PATH],m[33];
    SCpy(path,g_gameDir); SCat(path,"mafia_ffb.asi");
    ffb_variant143=0;
    if(!file_exists(path)){ ffb_variant=FFBV_NONE; return; }
    if(!file_md5(path,m)){ ffb_variant=FFBV_OTHER; return; }
    ffb_variant=ffb_VariantOfMd5(m);
    ffb_variant143=SEq(m,ASI_RELEASE_143_MD5);
}
/* WHY THE ROAD MODEL IS NOT WHAT DRIVES THE WHEEL, or NULL when it is. Said in red under its name
   (launcher.c colours the line): an earlier module in the folder first, because that one needs
   the toggle; then a file that switches the model off, which needs the file. */
static const char *ffb_ModelWarning(void){
    int fm=ffb_FileModel();
    if(g_state[LTAB_FFB]==JMOD_ON && ffb_variant==FFBV_OLD)
        return ffb_variant143
            ? "The game folder still holds the force feedback module of release 1.4.3, not this "
              "version's - switch the mod off and on again (with Mafia closed) to install it."
            : "The game folder still holds an earlier build of this mod's force feedback module - "
              "switch the mod off and on again (with Mafia closed) to install this version's.";
    if(fm==1)
        return "mafia_ffb.ini switches the road model off (ffb_mode = 1, or ground = 0) - the "
               "sliders below move nothing until that line is removed from the file.";
    if(fm==2)
        return "mafia_ffb.ini asks for the game developers' own feedback (ffb_mode = 2), which "
               "this utility does not offer - the sliders below move nothing until that line "
               "is removed from the file.";
    return NULL;
}

/* ---- 1.4.3'S GEOMETRY ------------------------------------------------------------------------
 * Measured 2026-08-01: in one column this page came to 971 px, most of the way down a 1400 px work
 * area, and "even on a big screen that will not do" - hence two columns, and these are 1.4.3's.
 * LEFT, to the pixel: label 26..176, undo 160..180, slider 186..436, box 446..502, % 508..520,
 * hint 522..722. RIGHT, from COL_MID: label 0..150, undo 156..176, slider 182..312, box 318..374,
 * unit 378..406, hint 410..544 - 1.4.3's table had a 124 px label and no hint, and the road model's
 * rows have longer names and each says what it does, so both grew inside the same column. Every
 * offset is measured against its neighbour, and the self-test measures every string against the box
 * it is drawn in. */
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
#define WW_LBL_W   150
#define WW_SL_X    182
#define WW_SL_W    130
#define WW_BX_X    318
#define WW_UNIT_X  378
#define WW_UNIT_W  28
#define WW_HINT_X  410
#define WW_HINT_W  (W_RIGHT-WW_HINT_X)       /* 134 */
/* How wide the WHEEL WEIGHT table is, label to unit. The seam between its two halves is drawn to
   exactly this and no further - 1.4.3's rule: run to the column's full width it reads as the end of
   the section rather than a seam inside it. */
#define WW_TABLE_W WW_HINT_X
#define R_H        27
/* the build-up row's three buttons, from the slider's edge: 182..266, 272..356, 362..446, and the
   line for a curve that is none of the three after them, 452..544 */
#define CRV_W    84
#define CRV_GAP  6
#define CRV_HINT_X (WW_SL_X+NCRV*(CRV_W+CRV_GAP))
#define CRV_HINT_W (W_RIGHT-CRV_HINT_X)
#define DOR_BTN_H 30                           /* 1.4.3's: one line, the degrees */
/* THE PAGE'S HEIGHT WITH NOTHING TO WARN ABOUT, held as a ceiling the self-test enforces: 657 px,
   measured 2026-10-01 after he asked for the utility to be shorter - 2.0 had let it grow to 861 and
   1.4.3 had 680. This page has grown back after being cut twice (2026-08-05 and 2026-10-01, his
   words both times), so a change that makes it taller has to raise this number on purpose rather
   than by accident. */
#define FFB_PAGE_MAX 657

/* One row's boxes. The two columns differ only in these numbers, and a row asks for its own -
   the control, the layout and the self-test all measure against the same box. */
typedef struct { int x, lblW, slX, slW, bxX, unX, unW, hnX, hnW; } ffb_geo;
/* The % is 14 px, not 1.4.3's 12: the self-test measured the glyph at more than 12 in the field
   face, so 1.4.3 had been drawing it clipped and nothing had ever asked. 508..522 meets the hint. */
static const ffb_geo GEO_L = { COL_LABEL, 150, COL_SLIDER, W_SLIDER, COL_BOX, COL_BOX+62, 14,
                               COL_HINT, W_HINT };
static const ffb_geo GEO_R = { COL_MID, WW_LBL_W, COL_MID+WW_SL_X, WW_SL_W, COL_MID+WW_BX_X,
                               COL_MID+WW_UNIT_X, WW_UNIT_W, COL_MID+WW_HINT_X, WW_HINT_W };
/* THE TOTAL EFFECTS ROW, CENTRED ON THE PAGE - his "put it in the centre, so it is clear it belongs
   to both columns" (2026-10-01). The left column's slider and box, a label wide enough for the name
   that says what it leaves out, and the whole group - label 0..250, undo 256..276, slider 282..532,
   box 542..598, % 604..618, hint 618..778 - centred on the page's middle. */
#define TOP_LBL_W   250
#define TOP_HINT_W  160
#define TOP_GROUP_W (TOP_LBL_W+32+W_SLIDER+10+56+6+14+TOP_HINT_W)          /* 778 */
#define TOP_X       (COL_LABEL+(W_FULL-TOP_GROUP_W)/2)
static const ffb_geo GEO_TOP = { TOP_X, TOP_LBL_W, TOP_X+TOP_LBL_W+32, W_SLIDER,
                                 TOP_X+TOP_LBL_W+32+W_SLIDER+10, TOP_X+TOP_LBL_W+32+W_SLIDER+10+62, 14,
                                 TOP_X+TOP_GROUP_W-TOP_HINT_W, TOP_HINT_W };
static const ffb_geo *ffb_GeoOf(int row){
    return ffb_LayHas(LAY_TOP,LI_ROW,row) ? &GEO_TOP
         : ffb_LayHas(LAY_R,LI_ROW,row)   ? &GEO_R : &GEO_L;
}
static int ffb_HintW(int row){ return ffb_GeoOf(row)->hnW; }

/* ---- the words around the sliders, in one place so the self-test measures them ---- */
/* The two notes that sat under the columns until later on 2026-10-01 went for the height he asked
   back ("too tall"): the tall mark is named in the line under the strength heading, and the slide
   lightness answered in his chat (78% of the damper goes at a full slide, 22% stays). The two
   small headings of the table carry the two facts that are not obvious from a row. */
static const char *FFB_TEXT[NTEXT] = {
    "FORCE FEEDBACK STRENGTH",
    "WHEEL WEIGHT",
    "100% - the tall mark - is the shipped feel. Up to 400% for weaker wheelbases.",
    /* his question: "what is the difference between steering weight and build-up with speed?" */
    "Steering - build-up is how the weight grows with speed",
    "Damper - not scaled by Total effects" };
/* Beside the range heading. 1.4.3's words, with 600 named in them: "in the previous version the
   description itself made it clear that 600 is the default" (2026-10-01). The line that stood under
   the 600 button said the same thing and went for the height. It still says SELECT, never SET:
   the range is what the wheel's driver is set to. */
#define FFB_DOR_TEXT "what your wheel's own driver is set to - 600 is the default and recommended. " \
                     "It changes nothing on the wheel."
/* One line under the gunfire row, starting with the word it is about. */
#define FFB_NOTE_GUN "Gunfire is recommended OFF: it shakes the wheel for every shot fired from " \
                     "your car, not just yours."
/* Beside the build-up buttons when the file holds a curve that is none of the three - a bench file
   or a hand edit, kept as read. 92 px in the WHEEL WEIGHT column. */
#define FFB_CURVE_KEPT "custom - kept"
/* "Back to default" says what it does - found 2026-09-30: it said "every slider on this page back
   to 100%" while gunfire went to 0 and the range and wheel stayed put. */
#define FFB_RECOMM_NOTE "every slider back to its tall mark - the reference, gunfire 0 - and " \
                        "build-up back to Reference. The range, the wheel and the presets are " \
                        "not touched."
#define FFB_PRESET_LBL "PRESETS - the one lit green is being edited, and every value on this " \
                       "page goes into it"

/* The note under the columns: the earlier version's values the road model still applies. */
static void ffb_OldNote(char *out,int n){
    int i,any=0; char one[64];
    out[0]=0;
    for(i=0;i<NOLD;i++){
        if(ffb_old[i]==100) continue;
        if(!any) SCpy(out,"From an earlier version and still applied, with no control here: ");
        else     SCat(out,", ");
        wsprintfA(one,"%s %d%%",OLDNAME[i],ffb_old[i]);
        if(SLen(out)+SLen(one)+40<n) SCat(out,one);
        any=1;
    }
    if(any) SCat(out," (mafia_ffb.ini)");
}

/* ---- live hints ---------------------------------------------------------------------------- */
static const char *ffb_GunWord(int v){
    if(v==0)   return "off - the tap is silenced";
    if(v>200)  return "a buzzer in a shootout";
    return "0 silences the tap";
}
static const char *ffb_HintFor(int i,int v){
    /* ABOVE 100 THE BIG ONES CLIP - said, not hidden. The kick is formula x trim x slider and is
       then clamped at the crash cap, so above 100 every high-speed hit lands on the same ceiling.
       The fix (the cap grows with the slider) is his decision and a module change. */
    if(i==S_CRASH) return v>100 ? "big hits clip above 100" : "";
    if(i==S_MASTER && v>100) return "may clip above 100";
    if(i==S_GUN) return ffb_GunWord(v);
    if(i==S_PED) return v==0 ? "off - pedestrians silent" : "";
    if(i==S_GPARK && v==0) return "none at a standstill";
    if(i==S_GSLIP && v==0) return "stays in a slide";
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
/* Is a key actually present, as opposed to absent-and-defaulted? Asked with TWO different
   defaults, because GetPrivateProfileInt returns UINT: a negative sentinel comes back as
   0xFFFFFFFF and `result < 0` is a comparison that can never be true. */
static int ffb_KeyPresent(const char *path,const char *key){
    return (int)GetPrivateProfileIntA("ffb",key,1,path)
        == (int)GetPrivateProfileIntA("ffb",key,2,path);
}
/* dev_keys = 1: this file belongs to the bench, and its hotkeys are its own business. */
static int ffb_DevKeysKept(const char *path){
    return GetPrivateProfileIntA("ffb","dev_keys",0,path)!=0;
}
/* Is the file already in the released state - every developer key present, and 0? */
static int ffb_DevKeysOff(const char *path){
    int i;
    for(i=0;FFB_DEVKEYS[i];i++){
        if(!ffb_KeyPresent(path,FFB_DEVKEYS[i])) return 0;
        if(GetPrivateProfileIntA("ffb",FFB_DEVKEYS[i],1,path)!=0) return 0;
    }
    return 1;
}

/* The keys THIS PAGE writes out of its own controls. Everything else in [ffb] is carried
   through verbatim by IniCarryOthers - see ini_carry.h for why it is a list of what we own
   rather than a list of what to preserve.
   Since 2026-09-30 the road model's keys are on it (its sliders write them, so a preset and an
   export carry them), and three kinds of key are OFF it and carried exactly as the file had them:
   the old feedback's (spring, sat, the damper pair, the truck pair, and the single `damper` they
   once migrated from), and the model choice (`ffb_mode`, `ground`) - the page chooses no model
   any more and leaves the module's handling of those keys alone. FFB_DEVKEYS are owned too unless
   the file keeps its own (dev_keys = 1). */
static const char *FFB_OWNED[] = {
    "master","crash","objects","ped","gun","road",
    "truck","range","device",
    "ground_sat_k","ground_caster_k","ground_opt_deg","ground_damp_stand",
    "ground_damp_move_pct","ground_damp_slip","ground_detail_k","ground_detail_lim",
    "ground_spd1_kmh","ground_spd2_kmh","ground_spd3_kmh","ground_spd4_kmh",
    "ground_spd5_kmh","ground_spd6_kmh","ground_spd7_kmh","ground_spd8_kmh",
    "ground_spd1_pct","ground_spd2_pct","ground_spd3_pct","ground_spd4_pct",
    "ground_spd5_pct","ground_spd6_pct","ground_spd7_pct","ground_spd8_pct",
    NULL };
#define FFB_NOWNED ((int)(sizeof FFB_OWNED/sizeof FFB_OWNED[0])-1)

/* A road-model row whose value was set without its keys - a caller that wrote ffb_val directly -
   has moved, and its keys follow it now. An UNMOVED row keeps the keys it read, untouched, which
   is the whole reason the page holds them apart. */
static void ffb_SyncRaw(void){
    int i;
    for(i=NPLAIN;i<NSLIDER;i++)
        if(ffb_val[i]!=ffb_RowFromRaw(i)) ffb_RawFromRow(i,ffb_val[i]);
}

/* 64 KB, static rather than on the stack: the carried-through part of the bench file is 245
   keys today and this function is called from a window procedure. */
static char ffb_saveBuf[65536];

static void ffb_SaveTo(const char *path,int announce){
    char *buf=ffb_saveBuf; buf[0]=0; char line[512];
    const char *owned[FFB_NOWNED+FFB_NDEVKEYS+1];
    int i,n=0,keepKeys;

    ffb_truck = (int)GetPrivateProfileIntA("ffb","truck",ffb_truck,path);
    /* `device` is a STRING, so it needs the other reader. Read into a SEPARATE buffer and copy
       only on a hit: passing ffb_device as both the default and the destination would have the
       API writing into the string it is reading its default from. */
    {
        char dev[80];
        GetPrivateProfileStringA("ffb","device","",dev,sizeof(dev),path);
        if(dev[0]){ int k=0; for(;dev[k]&&k<(int)sizeof(ffb_device)-1;k++) ffb_device[k]=dev[k];
                    ffb_device[k]=0; }
    }
    ffb_SyncRaw();
    /* THE HOTKEYS ARE THE FILE'S OWN BUSINESS ONLY ON THE BENCH - asked of the file being
       written, like everything else carried through, so a preset or an export cannot switch a
       bench's keys off and the bench's live file cannot switch a preset's on. */
    keepKeys=ffb_DevKeysKept(path);
    for(i=0;FFB_OWNED[i];i++) owned[n++]=FFB_OWNED[i];
    if(!keepKeys) for(i=0;FFB_DEVKEYS[i];i++) owned[n++]=FFB_DEVKEYS[i];
    owned[n]=NULL;

    /* MEASURED before changing it, 2026-08-12: nothing reads this line back. The file is rewritten
       whole, so the header is a caption for a human and not a marker anything matches on. */
    SCat(buf,"; Mafia force feedback - written by " BRAND_NAME ", Force Feedback tab\r\n"
             "; The road model (2026). The slider lines are a PERCENT of the reference: 100 =\r\n"
             "; the tuned feel, unchanged to the byte, and every force goes to 400. The block\r\n"
             "; further down holds the module's own numbers, written from its sliders. The mod\r\n"
             "; re-reads this file about once a second, so a change takes effect mid-drive.\r\n\r\n"
             "[ffb]\r\n");
    for(i=0;i<NPLAIN;i++){
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
             "; SIMAGIC 12 Nm wheelbase set to 600 degrees and 6.6 Nm. That is what 100% means.\r\n"
             "; 90 and 360 were measured too; the others follow the law, never driven.\r\n");
    wsprintfA(line,"range=%d\r\n",ffb_range); SCat(buf,line);

    SCat(buf,"\r\n; WHICH WHEEL, if you have more than one force-feedback device. Empty or\r\n"
             "; absent, the mod takes the first one Windows offers. The mod LOGS every device\r\n"
             "; it is offered, with its GUID - that log is where you read the string to paste.\r\n");
    if(ffb_device[0]){ wsprintfA(line,"device=%s\r\n",ffb_device); SCat(buf,line); }
    else               SCat(buf,"; device={XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}\r\n");

    /* ================= THE ROAD MODEL'S OWN NUMBERS, from its sliders ================= */
    SCat(buf,"\r\n; THE ROAD MODEL'S OWN NUMBERS, written by its sliders. Each slider scales its\r\n"
             "; whole channel in proportion, ceiling included, and 100% on a slider writes\r\n"
             "; exactly the reference given in brackets.\r\n"
             "; Steering weight - the aligning torque at the tyre peak (7875) and the caster\r\n"
             "; (2400), always together:\r\n");
    wsprintfA(line,"%s=%d\r\n%s=%d\r\n",GKEY[GK_SATK],ffb_gk[GK_SATK],
              GKEY[GK_CASTER],ffb_gk[GK_CASTER]); SCat(buf,line);
    SCat(buf,"; Breakaway - the front slip angle where grip peaks, degrees (9):\r\n");
    wsprintfA(line,"%s=%d\r\n",GKEY[GK_OPT],ffb_gk[GK_OPT]); SCat(buf,line);
    SCat(buf,"; Parking damper at a standstill (6000). The module reads 0 as the OLD heavy\r\n"
             "; parked damper, so the slider's 0% is written as 1:\r\n");
    wsprintfA(line,"%s=%d\r\n",GKEY[GK_STAND],ffb_gk[GK_STAND]); SCat(buf,line);
    SCat(buf,"; Driving damper - percent of the profile's moving damper that stays (50):\r\n");
    wsprintfA(line,"%s=%d\r\n",GKEY[GK_MOVE],ffb_gk[GK_MOVE]); SCat(buf,line);
    SCat(buf,"; Lightness in a slide - percent of the damper removed at a full slide (78):\r\n");
    wsprintfA(line,"%s=%d\r\n",GKEY[GK_SLIP],ffb_gk[GK_SLIP]); SCat(buf,line);
    SCat(buf,"; Road texture, the cobbles - gain (900) and its ceiling (5000), together:\r\n");
    wsprintfA(line,"%s=%d\r\n%s=%d\r\n",GKEY[GK_DETK],ffb_gk[GK_DETK],
              GKEY[GK_DETLIM],ffb_gk[GK_DETLIM]); SCat(buf,line);
    SCat(buf,"; Weight build-up with speed - km/h and percent at eight points. The tab offers\r\n"
             "; three curves that were driven: Light, Reference and Heavy.\r\n");
    for(i=0;i<NSPD;i++){
        wsprintfA(line,"ground_spd%d_kmh=%d\r\n",i+1,ffb_spdKmh[i]); SCat(buf,line);
    }
    for(i=0;i<NSPD;i++){
        wsprintfA(line,"ground_spd%d_pct=%d\r\n",i+1,ffb_spdPct[i]); SCat(buf,line);
    }

    /* ================= THE DEVELOPERS' HOTKEYS, OFF ================= */
    if(!keepKeys){
        SCat(buf,"\r\n; DEVELOPER HOTKEYS - OFF. The module carries in-game keys for the test bench\r\n"
                 "; (F-key banks, letter ladders, G/N, K/L); a player's wheel must not change feel\r\n"
                 "; on a stray key. 0 = no key. Written on every save - put dev_keys=1 in this\r\n"
                 "; section to keep your own.\r\n");
        for(i=0;FFB_DEVKEYS[i];i++){
            wsprintfA(line,"%s=0\r\n",FFB_DEVKEYS[i]); SCat(buf,line);
        }
    }

    /* ================= KEYS THIS PAGE DOES NOT EDIT, CARRIED THROUGH VERBATIM =================
     * This function builds the whole file in a buffer and writes it with CREATE_ALWAYS, so ANY
     * key it does not print is destroyed. That is fine for the keys it owns and fatal for
     * everything else, because the mod reads more keys than this dialog shows.
     *
     * Found 2026-08-07, before it could bite: `impulse_kick` defaults to 0 in the .asi, so one
     * drag of any slider would have silently turned the impulse channel OFF - the channel just
     * had just called "the reference" (his word, etalon) - and the file would have looked
     * perfectly normal afterwards. His words for the risk, translated: *"so the utility does not
     * spoil everything"*.
     *
     * IT WAS A HAND-WRITTEN LIST OF SEVEN KEYS UNTIL 2026-09-14, and by then the module read
     * fifty-five more that were not on it. The list is gone: the page names what it OWNS, and
     * ini_carry.h writes back everything else exactly as the file had it. A new key in the mod
     * now needs no change in this program at all. */
    {
        int carried = IniCarryOthers(buf, (int)sizeof(ffb_saveBuf), path, "ffb", owned,
            "\r\n; ---- carried over from the file as it was ----\r\n"
            "; These are not controls on this page. The utility preserves them so that saving\r\n"
            "; a slider cannot change anything else about how the mod behaves.\r\n");
        if(carried < 0){
            /* The one outcome that must not pass silently: we could not reproduce the file we
               were handed, so we do not overwrite it with a shorter one. */
            char m[400];
            wsprintfA(m,"REFUSED to save %s - its [ffb] section is too large to carry through "
                        "safely, so nothing was written and your settings are untouched",path);
            LogLine(m);
            return;
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
static void ffb_RefreshCurve(void);
static void ffb_SyncTestBtn(void);
static void ffb_Layout(void);

/* The lines that can change without a slider moving: why the road model is not in force, and the
   note naming an earlier version's values. Each takes room only while it has text, so when one of
   them appears or goes, the page is laid out again - and only then, since a layout moves every
   control on the page. */
static void ffb_RefreshNotes(void){
    char note[400];
    const char *w=ffb_ModelWarning();
    if(ffb_modeText){
        SetWindowTextA(ffb_modeText,w?w:"");
        InvalidateRect(ffb_modeText,NULL,TRUE);
    }
    ffb_OldNote(note,sizeof note);
    if(ffb_noteOld){
        SetWindowTextA(ffb_noteOld,note);
        InvalidateRect(ffb_noteOld,NULL,TRUE);
    }
    if(ffb_built&&((w!=NULL)!=ffb_laidWarn||(note[0]!=0)!=ffb_laidOld)) ffb_Layout();
}
/* Everything the page shows, from what it holds. */
static void ffb_RefreshAll(void){
    int i;
    ffb_Layout();
    for(i=0;i<NSLIDER;i++) ffb_RefreshRow(i);
    ffb_RefreshCurve();
    ffb_RefreshNotes();
}

/* Every value this page owns back to the reference - the recommended set. What "no file" means,
   what Back to default writes, and where a fresh process starts, so the three cannot disagree. */
static void ffb_ResetValues(void){
    int i;
    for(i=0;i<NPLAIN;i++) ffb_val[i]=SREF[i];
    for(i=0;i<NGK;i++)    ffb_gk[i]=GREF[i];
    for(i=NPLAIN;i<NSLIDER;i++) ffb_val[i]=ffb_RowFromRaw(i);
    for(i=0;i<NSPD;i++){ ffb_spdKmh[i]=SPD_KMH[i]; ffb_spdPct[i]=CRV_PCT[CRV_REF][i]; }
}

/* THE VALUES OF A FILE. What the live file, a preset slot and an import are all read with. A
   preset used to switch the model too, because its file carried `ffb_mode` and the load took it -
   found 2026-09-30; the page chooses no model any more, so no load can. Returns 0 for no file. */
static int ffb_LoadValues(const char *path){
    int i,deg,ok=0,dampLegacy;
    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) return 0;
    for(i=0;i<NPLAIN;i++)
        ffb_val[i]=Clamp((int)GetPrivateProfileIntA("ffb",SKEY[i],SREF[i],path),0,SBOXMAX[i]);
    /* The road model's keys, as the module would read them: absent means the module's default,
       and the module's default IS his reference. Read as LONG - GetPrivateProfileInt is UINT. */
    for(i=0;i<NGK;i++){
        LONG v=(LONG)GetPrivateProfileIntA("ffb",GKEY[i],(UINT)GREF[i],path);
        ffb_gk[i]=Clamp((int)v,0,GCAP[i]);
    }
    if(ffb_gk[GK_OPT]<1) ffb_gk[GK_OPT]=1;          /* the module's own floor */
    for(i=NPLAIN;i<NSLIDER;i++) ffb_val[i]=ffb_RowFromRaw(i);
    for(i=0;i<NSPD;i++){
        char k[24];
        wsprintfA(k,"ground_spd%d_kmh",i+1);
        ffb_spdKmh[i]=Clamp((int)(LONG)GetPrivateProfileIntA("ffb",k,(UINT)SPD_KMH[i],path),0,400);
        wsprintfA(k,"ground_spd%d_pct",i+1);
        ffb_spdPct[i]=Clamp((int)(LONG)GetPrivateProfileIntA("ffb",k,(UINT)CRV_PCT[CRV_REF][i],path),
                            0,400);
    }
    /* THE EARLIER VERSION'S KEYS, read as the module reads them - its `damper` migration
       included (ffb_settings.c: the old single key is the default for both halves) - and only to
       be SHOWN; they are carried, never written. */
    dampLegacy=(int)GetPrivateProfileIntA("ffb","damper",100,path);
    for(i=0;i<NOLD;i++){
        int dflt=(i==1||i==2)?dampLegacy:100;
        ffb_old[i]=(int)GetPrivateProfileIntA("ffb",OLDKEY[i],(UINT)dflt,path);
    }
    ffb_truck=(int)GetPrivateProfileIntA("ffb","truck",0,path);
    deg=(int)GetPrivateProfileIntA("ffb","range",DOR_DEFAULT,path);
    for(i=0;i<NDOR;i++) if(DORDEG[i]==deg) ok=1;
    if(ok) ffb_range=deg;
    else { char m[200]; wsprintfA(m,"range=%d in that file is not one the mod accepts - keeping %d",
                                 deg,ffb_range); LogLine(m); }
    /* THE CHOSEN DEVICE IS READ HERE TOO, not only harvested in ffb_SaveTo. Without this the
       selector would light "First one offered" over a file that names a wheel - the same shape of
       lie the H-shifter page paid for on 2026-08-01. Into a separate buffer: passing ffb_device
       as both the default and the destination has the API writing into the string it reads. */
    {
        char dev[80];
        GetPrivateProfileStringA("ffb","device","",dev,sizeof(dev),path);
        SCpy(ffb_device,dev);
    }
    for(i=0;i<NSLIDER;i++){ ffb_RefreshRow(i); ffb_logWas[i]=ffb_val[i]; ffb_logPend[i]=0; }
    ffb_RefreshCurve();
    ffb_RefreshDor();
    ffb_RefreshDev();
    ffb_RefreshNotes();
    return 1;
}

/* The live settings file: its values, and the two keys that could switch the road model off. */
static void ffb_LoadFrom(const char *path){
    if(!ffb_LoadValues(path)) return;
    ffb_ReadModelKeys(path);
    if(ffb_FileModel()) LogLine(ffb_ModelWarning());
    ffb_RefreshAll();
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
        const char *u=SUNIT[i][0]=='%'?"%":" deg";
        wsprintfA(m,"%s: %d%s -> %d%s",SNAME[i],ffb_logWas[i],u,ffb_val[i],u);
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
    ffb_RefreshVariant();
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
         * which is what Alex reported on 2026-08-15 for the gunfire row. */
        int i;
        ffb_ResetValues();
        for(i=0;i<NOLD;i++) ffb_old[i]=100;
        ffb_fileMode=-1; ffb_fileGround=1;
        for(i=0;i<NSLIDER;i++){ ffb_RefreshRow(i); ffb_logWas[i]=ffb_val[i]; ffb_logPend[i]=0; }
        ffb_RefreshAll();
        LogLine("no mafia_ffb.ini yet - showing the recommended settings, which is what switching "
                "the mod on will write");
        return;
    }
    ffb_LoadFrom(p);
    PageSaved(LTAB_FFB);
    LogLine("settings reloaded from mafia_ffb.ini");
}
/* A TOGGLE THAT INSTALLED THE MOD LEAVES A SETTINGS FILE THAT SWITCHES THE HOTKEYS OFF. Without
   one the module runs on its built-in defaults, and those are the bench's keys - a player who never
   touched a slider would have them. Marked dirty rather than written here, so it goes through the
   same save as everything else. */
static void FfbAfterInstall(void){
    char p[MAX_PATH];
    if(g_state[LTAB_FFB]!=JMOD_ON) return;
    ffb_RefreshVariant();
    ffb_IniPath(p);
    if(!ffb_DevKeysKept(p)&&!ffb_DevKeysOff(p)) PageDirty(LTAB_FFB);
    ffb_RefreshNotes();
}

/* ---- the mod's own status file ---------------------------------------------------------------
 * Written by the .asi (src/ffb/ffb_status.c). Two words, not one: `found` says we hold the wheel,
 * `effective` says the last force actually reached it - so the lamp can never read "connected"
 * while the wheel is silent. `generation` is the heartbeat. */
static DWORD ffb_lastGen=0, ffb_lastGenSeen=0;
static int   ffb_lampState=-1;      /* -1 unknown, 0 red, 1 green, 2 amber, 3 never run */
static char  ffb_devName[128]="";
static char  ffb_modVer[24]="";

/* THE LAMP'S SENTENCE, and since 2026-09-30 WHICH MODULE IS TALKING. The status file carries no
   version yet - that is a module need - so `ver` is empty today and the line says it the moment
   the module starts writing `version=`. An earlier build of ours (1.4.3) never will, so that one is
   named from the file in the folder instead. */
static void ffb_LampText(char *t,int state,const char *dev,const char *ver,int variant,int is143,
                         int modOn){
    char suf[64]; suf[0]=0;
    if(ver&&ver[0])              wsprintfA(suf," - module v%s",ver);
    else if(variant==FFBV_OLD)   SCpy(suf,is143?" - the 1.4.3 release module":" - an earlier build");
    if(state==1)      wsprintfA(t,"Driving effects on %s%s",dev,suf);
    else if(state==2) wsprintfA(t,"Game not running - last seen on %s%s",dev,suf);
    /* Alex, 2026-08-05, translated: *"the lamp is not quite clear - start the game once with the
       mod installed, or disable it first? What is expected of the user?"* The line states the
       ACTION, and a different one depending on whether the mod is on. */
    else if(state==3) SCpy(t, modOn
        ? "Not run here yet - leave it switched ON and start Mafia once, then this line "
          "says which wheel it took"
        : "Not run here yet - switch the mod on, then start Mafia once");
    else              wsprintfA(t,"No force is reaching the wheel - %s%s",dev,suf);
}

static void PageFfbPoll(void){
    char path[MAX_PATH]; ffb_StatusPath(path);
    int was=ffb_lampState, wasVariant=ffb_variant;
    const char *wasWarn;
    char wasDev[128]; SCpy(wasDev,ffb_devName);
    char wasVer[24];  SCpy(wasVer,ffb_modVer);

    wasWarn=ffb_ModelWarning();
    ffb_RefreshVariant();
    { char ini[MAX_PATH]; ffb_IniPath(ini);
      if(GetFileAttributesA(ini)!=INVALID_FILE_ATTRIBUTES) ffb_ReadModelKeys(ini); }

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
            char ini[MAX_PATH];
            synced=1;
            if(intent!=0){ ffb_SavToggle(1); ffb_RefreshIngame(); }
            /* AND PICK A WHEEL. As of 2026-08-12, switching the mod on should put the first
               device in the box by itself. Only when something is attached and nothing has been
               chosen: a device the user picked is never overridden. */
            if(FfbDevChosen(ffb_device)==FFBDEV_ANY && ffbdev_n>0){
                LogLine("no wheel was chosen - taking the first force-feedback device offered");
                ffb_SetDevice(0);
            }
            /* THE HOTKEYS OFF IN A FOLDER THAT PREDATES THIS VERSION. A player upgrading from
               1.4.3 has a settings file without a single hotkey line, and the module's defaults
               are the bench's keys - so the file is written once, now, rather than waiting for
               the first slider somebody may never touch. */
            ffb_IniPath(ini);
            if(!ffb_DevKeysKept(ini)&&!ffb_DevKeysOff(ini)){
                LogLine("switching the developers' in-game hotkeys off in mafia_ffb.ini - they "
                        "belong to the test bench, not to a player's game");
                PageDirty(LTAB_FFB);
            }
        }
    }

    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES){
        /* its own state, not "no wheel": the mod has simply never started here, and gluing that
           onto the red "No wheel driven" line read as a fault when nothing is wrong yet */
        ffb_lampState=3; ffb_devName[0]=0; ffb_modVer[0]=0;
    } else {
        DWORD gen=(DWORD)GetPrivateProfileIntA("status","generation",0,path);
        int eff=(int)GetPrivateProfileIntA("status","effective",0,path);
        GetPrivateProfileStringA("status","device","<none enumerated>",ffb_devName,
                                 sizeof(ffb_devName),path);
        GetPrivateProfileStringA("status","version","",ffb_modVer,sizeof(ffb_modVer),path);
        DWORD now=GetTickCount();
        if(gen!=ffb_lastGen){ ffb_lastGen=gen; ffb_lastGenSeen=now; }
        int live=(ffb_lastGenSeen!=0)&&((now-ffb_lastGenSeen)<20000u);
        if(!live)      ffb_lampState=2;   /* the file is old: the game is not running */
        else if(eff)   ffb_lampState=1;
        else           ffb_lampState=0;
    }
    if((ffb_lampState!=was||!SEq(wasDev,ffb_devName)||!SEq(wasVer,ffb_modVer)
        ||ffb_variant!=wasVariant)&&ffb_lampText){
        char t[300];
        ffb_LampText(t,ffb_lampState,ffb_devName,ffb_modVer,ffb_variant,ffb_variant143,
                     g_state[LTAB_FFB]==JMOD_ON);
        SetWindowTextA(ffb_lampText,t);
        if(ffb_lamp) InvalidateRect(ffb_lamp,NULL,TRUE);
        InvalidateRect(ffb_lampText,NULL,TRUE);
    }
    /* the model's line, said again the moment a reason appears or goes */
    if(ffb_ModelWarning()!=wasWarn){
        ffb_RefreshNotes();
        if(ffb_ModelWarning()) LogLine(ffb_ModelWarning());
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
        /* Two lines at most - the box is 32 px. The dropdown's face already says "the wheel you
           chose is not plugged in", so this line need not say it again; it was three lines and
           its last one fell off the box (the 2.0 screenshots, with the base switched off). */
        if(chosen==FFBDEV_GONE)
            wsprintfA(t,"%s is not plugged in - the mod will take the first device offered",
                      ffb_device);
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

/* The dropdown's FACE says what is chosen, in words, so it reads the same greyed as it does live -
   which is the other half of his complaint about the first version, translated: "the labels on
   the buttons are unclear in the off state". A row of identical grey slabs says nothing when
   disabled; one line of
   text says the same thing in either state. */
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
/* THE UNDO ARROW EXISTS ONLY WHILE THERE IS SOMETHING TO UNDO, and "something" is measured against
   the row's OWN reference. It was 100 in one of the three places that decided it and SREF in
   another, so the gunfire row - recommended 0 - showed an arrow at its recommended value and hid
   it at 100. One function now, used by every path. */
static void ffb_SyncArrow(int i){
    if(!ffb_reset[i]) return;
    ShowWindow(ffb_reset[i],(ffb_built&&ffb_RowShown(i)&&ffb_val[i]!=SREF[i])?SW_SHOW:SW_HIDE);
}
static void ffb_RefreshRow(int i){
    char t[16]; wsprintfA(t,"%d",ffb_val[i]);
    if(!ffb_box[i]) return;
    ffb_quiet=1; SetWindowTextA(ffb_box[i],t); ffb_quiet=0;
    /* GUARDED, and not defensively for its own sake: InvalidateRect(NULL,...) does not fail
       quietly - it invalidates EVERY window on the desktop. */
    if(ffb_hint[i]){
        SetWindowTextA(ffb_hint[i],ffb_HintFor(i,ffb_val[i]));
        InvalidateRect(ffb_hint[i],NULL,TRUE);
    }
    if(ffb_slider[i]) InvalidateRect(ffb_slider[i],NULL,TRUE);
    ffb_SyncArrow(i);
}

/* The build-up row: three selector buttons, the arrow, and one line for a curve that is none of
   the three. */
static void ffb_RefreshCurve(void){
    int c=ffb_CurveNow(),i;
    for(i=0;i<NCRV;i++) if(ffb_curveBtn[i]) InvalidateRect(ffb_curveBtn[i],NULL,TRUE);
    if(ffb_curveHint){
        SetWindowTextA(ffb_curveHint,c<0?FFB_CURVE_KEPT:"");
        InvalidateRect(ffb_curveHint,NULL,TRUE);
    }
    if(ffb_curveReset)
        ShowWindow(ffb_curveReset,(ffb_built&&c!=CRV_REF)?SW_SHOW:SW_HIDE);
}
static void ffb_SetCurve(int c){
    int k,was=ffb_CurveNow();
    char m[160];
    if(c<0||c>=NCRV) return;
    for(k=0;k<NSPD;k++){ ffb_spdKmh[k]=SPD_KMH[k]; ffb_spdPct[k]=CRV_PCT[c][k]; }
    if(was!=c){
        wsprintfA(m,"Weight build-up with speed: %s -> %s",was<0?"the file's own curve":CRV_NAME[was],
                  CRV_NAME[c]);
        LogLine(m);
        PageDirty(LTAB_FFB);
    }
    ffb_RefreshCurve();
}

/* The selection is the green on a button, and that is all. 1.4.3 also kept a line under the CHOSEN
   button - "everything was tuned here", "driven and confirmed", "calculated, never driven" - and on
   2026-10-01 he asked for the last two to go (as MEASURED and BY THE LAW) and for the page to be
   shorter, so what was true of 600 moved into the line beside the heading. */
static void ffb_RefreshDor(void){
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
/* One setter for every row. A road-model row writes its keys and then shows what those keys hold,
   so the slider can only ever rest on a value the file can carry. */
static void ffb_SetRow(int row,int v){
    int nv=Clamp(v,SMIN[row],SBOXMAX[row]);
    if(row>=NPLAIN){ ffb_RawFromRow(row,nv); nv=ffb_RowFromRaw(row); }
    if(ffb_val[row]!=nv){ ffb_logPend[row]=1; PageDirty(LTAB_FFB); }
    ffb_val[row]=nv;
    ffb_RefreshRow(row);
}

/* the callback block the shared slider class drives this page through. The slider works in
   0..travel; the breakaway does not start at zero, so its SMIN is taken off on the way in and put
   back on the way out. */
static int  ffb_SlGet(int row){ return ffb_val[row]-SMIN[row]; }
static void ffb_SlSet(int row,int v){ ffb_SetRow(row,v+SMIN[row]); }
static int  ffb_SlMax(int row){ return SMAX[row]-SMIN[row]; }
static int  ffb_SlStep(int row){ return SSTEP[row]; }
static int  ffb_SlRef(int row){ return SREF[row]-SMIN[row]; }
static int  ffb_SlAlt(int row){ return SALT[row]<0 ? -1 : SALT[row]-SMIN[row]; }
static int  ffb_SlMarks(int row){ return SMARKS[row]; }
/* The id base is a MACRO shared with the id block further down rather than a literal repeated in
   two places: a slider whose idBase disagrees with its control ids addresses the wrong rows and
   nothing says a word. */
#define FF_SLIDER_ID_BASE 5000
static const slider_ops FFB_SLIDER_OPS = {
    ffb_SlGet, ffb_SlSet, ffb_SlMax, ffb_SlStep, ffb_SlRef, ffb_SlAlt,
    FF_SLIDER_ID_BASE, ffb_SlMarks };

static void ffb_Recommended(void){
    int i;
    for(i=0;i<NSLIDER;i++) if(ffb_val[i]!=SREF[i]) ffb_logPend[i]=1;
    ffb_ResetValues();
    for(i=0;i<NSLIDER;i++) ffb_RefreshRow(i);
    ffb_RefreshCurve();
    PageDirty(LTAB_FFB);
    LogLine("back to the reference settings - every slider on its tall mark (gunfire 0) and "
            "build-up on Reference; the range, the wheel and the presets stay");
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
    ffb_LoadValues(p);
    ffb_RefreshAll();
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
    /* OPEN WHERE THE MOD LIVES, not in Documents. Alex, 2026-08-03, translated: *"I would open the
       Export Preset path in the game folder... so the user understands where the mod is and where
       their presets can be, so it all sits in one place."* The buffer must outlive the call, hence
       static: OPENFILENAME keeps the pointer. */
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
        ffb_LoadValues(buf);
        ffb_RefreshAll();
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
/* 5350 was the line under the chosen range button, removed 2026-10-01 - left unused */
#define FF_ID_RECOMM  5360
#define FF_ID_LAMP    5361
#define FF_ID_LAMPTXT 5362
#define FF_ID_SLOT0   5370
#define FF_ID_SAVESLOT 5380
#define FF_ID_LOADF   5381
#define FF_ID_SAVEF   5382
#define FF_ID_SECT0   5390
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
   means the recommended values are in the player's profile right now. */
#define FF_ID_INGAME  5424
/* The red line saying why the road model is not driving the wheel. 5430..5433 were the model
   selector's, removed 2026-09-30, and 5435 the model's name, removed 2026-10-01; all are left unused
   so an old id cannot come back meaning something else. */
#define FF_ID_MODETXT 5434
/* 2026-09-30: the grouped layout. 5440..5599 is this page's; the camera page starts at 5600, and
   the canvas routes a command by id alone, so a collision would press a button on the wrong tab. */
#define FF_ID_LABEL0  5440      /* ..5451, one per row */
#define FF_ID_UNIT0   5460      /* ..5471 */
/* ..5486, the words the layout places (FFB_TEXT). Were the channel headings 2026-09-30..10-01. */
#define FF_ID_TXT0    5480
#define FF_ID_CURVE0  5490      /* ..5492, Light / Reference / Heavy */
#define FF_ID_CURVELBL 5493
#define FF_ID_CURVEHINT 5494
#define FF_ID_CURVERST 5495
/* 5500 was the sub-line under the model's heading, removed 2026-10-01 - left unused */
#define FF_ID_NOTEOLD 5501
#define FF_ID_RECOMMNOTE 5502
/* 5503 was the line under 600 for one build on 2026-10-01 (5350 before that) - left unused */

/* The list. TPM_RETURNCMD, so the choice comes back here instead of arriving later as a WM_COMMAND
   the page would have to tell apart from a button press. */
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

/* A row's controls are made once, at 0,0, and ffb_Layout puts them where they belong - in the
   column's own boxes (GEO_L / GEO_R). */
static void ffb_MkRow(HWND h,int i){
    const ffb_geo *g=ffb_GeoOf(i);
    ffb_label[i]=MkTextF(h,SNAME[i],0,0,g->lblW,18,FF_ID_LABEL0+i,g_fBody,0);
    /* the per-row undo loop, left of the slider, shown only when the row has been moved off its
       reference - at the reference there is nothing to undo and a permanent row of arrows is
       noise */
    ffb_reset[i]=MkBtn(h,"",0,0,20,20,FF_ID_RESET0+i);
    ShowWindow(ffb_reset[i],SW_HIDE);
    ffb_slider[i]=MkSliderOps(h,0,0,g->slW,26,FF_ID_SLIDER0+i,&FFB_SLIDER_OPS);
    /* QUIET, because an EDIT created with text reports EN_CHANGE while it is being created - and
       the box handler took that for the user typing "100" over a page that had loaded nothing
       yet, marked the page changed, and the first flush wrote mafia_ffb.ini and a preset into
       whatever folder the window was opened on, force feedback installed or not. Traced on
       2026-10-01 with a side build logging every PageDirty: twelve, one per box, all from here. */
    ffb_quiet=1;
    ffb_box[i]=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","100",
        WS_CHILD|WS_VISIBLE|ES_RIGHT|ES_AUTOHSCROLL,0,0,56,22,h,
        (HMENU)(INT_PTR)(FF_ID_BOX0+i),NULL,NULL);
    ffb_quiet=0;
    SendMessageA(ffb_box[i],WM_SETFONT,(WPARAM)g_fField,TRUE);
    ffb_unit[i]=MkTextF(h,SUNIT[i],0,0,g->unW,18,FF_ID_UNIT0+i,g_fField,0);
    ffb_hint[i]=MkTextF(h,SHINT[i],0,0,g->hnW,18,FF_ID_HINT0+i,g_fSmall,0);
}
static void ffb_Move(HWND w,int x,int y,int cx,int cy){
    if(w) SetWindowPos(w,NULL,x,y,cx,cy,SWP_NOZORDER|SWP_NOACTIVATE);
}
/* 1.4.3's row, offset for offset: the label 4 px down, the undo loop 26 px left of the slider, the
   box 1 px down, the unit and the hint 5 px down. */
static void ffb_PlaceRow(int i,const ffb_geo *g,int y){
    ffb_Move(ffb_label[i], g->x,       y+4, g->lblW, 18);
    ffb_Move(ffb_reset[i], g->slX-26,  y+3, 20,      20);
    ffb_Move(ffb_slider[i],g->slX,     y,   g->slW,  26);
    ffb_Move(ffb_box[i],   g->bxX,     y+1, 56,      22);
    ffb_Move(ffb_unit[i],  g->unX,     y+5, g->unW,  18);
    ffb_Move(ffb_hint[i],  g->hnX,     y+5, g->hnW,  18);
    ffb_SyncArrow(i);
}
static void ffb_PlaceCurve(const ffb_geo *g,int y){
    int c;
    ffb_Move(ffb_curveLbl,  g->x,           y+4, g->lblW,    18);
    ffb_Move(ffb_curveReset,g->slX-26,      y+3, 20,         20);
    for(c=0;c<NCRV;c++)
        ffb_Move(ffb_curveBtn[c],g->slX+c*(CRV_W+CRV_GAP),y+1,CRV_W,24);
    ffb_Move(ffb_curveHint, g->x+CRV_HINT_X, y+5, CRV_HINT_W, 18);
}

/* THE LAYOUT, from the tables above. Everything from the Overall row down is placed here in one
   pass, so the page's height is measured rather than added up in two places. The vertical steps
   are 1.4.3's: a heading 24 px on the left and 26 on the right, the line under it 22, a small
   heading 20, a row 27, the seam 4 + 10. */
static void ffb_Layout(void){
    int yc[2],c,i,y,colTop;
    const char *warn;
    char note[400];
    HWND page;
    if(!ffb_built) return;
    page=GetParent(ffb_text[T_HEAD_L]);
    y=ffb_layTop;
    /* WHY THE ROAD MODEL IS NOT DRIVING THE WHEEL - red, straight above the sliders it makes
       meaningless, and given room ONLY while it is true: a line that is empty nearly always must
       not cost the page its height. Two lines of room, so a warning is never trimmed to fit. */
    warn=ffb_ModelWarning();
    ffb_laidWarn=(warn!=NULL);
    if(warn){ ffb_Move(ffb_modeText,COL_LABEL,y,W_FULL,34); y+=40; }
    if(ffb_modeText) ShowWindow(ffb_modeText,warn?SW_SHOW:SW_HIDE);
    /* OVERALL STRENGTH, full width, above both columns it scales (LAY_TOP) */
    for(i=0;LAY_TOP[i].kind!=LI_END;i++)
        if(LAY_TOP[i].kind==LI_ROW){ ffb_PlaceRow(LAY_TOP[i].arg,&GEO_TOP,y); y+=R_H; }
    y+=8;
    colTop=y;
    for(c=0;c<2;c++){
        const ffb_lay *it=ffb_Lay(c);
        const ffb_geo *g=c?&GEO_R:&GEO_L;
        int w=c?W_RIGHT:W_LEFT;
        y=colTop;
        for(;it->kind!=LI_END;it++) switch(it->kind){
        case LI_HEAD:    ffb_Move(ffb_text[it->arg],g->x,y,w,22); y+=c?26:24; break;
        case LI_LINE:    ffb_Move(ffb_text[it->arg],g->x,y,w,18); y+=22; break;
        case LI_SUB:     ffb_Move(ffb_text[it->arg],g->x,y,w,18); y+=20; break;
        case LI_ROW:     ffb_PlaceRow(it->arg,g,y); y+=R_H; break;
        case LI_CURVE:   ffb_PlaceCurve(g,y); y+=R_H; break;
        /* from the row label's edge, right under the gunfire row */
        case LI_GUNNOTE: ffb_Move(ffb_noteGun,g->x,y,w,16); y+=18; break;
        case LI_SEAM:    y+=4; ffb_Move(ffb_seam,g->x,y,WW_TABLE_W,2); y+=10; break;
        }
        yc[c]=y;
    }
    ffb_RefreshCurve();
    y=yc[0]>yc[1]?yc[0]:yc[1];
    /* a hairline between the columns, as tall as the two things it separates - 1.4.3's, 14 px left
       of the right column */
    ffb_Move(ffb_colDiv,COL_MID-14,colTop,2,y-colTop);
    y+=10;
    /* the earlier version's values the road model still applies - a line only when there are any */
    ffb_OldNote(note,sizeof note);
    ffb_laidOld=(note[0]!=0);
    if(note[0]){ ffb_Move(ffb_noteOld,COL_LABEL,y,W_FULL,16); y+=22; }
    if(ffb_noteOld) ShowWindow(ffb_noteOld,note[0]?SW_SHOW:SW_HIDE);
    /* ---- BACK TO DEFAULT SETTINGS - full width, centred, UNDER both columns and ABOVE the
       presets. Two of his instructions, 2026-08-05: it sets EVERY slider on the page back, so it
       cannot live inside one column; and, translated, *"let's put it not at the top but at the
       bottom, under Force Feedback and Wheel Weight, above the presets"*. */
    { int bw=220, bx=(WINW-bw)/2;
      ffb_Move(ffb_recommBtn,bx,y,bw,28);
      ffb_Move(ffb_recommNote,COL_LABEL,y+32,W_FULL,16);
      y+=56; }
    ffb_Move(ffb_presetDiv,COL_LABEL,y,W_FULL,2);
    y+=12;
    /* the presets under both columns, because a preset carries every value on the page, and
       sitting inside one column claimed it carried half of them */
    ffb_Move(ffb_presetLbl,COL_LABEL,y+6,W_FULL-560,20);
    for(i=0;i<3;i++) ffb_Move(ffb_slot[i],COL_LABEL+W_FULL-540+i*54,y,48,28);
    ffb_Move(ffb_impBtn,COL_LABEL+W_FULL-350,y,168,28);
    ffb_Move(ffb_expBtn,COL_LABEL+W_FULL-172,y,172,28);
    y+=34;
    ffb_pageH=y+8;
    if(g_page[LTAB_FFB]==page){ g_contentH[LTAB_FFB]=ffb_pageH; SyncScroll(LTAB_FFB,1); }
    if(page) InvalidateRect(page,NULL,TRUE);
}

static void PageFfbCreate(HWND h){
    int y,i;
    PageHeader(h,LTAB_FFB);
    y=PageBanner(h,LTAB_FFB,PAGE_TOP);

    ffb_lamp=MkBtn(h,"",COL_LABEL,y,22,22,FF_ID_LAMP);
    EnableWindow(ffb_lamp,FALSE);
    ffb_lampText=MkText(h,"waiting for the mod...",COL_LABEL+28,y+3,W_FULL-28,18,FF_ID_LAMPTXT);
    y+=30;

    /* No row naming the model here any more - he took it off on 2026-10-01 (top of this file).
       Why the model might NOT be driving the wheel is the red line ffb_Layout places above the
       sliders, when there is a reason. */

    /* ---- the game's own settings, above the wheel row ----
       Its own line because it is not about the wheel: it changes what the GAME does, and it is
       the first thing that has to be true before any force on this page means what it says. */
    ffb_ingame=MkBtn(h,"",COL_LABEL,y,330,26,FF_ID_INGAME);
    MkText(h,"the game's own handling and force feedback, as this mod was tuned",
           COL_LABEL+340,y+5,W_FULL-340,18,0);
    y+=32;

    FfbDevEnum();
    /* ================= WHICH WHEEL, IN ONE LINE =================
     * Above the range on his instruction ("put it above the angles"): which device, THEN how many
     * degrees that device is set to. ONE LINE, and that is his correction of the first attempt,
     * translated: *"The wheel choice must be literally one line."*
     * 88, not 64: at 64 the section face drew "WHEE". Bounds are written down rather than
     * eyeballed: drop 96..516   refresh 526..676   test 686..886   held 896..1268. */
    /* 22 tall, not 20: the section face is 21 px, so a 20 px box cut its last row - invisible on
       capitals, and exactly what STClipped exists to say out loud */
    MkTextF(h,"WHEEL",COL_LABEL,y+6,88,22,FF_ID_SECT0+4,g_fSection,0);
    ffb_devDrop=MkBtn(h,"",COL_LABEL+96,y,420,30,FF_ID_DEVDROP);
    ffb_devRefresh=MkBtn(h,"Refresh list",COL_LABEL+526,y,150,30,FF_ID_DEVREFRESH);
    ffb_devTest=MkBtn(h,"Test - push the wheel",COL_LABEL+686,y,200,30,FF_ID_DEVTEST);
    /* What the MOD says it took, from mafia_ffb_status.ini - a different question from what is
       chosen here, and the point of the row is the case where the two differ. */
    ffb_devHeld=MkTextF(h,"",COL_LABEL+896,y+7,W_FULL-896,32,FF_ID_DEVHELD,g_fSmall,0);
    y+=36;
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    COL_LABEL,y,W_FULL,2,h,NULL,NULL,NULL);
    y+=12;

    /* ================= ABOVE BOTH COLUMNS: the range =================
     * Alex, 2026-08-01, translated: "the choice of angles goes at the top, above everything. Only
     * then the two columns". Every other number here is a function of it.
     * The header and its sentence share a line - his, on the page growing back the first time:
     * "squeeze it vertically anyway, we squeezed and squeezed and it stretched out again". */
    MkTextF(h,"WHEEL ROTATION RANGE",COL_LABEL,y+3,270,22,FF_ID_SECT0,g_fSection,0);
    MkText(h,FFB_DOR_TEXT,COL_LABEL+280,y+5,W_FULL-280,18,0);
    y+=26;
    {   /* 600 keeps a wider face and the larger figure: it is the range every constant in this
           mod was chosen on, and bold-with-no-colour is this skin's word for "recommended". The
           line beside the heading says so in words (FFB_DOR_TEXT). */
        int gap=10,x=COL_LABEL;
        for(i=0;i<NDOR;i++){
            int wB=(DORDEG[i]==DOR_DEFAULT)?170:140;
            char t[16]; wsprintfA(t,"%d",DORDEG[i]);
            ffb_dorBtn[i]=MkBtn(h,t,x,y,wB,DOR_BTN_H,FF_ID_DOR0+i);
            x+=wB+gap;
        }
    }
    y+=DOR_BTN_H+6;
    CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                    COL_LABEL,y,W_FULL,2,h,NULL,NULL,NULL);
    y+=12;

    /* ================= THE TWO COLUMNS, laid out by ffb_Layout from LAY_L / LAY_R ================= */
    ffb_layTop=y;
    /* the red line - empty and hidden until ffb_Layout has a reason to give it room */
    ffb_modeText=MkTextF(h,"",COL_LABEL,y,W_FULL,34,FF_ID_MODETXT,g_fSmall,0);
    ShowWindow(ffb_modeText,SW_HIDE);
    /* The column headings are CENTRED over their own columns, in the section face. Alex,
       2026-08-03, translated: "centre them over their own categories, so it is clear at once where
       a category starts and ends. Left-aligned is wrong." The words under a heading and the two
       small headings of the wheel-weight table are 1.4.3's faces too: body, and the field face
       its Cars and Trucks were set in. */
    for(i=0;i<NTEXT;i++){
        HFONT f = (i==T_HEAD_L||i==T_HEAD_R) ? g_fSection
                : (i==T_LINE_L)               ? g_fBody : g_fField;
        int id = (i==T_HEAD_L) ? FF_ID_SECT0+1 : (i==T_HEAD_R) ? FF_ID_SECT0+2 : FF_ID_TXT0+i;
        ffb_text[i]=MkTextF(h,FFB_TEXT[i],0,0,10,18,id,f,
                            (i==T_HEAD_L||i==T_HEAD_R)?SS_CENTER:0);
    }
    for(i=0;i<NSLIDER;i++) ffb_MkRow(h,i);
    /* the build-up row: a label, three selector buttons and one line of text */
    ffb_curveLbl=MkTextF(h,"Build-up with speed",0,0,WW_LBL_W,18,FF_ID_CURVELBL,g_fBody,0);
    ffb_curveReset=MkBtn(h,"",0,0,20,20,FF_ID_CURVERST);
    ShowWindow(ffb_curveReset,SW_HIDE);
    for(i=0;i<NCRV;i++) ffb_curveBtn[i]=MkBtn(h,CRV_NAME[i],0,0,CRV_W,24,FF_ID_CURVE0+i);
    ffb_curveHint=MkTextF(h,"",0,0,CRV_HINT_W,18,FF_ID_CURVEHINT,g_fSmall,0);
    /* the seam between the table's two halves, and the hairline between the columns */
    ffb_seam=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                             0,0,WW_TABLE_W,2,h,NULL,NULL,NULL);
    ffb_colDiv=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDVERT,
                               0,0,2,10,h,NULL,NULL,NULL);
    ffb_noteGun=MkTextF(h,FFB_NOTE_GUN,0,0,W_LEFT,16,FF_ID_NOTE,g_fSmall,0);
    ffb_noteOld=MkTextF(h,"",0,0,W_FULL,16,FF_ID_NOTEOLD,g_fSmall,0);
    ffb_recommBtn=MkBtn(h,"Back to default settings",0,0,220,28,FF_ID_RECOMM);
    ffb_recommNote=MkTextF(h,FFB_RECOMM_NOTE,0,0,W_FULL,16,FF_ID_RECOMMNOTE,g_fSmall,SS_CENTER);
    ffb_presetDiv=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
                                  0,0,W_FULL,2,h,NULL,NULL,NULL);
    ffb_presetLbl=MkTextF(h,FFB_PRESET_LBL,0,0,W_FULL-560,20,FF_ID_SECT0+3,g_fSub,0);
    for(i=0;i<3;i++){
        char t[16]; wsprintfA(t,"%d",i+1);
        ffb_slot[i]=MkBtn(h,t,0,0,48,28,FF_ID_SLOT0+i);
    }
    ffb_impBtn=MkBtn(h,"Import preset...",0,0,168,28,FF_ID_LOADF);
    ffb_expBtn=MkBtn(h,"Export preset...",0,0,172,28,FF_ID_SAVEF);
    ffb_built=1;
    ffb_Layout();

    PageSaveRow(h,LTAB_FFB,ffb_pageH-8,FfbSaveIni,FfbLoadIni);

    /* PAINT THE FACES NOW THAT THE CONTROLS EXIST. FfbLoadIni ends with ffb_RefreshDev(), but a
       load that ran before this point found ffb_devDrop NULL and painted nothing - and the wheel
       row sat BLANK until something else happened to call it. Alex, 2026-08-12. */
    ffb_RefreshDev();
    ffb_RefreshIngame();
}

/* The undo loop: most of a circle, coming back on itself, with the head at the opening. The
   first version was a plain back-arrow, which reads as "previous", not as "undo". */
static void ffb_DrawUndo(DRAWITEMSTRUCT *d,int pressed,int off){
    RECT r=d->rcItem;
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
}
/* the little arrow, so a line reads as something that opens */
static void ffb_DrawDropArrow(HDC dc,const RECT *r){
    int cx=r->right-16, cy=(r->top+r->bottom)/2;
    POINT tri[3]={{cx-5,cy-2},{cx+5,cy-2},{cx,cy+4}};
    HBRUSH b=CreateSolidBrush(INK_SOFT); HGDIOBJ ob=SelectObject(dc,b);
    HPEN pen=CreatePen(PS_SOLID,1,INK_SOFT); HGDIOBJ op=SelectObject(dc,pen);
    Polygon(dc,tri,3);
    SelectObject(dc,op); DeleteObject(pen);
    SelectObject(dc,ob); DeleteObject(b);
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
           GREEN = in force now, BOLD = the recommended one, and never red. 1.4.3's drawing again
           since 2026-10-01: one line, the degrees and nothing else. */
        int deg=DORDEG[id-FF_ID_DOR0];
        int chosen=(deg==ffb_range), isRef=(deg==DOR_DEFAULT);
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,isRef?g_fRange:(chosen?g_fAction:g_fField));
        DrawPressable(d->hDC,&r,txt,pressed,off,isRef,chosen&&!off);
        SelectObject(d->hDC,save);
        return 1;
    }
    if((id>=FF_ID_RESET0&&id<FF_ID_RESET0+NSLIDER)||id==FF_ID_CURVERST){
        ffb_DrawUndo(d,pressed,off);
        return 1;
    }
    if(id==FF_ID_LAMP){
        /* a state, never clicked: green means the mod told us it put a force on the wheel, amber
           that the game is not running, red that it is running and the wheel is not being driven
           - which is the one place red belongs, because it IS a fault */
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
    if(id>=FF_ID_CURVE0&&id<FF_ID_CURVE0+NCRV){
        /* the build-up curves are a SELECTOR too: the one in the file is green, the reference is
           drawn heavier, as the range's 600 is */
        int c=id-FF_ID_CURVE0;
        PaperBlit(d->hDC,d->hwndItem,&r);
        HGDIOBJ save=SelectObject(d->hDC,g_fField);
        DrawPressable(d->hDC,&r,txt,pressed,off,c==CRV_REF,(ffb_CurveNow()==c)&&!off);
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
        if(!off) ffb_DrawDropArrow(d->hDC,&r);
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
        /* the LAST argument is the green one - `primary` is only an emphasised face. Same shape
           as the range detents: chosen && !off, so a greyed page greys it too. */
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
       hit on 2026-08-12 and what this button removes. It reports the COUNT out loud in both
       directions: "0 -> 0" is the answer to "I pressed it and nothing happened". */
    if(id==FF_ID_DEVREFRESH){
        char m[200];
        int before=ffbdev_n,i;
        FfbDevEnum();
        wsprintfA(m,"refresh: %d force-feedback device(s) before, %d now",before,ffbdev_n);
        LogLine(m);
        for(i=0;i<ffbdev_n;i++){ wsprintfA(m,"  [%d] %s",i,ffbdev_name[i]); LogLine(m); }
        if(!ffbdev_n)
            LogLine("  none. Switch the wheel on, wait for Windows to finish with it, press it again");
        ffb_RefreshDev();
        return 1;
    }
    if(id==FF_ID_DEVTEST){
        char why[200],m[400];
        int idx=FfbDevChosen(ffb_device);
        /* WITH NOTHING CHOSEN, test the one the mod would take - the first offered. Same for a
           chosen device that is not attached, and there the log says WHICH question is answered. */
        if(idx==FFBDEV_GONE)
            LogLine("the wheel you chose is not attached - testing the device the mod would take");
        if(idx<0) idx=(ffbdev_n>0)?0:-1;
        if(idx<0){ LogLine("no force-feedback device is attached - nothing to test"); return 1; }
        /* The mod holds the wheel EXCLUSIVE while the game runs. Two exclusive owners is a fight,
           and its loser is whoever asked second - so refuse and say why. */
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
    /* back to the row's OWN reference - 0 for gunfire, 9 degrees for the breakaway - never a
       literal 100 */
    if(id>=FF_ID_RESET0&&id<FF_ID_RESET0+NSLIDER){
        int row=id-FF_ID_RESET0;
        ffb_SetRow(row,SREF[row]);
        return 1;
    }
    if(id>=FF_ID_CURVE0&&id<FF_ID_CURVE0+NCRV){ ffb_SetCurve(id-FF_ID_CURVE0); return 1; }
    if(id==FF_ID_CURVERST){ ffb_SetCurve(CRV_REF); return 1; }
    if(id>=FF_ID_SLOT0&&id<FF_ID_SLOT0+3){ ffb_SelectSlot(id-FF_ID_SLOT0); return 1; }
    if(id==FF_ID_RECOMM){ ffb_Recommended(); return 1; }
    if(id==FF_ID_LOADF){ ffb_FileDialog(0); return 1; }
    if(id==FF_ID_SAVEF){ ffb_FileDialog(1); return 1; }
    /* Leaving a box puts the value the file will hold back into it - a typed 999 on a row that
       stops at 400, or an odd driving damper that the key rounds up. */
    if(id>=FF_ID_BOX0&&id<FF_ID_BOX0+NSLIDER&&code==EN_KILLFOCUS){
        ffb_RefreshRow(id-FF_ID_BOX0);
        return 1;
    }
    if(id>=FF_ID_BOX0&&id<FF_ID_BOX0+NSLIDER&&code==EN_CHANGE&&!ffb_quiet){
        char t[32]; int v;
        GetWindowTextA(ctl,t,sizeof(t));
        /* an empty box is somebody mid-edit, not a zero */
        if(t[0]&&ParseInt(t,&v)){
            int row=id-FF_ID_BOX0;
            int nv=Clamp(v,SMIN[row],SBOXMAX[row]);
            if(row>=NPLAIN){ ffb_RawFromRow(row,nv); nv=ffb_RowFromRaw(row); }
            if(ffb_val[row]!=nv){
                ffb_val[row]=nv;
                ffb_logPend[row]=1;
                PageDirty(LTAB_FFB);
                /* NOT ffb_RefreshRow: it rewrites the box, which would fight the typing. Only
                   the things that render the value elsewhere. */
                if(ffb_hint[row]){ SetWindowTextA(ffb_hint[row],ffb_HintFor(row,nv));
                                   InvalidateRect(ffb_hint[row],NULL,TRUE); }
                if(ffb_slider[row]) InvalidateRect(ffb_slider[row],NULL,TRUE);
                ffb_SyncArrow(row);
            }
        }
        return 1;
    }
    return 0;
}

/* Read the settings this install already has, and start the status poll. */
static void PageFfbStart(void){
    char p[MAX_PATH];
    /* the recommended set first, so a folder with no file shows the reference - gunfire 0 - and
       not whatever a fresh process happened to hold */
    ffb_ResetValues();
    ffb_RefreshVariant();
    ffb_IniPath(p);
    ffb_LoadFrom(p);
    for(int i=0;i<NSLIDER;i++){ ffb_RefreshRow(i); ffb_logWas[i]=ffb_val[i]; ffb_logPend[i]=0; }
    ffb_RefreshDor();
    ffb_RefreshSlots();
    ffb_RefreshAll();
    /* What is on screen came straight out of the file, so the page is NOT dirty. Without this
       the tab opened saying "not saved yet" about settings nobody had touched. */
    PageSaved(LTAB_FFB);
    PageFfbPoll();
}

#endif /* ALXG_PAGE_FFB_H */
