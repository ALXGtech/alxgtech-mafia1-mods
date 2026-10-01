/* gearbox_logic.h - the shifter's decision core, as pure C.
 *
 * Everything that decides WHAT to do lives here: which gear the lever is asking for, whether a
 * rest position is a real neutral or just the lever in transit, which direction to step, when a
 * refusal should be retried, and who owns the transmission mode. No Windows, no I/O, no clock -
 * the caller passes the time in. That is what makes it testable without the game, a wheel, or a
 * human, which matters because every bug in this module so far was a logic bug that cost a real
 * drive to find.
 *
 * The .asi includes this and does the side effects (press a key, wait, log). tests/test_gearbox
 * includes it and drives it through the sequences the real drives produced.
 */
#ifndef GEARBOX_LOGIC_H
#define GEARBOX_LOGIC_H

#define GB_MAXDEV 4
#define GB_NPOS   8      /* 0 = reverse, 1 = neutral, 2..7 = gears 1..6 */

typedef unsigned long long gb_bits;
typedef unsigned int       gb_ms;

typedef struct {
    int posDev[GB_NPOS], posBtn[GB_NPOS], posGear[GB_NPOS];
    int modeDev, modeBtn, modeHold;    /* modeHold: the control is a latching switch */
    int dikUp, dikDown, dikMode;
    int neutralDelayMs, retryMs;
    int closedLoop;
    /* A LATCHING SWITCH STATES THE MODE, SO THE MODE MAY BE PUT BACK. Only meaningful with
       modeHold: a switch has a position at all times, so "what the driver wants" is never a
       guess, and a game that disagrees with it is wrong rather than informative.
       This is what actually answers his 2026-08-15 report. See the enforcement block in
       GBStep. 0 restores the behaviour of every build before 2026-09-14. */
    int modeEnforce;
} GBCfg;

typedef struct {
    int sticky;            /* last position the lever actually selected */
    int lastTarget;
    int respectAuto;       /* the user asked for an automatic - hands off */
    int engaged;           /* we already pressed the mode key for this request */
    int blkDir, blkGear;   /* a refused direction, and from which gear */
    gb_ms blkUntil;
    gb_ms restStart;       /* when the lever first reported no gate at all */
    int prevModeBtn;
    gb_bits prevBits[GB_MAXDEV];
    int lastMode;
    gb_ms lastModeEmit;
    int modeCause;         /* 1 = the user's mode control, 2 = our own engage-manual,
                              3 = our own enforcement of the switch position */
    int modeWant;          /* what the last action of OURS asked for: 1 automatic, 0 manual.
                              Needed because "we caused this change" is not the same claim as
                              "we wanted manual" - enforcement can ask for either, and reading
                              our own automatic back as "nobody chose this" put the mod into a
                              fight with itself that showed up as a car stuck in neutral. */
    int pendAuto;          /* owed: switch to automatic once a gear is engaged */
    gb_ms pendAutoAt;      /* when that debt was incurred - it EXPIRES, see GB_PENDAUTO_MS */
    int haveMode;
    /* How many times in a row enforcement has asked for a mode it did not get. It backs off on
       the strength of this: a mod that cannot move the game must not keep trying four times a
       second for the length of a drive, and the count is what tells a log reader the difference
       between "it put the box back twice" and "it is fighting something and losing". */
    int enfTries;
} GBState;

typedef struct {
    gb_bits bits[GB_MAXDEV];
    int gear, mode;        /* mode: 0 manual, 1 automatic, -1 unknown */
    int gearValid;
    gb_ms now;
} GBIn;

typedef enum { GB_NONE=0, GB_UP, GB_DOWN, GB_MODE_KEY } GBAct;

typedef struct {
    GBAct act;
    int   target;          /* the gear the lever is asking for */
    int   known;           /* was a target resolvable at all */
    int   waitingNeutral;  /* the rest position is being debounced right now */
    int   blockedAuto;     /* in automatic and respecting it - doing nothing on purpose */
    /* WHICH mode the action is asking for: 1 automatic, 0 manual, -1 "the other one".
       It used to be implicit because the only way to change the mode was to tap the game's key,
       which toggles - so the caller never had to know. Since 2026-09-14 the caller can WRITE the
       two fields the engine's own toggle writes, and a write has to know its direction. */
    int   wantAuto;
} GBOut;

static void GBInit(GBState *s){
    for(int i=0;i<GB_MAXDEV;i++) s->prevBits[i]=0;
    s->sticky=-99; s->lastTarget=-99; s->respectAuto=0; s->engaged=0;
    s->blkDir=0; s->blkGear=-99; s->blkUntil=0; s->restStart=0; s->prevModeBtn=0;
    s->lastMode=-99; s->lastModeEmit=0; s->modeCause=0; s->haveMode=0; s->pendAuto=0;
    s->pendAutoAt=0; s->enfTries=0; s->modeWant=-1;
}

