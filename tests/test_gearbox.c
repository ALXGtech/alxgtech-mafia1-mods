/* test_gearbox.c - the shifter, tested without the game, the wheel or a human.
 *
 * Alex, 2026-07-24: "мне надоело каждый раз включать автоматическую и убеждаться, что она не
 * работает. Ты прекрасно всё это можешь эмулировать." So this drives the real decision core
 * (src/spike/gearbox_logic.h - the same header the .asi compiles) against a model of Mafia's
 * gearbox built from what the six recorded drives actually showed:
 *
 *   - the gear steps one at a time, R <-> N <-> 1 <-> 2 <-> 3 <-> 4;
 *   - a DOWN-shift is REFUSED while the car is going too fast for the lower gear;
 *   - in automatic the gear keys do nothing at all;
 *   - in automatic the car only pulls away IF a gear is already engaged - from neutral it just
 *     sits there, which is exactly what drive 6 recorded and what Alex kept hitting;
 *   - the mode key toggles automatic/manual.
 *
 * Time is simulated, so a 90-second drive runs in microseconds.
 *
 * Build: i686-w64-mingw32-clang -O2 -m32 -o build/test-gearbox.exe tests/test_gearbox.c
 */
#include <stdio.h>
#include "../src/spike/gearbox_logic.h"

/* ----------------------------------------------------------------- the emulated game ----- */
typedef struct {
    int gear;          /* -1 R, 0 N, 1..maxGear */
    int mode;          /* 0 manual, 1 automatic */
    int maxGear;
    double speed;      /* m/s, magnitude */
    int throttle;      /* the driver's foot - off means the car slows down */
    int refusedDowns;  /* how often it said no */
} Game;

static void GameInit(Game *g,int maxGear){
    g->gear=0; g->mode=0; g->maxGear=maxGear; g->speed=0.0; g->refusedDowns=0; g->throttle=1;
}
/* the speed above which the game will not accept the gear below `gear` */
static double DownLimit(int gear){
    switch(gear){ case 2: return 9.0; case 3: return 17.0; case 4: return 26.0; default: return 1e9; }
}
static double UpThreshold(int gear){
    switch(gear){ case 1: return 9.0; case 2: return 18.0; case 3: return 26.0; default: return 1e9; }
}
static void GameKey(Game *g,GBAct a){
    if(a==GB_MODE_KEY){ g->mode=!g->mode; return; }
    if(g->mode==1) return;                       /* automatic ignores the gear keys */
    if(a==GB_UP){   if(g->gear<g->maxGear) g->gear++; }
    else if(a==GB_DOWN){
        if(g->gear<=-1) return;
        if(g->gear>=2&&g->speed>DownLimit(g->gear)){ g->refusedDowns++; return; }
        g->gear--;
    }
}
/* one 8 ms tick of physics, throttle assumed down */
static void GameTick(Game *g){
    if(g->mode==1){
        if(g->gear>=1){
            g->speed+=g->throttle?0.06:-0.20; if(g->speed<0) g->speed=0;
            if(g->gear<g->maxGear&&g->speed>UpThreshold(g->gear)) g->gear++;
        } else {
            g->speed=0.0;                        /* automatic will not pull away from N or R */
        }
    } else {
        if((g->gear>=1||g->gear==-1)&&g->throttle) g->speed+=0.06;
        else if(g->speed>0.3) g->speed-=0.3;
        else g->speed=0.0;
    }
    if(g->speed>60.0) g->speed=60.0;
}

/* ----------------------------------------------------------------- the rig -------------- */
typedef struct {
    GBCfg cfg; GBState st; Game game;
    gb_bits bits[GB_MAXDEV];
    gb_ms now;
    int neutralDips;      /* gear reached N while the driver was moving between gates */
    int lastGear;
    int emits;
} Rig;

