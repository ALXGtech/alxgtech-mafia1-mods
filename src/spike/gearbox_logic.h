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
    int modeCause;         /* 1 = the user's mode control, 2 = our own engage-manual */
    int pendAuto;          /* owed: switch to automatic once a gear is engaged */
    gb_ms pendAutoAt;      /* when that debt was incurred - it EXPIRES, see GB_PENDAUTO_MS */
    int haveMode;
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
} GBOut;

static void GBInit(GBState *s){
    for(int i=0;i<GB_MAXDEV;i++) s->prevBits[i]=0;
    s->sticky=-99; s->lastTarget=-99; s->respectAuto=0; s->engaged=0;
    s->blkDir=0; s->blkGear=-99; s->blkUntil=0; s->restStart=0; s->prevModeBtn=0;
    s->lastMode=-99; s->lastModeEmit=0; s->modeCause=0; s->haveMode=0; s->pendAuto=0;
    s->pendAutoAt=0;
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

static GBOut GBStep(const GBCfg *c,GBState *s,const GBIn *in){
    GBOut o; o.act=GB_NONE; o.target=-99; o.known=0; o.waitingNeutral=0; o.blockedAuto=0;

    /* 1. who asked for the transmission mode we are now in */
    if(in->gearValid&&in->mode>=0){
        if(!s->haveMode){ s->lastMode=in->mode; s->haveMode=1; }
        else if(in->mode!=s->lastMode){
            /* THE KEYBOARD IS A FIRST-CLASS CONTROL. The driver must be able to switch to automatic
               from any gear and any lever position, including with a hold-switch fitted, and the
               choice has to survive. This rule therefore applies in BOTH modes: a mode change we
               did not cause is the user's, and it stands until he moves the lever. */
            gb_ms dt=in->now - s->lastModeEmit;
            if(dt<=800&&s->modeCause==2) s->respectAuto=0;      /* our own engage-manual */
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
            o.act=GB_MODE_KEY; return o;
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
                s->lastModeEmit=in->now; s->modeCause=1; o.act=GB_MODE_KEY; return o;
            }
            return o;
        }
        if(in->mode==0){
            if(in->gear<1){ o.act=GB_UP; o.target=1; o.known=1; return o; }
            s->pendAuto=0; s->respectAuto=1; s->lastModeEmit=in->now; s->modeCause=1;
            o.act=GB_MODE_KEY; return o;
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
            o.act=GB_MODE_KEY; return o;
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