static int GBHeld(const gb_bits *bits,int dev,int btn){
    if(btn<0||btn>=64||dev<0||dev>=GB_MAXDEV) return 0;
    return (bits[dev]&(1ull<<btn))?1:0;
}
static int GBAnyHeld(const GBCfg *c,const gb_bits *bits){
    for(int i=0;i<GB_NPOS;i++) if(GBHeld(bits,c->posDev[i],c->posBtn[i])) return 1;
    return 0;
}
/* The lever's target is STICKY: a wheel button is momentary and an H-gate is not always held,
   but a real lever keeps its position, so the last selection stands until another is made. The
   exception is a rig whose rest position genuinely IS neutral (neutral unbound). */
static int GBLeverTarget(const GBCfg *c,GBState *s,const gb_bits *bits,int *known){
    int found=-99;
    for(int i=0;i<GB_NPOS;i++) if(GBHeld(bits,c->posDev[i],c->posBtn[i])) found=c->posGear[i];
    if(found!=-99){ s->sticky=found; *known=1; return found; }
    if(c->posBtn[1]<0){ *known=1; return 0; }
    if(s->sticky!=-99){ *known=1; return s->sticky; }
    *known=0; return 0;
}

/* One cycle. Returns at most one action; the caller performs it and then calls GBAfterShift
   with whether the gear actually moved. */
/* How long an owed switch-to-automatic stays owed. Long enough for the manoeuvre it exists for
   - press mode, engage first, press mode - which the code itself paces at 400 ms per step, and
   short enough that a debt incurred on entering a car cannot be paid minutes later while the
   driver is happily in manual. */
#define GB_PENDAUTO_MS 3000u

/* How often enforcement may re-assert the switch's position, and how far it backs off once the
   game has refused it three times running. 400 ms matches the pace every other mode step here
   is given; 5 s is "something is holding it and we are not going to win by repeating faster". */
#define GB_MODE_ENFORCE_MS   400u
#define GB_MODE_ENFORCE_SLOW 5000u
#define GB_MODE_ENFORCE_FAST_TRIES 3