static void CfgDefault(GBCfg *c,int modeHold){
    for(int i=0;i<GB_NPOS;i++){ c->posDev[i]=0; c->posBtn[i]=-1; }
    /* Alex's real ODDOR-GEAR layout: gears on buttons 0..5, reverse 7, neutral = rest */
    static const int gears[6]={0,1,2,3,4,5};
    c->posBtn[0]=7;  c->posGear[0]=-1;          /* reverse */
    c->posBtn[1]=-1; c->posGear[1]=0;           /* neutral is the rest position */
    for(int i=0;i<6;i++){ c->posBtn[2+i]=gears[i]; c->posGear[2+i]=i+1; }
    c->modeDev=0; c->modeBtn=6; c->modeHold=modeHold;
    c->dikUp=0x1E; c->dikDown=0x2C; c->dikMode=0x30;
    c->neutralDelayMs=1100; c->retryMs=1200; c->closedLoop=1;
}
static void RigInit(Rig *r,int modeHold,int maxGear,int startGear){
    CfgDefault(&r->cfg,modeHold); GBInit(&r->st); GameInit(&r->game,maxGear);
    r->game.gear=startGear; r->lastGear=startGear;
    for(int i=0;i<GB_MAXDEV;i++) r->bits[i]=0;
    r->now=1000; r->neutralDips=0; r->emits=0;
}
static void Press(Rig *r,int btn){ r->bits[0]|=(1ull<<btn); }
static void Release(Rig *r,int btn){ r->bits[0]&=~(1ull<<btn); }
static void ReleaseAll(Rig *r){ r->bits[0]=0; }

/* advance simulated time, stepping the logic every 8 ms exactly as the .asi thread does */
static void Run(Rig *r,int ms){
    for(int t=0;t<ms;t+=8){
        GameTick(&r->game);
        GBIn in; for(int i=0;i<GB_MAXDEV;i++) in.bits[i]=r->bits[i];
        in.gear=r->game.gear; in.mode=r->game.mode; in.gearValid=1; in.now=r->now;
        GBOut o=GBStep(&r->cfg,&r->st,&in);
        if(o.act!=GB_NONE){
            int before=r->game.gear;
            GameKey(&r->game,o.act);
            r->emits++;
            if(o.act==GB_UP||o.act==GB_DOWN){
                /* the .asi holds the key ~40 ms then waits up to 300 ms for the gear to move */
                int moved=(r->game.gear!=before);
                GBAfterShift(&r->cfg,&r->st,o.act==GB_UP?1:-1,before,moved,r->now);
                r->now+=100;
            } else r->now+=100;
        }
        if(r->game.gear==0&&r->lastGear!=0) r->neutralDips++;
        r->lastGear=r->game.gear;
        r->now+=8;
    }
}

/* ----------------------------------------------------------------- assertions ----------- */
static int g_fail=0, g_run=0;
static void OK(const char *name,int cond,const char *detail){
    g_run++;
    if(cond) printf("  PASS  %s\n",name);
    else { g_fail++; printf("  FAIL  %s\n          %s\n",name,detail); }
}
static char g_buf[256];
static const char *Say(const char *fmt,int a,int b,double c){
    sprintf(g_buf,fmt,a,b,c); return g_buf;
}

/* ----------------------------------------------------------------- the scenarios -------- */
static void T_upshift_sequence(void){
    Rig r; RigInit(&r,0,4,1);
    r.game.mode=0;
    /* 1 -> 2 -> 3 -> 4, each gate change with a 250 ms transit, which is his measured speed */
    int gates[3]={1,2,3};                          /* buttons for gears 2,3,4 */
    Press(&r,0); Run(&r,400);
    for(int i=0;i<3;i++){
        ReleaseAll(&r); Run(&r,250);
        Press(&r,gates[i]); Run(&r,600);
    }
    OK("1->2->3->4 with 250 ms gate transits reaches 4th",r.game.gear==4,
       Say("gear ended at %d (expected 4), neutral dips %d",r.game.gear,r.neutralDips,0));
    OK("no parasitic neutral while moving between gates",r.neutralDips==0,
       Say("gear dropped to neutral %d time(s) - the transit was taken literally",r.neutralDips,0,0));
}

static void T_deliberate_neutral(void){
    Rig r; RigInit(&r,0,4,1);
    Press(&r,1); Run(&r,400);                      /* in 2nd */
    r.game.throttle=0;                             /* lift off, as anyone does before neutral */
    ReleaseAll(&r); Run(&r,3000);                  /* lever parked at rest */
    OK("a rest position held 2 s does engage neutral",r.game.gear==0,
       Say("gear %d (expected neutral)",r.game.gear,0,0));
}