static GBOut GBStep(const GBCfg *c,GBState *s,const GBIn *in){
    GBOut o; o.act=GB_NONE; o.target=-99; o.known=0; o.waitingNeutral=0; o.blockedAuto=0;
    o.wantAuto=-1;

    /* 1. who asked for the transmission mode we are now in */
    if(in->gearValid&&in->mode>=0){
        if(!s->haveMode){ s->lastMode=in->mode; s->haveMode=1; }
        else if(in->mode!=s->lastMode){
            /* THE KEYBOARD IS A FIRST-CLASS CONTROL. The driver must be able to switch to automatic
               from any gear and any lever position, including with a hold-switch fitted, and the
               choice has to survive. This rule therefore applies in BOTH modes: a mode change we
               did not cause is the user's, and it stands until he moves the lever. */
            gb_ms dt=in->now - s->lastModeEmit;
            /* A CHANGE WE CAUSED IS NOT A REQUEST FROM ANYBODY. Cause 3 joined cause 2 on
               2026-09-14: enforcement puts the mode back where the switch says, and reading its
               own success as "the driver just chose this" would make the next flip stick. */
            if(dt<=800&&(s->modeCause==2||s->modeCause==3))
                s->respectAuto=(s->modeWant>0)?1:0;
            else                         s->respectAuto=(in->mode==1);
            /* automatic with no gear engaged is a car that will not move, however the mode got
               there - so owe the manoeuvre that fixes it */
            if(in->mode==1&&in->gear<1){ s->pendAuto=1; s->pendAutoAt=in->now; }
            /* AND CANCEL THE DEBT THE MOMENT HE IS IN MANUAL BY HIS OWN CHOICE.
               2026-08-12: the gearbox jumped to automatic by itself several times during a drive.
               This is how. The debt means automatic was requested; it was incurred
               from an OBSERVATION - the game is in automatic with no gear, which is true every
               time you get into a car - and nothing cancelled it. He then selected manual, and
               the owed manoeuvre paid itself off by pressing the mode key, putting him back in
               automatic from a gear he had chosen himself.
               A mode change we emitted OURSELVES is excluded by the same 800 ms window
               respectAuto uses, and by dt alone rather than by modeCause: the manoeuvre's own
               step to manual is marked cause 1, so testing for cause 2 cancelled the debt in the
               middle of paying it and the car stayed in neutral. The offline suite caught that
               immediately - in the scenario of keyboard automatic from neutral getting the car moving. */
            if(in->mode==0 && dt>800) s->pendAuto=0;
            s->blkDir=0;
            s->lastMode=in->mode;
        }
    }

    /* 2. the mode control. A latching switch states the mode; a button toggles it.
       AND: Mafia's automatic will not pull away from neutral. Drive 6 ended with mode=auto,
       gear=N and the car simply sitting there - reported as switching to automatic and going nowhere.
       Switching to automatic therefore means engaging a gear FIRST and flipping afterwards,
       which is what a driver means by the request. */
    if(c->dikMode>0&&c->modeBtn>=0){
        int now=GBHeld(in->bits,c->modeDev,c->modeBtn);
        int wantAuto=-1;                                  /* -1 = no request this cycle */
        if(c->modeHold){
            /* A latching switch is acted on when it MOVES, not held against. Enforcing the
               position every cycle meant the keyboard could not change anything: drive 7 shows
               B pressed ten times, the mode flipping to automatic, and the mod dragging it back
               within 30 ms each time. */
            if(now!=s->prevModeBtn){
                wantAuto=now?1:0;
                s->respectAuto=wantAuto;
                if(!in->gearValid||in->mode<0||in->mode==wantAuto) wantAuto=-1;
            }
        } else if(now&&!s->prevModeBtn){
            wantAuto=(in->mode==1)?0:1;                   /* a press means "the other one" */
        }
        s->prevModeBtn=now;
        if(wantAuto==1&&in->gearValid&&in->gear<1){ s->pendAuto=1; s->pendAutoAt=in->now; }
        else if(wantAuto>=0){
            s->pendAuto=0; s->lastModeEmit=in->now; s->modeCause=1;
            s->modeWant=wantAuto; o.act=GB_MODE_KEY; o.wantAuto=wantAuto; return o;
        }

        /* ---- THE SWITCH STATES THE MODE, SO PUT THE MODE BACK -----------------------------
         * His report of 2026-08-15: he starts in manual, drives, and at some point the game is
         * in automatic without him choosing it. Solved in the decompilation on 2026-09-14 -
         * the engine re-derives the mode from `player+0xADB`, whose own initialiser writes 0,
         * so every re-initialisation of that object drops the choice back to automatic
         * ([[the-game-resets-the-gearbox-choice]]).
         *
         * THE WRITE THAT FIXES IT COULD NOT FIRE. It hangs off GB_MODE_KEY, and in exactly his
         * case nothing produced one: the block above only acts when the switch MOVES, and the
         * "who asked" block at the top of this function reads an unexplained flip as the
         * driver's own choice (`respectAuto = 1`), which makes the rule below deliberately do
         * nothing until he moves the lever. The mechanism was found and the mod still sat there.
         *
         * With a LATCHING switch there is nothing to infer: the switch has a position at all
         * times and that position IS the request, so a game that disagrees with it is wrong.
         * This is the one case where enforcing the position every cycle is right, and the drive-7
         * objection above does not apply to it - that was about the mod fighting a KEYBOARD press
         * within 30 ms, and this backs off to five seconds once the game has refused it three
         * times, which is what a fight looks like from here.
         *
         * Only with modeHold. A momentary button has no position to enforce. */
        if(c->modeHold&&c->modeEnforce&&wantAuto<0&&in->gearValid&&in->mode>=0){
            int want=now?1:0;
            if(in->mode==want){
                s->enfTries=0;
            } else {
                gb_ms gap=(s->enfTries<GB_MODE_ENFORCE_FAST_TRIES)
                          ? GB_MODE_ENFORCE_MS : GB_MODE_ENFORCE_SLOW;
                if((gb_ms)(in->now - s->lastModeEmit)>gap){
                    /* Asking for AUTOMATIC out of neutral is the one request that cannot be
                       served directly - Mafia's automatic will not pull away from a neutral
                       gearbox - so it becomes the owed manoeuvre instead, exactly as a moved
                       switch does five lines above. */
                    if(want==1&&in->gear<1){
                        if(!s->pendAuto){ s->pendAuto=1; s->pendAutoAt=in->now; }
                    } else {
                        s->respectAuto=want;
                        s->engaged=0;
                        s->pendAuto=0;
                        s->enfTries++;
                        s->lastModeEmit=in->now; s->modeCause=3;
                        s->modeWant=want; o.act=GB_MODE_KEY; o.wantAuto=want; return o;
                    }
                }
            }
        }
    }
    /* THE OWED MANOEUVRE: automatic, but no gear engaged. The gear keys do nothing in
       automatic, so the only way through is manual -> first gear -> automatic again. Three
       presses, done once, and the driver just sees the car pull away. */
    /* A DEBT THAT WAS NEVER PAID IS STALE, not patient. It exists to finish a switch to
       automatic within a moment of it being asked for; if a few seconds have passed, whatever
       made it true is no longer what anybody wants. Belt and braces for the cancel above - a
       latch with no expiry is the shape every gearbox bug here has had. */
    if(s->pendAuto && (gb_ms)(in->now - s->pendAutoAt) > GB_PENDAUTO_MS) s->pendAuto=0;

    if(s->pendAuto&&in->gearValid){
        if(in->mode==1&&in->gear<1){
            if((gb_ms)(in->now-s->lastModeEmit)>400){
                s->lastModeEmit=in->now; s->modeCause=1;
                s->modeWant=0; o.act=GB_MODE_KEY; o.wantAuto=0; return o;   /* manual, to engage */
            }
            return o;
        }
        if(in->mode==0){
            if(in->gear<1){ o.act=GB_UP; o.target=1; o.known=1; return o; }
            s->pendAuto=0; s->respectAuto=1; s->lastModeEmit=in->now; s->modeCause=1;
            s->modeWant=1; o.act=GB_MODE_KEY; o.wantAuto=1; return o;   /* a gear is in - automatic */
        }
        s->pendAuto=0;                                  /* automatic with a gear in - done */
    }

    /* 3. a fresh press on any lever position is a request only manual can serve */
    for(int i=0;i<GB_NPOS;i++){
        int dv=c->posDev[i],b=c->posBtn[i];
        if(b<0||b>=64||dv<0||dv>=GB_MAXDEV) continue;
        if((in->bits[dv]&~s->prevBits[dv])&(1ull<<b)){
            /* Moving the lever asks for a gear, and only manual can serve one - in every
               configuration, hold-switch included. This is how the driver gets out of an
               automatic he turned on with the keyboard. */
            s->respectAuto=0; s->engaged=0; break;
        }
    }
    for(int d=0;d<GB_MAXDEV;d++) s->prevBits[d]=in->bits[d];

    /* 4. the lever passes through nothing on its way to something */
    int atRest=!GBAnyHeld(c,in->bits);
    if(atRest){ if(!s->restStart) s->restStart=in->now?in->now:1; } else s->restStart=0;

    int known=0,target=GBLeverTarget(c,s,in->bits,&known);
    o.known=known; o.target=target;
    if(!known) return o;

    if(atRest&&c->posBtn[1]<0&&in->gearValid&&in->gear!=-1&&s->restStart&&
       (gb_ms)(in->now-s->restStart)<(gb_ms)c->neutralDelayMs){ o.waitingNeutral=1; return o; }

    if(target!=s->lastTarget){ s->lastTarget=target; s->blkDir=0; }

    if(!c->closedLoop||!in->gearValid) return o;

    /* 5. automatic eats the gear keys, so either respect it or leave it - never fight it.
       This is checked BEFORE the gear already matching, because the mode is part of what the
       driver selected. Mafia starts every mission in automatic; selecting the gear the car
       happens to be in already is still a request for MANUAL, and skipping it on the grounds
       that the number matched left the driver in an automatic that was not requested. */
    if(in->mode==1){
        if(s->respectAuto){ o.blockedAuto=1; return o; }
        if(c->dikMode>0&&!s->engaged){
            s->engaged=1; s->lastModeEmit=in->now; s->modeCause=2;
            s->modeWant=0; o.act=GB_MODE_KEY; o.wantAuto=0; return o;   /* a gear was asked: manual */
        }
        o.blockedAuto=1; return o;
    }

    if(target==in->gear) return o;

    /* 6. one sequential step, unless this exact move was just refused */
    int dir=(target>in->gear)?1:-1;
    if(dir==s->blkDir&&in->gear==s->blkGear&&(gb_ms)(in->now-s->blkUntil)>(gb_ms)0x80000000u) return o;
    if(dir==s->blkDir&&in->gear==s->blkGear&&in->now<s->blkUntil) return o;
    o.act=(dir>0)?GB_UP:GB_DOWN;
    return o;
}

/* Told whether the gear moved. A refusal is ALWAYS temporary - it blocks that one direction
   from that one gear for retryMs and nothing more. Learned permanent limits cost three drives. */
static void GBAfterShift(const GBCfg *c,GBState *s,int dir,int gearBefore,int moved,gb_ms now){
    if(moved){ s->blkDir=0; return; }
    s->blkDir=dir; s->blkGear=gearBefore; s->blkUntil=now+(gb_ms)c->retryMs;
}

#endif