static void T_leaving_reverse_is_instant(void){
    Rig r; RigInit(&r,0,4,0);
    Press(&r,7); Run(&r,500);
    OK("reverse engages",r.game.gear==-1,Say("gear %d",r.game.gear,0,0));
    ReleaseAll(&r); Run(&r,200);                   /* well under the 1100 ms debounce */
    OK("leaving reverse gives neutral immediately",r.game.gear==0,
       Say("gear %d after 200 ms at rest (expected neutral - reverse is exempt)",r.game.gear,0,0));
}

static void T_refused_downshift_retries(void){
    Rig r; RigInit(&r,0,4,1);
    Press(&r,3); Run(&r,6000);                     /* 4th, and let it build speed */
    OK("reached 4th and is moving fast",r.game.gear==4&&r.game.speed>26.0,
       Say("gear %d speed %.1f",r.game.gear,0,r.game.speed));
    ReleaseAll(&r); Press(&r,0);                   /* ask for 1st at speed - will be refused */
    Run(&r,1500);
    int refusedSoFar=r.game.refusedDowns;
    OK("the game refuses a downshift at speed",refusedSoFar>0,"no refusal was produced by the model");
    OK("a refusal does not become permanent - the mod keeps the request",r.st.lastTarget==1,
       Say("target is %d (expected 1)",r.st.lastTarget,0,0));
}

static void T_hold_switch_keeps_automatic(void){
    Rig r; RigInit(&r,1,4,1);                      /* modeHold: a latching switch */
    Press(&r,0); Run(&r,3000);                     /* driving in 1st, manual */
    Press(&r,6); Run(&r,8000);                     /* switch engaged and LEFT engaged */
    OK("engaging the switch puts the game in automatic",r.game.mode==1,
       Say("mode %d (expected automatic)",r.game.mode,0,0));
    OK("the automatic is not fought - it stays on for the whole hold",r.game.mode==1,
       Say("mode %d after 8 s of holding",r.game.mode,0,0));
    OK("the car actually accelerates in automatic",r.game.speed>5.0,
       Say("speed %.1f m/s in gear %d",r.game.gear,0,r.game.speed));
    OK("the automatic shifts up by itself",r.game.gear>=2,
       Say("gear %d (expected 2 or more)",r.game.gear,0,0));
    Release(&r,6); Run(&r,2000);
    OK("releasing the switch returns to manual",r.game.mode==0,
       Say("mode %d (expected manual)",r.game.mode,0,0));
}

/* THE ONE HE KEEPS HITTING: automatic selected while the car sits in neutral. */
static void T_automatic_from_neutral_moves(void){
    Rig r; RigInit(&r,1,4,0);                      /* sitting in neutral */
    Press(&r,6); Run(&r,6000);                     /* switch to automatic and wait */
    OK("automatic engaged from neutral actually drives",r.game.speed>3.0,
       Say("gear %d, speed %.1f - the car never pulled away",r.game.gear,0,r.game.speed));
    OK("automatic engaged from neutral has a gear engaged",r.game.gear>=1,
       Say("gear %d (expected 1 or more)",r.game.gear,0,0));
}

static void T_toggle_button_flips_once(void){
    Rig r; RigInit(&r,0,4,1);                      /* modeHold=0: a momentary button */
    Press(&r,0); Run(&r,1000);
    Press(&r,6); Run(&r,120); Release(&r,6); Run(&r,4000);
    OK("one press of a toggle button gives automatic",r.game.mode==1,
       Say("mode %d (expected automatic)",r.game.mode,0,0));
    OK("and the mod does not undo the user's choice",r.game.mode==1,
       Say("mode %d five seconds later",r.game.mode,0,0));
}

/* Mafia starts every mission in AUTOMATIC. Selecting the gear the car is already in is still a
   request for manual - the number matching does not make the mode right. */
static void T_start_in_auto_same_gear_still_switches(void){
    Rig r; RigInit(&r,0,4,1);
    r.game.mode=1;                                 /* the game as it starts */
    Press(&r,0); Run(&r,1500);                     /* lever into 1st - the gear it is already in */
    OK("selecting the gear the car is already in still engages manual",r.game.mode==0,
       Say("mode %d (expected manual), gear %d",r.game.mode,r.game.gear,0));
}

static void T_start_in_auto_hold_switch_off(void){
    Rig r; RigInit(&r,1,4,1);                      /* a latching switch, currently released */
    r.game.mode=1;
    Run(&r,1500);
    OK("a released hold-switch pulls the game out of its start-up automatic",r.game.mode==0,
       Say("mode %d (expected manual)",r.game.mode,0,0));
}

/* The lever is ALREADY in a gate when the mission loads - nobody presses anything, the game is
   in automatic, and the gear number happens to match. It must still go to manual: what Alex
   selected is "first gear, manual", and half of that is not it. */
static void T_lever_already_in_gear_at_start(void){
    Rig r; RigInit(&r,0,4,1);
    r.game.mode=1;                                 /* the game as it loads */
    Press(&r,0);                                   /* the lever was left in 1st - no new press */
    r.st.prevBits[0]=r.bits[0];                    /* so no rising edge is ever seen */
    Run(&r,1500);
    OK("a lever already sitting in gear at start still forces manual",r.game.mode==0,
       Say("mode %d (expected manual), gear %d",r.game.mode,r.game.gear,0));
}

/* The keyboard must work from ANY gear and ANY lever position, even with a hold-switch fitted -
   and the choice has to survive. Drive 7: B pressed ten times, the mode flipping to automatic
   and the mod dragging it back within 30 ms every time. */
static void T_keyboard_mode_key_is_respected(void){
    Rig r; RigInit(&r,1,4,1);                      /* a hold-switch rig, switch RELEASED */
    Press(&r,0); Run(&r,2000);                     /* driving in 1st, manual */
    r.game.mode=1;                                 /* he presses B on the keyboard */
    Run(&r,4000);
    OK("a keyboard switch to automatic is not undone",r.game.mode==1,
       Say("mode %d four seconds later (expected automatic)",r.game.mode,0,0));
    OK("and the car drives in it",r.game.speed>3.0,
       Say("speed %.1f in gear %d",r.game.gear,0,r.game.speed));
}

/* ...and moving the lever is how he gets back out of it. */
static void T_lever_action_returns_to_manual(void){
    Rig r; RigInit(&r,1,4,1);
    Press(&r,0); Run(&r,2000);
    r.game.mode=1;                                 /* automatic, chosen on the keyboard */
    Run(&r,2000);
    r.game.throttle=0;                             /* lift off, or the game refuses the drop */
    ReleaseAll(&r); Press(&r,1); Run(&r,6000);     /* move the lever to 2nd */
    OK("moving the lever pulls the game back to manual",r.game.mode==0,
       Say("mode %d (expected manual)",r.game.mode,0,0));
    OK("and the lever's gear arrives once the car is slow enough for it",r.game.gear==2,
       Say("gear %d (expected 2), speed %.1f",r.game.gear,0,r.game.speed));
}

/* From neutral, with the keyboard: automatic must still get the car moving. */
static void T_keyboard_automatic_from_neutral(void){
    Rig r; RigInit(&r,1,4,0);
    Run(&r,500);
    r.game.mode=1;                                 /* B on the keyboard, sitting in neutral */
    Run(&r,6000);
    OK("keyboard automatic from neutral gets the car moving",r.game.speed>3.0,
       Say("gear %d speed %.1f",r.game.gear,0,r.game.speed));
}

int main(void){
    printf("gearbox logic - simulated drives\n\n");
    T_upshift_sequence();
    T_deliberate_neutral();
    T_leaving_reverse_is_instant();
    T_refused_downshift_retries();
    T_hold_switch_keeps_automatic();
    T_automatic_from_neutral_moves();
    T_toggle_button_flips_once();
    T_start_in_auto_same_gear_still_switches();
    T_start_in_auto_hold_switch_off();
    T_lever_already_in_gear_at_start();
    T_keyboard_mode_key_is_respected();
    T_lever_action_returns_to_manual();
    T_keyboard_automatic_from_neutral();
    printf("\n%d checks, %d failed\n",g_run,g_fail);
    return g_fail?1:0;
}
