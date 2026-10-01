/*
 * Mafia LH force feedback v7 (hybrid SAT). See docs/v7-hybrid-sat-design.md.
 *
 * v7.46 (2026-07-24) - the bypass kick is scaled by dampMult, the wheel's own weight,
 *   exactly as v7.31 already does for the soft slide nudge. In a deep slide the wheel sits
 *   at ~15% weight but this ConstantForce never saw that, so a slide-scrub jab landed at
 *   full strength - reported as the scrub jab being too strong. Not sliding -> dampMult 1.0 -> the
 *   force is BIT-IDENTICAL to the reference. Nothing is gated, so no ram is ever lost.
 * v7.45 (2026-07-24) - REFUTED, never shipped. Gated the bypass on `braking` alone; 20 of 23
 *   known-real crashes measure as sustained too, so it would swallow real crashes.
 * v7.44 (2026-07-24) - REVERTED. Gated the bypass on braking||slipDeg -> lost rams in slides.
 * v7.43 (2026-07-24) - the bypass may preempt a soft nudge, same as the normal branch.
 * v7.42 (2026-07-24) - DETECTION ONLY, bypass branch only. The v7.31 reference crash
 *   force is untouched, the normal branch is untouched, no effect changes force,
 *   duration or timing. Kills the last phantom class: the position ring occasionally
 *   claims an entry speed the speedometer never showed (91.9 / 69.9 / 540.7 m/s on the
 *   v7.41 drive), and the bypass derives its force FROM that entry, so each one fired
 *   at 25000. See BYPASS_ENTRY_MAX / BYPASS_ENTRY_OVER.
 *
 * v7 changes on top of v6.20:
 *  - F1 slip onset lowered 8->4 deg (SLIP_DEG_LO): wheel lightens earlier, catch the
 *    slide by the nose (user feedback: v6.20 lightened too late).
 *  - Phantom-knock filter: crash threshold 0.8+0.04v -> 2.5+0.08v, + TELE_DROP guard.
 *    v6.20 log had 27 crashes/session, many on ~1 m/s scrub + 2 respawn mag=25000.
 *  - Bump-from-behind kick: yawRate (d heading/dt) minus steer-expected = external
 *    disturbance; a sudden step while not sliding -> short ConstantForce kick.
 *  - Logs full front-tyre-slip components (K_BUMP: yawRate/expectedYaw/disturb) so the
 *    calibrated tyre-slip alphaF model (v7.1) can be fit offline from one drive.
 *
 * v6.20 implemented F1 (lateral slip -> light wheel), built on the v6.19 MatrixHunt find:
 *  [car+0x58] -> LS3DF frame, frame+0x164 = 3x3 world rotation matrix. heading =
 *  atan2(m[6],m[8]). slip = heading - course - offset, course from d/dt(world pos
 *  0x2A18,0x2A10). When |slip| exceeds SLIP_DEG_LO the spring+damper are cut so the wheel
 *  goes light exactly as the car breaks into a slide (oversteer/handbrake), like AC.
 *  Zero-point auto-calibrates (offset EMA nudged only on small slip) so straight driving
 *  reads ~0 regardless of matrix convention. Speed-gated (course is noisy below ~4 m/s).
 *  Validated offline: |slip|<10deg for 84% of driving, 60-175deg in the test slides.
 *
 * v6.19 MatrixHunt is now MatrixLog: logs the known matrix at [car+0x58]+0x164 every
 *  50ms (F1 live cross-check + roll/pitch for F2 road rumble). Observation only.
 *
 * THREE-EFFECT ARCHITECTURE:
 *  - ConstantForce (g_eff):    crash/damage kicks. Transient only.
 *  - Spring (g_spring):        self-centering. Hardware-computed from wheel position.
 *                              Zero at standstill, builds with speed. No feedback loop.
 *  - Damper (g_damper):        rotation resistance. Hardware-computed from wheel velocity.
 *                              Heavy at standstill, lighter when moving. No feedback loop.
 *
 * Why hardware effects instead of software velocity damper (v6.0/v6.1):
 *  The software damper read the wheel's steer position, applied force, that force
 *  physically moved the wheel, the new position was read back on the next poll,
 *  creating an opposing force - classic control loop oscillation. Cannot be fixed
 *  by reducing gain alone (just oscillates more slowly). Hardware Spring/Damper effects
 *  run inside the wheel's own firmware at kHz using the wheel's own velocity sensor.
 *  No polling delay, no feedback instability.
 *
 * Effect GUIDs (first DWORD only differs):
 *  ConstantForce 0x13541C20, Spring 0x13541C27, Damper 0x13541C28
 *
 * Tuning knobs (DI units, 0..10000 nominal):
 *  K_CENTER_DI:      spring coefficient at full speed
 *  K_HISPEED_DI:     extra spring above 80 km/h
 *  K_DAMP_DI_STATIC: damper at standstill (heavy)
 *  K_DAMP_DI_MOVING: damper at speed (~25% of static)
 *
 * Sign confirmed: on CMagic SimPro, centering is in the POSITIVE direction for
 * the Spring effect (lOffset=0, symmetric coefficients work correctly).
 *
 * Crash detection unchanged from v5.1:
 *  EMA_HI_SPD=7 kills phantom kicks; MAG_SPD_SCALE=700 gives medium ~9000.
 *  MAX_MAG raised to 20000 (user requested).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* The build-recognition digest (SelectBuild, far below). Header only, no library: this module is
   linked -nostdlib, which is why the arithmetic lives in md5_core.h and not in md5_tiny.h. */
#include "../shared/md5_core.h"

/* gog-patcher M1: forward declarations for ffb_settings.c. The
   real definitions are #included further down, near GamePath, because they need GamePath and
   Log to already exist - but choke points earlier in this file (RefCrashForce, SetMag,
   SetSpring/SetDamper, the F3/F4/F5/road/SAT sites) call them before that include is reached.
   Each g_pct* is a tentative definition (legal C, no initializer); the initialised definition
   in ffb_settings.c supplies 100 and the two merge into one object. */
static LONG Mul(LONG mag, LONG pct);
static volatile LONG g_pctMaster;
static volatile LONG g_pctSpring;
/* gog-patcher 2026-07-27b: the car damper is TWO controls, standing and moving, because that is
   what the truck pair multiplies and one number in front of two very different quantities was
   already the complaint. Legacy `damper=` still loads into both - see ffb_settings.c. */
static volatile LONG g_pctDampStatic;
static volatile LONG g_pctDampMoving;
static volatile LONG g_pctObjects;
static volatile LONG g_pctPed;
static volatile LONG g_pctCrash;
static volatile LONG g_pctGun;
static volatile LONG g_pctRoad;
static volatile LONG g_pctSat;
/* gog-patcher 2026-07-27: the two TRUCK damper percents. Same tentative-definition arrangement.
   They pair one-for-one with the two car percents above - a truck value is the MULTIPLIER on
   what a car gets in the same condition, so the dialog can put them side by side - and they are
   inert (1.0f, returned before the percent is even read) in anything that is not a truck.
   There is deliberately no truck SPRING: settled 2026-07-27b that the truck spring is the
   same as the passenger-car spring, so it does not need a separate value. */
static volatile LONG g_pctTruckStatic;
static volatile LONG g_pctTruckMoving;
static void CheckFFBSettingsReload(void);
/* M1b, ffb_status.c, same arrangement: SetMag records the SetParameters HRESULT here so the
   status file can say "effective" and not merely "connected". */
static volatile LONG g_lastEffectHr;
static void WriteFFBStatus(void);

/* ---- crash detection tuning ---- */
#define MAX_MAG            25000  /* raised from 20000: crash force +1.5x hits cap sooner */
#define POLL_MS                8
#define TEST_PULSE_MS        120

#define SPEED_OFF   0x2A0Cu
#define STEER_OFF   0x38B0u
#define DMG_OFF     0x29E4u

#define EMA_FAST    0.20f
#define EMA_SLOW    0.05f
#define EMA_LO_SPD  5.0f
#define EMA_HI_SPD  7.0f

/* v7.1f: crash threshold REVERTED to the working v6.20 values (0.8 / 0.04). Raising it to
   1.6/0.05 for the phantom-knock filter ALSO killed real crashes: the crash signal is a
   drop in EMA-smoothed speed, and the EMA (alpha 0.05) damps an impact so a 20->10 m/s
   crash only yields drop ~1.85 - under the 2.6 threshold at 20 m/s, so 0 crashes fired in
   a 227s drive. 0.8/0.04 detects them (as in v6.20). Phantom knocks handled later without
   sacrificing crash detection. TELE_DROP guard kept (respawn). */
#define CRASH_BASE    0.8f
#define CRASH_K       0.04f
/* ---- v7.54: THE HIGH-SPEED DETECTION MISS ----
   Ported from upstream v7.54 (gog-patcher 2026-08-03). Upstream's own text, kept because the
   numbers ARE the justification and this one was confirmed BY FEEL - it is the only change in
   this sync he has driven and approved.
   Measured 2026-07-31: a 90 km/h car crash that produced NOTHING: arming needed BOTH
   `drop > thresh` AND `gsShortDrop > gsNeed`. At that impact the ground half passed nine
   times over (36.58 vs 4.00) and the speedometer half missed by 5% (1.652 vs 1.736), so
   the whole branch stayed shut. `thresh` RISES with speed (CRASH_BASE + CRASH_K*spdEma)
   while `drop` is a per-tick fall of the SMOOTHED speedometer, which a violent impact
   spreads instead of spiking - so the bar climbs with speed and the signal does not.
   Measured archive-wide: of the ticks where ground speed unmistakably collapsed, the share
   blocked by `drop <= thresh` runs 19% at 20-39 km/h, 81% at 40-59, 87% at 60-79 and 92%
   at 80-99. Systematic, and exactly his complaint.
   The fix lets a big enough ground-speed collapse arm the branch on its own. K priced over
   the whole archive in EVENTS (upstream src/v754_gsarm_audit.py), against 351 that already arm:
     K=4 -> 74 new, 8 of them in the 0-19 km/h parked-noise band   (rejected)
     K=5 -> 27 new, 40-179 km/h
     K=6 -> 13 new, 60-179, NOTHING below 60, margin 1.52x on his crash   <- chosen
     K=8 ->  5 new, but only 1.14x margin, thinner than anything shipped here
   Nothing else moves: the soft/sharp classification, the shape vetoes, the refractory and
   the locked force formula all still apply to whatever this admits. */
#define GS_ARM_K      6.0f
/* ---- v7.59: THE TRAM-TRACK PHANTOMS. Drive of 2026-08-06 ----
   Reported: phantom kicks at 70-90 km/h while driving on the tram tracks,
   and zero tolerance for a false hit. The log agreed and was worse than the complaint:
   41 sharp crashes, 28 armed ONLY by the GS branch above, 26 of them inside two bursts of
   10 and 9 seconds at 73-96 km/h. Inside a burst they alternate - one fire with the car
   losing 0.57-0.74 m/s at magnitude 9000-12500, the next half a second later with the car
   ACCELERATING (drop -0.05 to -0.65). That is a ~1.5 Hz oscillation, not a collision. It is
   the same physical case v7.3's ground-speed veto was written for ("race car's wheel speed
   spikes -> phantom MAX kicks on the straight at 120 km/h"): a rough surface at speed. v7.54
   let the ground half arm ALONE, which is precisely the protection that veto provided.

   TWO SHAPES WERE REFUSED BY HIM, both for good reasons, and they are recorded so neither
   comes back:
     - a speed gate at 50-60 km/h: it would cancel a real crash at 100 km/h, and worse, it
       would undo v7.54 itself - the archive measures the speedometer half failing on 92% of
       collapses at 80-99 km/h, which is why the GS branch exists at all.
     - the phantom class does persist while the real one does not (16 ticks vs 1): true here, but it
       admits a lone phantom, and he asked for none.

   WHAT THIS IS INSTEAD. The GS branch exists because the speedometer half MISSES. The question
   is whether it misses NARROWLY (a real impact the smoothing spread out) or COMPLETELY (noise).
   So require some agreement, as a fraction of the very threshold this branch bypasses.
   Priced over every log both benches hold - 165 distinct drives, 940 sharps
   (tools/price-gsarm.py):

     drop/thresh of all 38 GS-only fires on this bench   max 0.441
     his APPROVED 90 km/h catch of 2026-07-31, the one    0.952   <- what v7.54 was built for
     their loud unjudged fire (mag 14567, 65 km/h)        0.875
     their three boundary fires at 24 km/h                0.999-1.000

   An empty band of more than 2x. 0.75 sits in it: 1.70x above the worst phantom this bench has
   ever produced, 1.27x below his approved catch. It removes 38 of 38 GS-only fires here and 9
   of 13 there, and costs EXACTLY the same events as 0.50 - so the margin is free.

   WHAT STAYS BIT-IDENTICAL: all 889 speedometer-armed crashes on both benches, the soft/sharp
   classification, every shape veto, the refractory, and the force formula. This test can only
   ever subtract from the branch v7.54 added; it cannot arm anything, rescale anything, or
   delay anything. Zero added latency - it is one comparison at the same tick. */
#define GS_ARM_AGREE  0.75f

/* ---- v7.63, 2026-08-06: THE CEILING NEEDS THE WORLD'S AGREEMENT ----------------------------
   Same shape as GS_ARM_AGREE above, aimed at the branch that one cannot reach: a kick that
   reaches the MAX_MAG ceiling must be corroborated by the car actually slowing down.

   WHY ONLY THE CEILING, and this is measured rather than cautious. Applied to every sharp the
   test is WRONG: on the 2026-08-06 drive he approved a pole crash at ratio 1.06 and 1.78 and a
   car-to-car hit at 1.53, all of which this would have cancelled. At the ceiling it is right on
   every event we have. So the guard is scoped to the population he actually complained about.

   THE EVIDENCE - eight ceiling fires across two drives, ratio = the single-tick world step at
   the firing tick over the MEDIAN nonzero step of ticks -16..-9:

     drive 6  t+267234  1.01   he pressed F8       drive 6  t+643391  0.01   he pressed F7
     drive 6  t+387609  1.00   he pressed F8       drive 6  t+747203  0.54   he pressed F7
     drive 6  t+446078  1.01   he pressed F8
     drive 6  t+469609  1.01   he pressed F8
     drive 5  t+394922  0.98   RECLASSIFIED        drive 5  t+596360  1.02   he disliked it

   Fourteen live marks put his voice on the log's own axis, so six of those eight are labelled
   at the moment they happened rather than reasoned about afterwards. t+394922 is the exception
   and it is the one that killed the dropout-only gate: it had been recorded as approved, from
   his general remark that anything he approved was probably a real crash, on a drive where he
   pressed NO keys at all. Both independent measures - the world step and the speedometer's
   recovery - call it a phantom, and he agreed to reclassify it on 2026-08-06.

   MARGIN, and it is THIN: lowest phantom 0.98 against highest real 0.54, so the balanced
   threshold is 0.73 and the margin is 1.35x each way. GS_ARM_AGREE was priced over 165 drives;
   this has EIGHT events. That is why 0xEC logs the ratio at every ceiling fire whether it vetoes
   or not - the next drives price it for free - and why `ceil_agree = 0` turns it off outright.

   WHAT STAYS BIT-IDENTICAL: everything below the ceiling, which on the drive 6 log is 129 of
   135 FIRE records - every soft nudge, every object and pedestrian tap, every bullet tap, the
   continuous channels, and all 13 sub-ceiling sharps. The test can only ever SUBTRACT a
   maximum-force kick; it cannot arm, rescale, delay or lengthen anything. Zero added latency:
   the ring is already written each tick for the trace, and this is one comparison at the same
   tick. If fewer than CEIL_NEED ticks of history exist it does nothing, because a guard that
   removes force must fail OPEN. */
/* v7.64: the torn-read guard on the speed field. `tear_fix` is the RATIO below which a
   one-tick collapse is declared physically impossible; 0 disables the guard and restores
   v7.63 exactly. TEAR_MOVING_MS keeps it away from a car that is genuinely nearly stopped,
   where small absolute numbers make any ratio meaningless. */
#define CONTACT_PROBE   1      /* v7.64 logging-only contact probe; 0 = off */
#define CONTACT_MAX     8u     /* records logged per tick, so a pile-up cannot flood */
#define TEAR_MOVING_MS  2.0f
#define TEAR_RECOVER    0.50f  /* next sample back to half the old speed = it never really fell */
static float g_tearFix = 0.05f;

/* v7.65 THE TICK LOG. Requested 2026-08-07: log everything once and for all and fix
   everything, rather than continuing to patch around it.
   Nine versions filtered the CONSEQUENCES of the speed field and none touched the signal,
   because the signal has never been recorded. Every per-tick series this project owns comes
   from the 0xE7/0xE9 trace windows around fires - about 2% of ticks - and a rate measured
   there was twice quoted as a whole-drive rate. This channel ends that: the raw INPUTS at the
   FFB thread's own rate, for the whole drive, so every EMA, drop, threshold and filter can be
   replayed offline against BOTH speed sources instead of being argued about.

   IT LOGS INPUTS, NOT CONCLUSIONS. spdEma, gsNow, drop and thresh are all functions of these
   three numbers and the constants, so recording them too would only bake this build's
   arithmetic into the file and make a different arithmetic unmeasurable.

   THE CLOCK IS THE WHOLE POINT, and the mod did not have one. GetTickCount's granularity is
   ~15.6 ms, the same size as the tick itself, so it can only ever report 0, 15 or 31 - which
   is exactly why `rawStep` divides by a hardcoded nominal POLL_MS = 8 while the loop really
   runs at ~16 ms, and why every threshold in position units means something other than its
   name. QueryPerformanceCounter changes no system state (unlike timeBeginPeriod, which the
   archive forbids because it would triple every EMA time constant); it only reads a counter.
   The RAW counter delta is logged and the frequency is written once at startup, so the
   division happens offline - a 64-bit divide here would pull in a compiler helper that does
   not exist under -nostdlib.

   Cost: two 24-byte records per tick, about 3 KB/s, ~2.7 MB over a fifteen-minute drive,
   against MAX_REC 1200000. Logging only: not one force, threshold, duration or branch is
   touched by it, which is what makes it safe to carry on a drive that is also judging feel. */
static LONG g_tickLog = 1;

/* v7.66 `car_snap` - whether the 1.2 GB car-struct dump is written at all. 0 = not written, and
   that is the default: it is a field-finding instrument, not a per-drive record, and every
   ordinary drive was paying about 2.4 GB for it (the file plus the guard's archive copy).
   Read at the point the handle is created, not in ReadFFBSettings() - see there. */
static LONG g_carSnap = 0;

/* v7.66 impulse candidate log (0xD9). Measurement only - it arms nothing and scales no force.
   IMP_VEL_WIN: ticks the velocity vector is averaged over (~124 ms at the measured 15.50 ms).
   IMP_LAG    : ticks between the two velocities compared (~186 ms). ADJACENT ticks do not work
                - smoothing flattens a one-tick difference, and the offline replay read 0 of 3
                approved kicks until this lag was introduced.
   IMP_LOG_MIN: only records above this reach the log; the p90 of ordinary driving was 2.44 m/s
                on drive 9, so 5.0 keeps the file small while staying below the 7.31 threshold
                the replay settled on - the offline pass can still raise it, never lower it.
   IMP_TELEPORT: a mission load moves the car discontinuously (one read 33966 m/s). */
#define IMP_VEL_WIN    8
#define IMP_LAG       12
#define IMP_LOG_MIN    5.0f
#define IMP_TELEPORT  60.0f
/* QueryPerformanceFrequency, cached. 0 until DllMain reads it; every user must check. */
static double g_qpcHz = 0.0;

/* The impulse KICK - a force, not a measurement, and therefore off unless asked for.
   IMP_KICK_MIN is 3x the p90 of |dV| measured on drive 9 (2.44 m/s). It is a constant here
   rather than adaptive on purpose: an adaptive threshold that moves with the drive cannot be
   A/B'd, and this one has to be judged by feel against a build that differs in ONE thing.
   IMP_FORCE_A/B are the least-squares fit through his own three approved kicks - see the
   comment at the call site. IMP_KICK_MS matches the damage pulse; IMP_REFRACT_MS stops a
   multi-tick impact being felt as a machine-gun. */
/* v7.67: lowered from 7.3 once the speedometer veto existed to catch what a lower threshold
   lets through. The reason is physical: it likely comes down to relative speed. A car that
   rams you while you are both doing 80 has a
   small RELATIVE speed, so the impulse is small; the same ram at 20 km/h is violent. Measured
   over drive 10, the share of impulse candidates that clear 7.3:
       0-19 km/h 58%   20-39 43%   40-59 21%   60-79 10%   80-99 11%   100+ 0%
   The 6:27-7:29 stretch - reported as no feedback at all despite a lot of hits, almost no reaction -
   was driven at 50-143 km/h and produced 32 candidates, every one of them under 7.3.
   At 6.0, with the veto in place: 25 kicks over drive 10, the same 7 approvals kept, still
   0 of his 17 complaints covered, and the dead stretch stops being empty. 5.0 was measured too
   and brings a complaint back, so this is the floor. */
#define IMP_KICK_MIN   6.0f
#define IMP_FORCE_A   834.0f
#define IMP_FORCE_B  7572.0f
#define IMP_KICK_MS     160u
#define IMP_REFRACT_MS  300u
static LONG g_impulseKick = 0;
static DWORD s_impLastKick = 0;

/* ---- v7.67: the speedometer has to AGREE before the kick is allowed out ----
   Drive 10 measured what the impulse channel actually did: of 42 kicks, 11 sat on a real speed
   loss, 11 on a genuine speed GAIN (a ram from behind does accelerate the car), and **20 were
   fired while the speedometer did not move at all**. Those 20 are what he heard as "a bunch
   of phantom bad hits" - they cluster at 10:46-11:04, 11:38-11:42 and 11:57-12:01, exactly
   where he said it, and their mean speed is 61 km/h against 40 on the real ones: the position
   derivative gets noisier the faster the car goes.
   The cure is a second, INDEPENDENT witness. The speedometer field is unreliable per-sample
   (that is the whole root-cause story) but it is not correlated with the position noise, so
   requiring both to agree kills what neither alone can.
   It cannot be done looking backwards - the speed change caused by an impact appears AFTER the
   impulse does, and a backward-only test was measured leaving 27-32 kicks with every complaint
   still covered. So the kick is held for IMP_CONFIRM_MS and released only if the speedometer
   moved by IMP_SPD_CONFIRM. Measured over drive 10 at 100 ms / 8 km/h:
       42 kicks -> 17,  approvals kept 5 of 5,  complaints with a kick behind them 6 -> 0.
   100 ms is a fifth of the 450-1000 ms lag he complained about and shorter than the kick's own
   160 ms envelope. `impulse_confirm = 0` disables the hold for an A/B. */
#define IMP_CONFIRM_MS   100u
#define IMP_SPD_CONFIRM  2.22f   /* m/s = 8 km/h */
static LONG  g_impConfirm = 1;
static int   s_impPend    = 0;
static DWORD s_impPendT   = 0;
static float s_impPendDv  = 0.0f;
static float s_impPendSpd = 0.0f;
static float s_impPendAlong = 0.0f;
static int   s_impPendCount = 0;    /* the ring position the candidate was seen at */

/* ---- v7.88: THE WITNESS STOPS READING THE FIELD IT WAS MEANT TO POLICE -----------------------
   The veto above was added in v7.67 as an INDEPENDENT second opinion, and it is not independent:
   it reads `car+0x2A0C`, the field whose tearing is the whole root-cause story. Measured on drive
   ours-021, a steady 23 m/s reads as 11.4 and 42.6 inside four ticks, which is six times the
   2.22 m/s this asks for - so the veto does not refuse a phantom, it CONFIRMS one. All 53 kicks
   of that drive came out through it, 51 of them with no live contact anywhere near.

   The cure is to measure the consequence the same way the trigger measures the cause: from
   POSITION, which the engine integrates and a torn read cannot move. A crash changes the speed
   and the change STAYS; a tear is gone by the next tick.

       vBefore = the 8-tick window ending at the candidate
       vAfter  = the 8-tick window ending IMP_CONFIRM2_MS later
       fire only if |vAfter - vBefore| >= impulse_dv_min

   4.0 m/s across 200 ms is 2 g. No braking in this engine reaches it; a pole passes it easily.
   `impulse_witness = 0` restores the v7.67 speedometer veto for an A/B. */
#define IMP_CONFIRM2_MS  300u
#define IMP_WIT_SPAN       8     /* UPDATES per speed window, about 130-250 ms in practice */
/* ---- v7.89 THE RING IS THE GAME'S CLOCK, NOT OURS -------------------------------------------
   Drive ours-022 refuted v7.88 in 40 seconds: 11 kicks got through the new witness and 10 of them
   had no real speed change behind them. The reason is not the witness's idea, it is its sampling.
   Measured on that drive, 21.8% of our 15.6 ms polls see a BIT-IDENTICAL position and the next
   poll then carries two updates' worth of travel - per-tick speed reads `29 0 31 30 57 0 31`
   where the truth is a steady 30. Our poll beats against the game's own update.
   One missed step at the edge of a 128 ms window is about half a metre, i.e. 3.9 m/s at 30 m/s -
   and the witness's threshold is 4.0. It was noisy by exactly its own bar.
   So a sample is now written only when the position ACTUALLY MOVED, carrying the wall time
   accumulated since the previous one. Both endpoints of every window are then real updates and
   the time base is the real interval between them. The trigger reads the same ring and gets the
   same benefit; offline over five drives this takes the phantoms from 174 to 62 while the real
   impacts reported rise from 30 to 69. */
#define IMP_RING        64
#define IMP_RMASK       (IMP_RING - 1)
static LONG  g_impWitness  = 1;
static LONG  g_impDvMin    = 400;   /* m/s x100 */
static LONG  g_impConfirm2 = (LONG)IMP_CONFIRM2_MS;
/* The in-game A/B for this channel, the same shape as the ground channel's I and O: two keys
   that ASSERT a state rather than one that toggles, each answering in the wheel, because he is
   driving and cannot look at anything. K and L are free - the letter mark bank was retired in
   v7.65 and neither code appears anywhere else in this file. */
static LONG  g_impKeyOn  = 0x4B;    /* K */
static LONG  g_impKeyOff = 0x4C;    /* L */
static LONG  g_impOverride = -1;    /* -1 = the ini decides; 0/1 = he decided, mid-drive */

/* The two functions that decide it live next to Sqrtf, which they need - search for
   ImpRingSpeed. They are pure so a host can exercise them with no game and no wheel. */

/* ---- v7.66: the manifold tap stops waiting, and asks the impulse instead ----
   `PED_MIN_CONTACTS = 2` made the tap fire on the SECOND growth of the contact manifold, which
   is a built-in delay: measured 125-1062 ms on drive 9, median ~450, and this was called out loud
   (described as another hit from behind, again delayed - as if a second later). It also threw away 41
   single contacts that never produced anything at all.
   Firing on the FIRST growth removes the delay but takes the candidate count from 14 to 55.
   Requiring an IMPULSE alongside the contact fixes both at once - measured on drive 9:
     confirm >= 1.5 m/s -> 46 taps      confirm >= 2.5 -> 31      confirm >= 4.0 -> 14
   At 4.0 the count is exactly what it is today, the delay is gone, and 9 of the 14 present
   taps drop out because they have no impulse behind them - those are the phantoms he
   complained about. So: same number of events, none of them late, most of the false ones gone.
   `tap_needs_impulse = 0` restores the v7.65 behaviour for an A/B. */
#define PED_IMP_CONFIRM  4.0f
#define IMP_CONF_WIN     600u
static LONG  g_tapNeedsImpulse = 1;
static float g_impPeak  = 0.0f;   /* peak |dV| inside the confirmation window */
static DWORD g_impPeakT = 0;

#include "ffb_ceil.h"
static CeilRing g_ceil;
static float g_ceilAgree = 0.73f;   /* 0 = guard off; overridden by `ceil_agree` in the ini */

/* ---- v7.60: THE FIRE TRACE. Logging only - no detector, no constant, no force moves. ----
   Two complaints survived the v7.59 drive and NEITHER can be priced from the logs we have:

     A. t=81.1 s, "Boom! Random hit" - the record claims a 4.139 m/s drop and fires the 25000
        ceiling while `car_mtx` world position shows the car ACCELERATING, 74 -> 89 km/h.
     B. t=132.0 s: reported as a wheel crash one second later - one continuous 87 -> 0 km/h stop
        answered twice, a soft nudge then a sharp 410 ms later at 19 km/h. v7.49 named this
        class (END-OF-STOP JAB) and gated it by SHAPE, but bypass-only and 400 ms wide.

   WHY AN INSTRUMENT FIRST, and this is the whole reason this block exists. Both fixes need to
   know what the speed did AROUND a fire, and nothing logs that:
     - `car_mtx` is 50 ms, and a latency sweep over it separates the two populations at exactly
       ONE window - the same window used to LABEL them. Circular. At every other window the
       populations overlap by 16-27 events. That cadence cannot support the test.
     - `K_STEER`/`K_TELEM` speed carries `(++s_lp & 0x1F) == 0`, i.e. one sample per ~0.49 s.
       The shape v7.49 measured lives on ~130 ms.
   So a proposal built on either would be a guess wearing a number. `..\..\docs\TEST-METHODOLOGY.md`:
   a run that tests X carries the instrument that records X.

   WHAT THIS RECORDS. On every sharp and every soft nudge, the tick-rate speed trace either
   side of the fire: TRACE_PRE ticks kept in a ring and dumped at the fire, then TRACE_POST
   ticks as they happen. Three series per tick - the RAW speedometer, the EMA the detector
   actually arms on, and the ground speed from world position - which is exactly the three-way
   disagreement both complaints are about.

     0xE8  a = the fire code (0xC0 / 0xC1)   b = magnitude   c = drop*1000   d = spdEma*1000
     0xE7  a = tick index, SIGNED, -TRACE_PRE..+TRACE_POST-1 (0 = the firing tick)
           b = spd*100 (raw)   c = spdEma*100   d = gsNow*100

   Cost: 65 records = 1.6 KB per fire, ~20 fires in a drive. Nothing is read back by the mod. */
#define TRACE_PRE    16u    /* ticks kept before a fire (~250 ms at the 15.4 ms poll) */
#define TRACE_POST   48u    /* ticks logged after  (~740 ms), enough for the +0.7 s test */
#define TELE_DROP    40.0f   /* drop above this m/s = respawn/level-load teleport, ignore */
/* v7.3 ground-speed VETO: a crash kick fires only if the car's REAL ground speed
   (d/dt world position - clean, no wheel oscillation) also dropped. Kills the false
   high-speed kicks (race car's wheel speed 0x2A0C spikes -> phantom MAX kicks on the
   straight at 120 km/h; 16/48 in the race log had steady ground speed). Verified on 3
   datasets: removes the false ones, keeps real impacts + braking-drops. */
#define GS_LOOK          6       /* ticks (~48ms) for the ground-speed window            */
#define GS_RING_SZ       40      /* ~320ms history of ground speed for the pre-crash max */
#define GROUND_CRASH_MIN 2.5f    /* m/s: (legacy, superseded by CRASH_GS_DROP below)          */
#define GS_SHORT_N       19      /* ticks (~150ms) for the TIGHT crash-gate ground window     */
#define CRASH_GS_DROP    4.0f    /* m/s ground drop over ~150ms to allow a crash (v7.4o).     */
#define CRASH_MIN_GS     2.0f    /* v7.4p: car must be MOVING - kills parked wheel-noise kicks */
#define BRAKE_RATIO      1.6f    /* v7.4p: long-drop > short-drop*this = SUSTAINED (braking)   */
#define SLIP_SOFT_DEG   20.0f    /* v7.4p: above this slip = sliding -> soft (not sharp) crash */
#define BRAKE_SOFT_MAG   2200    /* v7.4p: soft curb-like lockup nudge for braking/slide       */
#define BRAKE_SOFT_MS     130    /* v7.4p: soft nudge duration                                 */

/* v7.4q: crashes raised because the (now much richer) curb/tram/offroad wallow was MASKING
   weak crashes. MAG_MIN lifts the LOW end most (user asked +30-50%% there), the scales lift the
   whole curve ~+30%%. SAFE for braking: braking/slide go down the SOFT branch with its own
   BRAKE_SOFT_MAG - this formula only feeds the SHARP (real-impact) branch.
   v7.4r user-tuned targets: weak (excess .2 @6 m/s) ~7300, mid (excess 1 @12) ~18000,
   strong unchanged (still caps at MAX_MAG 25000). */
#define MAG_MIN              1250  /* floor so a weak crash still clears the ~3000 wallow ceiling */
#define MAG_EXCESS_SCALE  7600.0f  /* severity term                                            */
#define MAG_SPD_SCALE      755.0f  /* speed baseline                                           */

#define DUR_BASE_MS   80
#define DUR_PER_DROP  30.0f
#define DUR_CAP_MS    400

#define CALM_THRESH   1.5f

/* v7.37 persistence-confirmed bypass. See the block that uses these. */
#define BYPASS_X          1.5f   /* raw drop this many times its requirement = unmistakable */
#define PERSIST_MS         48u   /* two phantom-spike lifetimes; they last 16-24 ms          */
#define BYPASS_MAG_SCALE 700.0f  /* raw m/s -> force. 5 m/s -> ~4750, 20 m/s -> ~15250       */

/* v7.41: minimum samples of the short window that must sit within 80% of the claimed entry
   speed for that entry to be believed. An engine spike occupies 2-3 (it lives 16-24 ms); a
   real pre-impact speed occupies 6+. Logged on every fire and veto so the first drive can
   confirm or retune it - the old logs cannot (per-sample gs was never recorded). */
#define GS_OCC_MIN          4

/* ---- v7.48: PHANTOM CUT - kill the tail of a kick whose speed drop never happened ----
   His v7.46 drive, t=31.5 s on a straight road: spdEma 97,97,94,95,95,96,**84**,101,106,104
   km/h - a one-sample dip that FULLY RECOVERS, the engine artefact recorded in
   [[phantom-crash-spikes]]. The car never lost speed, but the normal branch fired 25000 (the
   force scales with speed, and he was at 90 km/h) for DUR_BASE_MS + drop*30 = 161 ms.

   Zero added latency: the kick fires immediately as always. At +CUT_MS we look back - if the
   speed has returned to within CUT_FRAC of what it was BEFORE the drop, no collision
   happened, so the kick is cut short.

   Calibrated on EVERY archived drive that logs the 8 ms telemetry, city and racing, n=34 real
   crashes (src/cut_calibrate2.py). The test must be FRACTIONAL, not absolute m/s: a slow city
   crash loses few m/s but a large fraction of its speed, while a fast phantom recovers a large
   m/s but a tiny fraction. In absolute terms the two populations OVERLAP (phantom 0.91 m/s
   still-down at +80 ms vs real crashes at 0.69-0.87) - the same trap invariant 2 hit.

     +80 ms:  phantom recovered to 3.3% still-down;  closest REAL crash 7.9%
   Threshold 5% sits between, 1.6x under the closest real crash and 1.5x over the phantom.
   Cutting at 80 ms halves the phantom's 161 ms kick; the peak is unchanged, only the tail
   goes - stated plainly because that is the known limit of this approach. */
#define CUT_MS            80u
#define CUT_FRAC        0.05f

/* ---- v7.42: THE POSITION RING GLITCHES, AND ONLY THE BYPASS BELIEVES IT ----
   The one phantom class v7.41 still fired. Measured on his loved v7.41 drive
   (logs/2026-07-24-v741-REFERENCE, src/v742_entry_vs_ema.py), every bypass arm:

     t=1032.0 s  entry  91.92 m/s (331 km/h) while the speedometer peaked at 40.06
     t=1229.9 s  entry 540.68 m/s (a mission seam / teleport)      peak 87.76
     t=1282.6 s  entry  69.93 m/s (252 km/h)                       peak 35.12
     8 real hits entry 14.84 - 27.82                               peak  9.25 - 13.64

   `gsShortMax` comes from WORLD POSITION; `spdEma` is the car's OWN speed. A real
   entry speed was reached on both. A position glitch claims one the speedometer
   never showed. Two independent sources, so compare them - but by DIFFERENCE, not
   by ratio: the ratio is REFUTED for the third time here (the 1032 phantom sits at
   2.29, inside the real band 1.41-2.04, because at the moment of arming the car has
   already lost speed while the ring still holds its pre-impact peak).

     real  fires: entry exceeds the speedometer's own 150 ms peak by  +4.29 .. +14.18
     phantoms   : by +34.81, +51.86, +452.92

   Cut at 25.0 - 1.8x above the worst real, 1.4x below the closest phantom.
   Plus an absolute 60 m/s (216 km/h) sanity cap; no Mafia car travels that.

   BYPASS ONLY. The same test on the NORMAL branch would veto two REAL crashes he
   felt - 760.7 s (entry 70.22, +33.37 over the peak, EMA collapsed 37.7 -> 15.4,
   force 25000) and 966.5 s (+28.30, 23.7 -> 0.5). A hard crash genuinely disturbs
   the position ring. The normal branch survives it because its force comes from
   spdEma; only the bypass derives force FROM the entry, which is why a glitched
   entry there both fires the kick and pegs it at 25000. */
#define BYPASS_ENTRY_MAX   60.0f  /* m/s. No car in this game does 216 km/h.        */
#define BYPASS_ENTRY_OVER  25.0f  /* m/s the entry may exceed the speedometer peak  */

/* ---- v7.49: THE END-OF-STOP JAB. Reported "phantom jerks", 2026-07-24 ----
   He reported it on v7.48 and then AGAIN on v7.41, the build he loves - so it is not
   one of my regressions, it has always been there, and rolling back cannot fix it.

   What it is, measured over 48 archived drives (src/bypass_after_nudge.py): of 469 sharp
   crashes, 40 came from the bypass, and their gap to the PREVIOUS soft nudge is bimodal
   with an empty band in the middle -
       29 fires at  94-235 ms      <- the jab he marks 9999
       11 fires at 11.5-64 SECONDS <- standalone events
        0 fires anywhere in 250-1000 ms
   The normal branch is the opposite: only 1.2% of its 429 sit within 250 ms of a nudge.
   So a bypass fire right after a nudge is not a delayed crash, it is a SECOND answer to
   the event the nudge already answered.

   Why they are not collisions (src/bypass_shape.py, same 40 events): the two populations
   have different deceleration SHAPES. Standalone fires have a discontinuity - the biggest
   single-tick speed drop is a median 54x the drop rate just before. The band fires are a
   smooth ramp, median 4.5x. The 8 ms traces show what that is: braking from ~117 km/h to
   a full stop, where the game roughly DOUBLES the deceleration over the last ~130 ms of
   the stop (~7 -> ~40 m/s2). Both detectors read that ramp-up as an impact.

   The gate is therefore SHAPE, not magnitude - deliberately, because magnitude-based
   discriminators have failed here five times (EMA-fall, entry/EMA ratio, raw-vs-speedo,
   sudden-vs-sustained, and the v7.45 braking gate; see [[analysis-traps]]).
   Effect on the archive: 23 of the 29 band fires vetoed, including EVERY jab he marked
   bad (step 2.5, 2.6, 3.7) and the v7.46 braking jab (2.4); 6 band fires with a real
   discontinuity KEPT (12.4 and up); all 11 standalone fires untouched, because the veto
   needs a nudge within BYPASS_NUDGE_MS. Margin: highest vetoed 7.9 vs lowest kept 12.4.

   Stated plainly, because it is the weak point: that is a 1.57x margin, thinner than the
   2.45x of the v7.42 entry gate. It is bounded by only ever applying to a bypass fire
   that follows a nudge - a ram on an open road can never reach this test. */
#define BYPASS_NUDGE_MS   400u   /* a nudge this recently = the same event, already answered */
#define BYPASS_STEP_MIN   10.0f  /* below this the deceleration is a ramp, not an impact     */
#define EMA_RING_SZ         48   /* spdEma per FFB tick; ~16 ms each, so ~780 ms of history  */
#define STEP_TAIL_N         13   /* the ~200 ms before the fire - where an impact lives      */
#define STEP_BASE_N         25   /* the ~400 ms before THAT - the rate it is compared with   */
/* v7.51: median deceleration over the base window above which the car counts as ALREADY
   BRAKING, so a low step ratio means it is a continuation of that trend, not an impact. Archive:
   p50 0.000, p90 0.050, and his lone 90 km/h lurch 0.150 - the largest of 202. */
#define RAMP_BASE_MIN     0.10f
/* v7.51b: a HARD FLOOR the shape gate may never cross, whatever the ratio says. Found by
   the full-archive audit requested for this check (src/v751_audit.py): the archive contains a REAL
   25000 impact with a step ratio of 2.3 and a base of 0.290 - a hard brake from 140 km/h
   into something. It survives today only because it arrived on the bypass, where a recent
   nudge is required. Nothing guaranteed the normal branch could not produce the same
   profile, so the guarantee is made explicit instead: a kick the LOCKED reference formula
   rates at or above this is never silenced by any shape test. Every jab he has marked bad
   was 2210-17325, so this costs nothing today and bounds the failure mode permanently. */
#define RAMP_NEVER_MAG    20000

/* ---- v7.58: the ENTRY speed for the normal crash branch (complaint #4) ----
   Ported from upstream v7.58 (gog-patcher 2026-08-03). Upstream's own reasoning, verbatim,
   because every constant here is a measured value and the measurement is what makes it safe.
   `RefCrashForce(excess, spdEma)` is handed the speed the car has been LEFT with, not the
   one it arrived at, so the hardest impacts get the smallest speed baseline. The bypass has
   always done it correctly with `pendEntry`; only the normal branch reads the collapsed
   value.

   ENTRY_GATE_MS is the whole reason this is shippable. Reported 2026-08-01: crash force must not
   change everywhere, categorically - a pile of drives went into tuning crash force at low
   speeds. Ungated, this strengthened 94% of every archived crash.
   Gated at 15 m/s it leaves 244 of 290 BIT-IDENTICAL - everything entering below 53 km/h,
   which is exactly the range his drives tuned - and still covers complaint #4, whose entry
   is 17.9 m/s. A gate at 18 m/s would already miss it, so the usable window is one step wide
   and was measured, not guessed (upstream src/spdema_collapse_price.py).

   ENTRY_LOOK_MS 1500 is likewise measured rather than chosen: at that window the recovered
   entry for complaint #4 is 17.9 m/s = 64.4 km/h, and the complaint audit had independently
   derived "entry 64 km/h" for that event from the speed trace. It saturates by 2500 ms.

   ENTRY_SANITY_MS is not optional. The entry speed is a PEAK over 1500 ms, so an engine
   speed spike enters it: the changed population in the archive runs to 316 km/h, which no
   car in this game reaches. v7.41 met this exact class at 540.7 m/s and answered it with a
   cap. Without it a glitch becomes a 25000 slam. Above the cap we fall back to spdEma, i.e.
   to exactly today's behaviour.

   The ring is indexed by REAL elapsed milliseconds, not by a tick count, because the loop
   runs at 15.4 ms and not the nominal 8 (see the poll-rate note in memory) - and because the
   offline pricing was done in real milliseconds off the log ticks. Same number both sides. */
#define ENTRY_GATE_MS      15.0f   /* below this entry speed nothing changes at all       */
#define ENTRY_LOOK_MS      1500u   /* how far back the pre-impact peak is taken           */
#define ENTRY_SANITY_MS    60.0f   /* above this the peak is a spike, not a speed         */
#define ENTRY_RING_SZ        104   /* ~1600 ms at 15.4 ms per tick                        */

#define DMG_MIN_MAG   4500  /* +1.5x */
#define DMG_SCALE     3000  /* +1.5x */
#define DMG_PULSE_MS  160
/* v7.4h bullet fix: Tommy-gun fire increments 0x29E4 in a rapid storm of TINY deltas; the
   old code fired a 7500-mag alternating kick on EACH -> the wheel shook violently L-R under
   gunfire. Split by delta: small deltas (bullets/light) become a faint RATE-LIMITED tap you
   can barely feel ("top-top-top"); only a large delta (a real collision) still gets the full
   kick. Set DMG_BULLET_MAG=0 to turn bullet feel off entirely. */
#define DMG_CRASH_MIN_DELTA   4    /* delta >= this = real collision damage -> full kick     */
/* v7.14 - THE BULLET TAP IS OFF. car+0x29E4 is NOT "damage to the car": it increments by
   exactly +1 for EVERY SHOT THE PLAYER FIRES. Proven from the v7.12b log - the user got
   out of the car and fired a pistol and a tommy gun, and the field counted 1,2,3,...25
   monotonically while the car stood untouched, producing 48 wheel taps in 27 s.
   An incoming bullet and one of his own shots both give delta = 1, so this channel cannot
   tell them apart, and no flag indicating the player is in a vehicle exists anywhere visible: the
   car pointer does NOT change on exit, and a supervised scan of the whole car struct AND
   the game global (on-foot window vs parked-inside windows) found zero fields that
   separate the two states. Candidates that looked promising and were refuted: car+0x0800
   (combat/alert - also 1 while driving), car+0x2B88 (also 2 while driving and in other
   drives), steering-freeze (steering still changes on foot).
   So the honest fix is to switch the channel off rather than guess. The real
   collision-damage kick (0xD0, delta >= DMG_CRASH_MIN_DELTA) is untouched - a single shot
   never reaches it. COST: the Tommy-gun feel the user had approved is gone until a real
   in-vehicle flag is found. Set this back to 1 to restore it.

   ---- v7.21, 2026-07-22: BACK ON. THE PREMISE ABOVE WAS WRONG. ----
   Everything above treats the fact that it also taps on the player's OWN shots as the defect. The user
   has now said plainly that this is FINE and in fact logical - he is sitting in the car, he
   fires, some feedback belongs in the wheel: "I do like this effect, because
   it is logical". Measured on the golden series-2 drive, car+0x29E4 incremented 14 and 10
   times in the two windows where he fired FROM INSIDE the car - i.e. the "false" taps this
   channel was switched off for are exactly the case he wants.
   The ONE real defect is the tap firing while he is OUT of the car, because the game reuses
   a single player-vehicle slot whose address never changes, so GetCarPtr() keeps returning
   the car he just left. That is task F9 and it is NOT fixed here: three independent scans
   (glob+0x0000..0x2000, the whole car struct, and all three dereferenced objects, over three
   labelled drives and two vehicle types) found NO in-vehicle flag in captured memory.
   Candidates that separate on one drive fail on the next because the struct RETAINS THE LAST
   VEHICLE'S STATE after exit - verified: car+0x02F8 and car+0x00B8 read the truck's values
   while he walked away from the truck.
   So this build knowingly ships with the on-foot defect present, with the user's explicit
   agreement, in exchange for being able to feel the tap again in the same drive that hunts
   the flag (see the glob roots 10/11/12 added below). */
#define DMG_BULLET_ENABLE     1

#define DMG_BULLET_MAG     1600    /* v7.4l: slightly stronger (user: "let us beef it up a little") */
#define DMG_BULLET_MS        30    /* short soft pulse per bullet tap                        */
#define DMG_BULLET_MIN_MS    60    /* v7.4l: faster -> CONSISTENT light buzz under fire       */

/* ---- v7.5 F5: INCOMING impact - being rammed while parked ----------------------
   The crash detector keys off the PLAYER car's own speed DROP, so a police car ramming
   a PARKED player produces nothing (0 -> 0, and the ground speed goes UP not down).
   F5 is a separate branch for exactly that case: the car's OWN speed field (0x2A0C) is
   still ~0 while the WORLD POSITION suddenly moves = something moved us, we did not.

   Thresholds verified OFFLINE before building (src/f5_fp.py) against 57 minutes of
   archived driving across 7 drives: with the own-speed gate below, driving away from
   rest NEVER passes (it raises 0x2A0C first), so the trigger can sit low enough to
   catch a GENTLE shove. The only archived hits were level-load teleports (6.7-47 m/s),
   which F5_TELE_VETO removes -> ZERO false positives at every trigger 0.5..3.0 m/s. */
#define F5_PARK_W       0.30f  /* m/s of OWN speed: below this the car counts as standing */
#define F5_HOLD_TICKS      50  /* ~400ms standing before F5 arms (POLL_MS=8)              */
/* v7.6: 0.50 -> 0.08. The golden labelled drive fired 18 times, 16 of them on a slowly
   CREEPING car (own speed 0.22-0.35) - a case no archived drive contained, so the original
   offline proof was blind to it. The two real shoves read own speed EXACTLY 0.00. Applying
   candidate gates to the logged values (src/f5_retune.py) gives a wide plateau: anything
   from 0.20 down to 0.02 keeps 2/2 real shoves and removes 16/16 creep kicks, so 0.08 sits
   mid-plateau rather than on a knife edge. The trigger itself stays at 0.50 m/s so a GENTLE
   push on a truly stationary car still fires - that was the user's actual request. */
#define F5_MOVE_W       0.08f  /* own speed must STILL be under this when the shove lands */
#define F5_TRIG         0.50f  /* m/s of ground speed = shoved (worst legit creep ~0.37)  */
#define F5_TELE_VETO    6.00f  /* above this it is a level-load teleport, not a ram       */
#define F5_MIN_MAG       3500  /* gentle push: light but clearly felt (weak crash ~7300)  */
#define F5_SCALE       6000.0f /* per m/s of shove above F5_TRIG                          */
#define F5_MAX_MAG      20000  /* hard ram lands between mid (17910) and strong (25000)   */
#define F5_BASE_MS        150
#define F5_PER_GS        60.0f
#define F5_CAP_MS         350
#define F5_REARM_MS      600u  /* one kick per shove, not a burst                         */

/* ---- v7.6 F3: the light-object tap (booths, bins, hydrants, signs) --------------
   SOLVED by the golden labelled drive. car+0x0014/0x0018/0x001C are a begin/end/capacity
   triple - a growing list of contacts. The END pointer advances by +4 (one appended
   pointer) every time the car hits a destructible prop:

       OBJ marks 14/15 = 93%   PED 0/15   RAM-HARD 0/5   GENTLE 0/8   OTHER 0/6

   93% matches the user's own independent estimate of 90-95% pressed correctly, and it also
   appended on both confirmed incoming shoves, so F5 and F3 corroborate each other.

   Fire on ANY change, not on delta==4: 7 of the 18 changes were list REALLOCATIONS (the
   base moves, giving a huge delta) happening at those same events. Every change in the
   whole drive was a real "something hit us", which is what a light tap wants.

   PEDESTRIANS are NOT here (0/15 on labelled data) - they are not in the car struct at
   all. That needs LS3DF; do not scan this struct for them again. */
#define CONTACT_END_OFF  0x0018u  /* contact-list END pointer: any change = a prop hit */

/* ---- v7.11 F4: the PEDESTRIAN tap ---------------------------------------------
   Found by the v7.10 vector watch on a drive with 25 labelled ped hits:
   **car+0x02F8/0x02FC/0x0300 is a SECOND vector** - begin/end/capacity - whose elements
   are 24 BYTES (the end pointer moves in +-24 steps), so probably contact manifolds
   (3 floats point + 3 floats normal). It covered 25/25 marked pedestrians, and it is
   DISJOINT from the prop list: 0 of the 6 prop taps in that drive moved it, so the two
   taps can never double-fire on one event.

   Distinguishing a RUN-OVER from a DODGE. The user explained the mechanic: a pedestrian
   first plays a jump-away animation, and only if you correct and hit them again do you
   actually run them over - so more people dodge than get hit, and he only pressed the key
   when sure. Both touch the list, but they have different shapes: a dodge is a single
   instantaneous blip, a run-over is a SUSTAINED contact (a body under the car) with
   several manifold updates. Measured on the labelled drive:
       rule >=2 contact updates in one episode -> 65% of run-overs kept,
                                                  69% of dodges suppressed, ~1 tap/6 s.
   The suppression number is a FLOOR: the "dodge" class also contains run-overs he did not
   mark. Requiring a minimum DURATION as well was tested and is worse (it drops to 56-59%
   of run-overs for no extra suppression), so the rule is the contact COUNT alone. */
#define PED_END_OFF     0x02FCu  /* END pointer of the 24-byte contact-manifold vector  */
#define PED_EPI_MS        600u   /* growths closer than this belong to one contact      */
#define PED_MIN_CONTACTS     2   /* >=2 updates = a body under the car, not a dodge     */
#define PED_GAP_MS        500u   /* one tap per pedestrian                              */
/* v7.13 - shaped to resemble a CURB, on the user's request and on measurement.
   At 4800 the ped tap out-punched the prop tap (delivered medians: ped 4176 vs prop 3524)
   which is exactly why the two felt identical - my error, not a false positive.
   The goal is for a pedestrian to read a bit more stretched out, like the feedback from a curb at 20-30.
   Measured from the archived drive where curbs were actually being driven (v7.4e):
   a real curb strike at 20-30 km/h peaks ~5300 (often clipping the 6000 cap) and lasts
   ~314 ms. The other drives' "curb" episodes are just deadzone noise (median ~400) and
   were excluded. So: LONGER and SMOOTHER, with the peak deliberately under the prop tap. */
/* v7.15: 3400 -> 4500, ABOVE the prop tap (4200) at every speed. User: "a bin is a bin, but
   a person is big". The curb-like stretched duration he approved is unchanged. */
#define PED_MAG           5400   /* v7.16: +20% again - even at low speed it was not enough */
/* v7.17: SHORTER and SHARPER. With curbs and roll restored, the road wallow is back and a
   long soft swell simply sinks into it - the user: "the wallow creates a large share of the feedback,
   and somehow pedestrians do not get felt". Against a continuous low-frequency background what makes
   an event stand out is the RATE OF RISE, not the amplitude, so duration comes down and the
   attack comes down much harder. Peak deliberately NOT touched - force was not the
   complaint this time, and raising it would just add to the mush. */
#define PED_MS_LO          320   /* was 500 */
#define PED_MS_HI          130   /* was 200 */
#define PED_ATK_LO          80   /* was 165 - this is the main "sharper" lever            */
#define PED_ATK_HI          35   /* was 70 */
#define PED_MAG_LO_SCALE 0.70f
#define PED_SMOOTH           1   /* smoothstep the envelope: rounds the triangle's corners
                                    so it swells like the EMA-smoothed curb channel rather
                                    than stabbing. The PROP tap keeps its approved triangle. */
/* Force history: 1600 (v7.6, described as being hit by a gunshot - far too weak) -> 7000
   (v7.7, the 4-5x he asked for) -> 4200 (v7.8, user: 40% weaker than 7000). Sits between
   the bullet tap (1600) and the weak crash (~7300), which is where a knocked-over booth
   belongs. The BULLET tap stays untouched by explicit request: this game is about driving,
   not shooting, so a collision must outweigh a bullet hit. */
#define F3_TAP_MAG         4200   /* v7.8: 7000 - 40%                                   */
#define F3_MIN_GAP_MS      150u   /* one tap per hit, not a burst on a multi-contact    */

/* v7.9 SPEED-SHAPED TAP. User after the v7.8b drive: the hit is too sharp, and knocking a
   bin over at 10 km/h delivers exactly the same stab as doing it at 40. He wants the low
   speed case STRETCHED in time - closer to a curb, moving a little with a soft poof -
   while a fast hit stays a single thud and that is it.
   So duration, attack and peak all interpolate on speed, and the pulse is shaped by a
   triangular envelope recomputed every tick instead of being a flat block of force.
   Measured tap speeds in that drive: 0.8 .. 41 km/h, median 24 - so the knees sit at 10
   and 45 km/h, which puts the median right in the middle of the blend. */
#define F3_SPD_LO       10.0f  /* at or below: softest and longest                     */
#define F3_SPD_HI       45.0f  /* PEAK reaches maximum here                            */
/* v7.9b: TWO knees, because duration and force are not the same complaint. The user rated
   the slow taps "the coolest" but said 30 and especially 45 km/h nearly vanish, and that
   the fix is not so much about force as about duration. With one shared knee at 45
   the duration collapsed to 55 ms exactly where he still wants to feel something. So the
   PEAK keeps its old mapping (unchanged at every speed) while DURATION and ATTACK now run
   out to 60 km/h - at 30 km/h that is 156 ms instead of 125, at 45 km/h 108 ms instead of
   55, and only past 60 does it become the short tuk he agrees is natural at speed. */
#define F3_SPD_HI_DUR   60.0f  /* DURATION/ATTACK reach their floor only here          */
#define F3_MS_LO         220   /* stretched - not as long as a curb, but in that family */
#define F3_MS_HI          60
#define F3_ATK_LO         60   /* ms to reach peak at low speed = a swell, not a stab  */
#define F3_ATK_HI         12   /* ms at speed = sharp                                  */
#define F3_MAG_LO_SCALE 0.65f  /* a slow knock is gentler as well as longer            */

/* ---- v7.10 VECTOR WATCH: hunting the PEDESTRIAN event -------------------------
   Peds are NOT in the prop contact list (23 labelled hits, 2/23, single vtable). But a
   SECOND begin/end/capacity triple sits at car+0x0154/0x0158/0x015C with the same
   signature - heap pointers, END moving in +-4 append steps - and three of its moves in
   dataset #1 land next to PED marks with correct reaction-lag ordering.

   It cannot be judged from car_snap: at 50 ms a brief contact grows and shrinks between
   two snapshots and nets to zero, which is exactly what hid the prop list twice. So poll
   the whole header region at 8 ms and log anything that moves like a vector.

   Filter (keeps floats and churn out): both old and new look like heap pointers, 4-byte
   aligned, and the delta is small - or one side is 0 (list created/destroyed). Measured
   offline on dataset #3 first: ~10 qualifying changes/s over this window, 340 offsets.
   Channel 0xCA: a = car offset, b = old, c = new. */
/* ================= v7.12 ISOLATION BUILD - TEMPORARY, DIAGNOSTIC ONLY =================
   The pedestrian effect cannot be judged while the road channels are talking over it. The
   user's own reasoning: the ROLL channel fires not just on tram rails but whenever the car
   rocks - including driving along the PAVEMENT, which is exactly where pedestrians are. So
   the two are inseparable by ear until the road channels are quietened.

   BOTH road channels are now at ZERO (v7.12b - 30% roll still got in the way), applied as
   MULTIPLIERS at the point of use, so
   every user-approved REFERENCE constant (F2P_GAIN 98000, F2R_GAIN1 585000, ...) stays
   physically intact in this file. **To restore the approved feel: set both scales to 1.0.**
   Nothing else needs touching. This is not a tuning change - it is a measurement fixture.
   ===================================================================================== */
/* v7.16: BOTH RESTORED TO 1.0 - the isolation fixture is over. Curbs and roll are back at
   their approved reference strength. The multipliers stay in the file as the mechanism for
   any future isolation test; they must read 1.0f in every shipping build. */
#define F2P_ISO_SCALE   1.00f  /* curbs at full approved strength                       */
#define F2R_ISO_SCALE   1.00f  /* tram/offroad roll at full approved strength           */

#define VW_LO        0x0280u   /* v7.11: narrowed - the ped list was FOUND, keep only  */
#define VW_HI        0x0320u   /* its neighbourhood as verification for the next drive  */
#define VW_MAX_DELTA   4096
#define VW_PER_TICK       6    /* never stall the FFB thread                            */
#define VW_BUDGET     20000u

/* ---- v7.5 MARK keys stay (diagnostic) -----------------------------------------
   Kept for the next labelled drive - F6 (the police PIT spin) needs its own labels, and
   the 8ms FLAG WATCH from v7.5 is gone: it returned a clean null result (only 4 of 30
   ped/object marks moved anything in its windows) and the supervised scan of the full
   snapshot replaced it. */

/* ---- steering feel tuning (DI units, 0..10000 nominal) ---- */
/* Spring and damper are scaled proportionally together so spring/damper ratio
   (= return speed) stays constant while absolute forces change. */
#define K_CENTER_DI        4500  /* cruise spring (+50% from 3000 to match damper)  */
#define K_HISPEED_DI       2250  /* extra spring 80-120 km/h (proportional)         */
#define CENTER_FULL_KMH   50.0f  /* spring at 100% above 50 km/h                   */
#define K_DAMP_DI_STATIC  20000  /* standstill damper (unchanged, felt correct)     */
#define K_DAMP_DI_MOVING   5250  /* cruise damper +50% from 3500 (heavier feel)     */
#define K_DAMP_DI_HISPEED  8250  /* hispeed damper proportional to K_HISPEED_DI     */
#define DAMP_FADE_KMH     10.0f  /* standstill damper fades by 10 km/h              */
#define HISPEED_DAMP_LO_KMH 80.0f  /* hispeed ramp matches spring hispeed start     */
#define HISPEED_DAMP_HI_KMH 120.0f
#define HISPEED_LO_KMH    80.0f
#define HISPEED_HI_KMH   120.0f
#define MAX_STEER_SAT    10000   /* spring/damper saturation (max force they output) */

/* ---- v7.23 F6: being SPUN (police PIT maneuver) ----
   Design and numbers were settled and logged in a 2026-07-23 review.
   Two earlier versions are refuted: the anti-causal window cannot be built, and the plain
   causal trailing integral false-fires 2.8-8.4 times a MINUTE on ordinary hard cornering.
   Anchoring on an impact is impossible too - a PIT ram fires no crash event at all (only
   2 of 4 labelled PITs had any 0xC0/0xC5 within 7 s, and both of those were a later wall
   collision, not the ram).

   What survives: the trailing integral PLUS the two things that separate a ram from a
   corner, which had never been tried together -
     STEER : hard cornering happens at 0.63-1.0 full lock; being rammed does not.
     RATIO : |uncommanded rotation| / |commanded rotation| over the same window. A corner
             turns roughly as asked (ratio ~1); a PIT turns while nothing was asked.
   Plus a minimum speed, because near standstill expectedYaw ~ 0 makes the ratio explode.
   At 50 deg with all three: 4/4 labelled PITs caught, versus 2.8-8.4/min ungated.

   THE FALSE RATE WAS RE-MEASURED AGAINST THIS EXACT STATE MACHINE and it is NOT the
   0.17/min the numpy design pass reported. That pass scored each threshold episode ONCE, at
   its onset; the live machine re-checks the gates every tick, so an episode whose onset
   failed a gate still fires later when they all line up. Replayed properly
   (src/v723_verify.py): 0.62/min at an 800 ms re-arm, and 0.41/min at 2500 ms - which is
   why the re-arm below is 2500 and not the 800 the design proposed. One kick per spin
   instead of a train of them, and a third fewer false kicks, at no cost in PITs (still
   4/4). Roughly one spurious kick per 2.4 min of hard driving; every one inspected is a
   genuine large uncommanded rotation at speed, not noise.

   CADENCE is not a worry here even though the design was measured on 50 ms samples: the
   integral of yawRate over a window telescopes to the NET HEADING CHANGE, which is
   identical at any sample rate. Only the commanded term is a true numeric integral, and it
   is smooth. This is what makes F6 safe where the v7 BUMP kick (which thresholded the noisy
   per-tick disturb) was not. Heading dropouts still break the telescoping, so a tick with
   an invalid matrix RESETS the accumulator rather than pretending continuity. */
/* v7.27 - THE SPIN CHANNEL IS OFF. Rejected after driving v7.26, and the log agrees
   with him harder than he put it: the channel was live for 244.4 s of a 656.8 s drive
   (37.2%), its longest single "spin" lasted 47 SECONDS, and it was twice as likely to be
   fighting the F1 slip lightening as the rest of the drive was. His words: it behaves as a
   COUNTER-STEER ASSIST, it fires when he pulls the handbrake with no impact at all, and in a
   slide the wheel first empties, then punches your hands, and feels ragged.
   This is not a threshold to retune - an effect that runs a third of the time is the wrong
   SHAPE. Two faults, both mine:
     * the hold refreshed itself on any continuing rotation, so once armed it never released;
     * arming was never tied to an EXTERNAL event, and a handbrake slide produces exactly the
       signature it looked for (yaw the steering did not ask for), which
       the very first design note already warned about.
   Re-enable only with: arming on a detected LATERAL IMPACT, a hard maximum duration, and a
   slide gate. The v7.26 drive is the first that can calibrate the lateral signal at 8 ms. */
#ifndef PIT_ENABLE
#define PIT_ENABLE          0
#endif
#define PIT_WIN_TICKS     250    /* 2.0 s trailing window at POLL_MS = 8                  */
#define PIT_THRESH_DEG   50.0f   /* uncommanded rotation needed to fire                   */
#define PIT_STEER_MAX     0.60f  /* the wheel was not wound on                            */
#define PIT_RATIO_MIN     1.80f  /* turned this much more than commanded                  */
#define PIT_SPD_MIN       4.00f  /* m/s - a PIT happens while driving                     */
/* v7.25: the pulse constants are GONE. The spin is a sustained force now, so what it needs
   is a rate-to-force law and time constants, not a magnitude and a duration.
     SPIN_RATE_A     smoothing of the rotation rate that DRIVES the force. 0.06 at 8 ms is
                     ~110 ms - enough to kill the yaw noise that made the v7 bump branch
                     unusable, fast enough to follow a real spin.
     SPIN_GAIN       force per rad/s of uncommanded rotation past the deadzone. A brisk spin
                     runs 1.5-3 rad/s, so 7000 puts it at 10000-20000: firm, under a crash.
     SPIN_SMOOTH_A   the RAMP. 0.03 at 8 ms is ~260 ms to full - this is the constant that
                     makes it a lean rather than a punch, and the first one to change if he
                     says it arrives too fast or too slowly.
     SPIN_HOLD_TICKS how long the channel stays live after arming (2 s), refreshed while the
                     car is still turning. Without a hold the force would cut the moment the
                     2 s integral fell back under threshold - the abrupt edge he objected to. */
#define SPIN_RATE_A       0.06f
#define SPIN_DEADZONE     0.25f  /* rad/s below this is ordinary driving noise             */
/* SCALE, set from the offline replay rather than guessed (src/v725_verify.py).
   The first numbers (gain 7000, cap 20000) put a peak of 19698 on his BEST-EVER reference
   drive - a 2 s sustained force just under MAX_MAG. That is the wrong order of magnitude for
   a SUSTAINED effect: everything else that holds the wheel between kicks is the road wallow,
   capped at 3000 (roll) + 6000 (pitch). A lean three times stronger than anything sustained
   that was ever felt would not read as confident and smooth - it would read as the wheel being
   taken away. Kicks may be that big because they last 60-400 ms; this one lasts seconds.
   So the cap sits just above the wallow scale and below a mid crash (~17900). */
#define SPIN_GAIN         5000.0f
#define SPIN_CAP          12000
#define SPIN_SMOOTH_A     0.03f
#define SPIN_HOLD_TICKS     250  /* 2.0 s at POLL_MS 8 */
#define SPIN_RELEASE_RATE 0.40f  /* still turning faster than this -> keep the hold alive   */

/* ---- v7.28 F6, THIRD DESIGN: self-aligning torque, driven by the SLIP ANGLE ----
   The first two are dead and the data killed them, not taste:
     * a fixed kick - he asked for a steer, not a punch;
     * a force driven by the uncommanded rotation RATE - it ran 37% of a drive, its longest
       "spin" lasted 47 s, and it duelled with the F1 slip lightening (the wheel empties, then
       punches your hands, ragged").
   The natural fix looked like arming it only on a real impact, and that is now REFUTED too:
   F1 lightening is engaged in **14 of his 14 labelled PIT spins**, because a PIT spin IS a
   slide. Any gate that protects his handbrake slides also kills every real PIT. And neither
   lateral velocity (PITs sit at the 79-99th percentile of ordinary driving) nor yaw
   acceleration (11/14 at 2.5 false fires per minute) separates an external hit cleanly.

   So stop detecting events. What a real car does needs no detector at all: the front wheels
   are pulled toward the direction the car is actually travelling, by an amount that grows
   with the SLIP ANGLE - the angle between where the nose points and where the car is going.
   That is zero in normal driving by construction, so there is nothing to false-fire.

   Why this also fixes "ragged": the slip angle is GEOMETRY and changes smoothly, while yaw
   rate at 8 ms is the noisy signal that made the old v7 bump kick unusable. Same reason the
   force can be gentle and continuous instead of a pulse.

   Measured before building: the car is beyond SAT_DZ_DEG for 1.6% of the approved object-tap
   reference drive and 0.8% of the fire-truck drive - so on the reference feel this is silent
   almost all of the time, versus the 37% the rejected design managed. */
#ifndef SAT_ENABLE
#define SAT_ENABLE          1
#endif
/* THE TYRE CURVE, not a ramp. A real self-aligning torque rises with slip angle only up to
   the tyre's peak and then FALLS as the contact patch saturates - which is exactly why a
   deep slide feels lighter at the wheel than a controlled drift, and why a driver can tell
   the two apart with their hands. A force that kept rising to a flat cap would say the
   opposite. So: rise to the peak, then decay toward SAT_TAIL by the time the car is properly
   sideways. It also makes a full spin gentler than a drift, which is the safe direction. */
#define SAT_DZ_DEG       15.0f   /* below this the car is simply cornering - no force        */
#define SAT_PEAK_DEG     35.0f   /* the tyre's peak: maximum self-aligning torque            */
#define SAT_TAIL          0.35f  /* fraction of the peak left once fully sideways            */
#define SAT_MAX_DEG     150.0f   /* beyond this the car is travelling BACKWARDS, not sliding */
/* The cap now comes from the ACTIVE PROFILE (F1..F4), so he can A/B it mid-slide. The value
   below is only the design reference: wallow-scale, NOT kick-scale, because he asked for a
   force that insistently twists the wheel, not one that punches your hands - a weak crash kick
   is ~7300 for comparison. */
#define SAT_CAP           4000
#define SAT_MIN_SPD       4.0f   /* m/s - below this the course direction is meaningless     */
/* ASYMMETRIC, and this is the fix for the one thing he disliked about every strength.
   v7.28 used one constant for both directions (~530 ms), so the force LINGERED after the
   slide ended - measured: 71% of all force samples sat at slip below the 15 deg deadzone,
   still pushing while he was already driving straight, fighting the centering spring. His
   words for it: the car keeps turning too slowly and centering while driving straight.
   A tyre regains grip far faster than it loses it, so the release should be much quicker than
   the attack. Attack stays gentle (he asked for a lean, not a hit); release is ~3x faster. */
#define SAT_SMOOTH_A     0.015f  /* ~530 ms to full at 8 ms: a lean that arrives, not a hit  */
#define SAT_RELEASE_A    0.050f  /* ~160 ms back to nothing once the slide is over           */
#define SAT_SIGN            1    /* flip if it pushes the wrong way - one character          */

/* ---- v7.25 F10: SIDE IMPACT - being shunted sideways / cut up ----
   Measured, not suspected: on his labelled control run
   (logs/2026-07-23-v722-CONTROL-PIT-LABELLED) he marked SIX sideswipes with the 8 key, and
   FOUR of them produced no impact feedback at all - only unrelated pedestrian taps. The
   cause is structural, and it is the same one that makes a police PIT ram produce no crash
   event: the crash branch fires on a sudden drop in FORWARD ground speed, and a side shunt
   barely changes forward speed.

   The signal needs no new addresses and no matrix. Take the velocity vector from the
   position ring over a short window, and the same vector one window earlier; the CHANGE
   between them splits into a component along the old direction of travel (that is what the
   crash detector already sees) and one perpendicular to it (that is the shunt). Detect on
   the perpendicular part being both large AND larger than the longitudinal part - a normal
   crash is longitudinal, a normal corner builds its lateral change gradually rather than in
   one 50 ms step.

   THE SIGN IS FREE AND WORTH HAVING: the cross product says WHICH SIDE was hit, so the wheel
   can be pushed the way the car was actually shoved instead of the alternating flip every
   other kick in this file uses.

   SIDE_TRIG IS UNCALIBRATED - there has never been a lateral measurement in this project.
   0xC4 therefore logs EVERY lateral event above a low floor, fired or not, exactly as 0xC6
   does for F5 near-misses, so the threshold can be set from the next drive's log instead of
   from another guess. */
/* v7.26 - THE KICK IS OFF BY DEFAULT, THE MEASUREMENT IS NOT.
   v7.25c shipped SIDE_TRIG = 1.50 m/s as an admitted guess. It fired 33 times in 96.8 s
   (20.4/min) and marks noting this is wrong land within a second of those kicks. The live
   0xC4 diagnostic then showed the guess sat on the 90th PERCENTILE OF ORDINARY DRIVING - it
   was never detecting impacts at all.
   Calibrating offline against his six hand-labelled sideswipes (src/side_calibrate.py) did
   not rescue it either: at the archive's 63 ms cadence a lateral-only rule catches 5 of 6 at
   2.76 false fires per minute, or 2 of 6 at zero. A gate based on a sudden step after a calm stretch -
   the trick the head-on crash branch uses - does NOT separate them: his sideswipes score
   1.03-2.33 against their own recent history, i.e. no spike at all.
   AND THE NOISE FLOOR IS CADENCE-DEPENDENT: median |lat| is 0.09 at 63 ms but 0.94 live at
   8 ms, so a 48 ms window at 8 ms is mostly position-quantisation noise and no threshold
   measured at one rate transfers to the other.
   So: the window is lengthened to ~160 ms to get out of the noise, the kick is DISABLED, and
   0xC4 keeps logging every lateral event unsampled. One drive with deliberate sideswipes
   marked `8` then sets the threshold from data measured at the rate the build actually runs.
   Guessing it twice would cost another drive; this project's own rule is to do the maths
   first. */
/* ---- 2026-08-04: THE NAMES NOW SAY IT, because the comment above did not stop the question.
 * Asked, after the v7.58 drive, why zero lateral kicks fired against 19 650 logged events,
 * and then: settled on renaming the stub so the name itself shows at once that it must not
 * be switched on. So `SIDE_ENABLE` and `SIDE_TRIG` carry `_UNCALIBRATED_TESTONLY` in their names -
 * a `#define SIDE_ENABLE 1` on a build line now reads as what it is.
 *
 * HIS DRIVE IS THE FIRST REAL MEASUREMENT OF THIS DISTRIBUTION, and it condemns the placeholder
 * outright. 534.8 s, 19 650 events above the 0.30 log floor:
 *     median |lat| 1.32   p90 3.98   p99 6.12   max 8.08
 *     |lat| > |lon| in 11 492 of 19 650 events - i.e. the ratio test alone excludes nothing
 *     at the placeholder 2.20 the kick would have fired ~5 900 times, about once a second
 * The guess is not "close but untuned", it is an order of magnitude out, exactly as v7.26
 * found for the previous guess of 1.50. Numbers kept here rather than in a memory file: this
 * is where the next person changes the constant.
 *
 * The `#error` below is deliberate and is the part a rename cannot do. Enabling the kick now
 * requires ALSO defining SIDE_TRIG_CALIBRATED, which is a statement that a drive was measured -
 * not something anyone types by accident. */
#ifndef SIDE_ENABLE_UNCALIBRATED_TESTONLY
#define SIDE_ENABLE_UNCALIBRATED_TESTONLY   0
#endif
#if SIDE_ENABLE_UNCALIBRATED_TESTONLY && !defined(SIDE_TRIG_CALIBRATED)
#error "the lateral kick is UNCALIBRATED: at the placeholder trigger it fires about once a second (measured, 2026-08-04, 19650 events). Calibrate it against a drive with marked sideswipes and define SIDE_TRIG_CALIBRATED, or leave it off."
#endif
#define SIDE_WIN            20   /* ticks per velocity window (~160 ms at POLL_MS 8)        */
#define SIDE_TRIG_UNCALIBRATED_TESTONLY 2.20f  /* PLACEHOLDER - see the block above         */
#define SIDE_RATIO        1.00f  /* lateral must exceed longitudinal -> not a normal crash  */
#define SIDE_MIN_SPD      1.50f  /* m/s: below this the direction of travel is meaningless  */
#define SIDE_REARM_MS      400u
#define SIDE_MS            200u
#define SIDE_MAG_BASE      6000
#define SIDE_MAG_PER_MS    4000  /* per m/s of lateral step above the trigger              */
#define SIDE_MAG_CAP      18000  /* below MAX_MAG: a head-on crash stays the bigger event   */
#define SIDE_LOG_FLOOR    0.30f  /* log EVERY lateral event above this - this is the point  */

/* ---- v7.25: TRUE 8 ms TELEMETRY (log code 0x75) ----
   The question, and it was the right one: what is the point of archives at 50 ms when the
   thing being designed runs at 8 ms? Every rotation analysis in this project so far has been
   done on car_mtx, written by the SNAPSHOT thread at SNAP_MS = 50 - six times coarser than
   the FFB thread that actually decides the forces. That is why every design document here
   carries a caveat to re-check against live 8 ms telemetry before shipping, that was never
   dischargeable: the data did not exist.

   It does now. One record per FFB tick with the four quantities every rotation feature is
   built on, so an offline replay can run at exactly the rate the build runs at.
     a = yawRate * 10000      (signed, rad/s - actual rotation)
     b = expectedYaw * 10000  (signed, rad/s - what the steering asked for)
     c = steer * 1000         (signed) | (spd * 100) << 16   packed, both fit
     d = spinForce            (signed) - what the wheel was actually given
   125 records/s x 24 B = 3 KB/s, ~1 MB for a six-minute drive. The MAX_REC cap is 200000
   records, which at this rate is 26 minutes - longer than any drive so far, and the cap is
   graceful (logging stops, the mod keeps running). */
#ifndef TELE8_ENABLE
#define TELE8_ENABLE 1
#endif
/* 2026-07-24 gear-probe channel (log code 0x76). ON by default for the auto-vs-manual test
   drive; costs one guarded read + one GetAsyncKeyState sweep per 8 ms tick and writes nothing
   when GEAR_OFF is 0 (an unrecognised build). Set to 0 to drop the channel entirely once the
   automatic-gearbox question is settled. */
#ifndef GEAR_ENABLE
#define GEAR_ENABLE 1
#endif
/* Which way the wheel is yanked for a given rotation sign. The mapping from SetMag's sign
   to a physical direction is NOT established offline - the only other directional kick in
   this file (the v7 bump branch) was disabled before it was ever felt. If the kick pushes
   the wrong way on the test drive, flip this to -1; nothing else changes. */
#define PIT_SIGN            1

/* ---- longitudinal slip (wheelspin) detection ---- */
/* raw wheel speed minus slow EMA = slip excess. EMA oscillation noise is ~5 m/s at
   high speed, so threshold at 6 to ignore it. Genuine wheelspin (burnout, oversteer)
   produces 10-30 m/s excess and reduces spring/damper so wheel goes light. */
/* Data from v6.16 test: noise max +1.0 m/s, wheelspin +6..+8 m/s. Big gap.
   Floor at 3.0 keeps clear of noise and activates earlier in spin.
   Crash = negative excess (raw drops, EMA stays) - NOT handled here (would fire on impacts).
   Only POSITIVE excess (wheel spinning faster than vehicle) reduces spring/damper. */
/* v6.17 test data: noise max +1.0 m/s, wheelspin +3..+6 m/s. Big gap → floor at 3.0 safe.
   v6.17 K=0.08 gave only 5-22% reduction → imperceptible on DD wheel.
   v6.18: K=0.25, floor=0.15 → at +5.7 m/s excess: mult=0.325 (67% reduction).
   Equivalent to spring dropping from 48 km/h level down to below 25 km/h level. */
#define SLIP_EXCESS_FLOOR  3.0f   /* m/s: well above +1.0 noise, well below +3 spin  */
#define SLIP_REDUCTION_K   0.25f  /* spring/damper reduction per m/s above floor      */
#define SLIP_MULT_MIN      0.15f  /* minimum multiplier: wheel goes very light at max  */
#define SLIP_EXCESS_MAX   30.0f   /* sanity guard: above this is stale pointer garbage */

/* gog-patcher fork: no longer a constant. Selected at DllMain from the build table near
   VA_CAR_PROBE below; 0 means "unrecognised exe" and the mod disables itself. */
static DWORD VA_GAMEPTR = 0;
#define DIEP_NORESTART 0x40000000
#define DI_INFINITE 0xFFFFFFFFu

/* ---- DirectInput constants ---- */
#define DI8_VERSION            0x0800
#define DI8DEVCLASS_GAMECTRL   4
#define DIEDFL_ATTACHEDONLY    0x00000001
#define DIEDFL_FORCEFEEDBACK   0x00000100
#define DIENUM_STOP            0
#define DIENUM_CONTINUE        1
#define DIDF_ABSAXIS           0x00000001
#define DIDFT_ABSAXIS          0x00000002
#define DIDFT_ANYINSTANCE      0x00FFFF00
#define DIDFT_OPTIONAL         0x80000000
#define DISCL_EXCLUSIVE        0x00000001
#define DISCL_BACKGROUND       0x00000008
#define DIEFF_OBJECTOFFSETS    0x00000002
#define DIEFF_CARTESIAN        0x00000010
#define DIPH_DEVICE            0
#define DIPROPAUTOCENTER_OFF   0
#define DIPROP_AUTOCENTER      ((const GUID_ *)3)
#define DIEP_DIRECTION         0x00000040
#define DIEP_TYPESPECIFICPARAMS 0x00000100
#define DIEP_START             0x20000000
#define DIEB_NOTRIGGER         0xFFFFFFFF
#define GW_OWNER               4

#define DI_CREATEDEVICE   3
#define DI_ENUMDEVICES    4
#define DEV_SETPROPERTY   6
#define DEV_GETDEVICESTATE 9
#define DEV_ACQUIRE       7
#define DEV_UNACQUIRE     8
#define DEV_SETDATAFORMAT 11
#define DEV_SETCOOP       13
#define DEV_CREATEEFFECT  18
#define EFF_SETPARAMETERS 6
#define EFF_START         7
#define EFF_STOP          8

typedef struct { DWORD a; WORD b, c; BYTE d[8]; } GUID_;

typedef struct {
    DWORD dwSize;
    GUID_ guidInstance;
    GUID_ guidProduct;
    DWORD dwDevType;
    CHAR  tszInstanceName[260];
    CHAR  tszProductName[260];
    GUID_ guidFFDriver;
    WORD  wUsagePage;
    WORD  wUsage;
} DIDEVINST;

typedef struct {
    const GUID_ *pguid;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
} DIOBJDF;

typedef struct {
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    DIOBJDF *rgodf;
} DIDF;

typedef struct {
    DWORD dwSize, dwFlags, dwDuration, dwSamplePeriod, dwGain;
    DWORD dwTriggerButton, dwTriggerRepeatInterval, cAxes;
    DWORD *rgdwAxes;
    LONG  *rglDirection;
    void  *lpEnvelope;
    DWORD cbTypeSpecificParams;
    void  *lpvTypeSpecificParams;
    DWORD dwStartDelay;
} DIEFF;

typedef struct { LONG lMagnitude; } DICONST;

/* For Spring and Damper effects (one per axis) */
typedef struct {
    LONG  lOffset;
    LONG  lPositiveCoefficient;
    LONG  lNegativeCoefficient;
    DWORD dwPositiveSaturation;
    DWORD dwNegativeSaturation;
    LONG  lDeadBand;
} DICONDITION;

typedef struct { DWORD dwSize, dwHeaderSize, dwObj, dwHow; } DIPROPHEADER;
typedef struct { DIPROPHEADER diph; DWORD dwData; } DIPROPDWORD;

typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE, DWORD, const GUID_ *, void **, void *);
typedef HRESULT (__stdcall *SetProperty_t)(void *, const GUID_ *, const DIPROPHEADER *);
typedef HRESULT (__stdcall *EnumDevices_t)(void *, DWORD, void *, void *, DWORD);
typedef HRESULT (__stdcall *CreateDevice_t)(void *, const GUID_ *, void **, void *);
typedef HRESULT (__stdcall *SetDataFormat_t)(void *, const DIDF *);
typedef HRESULT (__stdcall *GetDeviceState_t)(void *, DWORD, void *);
typedef HRESULT (__stdcall *SetCoop_t)(void *, HWND, DWORD);
typedef HRESULT (__stdcall *Acquire_t)(void *);
typedef HRESULT (__stdcall *CreateEffect_t)(void *, const GUID_ *, const DIEFF *, void **, void *);
typedef HRESULT (__stdcall *SetParams_t)(void *, const DIEFF *, DWORD);
typedef HRESULT (__stdcall *Start_t)(void *, DWORD, DWORD);
/* v7.25c: IDirectInputEffect::Stop() takes NO arguments. Calling it through Start_t pushes
   two DWORDs the callee never pops - and in __stdcall the CALLEE cleans up, so every such
   call LEAKS 8 BYTES OF STACK. That is what crashed v7.24 and v7.25: SetFriction() stopped
   the effect during normal operation, the FFB thread's stack pointer walked, its locals were
   then read from the wrong addresses (the last crash log shows finalSpring = 9651808 where 0
   was the only possible value), and eventually a write landed somewhere it should not - hence
   heap corruption reported later, at a different ntdll offset every time.
   The three pre-existing miscalls at DLL_PROCESS_DETACH were harmless only because the
   process was already dying; they are fixed too rather than left as a trap. */
typedef HRESULT (__stdcall *Stop_t)(void *);

static const GUID_ GUID_XAxis =
    {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID_ GUID_YAxis =
    {0xA36D02F4, 0xC9F3, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID_ GUID_ZAxis =
    {0xA36D02F5, 0xC9F3, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID_ GUID_ConstantForce =
    {0x13541C20, 0x8E33, 0x11D0, {0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35}};
static const GUID_ GUID_Spring =
    {0x13541C27, 0x8E33, 0x11D0, {0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35}};
static const GUID_ GUID_Damper =
    {0x13541C28, 0x8E33, 0x11D0, {0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35}};
/* v7.24: 13541C29 is Inertia, 13541C2A is Friction - one apart, easy to get wrong. */
static const GUID_ GUID_Friction =
    {0x13541C2A, 0x8E33, 0x11D0, {0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35}};
static const GUID_ IID_IDirectInput8A =
    {0xBF798030, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};

static DIOBJDF g_objs[3] = {
    { &GUID_XAxis, 0, DIDFT_ABSAXIS|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL, 0 },
    { &GUID_YAxis, 4, DIDFT_ABSAXIS|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL, 0 },
    { &GUID_ZAxis, 8, DIDFT_ABSAXIS|DIDFT_ANYINSTANCE|DIDFT_OPTIONAL, 0 },
};
static DIDF g_fmt = { sizeof(DIDF), sizeof(DIOBJDF), DIDF_ABSAXIS, 12, 3, g_objs };

/* ---- DI effect state ---- */
static void *g_di     = NULL;
static void *g_dev    = NULL;
static void *g_eff    = NULL;   /* ConstantForce: crash kicks */
static void *g_spring = NULL;   /* Spring: centering          */
static void *g_damper = NULL;   /* Damper: rotation resistance */
static HWND  g_hwnd   = NULL;
static GUID_ g_wheel;
static int   g_found  = 0;
static char  g_wheelName[260];

/* ---- M3: choose the wheel by instance GUID, 2026-07-30 --------------------------------------
 * Until now the enum took the FIRST force-feedback device it was handed and stopped. On this
 * rig that is right by luck: the SIMAGIC wheelbase enumerates first and the ODDOR-GEAR shifter
 * is a separate non-FFB device, so it is never a candidate. On a rig with two FFB devices - a
 * wheel and a joystick, a wheel and a second base left plugged in - "first" is whatever the
 * driver stack felt like, and it can change between boots without anything visible happening.
 *
 * `device=` in the [ffb] section takes an instance GUID, `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`.
 * Absent or unparseable, the behaviour is EXACTLY today's - that is the fallback the item asked
 * for, and it is what makes this change safe to ship without anybody editing a file.
 *
 * TWO RULES THIS OBEYS, both bought elsewhere in this project:
 *   1. THE FALLBACK IS LOUD. A configured device that is not plugged in must not silently become
 *      a different wheel: that is a mod that works, on the wrong hardware, and says nothing. The
 *      log names what was asked for, what was found and that a substitution happened.
 *   2. EVERY device seen is logged with its GUID, whether it was chosen or not. A user cannot
 *      write a GUID into an ini that nothing ever prints, and looking it up in the registry is
 *      not an answer. The log IS the discovery mechanism.
 */
/* Defined in ffb_settings.c, which this file includes at the BOTTOM - so it is declared here to
   be used at init, above. A static declaration followed by the static definition is legal C and
   is the same shape fp_wmatrix.h uses for the logger. */
static void FFBIniPath(char *out);

static GUID_ g_wantGuid;                 /* what the ini asked for                             */
static int   g_haveWant  = 0;            /* 0 = no device= key, behave exactly as before       */
static GUID_ g_firstGuid;                /* the first FFB device seen - today's choice          */
static int   g_haveFirst = 0;
static char  g_firstName[260];
static int   g_matched   = 0;            /* the wanted GUID was actually found                  */
static int   g_devSeen   = 0;            /* how many FFB devices the enum offered               */

static DWORD g_axes[1] = { 0 };
static LONG  g_dir[1]  = { 0 };

static DICONST    g_cf         = { 0 };
static DICONDITION g_springCond = { 0, 0, 0, 0, 0, 0 };
static DICONDITION g_damperCond = { 0, 0, 0, 0, 0, 0 };
static void *g_friction = NULL; /* v7.24 Friction: weight that does NOT depend on how fast
                                   the wheel is being turned. May be NULL if the driver does
                                   not support it - every use is guarded, and NULL means the
                                   build behaves exactly like v7.23. */
static DICONDITION g_fricCond = { 0, 0, 0, 0, 0, 0 };
static DIEFF       g_eparm;      /* ConstantForce params */
static DIEFF       g_springParm; /* Spring params        */
static DIEFF       g_damperParm; /* Damper params        */
static DIEFF       g_fricParm;   /* Friction params      */
static int         g_flip = 0;

static HANDLE g_log = INVALID_HANDLE_VALUE;
static volatile DWORD g_reccount = 0;

/* v7.36: what the last kick was, captured inside Log() so no call site can be missed.
   See the note there. g_spdX100 is mirrored out of the FFB loop for the same reason - a mark
   has to record whether he was STANDING or MOVING, because the same multiplier can be right
   for one and wrong for the other. */
static volatile DWORD g_lastKickCode = 0;
static volatile DWORD g_lastKickMag  = 0;
static volatile DWORD g_lastKickTick = 0;
static volatile LONG  g_spdX100      = 0;   /* km/h x100 */
static volatile LONG  g_wallowNow    = 0;   /* the live road-texture force */

/* ---- v7.32-DOR: the WHEEL'S OWN POSITION, read back from the device ----
   Until now this mod was write-only: it commanded forces and never once looked at where the
   wheel actually was. That is exactly the blind spot the rotation-range drive needs closed -
   the whole question is what a given force DOES to the wheel at 90 vs 900 degrees, and the
   answer is a displacement, not a command.

   Free: the data format at g_fmt already declares three absolute axes (12 bytes), so
   GetDeviceState needs no format change and no re-Acquire. Read-only, no effect is touched.

   The raw value has NO range set by us (we never call DIPROP_RANGE), so it is whatever the
   driver reports - expect 0..65535 with centre near 32768, but the offline pass must measure
   the centre and the endpoints from the data rather than assume them. */
static volatile LONG g_axRaw[3] = { 0, 0, 0 };
static volatile LONG g_axOk     = 0;   /* 0 = last GetDeviceState failed (alt-tab, lost) */

/* ---- v7.33: SCALE THE CONSTANT FORCE BY THE WHEEL'S ROTATION RANGE ----
   Measured on the 2026-07-23 range drive (src/v732_burst.py), not argued. Per unit of
   commanded impulse the wheel turns the SAME NUMBER OF PHYSICAL DEGREES at every range:
   -122, -157, -124, -124, -136 deg/impulse at 360, 360, 600, 900, 1500 - within 13% of each
   other. The wheel is a plain inertia and the range does not enter the physics at all.

   What DOES change is what those degrees mean. The same curb kick eats:
       90 deg range  ~89% of the lock (measured 45%, CLIPPED by the end stop)
      360            17-18%
      600            13.9%   <- the range he tuned everything on
      900             8.4%
     1500             4.7%
   and at 90 the wheel sits past 90% of lock for 6.5-13.3% of the driving time, which is
   exactly his complaint - a released wheel banging into the stops - and is also the real
   injury risk on a 12 Nm direct drive.

   So: scale DOWN below 600, never up. k = min(1, DOR/600).

   NEVER ABOVE 1.0, for three separate reasons, any one of which is sufficient:
     1. going up means more PHYSICAL degrees of throw (105 deg at 1500 against 42 at 600) on a
        12 Nm wheel - his own observation, and correct;
     2. we already command up to 21542 against DirectInput's 10000 ceiling, so the big crashes
        are clipped by the driver. Scaling up would amplify only the QUIET effects (wallow,
        taps) and not the loud ones, compressing everything toward the same loudness;
     3. at 600 and above the build must stay bit-identical to v7.31, which he approved.

   The range cannot be read from DirectInput - there is no such property - so it comes from
   the F1..F5 label he presses. No label yet (g_dorNow == 0) means k = 1.0: an unlabelled
   drive behaves exactly like v7.31, which is the safe default.

   TRIM: he expected to want the crashes back up a little at 90 ("I will say to raise them a
   little"), and PageUp/PageDown once moved the trim live so he could find the value during
   a drive. THAT IS NO LONGER TRUE - those keys drive g_gateFrac now, and g_dorTrim is pinned
   at 100 with no way to move it (gog-patcher 2026-07-27, his call: leave it pinned, the tuner
   utility's sliders cover the same ground and two knobs on one quantity is how a settings
   dialog starts lying). The multiply below stays because the product is still clamped at 1.0,
   so it can never make anything louder than today's build - it is simply always x1 today. */
/* ---- v7.34: THE LINEAR LAW IS REFUTED. The scale is a CUBE ROOT, and it is measured ----
   k = DOR/600 was over-correction and he said so within one drive: "it feels like 80% of the
   effects disappeared entirely". He was right to the percent - k was 0.15, so 85% of every force
   was gone - and the reason a linear law fails is specific, not a matter of taste. It does not
   quieten the effects evenly, it kills the QUIET ones outright: an object or pedestrian tap
   runs 1000-2000 units, and 15% of that is 150-300, which a 6.6 Nm wheel does not convey at
   all. The loud crashes survived, the small texture did not, so the count of audible effects
   collapsed rather than their volume.

   He then found the right values himself, live on PageUp/PageDown, on two ranges, bracketed
   from both sides (logs/2026-07-23-v733b-trim, src/v733_marks.py):
        90 deg   0.48 -> 1 LIKE / 4 NOPE     0.51 -> 6/1     0.57 -> 0/1
       360 deg   0.719 -> 2/2                0.84 -> 6/1     0.96 -> 0/1
   and ended the drive sitting on 0.51 and 0.84.

   Fit k = (DOR/600)^p to those two choices and the exponent comes out at 0.355 and 0.341 -
   two numbers picked by hand a quarter of an hour apart, landing on ONE law. p = 1/3 predicts
   0.531 and 0.843 against his 0.510 and 0.840. With k(600) = 1 pinned, the law has a single
   free parameter and there are two points, so one genuinely checks the other.

   WHAT SHIPS IS THE MEASUREMENT, NOT THE FIT. The table below holds his 0.510 and 0.840, not
   the law's 0.531 and 0.843. The 4% gap at 90 deg is small but it points the wrong way: 0.531
   is untested and sits a third of the way toward 0.570, which he marked NOPE. The cube root
   is what fills in a range that is NOT in the table - and every entry here is a range he
   actually drove, so no extrapolation is in force today.

   The scale is a lookup table and no cube root is computed at runtime. That matters: this file
   builds -nostdlib, so there is no powf and no cbrtf to call. */
#define DOR_REF 600
/* v7.35: slot 5 moved from 1500 to 1080. 1500 is settled - the hands/screen mismatch there is
   not fixable by anything we can do (see [[wheel-rotation-range]]) - and 1080 is the range he
   actually asked about next.

   gog-patcher 2026-07-25: SLOT 6 = 1440 ADDED, per instruction, for the tuner utility's
   range selector. There is no F-key for it - F6 is already "new run starts here" (0x75) - so
   1440 is reachable only from the INI, which is the direction the whole utility goes anyway.
   It is EXTRAPOLATED, never driven: see g_wallowK below, and everything that shows it to the
   user must say so.

   gog-patcher 2026-07-27: SLOTS 7 AND 8 = 540 and 720, per instruction, and with them the
   free-degrees text box is dropped - eight detents from 90 to 1440 cover what a real wheelbase
   offers, so nothing needs an arbitrary-angle cube root and this file still builds -nostdlib.

   THEY ARE APPENDED, NOT INSERTED IN ORDER, and that is deliberate. F1..F5 select a slot by its
   INDEX (PollFeelKeys), so putting 540 between 360 and 600 would silently make F3 mean 540 and
   F4 mean 600 - reassigning the keys every archived drive was recorded with, and moving the
   reference off the key his hands know. The INI path does not care: ReadRange searches this
   table for a value in DEGREES, so order is irrelevant there, and the utility sorts for display.
   The two new rows are therefore INI-only, exactly like 1440.

   Both values are the cube-root law, computed, not eyeballed:
       540 kicks  (540/600)^(1/3) = 0.96549   -> 0.965
       720 wallow (720/600)^(1/3) = 1.06266   -> 1.063
   and each is BETWEEN measured points rather than beyond them, which is what the law is for -
   540 sits between his 360 and his 600, 720 between 600 and the 900 row. Neither is the reach
   that 1440 is. Neither has been driven either, and the UI says so. */
#define N_DOR_SLOTS 8
static const LONG  g_dorDegTab[N_DOR_SLOTS] = { 90, 360, 600, 900, 1080, 1440, 540, 720 };
/* v824: NOTHING IS SCALED DOWN BELOW 600 ANY MORE. ours-042, the A/B he asked for: 90 and 360 at
   the July scale (F1 0.510, F2 0.840) against full force (F3, F4) - verdict: F3 and F4 better.
   On ours-040 F1 had never registered and 90 at full force had already been rated excellent.
   The July values were fitted on the OLD model's kicks; the new physics wants none of it. They are
   kept in the comment for the record: 90 0.510, 360 0.840, 540 0.965 ((540/600)^(1/3)). */
static const float g_dorK[N_DOR_SLOTS] = {
    1.000f,   /*   90 - was 0.510 (July, his value on the old model) */
    1.000f,   /*  360 - was 0.840 */
    1.000f,   /*  600 - the reference he tuned everything on */
    1.000f,   /*  900 - nothing above 600 is scaled down; the centring grows instead, g_centreK */
    1.000f,   /* 1080 */
    1.000f,   /* 1440 */
    1.000f,   /*  540 - was 0.965 */
    1.000f,   /*  720 */
};
static volatile LONG g_dorNow  = 0;    /* degrees, for the log. 0 = none declared yet */
static volatile LONG g_dorSlot = -1;   /* -1 = none declared -> k = 1.0, i.e. v7.31   */
/* Pinned at 100. It was a live PageUp/PageDown knob for one drive in v7.33b; those keys drive
   g_gateFrac today and nothing writes this except the reset on a range switch. Left pinned on
   the 2026-07-27 call rather than given an INI key - the utility's own sliders cover the
   same ground, and two controls on one quantity is how a settings dialog starts lying. */
static volatile LONG g_dorTrim = 100;

/* ---- v822, A DEBUG BUILD FOR ONE DRIVE: THE RANGE ON F1..F5 AGAIN ----
   His request, 2026-09-30 (translated): "build me a debug version with 90, 360, 720 and 900
   degrees on F1, F2, F3, F4 - I press F1, set 90 on the wheel, test how it feels, then switch";
   above all, whether the centring still works away from 600. F5 = 600, the reference, so every
   range is judged against it in the same drive.
   THE KEY ONLY TELLS THE MODULE WHICH RANGE THE WHEEL IS SET TO. The wheel's own range is set by
   hand in the wheel's software; a key that disagrees with it scales everything for the wrong range.
   Off unless `range_keys = 1`. While on, the roll and crash banks give their F keys up (both are
   settled: roll 38 and crash F6 are the unpressed state) and the ground presets bound to F1..F5
   stay silent (FKeyOwnedByBank). A pressed key wins over the ini's `range` for the rest of the
   session, the same rule as every bank here. Logged as 0x3D. */
/* v823: THE SMALL-ANGLE A/B, his request after ours-040 (translated): "make me 90 with the July
   values and 360 as in the drive on F1/F2, and on F3/F4 what was 90 x1 and 360 x1". On ours-040
   his F1 never registered, so he drove 90 deg with the module at 600 - every force unscaled - and
   called it excellent. F3/F4 reproduce exactly that: the SCALE comes from the 600 slot (k 1.000,
   no kick floor, no roll boost), while the log records the degrees he set on the wheel.
     key  declared  scaled as         k
     F1      90        90          0.510, kicks at least 1300
     F2     360       360          0.840, kicks at least 1500
     F3      90       600          1.000
     F4     360       600          1.000
     F5     600       600          1.000, the reference */
#define RANGE_KEYS_N 5
static const int  g_rangeKeyVk     [RANGE_KEYS_N] = { 0x70, 0x71, 0x72, 0x73, 0x74 };   /* F1..F5 */
/* v824: the bank tries THE NEW LAW at every range - no scale-down below 600, the centring grown
   above it: F1 90, F2 360, F3 720, F4 900, F5 600, each scaled as itself. */
static const LONG g_rangeKeyDeg    [RANGE_KEYS_N] = {   90,  360,  720,  900,  600 };   /* declared */
static const LONG g_rangeKeyScaleAs[RANGE_KEYS_N] = {   90,  360,  720,  900,  600 };   /* the slot */
static volatile LONG g_rangeKeys    = 0;
static volatile LONG g_rangeKeyUsed = 0;

/* ---- v7.52: A PERCEPTIBILITY FLOOR FOR KICKS AT SMALL RANGES ----
   Reported 2026-07-24, after reading the per-angle force table and explicitly declining to
   spend a drive on it: "I hope you manage to raise it a little above the threshold,
   so that it can be felt. Barely at 90 and a bit stronger at 360."

   What the table showed. DorScale multiplies every kick by 0.510 at 90 deg and 0.840 at
   360, and two effects fall under the ~1200 he can feel on the 12 Nm wheel:
     - the bullet tap, 1600 -> 816 at 90 deg (100% of them);
     - the brake/slide nudge, which is already 2200 x dampMult and reaches 2200 x 0.15 in
       a deep slide - x0.510 leaves 168, i.e. silence. At 360 it is under the floor in 34%
       of cases.
   Neither is a bug: DorScale is his own measured cube-root law and dampMult is his own
   approved v7.31 change. They simply multiply.

   Why a hard clamp and not a smooth remap. A remap of [0, REF] onto [FLOOR, REF] would
   preserve the ordering of weak kicks, but it also lifts kicks that were ALREADY audible -
   the object tap at 90 would move 1641 -> 2087. He asked for the inaudible ones to become
   audible, not for the weak end to be rebalanced, and he is not spending a drive to judge
   it. A clamp touches only what is below the floor and nothing else.

   The cost, stated because it is real: below the floor the ordering is lost - at 90 deg a
   bullet tap and a deep-slide nudge both arrive as 1300. They were both silent before, so
   this trades distinguishable-but-unfelt for felt-but-indistinguishable results.

   The bound that keeps it safe: NO FLOOR EXCEEDS WHAT THE SAME EFFECT ALREADY DELIVERS AT
   600 deg. 1300 and 1500 are both under the bullet tap's own 1600, so no angle can end up
   buzzing harder in a firefight than 600 does - and [[reference-v750]] records that his
   `9` marks on the v7.51 drive landed on bullet-tap clusters, so making that worse would
   be fixing one complaint by feeding another.

   600 and above are untouched: the array is 0 there, the branch is skipped, and a 600+
   drive stays bit-identical. The deep-slide nudge is under the floor at 600 too, but
   fixing that means changing the reference he has just frozen, so it is left alone and
   recorded instead. */
/* v824: every floor is 0. The floors only existed to lift kicks the July scale had pushed under
   his threshold at 90 and 360; with no scale-down there is nothing to lift, and F3/F4 of ours-042 -
   the states he chose - had none. (Was 1300 at 90, 1500 at 360.) */
static const LONG g_kickFloor[N_DOR_SLOTS] = {
    0,      /*   90 - was 1300 */
    0,      /*  360 - was 1500 */
    0,      /*  600 - the reference. No floor, bit-identical                             */
    0,      /*  900 */
    0,      /* 1080 */
    0,      /* 1440 - no floor above 360, same rule as every row above it */
    0,      /*  540 - the rule is "1300 at 90, 1500 at 360, none above 360", and 540 is above
               360. At 0.965 the quietest kick (the bullet tap, 1600) arrives at 1544, well
               clear of the ~1200 he can feel at all, so there is nothing for a floor to lift */
    0,      /*  720 - above 600: unscaled, so nothing is under the threshold */
};
static volatile int g_continuous = 0;  /* set only around the wallow/spin/SAT SetMag */

static float DorScale(void)
{
    LONG s = g_dorSlot;
    float k = (s >= 0 && s < N_DOR_SLOTS) ? g_dorK[s] : 1.0f;
    k *= (float)g_dorTrim * 0.01f;
    if (k > 1.0f)  k = 1.0f;    /* never louder than v7.31 - see reason 3 above */
    if (k < 0.02f) k = 0.02f;
    return k;
}

/* ---- v7.35: THE OTHER END. Boost the road wallow ABOVE 600, and only it ----
   At 900 and 1500 his complaint is the opposite of the 90 deg one: the effects arrive in the
   hands correctly but the car on screen barely responds, because the same physical throw is a
   much smaller share of the steering. Closing that gap needs a BIGGER throw, and the earlier
   reading that this was impossible came from a mislabelled log code: 0xC5 is the F5
   rammed-while-parked kick, NOT curbs. Curbs, trams and the roll wallow are not discrete kicks
   at all - they are the continuous channel logged at 0x72, and it has room:

       p90 2278   p99 2929   max 3888   -> 2.6x of headroom to the 10000 ceiling

   So the boost goes on `wallowForce` alone, at the one place the three continuous forces are
   summed. spinForce and satForce are NOT touched: the slip-angle SAT is the effect he called
   his best ever, and this drive must not put a second variable next to it.

   Crashes are not boosted either, and cannot be - they already command up to 21542 against a
   10000 ceiling, so above 600 they stay exactly as v7.31 left them.

   The base values are the cube-root law continued upward, which is the same law his own 90 and
   360 choices produced. Equalising the share of lock would need 1.64x at 900 and 2.0x at 1080;
   the law asks for far less, and which of the two he actually wants is a hands question. It was
   once answerable live: PageUp/PageDown drove THIS gain above 600 and the range scale below it,
   so one pair of keys always moved the quantity under test. Those keys drive g_gateFrac now and
   the trim is pinned - the answer comes from the tuner utility's road-surface slider instead. */
/* gog-patcher 2026-07-25, and this OVERRIDES one of the seven decisions of 2026-07-24.
   That sign-off described wallow boost clamped at the 1080 value, with 1440 labelled untested, i.e.
   1440 would have carried 1.216. His instruction today is the opposite and explicit -
 * "make the angle 1440 and continue the extrapolation up to there, with a note that this angle was
 * not tested" - so the law is CONTINUED rather than clamped, and 1440 gets 1.339.
   Recorded rather than silently applied, because the two cannot both be true. */
static const float g_wallowK[N_DOR_SLOTS] = {
    1.000f,   /*   90 - handled by DorScale going down; nothing to boost here */
    1.000f,   /*  360 - same                                                  */
    1.000f,   /*  600 - the reference. Untouched, by design                   */
    1.145f,   /*  900 - (900/600)^(1/3)                                       */
    1.216f,   /* 1080 - (1080/600)^(1/3)                                      */
    1.339f,   /* 1440 - (1440/600)^(1/3) = 1.3389. EXTRAPOLATED, NEVER DRIVEN.
                 Every other row above 600 is also the law rather than a drive, but this
                 one is the furthest from any measured point, so the UI must label it. */
    1.000f,   /*  540 - below the reference. DorScale takes it down; there is nothing to
                 boost, exactly as for 90 and 360                                        */
    1.063f,   /*  720 - (720/600)^(1/3) = 1.06266. Between 600 and the 900 row, so it is
                 interpolation on the law rather than the reach 1440 is                  */
};
/* Pinned at 100, same story as g_dorTrim above: a one-drive live knob whose keys now belong to
   g_gateFrac. Left pinned deliberately - slider #6 "road surface" in the tuner utility moves
   this same quantity, and exposing both would give one value two owners. */
static volatile LONG g_wallowTrim = 100;
/* v813: the ROLL half of the wallow channel, as a live percent. 100 = every drive up to v812,
   bit for bit. v813 shipped 25; v818 ships 100 again - the original is the BASE, his correction
   of 2026-09-30, and the reductions live on F2/F3. See the bank below. */
static volatile LONG g_rollGainPct = 38;   /* v820: his roll reference, see the bank below */
/* v814: the roll CEILING as a live number, because his three-way test needs it. F2R_CAP 3000 is
   what every drive up to v813 ran; the preset that cuts by ceiling instead of by gain moves it. */
static volatile LONG g_rollCap = 3000;
/* v814: HIS THREE-WAY TEST OF THE ROLL CHANNEL, 2026-09-14 (translated): "make the roll three
   states: F1 as it is now, F2 cut by gains so the whole rocking is quieter, F3 by the ceiling."
   And for crashes in the same breath: "F4 the ORIGINAL crash curve", then "in 4 our current
   state, in 5 and 6 the ones you changed".

   v814 MISREAD THAT, and v818 corrects it. "Current state" was read as the current BUILD - v813,
   whose roll was already cut to 25% and whose crashes were already at 85%, numbers he had never
   driven - so F1 and F4 carried cuts and every other key was a cut of a cut. To him "current"
   was what he drove, and his first word was "original". After ours-035 (2026-09-30, translated):
   "F1 and F4 must be the base values, and F2/F3 the choice of going down proportionally or
   through the ceiling." On ours-035 the old F1 (gain 25) was, in his words, weak.

     F1   gain 100, cap 3000   THE ORIGINAL - every drive up to v812, bit for bit
     F2   gain  38, cap 3000   quieter PROPORTIONALLY - "cut by gains", the whole rocking smaller
     F3   gain 100, cap 1815   quieter by CEILING - ordinary rocking as it was, the big lean cut

   THE DEPTH IS SET IN DELIVERED FORCE. The original is clipped: 54% of its live rows sit on the
   3000 ceiling, so gain 25 delivered 44% of the original's mean, not 25%. His two verdicts
   bracket 992 (gain 25, "weak") and 2229 (the original, "too amplitudinous"); F2 and F3 aim at
   their geometric middle and are MATCHED ON MEAN FORCE there - 1504 and 1504 over the same 1476
   live rows of ours-034 (tools\price-banks.py). F2 keeps every lean distinguishable (3% pinned),
   F3 squashes the big ones into one another (69% pinned). What separates them is shape only. */
/* v820: THE ROLL IS SETTLED. His verdict on ours-037 (translated): "F2 shows that the reference
   for how the roll should feel is not F1 but F2" - gain 38 under the 3000 ceiling, and "the
   rocking feels excellent" on it. So all three keys now give that reference: the question is
   closed, and a key that changed it would only be a way to lose it mid-drive. The bank stays
   ARMED on purpose - disarmed, F1..F3 would fall back to the ground presets (FKeyOwnedByBank).
   The v818 layout (100 / 38 / cap 1815) is in git and in memory if it is ever wanted again. */
#define ROLL_PRESETS 3
static const LONG g_rollBankGain[ROLL_PRESETS] = {   38,   38,   38 };
static const LONG g_rollBankCap [ROLL_PRESETS] = { 3000, 3000, 3000 };
static volatile LONG g_rollKeys = 1;      /* F1..F3 armed; 0 gives the F keys back to the presets */
static volatile LONG g_rollPresetCur = 1; /* ships on F1, the original                           */
/* The key wins over the file for the rest of the session: the ini is re-read once a second, and
   without this his F1 press would be undone a second later while the log went on reporting the
   state he chose. Same trap, same fix, as the preset bank and the damper ladders. */
static volatile LONG g_rollKeyUsed = 0;
/* v813: THE MOD'S OWN CRASH TRIM, and it is deliberately NOT the `crash` slider. That slider is
   the user's, and its 100 means "the mod as designed" - moving its default would make every
   support conversation and every screenshot of the tuner lie. This one sits before it.
   v813 shipped 85 on his ours-034 verdict ("crashes are a bit sharp after all... make them
   smaller", translated). v818 ships 100 again: the original is the BASE on F4 by his correction
   of 2026-09-30, and the reductions live on F5/F6 - see the bank below. 100 is the exact identity,
   so with the bank switched off every drive up to v812 is reproduced bit for bit. The crash
   FORMULA stays bit-identical - MAG_MIN 1250, MAG_EXCESS_SCALE 7600, MAG_SPD_SCALE 755, ceiling
   25000 - as the standing rule requires; this is a percent applied to its output.
   v821: THE UNPRESSED STATE IS F6, his reference after ours-039 (translated): "overall I liked F6
   more... keep F6 as the reference". 70% up to 70 km/h of true impact speed, a straight line to
   100% at 90 km/h. The initializer, the ini defaults below and the bank's start index must agree;
   test_ffb_ground.c says so. F4 (the original) and F5 stay on their keys. */
static volatile LONG g_crashTrimPct = 70;
static volatile LONG g_crashCap = MAX_MAG;   /* the crash ceiling as a live number, v814 */
/* v820: the speed ramp of the crash trim, km/h of TRUE entry speed. 0/0 = no ramp, i.e. the flat
   g_crashTrimPct of every build before v820. See CrashTrimFor. v821: F6's 70/90 by default. */
static volatile LONG g_crashRampLo = 70;
static volatile LONG g_crashRampHi = 90;
/* v814: HIS THREE-WAY TEST OF THE CRASH CURVE, asked for in the same breath as the roll one
   (translated): "F4 the ORIGINAL crash curve, F5 by the ceiling, and F6 proportionally or
   linearly... calculate yourself how best to shift everything lower... I will just switch between
   the states and say which I like more."

   v814 put v813's trimmed state on F4 on the same misreading as the roll bank - see there. v818
   restores his own layout: F4 is the ORIGINAL, and F5/F6 are the two ways down from it.

   v819: THE SAME ORDER AS THE ROLL BANK, his instruction of 2026-09-30 (translated): "make F5 and
   F6 by gain and by ceiling too, otherwise F2 is by gain, F5 by ceiling, F3 by ceiling, F6
   proportional - it is not consistent". So in BOTH banks the second key is the proportional cut
   and the third is the ceiling. v814..v818 had F5 = ceiling and F6 = proportional; the state he
   preferred on ours-035 under the name F6 ("F6 hit more interestingly than F5") is on F5 now,
   unchanged.

     F4   trim 100, cap 25000  THE ORIGINAL - every drive up to v812
     F5   trim  70, cap 25000  quieter PROPORTIONALLY - the whole curve slides down
     F6   trim 100, cap 10686  quieter by CEILING - an ordinary crash as it was, big ones cut

   MATCHED ON MEAN FORCE so the A/B is about SHAPE, priced over the same 110 crashes from five
   drives recorded at trim 100 (ours-024, ours-028, ours-034 and the release drives 10 and 11),
   tools\price-banks.py:

     F4  p50 11894  p90 25000  mean 13415   13.6% at the ceiling
     F5  p50  8325  p90 17500  mean  9390    0.0% at the ceiling
     F6  p50 10686  p90 10686  mean  9390   55.5% at the ceiling

   The crash FORMULA is bit-identical in all three - MAG_MIN 1250, MAG_EXCESS_SCALE 7600,
   MAG_SPD_SCALE 755. Only these two numbers move. */
/* v820: THE CRASH TRIM FOLLOWS THE IMPACT SPEED. His verdict on ours-037 (translated): "up to 50
   F5 scales the force of the hit excellently... just now at 60-70 it was too weak", and F4 "much
   better" at 60. His numbers are the speedometer's MPH (checked: the needle on 60 while the log
   read 97.6 km/h), so 50 mph = ~80 km/h and 60 mph = ~97 km/h.
   WHY F5 WAS WEAK AT SPEED, measured over the drive's 51 kicks: above ~70 km/h the crash formula
   is always at its 25000 ceiling, so 70% of it is a flat 17500 - every one of the 11 F5 hits up
   there delivered exactly that. He expects a hit to keep growing with speed, and 70% of a clipped
   ceiling cannot. At low speed the same 70% was rated excellent (7-8 thousand at 20-40 km/h), and the
   ORIGINAL there was rated: the weak ones too strong.
     F4  the ORIGINAL, flat 100 (his rule: the first key is the base values)
     F5  70% up to 80 km/h, rising in a straight line to 100% at 97 km/h - his words, literally
     F6  the same shape moved earlier: 70% to 70 km/h, 100% at 90 - the only open parameter is
         where the rise belongs, and the not-bad rating at 70 km/h is the edge it is tested against
   The ceiling stays the original 25000 in all three: the ceiling state was retired - on ours-037
   it gave a 117 km/h and a 40 km/h hit the identical 10686, and he never spoke for it. */
#define CRASH_PRESETS 3
static const LONG g_crashBankTrim  [CRASH_PRESETS] = {     100,      70,      70 };
static const LONG g_crashBankCap   [CRASH_PRESETS] = { MAX_MAG, MAX_MAG, MAX_MAG };
static const LONG g_crashBankRampLo[CRASH_PRESETS] = {       0,      80,      70 };
static const LONG g_crashBankRampHi[CRASH_PRESETS] = {       0,      97,      90 };
static volatile LONG g_crashKeys = 1;
static volatile LONG g_crashPresetCur = 3;   /* v821: ships on F6, his reference after ours-039 */
static volatile LONG g_crashKeyUsed = 0;

/* ---- v7.36: the TRUCK surplus becomes a live knob, because it was never judged ----
   Reported 2026-07-24: the truck damper was never properly tested, and approval was not
   remembered. Correct, and worse: on 2026-07-23 this feature was CLOSED - weighting up the
   trucks was declined, since they roll more and the roll effect already gives its own effect on them, written
   into open-tasks as "Do not build it" - yet the constants were never removed. Friction went
   to zero that night; HEAVY_DAMP_STATIC_X and its two neighbours did not. So the shipped build
   has been weighting trucks against his stated decision ever since.

   It never showed: `g_heavy` was 0 in all three of the recent drives (0xCE, max votes 0 in
   every one), because he only ever drove cars. 0 votes for a car is the CORRECT reading -
   the census measured 29/29 for a truck and 0/29 for a car - so the detector is fine and the
   feature simply never engaged.

   Rather than delete it unjudged a second time, it gets the same treatment as everything else
   that turned out to matter: a live trim, and a drive.

   THE TRIM SCALES THE SURPLUS, NOT THE ABSOLUTE. At 0% a truck steers exactly like a car,
   which is the answer he already gave in words; at 100% this is the v7.31 behaviour. A naive
   multiply would drive the damper to zero at 0% and leave the wheel dead. */
/* gog-patcher 2026-07-25: DEFAULT 0, not 100, and TruckX is now actually CALLED.
   v7.36 wrote this trim so the truck surplus could be judged instead of deleted unjudged -
   then never wired it to anything: TruckX() was defined and called from nowhere, and the
   surplus kept being applied raw. So the shipped build has been weighting trucks by
   2.00/1.30/1.30 ever since, against the 2026-07-23 ruling (weighting up the trucks was
   declined either way, since they roll more and the roll effect already gives its own effect on them, recorded as CLOSED, not
   deferred, at tracker lines 110-113). It stayed invisible only because the person testing
   has driven nothing but cars, so g_heavy was 0 in every logged drive.
   His call, 2026-07-25: wire it and default the trim to 0, so a truck steers like a car until
   he asks otherwise - and the asking is `truck=` in mafia_ffb.ini (0 = his ruling, 100 = the
   old v7.31 behaviour).
   THE REFERENCE CANNOT MOVE, and that is structural rather than a promise: TruckX returns 1.0f
   on its first line whenever g_heavy is 0, which is every drive he has ever recorded. */
static volatile LONG g_truckTrim = 0;      /* percent of the surplus above 1.0 */

/* ---- v7.36: THE LOW-SPEED CRASH HOLE ----
   His report, 2026-07-24: "a hit into a stationary object, or into a car, or into a building at low
   speed... practically never came through at all. I remember it many times: the hit just did not land".
   Measured across three drives: 9 of 14 sustained low-speed impacts produced NO force at all,
   and 19-32% of all impacts were silent. (Upper bound - the detector used for that count
   cannot separate a wall from very hard braking. The two structural faults below can be read
   straight off the source and need no proxy.)

   FAULT 1 - the drop threshold is an absolute 4.0 m/s over 150 ms. An impact entered at 3 m/s
   cannot produce a 4 m/s drop however hard it is, so nothing below 14.4 km/h can ever fire.

   FAULT 2, and this one is worse - `gsNow > CRASH_MIN_GS` tests the speed AFTER the impact.
   Hitting a wall and stopping dead gives gsNow = 0, so the most obvious crash in the game is
   rejected by construction. The intent was to suppress wheel-speed noise on a parked car, and
   that intent is served correctly by testing the speed BEFORE the event instead.

   Both faults share one wrong assumption: that a crash leaves the car still moving.

   The threshold now scales with the entry speed, so a hit that kills the car dead always
   qualifies, while a fast crash keeps exactly the 4.0 m/s gate it has today - nothing above
   6.7 m/s entry changes at all. G_GATE_FRAC is on PageUp/PageDown because the false-positive
   cost (hard braking at walking pace) is a feel question, not an arithmetic one. */
#define GATE_FRAC_DEF     20      /* percent of entry speed. v7.38: baked at the value he drove
                                     the whole v7.37 session on - 20% gave 0 false event-NOPEs
                                     and the bypass rejected 0 phantoms, so detection is settled
                                     and the trim keys are freed for crash FORCE. */
#define GATE_FRAC_FLOOR  1.0f     /* m/s - never demand less than this, or noise fires */
static volatile LONG g_gateFrac = GATE_FRAC_DEF;

static float CrashDropNeeded(float entrySpeed)
{
    float need = entrySpeed * (float)g_gateFrac * 0.01f;
    if (need > CRASH_GS_DROP) need = CRASH_GS_DROP;   /* fast crashes keep today's gate */
    if (need < GATE_FRAC_FLOOR) need = GATE_FRAC_FLOOR;
    return need;
}

/* ---- v7.40: the crash FORCE is the LOCKED REFERENCE CURVE, untouched ----
   Instruction, 2026-07-24: bring back the reference build, but stop detecting weak, low-speed
   collisions on top of it... take the feedback value from the 600 deg reference run, the curve there is already
   tuned and locked". So every force experiment of this session (knee, approved-curve,
   speed attenuation) is REMOVED. The crash magnitude is exactly the reference formula:
        mag = MAG_MIN + excess*MAG_EXCESS_SCALE + spdEma*MAG_SPD_SCALE   (clamped)
   which is the curve driven and locked on the 600 deg reference. Nothing above changes what a
   crash FEELS like; the only additions in this build are DETECTION - catching impacts the
   reference missed - and they hand the caught impact straight to this same formula.

   The one thing kept from the bug-fix work is the refractory window, because it is not a feel
   change: it stops the ADDED bypass detector from firing a second kick 200 ms after the normal
   branch on one impact (roughly a second and a half of the wheel turning). With the reference's single
   detector it would essentially never trigger; it exists solely to keep the new second detector
   honest. */
#define CRASH_REFRACTORY_MS  300u
static volatile DWORD g_lastCrashTick = 0;
static volatile int  g_heavy     = 0;      /* v7.23 F8 vote result, set by UpdateVehicleClass */

/* The reference crash force, in one place so both the normal branch and the bypass use the
   identical locked formula. `excess` is drop-above-threshold (severity), spdKmh the entry
   speed. Byte-identical to what the 600 deg reference produced. */
static LONG RefCrashForce(float excess, float spdMS)
{
    LONG mag = MAG_MIN + (LONG)(excess * MAG_EXCESS_SCALE) + (LONG)(spdMS * MAG_SPD_SCALE);
    if (mag > MAX_MAG) mag = MAX_MAG;
    if (mag < MAG_MIN) mag = MAG_MIN;
    /* M1: slider #2 does NOT scale here, and the reason is the third caller.
       This function has three call sites, and only two of them are forces. The third
       (the v7.50 ramp veto) uses it as a CLASSIFIER: `RefCrashForce(...) < RAMP_NEVER_MAG`
       is what separates a real impact from the end-of-stop deceleration ramp. Scaling
       inside the function leaks the slider into that decision - at 50% real crashes drop
       under RAMP_NEVER_MAG and get vetoed ENTIRELY rather than softened, and at 150% the
       false sharps v7.50 measured and killed come back. The handover's invariant is that
       every slider is a multiplier on an output force, never on a detector, so the two
       force sites scale themselves and the veto keeps reading the reference curve. */
    return mag;
}

/* ---- v7.49: is this deceleration a STEP or a RAMP? ----
   Returns max(single-tick speed drop over the last ~200 ms) / median(drop over the ~400 ms
   before that). An impact is a discontinuity against whatever the car was doing; a braking
   stop is smooth, and the game's end-of-stop deceleration ramp - the thing that produces
   the jab logged as 9999 - is smooth even though it is steep.

   Measured on all 40 archived bypass fires: standalone (real) events sit at a median 54,
   the fires that follow a soft nudge at 4.5. Computed here on the SAME per-tick spdEma
   series the offline pass used, so the two numbers are comparable by construction.

   FAILS OPEN in every degenerate case - too little history, or a flat baseline - by
   returning a large ratio, effectively treated as an impact. A missed ram is the failure mode
   judged unacceptable (it is what killed v7.44); a surplus jab is merely annoying. */
static float g_lastStepBase = 0.0f;   /* v7.51: the denominator, kept for the caller */

static float StepRatio(const float *ring, int n)
{
    g_lastStepBase = 0.0f;
    if (n < STEP_TAIL_N + STEP_BASE_N + 2) return 1e9f;   /* not enough history: fire */
    int j0 = n - 1;
    float tail = 0.0f;
    for (int k = 0; k < STEP_TAIL_N; k++) {
        float d = ring[(j0 - k - 1) % EMA_RING_SZ] - ring[(j0 - k) % EMA_RING_SZ];
        if (d > tail) tail = d;
    }
    float base[STEP_BASE_N];
    for (int k = 0; k < STEP_BASE_N; k++) {
        int i = STEP_TAIL_N + k;
        float d = ring[(j0 - i - 1) % EMA_RING_SZ] - ring[(j0 - i) % EMA_RING_SZ];
        base[k] = d > 0.0f ? d : 0.0f;
    }
    for (int a = 1; a < STEP_BASE_N; a++) {          /* insertion sort, 25 elements */
        float v = base[a]; int b = a - 1;
        while (b >= 0 && base[b] > v) { base[b + 1] = base[b]; b--; }
        base[b + 1] = v;
    }
    float med = base[STEP_BASE_N / 2];
    g_lastStepBase = med;
    if (med < 1e-4f) return 1e9f;      /* the car was not decelerating at all: a real step */
    return tail / med;
}

static float TruckX(float base)
{
    if (!g_heavy) return 1.0f;
    return 1.0f + (base - 1.0f) * (float)g_truckTrim * 0.01f;
}

/* ---- gog-patcher 2026-07-27: THE TWO TRUCK DAMPER SLIDERS ----
   The decision that follows (sections
   2026-07-27 and 2026-07-27b): a truck gets its OWN damper on the wheel and its OWN standstill
   damper, separately adjustable, so a truck is distinguishable from a car.

   DEFAULT IS A CAR, and that is his correction rather than an omission: "For trucks the default
   recommended values should be the same as for passenger cars. 1.75 is
   not it." The x2 / x1.75 numbers of his 2026-07-22 spec were never judged by feel, and shipping
   an unjudged number as a recommendation makes a default look like a measurement. So both
   percents default to 100 and Recommended leaves them there.

   Unlike g_truckTrim above this is a plain multiplier, not a surplus scaler, because there is no
   surplus to scale: the quantity it acts on is the finished damper coefficient, and 100% of it
   is what a car gets. The surplus form exists only where a constant above 1.0 has to be walked
   back to 1.0 (HEAVY_DAMP_STATIC_X and friends); here 1.0 IS the default.

   INERT BY CONSTRUCTION, twice over: it returns 1.0f before reading the percent whenever the
   vehicle is not heavy, which is every drive in the archive, and 1.0f again when the percent is
   the default 100. Multiplying a float by an exact 1.0f is exact, so a car is bit-identical.

   WHY IT COMPOSES HERE and not in SetDamper: g_pctDamper (slider #7b) is the user's word on the
   wheel's damper in EVERY vehicle and it belongs at the output choke point; this is a property of
   the VEHICLE and belongs at the coefficient source, before the fade blend, so the standstill and
   moving terms can move independently - which is the whole point of there being two of them.
   Multiplication is associative here and no floor sits between the two (the v7.52 kick floor is
   inside SetMag and the damper does not pass through it), so the order costs nothing and the
   final coefficient is base * truck% * damper%.

   FRICTION IS DELIBERATELY NOT SCALED. It shares hvS/hvM below, and friction was rejected on
   trucks by feel over eight driven profiles on 2026-07-23. Every friction constant here is 0, so
   scaling it would be arithmetic on nothing today - and a live wire for whoever raises one later.
   These two factors are applied to the damper terms only.

   The arithmetic these sliders move, from the same handover section, and it is arithmetic off the
   shipped constants rather than anything anyone has felt: a DI condition is
   force = coefficient * deflection clamped at MAX_STEER_SAT, so the coefficient is a SLOPE and
   the real question is where it saturates. K_DAMP_DI_STATIC 20000 already saturates at 50% of
   travel at 100%, so the standstill slider feels non-linear (25% of travel at 200%, 12.5% at
   400%); K_DAMP_DI_MOVING 5250 is still under the ceiling at full travel, so the moving slider is
   proportional across its whole range. 400% is the recommended ceiling for exactly that reason -
   past it the standstill damper is saturated inside the first eighth of the travel and the slider
   stops changing the response. Nothing above 400 is refused, though: a typed value is allowed,
   a slider whose top half does nothing is not. */
/* A percent as a float factor, exact at the default. The `== 100` branch is not an optimisation:
   an exact 1.0f multiplied into a float is exact, so the whole damper chain is bit-identical to
   the build before any of these controls existed whenever every one of them is at its default. */
static float PctF(volatile LONG pct)
{
    if (pct == 100) return 1.0f;
    return (float)pct * 0.01f;
}

static float TruckDampX(volatile LONG pct)
{
    if (!g_heavy) return 1.0f;
    return PctF(pct);
}

static float WallowGain(void)
{
    LONG s = g_dorSlot;
    float g = (s >= 0 && s < N_DOR_SLOTS) ? g_wallowK[s] : 1.0f;
    g *= (float)g_wallowTrim * 0.01f;
    /* 2.5x puts the observed peak (3888) at 9720, just under the ceiling. Higher would only
       buy clipping, which is what flattens one effect into another. */
    if (g > 2.5f) g = 2.5f;
    if (g < 0.5f) g = 0.5f;
    return g;
}

/* ---- v824: THE CENTRING GROWS ABOVE 600 BY THE SAME LAW AS THE ROLL ----
   His verdict on ours-040 (translated): "from 600 and up the centring force needs our law... at
   900 it returns to centre too reluctantly", and in the drive: this cube, set this value
   of the cube. The same centring force at 900 is spread over 1.5x the wheel travel, so near centre it is
   two thirds as stiff per degree and has further to come back against the same damper. The SAT and
   the caster - the pair that centres - are multiplied by (R/600)^(1/3), exactly g_wallowK's rows;
   crashes and roll are NOT touched by this (crashes and rolls to stay unchanged), and below 600 it is 1.
   Applied where the three ground forces are summed, so the texture (detail) keeps its own scale. */
static const float g_centreK[N_DOR_SLOTS] = {
    1.000f,   /*   90 */
    1.000f,   /*  360 */
    1.000f,   /*  600 - the reference */
    1.145f,   /*  900 - (900/600)^(1/3) */
    1.216f,   /* 1080 */
    1.339f,   /* 1440 - the law only, never driven */
    1.000f,   /*  540 */
    1.063f,   /*  720 */
};

static float CentreGain(void)
{
    LONG s = g_dorSlot;
    return (s >= 0 && s < N_DOR_SLOTS) ? g_centreK[s] : 1.0f;
}

#define K_INIT   0u
#define K_TELEM  1u
#define K_FIRE   2u
#define K_STEER  3u
#define K_BUMP   4u
#define K_WHEEL  5u   /* v7.32-DOR: wheel axis read-back. K_STEER is TAKEN (slip channel). */
/* a = load*10000 (all wheels), b = planted bits | wheels<<16 | frontLoad*100<<20,
   c = tan(slip)*10000 signed, d = the ground force actually added to the constant force. */
#define K_GROUND 6u
/* v7.32-DOR: K_WHEEL adds a second 8 ms stream beside the 0x75 telemetry, so the old cap of
   200000 records would have run out at ~13 minutes - half a drive that has five ranges to
   walk. And the cap is per PROCESS, not per run: he intends to replay the same mission three
   times from the menu without closing the game, which is one continuous log, so the budget has
   to cover all three plus the menu time between them. 1200000 x 24 B = 28.8 MB, and the cap
   stays graceful (logging stops, the mod keeps running). */
#define MAX_REC  1200000

/* ---- car-object capture ---- */
/* ===================== gog-patcher fork: the build table ==============================
   The two addresses this mod depends on are different in every Game.exe build, and the
   GOG exe is a DIFFERENT BUILD rather than ours minus patches - its .text is 0x18000
   smaller and ours carries SecuROM sections GOG's does not. Remapped with
   src\sigremap.py; evidence in docs\FFB-PORT.md.

   HOW THE BUILD IS RECOGNISED, and why not by a FILE md5 or by size.
   Neither of those says anything about whether the ADDRESS is right - they only say the
   file is the one someone measured once. So the mod verifies the thing it actually cares
   about: the 40 bytes of code around the probe site. Those bytes are byte-identical
   between the two builds (verified: 0 differences), so one signature covers both, and a
   match means the address about to be patched really is that code.

   A build whose signature does not match is NOT patched and NOT guessed at. The mod logs
   and disables itself. That is the rule for a patcher that will meet builds its author
   never saw.

   WHAT IS STORED IS THE DIGEST OF THOSE 40 BYTES, NOT THE BYTES, since 2026-08-14.
   The 40 bytes are the game's own machine code, and this file is published. They are a
   locator rather than anything of value, but they are avoidable, and the argument for
   keeping them is not. The check has only ever been an equality test, so hashing loses
   nothing: a build whose code at the probe site differs still fails to match and still
   disables the mod rather than guessing. The digest was taken from the bytes that stood
   here, so the same two builds match and no third one starts to. */
#define PROBE_SIG_BACK   15u    /* the signature starts this far BEFORE the probe site */
#define PROBE_SIG_LEN    40u
#define PROBE_SIG_MD5    "9c2540ce199ba8edaca4761259b4c199"

/* mtxOff is the world rotation matrix inside the LS3DF frame at [car+0x58]. It is NOT the
   same on the two builds and that is not a detail - everything lateral hangs off it:
   slip, the roll and pitch road channels, curbs, tram rails.
   Custom uses +0x164. On GOG that offset reads zeros, so a runtime hunt scanned the frame
   for orthonormal non-identity 3x3 blocks and found three candidates - 0x09C, 0x0E4, 0x0F8.
   Shape alone could not choose between them; a bounding volume is orthonormal too. What
   chose was MOVEMENT: over one drive, atan2(m[6], m[8]) swung 49 degrees at 0x0E4 and
   exactly 0.0 degrees at the other two. The world matrix is the one that turns with the
   car. Stride 3 (packed), same as custom. */
/* gearOff: the current-gear int inside the LS3DF frame at [car+0x58], derived offline from
   captured snapshots. Custom +0x648, GOG +0x5D0 (delta -0x78, NOT the same shift the matrix
   took, so the two are derived independently - see docs/SHIFTER-ROUTE-B.md). Only used by the
   gear-probe channel (log code 0x76); nothing in the FFB path depends on it. 0 = not derived
   for this build -> the channel logs a "no offset" marker once and stays quiet. */
typedef struct { const char *name; DWORD gamePtr; DWORD carProbe; DWORD mtxOff; DWORD gearOff; } BuildEntry;
static const BuildEntry g_builds[] = {
    { "custom", 0x65115Cu, 0x5E2061u, 0x164u, 0x648u }, /* the older non-GOG build             */
    { "gog",    0x63788Cu, 0x5A6619u, 0x0E4u, 0x5D0u }, /* stock GOG release                  */
};
#define N_BUILDS 2

static DWORD VA_CAR_PROBE = 0;
static DWORD GEAR_OFF     = 0;   /* set at DllMain from the build table */
static int   g_buildIdx   = -1;
#define SNAP_BYTES    0x2000u
#define SNAP_MS       50
#define CAR_FRESH_MS  5000u  /* increased from 1000: racing tracks have few dynamic objects */

static volatile DWORD g_car      = 0;
static volatile DWORD g_car_tick = 0;
static volatile DWORD g_hookHits = 0;   /* DIAG: how many times HookCar/0x5E2061 fired */
static HANDLE         g_snap     = INVALID_HANDLE_VALUE;
static volatile DWORD g_snapcount = 0;

/* ---- Orientation matrix (F1), found v6.19 MatrixHunt ---- */
#define FRAME_PTR_OFF  0x58u    /* car+0x58 -> LS3DF frame pointer */
/* gog-patcher fork: no longer a constant - it differs per build. Set at DllMain from the
   build table (custom 0x164, gog 0x0E4). 0 until then, which reads as "no matrix". */
static DWORD MATRIX_OFF = 0;
#define MTX_MAX_REC    400000u  /* ~24MB total cap (now 50ms cadence) */
#define MTX_EVERY      1        /* MatrixLog every snap cycle (~50ms) */
#define CARPOS_X_OFF   0x2A18u  /* world X (confirmed) */
#define CARPOS_Y_OFF   0x2A10u  /* world Y (confirmed) */

/* ---- v7.20: the other two REAL pointers out of the car struct ----
   The 2026-07-22 pointer census cut the
   old script's useless ~626 "pointers" down to six, by the one filter that separates a heap
   address from a float constant: a heap address DIFFERS between game launches, a constant
   baked into the binary does not. car+0x58 was the positive control and passed. These two
   are the other survivors, and both outrank 0x58 as F7 suspects. */
/* ---- v7.22 F9: the IN-VEHICLE GATE ----
   The bug it fixes: the game reuses ONE player-vehicle slot whose address never changes, so
   GetCarPtr() keeps returning the car the player just left. The wheel stayed HEAVY while he
   walked around, and F3/F5 kicks fired on whatever happened to the abandoned car - shoving it
   on foot set off the roll channel.

   The signal, established on the reference drive logs/2026-07-22-v721-F9-GATE/: the FRAME
   POINTER [car+0x58] is non-null essentially all the time while seated and almost never while
   on foot. Measured presence there:
       driving                        100%
       sitting in a PARKED, IDLE car   83-100%   <- the case that breaks naive gates
       on foot                         5%, 10%, 5%
   With a 1 s window, 1 s dwell and a 0.70 threshold: ZERO of 4177 labelled samples would
   drop the wheel while driving, and it is ~0.6 s late releasing after an exit. That is the
   asymmetry we want - never wrong in the dangerous direction.

   WINDOW LENGTH: the reference numbers come from 50 ms snapshots, while this runs at
   POLL_MS=8. A FRACTION over a fixed wall-clock second should carry across, but it is a
   1 s / 8 ms = 125-sample sum here versus 20 samples offline, so the nulling pattern could
   look different at the finer rate. The threshold is deliberately kept at the offline
   optimum rather than tuned tighter, and the fail-open rule below covers the bad case. */
#define GATE_WIN_TICKS   125    /* 1.0 s at POLL_MS = 8                                    */
#define GATE_NEED_PCT     70    /* >= 70% of the window present -> in vehicle              */
#define GATE_DWELL_MS   1000u   /* a new state must hold this long before it is adopted    */

/* ---- v7.23 F8: HEAVY VEHICLE CLASS, read from the same scene node ----
   Settled 2026-07-23 by direct measurement. Across FIVE
   different passenger cars the scene node agrees on 853 dwords; the flatbed truck differs on
   38 of them; the FIRE truck - a different heavy vehicle, different session, different build -
   holds the flatbed's exact value on 29 of those 38. Those 29 are the table below.

   VOTE, do not gate on one field. Several of the 29 look like speeds (90 vs 140, 120 vs 200,
   70 vs 100), so a genuinely fast car could cross a single-field threshold and get truck
   steering. Requiring a MAJORITY to match the heavy value is robust to any one field being
   model-specific rather than class-specific.

   Read once per new scene-node pointer, not per tick: the values are static for the life of
   the vehicle object. Highest offset read is 0x1F18, inside the 8 KB the snapshot already
   dumps, and the read is VirtualQuery-guarded like every other pointer deref here. */
#define VCLASS_N 29
#define VCLASS_NEED 15          /* majority of 29 -> heavy                                 */
#define VCLASS_SETTLE 25        /* ticks the frame pointer must hold before we read it      */
/* v7.25c BISECT FLAGS. Everything added since the v7.22 reference can be compiled out from
   the command line (-DVCLASS_ENABLE=0 ...), so a crash can be bisected in automated 30 s
   launches instead of one hand-driven session per hypothesis. Default is ON = the full build. */
#ifndef VCLASS_ENABLE
#define VCLASS_ENABLE 1
#endif
#ifndef FRIC_ENABLE
#define FRIC_ENABLE 1
#endif
/* FRIC_ENABLE creates the effect; FRIC_USE decides whether SetFriction ever touches it.
   Split so the bisect can tell a wheel with no friction effect at all apart from
   a wheel where calls on it are wrong - the second is fixable, the first ends the idea. */
#ifndef FRIC_USE
#define FRIC_USE 1
#endif
#ifndef PROFILE_ENABLE
#define PROFILE_ENABLE 1
#endif
static const DWORD kVClassOff[VCLASS_N] = {
    0x054Cu, 0x056Cu, 0x0628u, 0x06A8u, 0x0B48u, 0x0B58u,
    0x0B5Cu, 0x0E78u, 0x0E84u, 0x0F30u, 0x1018u, 0x101Cu,
    0x13D0u, 0x1458u, 0x1494u, 0x14BCu, 0x14D4u, 0x14D8u,
    0x14E0u, 0x1504u, 0x1648u, 0x1D68u, 0x1D98u, 0x1DC0u,
    0x1DD8u, 0x1DE8u, 0x1DECu, 0x1EF0u, 0x1F18u,
};
static const DWORD kVClassHeavy[VCLASS_N] = {
    0x0000000Fu, 0x430C0000u, 0x40400000u, 0x43480000u,
    0x3F333333u, 0x3E4CCCCDu, 0x3E4CCCCDu, 0x430C0000u,
    0x43480000u, 0x40400000u, 0x00006785u, 0x00006785u,
    0x41A00000u, 0x0000000Fu, 0x41F00000u, 0x41F00000u,
    0x420C0000u, 0x41300000u, 0x42C80000u, 0x41C80000u,
    0x7661772Eu, 0x00000000u, 0x42B40000u, 0x428C0000u,
    0x3F333333u, 0x3E4CCCCDu, 0x3E4CCCCDu, 0x00000000u,
    0x00000000u,
};

/* His spec, verbatim intent (2026-07-22): trucks 75% heavier in motion, twice as heavy
   standing, with the same ratio for turning speed - so the SPRING scales with the moving
   damper and the spring/damper ratio, which sets the wheel's return speed, is unchanged.
   The spring is ~0 at standstill, so only the moving regime has a ratio worth preserving,
   and there it stays exact.
   NOTE K_DAMP_DI_STATIC is already 20000, i.e. above the nominal 0..10000 DI range - it is a
   SLOPE and MAX_STEER_SAT caps the output, so it saturates sooner rather than being invalid.
   40000 is untested with this driver; if the wheel misbehaves standing still in a truck,
   this multiplier is the first thing to halve. */
#define HEAVY_DAMP_STATIC_X  2.00f
/* v7.24, from the FEEL on the v7.23 drive: in motion it needed to be 30% heavier, not 70%.
   Standing stays x2 - he did not ask for that to change. The spring follows the MOVING factor,
   as it did at 1.75, so the spring/damper ratio and therefore the wheel's return speed are
   still unchanged; only the weight comes down. 5250 -> 6825 moving, 4500/2250 -> 5850/2925. */
#define HEAVY_DAMP_MOVING_X  1.30f
#define HEAVY_SPRING_X       1.30f

#define COMP_PTR_OFF   0x54u    /* car+0x54 -> an object the struct stores THREE times
                                   (0x54 == 0xE4 == 0xE8 in 99.1-99.8% of samples, verified
                                   on 4 drives). Three cached handles to one object is what
                                   you get when render, physics and damage code all want it. */
#define LIST_BEG_OFF   0x59Cu   /* car+0x59C / car+0x5A0 -> a begin/end PAIR: the delta is a
                                   near-constant 328 or 352 bytes (both values seen inside
                                   v74r alone). Same shape as car+0x14 / car+0x18, the
                                   contact list that cracked F3 - and F3's answer turned out
                                   to be PER-PART collision data. A bullet hole is per-part
                                   visual damage, so this is the top structural suspect. */

static HANDLE         g_mtx      = INVALID_HANDLE_VALUE;
static volatile DWORD g_mtxcount = 0;

typedef struct {
    DWORD tick, slot, matoff, ptr;
    float m[9];
    float carX, carY;
} MtxRec;   /* 60 bytes */

/* ---- F1 lateral-slip tuning ---- */
#define SLIP_HEAD_OFFSET  1.559f  /* calibrated heading-course bias (rad); auto-refines   */
#define SLIP_MIN_SPEED     4.0f   /* m/s: below this, course from position is too noisy    */
#define SLIP_WIN          15      /* poll samples (~120ms at 8ms) for the course window    */
/* v7.1g: values verified offline against the archived slide drive (mtx_slip_verify.py):
   engages smoothly only in real slides above ~25 km/h GROUND speed, 0 rapid flips.
   The gate is on disp2 (ground displacement over the window), NOT wheel speed - a
   handbrake slide locks the wheels (spdEma->0) while the car still moves fast, so a
   wheel-speed gate would miss exactly those slides. */
#define SLIP_MIN_DISP2    0.70f   /* ~25 km/h ground over the 120ms window: speed gate + course-trust */
/* v7.2 BELL curve (real-SAT shape, user-requested "podvodka"): the wheel LOADS UP above
   normal as slip builds toward the grip peak, tips over at the peak, then progressively
   lightens - the rising phase is the felt approach-to-breakaway warning. Matches real SAT
   (linear rise -> peak -> falloff) and GTA5 MT's bell. Verified offline on BOTH archived
   datasets (multi-dataset rule): 0 flips, quietBad <=0.55%, load-up 11-20% (cornering).
   Shape: 1.0 below RISE; -> PEAK_MULT at PEAK; falls at FALL_K past peak to floor.
   slip->mult: 5:1.08  8:1.20  10:1.13  15:0.95  20:0.78  25:0.60  30:0.43  37+:0.20.
   Spring gets the full bell (max 6750*1.2=8100 < DI 10000); damper is capped at 1.0. */
#define SLIP_RISE_DEG      3.0f   /* rise starts (above the 2.2-2.5 deg noise median)      */
#define SLIP_PEAK_DEG      8.0f   /* grip-peak feel: wheel is heaviest here                */
#define SLIP_PEAK_MULT     1.2f   /* load-up factor at the peak                            */
#define SLIP_FALL_K      0.035f   /* fall per deg past peak (floor 0.2 reached ~37 deg)    */
#define SLIP_LAT_MIN_MULT  0.2f   /* floor: lightest the wheel gets at full slide          */
#define SLIP_SMOOTH_ALPHA 0.15f   /* EMA on the applied slip mult (gentle onset/offset, no judder) */

/* ---- v7.4f F2 road wallow: TWO channels summed on the ConstantForce -------------------
   PROVEN axes (sign-flip test src/axis_id.py + v7.4e feel test): m[7]=ROLL (lateral lean,
   tram/offroad side-rock), m[1]=PITCH (fore-aft: curb strikes, hills, braking nose-dive).
   Earlier notes had these two SWAPPED - do not re-swap.

   ROLL channel (m7): smooth L-R "wallowing" on tram tracks / offroad. Fast HP removes
     steady corner lean, keeps the ~1-1.5 Hz rock. Lateral, so immune to braking/hills.
   PITCH channel (m1): sharp curb "tuk". FAST HP removes slow hills (seconds) + ~half the
     braking dive while KEEPING sharp curb transients (offline: curb fast-HP is 3-7x the
     braking fast-HP). Deadzone sits above braking / below curb. SPEED-attenuated (full at
     low speed for immersion, reduced at high speed) + low CAP so a high-speed curb or jump
     cannot yank the wheel into a spin. Soft BRAKE gate (reuses the ground-speed drop already
     computed for the crash veto) knocks down the residual braking-onset dive.

   Offline-designed on 3 datasets (src/f2f_design.py): roll felt on side-rock & exactly 0 on
   smooth; pitch strong at curbs (p50~0.9-2.0k, capped 3.5k), braking p50=p90=0. Each channel
   is an independent #define block - tune or disable either alone. Summed, EMA-smoothed. */
#define F2_SMOOTH_A     0.20f     /* light EMA on the summed output (smudge in/out, no snap) */
#define F2_SLIP_GATE   15.0f      /* deg: no wallow while sliding (F1 slip owns the wheel)   */
#define F2_TOTAL_CAP 8000.0f      /* v7.4k: combined roll+pitch cap raised for stronger feel  */
/* ROLL channel (m[7] = lateral lean) */
#define F2R_M            7        /* ROLL matrix element                                     */
#define F2R_ALPHA      0.15f      /* fast HP: keep ~1-1.5Hz rock, drop steady corner lean    */
#define F2R_DEADZONE   0.006f     /* v7.4l: lower so the small tram signal passes             */
/* v7.4l COMPRESSIVE (soft-knee) roll response: boost SMALL signal (tram) ~3x while large
   signal (offroad / angled curbs) rises only ~1.2x, instead of a flat gain that would slam
   offroad. Modelled in src/roll_model.py: tram p50 72->760, offroad p50 1824->2572. */
#define F2R_GAIN1   585000.0f     /* v7.4p: tram +30%% more (offroad unchanged = user reference) */
#define F2R_KNEE_E     0.012f     /* (|rollHP|-dz) above this switches to the gentle slope    */
#define F2R_GAIN2    50000.0f     /* v7.4o: gentler above-knee                                */
#define F2R_CAP       3000.0f     /* v7.4o: HALVED - offroad was 50%% too strong (slam)       */
#define F2R_SIGN        1.0f      /* flip if the sway feels inverted vs visible body lean    */
/* v7.4g load-envelope: kill the end-of-slide/spinout roll JOLT. A slide = big lateral
   weight-transfer = big fast m7 excursion; the old hard slip-gate un-gated right as the car
   was rolling hard (and its HP state was stale) -> snap jolt. Fix: cut roll instantly on a
   slide, hold through a cooldown, then ramp back GENTLY (a soft swell "underlines" the slide
   ending, never a hit). Low-slip side-rock (tram/offroad) is untouched. Offline: v74g_verify.py
   -> roll force 0 during slides, <=750 just-after, side-rock unchanged, on 3 datasets. */
#define F2R_SLIP_ON     12.0f     /* deg: slip above this = slide -> cut roll instantly       */
#define F2R_COOLDOWN_TICKS 88     /* ~700ms @ POLL_MS=8: hold roll off after a slide          */
#define F2R_ATTACK_A    0.027f    /* per-tick gentle ramp-in after cooldown (~0.3s tau)       */
/* PITCH channel (m[1] = nose bob) */
#define F2P_M            1        /* PITCH matrix element                                    */
#define F2P_ALPHA      0.20f      /* fast HP: kill hills (slow) + ~half the braking dive     */
#define F2P_DEADZONE   0.014f     /* above braking fast-HP (~0.009), below curb (~0.025+)    */
#define F2P_GAIN     98000.0f     /* v7.4p: curbs -15%% more (user)                           */
#define F2P_CAP       6000.0f     /* cap unchanged (gain halving does the reduction)          */
#define F2P_SIGN        1.0f
#define F2P_SPD_FULL_KMH 60.0f    /* full pitch through normal city speed (curbs stay strong 30-50) */
#define F2P_SPD_MIN_KMH 110.0f    /* pitch scale hits its floor only at genuinely high speed  */
#define F2P_SPD_MINSCALE 0.60f    /* even high-speed keeps 60%% (cap 3500 is the real safety) */
#define F2P_BRAKE_GS     1.0f     /* m/s ground-speed drop = braking -> gate the pitch dive  */
#define F2P_BRAKE_SCALE  0.30f    /* pitch scale while braking (soft gate, keeps curb-hits)  */
#define SLIP_CAL_GATE     0.26f   /* rad (~15 deg): only auto-calibrate offset below this  */
#define SLIP_CAL_RATE    0.006f   /* offset EMA rate: slow, tracks drift not slide onset    */

/* ---- v7 bump-from-behind (external yaw not explained by steering -> kick) ---- */
#define YAW_GAIN         0.240f   /* expectedYaw = spdEma*steer*YAW_GAIN ~= STEER_MAX/wheelbase.
                                     Physically derived (was 0.06, 4x too low -> false kicks in
                                     corners). Calibrate exactly from K_BUMP telemetry offline. */
#define BUMP_THRESH       0.60f   /* rad/s: |disturbance| above this may fire (conservative)  */
#define BUMP_JERK         0.45f   /* rad/s step vs prev: bump is a sudden spike, a corner isn't */
#define BUMP_BODYSLIP_MAX 0.14f   /* rad (~8deg): only if not already sliding             */
#define BUMP_MAG_SCALE   14000.0f /* kick magnitude per rad/s of disturbance              */
#define BUMP_PULSE_MS       120
#define STEER_MAX_RAD     0.60f   /* v7.1: full-lock road-wheel angle (logged, not used yet) */
#define FRONT_ARM         1.30f   /* v7.1: CG-to-front-axle m (logged, not used yet)         */

/* ---- steering state ---- */

typedef struct { DWORD tick, kind, a, b, c, d; } L;

/* v7.25 buffered logging - see Log() for why.
   MUST BE LOCKED. Log() is called from THREE places at once: DllMain during init, the FFB
   thread every 8 ms, and the snapshot thread every 50 ms. The first cut of this buffer had no
   lock, and two threads doing `buf[g_logn]` then `if (++g_logn >= N) reset` will both take the
   same slot and one will skip the reset, so the index walks off the end of a 64-entry static
   array and writes over whatever follows it. That crashed the game during init, right after
   the FFB thread's startup pulse - which is exactly the moment the second thread starts
   logging. Uncontended EnterCriticalSection is tens of nanoseconds; at 125 Hz it costs
   nothing measurable. */
#define LOG_BUF_N 64
static L                g_logbuf[LOG_BUF_N];
static DWORD            g_logn = 0;
static CRITICAL_SECTION g_logcs;
static volatile LONG    g_logcs_ready = 0;
static DWORD            g_logflush = 0;   /* tick of the last flush - see Log() */

void *memset(void *d, int c, size_t n)
{ BYTE *p=(BYTE*)d; while(n--) *p++=(BYTE)c; return d; }
void *memcpy(void *d, const void *s, size_t n)
{ BYTE *p=(BYTE*)d; const BYTE *q=(const BYTE*)s; while(n--) *p++=*q++; return d; }

#ifdef MAFIA_FFB_TEST
/* THE SEAM THAT LETS A HOST TEST SEE A LOG ROW. Without it every Log() call returns at the handle
   check below - there is no file on a test host - so a row that exists only to be read later
   could be deleted entirely and nothing offline would notice. The tests assert on these. */
DWORD g_ffbTestSpikeN = 0, g_ffbTestSpikeB = 0, g_ffbTestSpikeC = 0;
LONG  g_ffbTestSpikeStep = 0;
/* v816: the vehicle-swap row, so a host test can assert the INSTRUMENT a drive is judged by and
   not only the behaviour. v815 shipped that row on a code another row wrote every second, and a
   test that checked only the pointer would have passed it. */
DWORD g_ffbTestSwapN = 0, g_ffbTestSwapB = 0, g_ffbTestSwapC = 0, g_ffbTestSwapD = 0;
#endif

static void Log(DWORD kind, DWORD a, DWORD b, DWORD c, DWORD d)
{
#ifdef MAFIA_FFB_TEST
    if (kind == K_INIT && a == 0x29) {
        g_ffbTestSpikeN++;
        g_ffbTestSpikeB = b; g_ffbTestSpikeC = c; g_ffbTestSpikeStep = (LONG)d;
    }
    if (kind == K_INIT && a == 0x37) {
        g_ffbTestSwapN++;
        g_ffbTestSwapB = b; g_ffbTestSwapC = c; g_ffbTestSwapD = d;
    }
#endif
    /* v7.25: BUFFERED. Until now every single record did WriteFile + FlushFileBuffers, a
       synchronous disk flush per call - which is precisely why nothing in this project could
       ever be logged at the FFB thread's own 8 ms rate, and why every offline analysis has
       had to work from the 50 ms snapshot instead. As framed plainly: what is the point of
       archives at 50 ms when the thing being designed runs at 8 ms.
       Records now accumulate and go out in one write per LOG_BUF_N, with the flush kept so a
       crash still leaves at most LOG_BUF_N records unwritten. 24 bytes x 125/s = 3 KB/s. */
    if (g_log == INVALID_HANDLE_VALUE || g_reccount >= MAX_REC) return;
    if (!g_logcs_ready) return;          /* before the lock exists, drop rather than race */
    EnterCriticalSection(&g_logcs);
    /* ---- v7.36: remember the LAST KICK, centrally ----
       Reported 2026-07-24: preference might differ between a weak crash or a tap on a pedestrian and
       a big crash; the request was to always track exactly when a tap occurs,
       on which event, at which force values.
       He is right that a mark carrying only the global multiplier is nearly useless: it says
       "I like this" without saying WHAT. Every kick in this file already logs a K_FIRE record
       whose first two fields are its code and its magnitude, so capturing it HERE catches all
       of them - including any effect added later - instead of touching nine call sites and
       missing one. */
    if (kind == K_FIRE) { g_lastKickCode = a; g_lastKickMag = b; g_lastKickTick = GetTickCount(); }
    g_reccount++;
    if (g_logn >= LOG_BUF_N) g_logn = 0; /* belt and braces - must never be true */
    DWORD nowT = GetTickCount();
    L *r = &g_logbuf[g_logn];
    r->tick = nowT; r->kind = kind; r->a=a; r->b=b; r->c=c; r->d=d;
    g_logn++;
    /* Flush when the buffer is full OR every 500 ms, whichever comes first. The timer matters:
       a crash used to throw away everything still buffered - the v7.25 crash logs were 0 bytes
       because the game died before 64 records had accumulated, which is precisely when a log
       is most needed. The timer bounds the loss no matter how slowly records arrive. */
    if (g_logn >= LOG_BUF_N || (nowT - g_logflush) >= 500u) {
        DWORD w;
        WriteFile(g_log, (LPCVOID)g_logbuf, sizeof(L) * g_logn, &w, NULL);
        FlushFileBuffers(g_log);
        g_logn = 0;
        g_logflush = nowT;
    }
    LeaveCriticalSection(&g_logcs);
}

/* flush whatever is still buffered - called on process detach so a quit does not lose the
   tail of the drive */
static void LogFlush(void)
{
    if (g_log == INVALID_HANDLE_VALUE || !g_logcs_ready) return;
    EnterCriticalSection(&g_logcs);
    if (g_logn) {
        DWORD w;
        WriteFile(g_log, (LPCVOID)g_logbuf, sizeof(L) * g_logn, &w, NULL);
        FlushFileBuffers(g_log);
        g_logn = 0;
    }
    LeaveCriticalSection(&g_logcs);
}

static int Sane(DWORD p) { return p >= 0x10000u && p < 0x7FFF0000u; }
static void *VT(void *obj, int idx) { return (void *)((DWORD *)(*(DWORD *)obj))[idx]; }
static float Clampf(float x, float lo, float hi) { return x<lo?lo:x>hi?hi:x; }
static float Absf(float x) { return x < 0.0f ? -x : x; }
/* sqrt without libm (bit-hack seed + 3 Newton). For ground-speed from position. */
static float Sqrtf(float x)
{
    if (x <= 0.0f) return 0.0f;
    unsigned int i; float y;
    memcpy(&i, &x, 4); i = 0x1FBD1DF5u + (i >> 1); memcpy(&y, &i, 4);  /* safe type-pun */
    y = 0.5f*(y + x/y); y = 0.5f*(y + x/y); y = 0.5f*(y + x/y);
    return y;
}

/* ---- v7.88 the impulse channel's witness, measured from POSITION -----------------------------
   Speed over the `span` ticks ending at ring position `endCount`, or -1 when the ring does not
   hold that much. PURE, and kept pure on purpose: it is the half of the witness a host can test
   with no game, no wheel and no clock - hand it three arrays and it answers.
   `endCount` is the monotone counter, masked here, so no caller juggles ring indices. */
static float ImpRingSpeed(const float *px, const float *py, const float *dt,
                          int endCount, int span)
{
    int i, last, first;
    float t = 0.0f, dx, dy;
    if (span < 1 || span > 24 || endCount < span + 1) return -1.0f;
    last  = (endCount - 1) & IMP_RMASK;
    first = (endCount - 1 - span) & IMP_RMASK;
    for (i = 0; i < span; i++) t += dt[(endCount - 1 - i) & IMP_RMASK];
    if (t <= 0.0f) return -1.0f;
    dx = px[last] - px[first];
    dy = py[last] - py[first];
    return Sqrtf(dx * dx + dy * dy) / t;
}

/* Did the car's speed actually CHANGE, and is it still changed? 1 lets the kick out. */
static int ImpulseConfirmed(const float *px, const float *py, const float *dt,
                            int candCount, int nowCount, int span, float dvMin)
{
    float vB = ImpRingSpeed(px, py, dt, candCount, span);
    float vA = ImpRingSpeed(px, py, dt, nowCount, span);
    float d;
    if (vB < 0.0f || vA < 0.0f) return 0;
    d = vA - vB;
    if (d < 0.0f) d = -d;
    return d >= dvMin ? 1 : 0;
}

/* ---- device discovery ---- */

static BOOL CALLBACK EnumWndCb(HWND h, LPARAM l)
{
    (void)l;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
        g_hwnd = h;
        return FALSE;
    }
    return TRUE;
}

/* One hex nibble, or -1. No CRT: this file builds -nostdlib. */
static int HexNib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` into a GUID_. Returns 1 on success.
 *
 * STRICT ON PURPOSE, and it returns 0 rather than a half-filled GUID. A partially parsed GUID
 * would select nothing and fall back - the same outcome as no key at all - but it would do so
 * for a reason the log could not distinguish from the device being unplugged, and those two want
 * different advice. The braces are optional because half the places Windows prints a GUID omit
 * them, and a user copying one out should not have to know which half they used.
 */
static int ParseGuid(const char *s, GUID_ *out)
{
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '{') s++;

    DWORD a = 0;
    for (int i = 0; i < 8; i++) { int n = HexNib(*s++); if (n < 0) return 0; a = (a << 4) | (DWORD)n; }
    if (*s++ != '-') return 0;

    WORD bc[2] = { 0, 0 };
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 4; i++) { int n = HexNib(*s++); if (n < 0) return 0;
                                      bc[k] = (WORD)((bc[k] << 4) | n); }
        if (*s++ != '-') return 0;
    }
    /* The last eight bytes are printed as 4 hex digits, a dash, then 12 - but they are just
       eight bytes in order, so the dash is skipped rather than given meaning. */
    BYTE d[8];
    for (int i = 0; i < 8; i++) {
        if (i == 2) { if (*s == '-') s++; }
        int hi = HexNib(*s++); if (hi < 0) return 0;
        int lo = HexNib(*s++); if (lo < 0) return 0;
        d[i] = (BYTE)((hi << 4) | lo);
    }
    out->a = a; out->b = bc[0]; out->c = bc[1];
    for (int i = 0; i < 8; i++) out->d[i] = d[i];
    return 1;
}

static int GuidEq(const GUID_ *x, const GUID_ *y)
{
    if (x->a != y->a || x->b != y->b || x->c != y->c) return 0;
    for (int i = 0; i < 8; i++) if (x->d[i] != y->d[i]) return 0;
    return 1;
}

/* The enum no longer stops at the first device unless it has no reason to keep looking.
 *
 * Returning DIENUM_CONTINUE while searching is the whole change, and it is also the whole risk:
 * the old code could not see a second device, so it could not be confused by one. Every path
 * below therefore ends with g_found set from exactly one of two places - the match here, or the
 * explicit fallback after the enum - and both say so in the log. */
static BOOL __stdcall EnumDevCb(const DIDEVINST *ddi, void *ctx)
{
    (void)ctx;
    g_devSeen++;

    /* Every device, chosen or not. Two records because a GUID does not fit in one: this is what
       a user reads to find the string to put in `device=`, so it is not optional detail. */
    Log(K_INIT, 0x11, (DWORD)g_devSeen, ddi->dwDevType, ddi->guidInstance.a);
    Log(K_INIT, 0x12, (DWORD)((ddi->guidInstance.b << 16) | ddi->guidInstance.c),
        *(const DWORD *)&ddi->guidInstance.d[0], *(const DWORD *)&ddi->guidInstance.d[4]);

    if (!g_haveFirst) {
        g_haveFirst = 1;
        g_firstGuid = ddi->guidInstance;
        for (int i = 0; i < 259 && ddi->tszProductName[i]; i++)
            g_firstName[i] = ddi->tszProductName[i];
    }

    if (!g_haveWant) {
        /* No preference expressed: this is the old behaviour, byte for byte - take the first and
           stop. Deliberately not enumerating everything and then taking the first, because that
           changes how long the enum runs and what a flaky driver gets asked, on every rig,
           to buy nothing. */
        g_wheel = ddi->guidInstance;
        g_found = 1;
        for (int i = 0; i < 259 && ddi->tszProductName[i]; i++) g_wheelName[i] = ddi->tszProductName[i];
        Log(K_INIT, 0x10, ddi->dwDevType, 0, 0);
        return DIENUM_STOP;
    }

    if (GuidEq(&ddi->guidInstance, &g_wantGuid)) {
        g_wheel   = ddi->guidInstance;
        g_found   = 1;
        g_matched = 1;
        for (int i = 0; i < 259 && ddi->tszProductName[i]; i++) g_wheelName[i] = ddi->tszProductName[i];
        Log(K_INIT, 0x13, ddi->dwDevType, (DWORD)g_devSeen, 0);
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}

/* ---- effect helpers ---- */

static HRESULT EnsureAcquired(void)
{
    if (!g_dev) return (HRESULT)0x80070015;
    return ((Acquire_t)VT(g_dev, DEV_ACQUIRE))(g_dev);
}

static LONG g_curmag = 0x7FFFFFFF; /* force first write */

/* ---- THE DEVICE CAN BE TAKEN FROM US MID-DRIVE, and until ours-019 nothing noticed ----------
   `SetMag` has always called `EnsureAcquired()` before every write, which is why this needed
   more than one line: once another process holds the wheel EXCLUSIVELY, `Acquire` cannot take it
   back - it returns success on a device we do not own, and every `SetParameters` after it fails
   with DIERR_NOTEXCLUSIVEACQUIRED. The only thing that works is to let go and take it again.
   Counters, so the state is a number rather than a guess. */
static volatile LONG  g_effFailN  = 0;     /* consecutive SetParameters refusals               */
static volatile LONG  g_effFailHr = 0;     /* the last one's HRESULT                           */
static volatile DWORD g_effFailAt = 0;     /* when it happened                                 */
static volatile LONG  g_recovN    = 0;     /* recovery attempts                                */
static volatile LONG  g_recovOk   = 0;     /* ... that got the device back                     */
/* ini, read in GroundReadIni with the rest of the live-tuned keys. 0 = never attempt recovery,
   which is the escape hatch if a driver ever dislikes being unacquired mid-flight. */
static volatile LONG  g_recoverMs    = 2000;  /* no more often than this                       */
static volatile LONG  g_recoverAfter = 8;     /* refusals in a row before we act               */

/* ---- v7.56: a kick must be a REVERSAL, not more of the same ----
   Ported from upstream v7.56 (gog-patcher 2026-08-03), upstream's own measurement kept.
   Measured 2026-08-01. His 373.5 s side impact commanded 16374 for 400 ms - the longest
   kick of that drive - and he felt nothing: "the car was thrown, but there was no feedback
   at all". `K_WHEEL` shows why. The continuous self-aligning-torque channel had already
   ramped the wheel to -2953 in the SAME direction, so the kick was a change of 13421 in a
   direction the wheel was already being driven. The 20564 kick he liked, 33 s earlier,
   fired from a neutral wheel.
   The sign came from blind `g_flip` alternation with no reference to the standing force, so
   whether a crash reads as a reversal or as more-of-the-same was a coin toss - and worst
   exactly when the car is thrown sideways, because that is when SAT loads the wheel hardest.
   Priced over every log with a wheel channel (upstream src/v756_price_signfix.py): 36
   measurable sharp kicks, 6 (17%) land WITH the standing force and flip here; the other 30
   are BIT-IDENTICAL. No magnitude changes, no duration changes.
   Falls back to the old alternation when the wheel is neutral, so a kick from rest behaves
   exactly as before. Applied to the two CRASH sites only - the taps, the bullet kick, the
   soft nudge and F5 keep their old behaviour, because he has never complained about their
   direction and the standing rule is that a fix touches one effect.
   Fork note: `g_curmag` is the value AFTER SetMag's multiplier chain (master percent,
   DorScale, kick floor), i.e. what actually reached the wheel. That is the right input -
   the question this answers is which way the wheel is being pushed right now, not what
   some caller asked for. */
static LONG KickSigned(LONG mag)
{
    LONG standing = g_curmag;
    if (standing == 0x7FFFFFFF) standing = 0;   /* nothing written yet */
    if (standing > 0) return -mag;
    if (standing < 0) return  mag;
    g_flip = !g_flip;
    return g_flip ? mag : -mag;
}

/* v7.22 F9: 1 while the player is IN a vehicle, 0 while on foot. Starts at 1 and FAILS OPEN
   (see UpdateVehicleGate). Every force in this file passes through SetMag/SetSpring/SetDamper,
   so gating those three is the whole fix - no need to touch the six separate places that
   fire kicks, and no risk of missing one. */
static volatile int g_inVehicle = 1;

static void SetMag(LONG mag)
{
    /* On foot: force the constant force to ZERO rather than skipping the call. Skipping
       would leave whatever force was last set standing on the wheel. */
    if (!g_inVehicle) mag = 0;
    /* v7.33: the ONE place the range scale is applied. Every kick, tap, wallow and slide
       nudge in this file goes through SetMag, so scaling here cannot miss one - and it cannot
       accidentally touch the spring or the damper, which the measurement says already deliver
       the same physical force per degree at any range.
       The multiply is SKIPPED at k == 1 so that a 600+ drive is bit-identical, not
       merely arithmetically equal. */
    if (mag) {
        /* M1: Master slider, the one choke point every constant force in this file passes
           through (handover section 2.3 row 1). Order settled offline (section 3):
           multiplier -> DorScale -> SCALED floor. The floor scales too, or a channel turned
           down at 90/360 deg is silently restored to 1300/1500 and the slider does nothing. */
        mag = Mul(mag, g_pctMaster);
        float k = DorScale();
        if (k != 1.0f) mag = (LONG)((float)mag * k);
        /* v7.52: lift a KICK that the range scale has pushed under the perceptibility
           floor. Kicks only - g_continuous marks the one wallow/spin/SAT call. Applied
           after the scale, because the floor is about the force that actually reaches his
           hands, not the force the effect asked for. */
        if (!g_continuous && g_dorSlot >= 0 && g_dorSlot < N_DOR_SLOTS) {
            LONG fl = g_kickFloor[g_dorSlot];
            if (fl) {
                fl = Mul(fl, g_pctMaster);
                LONG a = mag < 0 ? -mag : mag;
                if (a < fl) mag = (mag < 0) ? -fl : fl;
            }
        }
    }
    if (!g_eff || mag == g_curmag) return;
    g_curmag = mag;
    g_cf.lMagnitude = mag;
    EnsureAcquired();
    HRESULT hr = ((SetParams_t)VT(g_eff, EFF_SETPARAMETERS))(
        g_eff, &g_eparm, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
    g_lastEffectHr = (LONG)hr;   /* M1b: the only line the status file needs from in here */
    /* ---- THE DEVICE'S ANSWER IS A MEASUREMENT AND IT WAS NEVER COUNTED --------------------
       0xEE has been written since v7.x and nothing ever read it. Drive ours-019: from 217.3 s
       to the end, 1972 consecutive refusals, every one 0x80040205 = DIERR_NOTEXCLUSIVEACQUIRED,
       and the last thirty-five seconds of his drive reached the wheel as nothing at all while
       this module went on computing forces and logging them. He felt it and said so; the audit
       passed the run. A log that records what we SENT and never whether it was ACCEPTED is an
       instrument with a blind side exactly where the hardware fails.
       These two counters are what the recovery below is driven by, and what the audit reads. */
    if (hr < 0) {
        Log(K_FIRE, 0xEE, (DWORD)mag, 0, (DWORD)hr);
        g_effFailHr = (LONG)hr;
        g_effFailAt = GetTickCount();
        if (g_effFailN < 0x7FFFFFF0) g_effFailN++;
    } else if (g_effFailN) {
        Log(K_INIT, 0x27, (DWORD)g_effFailN, (DWORD)g_effFailHr, (DWORD)mag);
        g_effFailN = 0;
    }
}

static LONG g_curSpring = -1;
static void SetSpring(LONG coeff)
{
    /* M1: slider #7a (handover section 2.3 row 7, open decision #2 - spring and damper are
       two separate controls). Scales K_CENTER_DI / K_HISPEED_DI, whichever the caller passed. */
    coeff = Mul(coeff, g_pctSpring);
    if (!g_spring || coeff == g_curSpring) return;
    g_curSpring = coeff;
    g_springCond.lPositiveCoefficient = coeff;
    g_springCond.lNegativeCoefficient = coeff;
    ((SetParams_t)VT(g_spring, EFF_SETPARAMETERS))(
        g_spring, &g_springParm, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
}

static LONG g_curDamper = -1;
static void SetDamper(LONG coeff)
{
    /* NO percent is applied here, and that is the 2026-07-27b change. The damper became TWO
       controls - standing and moving - and by the time a coefficient reaches this function the
       two have already been blended by speed into one number, so there is nothing left to tell
       them apart. Both the car percents and the truck multipliers are therefore applied at the
       COEFFICIENT SOURCE (search dStatic / dMoving), which is the last place they are separate.
       The two callers that pass K_DAMP_DI_STATIC directly scale it themselves - they are the
       standstill damper by definition and there is no blend to undo. */
    if (!g_damper || coeff == g_curDamper) return;
    g_curDamper = coeff;
    g_damperCond.lPositiveCoefficient = coeff;
    g_damperCond.lNegativeCoefficient = coeff;
    ((SetParams_t)VT(g_damper, EFF_SETPARAMETERS))(
        g_damper, &g_damperParm, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
}

/* v7.24 FRICTION - the effect that actually makes a wheel HEAVY to turn.
   Established by feel on the v7.23 drive: K_DAMP_DI_STATIC 20000 and 40000 are
   indistinguishable, because a damper's force is proportional to how FAST the wheel is being
   turned and both values reach the 10000 output ceiling at ordinary turning speeds. The
   coefficient is a slope, not a force, so doubling it only moves where it saturates.
   Real steering weight at a standstill is tyre SCRUB - roughly the same force whether you
   turn slowly or quickly. That is friction, not damping.

   THE STRENGTH KNOB IS THE SATURATION, NOT THE COEFFICIENT. Drivers implement friction as a
   force opposing the direction of motion that reaches its ceiling almost immediately, so the
   coefficient is left high (reach it fast) and the saturation sets how heavy it actually is.
   This is the opposite of how the spring and damper here are driven, and mixing the two up
   would produce another knob that does nothing.

   CARS GET ZERO. That is deliberate: with 0 saturation the effect contributes no force at
   all, so the approved reference feel is untouched and this build can only differ from
   v7.23 in a truck. */
#define FRIC_COEFF        10000  /* reach the ceiling immediately - saturation is the knob   */

/* ---- v7.24 A/B FEEL PROFILES, switched live on F1..F4 ----
   Nobody can answer "damper or friction" from a description - as established - and two
   blind drives would answer it badly. So the four candidates ride in one build and he switches
   between them on the same corner, with the REFERENCE always one key away.

   Standing:moving ratio for friction is the damper's own (5250/20000 = 26%) and it fades on
   the same DAMP_FADE_KMH ramp, so the curve genuinely stays the same.

   THE FRICTION NUMBERS ARE THE FIRST CALIBRATION OF THIS EFFECT IN THE PROJECT. There is no
   prior data point. 3500 is an estimate of what damper 20000 delivers at a typical hand speed
   (it saturates at half of full-scale turning velocity, so an ordinary turn sits around
   30-40% of device force). It may be out by a factor of two either way - hence the live trim
   below, which is what actually turns this drive into a number. */
/* v7.28: the F1-F4 ladder now scales the SELF-ALIGNING TORQUE, not friction.
   His hard constraint, stated after seeing this design: "the most important thing is not to lose that
   subjective feeling of a LIGHT WHEEL IN A DEEP SLIDE... we spent a lot of time getting
   that feeling". A sustained force during a slide is exactly what could mask it,
   and no archive can answer whether it does - that is a hands question.
   So F1 is the untouched reference and F2-F4 are a strength ladder, switchable mid-slide, so
   he can pull the handbrake and flip between them on the SAME slide. Friction stays at 0 in
   every profile: he never got to test it, and mixing two open questions into one drive is how
   both end up unanswered. */
/* The ladder varies TWO things, not one, because his constraint is specifically about the
   DEEP slide: the peak force, and how much of it survives once the car is fully sideways.
   F4 is the interesting one - strong where the tyre is at its peak (the moment that tells you
   to catch the slide) and then almost gone by the time the car is properly sideways, which is
   exactly where he wants the wheel to feel light.
   Damper and friction are identical in all four: only ONE thing changes between profiles, so
   whatever he feels can only be this. */
#define PROF_N 6
typedef struct {
    LONG  dampStatic, dampMoving, fricStatic, fricMoving, satCap;
    float satTail;      /* fraction of the SAT peak left when fully sideways */
    float slipFloor;    /* how light the F1 lightening is allowed to make the wheel */
    int   slideScale;   /* v7.31: 1 = the SOFT slide nudge follows the wheel's own weight */
} FeelProfile;
/* v7.29 ladder. He judged the v7.28 one decisively - F2 (peak 2000) got 8 LIKE marks while
   F3 (4000) and F4 (5000) got 5 NOPE between them and not one LIKE - and he diagnosed the
   reason himself: after a corner, driving straight again, the wheel "is still turning too
   slowly, centering". The log agreed exactly: 71% of all force samples were at
   slip BELOW the 15 deg deadzone, i.e. the release tail fighting the centering spring.
   So the peak is now fixed at his chosen 2000 and the ladder tests the ONE thing he was
   unsure about: whether the wheel turns too easily at the moment of maximum slide.
   Numbers behind his doubt: at 35-70 deg of slip the spring is already down to 558 and the
   damper to 1244, against 2652/5254 driving straight - the wheel really is at ~15% of its
   normal weight there.

   AND HIS DOUBT IS ABOUT A DIFFERENT QUANTITY THAN THE SAT. Spring and damper are RESISTANCE
   (how hard the wheel is to turn); the SAT is a DIRECTED force (which way it pulls). Raising
   the SAT tail adds pull, not weight. So the ladder separates them - one variable per step:
     F2 -> F3 changes ONLY the wheel's WEIGHT in a deep slide (the slip-lightening floor);
     F3 -> F4 changes ONLY how much SAT PULL survives once fully sideways.
   F1 stays the untouched reference floor. */
/* ---- v7.30: EIGHT profiles, and the question is FRICTION at last ----
   The SAT question is closed - v7.29 F2 is his declared best-ever - so satCap 2000,
   satTail 0.35 and slipFloor 0.15 are IDENTICAL in all eight and must stay that way. Only
   dampStatic / fricStatic / fricMoving move. dampMoving is 5250 everywhere, so nothing here
   can change how the wheel feels at cruising speed except fricMoving.

   His instruction, verbatim in intent: two ladders he can walk with CONSECUTIVE keys, each
   monotone, rather than alternating between two variables every key.

     F1        the untouched reference, always one key away
     F2 F3 F4  ladder A - WHERE the standstill weight comes from. Damper down, friction up,
               total weight roughly constant, so what changes is the CHARACTER: viscous
               (heavier the faster you turn) -> scrub (the same however slowly you turn).
               Friction dies by DAMP_FADE_KMH in all three.
     F5 F6 F7 F8  ladder B - HOW MUCH friction survives on the road. Base fixed at the middle
               of ladder A, fricMoving 0 -> 26% -> 50% -> 100% (100% = no speed fade at all).
               F5 is deliberately identical to F3: it is the zero of ladder B, so walking
               F5-F6-F7-F8 changes exactly one number.

   3500 is the project's only estimate of what damper 20000 delivers at an ordinary hand
   speed, and it may be out by 2x either way - that is what PageUp/PageDown is for. 9000 and
   the 5250 pairing are chosen to hold total standstill weight roughly level as the damper
   leaves; 10000 = MAX_STEER_SAT would be the ceiling.

   Known risk, stated before the drive: F4 runs the standstill damper at 3000, and the damper
   is also what suppresses parked-wheel jitter (this project has had that bug). If the wheel
   buzzes when parked on F4, that is the cause, not a new defect. */
/* ---- v7.31: FRICTION IS REJECTED, and the ladder is retired ----
   He drove all eight v7.30 profiles and chose F1, the one with no friction at all: "I liked F1
   most, so we keep our references." Trucks were settled in the same
   breath and need NO extra weight - the roll channel already makes them feel different,
   because they wallow: already feeling different regardless, since the steering does not need changing.
   So every friction number goes back to 0 and the damper back to 20000/5250 everywhere.

   The six slots stay because they were requested as future profiles (F1 to F6), and
   the marks moved to F7/F8 - F9/F10 are impossible to use, the shifter's AHK script binds
   them as emergency hotkeys and its keyboard hook eats them before the game or we ever see
   them. That is why not one mark landed in the v7.30 log.

   The ONE difference between F1 and F2 is the change he approved this session, so it can be
   judged mid-slide on one key, which is how the last two features were settled. */
static const FeelProfile g_prof[PROF_N] = {
 /*  dampS  dampM  fricS  fricM  satCap satTail slipFloor slideScale                         */
    { 20000, 5250,     0,     0,  2000, 0.35f, 0.15f, 1 },  /* F1 reference + soft slide kick */
    { 20000, 5250,     0,     0,  2000, 0.35f, 0.15f, 0 },  /* F2 the OLD kick, for A/B       */
    { 20000, 5250,     0,     0,  2000, 0.35f, 0.15f, 1 },  /* F3 spare = F1                  */
    { 20000, 5250,     0,     0,  2000, 0.35f, 0.15f, 1 },  /* F4 spare = F1                  */
    { 20000, 5250,     0,     0,  2000, 0.35f, 0.15f, 1 },  /* F5 spare = F1                  */
    { 20000, 5250,     0,     0,  2000, 0.35f, 0.15f, 1 },  /* F6 spare = F1                  */
};
/* v7.32-DOR: PINNED to 0 and nothing writes it any more - F1..F5 became range labels, so the
   approved v7.31 feel (reference + soft slide nudge) is the only feel this build can have. */
static volatile int  g_prof_i   = 0;
static volatile int  g_fricTrim = 100;   /* frozen: friction is retired, every fricStatic is 0 */
static volatile LONG g_fricNow  = 0;     /* effective saturation, for the F5 marker */
/* v7.31: the magnitude the LAST soft slide/brake nudge actually sent. Friction is retired,
   so the marks stamp this instead - it is the quantity that changed this build. */
static volatile LONG g_lastSoftMag = 0;
/* v7.49: WHEN it fired. The end-of-stop jab is defined by its distance from this. */
static volatile DWORD g_lastSoftTick = 0;
/* v7.60 fire trace. `g_trArm` is set by a fire and consumed at the top of the NEXT tick, so the
   pre-ring dump always contains the firing tick itself. `g_trLeft` counts the tail down. A
   second fire inside a running trace re-arms it - deliberately, because the jab class of
   complaint B IS two fires close together and truncating the first trace would hide it. */
static volatile DWORD g_trArm  = 0;
static volatile DWORD g_trLeft = 0;

/* v7.50: "this sharp is a duplicate answer to an event already answered, and its
   deceleration is a ramp". Both halves are required. The FIRST half is what keeps it away
   from ordinary crashes: on an open road neither timer is recent, so the shape test is
   never even consulted. Logs 0xE8 (a=ratio*100, b=ms since the nudge, c=ms since the last
   sharp) so the next drive can be audited without guessing which branch was silent. */
static int RampDup(DWORD now, float ratio)
{
    int nearNudge = (g_lastSoftTick  && (now - g_lastSoftTick)  <= BYPASS_NUDGE_MS);
    int nearCrash = (g_lastCrashTick && (now - g_lastCrashTick) <= BYPASS_NUDGE_MS);
    /* ---- v7.51: THE LONE LURCH ----
       His v7.50 drive found the case both preconditions above miss: a civilian car at
       90 km/h on a flat road, one sharp of 8652, no nudge before it and no sharp for 23
       seconds. He then drove the same road repeatedly, in the same car and in the racing
       car, and it never happened again - so it is not the road.

       The tell is already computed: `g_lastStepBase`, the DENOMINATOR of the step ratio,
       is the median deceleration over the 400 ms BEFORE the event. It says whether the
       driver was already braking steadily. That is the braking context the two timers
       were standing in for, measured directly instead of inferred from a previous kick.

       Measured over 202 archived normal-branch sharps: `base` is 0.000 at the median and
       0.050 at p90 - a crash while cruising has essentially none. His lone lurch has
       0.150, the LARGEST in the entire archive. A floor of 0.10 with the same ratio test
       vetoes 4 events and keeps both 25000 crashes that also had a braking approach
       (ratio 24.4 at base 0.120, and 12.3 at base 0.100).

       Union of the two preconditions: 7 vetoed of 202, margin 9.4 vs 12.3 = 1.31x. That
       is thinner than v7.50's 1.49x and is stated plainly - it buys the case he actually
       reported, and the ratio test still has to pass either way. */
    int braking = (g_lastStepBase >= RAMP_BASE_MIN);
    if (!nearNudge && !nearCrash && !braking) return 0;
    if (ratio >= BYPASS_STEP_MIN) return 0;
    /* v7.51: c now also carries the base x10000, so a drive can be audited on WHICH of
       the three preconditions fired without re-deriving it. */
    Log(K_INIT, 0xE8, (DWORD)(LONG)(ratio * 100.0f),
        (DWORD)(nearNudge ? now - g_lastSoftTick : (nearCrash ? now - g_lastCrashTick : 0u)),
        (DWORD)(LONG)(g_lastStepBase * 10000.0f));
    return 1;
}


/* v7.24b: ZERO MEANS STOPPED, not "saturation 0".
   Found the moment v7.24 was driven: profile F1 is meant to be the v7.22 reference exactly,
   and it did not feel like it - "it feels very bad". The only difference between the two
   builds on F1 is that v7.24 CREATES AND STARTS a friction effect and relies on saturation 0
   to silence it. Two ways that backfires, and either is enough:
     1. a driver may read saturation 0 as "no limit specified" rather than "no force", which
        would put FULL friction on the wheel exactly where we asked for none;
     2. a running effect takes a share of the device's force budget on many drivers, so merely
        having it started weakens the spring and damper beside it.
   So a zero request now stops the effect and zeroes the coefficient too, and a non-zero
   request starts it again. With friction stopped the build is byte-for-byte v7.22 behaviour
   on F1, which is the whole point of having a reference profile. */
static LONG g_curFric = -1;
static int  g_fricStarted = 0;   /* v7.25c: never Stop() an effect that was never Start()ed */
static void SetFriction(LONG sat)
{
    if (!FRIC_USE) return;
    if (!g_inVehicle) sat = 0;          /* same release-on-foot rule as every other force */
    if (!g_friction || sat == g_curFric) return;
    int wasOff = (g_curFric <= 0);
    g_curFric = sat;
    if (sat <= 0) {
        g_fricCond.lPositiveCoefficient = 0;
        g_fricCond.lNegativeCoefficient = 0;
        g_fricCond.dwPositiveSaturation = 0;
        g_fricCond.dwNegativeSaturation = 0;
        ((SetParams_t)VT(g_friction, EFF_SETPARAMETERS))(
            g_friction, &g_fricParm, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
        /* only stop what was actually started - and with the right signature */
        if (g_fricStarted) {
            ((Stop_t)VT(g_friction, EFF_STOP))(g_friction);
            g_fricStarted = 0;
        }
        return;
    }
    g_fricCond.lPositiveCoefficient = FRIC_COEFF;
    g_fricCond.lNegativeCoefficient = FRIC_COEFF;
    g_fricCond.dwPositiveSaturation = (DWORD)sat;
    g_fricCond.dwNegativeSaturation = (DWORD)sat;
    ((SetParams_t)VT(g_friction, EFF_SETPARAMETERS))(
        g_friction, &g_fricParm, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
    if (wasOff && !g_fricStarted) {
        ((Start_t)VT(g_friction, EFF_START))(g_friction, 1, 0);
        g_fricStarted = 1;
    }
}

static void FillConditionEffect(DIEFF *ep, DICONDITION *cond)
{
    ep->dwSize = sizeof(DIEFF);
    ep->dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    ep->dwDuration = DI_INFINITE;
    ep->dwSamplePeriod = 0;
    ep->dwGain = 10000;
    ep->dwTriggerButton = DIEB_NOTRIGGER;
    ep->dwTriggerRepeatInterval = 0;
    ep->cAxes = 1;
    ep->rgdwAxes = g_axes;
    ep->rglDirection = g_dir;
    ep->lpEnvelope = NULL;
    ep->cbTypeSpecificParams = sizeof(DICONDITION);
    ep->lpvTypeSpecificParams = cond;
    ep->dwStartDelay = 0;
    cond->lOffset = 0;
    cond->lPositiveCoefficient = 0;
    cond->lNegativeCoefficient = 0;
    cond->dwPositiveSaturation = MAX_STEER_SAT;
    cond->dwNegativeSaturation = MAX_STEER_SAT;
    cond->lDeadBand = 0;
}

/* v7.4d: watchdog wrapper for DirectInput Acquire(). The REFERENCE drive (2026-07-20)
   proved Acquire can HANG indefinitely inside the driver/OS call itself (0x53 logged,
   never followed by step 9 or a next attempt - 46+s of total silence from the FFB thread).
   A plain retry loop cannot help a true hang: the loop body never returns to retry. Run
   each attempt on its own thread and bound the wait; if it times out, abandon that attempt
   (the stuck thread is leaked - unavoidable, a kernel-level block cannot be cancelled from
   user mode) and try again fresh. Guarantees InitFFB always makes forward progress. */
typedef struct { void *dev; HRESULT hr; } AcqCtx;
static DWORD WINAPI AcquireWorker(LPVOID p)
{
    AcqCtx *c = (AcqCtx *)p;
    c->hr = ((Acquire_t)VT(c->dev, DEV_ACQUIRE))(c->dev);
    return 0;
}
#define ACQUIRE_TIMEOUT_MS 1500u
#define ACQUIRE_TRIES         3    /* v7.4j: fewer tries -> faster failure path for the retry */
#define INIT_TRIES            4    /* v7.4j: recreate the DI stack + retry whole init on failure */

static HRESULT AcquireWithTimeout(void *dev)
{
    for (int aq = 0; aq < ACQUIRE_TRIES; aq++) {
        Log(K_INIT, 0x53, (DWORD)aq, 0, 0);
        AcqCtx ctx; ctx.dev = dev; ctx.hr = (HRESULT)0x80070005; /* E_ACCESSDENIED default */
        DWORD tid;
        HANDLE th = CreateThread(NULL, 0, AcquireWorker, &ctx, 0, &tid);
        if (!th) { Log(K_INIT, 0x55, 0, 0, 0); Sleep(250); continue; }
        DWORD w = WaitForSingleObject(th, ACQUIRE_TIMEOUT_MS);
        if (w == WAIT_OBJECT_0) {
            CloseHandle(th);
            Log(K_INIT, 9, (DWORD)ctx.hr, (DWORD)aq, 0);
            if (ctx.hr >= 0) return ctx.hr;
        } else {
            CloseHandle(th);   /* worker thread leaked if truly hung - cannot force-free a kernel block */
            Log(K_INIT, 0x54, (DWORD)aq, 0, 0);   /* TIMEOUT: this attempt genuinely hung */
        }
        Sleep(250);
    }
    return (HRESULT)0x80070005;
}

/* THE POLICY, SPLIT OUT SO IT CAN BE TESTED WITHOUT A WHEEL. Everything else in the recovery path
   needs a real DirectInput device and cannot be exercised on a host; this decision is where an
   off-by-one would live, and it is pure arithmetic. */
static int RecoverDue(LONG failN, DWORD lastAt, DWORD now)
{
    if (g_recoverMs <= 0) return 0;                 /* switched off in the ini                 */
    if (failN < g_recoverAfter) return 0;           /* one refusal is not a lost device        */
    if (lastAt && (DWORD)(now - lastAt) < (DWORD)g_recoverMs) return 0;   /* not yet            */
    return 1;
}

/* WHO IS HOLDING THE FOREGROUND when the device stopped answering. It does not prove who took
   the wheel - DirectInput will not say - but it is the difference between "something took it" and
   "the game took it", and on this machine those need different fixes. Two rows: the process id
   and the first eight characters of the window title, packed four to a DWORD. */
static void LogForegroundOwner(void)
{
    HWND h = GetForegroundWindow();
    DWORD pid = 0, a = 0, b = 0;
    char t[12];
    int i;
    for (i = 0; i < 12; i++) t[i] = 0;
    if (h) {
        GetWindowThreadProcessId(h, &pid);
        GetWindowTextA(h, t, 9);
    }
    for (i = 0; i < 4; i++) a |= ((DWORD)(unsigned char)t[i]) << (i * 8);
    for (i = 0; i < 4; i++) b |= ((DWORD)(unsigned char)t[4 + i]) << (i * 8);
    Log(K_INIT, 0x24, pid, a, b);
}

/* LET GO AND TAKE IT AGAIN. Runs on the FFB thread deliberately, at the top of its loop and never
   inside SetMag: every DirectInput call in this module then happens on one thread, so there is no
   race with the writes it is trying to repair. The hang that cost v7.4j is handled by
   AcquireWithTimeout, which is the same watchdog the initial acquire uses - a bare Acquire here
   could block the FFB thread inside the driver for ever.
   An unacquired device unloads its effects, so they are started again and every cached
   "coefficient unchanged" sentinel is cleared - otherwise the first write after a successful
   recovery would be skipped as a no-op and the wheel would stay dead with the log saying it had
   been fixed. That is the quiet half of this function and the reason it is not three lines. */
static int DeviceRecover(void)
{
    HRESULT hr;
    if (!g_dev) return 0;
    g_recovN++;
    Log(K_INIT, 0x23, (DWORD)g_effFailN, (DWORD)g_effFailHr, (DWORD)g_recovN);
    LogForegroundOwner();
    ((Acquire_t)VT(g_dev, DEV_UNACQUIRE))(g_dev);
    Sleep(60);
    hr = AcquireWithTimeout(g_dev);
    if (hr < 0) {
        Log(K_INIT, 0x25, (DWORD)hr, (DWORD)g_recovOk, (DWORD)g_recovN);
        return 0;
    }
    if (g_eff)    ((Start_t)VT(g_eff, EFF_START))(g_eff, 1, 0);
    if (g_spring) ((Start_t)VT(g_spring, EFF_START))(g_spring, 1, 0);
    if (g_damper) ((Start_t)VT(g_damper, EFF_START))(g_damper, 1, 0);
    g_curmag = 0x7FFFFFFF;
    g_curSpring = -1;
    g_curDamper = -1;
    g_recovOk++;
    Log(K_INIT, 0x25, 0, (DWORD)g_recovOk, (DWORD)g_recovN);
    return 1;
}

static int InitFFB(void)
{
    char sys[MAX_PATH];
    UINT n = GetSystemDirectoryA(sys, MAX_PATH);
    if (!n) return 0;
    const char *tail = "\\dinput8.dll";
    for (int i = 0; tail[i]; i++) sys[n+i] = tail[i];
    sys[n + 12] = 0;

    HMODULE di8 = LoadLibraryA(sys);
    Log(K_INIT, 1, (DWORD)di8, 0, 0);
    if (!di8) return 0;

    DI8Create_t create = (DI8Create_t)GetProcAddress(di8, "DirectInput8Create");
    Log(K_INIT, 2, (DWORD)create, 0, 0);
    if (!create) return 0;

    HRESULT hr = create(GetModuleHandleA(NULL), DI8_VERSION, &IID_IDirectInput8A, &g_di, NULL);
    Log(K_INIT, 3, (DWORD)hr, (DWORD)g_di, 0);
    if (hr < 0 || !g_di) return 0;

    /* M3: read the preferred device BEFORE the enum. Read here and not in ReadFFBSettings on
       purpose - that function is hot-reloaded once a second, and a device swap is not a setting
       that can be applied to a live acquired device. It is an init-time decision, so it is read
       at init time and the log says what it read. */
    {
        char ini[MAX_PATH], want[64];
        FFBIniPath(ini);
        want[0] = 0;
        GetPrivateProfileStringA("ffb", "device", "", want, sizeof(want), ini);
        if (want[0]) {
            g_haveWant = ParseGuid(want, &g_wantGuid);
            /* An unparseable string is NOT treated as "no preference" quietly. It means the user
               tried to select a device and failed, which wants a different answer from having
               never tried - so it gets its own record and the first character, which is usually
               enough to see that a quote or a stray space came along with the paste. */
            Log(K_INIT, 0x14, (DWORD)g_haveWant, (DWORD)want[0], g_haveWant ? g_wantGuid.a : 0);
        }
    }

    hr = ((EnumDevices_t)VT(g_di, DI_ENUMDEVICES))(
        g_di, DI8DEVCLASS_GAMECTRL, (void *)EnumDevCb, NULL,
        DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    Log(K_INIT, 4, (DWORD)hr, (DWORD)g_found, 0);

    /* THE FALLBACK, AND IT IS LOUD. A device was asked for by GUID and the enum ended without
       finding it: the wheel is unplugged, or it enumerated under a different instance GUID after
       a driver update. Taking "the first one" silently would be a mod that works perfectly, on
       the wrong hardware, and never mentions it - the exact failure this project has paid for
       twice. So the substitution is announced with what was wanted, what is being used instead,
       and how many devices were offered. */
    if (hr >= 0 && !g_found && g_haveWant && g_haveFirst) {
        g_wheel   = g_firstGuid;
        g_found   = 1;
        for (int i = 0; i < 259 && g_firstName[i]; i++) g_wheelName[i] = g_firstName[i];
        Log(K_INIT, 0x15, g_wantGuid.a, g_firstGuid.a, (DWORD)g_devSeen);
    }
    /* ...and the case where there is nothing to fall back TO, which is a different report: the
       enum offered no force-feedback device at all. */
    if (hr >= 0 && !g_found)
        Log(K_INIT, 0x16, (DWORD)g_devSeen, (DWORD)g_haveWant, 0);

    if (hr < 0 || !g_found) return 0;

    hr = ((CreateDevice_t)VT(g_di, DI_CREATEDEVICE))(g_di, &g_wheel, &g_dev, NULL);
    Log(K_INIT, 5, (DWORD)hr, (DWORD)g_dev, 0);
    if (hr < 0 || !g_dev) return 0;

    hr = ((SetDataFormat_t)VT(g_dev, DEV_SETDATAFORMAT))(g_dev, &g_fmt);
    Log(K_INIT, 6, (DWORD)hr, 0, 0);
    if (hr < 0) return 0;

    for (int i = 0; i < 100 && !g_hwnd; i++) { EnumWindows(EnumWndCb, 0); if (!g_hwnd) Sleep(100); }
    Log(K_INIT, 7, (DWORD)g_hwnd, 0, 0);
    if (!g_hwnd) return 0;

    hr = ((SetCoop_t)VT(g_dev, DEV_SETCOOP))(g_dev, g_hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    Log(K_INIT, 8, (DWORD)hr, 0, 0);
    if (hr < 0) return 0;

    DIPROPDWORD ac;
    ac.diph.dwSize = sizeof(DIPROPDWORD);
    ac.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    ac.diph.dwObj = 0;
    ac.diph.dwHow = DIPH_DEVICE;
    ac.dwData = DIPROPAUTOCENTER_OFF;
    hr = ((SetProperty_t)VT(g_dev, DEV_SETPROPERTY))(g_dev, DIPROP_AUTOCENTER, &ac.diph);
    Log(K_INIT, 11, (DWORD)hr, 0, 0);

    hr = AcquireWithTimeout(g_dev);
    /* v7.4j: if Acquire never succeeded, a leaked worker thread is stuck INSIDE Acquire
       holding the device lock -> calling CreateEffect on this poisoned device DEADLOCKS the
       whole FFB thread (seen: log ends at 0x54x8, no step 10, no 100/99). Bail cleanly so
       FFBThread can tear down and retry with a FRESH DI stack instead of hanging. */
    if (hr < 0) { Log(K_INIT, 0x56, (DWORD)hr, 0, 0); return 0; }

    /* ---- ConstantForce effect (crash kicks) ---- */
    g_eparm.dwSize = sizeof(DIEFF);
    g_eparm.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    g_eparm.dwDuration = DI_INFINITE;
    g_eparm.dwSamplePeriod = 0;
    g_eparm.dwGain = 10000;
    g_eparm.dwTriggerButton = DIEB_NOTRIGGER;
    g_eparm.dwTriggerRepeatInterval = 0;
    g_eparm.cAxes = 1;
    g_eparm.rgdwAxes = g_axes;
    g_eparm.rglDirection = g_dir;
    g_eparm.lpEnvelope = NULL;
    g_eparm.cbTypeSpecificParams = sizeof(DICONST);
    g_eparm.lpvTypeSpecificParams = &g_cf;
    g_eparm.dwStartDelay = 0;
    g_cf.lMagnitude = 0;
    g_dir[0] = 1;

    hr = ((CreateEffect_t)VT(g_dev, DEV_CREATEEFFECT))(
        g_dev, &GUID_ConstantForce, &g_eparm, &g_eff, NULL);
    Log(K_INIT, 10, (DWORD)hr, (DWORD)g_eff, 0);
    if (hr < 0 || !g_eff) return 0;
    ((Start_t)VT(g_eff, EFF_START))(g_eff, 1, 0);

    /* ---- Spring effect (centering) ---- */
    FillConditionEffect(&g_springParm, &g_springCond);
    hr = ((CreateEffect_t)VT(g_dev, DEV_CREATEEFFECT))(
        g_dev, &GUID_Spring, &g_springParm, &g_spring, NULL);
    Log(K_INIT, 13, (DWORD)hr, (DWORD)g_spring, 0);
    if (hr >= 0 && g_spring)
        ((Start_t)VT(g_spring, EFF_START))(g_spring, 1, 0);

    /* ---- Damper effect (rotation resistance) ---- */
    FillConditionEffect(&g_damperParm, &g_damperCond);
    hr = ((CreateEffect_t)VT(g_dev, DEV_CREATEEFFECT))(
        g_dev, &GUID_Damper, &g_damperParm, &g_damper, NULL);
    Log(K_INIT, 14, (DWORD)hr, (DWORD)g_damper, 0);
    if (hr >= 0 && g_damper)
        ((Start_t)VT(g_damper, EFF_START))(g_damper, 1, 0);

    /* v7.24 Friction. FAILURE IS NOT FATAL and must not be: the driver may not implement
       friction at all, and a missing effect has to degrade to "exactly like v7.23", never to
       a dead wheel. g_friction stays NULL, SetFriction() becomes a no-op, and K_INIT 15
       records the HRESULT so the log answers whether this wheel does friction, for good. */
    if (!FRIC_ENABLE) { g_friction = NULL; goto skip_friction; }
    FillConditionEffect(&g_fricParm, &g_fricCond);
    g_fricCond.lPositiveCoefficient = FRIC_COEFF;
    g_fricCond.lNegativeCoefficient = FRIC_COEFF;
    g_fricCond.dwPositiveSaturation = 0;      /* start silent - a car is the common case */
    g_fricCond.dwNegativeSaturation = 0;
    hr = ((CreateEffect_t)VT(g_dev, DEV_CREATEEFFECT))(
        g_dev, &GUID_Friction, &g_fricParm, &g_friction, NULL);
    Log(K_INIT, 15, (DWORD)hr, (DWORD)g_friction, 0);
    /* v7.24b: created but deliberately NOT started - it is started by SetFriction the first
       time a profile actually asks for friction. A stopped effect cannot take a share of the
       device's force budget, and cannot be misread as having unlimited saturation. */
    if (hr < 0) g_friction = NULL;
skip_friction:

    Log(K_INIT, 12, 0, 0, 0); /* mark init complete */
    return 1;
}

void __cdecl OnCarSeen(DWORD car)
{
    g_car = car;
    g_car_tick = GetTickCount();
    g_hookHits++;   /* DIAG */
}

asm(
    ".globl _HookCar                 \n"
    "_HookCar:                       \n"
    "  pushal                        \n"
    "  pushl %ebp                    \n"
    "  call  _OnCarSeen              \n"
    "  addl  $4, %esp                \n"
    "  popal                         \n"
    "  movl  (%eax), %eax            \n"
    "  pushl %eax                    \n"
    "  movl  (%eax), %edx            \n"
    "  call  *0x2C(%edx)             \n"
    "  ret                           \n"
);
void HookCar(void);

static void MakeRWX(DWORD va, SIZE_T size)
{ DWORD old; VirtualProtect((void *)va, size, PAGE_EXECUTE_READWRITE, &old); }

static void PatchWithCall(DWORD site, DWORD hook, int count)
{
    BYTE *p = (BYTE *)site;
    MakeRWX(site, (SIZE_T)count);
    p[0] = 0xE8;
    *(DWORD *)(p + 1) = hook - site - 5;
    for (int i = 5; i < count; i++) p[i] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), p, (SIZE_T)count);
}

static void SnapOne(DWORD root, DWORD base, DWORD lo, DWORD hi)
{
    if (g_snap == INVALID_HANDLE_VALUE) return;
    static BYTE buf[SNAP_BYTES];
    for (DWORD i = 0; i < SNAP_BYTES; i++) buf[i] = 0;

    DWORD start = base + lo, valid = 0;
    if (Sane(base)) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((const void *)start, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            int readable = (mbi.State == MEM_COMMIT) &&
                           !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
            if (readable) {
                DWORD regionEnd = (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize;
                DWORD want = base + hi;
                DWORD end = want < regionEnd ? want : regionEnd;
                if (end > start) {
                    DWORD n = end - start;
                    if (n > SNAP_BYTES) n = SNAP_BYTES;
                    memcpy(buf, (const void *)start, n);
                    valid = n;
                }
            }
        }
    }
    DWORD hdr[4] = { GetTickCount(), root, start, valid };
    DWORD w;
    WriteFile(g_snap, hdr, sizeof(hdr), &w, NULL);
    WriteFile(g_snap, buf, SNAP_BYTES, &w, NULL);
    if ((++g_snapcount & 31) == 0) FlushFileBuffers(g_snap);
}

/* ---- MatrixHunt helpers ---- */

static double Absd(double x) { return x < 0.0 ? -x : x; }

/* finite = exponent not all-ones (rejects NaN/Inf without libm) */
static int Finf(float f) { DWORD u = *(DWORD *)&f; return ((u >> 23) & 0xFFu) != 0xFFu; }

/* 9 floats row-major: 3 unit rows, mutually orthogonal = rotation matrix fingerprint */
static int IsOrthonormal(const float *m)
{
    for (int r = 0; r < 3; r++) {
        if (!Finf(m[r*3]) || !Finf(m[r*3+1]) || !Finf(m[r*3+2])) return 0;
        double nn = (double)m[r*3]*m[r*3] + (double)m[r*3+1]*m[r*3+1] + (double)m[r*3+2]*m[r*3+2];
        if (nn < 0.94 || nn > 1.06) return 0;
    }
    double d01 = (double)m[0]*m[3] + (double)m[1]*m[4] + (double)m[2]*m[5];
    double d02 = (double)m[0]*m[6] + (double)m[1]*m[7] + (double)m[2]*m[8];
    double d12 = (double)m[3]*m[6] + (double)m[4]*m[7] + (double)m[5]*m[8];
    if (Absd(d01) > 0.06 || Absd(d02) > 0.06 || Absd(d12) > 0.06) return 0;
    return 1;
}

/* atan on [0,1] (Rajan), then full atan2 - no libm (-nostdlib). Error < 0.004 rad. */
static float Atan01(float a) { return 0.7853981634f*a - a*(a-1.0f)*(0.2447f + 0.0663f*a); }
static float Atan2f(float y, float x)
{
    if (x == 0.0f && y == 0.0f) return 0.0f;
    float ax = Absf(x), ay = Absf(y);
    float a = ax > ay ? ay/ax : ax/ay;
    float r = Atan01(a);
    if (ay > ax) r = 1.5707963268f - r;
    if (x  < 0.0f) r = 3.1415926536f - r;
    if (y  < 0.0f) r = -r;
    return r;
}

/* Car world heading from [car+0x58]+0x164 matrix, row2. 0 if pointer/matrix invalid. */
/* Safely read the 9-float matrix at [car+FRAME_PTR_OFF]+MATRIX_OFF. Returns 1 and fills
   mm[] only if the pointer is mapped AND the matrix is orthonormal. VirtualQuery-guarded:
   the heading offset is NOT portable across sessions/cars (v7 regression - the v6.19
   MatrixHunt find was session-specific), so a wrong pointer here would otherwise deref
   unmapped memory and KILL the FFB thread (centering lost). This degrades slip gracefully
   instead. */
static int ReadHeadingMatrix(DWORD car, float *mm)
{
    DWORD frame = *(volatile DWORD *)(car + FRAME_PTR_OFF);
    if (!Sane(frame)) return 0;
    DWORD addr = frame + MATRIX_OFF;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if (addr + 36u > (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize) return 0;
    const volatile float *m = (const volatile float *)addr;
    for (int j = 0; j < 9; j++) mm[j] = m[j];
    return IsOrthonormal(mm);
}

static int GetCarHeading(DWORD car, float *outHead)
{
    float mm[9];
    if (!ReadHeadingMatrix(car, mm)) return 0;
    *outHead = Atan2f(mm[6], mm[8]);
    return 1;
}

/* Log the known orientation matrix each snap cycle (F1 cross-check + F2 roll/pitch). */
static void MatrixLog(DWORD car)
{
    if (g_mtx == INVALID_HANDLE_VALUE || g_mtxcount >= MTX_MAX_REC) return;
    float mm[9];
    if (!ReadHeadingMatrix(car, mm)) return;   /* guarded read; 0 records = offset wrong */
    DWORD frame = *(volatile DWORD *)(car + FRAME_PTR_OFF);
    MtxRec r;
    r.tick = GetTickCount(); r.slot = FRAME_PTR_OFF; r.matoff = MATRIX_OFF; r.ptr = frame;
    for (int j = 0; j < 9; j++) r.m[j] = mm[j];
    r.carX = *(volatile float *)(car + CARPOS_X_OFF);
    r.carY = *(volatile float *)(car + CARPOS_Y_OFF);
    DWORD w;
    WriteFile(g_mtx, &r, sizeof(r), &w, NULL);
    g_mtxcount++;
    if ((g_mtxcount & 63) == 0) FlushFileBuffers(g_mtx);
}

/* Guarded single-float read: 1 + fills *out only if addr is mapped. */
/* ---- gog-patcher: rotation-matrix shape tests, used by the frame-layout hunt ---- */
static volatile int g_mtxScanDone = 0;

/* The hunt found THREE matrix-shaped blocks in the frame, none of them at the custom
   build's +0x164. Shape alone cannot say which one is the world rotation - a bounding
   volume or a parent-relative transform is just as orthonormal. The one we want is the one
   that TURNS WITH THE CAR, so keep the candidates and log their heading elements over a
   drive; heading is atan2(m[6], m[8]).
   Capacity 4: the scan stops at 6 finds, and more than a handful would mean the shape test
   is too loose to be worth following anyway. */
#define MTX_CAND_MAX 4
static volatile DWORD g_mtxOff[MTX_CAND_MAX];
static volatile int   g_mtxStride[MTX_CAND_MAX];
static volatile int   g_mtxNCand = 0;

static int MtxOrtho3(const float *m)
{
    for (int r = 0; r < 3; r++) {
        float l = m[r*3+0]*m[r*3+0] + m[r*3+1]*m[r*3+1] + m[r*3+2]*m[r*3+2];
        if (l < 0.98f || l > 1.02f) return 0;
    }
    if (Absf(m[0]*m[3] + m[1]*m[4] + m[2]*m[5]) > 0.02f) return 0;
    if (Absf(m[0]*m[6] + m[1]*m[7] + m[2]*m[8]) > 0.02f) return 0;
    if (Absf(m[3]*m[6] + m[4]*m[7] + m[5]*m[8]) > 0.02f) return 0;
    return 1;
}

static int MtxIdentity3(const float *m)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float want = (i == j) ? 1.0f : 0.0f;
            if (Absf(m[i*3+j] - want) > 0.001f) return 0;
        }
    return 1;
}

static int SafeReadF(DWORD addr, float *out)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if (addr + 4u > (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize) return 0;
    *out = *(volatile float *)addr;
    return 1;
}

/* Guarded single-DWORD read. */
static int SafeReadD(DWORD addr, DWORD *out)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if (addr + 4u > (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize) return 0;
    *out = *(volatile DWORD *)addr;
    return 1;
}

/* PRIMARY car source (v7.1b): car = *(*(0x65115C) + 0x24). Independent of the
   collision-loop hook (which does not fire under the VR/d3d8to9 stack). The player
   vehicle pointer sits at glob+0x24 - CONFIRMED across two separate sessions (both had
   the same car ptr 0x0E599350 there, speed tracking the pedal). glob+0x24 is a struct
   FIELD offset (compile-time stable), unlike the heap-layout-dependent heading offset,
   so it is safe to hardcode. NO blind hunt (that latched onto false positives with a
   static 0 speed) and NO world-position gate (that transiently rejected the real car).
   Light guard only: the pointer must be Sane and its speed field a finite sane float. */
#define CAR_PTR_OFF 0x24u
#define CAR_GRACE_MS 250u   /* hold last car across brief GetCarPtr dropouts (anti-judder) */

static DWORD GetCarPtr(void)
{
    DWORD glob = *(volatile DWORD *)VA_GAMEPTR;
    if (!Sane(glob)) return 0;
    DWORD car;
    if (!SafeReadD(glob + CAR_PTR_OFF, &car) || !Sane(car)) return 0;
    float spd;
    if (!SafeReadF(car + SPEED_OFF, &spd)) return 0;   /* must be readable */
    if (spd < -5.0f || spd > 150.0f)       return 0;   /* finite + sane -> a real car */
    return car;
}

/* ================= THE GROUND CHANNEL - ported from the developers' own arithmetic ============
 *
 * Two things the stock game does that this module never had, both settled 2026-09-13 in the raw
 * bytes of `FUN_0041fac0` after Ghidra mis-assigned the stack slots:
 *
 *     L = clamp( SUM over live wheels of (wheel+0xDC * wheel+0x90) / (mass * g * 1.1), 0, 1 )
 *     friction coefficient = gain * master * L * max(1 - v/6, 0.034)
 *     spring   coefficient = gain * master * L * min(v * 0.5, 1)
 *
 * `0x420687` and `0x4207e5` are the same instruction, `d8 4c 24 30`, reading the same slot: BOTH
 * channels are multiplied by the tyre load. When the wheels unload, the game's whole feel dies -
 * which is what Alex reported as the best thing their feedback does and the one thing ours lacks:
 * when the wheels leave the ground... the weight on the wheel disappears. The wheel becomes empty.
 *
 * WHAT IS PORTED AND WHAT IS DELIBERATELY NOT.
 *
 *  - `L` is ported exactly. It is not a flag and not a wheel count: one planted wheel keeps it
 *    non-zero and partial unloading gives a partial fade, which is his own question answered.
 *  - The centre offset is ported in SPIRIT ONLY, and that is the instruction: natural self-steering
 *    is wanted, but not the way it currently feels on the factory
 *    settings, because they are somewhat clumsy. Theirs is a slip angle clamped to full lock
 *    and rewritten 12.5 times a second, so it arrives as a series of steps - the sawing he
 *    disliked in run 011. Ours is the same physical quantity, computed every tick of this thread,
 *    smoothed with a time constant, slew-limited, and clamped to a fraction of lock. Same idea,
 *    none of the staircase.
 *
 * I do not have the GTA V direct-drive mod's source in front of me and will not invent its
 * internals. What is implemented here is the model such mods use and that the physics supports:
 * self-aligning torque grows with slip angle, dies with tyre load, and fades in with speed.
 * Point me at the repository and the details can be matched to it.
 *
 * Everything is live-tunable from mafia_ffb.ini and ON by default, because this is a test build:
 * Request, 2026-09-13: enable it by default, because testing is under way anyway.
 * No release is planned. THE RELEASE DEFAULT MUST GO BACK TO 0 BEFORE ANY ARCHIVE IS CUT - the
 * approved reference build's forces are not to change unasked ([[ffb-reference-v767]]).
 */
#define VA_TICK_CALL   0x0042F60Fu   /* the E8 of `call 0041fac0` inside the vehicle tick       */
#define VA_TICK_FUNC   0x0041FAC0u   /* its target; ECX is the VEHICLE, which is NOT our g_car   */

volatile DWORD g_vehTick  = 0;               /* ECX as the game passes it, every physics tick   */
volatile DWORD g_vehOrig  = VA_TICK_FUNC;

/* Naked: nothing touched but a copy of ECX, then a tail jump, so the original function returns
   straight to the game and nothing on its stack moves. */
__attribute__((naked, used)) static void VehTickStub(void)
{
    __asm__ __volatile__("movl %ecx, _g_vehTick\n\t"
                         "jmp  *_g_vehOrig\n\t");
}

/* WHY A DETOUR AND NOT A POINTER CHAIN. `[[0x0065115C]+0x24]`, which this module has used since
   v7.1b, reaches the PLAYER ACTOR - the object whose speed is at +0x2A0C. The vehicle class that
   owns +0x534 / +0xCA8 / wheel+0x90 is a different object and has no field at 0x2A0C at all. Run
   014 proved it the expensive way: a sampler built on the old chain wrote not one row all drive.
   The game itself hands us the right pointer four times a second, so we take it from there. */
static int PatchVehicleTick(void)
{
    unsigned char now[5];
    DWORD old = 0, rel;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)VA_TICK_CALL, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    { int i; for (i = 0; i < 5; i++) now[i] = ((const unsigned char *)VA_TICK_CALL)[i]; }
    /* refuse on anything but this build's exact bytes - a different Game.exe is not this one */
    if (now[0] != 0xE8 || now[1] != 0xAC || now[2] != 0x04 || now[3] != 0xFF || now[4] != 0xFF)
        return 0;
    rel = (DWORD)(DWORD_PTR)&VehTickStub - (VA_TICK_CALL + 5);
    if (!VirtualProtect((void *)(VA_TICK_CALL + 1), 4, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(DWORD *)(VA_TICK_CALL + 1) = rel;
    VirtualProtect((void *)(VA_TICK_CALL + 1), 4, old, &old);
    return 1;
}

/* ---- the model, and it is GTA V Manual Transmission's, not a spring --------------------------
 *
 * His instruction, 2026-09-13: take the zero-resistance-when-airborne from that mod, and make the
 * steering weight natural - not just a dumb damper, but perhaps an offset of the return centre
 * of the spring will give exactly this weight at speeds when steering, in sustained corners - and
 * check it against GTAVManualTransmission and Assetto Corsa rather than inventing one.
 *
 * We analysed that mod already; the notes are `..\force-feedback\memory\gta-mt-analysis.md`.
 * Its architecture answers both of his tasks at once, and it is better than the moving spring
 * centre this block first carried, so that is dropped:
 *
 *     ConstantForce = SAT + Detail          (one effect, both components)
 *     Damper        = speed-dependent friction        <- we already have this
 *     NO spring effect at all
 *
 *     satForce = SATAmp * SATMult * 10000 * slipRatio * velFac * weightFactor * longSlipMult
 *     weightFactor = frontWheelLoad / designFrontWheelLoad, raised to 2.5 when below 1
 *     detail   = 1000 * DetailMult * (suspensionSpeed[FR] - suspensionSpeed[FL])
 *
 * WHY SAT IS THE ANSWER TO "WHERE THE WHEEL RETURNS TO". A self-aligning torque points where the
 * tyres are actually rolling, so the wheel settles at the slip-free angle by itself - in a long
 * corner that is a steady weight against your hands, in a slide it is the counter-steer pulling
 * the wheel into the slide, and past the tyre's peak slip it FADES, which is how a real wheel
 * tells you the front has let go. A spring pulled toward a computed centre cannot do that last
 * part, and the stock game's version - centre clamped to full lock, rewritten 12.5 times a second
 * - is the staircase he disliked in run 011.
 *
 * WEIGHT FACTOR IS HIS TASK 1, and it is per axle on purpose: the mod divides by the FRONT
 * wheels' design load, so front wheels in the air take the steering force to zero even while the
 * rears are still planted - most likely it is 1-2 front wheels in the air. The 2.5 exponent
 * below design load is theirs: it makes the fade fast rather than linear, which is why it reads
 * as the wheel going hollow rather than merely lighter. The wheel array is ordered L,R per axle,
 * so indices 0 and 1 are the front pair.
 *
 * Kept from the stock game, because it is the same physics from the other end: the load comes
 * from `wheel+0x90`, the suspension normal force, which is what `FUN_0041fac0` multiplies both of
 * its channels by ([[empty-wheel-when-airborne]]).
 */
static volatile LONG g_gndOn      = 1;     /* master switch for everything in this block        */
/* WHICH FEEDBACK THE PLAYER CHOSE in the utility: 0 this road model, 1 legacy (v7.67), 2 the
   game developers' own. -1 = the key is absent, which is every file written before 2026-09-14 -
   and at -1 nothing here changes behaviour at all, `ground` decides on its own exactly as it
   did. See GroundReadIni. */
static volatile LONG g_ffbMode    = -1;
/* v815. The escape hatch for the vehicle re-validation in VehHunt: 1 = check every second that
   the game still hands the actor the same vehicle object and re-resolve when it does not;
   0 = the behaviour of every build up to v814, which keeps the old object for as long as it
   still looks like a car - and a freed one does, for ever. */
static volatile LONG g_gndRevalidate = 1;
static volatile LONG g_gndGate    = 1;     /* our OWN continuous force also fades with the load */
static volatile LONG g_gndSat     = 1;     /* the self-aligning torque - the steering weight    */
static volatile LONG g_gndDetail  = 1;     /* the front suspension left-right differential      */
static volatile LONG g_gndSatK    = 5000;  /* SAT force at the tyre's peak, full load and speed.
                                              It must AGREE with the ini default below: the ini is
                                              read at startup, so a disagreement only shows itself
                                              on the one path where the read fails - which is the
                                              path nobody tests.
                                              6000 -> 5000 after drive ours-018b: *"if I turn it
                                              like this and let go, it straightens it out far too
                                              hard"* (Alex, 2026-09-13).
                                              For scale, GTA MT's own peak is the
                                              full 10000 of the DI range (`satForce = SATAmpMult *
                                              10000 * slipRatio * velFac * weight * longSlipMult`,
                                              WheelInput.cpp:962), so we sit at half of theirs.  */
static volatile LONG g_gndOptDeg  = 9;     /* the tyre's optimal slip angle, degrees            */
/* Percent of the peak left past the fade - GTA MT's `postOptSlipMin`, which THEY hardcode to 0.
   15, not their 0, and the reason is his: *"it was as if there was no self-steer during the
   U-turn"* (Alex, 2026-09-13, drive ours-018b).
   Measured in his own log at 131.0-131.8 s - the steering angle sat at 0.55-0.68 rad with an
   effective slip of 25-30 degrees, well past 2.5x the optimal 9, so SAT was EXACTLY 0 for the
   whole manoeuvre and the only thing left holding the wheel was the caster's 1800. The floor is
   their own parameter at a different value, not a mechanism of ours, and the dropout he likes
   survives it: 15% of the peak is still an 85% collapse, and a locked wheel multiplies it by
   `ground_long_floor` 20% on top. */
static volatile LONG g_gndFalloff = 15;
static volatile LONG g_gndPostRatio = 250; /* the fade ends at this x the optimal angle, x100   */
static volatile LONG g_gndVmin    = 0;     /* cm/s where SAT starts - from the first motion     */
static volatile LONG g_gndVfull   = 100;   /* cm/s where it is fully in - 3.6 km/h              */
static volatile LONG g_gndDetailK = 900;   /* the road-texture differential                     */
/* ---- THE DETAIL CHANNEL'S TWO MISSING HALVES, both taken from GTA MT rather than invented -----
   Theirs (`VehicleData.cpp:132-153` and `WheelInput.cpp:635-647,1064`) differences the suspension
   travel exactly as ours does, and then does two things ours did not:
     - a trailing MOVING AVERAGE over `DetailMAW` frames, default 3, on the per-wheel compression
       speed. A first difference of a position sampled every ~15 ms is a noise amplifier; theirs
       is smoothed before it ever becomes a force and ours was not.
     - an explicit clamp to +-`DetailLim`, default 5000 on the same +-10000 scale we use. Ours
       borrowed `ground_sat_k` for the clamp, so the texture channel was allowed to be as large as
       the primary one - and on his drive the residual after SAT and the caster reached 10188.
   The load gate stays OURS and is a deliberate difference from them: no texture through wheels
   that are in the air, which is the empty wheel he asked for. */
static volatile LONG g_gndDetailLim = 5000; /* theirs: DetailLim                                */
static volatile LONG g_gndDetailMaw = 3;    /* theirs: DetailMAW, samples in the moving average */
/* ---- WHERE THE SLIP ANGLE'S SIGN COMES FROM, and it is the biggest single fix of this pass ----
   THE MAGNITUDE and THE SIGN of the slip angle came from two different places, and only the
   magnitude was any good. `wheel+0x108` is stored unsigned, so GroundRead signed it with the
   lateral component of `wheel+0x4C` in the body frame - a quantity that is NEAR ZERO exactly when
   the car is going roughly straight, which is when a large steering-angle slip is most common.
   The result, measured on his own drive ours-018b:

     - 169 reversals of the engine field between adjacent logged rows with BOTH ends over 0.05,
       against 15 for the kinematic estimate over the same rows - the sign chattered while the
       magnitude ran smoothly through its own bump;
     - 211 reversals of SAT itself between adjacent rows with both ends over 1000 units;
     - the two largest force steps of the whole drive, -5043 -> +6226 and -2370 -> +5162, are both
       a sign flip at constant magnitude with the steering angle NOT moving (0.0839 -> 0.0818 rad),
       and both land within four seconds of *"too much on flat ground, it hits far too hard"*
       (Alex, 2026-09-13).

   So the bangs on flat ground were not the road and not the detail channel: they were our own sign
   source flipping under a force of five thousand units. His own hypothesis - the suspension - is
   answered by the same log: the residual after SAT and the caster has a median of 109 units.

   The fix is GTA MT's construction rather than a filter on ours. Theirs keeps one signed slip
   angle throughout and returns `slipRatio * sgn(slip)` (WheelInput.cpp:810), so magnitude and sign
   cannot disagree. Ours cannot measure their angle, but the kinematic front slip angle
   `beta + a*yaw/v - delta` is the same quantity in the textbook form, it is already computed here,
   and on his drive it carries the centring sign in 99.6% of loaded rows against 87.8% for the
   device-frame sign. So: MAGNITUDE from the engine, which is the accurate half, SIGN from the
   kinematic angle, which is the smooth half.

   THE BAND makes a zero crossing a crossing rather than a flip: within `ground_sign_band`
   milliradians of straight the force is scaled down linearly instead of jumping sides, which is
   also the physically right answer - with no slip angle there is no aligning torque. Replayed
   over his drive it takes the SAT reversals from 211 to 3 and the largest step from 11497 to 5917.
   0 restores the old device-frame sign exactly, as an escape hatch. */
static volatile LONG g_gndSignBand = 30;   /* milliradians; 0 = the old sign from wheel+0x4C     */
/* Calls with bit-identical inputs before the car is called frozen. 20 is about 300 ms of an FFB
   tick - long enough that no run of real physics repeats itself that exactly, short enough that a
   pause is quiet almost at once. 0 = off, and then the wheel holds its last force in a menu the
   way it did on drive ours-019. */
/* v7.96: the speed curve's anchors. Declared HERE, with the rest of the ground channel's state,
   because GroundReadIni runs far above the curve's own function and a definition that sits next
   to its user compiled to fourteen "undeclared identifier" errors. */
#define GND_SPD_PTS 8
/* v817: THE BASE CURVE IS "THE HALF" AGAIN, his decision of 2026-09-30 after ours-035:
   "make the heavier curve ... at high speed it needs more effort" (Alex, translated).
   From v810 to v816 the base was the curve on I (22 34 39 49 65 84 105 173), on a reading of his
   ours-033 verdict that the log does not support: the last curve he drove before it was THIS one,
   on O, for 290 s, and I was never pressed. See memory\the-approved-driving-feel.md. The lighter
   curve stays on I, so the two can be driven against each other without a build. */
static volatile LONG g_gndSpdV[GND_SPD_PTS] = { 10, 20, 30, 50, 60, 70, 80, 110 };
static volatile LONG g_gndSpdM[GND_SPD_PTS] = { 22, 34, 42, 60, 80, 100, 122, 190 };
static volatile LONG g_gndSpdOn = 1;
/* v7.97 - FOUR CURVES ON THE LETTERS, requested after the first drive on the curve: several of
   these curves also on UIOP with an offset, so that several curves can be tested
   while driving.

   All four share the SAME speed anchors and all four pass through 100% at 40 km/h, which is the
   property that makes them comparable: switching curves does not change how the city feels, only
   how the weight GROWS away from it. Row 0 is flat-ish (nearly the pre-v796 wheel), row 1 is the
   curve he chose, row 2 is what flattening the measured force-per-g exactly would need, row 3 is
   past it - a ladder with a wrong end at each side, so the middle means something.

   This also retires the letters' old job. Until v796 they set the base return, so pressing I in a
   drive changed the weight and he could not tell that from the curve being cancelled - he said so
   in as many words. One key, one meaning. */
/* v7.98 - ONE CURVE, SCALED AS A WHOLE. v7.97 put four differently SHAPED curves on the
   letters and it was refused in one sentence: a linear shift was expected, but the curves change.
   It is unclear which button to press for the base, approved variant. The objection is
   right twice - the shape he approved should not be a variable while something else is being
   judged, and a bank whose baseline is not obvious cannot be used at speed.

   So the curve is the one he drove and the letters multiply it. He expected exactly this
   (movement by a multiplier) and that expectation beat the first answer:
   shifting along the SPEED axis made the four keys read 1.19 / 1.00 / 0.66 / 0.40 at 40 km/h, so
   switching a curve changed the city feel - the one thing that had to stay fixed while the growth
   is being judged. A multiplier keeps the SHAPE identical and moves only the level.
   I is 100 - the approved curve, bit for bit. */
static volatile LONG g_gndSpdScale = 100;   /* percent, live; 100 = the curve as its key defines it */
static volatile LONG g_gndCurveKey = 6;     /* which preset's curve is live; 6 = U, his scheme   */
/* What the curve last read and last produced, for the log row 0x33. */
static volatile float g_gndLastKmh = 0.0f;

/* ---- v820: THE TRUE SPEED THE CAR HIT WITH, for the crash trim ----------------------------
   His verdict on ours-037 made the crash trim depend on SPEED: 70% is right up to about 50 mph,
   too weak at 60-70 mph, where the original is better. The crash path's own speed is the old
   car+0x2A0C field, which is not a speed ([[speed-field-double-store]]) - so the trim reads the
   physics velocity the ground channel already computes, and its MAXIMUM over the last 1.5 s,
   because by the time a crash is detected the car has already slowed: the entry speed is what
   he feels the hit against. Sixteen slots, one every 100 ms. Written and read on the FFB thread
   only, so it needs no lock. */
#define TSR_SLOTS    16
#define TSR_STEP_MS  100u
#define TSR_LOOK_MS  1500u
static float g_tsrKmh[TSR_SLOTS];
static DWORD g_tsrT[TSR_SLOTS];
static int   g_tsrHead = 0;
static DWORD g_tsrLast = 0;

static void TrueSpeedRingPush(float kmh, DWORD now)
{
    if (g_tsrLast != 0 && (DWORD)(now - g_tsrLast) < TSR_STEP_MS) {
        /* inside the same 100 ms: keep the higher of the two, so a short peak is not lost */
        int cur = (g_tsrHead + TSR_SLOTS - 1) % TSR_SLOTS;
        if (kmh > g_tsrKmh[cur]) g_tsrKmh[cur] = kmh;
        return;
    }
    g_tsrKmh[g_tsrHead] = kmh;
    g_tsrT[g_tsrHead] = now;
    g_tsrHead = (g_tsrHead + 1) % TSR_SLOTS;
    g_tsrLast = now;
}

/* the highest true speed in the last 1.5 s, km/h; 0 when the ring has nothing that recent */
static float TrueEntryKmh(DWORD now)
{
    float best = 0.0f;
    int i;
    for (i = 0; i < TSR_SLOTS; i++) {
        if (g_tsrT[i] == 0) continue;
        if ((DWORD)(now - g_tsrT[i]) > TSR_LOOK_MS) continue;
        if (g_tsrKmh[i] > best) best = g_tsrKmh[i];
    }
    return best;
}

/* THE CRASH TRIM FOR AN IMPACT AT `kmh`: `lowPct` up to rampLo, 100 from rampHi, a straight line
   between. rampHi <= rampLo means no ramp - the flat trim every build before v820 applied, which
   is what F4 (the original, 100) and an ini with no ramp keys still get. Pure, so a host test can
   walk it across every speed. */
static LONG CrashTrimFor(float kmh, LONG lowPct, LONG rampLo, LONG rampHi)
{
    float f;
    if (lowPct >= 100 || rampHi <= rampLo) return lowPct;
    if (kmh <= (float)rampLo) return lowPct;
    if (kmh >= (float)rampHi) return 100;
    f = (kmh - (float)rampLo) / (float)(rampHi - rampLo);
    return (LONG)((float)lowPct + (100.0f - (float)lowPct) * f + 0.5f);
}
static volatile float g_gndLastSatMult = 1.0f;
static volatile LONG g_gndStaleTicks = 20;
static volatile LONG g_gndStale = 0;       /* 1 while the inputs have stopped moving             */
static volatile LONG g_gndTauMs   = 40;    /* smoothing on SAT - theirs is per frame, we filter */
static volatile LONG g_gndSlew    = 40000; /* ceiling on force change, units per second         */
static volatile LONG g_gndInvert  = 0;     /* sign of SAT against this wheel's axis             */
static volatile LONG g_gndLog     = 1;
/* v7.87 - THE SPIKE ROW. Every other ground row is written one tick in four, which is one row
   per ~64 ms: a bang that lasts two ticks can fall between them entirely, and what reaches the
   report is then the quiet ticks either side of the thing he felt. That is tolerable for a
   channel judged on its average and useless for one judged on its worst moment, which is what
   his question after ours-020 is - the wheel is hit hard in a fast car and the old impact
   family and the new ground model feel identical through the rim.
   So: whenever the SENT force steps by this much between two ticks, write one row naming the
   three terms it is made of. Unconditional, outside the 1-in-4 gate. 0 = off. */
static volatile LONG g_gndSpike   = 1500;
/* v7.8 - the front axle's slip angle and the damper trim. Declared here with the rest so
   GroundReadIni, which sits above their users, can see them. */
static volatile LONG g_gndAlphaMode   = 0;   /* 0 auto, 1 lateral-acceleration, 2 kinematic     */
static volatile LONG g_gndAyGain      = 100; /* percent, the fallback term's weight             */
static volatile LONG g_gndFrontAxleCm = 130; /* centre of mass to the front axle, cm            */
static volatile LONG g_gndSteerLive   = 0;   /* set once the steering field is seen to MOVE     */
/* The three inputs, kept for the log. Without them a row saying "the force was 3000" cannot be
   told from a row saying "the force was 3000 for the wrong reason". */
static volatile float g_gndLastBodySlip = 0.0f;
static volatile float g_gndLastYaw      = 0.0f;
static volatile float g_gndLastDelta    = 0.0f;
static float GroundGate(void);   /* defined below, used by the logging block in GroundForce */
/* The longest tick the assembly will believe. Past this the machine hitched; the force still moves
   at its own slew ceiling and no faster. */
#define GND_DT_MAX_MS   100
/* THE CHANNEL'S CLOCK, and it has a seam for the tests on purpose. GetTickCount's granularity is
   ~15.6 ms - the size of the FFB tick itself - so its delta can only ever be 0, 15 or 31, and an
   offline test calling this function in a loop sees 0 every time. Everything time-dependent here
   (the filter, the slew ceiling, the suspension derivative, the caster's rate term) would then be
   untestable, which is how the hitch defect survived: no check could reach the branch.
   QueryPerformanceCounter is not used here deliberately - the 64-bit division it needs pulls in a
   compiler helper that does not exist under -nostdlib, which is why the tick log ships RAW counter
   deltas and divides offline. */
#ifdef MAFIA_FFB_TEST
DWORD g_ffbTestNowMs = 100000;
#define GND_NOW()   (g_ffbTestNowMs)
#else
#define GND_NOW()   GetTickCount()
#endif
/* The detail channel's moving-average window, bounded because it is a fixed array in a thread
   frame that has already had to be defended once (see the NOINLINE note on GroundForce). */
#define GND_MAW_MAX     8
/* Samples of the sent force kept for the spike row's 60 ms window. Eight covers it at the FFB
   thread's 8 ms tick and has slack at the 16 ms the game actually delivers. */
#define GND_SPIKE_HIST  8

/* ---- THE "0 HITS" INSTRUMENT, and it is not optional ----------------------------------------
   Every refusal in GroundRead returns 0.0f and changes nothing, which on the wheel is
   indistinguishable from a channel that is working and has nothing to say. Four of his drives
   have already been spent on exactly that shape ([[the-drives-we-wasted]]), so these counters are
   logged once a second whether or not anything happened - a run that measures nothing has to SAY
   that it measured nothing. */
static volatile LONG  g_gndCalls    = 0;   /* GroundForce entered                               */
static volatile LONG  g_gndReads    = 0;   /* ... and a vehicle was actually read off the detour */
static volatile LONG  g_gndAirTicks = 0;   /* ... with the front axle under 10% of its load      */
/* -1 = the ini decides; 0 or 1 = SCROLL LOCK decided, mid-drive, and the ini stops overruling it.
   The A/B has to happen inside ONE drive: his verdict on a feel is a hypothesis until the same
   road is driven both ways within a minute of itself, and he cannot alt-tab to edit a file
   because Mafia pauses the moment it loses focus ([[he-does-not-edit-files]]). */
static volatile LONG  g_gndOverride = -1;
/* Percent of the profile's moving damper kept while the ground model runs. It scales the MOVING
   term only - the standstill damper he approved is untouched at any speed below the fade
   threshold. It was 50, on his call of 2026-09-13: *"the damper can be turned down a bit, because
   if we have an honest aligning torque... too much damper while moving may not be needed"*.

   IT WAS 150 FOR ONE DRIVE AND HE THREW IT OUT, ours-019: *"the wheel was too stiff when the
   feedback worked. Really too stiff, thick, viscous. I did not like it. In ordinary driving it
   has to be much lighter than it was just now"* (Alex, 2026-09-13). Back to 50.

   THE REASONING THAT PUT IT AT 150 WAS WRONG, AND THE WAY IT WAS WRONG IS WORTH KEEPING. GTA MT
   ships DamperMax 100 and DamperMin 40, so their damper never falls below 40% of its standstill
   value, and ours was at 13% - that part is true. But a DI condition coefficient is a SLOPE, and
   theirs and ours are on the same +-10000 scale: with their gain of 50 they send 5000 standing
   and 2000 moving, while we send 20000 standing - four times their slope, deliberately, because
   20000 saturates at half of travel and that is the standstill weight he approved. Taking a RATIO
   from a base four times larger delivered 7875 where they deliver 2000. The ratio matched and the
   force did not, and he felt it in one drive. 2625 is 31% above their absolute, which is the
   comparison that means something. Measured in his log: the damper sent while moving was a
   constant 7875 and its p90 over the whole drive was 16232.

   A number borrowed from another project has to be borrowed in the units the DEVICE sees, not in
   the proportions the other project's own constants happen to have. */
static volatile LONG  g_gndDampMovePct = 50;
/* THE AIR IS NOT FREE-SPINNING. His correction, 2026-09-13: *"I do not care what happens in the
   air, but the force on the wheel has to be zero. Some base damper should stay, but a really thin
   one, just so the wheel does not spin freely - it should turn with a little finger. Say a damper
   of 1000 or 2000 stays and everything else disappears completely."*
   So the load gate takes SAT, the detail channel and the damper's whole moving value to nothing,
   and this floor is what is left: enough to stop a direct drive coasting, not enough to feel like
   weight. It can never exceed the damper the same moment would have had on the ground, so a user
   who sets the damper sliders to zero still gets zero.

   HIS NUMBER SINCE ours-018b, 1500 -> 500: *"but I would take the damper down further... even the
   damper of that rest state should come off. Right now it is 2000 there or whatever. You can set
   it to, say, 500"* (Alex, 2026-09-13). It goes in as asked. For the record it moves us TOWARDS
   GTA MT, whose damper is multiplied by `(1 - wheelsOffGroundRatio)` and therefore reaches zero
   outright with the steered wheels in the air (`WheelInput.cpp:251`). */
static volatile LONG  g_gndDampAir = 250;
/* ---- THE LONGITUDINAL-SLIP FADE, GTA MT's longSlipMult, ported rather than approximated -------
   Theirs divides the tyre's surface speed by the contact patch's and maps 1..2 onto 1..0 with a
   floor of 0.2: a spinning or locked tyre cannot align itself, so the steering goes light. We were
   told this could not be ported because the wheel has no angular-velocity field. It can: the
   tyre solver sets `wheel+0x120` bit 0x20 when the brake demand exceeds that wheel's longitudinal
   grip limit (the wheel is LOCKED) and bit 0x80 when the drivetrain's surface speed differs from
   the car's by more than 2 m/s with the drive force over the same limit (WHEELSPIN). The engine
   clears both at the top of every tick, so they are this tick's verdict, not a latch.
   Discrete where theirs is continuous, which is the simpler solution and the honest one: we have
   the game's own yes/no rather than a ratio reconstructed out of three other fields. */
static volatile LONG  g_gndLong      = 1;    /* the fade at all                                 */
static volatile LONG  g_gndLongFloor = 20;   /* percent of SAT left, theirs is 20               */
static volatile LONG  g_gndLongTauMs = 120;  /* smoothing, so a one-tick flag is not a jolt     */
/* Which slip angle SAT follows. 0 = auto: the engine's own per-wheel angle once it has proved it
   moves, and the lateral-acceleration estimate until then. 1 = force the estimate, 2 = force the
   kinematic form, 3 = force the engine's angle. The auto default exists because a field read from
   a listing is a hypothesis until a live game agrees with it, and the fallback is what the wheel
   feels like while that is still unknown. */
static volatile LONG  g_gndSlipSrc   = 0;
/* ---- THE UNATTENDED RUN STEERS WITH WHATEVER THE WHEEL IS RESTING AT ------------------------
   Alex, watching the autotest of 2026-09-13: *"possibly the wheel's position is already turned
   left to begin with"*. It was, and the log proves it outright - the measured steering angle is
   -0.53 rad in the very first row and walks to -0.69, near full lock, and never once crosses to
   the other side. The car spun 270 degrees on the spot in a car park and drove into a wall
   because the WHEEL was at full lock and nobody was holding it.

   This is new damage from this very rework: the centring spring is gone and SAT is deliberately
   silent below 2 m/s (GTA MT's own gate), so at a standstill nothing pulls the wheel back. With a
   human in the seat that is correct and is what he asked for. With nobody in the seat it means
   every unattended run measures a car driving in a circle.

   So a bench-only pulse: hold a strong DirectInput spring for this many milliseconds after the
   first tick, which physically drags the wheel to centre before a replay starts. DEFAULT 0 - it
   must never be in a shipped ini, and `ground-keys.ps1` arms it on the bench only. */
static volatile LONG  g_gndCentreMs  = 0;
/* ---- THE CASTER TERM: self-steer that does not wait for speed ---------------------------------
   His objection, 2026-09-13, and it is right: *"if there is no centring at all below 10 km/h that
   is incorrect... what I need is not really a spring but self-steer as I drive. When I have turned
   the car and start moving, the wheels naturally steer back."*

   The physics, and our own log agrees with it: the aligning torque is `Fy x (pneumatic trail +
   caster trail)`, and `Fy` follows the SLIP ANGLE and the load - not the speed. What falls away
   with speed is the lateral force a steady corner DEMANDS (m v^2 / R), not the force a dragged
   tyre generates. Measured in the corner run: at 2-5 km/h the front slip angle sits at 0.19 rad,
   eleven degrees, right in the tyre's working range - and our force was zero there only because
   `ground_v_min_cms` silenced it. GTA MT's 0.5-2.0 m/s ramp is numerical hygiene (their own
   comment says the longitudinal slip is unstable below about 5 km/h), not a statement about
   aligning torque.

   So two changes. SAT now fades in from the first motion rather than from 1.8 km/h, and this term
   is added beside it: the caster and kingpin geometry, which produces a restoring torque
   proportional to the STEERING ANGLE and the front load at any rolling speed. It is what
   straightens the wheels as you pull out of a turn.

   At a true standstill it is silent, and that is deliberate rather than a limitation: with no
   rolling there is no slip angle and a real car's wheel does NOT return - static scrub friction
   beats the geometry, which is why a parked car keeps its lock. The standstill damper is that
   friction, and it is untouched at 20000. */
/* ---- THE CHECK THAT WOULD HAVE CAUGHT THE INVERTED SIGN --------------------------------------
   The audit already had a check called "the caster restores - its sign opposes the steering
   angle", and it passed on 261 rows of the very drive where the wheel was being dragged to full
   lock. It could not have failed: it compared the sign of OUR force against the sign of OUR
   measured angle, and both are expressed in the vehicle's frame. A flip between that frame and
   the DirectInput axis is invisible to any comparison made entirely on one side of it.

   So this one is closed-loop and physical, and the wheel itself answers it. While the car is
   rolling and nothing is steering, push the wheel off centre with a fixed force, let go, and
   watch the angle the GAME reports. If our sign is right the angle comes back; if it is wrong it
   runs to the lock. No convention of ours takes part in the verdict - only the direction the
   hardware actually moved.

   BENCH ONLY, default 0, armed by `ground-keys.ps1`: it takes the wheel out of the driver's
   hands for half a second, which is fine for an unattended replay and unacceptable in a drive. */
/* ---- THE REPLAY CANNOT DRIVE A ROUTE WHILE WE ARE STEERING FOR IT ----------------------------
   Three authored routes and one of HIS OWN recorded drives all ended wedged in a building inside
   ninety metres. His route is not the problem - it is the only one in this project known to work.
   The problem is that a recorded drive replays KEYBOARD taps that were made with a wheel sitting
   where he left it, and in an unattended run our own force is free to turn that wheel: every tap
   then lands on a car already steering somewhere else.

   So on the bench the wheel is held near centre for the whole run, and released only for the
   seconds the self-test is actually measuring. It changes what the CAR does, never what the model
   COMPUTES - SAT, the caster, the gate and the fade are logged exactly as they would be - so the
   forces stay readable while the route becomes repeatable.

   BENCH ONLY, default 0. In a drive it would be a centring spring, which is the one thing this
   whole rework removed. */
static volatile LONG  g_gndHoldCentre = 0;
static volatile LONG  g_gndSelfActive = 0;    /* 1 while the self-test owns the wheel            */
static volatile LONG  g_gndSelfTest  = 0;     /* 0 = off, N = do it N times, alternating sides  */
static volatile LONG  g_gndSelfMag   = 5000;  /* the displacing push                            */
/* THE CASTER IS OURS, and GTA MT has no term like it - said plainly so nobody looks for it in
   their source. It exists because their model has a hole we cannot live with: past 2.5x the
   optimal slip angle their tyre curve is zero, so at full lock there is nothing holding the wheel
   at all. They accept that; he does not, and drive ours-018b named it.
   1800 -> 2400 WITH A SQUARE LAW (`ground_caster_exp` 200), which answers two of his complaints
   with one curve instead of pulling the same constant in both directions:
       at full lock   2400 against 1800 - the U-turn he said had no self-steer
       at half lock    600 against  900
       at a quarter    150 against  450 - the release he said straightened far too hard
   It is a shape, not a new mechanism, and it is defensible as geometry: the kingpin's jacking
   torque grows faster than the steering angle, which is why a real car's wheel comes back hardest
   from full lock and gently from a few degrees. */
static volatile LONG  g_gndCasterK   = 2400;  /* DI force at full lock, full load, rolling      */
static volatile LONG  g_gndCasterExp = 200;   /* percent: 100 linear in the angle, 200 square   */
static volatile LONG  g_gndCasterVcms = 40;   /* cm/s at which it is fully in - "rolling at all" */
/* The rate term, and it was the self-test that asked for it rather than anybody's taste. With the
   sign fixed, a wheel released from full lock came back in 0.66 s and then RANG at +-0.18 rad,
   ten degrees, barely decaying. A restoring force with no damping is a spring by another name and
   an oscillator by behaviour; a real steering system damps through the tyre and the box. So the
   caster gets a term proportional to the steering angle's RATE, opposing it - and it rides the
   same load and rolling gates, because a wheel in the air has nothing to damp against either. */
static volatile LONG  g_gndCasterD   = 600;   /* force per rad/s of steering movement           */
static volatile float g_gndLastCaster = 0.0f;
static volatile float g_gndLastDetail = 0.0f;   /* the texture channel, for the log row 0x22     */
static volatile float g_gndLastCasterD = 0.0f;  /* the caster's RATE term alone, for the log     */
static volatile LONG  g_gndSlipLive  = 0;    /* the engine's angle has been seen to move        */
static volatile float g_gndLastSinSlip = 0.0f;  /* what the engine said, for the log            */
static volatile float g_gndLastLong    = 1.0f;  /* the smoothed longitudinal fade, for the log  */
/* TWO KEYS, NOT A TOGGLE, and they are settings rather than constants.
   It was Scroll Lock, then K. Alex killed both in turn, 2026-09-13, and the second objection is
   the interesting one - *"two buttons then, one to switch it on and one to switch it off,
   because I press K and I do not know whether it is on or off"*. A toggle only carries
   information to
   someone who already knows the current state, and he is driving a full-screen game with no
   indicator - so the control was ambiguous exactly when it mattered, in the middle of the A/B it
   exists to make possible. Two keys each ASSERT a state: press it twice and the state is still
   the one you asked for.

   'I' = on, 'O' = off - the I/O marking on every power switch ever made. Both come out of the
   letter mark bank retired in v7.65 (U I O P J K L), which was chosen in v7.18 for a reason that
   still holds: Windows maps letter keys by POSITION, so the physical key reports VK 0x49
   whatever his Russian layout prints on it. `ground_key_on` / `ground_key_off` in the ini take
   any VK, so a clash with the game is a number in a file rather than a rebuild. */
static volatile LONG  g_gndKeyOn  = 0x47;  /* VK 'G' - switch the ground channel ON             */
static volatile LONG  g_gndKeyOff = 0x4E;  /* VK 'N' - switch it OFF                            */
static volatile LONG  g_gndAckMag = 4000;  /* the acknowledging nudge, 0 disables it            */
static volatile LONG  g_gndAck = 0;        /* ticks of that nudge still owed                    */
static volatile LONG  g_gndAckPat = 0;     /* 0 = ON nudge, 1 = OFF nudge, 2 = count N pulses   */
static volatile float g_gndLastLoad = 0.0f;
static volatile float g_gndLastGrip = 1.0f;  /* 1 = the front is gripping, 0 = it has let go   */
static volatile LONG  g_gndDampSlip = 0;     /* % of the damper that goes with the grip, 0 = old */

/* ---- v811: WHAT "THE FRONT HAS LET GO" MEANS TO THE DAMPER, and why it needed its own answer.
   MEASURED IN HIS OWN LOGS, ours-033 and the 2026-09-14 morning drive, above 25 km/h, the damper
   as SENT (log row 0x1D) against the engine's own slip angle (row 0x1E):

       under 1 deg, driving straight   610 / 618   =  23% of the 2625 the setting asks for
       1-3 deg                         985 / 972   =  38%
       3-6 deg                        1434 / 1461  =  55%
       6-9 deg, the peak              2129 / 2252  =  81-86%
       over 9 deg, a real slide        756 / 577   =  22-29%

   The damper was following the tyre-force CURVE, which is zero at zero angle and zero past the
   fade - so a straight road and a slide were the same thing to it, and the wheel ran on a quarter
   of its damper everywhere except mid-corner. On a parked car the same arithmetic turned the
   20000 standstill damper into 4399, which is what Alex had been reporting by feel for days.

   `ground_damp_grip_mode = 1` (the default from v811) says the front is GRIPPING until the slip
   angle passes the peak, and only then follows the curve down. `damp_slip` keeps doing exactly
   what he asked it for - removing the damper in a slide - and stops doing the thing nobody asked
   for. Mode 0 is the arithmetic every drive up to v810 was judged on, kept because the approved
   feel was approved WITH it, and the B key switches between them so the difference is heard
   rather than argued. */
/* ---- SETTLED BY ours-034, 2026-09-14, and both halves are his -------------------------------
   MOVING: mode 0, the v810 arithmetic. He drove the fix and refused it, twice in his own words -
   driving is definitely preferred on F6 and a direct damper is not wanted,
   because everything was done to get a responsive wheel... yesterday it was tuned very finely and
   for a long time. The fix stays in the file behind mode 1 because it is correct and because
   the standstill half is built on the same arithmetic; it does not ship on.
   PARKED: 6000, walked to on the F3/F4 ladder and then marked FIVE TIMES with F7 -
   this standstill damper to be made the reference - the current one. The log has the state at those
   marks: standstill 6000, mode 0, moving 2625. He allowed 7000 later and did not ask for it.
   `ground_damp_stand` applies in either mode, so 6000 is reached without the moving feel moving. */
static volatile LONG  g_gndDampGripMode = 0; /* 1 = gripping below the peak, 0 = the v810 curve   */
static volatile LONG  g_gndDampStand = 6000; /* the damper at a standstill, 0 = whatever falls out */
/* Rung 10 = 20000, because that is where the FIXED arithmetic actually lands a parked car: with
   the grip mode on, a car at rest has no slip angle, so nothing is removed and the standstill
   damper is the 20000 the setting has always named. Starting the ladder anywhere else would make
   his first F3 press jump instead of step. */
static volatile LONG  g_gndDampStandIdx = 9;
static volatile LONG  g_gndDampMoveAbs = 0;  /* the moving damper as an ABSOLUTE, 0 = use the pct  */
static volatile LONG  g_gndDampKeys = 0;     /* F1..F6 damper ladders, off since ours-034 settled  */
/* THE KEY WINS OVER THE FILE for the rest of the session, exactly as the I/O override does. The
   ini is re-read once a second, so without this his F1 press would be undone a second later while
   the log went on reporting the value he chose - a trap this project has already paid for once
   (v7.90, the preset bank) and the one thing that would make a whole tuning drive unreadable. */
static volatile LONG  g_dampKeyUsed   = 0;
static volatile LONG  g_dampKeepMode  = 1;
static volatile LONG  g_dampKeepAbs   = 0;
static volatile LONG  g_dampKeepStand = 0;
static volatile LONG  g_gndCentreK   = 0;     /* concave rise: slope at zero is (1 + k/100), 0 = off */
static volatile float g_gndLastForce = 0.0f;

/* ---- v825: EVERY SLIDER GOES TO 400%, AND EVERY CEILING GOES WITH IT --------------------------
   His decision after ours-041 (900 deg on a 2 Nm base, "far too weak", Alex 2026-09-30): no
   tuning for weak hardware - instead every slider the utility writes gets headroom to 3-4x,
   0..400%, with 100% = his reference. And HOW a slider scales is the rule the roll and crash cuts
   were settled on: PROPORTIONALLY. A slider multiplies its channel's whole curve, that channel's
   own ceiling included. A ceiling that stays put while the force grows under it is a cut, not a
   slider - at 150% the v824 crash cap flattened every fast hit to the same 25000.

   What that takes, one line each:
     - the crash kick is capped BEFORE the slider, so the cap scales with it (CrashKickMag);
     - the ceiling guard reads the SLIDER-FREE magnitude, or a slider would decide which kicks
       have to be corroborated ([[slider-vs-detector]] - the trap this project has paid for);
     - the ceiling on the continuous SUM scales with the largest slider in it (ContinuousCap);
     - the ground channel's slew ceiling scales with the channel (GroundScalePct), or at 400% the
       wheel would trail the same corner four times further behind;
     - every ini clamp on a key the utility writes admits 4x his reference (HEADROOM).
   The device's own maximum stays the physical ceiling: nothing here adds a clamp below the scaled
   values. AT 100% EVERYWHERE EVERY CEILING IS THE v824 NUMBER TO THE BIT - the scales are whole
   percents and every multiply is skipped at 100, because a float that is "1.0 give or take" at the
   reference is exactly how "inert at the default" stops being true on an x87. */
#define SLIDER_MAX_PCT       400
#define HEADROOM(ref)        ((LONG)(ref) * SLIDER_MAX_PCT / 100)
/* His reference as the module's ini defaults, in ONE place, so the scale a key is measured against
   and the value a silent ini gives cannot drift apart. memory\the-approved-driving-feel.md. */
#define GND_REF_SAT_K        7875   /* 875 per degree x the 9-degree breakaway */
#define GND_REF_CASTER_K     2400
#define GND_REF_DETAIL_K      900
#define GND_REF_DETAIL_LIM   5000
#define GND_REF_DAMP_STAND   6000
#define ROAD_REF_ROLL_CAP    3000

/* THE CRASH KICK'S MAGNITUDE, for both sharp-crash fire sites. v824 applied the slider first and
   the cap after it - `Mul(Mul(force, trim), crash)`, then `min(.., g_crashCap)` - so above 100% a
   hit already at the formula's 25000 could not grow at all. Capping FIRST and scaling after is the
   same number at 100% (Mul is the identity there) and min(a*p, c*p) = min(a, c)*p for every p, so
   the cap now scales with the slider exactly as the kick does.
   *guardMag receives the magnitude AT 100%: the ceiling guard's population must not move with a
   slider. In v824 crash = 400 made every kick above 6250 a "ceiling kick" to be corroborated, and
   crash = 50 took the guard away from the real ceiling kicks altogether. */
static LONG CrashKickMag(LONG force, LONG trim, LONG *guardMag)
{
    LONG ref = Mul(force, trim);
    if (ref > g_crashCap) ref = g_crashCap;
    if (guardMag) *guardMag = ref;
    return Mul(ref, g_pctCrash);
}

/* How big a key is against his reference, in WHOLE percent, rounded UP - a ceiling scaled from it
   may be a hair generous, never a hair short. Exactly 100 at the reference. The 1000000 is an
   overflow guard on `v * 100` for a hand-typed absurdity, not a limit on anything real. */
static LONG PctOfRef(LONG v, LONG ref)
{
    if (v < 0) v = (v < -1000000) ? 1000000 : -v;
    if (v > 1000000) v = 1000000;
    return (v * 100 + ref - 1) / ref;
}

/* The ground channel's size against his reference, in percent: the largest of its three gains.
   The utility writes the weight as sat_k AND caster_k together and the texture as detail_k (with
   its limit), so a slider moves at least one of these - and a bench file that moved only one is
   still measured by the one it moved. The limit is not a gain and is not counted: it follows
   detail_k in the utility, and a limit raised alone does not make the channel any bigger. */
static LONG GroundScalePct(void)
{
    LONG s = PctOfRef(g_gndSatK, GND_REF_SAT_K), r;
    r = PctOfRef(g_gndCasterK, GND_REF_CASTER_K); if (r > s) s = r;
    r = PctOfRef(g_gndDetailK, GND_REF_DETAIL_K); if (r > s) s = r;
    return s;
}

/* THE CEILING ON THE CONTINUOUS SUM - road (roll + pitch) x `road`, the old slip SAT x `sat`, and
   the ground channel - which v824 clamped at a flat MAX_MAG. It scales with the LARGEST slider in
   the sum, so no channel at up to 400% meets a ceiling tighter than the one it met at 100%; a
   channel at 100% beside it keeps its v824 relation to the ceiling or gains room, never loses it.
   Whole percents, so at the reference this is Mul(MAX_MAG, 100) = MAX_MAG exactly. 10000 is an
   overflow guard on Mul (100x, far past any slider), not a ceiling on anything a slider reaches. */
static float ContinuousCap(void)
{
    LONG h = g_pctRoad, g;
    if (g_pctSat > h) h = g_pctSat;
    if (g_gndOn) { g = GroundScalePct(); if (g > h) h = g; }
    if (h < 0) h = 0;
    if (h > 10000) h = 10000;
    return (float)Mul(MAX_MAG, h);
}

/* v825: A HOTKEY OF 0 IS NO KEY. The utility writes 0 into a player's ini for every developer key -
   his rule since 2026-08-01, "no F-keys in a shipping build", restated for release 2.0 - and v824
   turned a 0 back into the DEFAULT key for `ground_key_on/off` and every `presetN_key`. With the
   banks off that was worse than writing nothing: F1..F5 went live as presets, F5 swapped the
   steering to the old 4-degree reference for the rest of the session, U I O P switched the speed
   curve, G and N the road model. A value outside 1..254 is still a typo and still falls back;
   a 0 is a decision and is kept. KeyDown refuses vk 0 and PollPresetKeys skips it, so a 0 kept
   here never reaches GetAsyncKeyState. The bench keeps its keys: tools\ground-keys.ps1 writes
   real VKs. */
static LONG HotkeyVk(LONG v, LONG dflt)
{
    if (v == 0) return 0;
    if (v < 1 || v > 254) return dflt;
    return v;
}

/* ---- v7.90: THE PRESET BANK - five complete feels, one key each ------------------------------
   Request, 2026-09-13: make all the modes easy to change with one key press or with
   several key presses, so that what is liked, and when, can then be reported.

   One key per preset, never a cycle - the same reason the ground channel got two keys instead of
   one ([[no-numpad-keyboard]]): a cycle key leaves him not knowing where he now is, and he is
   driving a full-screen game with no indicator. F3 means preset 3 whatever was live before, so
   the state never has to be remembered, and pressing it twice still leaves him in preset 3.

   The five presets are DATA IN THE INI, not constants, and GroundReadIni re-reads the file every
   second - so a preset can be retuned WHILE HE DRIVES, one number at a time, without a rebuild
   and without him leaving the car. That is the tuning session the worklist asks for.

   F1..F5 are the camera's seat nudges in ordinary use; `tools\ffb-preset-keys.ps1 -Take` clears
   the camera's six key_seat_* for the duration of an FFB drive and -Give puts them back, exactly
   as the origffb probe already does it. Every key is a setting (preset_key1..5), so a clash is a
   number in a file rather than a rebuild. */
/* v7.94: NINE presets, because the bank is now TWO LADDERS and his instruction was to build both:
   the demand for a later breakaway everywhere except F5 put a breakaway ladder on F1..F4, and the added
   demand for a steering ladder on U I O P puts a steering-return ladder on the four letters. Each ladder
   moves ONE quantity and holds everything else equal, which is the only way a drive can answer
   which rung is right. F5 is untouched and belongs to neither: it is the reference he asked to
   keep as the starting point. */
#define GND_PRESETS 9
/* F1..F5, then U I O P. The letters were OUR OWN retired mark bank (docs\KEY-MAP.md), so this is
   a reuse, which is what he asked for. The game's defaults leave O and P alone; U and I it binds
   (I is INVENTORY), and that class of clash was already ruled not to matter. What could
   NOT stand is a key doing two of OUR things at once - I and O were the ground channel's own
   on/off pair until this build, so that pair moves to G and N below. */
static volatile LONG g_presetKey[GND_PRESETS]  = { 0x70, 0x71, 0x72, 0x73, 0x74,
                                                   0x55, 0x49, 0x4F, 0x50 };
/* THE NUMBERS COME FROM HIS OWN DRIVING, not from taste. `tools\preset-preview.py` replays an
   archived drive's real angles and loads through each preset's curve; on ours-022 his front slip
   angle has a MEDIAN of 1.41 degrees, p90 of 5.4 and p99 of 42. The first bank written here put
   the peaks at 12 and 14 degrees with a bend of 200, and the preview said those presets would
   have left 2 units of force where the baseline has 134 - a wheel that is dead in ordinary
   driving, which he would have reported as "empty" and been right. A peak must sit near the
   angles he actually reaches, and a square rise is too much at 1.4 degrees whatever the peak. */
/* THE BANK AFTER ours-023, and the layout is SPECIFIED: F1 kept as the base, F2 and F4 dropped, and
   in their place were added... two new modes that might be even stronger than F5,
   vivid, but with a weaker damper. F1, F3 and F5 are untouched so the three that the driver
   already has an opinion about stay comparable; F2 and F4 are the new pair.

   WHAT HE JUDGED, MEASURED FROM HIS OWN DRIVE. The "centre feel" he ranked the presets by is the
   force at SMALL slip angles, and the log agrees with him to the row - median |force| between 0
   and 0.5 degrees above 50 km/h: F5 232, F3 154, F2 148, F1 110, F4 106, and F5 is the one he
   liked most. That number is the curve's SLOPE AT ZERO, `sat_k / opt_deg * (1 - bend/100)`, so
   the two new presets get there by moving the peak in and straightening the rise, not by a knob
   nobody has. (The caster was my guess for this and it was WRONG - it cannot be, it follows the
   steering angle and is unchanged between presets.) */
/* THE BANK AFTER ours-024 IS A LADDER OF BREAKAWAY ANGLES, and that is his instruction:
   the breakaway to come later on all except F5, since it is the reference point. Maybe make several
   different breakaways to find a good one. So F1..F4 differ ONLY in where the tyre lets go - 12, 16,
   20, 24 degrees - and every other quantity is held equal between them: the same ceiling, the
   same light damper, and the same force per degree at dead centre (~1140), which is what
   ground_centre_k is for. The measurement the ladder brackets: on ours-024 the engine's own
   squeal flag is up in 0.2-2.6% of rows below 20 degrees of OUR angle, 14.5% at 20-25 and 90.7%
   at 25-30, so the game's tyre lets go somewhere around 20-22.

   THE DAMPER IS SPECIFIED TOO: drifting wanted by feel, by lightness, like on F2 or even like
   between F2 and F4, not like now on F5. F5 was too heavy because of the damper. ours-024's
   F2 was 30/70 and its F4 15/85, so the whole ladder runs 22/78 - between the two he named.
   F5 keeps 50/0 because F5 is the reference he asked to leave alone, which makes it the heavy
   "before" rather than a candidate. */
/* THE SECOND LADDER, U I O P: one breakaway for all four (16 degrees, the middle of the first
   ladder) and only the centre moves - 875, 1140, 1400, 1700 force per degree at dead centre.
   875 is F5's own return, the one he has liked; 1400 is the old F2 he called too sharp when it
   came with a peak at 5 degrees; 1700 is past it, because a ladder that stops at the number he
   already rejected cannot tell us the rejection was about the return rather than the peak. */
static volatile LONG g_presetOn[GND_PRESETS]   = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static volatile LONG g_presetOpt[GND_PRESETS]  = { 9, 9, 9, 9, 4, 9, 9, 9, 9 };
static volatile LONG g_presetFall[GND_PRESETS] = { 0,  0,  0,  0,  0, 0,  0,  0,  0  };
static volatile LONG g_presetPost[GND_PRESETS] = { 170, 170, 170, 170, 300, 170, 170, 170, 170 };
static volatile LONG g_presetBend[GND_PRESETS] = { 100, 100, 100, 100, 130, 100, 100, 100, 100 };
static volatile LONG g_presetSatK[GND_PRESETS] = { 7875, 7875, 7875, 7875, 5000,
                                                   7875, 7875, 7875, 7875 };
/* The damper, per preset. `damp_pct` is the moving damper as a percentage of the profile's,
   `damp_slip` is how much of it goes away when the front lets go. The whole ladder is 22/78 (see
   above); only F5 keeps the heavy 50/0 he drove before, as the reference. */
static volatile LONG g_presetDamp[GND_PRESETS] = { 50, 50, 50, 50, 50, 50, 50, 50, 50 };
static volatile LONG g_presetDSlip[GND_PRESETS] = { 78, 78, 78, 78, 0, 78, 78, 78, 78 };
/* v7.94: the concave rise is what HOLDS THE CENTRE STILL while the peak walks out. The slope at
   zero is sat_k / opt_deg * (1 + k/100), so a peak twice as far out halves the centre unless k
   rises with it. Solved for ~1140 force per degree at dead centre, the middle of the three
   returns designed for v793: k = 1140 * opt / 14000 - 1, which is 0 (12 deg, the line already
   gives 1167), 30 (16), 63 (20) and 95 (24). That is the point of the ladder - the only thing
   his hands should be able to tell apart between F1 and F4 is WHERE the front lets go.

   The letter ladder solves the same equation the other way: the peak is pinned at 16 and k is the
   variable, k = target * 16 / 14000 - 1, so 0 / 30 / 60 / 94 give 875 / 1140 / 1400 / 1697. */
static volatile LONG g_presetCK[GND_PRESETS]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
/* v7.95: the RETURN axis, force per degree at dead centre. Non-zero marks a preset as a return
   rung: it sets this one quantity and leaves the breakaway, the damper and the fade alone.
   875 is the return APPROVED on ours-027 (f3 and U are the reference, better than f5 before), and
   on the same day two rungs BELOW it were requested: sometimes there still arose a feeling of too
   fast and sharp feedback. So the ladder slid down by two: the approved 875 now sits on
   O, the old I sits on P, and U and I are the softer pair nobody has driven yet. */
static volatile LONG g_presetRet[GND_PRESETS]  = { 875, 875, 875, 875, 0, 0, 0, 0, 0 };
/* v7.97: which SPEED CURVE each preset selects, -1 = none (it is not a curve key). U I O P are
   the speed SHIFT of the one approved curve; F1..F4 and F5 are not curve keys and leave it alone. */
/* 0 = NOT a curve key; 1..4 select rows 0..3 of the bank. Zero rather than -1 because
   GetPrivateProfileInt returns a UINT: an ini holding -1 reads back as 4294967295, the arming
   tool refuses it as "did not take", and the trap is already written up in [[truck-damper-sliders]]. */
/* v7.99 - TWO CURVES ON TWO KEYS, instruction of 2026-09-14: put the specified scheme on key
   U and the physics on key I. These two keys then switch between the specified scheme and the physics.
   U carries the shape he specified band by band after ours-030; I carries the measured flattening
   from [[the-wheel-must-get-heavier-with-speed]]. O and P deliberately carry NOTHING: they restore
   the base, exactly like F1..F4, so a stray press cannot land him on a third feel in the middle of
   an A/B. Two keys, two answers. */
static volatile LONG g_presetCurve[GND_PRESETS][GND_SPD_PTS] = {
    {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    /* U - the curve he DROVE on ours-032 (v803). The fixed foot of every comparison. */
    { 22, 34,  46,  70,  95, 115, 138, 208 },
    /* I - THE LIGHTER CURVE, and the base from v810 to v816. Computed from his three conditions
       against the curve he had driven (20 unchanged, 50 cut by 30%, the weight felt at 80 arriving
       at 95). It became the base on a reading of his ours-033 verdict that the log does not
       support - "write this new one on I as the absolute reference for how driving feels"
       (Alex, 2026-09-14) came after 290 s on O, with I never pressed. After ours-035 he found
       the wheel too empty on it, above all at speed. Kept here so the comparison is one key away.
       See memory/the-approved-driving-feel.md. */
    { 22, 34,  39,  49,  65,  84, 105, 173 },
    /* O - "THE HALF", halfway between U and I, and THE BASE AGAIN since v817. The curve he drove
       last before naming a reference, and the one he asked for back after ours-035: "make the
       heavier curve - at high speed it needs more effort" (Alex, 2026-09-30). Same numbers as
       g_gndSpdM, so pressing O restores the unpressed state exactly. */
    { 22, 34,  42,  60,  80, 100, 122, 190 },
    {0,0,0,0,0,0,0,0}
};
static volatile LONG g_presetScale[GND_PRESETS] = { 100, 100, 100, 100, 100, 100, 100, 100, 100 };
/* 1 = this preset sets the speed shift, 0 = it does not touch it. A separate flag rather than
   "shift != 0", because I's shift IS zero and that is the approved curve, not "no opinion". */
static volatile LONG g_presetIsCurve[GND_PRESETS] = { 0, 0, 0, 0, 0, 1, 1, 1, 0 };
static volatile LONG g_presetCur = 0;      /* 0 = none pressed yet, the ini's own values stand  */

/* THE BEND of the tyre curve below its peak. 100 = the straight line we have shipped so far;
   200 = a square, which is light at small angles and builds hard as the corner tightens. His
   words for what is missing: the wheel getting loaded as the corner tightens, like in Assetto
   Corsa"*. Implemented as a blend between the line and the square rather than a true power,
   because this module links -nostdlib and has no powf - and a blend is monotonic, exact at both
   ends, and costs two multiplies. */
static volatile LONG g_gndBend = 100;

/* "preset" + digit + suffix, built by hand because this module links -nostdlib and has no
   sprintf. buf must hold at least 32 bytes; every suffix below is far shorter than that. */
static void PresetKeyName(char *buf, int idx, const char *suffix)
{
    const char *p = "preset";
    int i = 0;
    while (*p) buf[i++] = *p++;
    buf[i++] = (char)('1' + idx);
    for (p = suffix; *p; p++) buf[i++] = *p;
    buf[i] = 0;
}

/* Put a preset's whole feel in force. Called from the key AND from the end of every ini re-read:
   without the second call the file would silently undo his choice one second after he made it,
   which is exactly the trap the ground channel's override line above was written for. */
/* v7.95 - THE BANK IS TWO AXES, and the design was requested: UIOP as the steering-return tuning,
   not at breakaway 8 but at the breakaway set with the keys F1 and F4 (2026-09-14).

   Until now every key asserted a COMPLETE feel, so the letter ladder had to pin a breakaway of its
   own and pressing F2 silently threw away whichever centre he had chosen. Two ladders that
   overwrite each other cannot answer two questions in one drive - ours-026 shows exactly that, at
   03:04: unclear what was going on with the steering settings.

   So: F1..F4 set WHERE THE TYRE LETS GO, U I O P set HOW HARD THE WHEEL RETURNS, and the two
   compose. The return is stored as force per degree at dead centre, not as a curve constant,
   because that is the quantity he ranks by and it is the one that must survive a change of peak.
   With a straight rise the peak force follows from the two: sat = return x opt. So moving the
   breakaway out makes the peak stronger and leaves the centre exactly where it was, which is the
   whole point of separating them.

   F5 is neither axis: it is the untouched reference, and pressing it CLEARS the return override so
   the wheel is bit-for-bit the preset he has been comparing against since ours-023. */
static volatile LONG g_curRetK   = 0;   /* force per degree at centre, 0 = no override (F5's own) */
static volatile LONG g_curBreak  = 0;   /* 1..4, which breakaway rung is live; 0 = none pressed   */

static void PresetApply(int idx)
{
    if (idx < 0 || idx >= GND_PRESETS) return;
    /* v7.97: A LETTER SELECTS A SPEED CURVE and nothing else. It does not touch the breakaway
       (fixed at F3's 9 degrees on 2026-09-14: F3 is now left in place and locked, and
       no further change is wanted), it does not touch the base return, the damper or the fade.
       Every curve passes through 100% at 40 km/h, so switching one mid-drive cannot change how
       the city feels - only how the weight grows away from it. */
    if (g_presetIsCurve[idx]) {
        int k;
        for (k = 0; k < GND_SPD_PTS; k++) g_gndSpdM[k] = g_presetCurve[idx][k];
        g_gndSpdScale = g_presetScale[idx];
        g_gndSpdOn = 1;
        g_gndCurveKey = (LONG)(idx + 1);
        return;
    }
    g_gndOn        = g_presetOn[idx] ? 1 : 0;
    g_gndOptDeg    = g_presetOpt[idx];
    g_gndFalloff   = g_presetFall[idx];
    g_gndPostRatio = g_presetPost[idx];
    g_gndBend      = g_presetBend[idx];
    g_gndSatK      = g_presetSatK[idx];
    g_gndDampMovePct = g_presetDamp[idx];
    g_gndDampSlip    = g_presetDSlip[idx];
    g_gndCentreK     = g_presetCK[idx];
    if (g_gndCentreK < 0)   g_gndCentreK = 0;
    if (g_gndCentreK > 400) g_gndCentreK = 400;
    if (g_gndDampMovePct < 0)   g_gndDampMovePct = 0;
    if (g_gndDampMovePct > 200) g_gndDampMovePct = 200;
    if (g_gndDampSlip < 0)      g_gndDampSlip = 0;
    if (g_gndDampSlip > 100)    g_gndDampSlip = 100;
    if (g_gndOptDeg < 1)        g_gndOptDeg = 1;
    if (g_gndOptDeg > 45)       g_gndOptDeg = 45;
    if (g_gndFalloff < 0)       g_gndFalloff = 0;
    if (g_gndFalloff > 100)     g_gndFalloff = 100;
    if (g_gndPostRatio < 110)   g_gndPostRatio = 110;
    if (g_gndPostRatio > 600)   g_gndPostRatio = 600;
    if (g_gndBend < 100)        g_gndBend = 100;
    if (g_gndBend > 200)        g_gndBend = 200;
    if (g_gndSatK < 0)          g_gndSatK = 0;
    /* v825: 4x his reference (31500), not 20000 - see SLIDER_MAX_PCT. Bench presets only; the
       player's ground_sat_k never comes through here, and no preset in the bank is near either. */
    if (g_gndSatK > HEADROOM(GND_REF_SAT_K)) g_gndSatK = HEADROOM(GND_REF_SAT_K);
    /* THE OTHER AXIS SURVIVES A BREAKAWAY PRESS. F5 (the reference) clears it instead, so it
       stays the preset he has been comparing against. A rung is "a breakaway rung" by carrying
       an opt of its own and no return - the same test the ini tables are built on. */
    if (idx == 4) {
        g_curRetK  = 0;
        g_curBreak = 0;
    } else {
        g_curBreak = (LONG)(idx + 1);
        if (g_curRetK > 0) {
            g_gndCentreK = 0;
            g_gndBend    = 100;
            g_gndSatK    = g_curRetK * g_gndOptDeg;
            if (g_gndSatK > HEADROOM(GND_REF_SAT_K)) g_gndSatK = HEADROOM(GND_REF_SAT_K);
        }
    }
}

static void GroundReadIni(void)
{
    char ini[MAX_PATH];
    FFBIniPath(ini);
    g_gndOn      = (LONG)GetPrivateProfileIntA("ffb", "ground", 1, ini);
    /* THE FEEDBACK MODE, the utility's selector of 2026-09-14. `ffb_mode` is one choice where
       there used to be a flag, because three things can drive the wheel and only two of them can
       be told apart by `ground`:
           0 = this road model      -> ground on
           1 = legacy, i.e. v7.67   -> ground off
           2 = the game's developers' own feedback, which this module does not compute at all
       ABSENT ON PURPOSE MEANS NOTHING CHANGES: the key defaults to -1, and at -1 `ground` alone
       decides exactly as it did before, so every ini already on a bench or in a player's folder
       keeps its behaviour to the byte. A value this build does not know is ignored the same way
       rather than silently picking a mode.
       Mode 2 is a different code path chosen before the game creates its device, so it is read
       here only to be REPORTED and to keep our own forces out of the way; the utility says a
       restart is needed and this is why. */
    {
        /* GetPrivateProfileInt returns UINT, so the "absent" sentinel is read as a LONG and
           compared to -1 - a `< 0` test on the raw return can never be true, which this project
           has already paid for once (memory\truck-damper-sliders.md). */
        LONG m = (LONG)GetPrivateProfileIntA("ffb", "ffb_mode", (UINT)-1, ini);
        if (m == 0)                 g_gndOn = 1;   /* this road model */
        else if (m == 1 || m == 2)  g_gndOn = 0;   /* legacy, or the developers' own */
        if (m != -1 && m != g_ffbMode) {
            /* 0x38, not 0x31: 0x31 is the preset's damper half (v7.92). */
            Log(K_INIT, 0x38, (DWORD)g_ffbMode, (DWORD)m, (DWORD)g_gndOn);
            g_ffbMode = m;
        }
    }
    /* v815. 1 = notice when the game hands the actor a DIFFERENT vehicle object and re-resolve;
       0 = keep the v814 behaviour, which holds the old object for as long as it still looks like
       a car. See VehHunt. A force, in the sense that it decides whether the wheel is alive after
       a level restart, so it is switchable and its default is written down rather than assumed. */
    g_gndRevalidate = (LONG)GetPrivateProfileIntA("ffb", "ground_revalidate", 1, ini);
    g_gndGate    = (LONG)GetPrivateProfileIntA("ffb", "ground_gate", 1, ini);
    g_gndSat     = (LONG)GetPrivateProfileIntA("ffb", "ground_sat", 1, ini);
    g_gndDetail  = (LONG)GetPrivateProfileIntA("ffb", "ground_detail", 1, ini);
    /* v7.95b: the baked defaults ARE the reference pair now - F3 + U, 9 degrees of breakaway and
   875 of return, which is 7875 at the peak. The verdict on ours-027: f3 and U are the reference,
   better than f5 before. An ini that is silent should give the approved wheel, not the
   feel that was current when the key was first added. */
    /* No clamp, and v825 keeps it that way: the utility's weight row writes up to 4x (31500). */
    g_gndSatK    = (LONG)GetPrivateProfileIntA("ffb", "ground_sat_k", GND_REF_SAT_K, ini);
    g_gndOptDeg  = (LONG)GetPrivateProfileIntA("ffb", "ground_opt_deg", 9, ini);
    g_gndFalloff = (LONG)GetPrivateProfileIntA("ffb", "ground_falloff_pct", 0, ini);
    if (g_gndFalloff < 0)   g_gndFalloff = 0;
    if (g_gndFalloff > 100) g_gndFalloff = 100;
    g_gndPostRatio = (LONG)GetPrivateProfileIntA("ffb", "ground_post_ratio_pct", 170, ini);
    if (g_gndPostRatio < 110) g_gndPostRatio = 110;
    if (g_gndPostRatio > 600) g_gndPostRatio = 600;
    g_gndDampSlip = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_slip", 78, ini);
    /* v811 - see g_gndDampGripMode for the measurement these two came out of. The mode ships at 1
       (the tyre holds below the peak); 0 restores the arithmetic every drive up to v810 was judged
       on, so the approved feel can be gone back to in one key. The standstill damper ships at 0,
       which means "do not intervene" - the J bank is a tuning knob, not a shipped number, and with
       the mode fixed a parked car lands on its own 20000 again. */
    g_gndDampGripMode = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_grip_mode", 0, ini);
    if (g_gndDampGripMode) g_gndDampGripMode = 1;
    g_gndDampStand = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_stand", GND_REF_DAMP_STAND, ini);
    if (g_gndDampStand < 0)     g_gndDampStand = 0;
    /* v825: 4x his 6000 (24000), not 20000 - the utility's parking damper goes to 400%. */
    if (g_gndDampStand > HEADROOM(GND_REF_DAMP_STAND)) g_gndDampStand = HEADROOM(GND_REF_DAMP_STAND);
    /* v821: default 70 with the 70/90 ramp below - F6, his reference - and it must equal the
       initializer. (v818..v820: 100, the original, which F4 still carries.) */
    g_crashTrimPct = (LONG)GetPrivateProfileIntA("ffb", "crash_trim_pct", 70, ini);
    if (g_crashTrimPct < 10)  g_crashTrimPct = 10;
    if (g_crashTrimPct > 200) g_crashTrimPct = 200;
    g_crashCap = (LONG)GetPrivateProfileIntA("ffb", "crash_cap", MAX_MAG, ini);
    if (g_crashCap < 2000)    g_crashCap = 2000;
    if (g_crashCap > MAX_MAG) g_crashCap = MAX_MAG;
    /* v820: the crash trim's speed ramp, true km/h. 0/0 = no ramp: the flat trim above, exactly
       as every build before v820 - so an ini without these keys changes nothing. */
    g_crashRampLo = (LONG)GetPrivateProfileIntA("ffb", "crash_ramp_lo_kmh", 70, ini);
    g_crashRampHi = (LONG)GetPrivateProfileIntA("ffb", "crash_ramp_hi_kmh", 90, ini);
    if (g_crashRampLo < 0 || g_crashRampLo > 300) g_crashRampLo = 0;
    if (g_crashRampHi < 0 || g_crashRampHi > 300) g_crashRampHi = 0;
    /* v820: default 38 - his roll reference after ours-037, and it must equal the initializer. */
    g_rollGainPct = (LONG)GetPrivateProfileIntA("ffb", "road_roll_gain_pct", 38, ini);
    if (g_rollGainPct < 0)   g_rollGainPct = 0;
    if (g_rollGainPct > 300) g_rollGainPct = 300;
    g_rollCap = (LONG)GetPrivateProfileIntA("ffb", "road_roll_cap", ROAD_REF_ROLL_CAP, ini);
    if (g_rollCap < 100)   g_rollCap = 100;
    /* v825: 4x his 3000 (12000), not 10000. The `road` slider multiplies AFTER this cap, so it
       scales it already; this is for a file that raises the cap itself. */
    if (g_rollCap > HEADROOM(ROAD_REF_ROLL_CAP)) g_rollCap = HEADROOM(ROAD_REF_ROLL_CAP);
    g_rollKeys = (LONG)GetPrivateProfileIntA("ffb", "road_roll_keys", 1, ini);
    if (g_rollKeys) g_rollKeys = 1;
    g_crashKeys = (LONG)GetPrivateProfileIntA("ffb", "crash_keys", 1, ini);
    if (g_crashKeys) g_crashKeys = 1;
    /* v822: the debug range keys take F1..F5, so both banks give theirs up (see g_rangeKeys) */
    g_rangeKeys = (LONG)GetPrivateProfileIntA("ffb", "range_keys", 0, ini);
    if (g_rangeKeys) { g_rangeKeys = 1; g_rollKeys = 0; g_crashKeys = 0; }
    /* v814: the two banks' keys win over the file for the rest of the session - see
       g_rollKeyUsed. Without this the once-a-second re-read undoes his choice one second after
       he makes it, and the log still reports the state he chose. */
    if (g_rollKeyUsed) {
        g_rollGainPct = g_rollBankGain[g_rollPresetCur - 1];
        g_rollCap     = g_rollBankCap[g_rollPresetCur - 1];
    }
    if (g_crashKeyUsed) {
        g_crashTrimPct = g_crashBankTrim[g_crashPresetCur - 1];
        g_crashCap     = g_crashBankCap[g_crashPresetCur - 1];
        g_crashRampLo  = g_crashBankRampLo[g_crashPresetCur - 1];
        g_crashRampHi  = g_crashBankRampHi[g_crashPresetCur - 1];
    }
    g_gndDampKeys = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_keys", 0, ini);
    if (g_gndDampKeys) g_gndDampKeys = 1;
    g_gndDampMoveAbs = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_move_abs", 0, ini);
    if (g_gndDampMoveAbs < 0)     g_gndDampMoveAbs = 0;
    if (g_gndDampMoveAbs > 20000) g_gndDampMoveAbs = 20000;
    g_gndCentreK  = (LONG)GetPrivateProfileIntA("ffb", "ground_centre_k", 0, ini);
    if (g_gndCentreK < 0)   g_gndCentreK = 0;
    if (g_gndCentreK > 400) g_gndCentreK = 400;
    if (g_gndDampSlip < 0)   g_gndDampSlip = 0;
    if (g_gndDampSlip > 100) g_gndDampSlip = 100;
    g_gndBend = (LONG)GetPrivateProfileIntA("ffb", "ground_curve_bend", 100, ini);
    if (g_gndBend < 100) g_gndBend = 100;
    if (g_gndBend > 200) g_gndBend = 200;
    /* 0 and 100, not GTA MT's 50 and 200: the aligning torque does not wait for 7 km/h, and the
       measurement that settled it is in this file above g_gndCasterK. */
    g_gndVmin    = (LONG)GetPrivateProfileIntA("ffb", "ground_v_min_cms", 0, ini);
    g_gndVfull   = (LONG)GetPrivateProfileIntA("ffb", "ground_v_full_cms", 100, ini);
    g_gndDetailK = (LONG)GetPrivateProfileIntA("ffb", "ground_detail_k", GND_REF_DETAIL_K, ini);
    /* theirs: DetailLim 5000 and DetailMAW 3. The window is bounded at GND_MAW_MAX because it is
       a fixed array in the FFB thread's frame, which has no room to grow. */
    g_gndDetailLim = (LONG)GetPrivateProfileIntA("ffb", "ground_detail_lim", GND_REF_DETAIL_LIM, ini);
    if (g_gndDetailLim < 0)     g_gndDetailLim = 0;
    /* v825: 4x his 5000 (20000), not 10000 - the texture row writes gain and limit together, and
       a limit that stopped at 2x would turn the top half of that slider into a cut. */
    if (g_gndDetailLim > HEADROOM(GND_REF_DETAIL_LIM)) g_gndDetailLim = HEADROOM(GND_REF_DETAIL_LIM);
    g_gndDetailMaw = (LONG)GetPrivateProfileIntA("ffb", "ground_detail_maw", 3, ini);
    if (g_gndDetailMaw < 1)           g_gndDetailMaw = 1;
    if (g_gndDetailMaw > GND_MAW_MAX) g_gndDetailMaw = GND_MAW_MAX;
    /* The spike row's threshold, in force units of step between two ticks. 1500 is about a
       seventh of the wheel's full scale and well above the model's ordinary breathing on a city
       street; it is deliberately not derived from ground_slew, because the question is what the
       wheel DID, not what it was allowed to do. */
    g_gndSpike = (LONG)GetPrivateProfileIntA("ffb", "ground_spike", 1500, ini);
    if (g_gndSpike < 0)     g_gndSpike = 0;
    if (g_gndSpike > 10000) g_gndSpike = 10000;
    g_gndSignBand = (LONG)GetPrivateProfileIntA("ffb", "ground_sign_band", 30, ini);
    if (g_gndSignBand < 0)   g_gndSignBand = 0;
    if (g_gndSignBand > 500) g_gndSignBand = 500;
    /* v7.96: the speed curve's six anchors, re-read every second like everything else here. */
    {
        static const LONG dflV[GND_SPD_PTS] = { 10, 20, 30, 50, 60, 70, 80, 110 };
        /* v817: the half - must equal g_gndSpdM's initializer, or a silent ini gives a different
           wheel from the one the module starts with. */
        static const LONG dflM[GND_SPD_PTS] = { 22, 34, 42, 60, 80, 100, 122, 190 };
        /* "ground_spd1_kmh" / "ground_spd1_pct", built by hand: this module links -nostdlib and
           has no sprintf, and the digit is the only part that moves. */
        char nm[20] = "ground_spd0_kmh";
        int i;
        g_gndSpdOn = (LONG)GetPrivateProfileIntA("ffb", "ground_spd_curve", 1, ini);
        for (i = 0; i < GND_SPD_PTS; i++) {
            nm[10] = (char)('1' + i);
            nm[12] = 'k'; nm[13] = 'm'; nm[14] = 'h'; nm[15] = 0;
            g_gndSpdV[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflV[i], ini);
            nm[12] = 'p'; nm[13] = 'c'; nm[14] = 't'; nm[15] = 0;
            g_gndSpdM[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflM[i], ini);
            if (g_gndSpdV[i] < 0)    g_gndSpdV[i] = 0;
            if (g_gndSpdV[i] > 400)  g_gndSpdV[i] = 400;
            if (g_gndSpdM[i] < 0)    g_gndSpdM[i] = 0;
            if (g_gndSpdM[i] > 400)  g_gndSpdM[i] = 400;
        }
    }
    g_gndStaleTicks = (LONG)GetPrivateProfileIntA("ffb", "ground_stale_ticks", 20, ini);
    if (g_gndStaleTicks < 0)    g_gndStaleTicks = 0;
    if (g_gndStaleTicks > 1000) g_gndStaleTicks = 1000;
    /* NOT ground keys, and they are read here because this is the one function that re-reads the
       ini every second - so a wheel that has been taken away can be given a different recovery
       policy without a rebuild, which is the situation where a rebuild is least convenient. */
    g_recoverMs    = (LONG)GetPrivateProfileIntA("ffb", "ffb_recover_ms", 2000, ini);
    if (g_recoverMs < 0)      g_recoverMs = 0;
    if (g_recoverMs > 60000)  g_recoverMs = 60000;
    g_recoverAfter = (LONG)GetPrivateProfileIntA("ffb", "ffb_recover_after", 8, ini);
    if (g_recoverAfter < 1)    g_recoverAfter = 1;
    if (g_recoverAfter > 1000) g_recoverAfter = 1000;
    g_gndTauMs   = (LONG)GetPrivateProfileIntA("ffb", "ground_tau_ms", 40, ini);
    g_gndSlew    = (LONG)GetPrivateProfileIntA("ffb", "ground_slew", 40000, ini);
    g_gndInvert  = (LONG)GetPrivateProfileIntA("ffb", "ground_invert", 0, ini);
    g_gndLog     = (LONG)GetPrivateProfileIntA("ffb", "ground_log", 1, ini);
    /* v7.8: the model's own settings. `ground_sat_k` rises from 4000 because SAT now replaces the
       spring's 4500 plus its 2250 highway term - it is the only centring force left. */
    g_gndAlphaMode    = (LONG)GetPrivateProfileIntA("ffb", "ground_alpha_mode", 0, ini);
    g_gndAyGain       = (LONG)GetPrivateProfileIntA("ffb", "ground_ay_gain", 100, ini);
    g_gndFrontAxleCm  = (LONG)GetPrivateProfileIntA("ffb", "ground_front_axle_cm", 130, ini);
    g_gndDampMovePct  = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_move_pct", 50, ini);
    /* v7.90: 500 -> 250 on request, 2026-09-13: the damper when the wheels are in the air... can be
       halved again. If it is currently 500, set it to around 250. The floor exists
       only so the wheel does not spin freely in the air; he has now judged 500 twice. */
    g_gndDampAir      = (LONG)GetPrivateProfileIntA("ffb", "ground_damp_air", 250, ini);
    if (g_gndDampAir < 0)     g_gndDampAir = 0;
    if (g_gndDampAir > 20000) g_gndDampAir = 20000;
    g_gndLong         = (LONG)GetPrivateProfileIntA("ffb", "ground_long", 1, ini);
    g_gndLongFloor    = (LONG)GetPrivateProfileIntA("ffb", "ground_long_floor", 20, ini);
    g_gndLongTauMs    = (LONG)GetPrivateProfileIntA("ffb", "ground_long_tau_ms", 120, ini);
    g_gndSlipSrc      = (LONG)GetPrivateProfileIntA("ffb", "ground_slip_src", 0, ini);
    if (g_gndLongFloor < 0)    g_gndLongFloor = 0;
    if (g_gndLongFloor > 100)  g_gndLongFloor = 100;
    if (g_gndLongTauMs < 0)    g_gndLongTauMs = 0;
    if (g_gndLongTauMs > 2000) g_gndLongTauMs = 2000;
    if (g_gndSlipSrc < 0 || g_gndSlipSrc > 3) g_gndSlipSrc = 0;
    g_gndCentreMs     = (LONG)GetPrivateProfileIntA("ffb", "ground_centre_ms", 0, ini);
    if (g_gndCentreMs < 0)     g_gndCentreMs = 0;
    if (g_gndCentreMs > 10000) g_gndCentreMs = 10000;
    g_gndHoldCentre   = (LONG)GetPrivateProfileIntA("ffb", "ground_hold_centre", 0, ini);
    g_gndSelfTest     = (LONG)GetPrivateProfileIntA("ffb", "ground_selftest", 0, ini);
    g_gndSelfMag      = (LONG)GetPrivateProfileIntA("ffb", "ground_selftest_mag", 5000, ini);
    if (g_gndSelfTest < 0)    g_gndSelfTest = 0;
    if (g_gndSelfTest > 10)   g_gndSelfTest = 10;
    if (g_gndSelfMag < 0)     g_gndSelfMag = 0;
    if (g_gndSelfMag > 10000) g_gndSelfMag = 10000;
    g_gndCasterK      = (LONG)GetPrivateProfileIntA("ffb", "ground_caster_k", GND_REF_CASTER_K, ini);
    g_gndCasterExp    = (LONG)GetPrivateProfileIntA("ffb", "ground_caster_exp", 200, ini);
    if (g_gndCasterExp < 100) g_gndCasterExp = 100;
    if (g_gndCasterExp > 300) g_gndCasterExp = 300;
    g_gndCasterD      = (LONG)GetPrivateProfileIntA("ffb", "ground_caster_d", 600, ini);
    if (g_gndCasterD < 0)     g_gndCasterD = 0;
    if (g_gndCasterD > 10000) g_gndCasterD = 10000;
    g_gndCasterVcms   = (LONG)GetPrivateProfileIntA("ffb", "ground_caster_v_cms", 40, ini);
    if (g_gndCasterK < 0)       g_gndCasterK = 0;
    /* v825: 10000 already admits 4x his 2400 (9600), the most the weight row writes. */
    if (g_gndCasterK > 10000)   g_gndCasterK = 10000;
    if (g_gndCasterVcms < 5)    g_gndCasterVcms = 5;
    if (g_gndCasterVcms > 1000) g_gndCasterVcms = 1000;
    if (g_gndAlphaMode < 0 || g_gndAlphaMode > 2) g_gndAlphaMode = 0;
    if (g_gndAyGain < 0)        g_gndAyGain = 0;
    if (g_gndAyGain > 400)      g_gndAyGain = 400;
    if (g_gndFrontAxleCm < 30)  g_gndFrontAxleCm = 30;
    if (g_gndFrontAxleCm > 400) g_gndFrontAxleCm = 400;
    if (g_gndDampMovePct < 0)   g_gndDampMovePct = 0;
    /* v825: 200 is exactly 4x his 50 - the driving damper row at 400% writes 200 and it passes. */
    if (g_gndDampMovePct > 200) g_gndDampMovePct = 200;
    /* v7.94: G and N, not I and O. The letter ladder took I and O, and a key that selects a
       preset AND toggles the whole channel is a control nobody can aim. G and N are both in the
       set the game's own defaults leave alone (docs\KEY-MAP.md).
       v825: 0 = no key - see HotkeyVk. */
    g_gndKeyOn   = HotkeyVk((LONG)GetPrivateProfileIntA("ffb", "ground_key_on",  0x47, ini), 0x47);
    g_gndKeyOff  = HotkeyVk((LONG)GetPrivateProfileIntA("ffb", "ground_key_off", 0x4E, ini), 0x4E);
    g_gndAckMag  = (LONG)GetPrivateProfileIntA("ffb", "ground_ack", 4000, ini);
    /* A nonsense VK is not a control, and two keys that are the SAME key is a toggle again -
       which is the thing he just rejected. Both fall back rather than being half-honoured.
       Two keys of 0 are not "the same key": they are no key twice, and they stay 0. */
    if (g_gndKeyOn != 0 && g_gndKeyOn == g_gndKeyOff) { g_gndKeyOn = 0x47; g_gndKeyOff = 0x4E; }
    if (g_gndAckMag < 0) g_gndAckMag = 0;
    if (g_gndAckMag > 8000) g_gndAckMag = 8000;
    /* ---- the preset bank, read every second so a preset can be retuned while he drives ---- */
    {
        static const LONG dflKey[GND_PRESETS]  = { 0x70, 0x71, 0x72, 0x73, 0x74,
                                                   0x55, 0x49, 0x4F, 0x50 };
        static const LONG dflOn[GND_PRESETS]   = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        static const LONG dflOpt[GND_PRESETS]  = { 9, 9, 9, 9, 4, 9, 9, 9, 9 };
        static const LONG dflFall[GND_PRESETS] = { 0,  0,  0,  0,  0, 0,  0,  0,  0  };
        static const LONG dflPost[GND_PRESETS] = { 170, 170, 170, 170, 300, 170, 170, 170, 170 };
        static const LONG dflBend[GND_PRESETS] = { 100, 100, 100, 100, 130, 100, 100, 100, 100 };
        static const LONG dflSatK[GND_PRESETS] = { 7875, 7875, 7875, 7875, 5000,
                                                   7875, 7875, 7875, 7875 };
        static const LONG dflDamp[GND_PRESETS]  = { 50, 50, 50, 50, 50, 50, 50, 50, 50 };
        static const LONG dflDSlip[GND_PRESETS] = { 78, 78, 78, 78, 0, 78, 78, 78, 78 };
        static const LONG dflCK[GND_PRESETS]    = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        static const LONG dflRet[GND_PRESETS]   = { 875, 875, 875, 875, 0, 0, 0, 0, 0 };
        static const LONG dflScale[GND_PRESETS] = { 100, 100, 100, 100, 100, 100, 100, 100, 100 };
        static const LONG dflIsCur[GND_PRESETS] = { 0, 0, 0, 0, 0, 1, 1, 1, 0 };
        char nm[40];
        int i;
        for (i = 0; i < GND_PRESETS; i++) {
            PresetKeyName(nm, i, "_key");
            /* A key of 0 must never reach GetAsyncKeyState - that is how a retired control comes
               back as a phantom press (docs\KEY-MAP.md). v825: and a 0 is NO KEY, not the default
               key - see HotkeyVk. PollPresetKeys skips it, so it never reaches the keyboard. */
            g_presetKey[i]  = HotkeyVk((LONG)GetPrivateProfileIntA("ffb", nm, dflKey[i], ini),
                                       dflKey[i]);
            PresetKeyName(nm, i, "_ground");
            g_presetOn[i]   = (LONG)GetPrivateProfileIntA("ffb", nm, dflOn[i], ini);
            PresetKeyName(nm, i, "_opt_deg");
            g_presetOpt[i]  = (LONG)GetPrivateProfileIntA("ffb", nm, dflOpt[i], ini);
            PresetKeyName(nm, i, "_falloff_pct");
            g_presetFall[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflFall[i], ini);
            PresetKeyName(nm, i, "_post_ratio_pct");
            g_presetPost[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflPost[i], ini);
            PresetKeyName(nm, i, "_curve_bend");
            g_presetBend[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflBend[i], ini);
            PresetKeyName(nm, i, "_sat_k");
            g_presetSatK[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflSatK[i], ini);
            PresetKeyName(nm, i, "_damp_pct");
            g_presetDamp[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflDamp[i], ini);
            PresetKeyName(nm, i, "_damp_slip");
            g_presetDSlip[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflDSlip[i], ini);
            PresetKeyName(nm, i, "_centre_k");
            g_presetCK[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflCK[i], ini);
            PresetKeyName(nm, i, "_is_curve");
            g_presetIsCurve[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflIsCur[i], ini);
            {   /* the curve's own six anchors: preset6_spd1_pct .. preset7_spd6_pct */
                char cn[24] = "preset0_spd0_pct";
                int q;
                cn[6] = (char)('1' + i);
                for (q = 0; q < GND_SPD_PTS; q++) {
                    cn[11] = (char)('1' + q);
                    g_presetCurve[i][q] = (LONG)GetPrivateProfileIntA("ffb", cn,
                                              (DWORD)g_presetCurve[i][q], ini);
                    if (g_presetCurve[i][q] < 0)   g_presetCurve[i][q] = 0;
                    if (g_presetCurve[i][q] > 400) g_presetCurve[i][q] = 400;
                }
            }
            PresetKeyName(nm, i, "_spd_scale");
            /* Percent, so it is never negative and the UINT trap that made the arming tool refuse
               v7.97 cannot come back ([[truck-damper-sliders]]). 100 = the approved curve. */
            g_presetScale[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflScale[i], ini);
            if (g_presetScale[i] < 20)  g_presetScale[i] = 20;
            if (g_presetScale[i] > 300) g_presetScale[i] = 300;
            PresetKeyName(nm, i, "_return_k");
            g_presetRet[i] = (LONG)GetPrivateProfileIntA("ffb", nm, dflRet[i], ini);
            if (g_presetRet[i] < 0)     g_presetRet[i] = 0;
            if (g_presetRet[i] > 4000)  g_presetRet[i] = 4000;
        }
        /* Two keys that are the same key are one key nobody can aim: fall the pair back rather
           than half-honour them, exactly as ground_key_on / _off do above. v825: two zeros are no
           key twice, not one key twice - a player's ini carries nine of them. */
        for (i = 1; i < GND_PRESETS; i++) {
            int j;
            for (j = 0; j < i; j++)
                if (g_presetKey[i] != 0 && g_presetKey[i] == g_presetKey[j]) { g_presetKey[i] = dflKey[i]; g_presetKey[j] = dflKey[j]; }
        }
    }
    /* A preset he has selected outranks the plain ground_* keys in the same file, for the same
       reason the override line below exists. Before the override line, so that pressing O still
       silences the channel after a preset press. */
    if (g_presetCur >= 1) PresetApply((int)g_presetCur - 1);
    /* The key wins over the file for the rest of the session. Without this the ini re-read one
       second later would silently undo his A/B switch, and the log would still say "on". */
    if (g_gndOverride >= 0) g_gndOn = g_gndOverride;
    /* v811: and the same for the damper ladders - see g_dampKeyUsed. */
    if (g_dampKeyUsed) {
        g_gndDampGripMode = g_dampKeepMode;
        g_gndDampMoveAbs  = g_dampKeepAbs;
        g_gndDampStand    = g_dampKeepStand;
    }
    if (g_gndOptDeg < 1) g_gndOptDeg = 1;
    if (g_gndVfull <= g_gndVmin) g_gndVfull = g_gndVmin + 1;
    if (g_gndFalloff < 0)   g_gndFalloff = 0;
    if (g_gndFalloff > 100) g_gndFalloff = 100;
}

/* ---- the tyre load, per axle, from the fields the game's own FFB multiplies by ---------------- */
typedef struct {
    float loadAll;        /* the stock game's L - every wheel over the car's weight            */
    float loadFront;      /* GTA MT's weightFactor input - the FRONT axle over its design load */
    float suspFL, suspFR; /* front suspension lengths, for the left-right detail differential   */
    DWORD planted;        /* one bit per wheel, clear = in the air                              */
    int   n;
    int   haveSusp;
    /* ---- THE ENGINE'S OWN TYRE STATE, found in the decompilation 2026-09-13 ------------------
       Every one of these is written by the tyre solver at 0x004E2090 every tick, and they are
       the quantities GTA MT gets handed by GTA's physics and we believed we had to estimate. */
    float sinSlipF;       /* sin of the front axle's slip angle, SIGNED BY US - wheel+0x108     */
    float deltaF;         /* the steering angle, from the wheel's own forward vector vs the body */
    DWORD flagsF;         /* the steered wheels' state bits OR-ed: 0x10 squeal, 0x20 locked,
                             0x80 wheelspin - cleared and rewritten by the solver every tick    */
    int   nSteer;         /* how many steered wheels answered; 0 = this car has none we can read */
} GNDSTATE;

static int GroundRead(DWORD veh, GNDSTATE *g)
{
    DWORD arr = 0, w, flags;
    int n = 0, i, haveAxes;
    float sum = 0.0f, front = 0.0f, mass = 0.0f, grav = 0.0f, nf, mu, len, denom;
    float fwd[3], rgt[3];
    g->loadAll = g->loadFront = 0.0f;
    g->planted = 0; g->n = 0; g->haveSusp = 0;
    g->sinSlipF = g->deltaF = 0.0f; g->flagsF = 0; g->nSteer = 0;
    if (!veh) return 0;
    if (!SafeReadD(veh + 0x534u, (DWORD *)&n) || n <= 0 || n > 8)      return 0;
    if (!SafeReadD(veh + 0xCA8u, &arr) || !Sane(arr))                  return 0;
    if (!SafeReadF(veh + 0x664u, &mass) || mass <= 0.0f)               return 0;
    if (!SafeReadF(veh + 0x50Cu, &grav) || grav == 0.0f)               return 0;
    haveAxes = SafeReadF(veh + 0x0CACu, &fwd[0]) && SafeReadF(veh + 0x0CB0u, &fwd[1]) &&
               SafeReadF(veh + 0x0CB4u, &fwd[2]) && SafeReadF(veh + 0x0CB8u, &rgt[0]) &&
               SafeReadF(veh + 0x0CBCu, &rgt[1]) && SafeReadF(veh + 0x0CC0u, &rgt[2]);
    for (i = 0; i < n; i++) {
        if (!SafeReadD(arr + (DWORD)i * 4u, &w) || !Sane(w))           return 0;
        if (!SafeReadD(w + 0x120u, &flags))                            return 0;
        if (flags & 0x40000000u) continue;         /* destroyed - the game skips it, so do we */
        if (!SafeReadF(w + 0x90u, &nf) || !SafeReadF(w + 0xDCu, &mu))  return 0;
        if (mu <= 0.0f || mu > 100.0f)                                 return 0;
        if (flags & 0x8u) g->planted |= (DWORD)1 << i;
        sum += mu * nf;
        if (i < 2) front += mu * nf;               /* L,R per axle: 0 and 1 are the front pair */
        if (i < 2 && SafeReadF(w + 0x94u, &len)) { /* previous suspension length, per wheel   */
            if (i == 0) g->suspFL = len; else g->suspFR = len;
            g->haveSusp = 1;
        }
        /* ---- THE STEERED WHEEL'S OWN SLIP, as the engine measured it this tick --------------
           Flag 0x1 is set by the car loader for a steered wheel, so this asks the game which
           wheels steer instead of assuming the first two. `wheel+0x108` is sin(slip angle),
           written from the angle between the contact-point velocity and the wheel's forward
           vector - the same construction GTA MT's CalculateSlipInfo uses, only already done.
           It is stored UNSIGNED, so the sign comes from which way the contact patch is sliding
           across the car. */
        if ((flags & 0x1u) && haveAxes) {
            float wf[3], cv[3], s = 0.0f, lat, cosd, sind;
            if (SafeReadF(w + 0x108u, &s) &&
                SafeReadF(w + 0x58u, &wf[0]) && SafeReadF(w + 0x5Cu, &wf[1]) &&
                SafeReadF(w + 0x60u, &wf[2]) &&
                SafeReadF(w + 0x4Cu, &cv[0]) && SafeReadF(w + 0x50u, &cv[1]) &&
                SafeReadF(w + 0x54u, &cv[2])) {
                if (s < 0.0f) s = -s;
                if (s > 1.0f) s = 1.0f;
                lat  = cv[0] * rgt[0] + cv[1] * rgt[1] + cv[2] * rgt[2];
                if (lat < 0.0f) s = -s;
                /* The steering angle, measured rather than asked for: the wheel's forward vector
                   is already steered by the time the solver reads it, so its angle against the
                   body's forward axis IS the steering angle. This replaces `wheel+0x114`, which
                   this project called the steering angle and which is nothing of the kind - it is
                   the wheel's SPIN angle, integrated as distance over radius and wrapped at 2 pi
                   (FUN_004de020). It moves whenever the car rolls, so the old "does this field
                   move" liveness test would have said yes and fed a rolling angle into a slip
                   estimate. */
                cosd = wf[0] * fwd[0] + wf[1] * fwd[1] + wf[2] * fwd[2];
                sind = wf[0] * rgt[0] + wf[1] * rgt[1] + wf[2] * rgt[2];
                g->sinSlipF += s;
                g->deltaF   += (cosd > 0.25f) ? (sind / cosd) : sind;
                g->flagsF   |= flags;
                g->nSteer++;
            }
        }
    }
    if (g->nSteer > 1) {
        g->sinSlipF /= (float)g->nSteer;
        g->deltaF   /= (float)g->nSteer;
    }
    if (sum   < 0.0f) sum   = 0.0f;
    if (front < 0.0f) front = 0.0f;
    denom = mass * grav * 1.1f;
    if (denom <= 0.0f) return 0;
    g->loadAll   = sum / denom;
    g->loadFront = front / (denom * 0.5f);         /* the front axle's own share of the weight */
    if (g->loadAll   > 1.0f) g->loadAll   = 1.0f;
    if (g->loadFront > 1.0f) g->loadFront = 1.0f;
    g->n = n;
    return 1;
}

/* ================= THE HUNT: where IS the vehicle object, if the detour is dead ================
 *
 * Run 016 and the autotest that reproduced it both say the same thing: the detour on
 * `0x0042F60C` is installed and never runs. So the vehicle pointer has to come from somewhere
 * else, and rather than guess an offset we ask the running game.
 *
 * The test is a SIGNATURE, not an address: a vehicle is an object whose wheel count at +0x534 is
 * 1..8, whose wheel array at +0xCA8 is a readable pointer, whose mass at +0x664 is positive and
 * plausible, whose gravity at +0x50C is near 9.8, and whose first wheel has a readable normal
 * force and grip. That is five independent fields agreeing - a random pointer does not pass it.
 *
 * WHY A SCAN AND NOT A LOOKUP. Run 014 was spent on an offset taken from the listing at 90%
 * confidence, and it was the WRONG OBJECT: the chain we have always used reaches the player
 * actor. A scan reports what is actually there, and it reports NOTHING when nothing is there,
 * which is the answer we most need to be able to distinguish. */
static int VehSigOK(DWORD p, int *outN, float *outMass)
{
    DWORD arr = 0, w = 0;
    int n = 0;
    float mass = 0.0f, grav = 0.0f, nf = 0.0f, mu = 0.0f;
    if (!Sane(p)) return 0;
    if (!SafeReadD(p + 0x534u, (DWORD *)&n) || n < 1 || n > 8)      return 0;
    if (!SafeReadD(p + 0xCA8u, &arr) || !Sane(arr))                 return 0;
    if (!SafeReadF(p + 0x664u, &mass) || mass < 50.0f || mass > 50000.0f) return 0;
    if (!SafeReadF(p + 0x50Cu, &grav) || grav < 5.0f  || grav > 15.0f)    return 0;
    if (!SafeReadD(arr, &w) || !Sane(w))                            return 0;
    if (!SafeReadF(w + 0x90u, &nf) || !SafeReadF(w + 0xDCu, &mu))   return 0;
    if (mu <= 0.0f || mu > 100.0f)                                  return 0;
    if (outN)    *outN = n;
    if (outMass) *outMass = mass;
    return 1;
}

/* Walk the player actor for a pointer to something that passes the signature, and log where it
   was found. 0x6D carries (offset, pointer, wheel count, mass x100); 0x6E is the "nothing found"
   row and it is written EVERY time, because "no candidate" and "never looked" are different
   claims and only one of them is a finding. */
static DWORD g_vehFound = 0;
/* WHERE the pointer we hold came from, so it can be re-read rather than re-guessed:
   the offset inside the actor, 0xFFFFFFFF meaning "the actor itself", and the actor it was
   found in. Both written whenever g_vehFound is adopted below. */
static DWORD g_vehOff  = 0xFFFFFFFFu;
static DWORD g_vehBase = 0;
static void VehHunt(void)
{
    DWORD base = GetCarPtr();     /* the PLAYER ACTOR - the object our forces have always used */
    DWORD off, p;
    int n = 0, hits = 0;
    float mass = 0.0f;

    /* ---- HAS THE GAME SWAPPED THE CAR UNDER US? -----------------------------------------
     * Measured twice: ours-025 ended with 55 s of exactly zero force after a level restart, and
     * ours-034 carries three more windows of 4.0-4.5 s, each one frozen bit for bit at 49.15,
     * 30.82 and 4.28 km/h ([[the-channel-keeps-reading-a-dead-car]]).
     *
     * The cause is the line below this block: it keeps the pointer as long as it still PASSES
     * the signature, and a freed object in still-mapped memory keeps its wheel count, its mass
     * and its gravity, so it passes for ever. The signature answers "is this a car", and the
     * question that needed asking is "is this THE car the game is using now".
     *
     * So ask the game. We know where we got the pointer - the offset inside the actor, recorded
     * when it was adopted - and reading it again costs one guarded read a second. If the value
     * there has moved, the object we hold has been abandoned and the hunt below re-resolves it.
     *
     * NOT the stale flag as the trigger: a genuinely parked car is stale too, and that would be
     * a rescan storm every time he stops at a kerb.
     *
     * A FALSE POSITIVE IS CHEAP AND THAT IS DELIBERATE: if the pointer has not really changed,
     * the hunt below finds the same object again in the same place and nothing else happens. The
     * failure mode of this check is a few milliseconds, not a wrong car.
     *
     * 0x37: a = the pointer we were holding, b = what sits there now (0 = the actor went away),
     *       c = the offset it was read from, or the new actor when the actor itself changed.
     *
     * 0x37 AND NOT 0x33, which is what v815 shipped with. 0x33 is the speed-curve row, written
     * EVERY SECOND since v7.96 - so the one question this row exists to answer ("did the check
     * fire across the restart") could never have come back no. The card for ours-035 said
     * "0x33 present = the swap was noticed", and it would have been present in every second of
     * every drive. Caught 2026-09-30 before the drive, by listing every producer of each code
     * rather than trusting the number to be free. */
    if (g_gndRevalidate && g_vehFound) {
        DWORD now = 0;
        int   drop = 0;
        if (base != g_vehBase) {
            Log(K_INIT, 0x37, g_vehFound, 0, base);
            drop = 1;
        } else if (g_vehOff == 0xFFFFFFFFu) {
            if (base != g_vehFound) { Log(K_INIT, 0x37, g_vehFound, base, g_vehOff); drop = 1; }
        } else if (!SafeReadD(base + g_vehOff, &now) || now != g_vehFound) {
            Log(K_INIT, 0x37, g_vehFound, now, g_vehOff);
            drop = 1;
        }
        if (drop) { g_vehFound = 0; g_vehBase = 0; g_vehOff = 0xFFFFFFFFu; }
    }

    if (g_vehFound && VehSigOK(g_vehFound, &n, &mass)) return;   /* still good, nothing to do */
    g_vehFound = 0;
    if (!base) { Log(K_INIT, 0x6E, 0, 0, 0); return; }
    /* the actor itself first - it costs nothing and settles the "two objects" question outright */
    if (VehSigOK(base, &n, &mass)) {
        g_vehFound = base; g_vehBase = base; g_vehOff = 0xFFFFFFFFu;
        Log(K_INIT, 0x6D, 0xFFFFFFFFu, base, (DWORD)((n << 24) | (DWORD)(LONG)(mass * 100.0f)));
        return;
    }
    for (off = 0; off < 0x4000u; off += 4u) {
        if (!SafeReadD(base + off, &p)) continue;
        if (!VehSigOK(p, &n, &mass)) continue;
        if (hits == 0) { g_vehFound = p; g_vehBase = base; g_vehOff = off; }
        Log(K_INIT, 0x6D, off, p, (DWORD)((n << 24) | (DWORD)(LONG)(mass * 100.0f)));
        if (++hits >= 4) break;
    }
    if (!hits) Log(K_INIT, 0x6E, base, 0, 0);
}

/* ---- ARE WE READING THE RIGHT PLACES? The model's inputs, logged raw ------------------------
 *
 * His question, 2026-09-13: what of the GTA model do we not have. This answers the half that a
 * listing cannot: whether the fields we believe in hold sane numbers in a live game.
 *
 * 0x6F pairs the two candidate vehicle pointers - the one our signature scan found at actor+0x58
 * and the one the listing names at actor+0xE4 - with the steering angle and the steering lock.
 * If those two pointers are equal, the scan and the listing agree and the question is closed.
 *
 * 0x1A carries the tyre-load arithmetic UNCLAMPED, because loadAll read a flat 1.000 all drive:
 * a value pinned at its ceiling cannot express partial unloading, which is half of the empty
 * wheel. Either the numerator is bigger than the listing implies or the denominator is wrong,
 * and only the raw numbers say which. */
static void LogModelInputs(void)
{
    DWORD actor = GetCarPtr(), v58 = 0, ve4 = 0, arr = 0, w = 0;
    float steer = 0.0f, lock = 0.0f, mass = 0.0f, grav = 0.0f, nf = 0.0f, mu = 0.0f, sum = 0.0f;
    int n = 0, i;
    if (!actor) return;
    SafeReadD(actor + 0x58u, &v58);
    SafeReadD(actor + 0xE4u, &ve4);
    SafeReadF(actor + STEER_OFF, &steer);
    if (Sane(ve4)) SafeReadF(ve4 + 0x680u, &lock);
    else if (Sane(v58)) SafeReadF(v58 + 0x680u, &lock);
    Log(K_INIT, 0x6F, v58, ve4,
        (DWORD)(((LONG)(steer * 1000.0f) & 0xFFFF) | (((LONG)(lock * 1000.0f) & 0xFFFF) << 16)));

    { DWORD veh = Sane(ve4) ? ve4 : (Sane(v58) ? v58 : 0);
      if (!veh) return;
      if (!SafeReadD(veh + 0x534u, (DWORD *)&n) || n < 1 || n > 8) return;
      if (!SafeReadD(veh + 0xCA8u, &arr) || !Sane(arr)) return;
      SafeReadF(veh + 0x664u, &mass);
      SafeReadF(veh + 0x50Cu, &grav);
      { float nfs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (i = 0; i < n; i++) {
            if (!SafeReadD(arr + (DWORD)i * 4u, &w) || !Sane(w)) continue;
            if (SafeReadF(w + 0x90u, &nf) && SafeReadF(w + 0xDCu, &mu)) {
                sum += mu * nf;
                if (i < 4) nfs[i] = nf;
            }
        }
        /* b=sum of mu*nf, c=mass x10, d=gravity x100 - the three numbers the clamp is built
           out of, none of them clamped here. The per-wheel forces follow on 0x1B. */
        Log(K_INIT, 0x1A, (DWORD)(LONG)sum, (DWORD)(LONG)(mass * 10.0f),
            (DWORD)(LONG)(grav * 100.0f));
        Log(K_INIT, 0x1B, (DWORD)(LONG)nfs[0], (DWORD)(LONG)nfs[1],
            (DWORD)(LONG)((nfs[2] + nfs[3]) * 0.5f));
      }
    }
}

/* ---- the slip angle, ours rather than theirs -------------------------------------------------
   No atan2: this module links -nostdlib and a libm call here would not resolve. The tangent
   approximation is exact enough for the range that matters and the result is clamped anyway -
   beyond about 35 degrees of slip the wheel is already at the ceiling. */
static int GroundSlip(DWORD veh, float *outSlip, float *outSpeed)
{
    float v[3], f[3], r[3], len, dotF, dotR;
    if (!veh) return 0;
    if (!SafeReadF(veh + 0x2000u, &v[0]) || !SafeReadF(veh + 0x2004u, &v[1]) ||
        !SafeReadF(veh + 0x2008u, &v[2])) return 0;
    if (!SafeReadF(veh + 0x0CACu, &f[0]) || !SafeReadF(veh + 0x0CB0u, &f[1]) ||
        !SafeReadF(veh + 0x0CB4u, &f[2])) return 0;
    if (!SafeReadF(veh + 0x0CB8u, &r[0]) || !SafeReadF(veh + 0x0CBCu, &r[1]) ||
        !SafeReadF(veh + 0x0CC0u, &r[2])) return 0;
    len = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    if (len < 0.0001f) { *outSlip = 0.0f; *outSpeed = 0.0f; return 1; }
    len = Sqrtf(len);
    dotF = (v[0]*f[0] + v[1]*f[1] + v[2]*f[2]) / len;
    dotR = (v[0]*r[0] + v[1]*r[1] + v[2]*r[2]) / len;
    if (dotF < 0.0f) dotF = -dotF;                 /* reversing is not a 180-degree slide */
    if (dotF < 0.2f) dotF = 0.2f;
    *outSlip  = dotR / dotF;                       /* tan(slip), signed by which side it slides */
    *outSpeed = len;
    return 1;
}

/* ---- THE FRONT AXLE'S SLIP ANGLE - the defect run 016 found -----------------------------------
 *
 * The complaint: as for sustained corners, they still seem not to be felt. The
 * slip is the angle between the velocity and the BODY's forward axis, so in an ordinary corner it
 * is near zero and SAT said nothing. Measured, not guessed: of 467 rows in the autotest, the large
 * angles are all slides.
 *
 * A real self-aligning torque follows the FRONT TYRES' slip angle, and the textbook expression is
 *
 *     alpha_front = (vy + a * r) / vx - delta
 *
 * with `vy/vx` the body slip we already have, `a` the distance from the centre of mass to the
 * front axle, `r` the yaw rate and `delta` the front wheels' steering angle. Every term is now
 * readable in this build:
 *
 *   r      `veh+0x490..0x498` is the angular velocity VECTOR in rad/s - a real physics field,
 *          not our differentiated heading. Yaw is its component along the up axis `veh+0xCC4`.
 *   delta  `wheel+0x114` in radians, and `wheel+0x188` is non-zero only on a steerable wheel.
 *          The listing could not confirm the game WRITES it per frame from the player's input,
 *          so the code measures whether it moves and falls back when it does not.
 *   a      from the wheel mount offsets at `wheel+0x10..0x18`, with an ini override.
 *
 * THE FALLBACK IS PHYSICS TOO, not a fudge: lateral acceleration in a corner is `v * r`, the
 * lateral force on the front tyres is proportional to it, and one g of it corresponds to roughly
 * the optimal slip angle of an ordinary tyre - which is why the constant below is 0.16, the same
 * 9 degrees the tyre curve peaks at.
 */
/* Reads the yaw rate and the front wheels' steering angle. Returns 0 if the vehicle cannot be
   read at all; `haveSteer` says whether a steerable wheel was found, which is a different claim
   from the angle being non-zero. */
static int GroundYawSteer(DWORD veh, float *outYaw, float *outDelta, int *haveSteer)
{
    DWORD arr = 0, w, gate;
    int n = 0, i, nsteer = 0;
    float om[3], up[3], d = 0.0f, sum = 0.0f;
    *outYaw = 0.0f; *outDelta = 0.0f; *haveSteer = 0;
    if (!veh) return 0;
    if (!SafeReadF(veh + 0x490u, &om[0]) || !SafeReadF(veh + 0x494u, &om[1]) ||
        !SafeReadF(veh + 0x498u, &om[2])) return 0;
    if (!SafeReadF(veh + 0xCC4u, &up[0]) || !SafeReadF(veh + 0xCC8u, &up[1]) ||
        !SafeReadF(veh + 0xCCCu, &up[2])) return 0;
    *outYaw = om[0] * up[0] + om[1] * up[1] + om[2] * up[2];
    if (!SafeReadD(veh + 0x534u, (DWORD *)&n) || n < 1 || n > 8) return 1;
    if (!SafeReadD(veh + 0xCA8u, &arr) || !Sane(arr))            return 1;
    /* THE OLD READ HERE WAS WRONG TWICE OVER and is kept only as this note, 2026-09-13.
       It took `wheel+0x188` as "this wheel steers" and `wheel+0x114` as the steering angle. In the
       decompilation `+0x188` is the DAMAGE WOBBLE amplitude - zero on an undamaged car, which is
       why the field read a flat zero for 122 samples and looked dead - and `+0x114` is the wheel's
       SPIN angle, integrated as distance over radius and wrapped at 2 pi (FUN_004de020). On a
       damaged car the gate would have opened and fed a rolling angle in as steering.
       The steering angle now comes from the wheel's own forward vector against the body's, in
       GroundRead, where the wheel is read anyway. Flag 0x1 says which wheels steer. */
    for (i = 0; i < n; i++) {
        if (!SafeReadD(arr + (DWORD)i * 4u, &w) || !Sane(w)) continue;
        if (!SafeReadD(w + 0x120u, &gate) || (gate & 0x1u) == 0) continue;  /* not steered */
        d = g_gndLastDelta;                       /* measured in GroundRead, from the vectors */
        if (d < -1.5f || d > 1.5f)                          continue;   /* radians, or nonsense */
        sum += d; nsteer++;
        break;                                    /* it is already the average of both wheels */
    }
    if (nsteer) { *outDelta = sum / (float)nsteer; *haveSteer = 1; }
    return 1;
}

/* The effective slip angle SAT is built from. `mode` picks the expression; in auto it uses the
   kinematic one as soon as the steering field has been seen to move, and the lateral-acceleration
   one until then - so a dead field degrades to something that still loads the wheel in a corner
   rather than to silence. */
static float GroundAlphaEff(float slipBody, float speed, float yaw, float delta, int haveSteer)
{
    float vx = speed < 2.0f ? 2.0f : speed;          /* below walking pace the divisor explodes */
    float a  = (float)g_gndFrontAxleCm * 0.01f;
    int kinematic = (g_gndAlphaMode == 2) ||
                    (g_gndAlphaMode == 0 && haveSteer && g_gndSteerLive);
    if (kinematic) return slipBody + (a * yaw) / vx - delta;
    { float ayG = (speed * yaw) / 9.81f;
      return slipBody + ayG * 0.16f * ((float)g_gndAyGain * 0.01f); }
}

/* THE SIGN SOURCE, and it is the kinematic expression UNCONDITIONALLY - never the mode selector
   above. That distinction is the whole safety of this function: the lateral-acceleration fallback
   has no `- delta` in it, so its sign is the body's, and the body's lateral velocity is exactly
   the small noisy quantity this change exists to stop signing the force with. Using GroundAlphaEff
   here would have inverted SAT for the seconds before the steering field proves live - a runaway
   in his hands, not a subtle regression. See g_gndSignBand. */
static float GroundAlphaKin(float slipBody, float speed, float yaw, float delta)
{
    float vx = speed < 2.0f ? 2.0f : speed;
    float a  = (float)g_gndFrontAxleCm * 0.01f;
    return slipBody + (a * yaw) / vx - delta;
}

/* THE TYRE CURVE. Force rises with slip to the tyre's optimal angle and FALLS beyond it - that
   fall is the whole point: it is how a wheel tells the driver the front has let go, and no spring
   pulled toward a centre can express it. GTA MT calls this calcSlipRatio; Assetto Corsa's SAT has
   the same shape and the same reason. Below the peak it is linear; above, it decays to
   `ground_falloff_pct` of the peak at twice the optimal angle and stays there. */
/* v7.96 - THE SPEED CURVE, six anchors and straight lines between them.
   A table rather than a power law for two reasons: this module links -nostdlib and has no powf,
   and a table can be retuned WHILE HE DRIVES, one number at a time, which is how every other
   decision in this rework was actually made. Outside the ends it is flat, so a stopped car and a
   130 km/h straight both have a defined answer.
   Anchors are speed in km/h and the multiplier in percent. Sorted by construction, not by trust:
   if a pair is out of order the interpolation would run backwards, so the loop below simply takes
   the first bracket that contains the speed and falls back to the last anchor. */
static float GroundSpeedMult(float kmh)
{
    float m;
    int i;
    if (!g_gndSpdOn) return 1.0f;
    /* the letters scale the curve, never its shape - one multiply at the end, so every anchor
       and every point between them moves by the same factor */
    if (kmh <= (float)g_gndSpdV[0]) {
        m = (float)g_gndSpdM[0] * 0.01f;
    } else {
        m = (float)g_gndSpdM[GND_SPD_PTS - 1] * 0.01f;
        for (i = 1; i < GND_SPD_PTS; i++) {
            float v0 = (float)g_gndSpdV[i - 1], v1 = (float)g_gndSpdV[i];
            if (kmh <= v1) {
                float span = v1 - v0;
                float t = span > 0.01f ? (kmh - v0) / span : 0.0f;
                float m0 = (float)g_gndSpdM[i - 1] * 0.01f;
                float m1 = (float)g_gndSpdM[i] * 0.01f;
                m = m0 + (m1 - m0) * t;
                break;
            }
        }
    }
    return m * (float)g_gndSpdScale * 0.01f;
}

static float GroundSlipCurve(float aSlip, float optimal)
{
    float a, r, floorv, post, w;
    if (optimal < 0.01f) optimal = 0.01f;
    a = aSlip / optimal;
    if (a <= 1.0f && g_gndCentreK > 0) {
        /* v7.93 - THE CENTRE, and it is the knob that stops the peak and the centre dragging each
           other. Measured on ours-024: on OUR angle scale the engine's squeal flag is up in
           0.2-2.6% of rows below 20 deg, 14.5% at 20-25 and 90.7% at 25-30 - so the tyre's real
           limit is around 20 degrees and the peaks sat at 4-5, which is: tyres not squealing yet,
           but all is already here. Moving the peak out that far flattens the rise near zero, and the
           force near zero is exactly what he ranks presets by.

               curve(a) = a * (1 + k) / (1 + k * a)

           0 at 0, exactly 1 AT THE PEAK whatever k is, monotonic, and its slope at zero is
           (1 + k). So k sets the centre and `ground_opt_deg` sets where the tyre lets go, and
           neither moves the other. k = 0 is the straight line, which is what the bend block
           below still provides; the two are alternatives and centre_k wins when it is set. */
        float kk = (float)g_gndCentreK * 0.01f;
        return a * (1.0f + kk) / (1.0f + kk * a);
    }
    if (a <= 1.0f) {
        /* v7.90 - THE BEND below the peak, requested like in Assetto Corsa: with bend 100 this is the
           straight line every drive so far was measured on, and the expression is bit-identical
           to the old `return a`. With bend 200 it is a*a - light at small angles, building hard
           as the corner tightens. In between, a blend of the two, which stays monotonic and is
           exact at both ends. No powf: this module links -nostdlib. */
        w = (float)(g_gndBend - 100) * 0.01f;
        if (w <= 0.0f) return a;
        if (w > 1.0f)  w = 1.0f;
        return a * ((1.0f - w) + w * a);
    }
    /* THEIRS, read out of the repository on 2026-09-13 and verified against the file myself:
           calcSlipRatio(): map(|slip|, slipOpt, postSlipOptRatio*slipOpt, 1.0, postOptSlipMin)
       with postSlipOptRatio = 2.5 and postOptSlipMin = 0.0 - so the force decays to NOTHING by
       two and a half times the optimal angle and stays there. Ours decayed only to 45% and did
       it by 2x, which is a wheel that keeps talking after the front has gone. Both numbers are
       settings now, but the defaults are theirs. */
    floorv = (float)g_gndFalloff * 0.01f;
    post   = (float)g_gndPostRatio * 0.01f;
    if (post <= 1.01f) post = 1.01f;
    if (a >= post) return floorv;
    r = 1.0f - (1.0f - floorv) * ((a - 1.0f) / (post - 1.0f));
    if (r < floorv) r = floorv;
    return r;
}

/* ---- GTA MT's longSlipMult, computed from the game's own drivetrain --------------------------
   Theirs: ratio = max(surfaceSpeed, groundSpeed) / max(min(...), 1), clamped 1..10, then
   map(ratio, 1..2 -> 1..0) with a floor of 0.2. A spinning or locked tyre cannot align itself, so
   the steering goes light - and that lightness is information, not a loss.

   The surface speed is the engine's own expression, found in three places in the decompilation
   and identical in each (FUN_004ED4F0, the squeal; FUN_004E19D0; the inverse in FUN_004DF9A0):

       surface = revs(veh+0x5B8) / ratio(veh+0x5E0 + gear*4) * circumference(veh+0x61C)

   so this is a port, not an approximation. Two gates, both the game's own: in neutral the wheels
   are not tied to the engine at all, and FUN_004DF9A0 itself refuses the comparison below a
   clutch of 0.9. A LOCKED wheel is the case the formula cannot see - the drivetrain says nothing
   about a wheel the brake has stopped - so the flag the solver sets for exactly that (0x20) takes
   it to the floor directly. */
static float GroundLongMult(DWORD veh, const GNDSTATE *g)
{
    float revs = 0.0f, ratio = 0.0f, circ = 0.0f, gnd = 0.0f, clutch = 0.0f;
    float surf, hi, lo, r, floorV = (float)g_gndLongFloor * 0.01f;
    int gear = 0;
    if (!g_gndLong) return 1.0f;
    if (g->nSteer && (g->flagsF & 0x20u)) return floorV;      /* the brake has locked a wheel */
    if (!SafeReadD(veh + 0x5CCu, (DWORD *)&gear) || gear == 0 || gear < -1 || gear > 10)
        return 1.0f;                                          /* neutral, or not a gear at all */
    if (!SafeReadF(veh + 0x64Cu, &clutch) || clutch < 0.9f)    return 1.0f;
    if (!SafeReadF(veh + 0x5B8u, &revs))                       return 1.0f;
    if (!SafeReadF(veh + 0x5E0u + (DWORD)(gear * 4), &ratio) || ratio <= 0.001f) return 1.0f;
    if (!SafeReadF(veh + 0x61Cu, &circ) || circ <= 0.01f)      return 1.0f;
    if (!SafeReadF(veh + 0x60Cu, &gnd))                        return 1.0f;
    surf = (revs / ratio) * circ;
    if (surf < 0.0f) surf = -surf;
    if (gnd  < 0.0f) gnd  = -gnd;
    hi = surf > gnd ? surf : gnd;
    lo = surf > gnd ? gnd  : surf;
    if (lo < 1.0f) lo = 1.0f;                                  /* theirs, and it caps a standstill */
    r = hi / lo;
    if (r <= 1.0f) return 1.0f;
    if (r >= 2.0f) return floorV;
    { float m = 2.0f - r;                                      /* map(r, 1..2 -> 1..0) */
      return m < floorV ? floorV : m; }
}

/* ---- v811: HOW MUCH GRIP THE DAMPER SHOULD SEE, which is not the force curve -----------------
   The force curve is zero at zero slip angle: no angle, no aligning torque. Handing that same
   number to the damper as "how much the tyre is holding" made a straight road indistinguishable
   from a slide, and `ground_damp_slip = 78` then removed the damper on both. Measured in his own
   logs above 25 km/h (0x1D against 0x1E): 610 of 2625 while driving straight, 2129 at the peak,
   756 in a real slide - a curve, where the setting asks for a step at the breakaway.

   mode 1 (shipped from v811): the tyre HOLDS below the peak and only lets go past it.
   mode 0: the v810 arithmetic, bit for bit, because the approved feel was approved with it.

   Kept as its own function so a test can reach it. Every check that existed before handed
   DamperCoeffFor a grip of 1.0 by hand, which is why 37 green checks never saw this. */
static float GroundGripForDamper(float aSlip, float optimal, float ratio, float longMult)
{
    float held;
    if (g_gndDampGripMode)
        held = (aSlip <= optimal) ? 1.0f : ratio;
    else
        held = ratio;
    if (held < 0.0f) held = 0.0f;
    if (held > 1.0f) held = 1.0f;
    return held * longMult;
}

/* x^2.5 without libm: x*x*sqrt(x). GTA MT raises the weight factor by this below its design load,
   and the exponent is what makes an unloaded front axle read as hollow rather than merely light. */
static float GroundPow25(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * Sqrtf(x);
}

/* The whole channel, one call per tick. Returns the force to ADD to the continuous term, and
   writes out the load so the caller can fade its own forces with it (his task 1). */
/* NOINLINE, and it is load-bearing rather than a style choice: inlined into the FFB thread its
   locals plus a GNDSTATE push that frame past 4 KB, clang emits a call to __alloca for the stack
   probe, and __alloca does not exist under -nostdlib. The build fails loudly, which is the good
   case - but the thread has no frame to spare, so this stays. */
__attribute__((noinline))
static float GroundForce(DWORD veh, float *outLoad)
{
    static float s_force = 0.0f, s_suspFL = 0.0f, s_suspFR = 0.0f, s_longMult = 1.0f;
    static int   s_haveSusp = 0;
    static DWORD s_last = 0;
    DWORD nowMs = GND_NOW();
    int dtMs;
    GNDSTATE g;
    /* MEASURED dt, never the nominal tick - a period asked for is not a period achieved, and a
       rate computed from the one you asked for is wrong by however much the OS missed it
       (`docs\MEASURED-TIME-NOT-NOMINAL-TIME.md`).
       A LONG TICK IS A LONG TICK, NOT A FREE STEP. This used to read
       `if (dtMs < 0 || dtMs > 100) dtMs = 0;` and the assembly below then took its `else` branch,
       which assigned the raw target straight into the sent force - past the smoothing AND past the
       slew ceiling. So the one moment the machine hitched was the one moment the wheel was allowed
       to move by any amount at all, and his drive has twelve pairs of rows that outran the 40000
       units/second ceiling, the largest of them 11269 units across 47 ms where 1880 was the limit.
       Clamping the long tick keeps the ceiling in force: at 100 ms the biggest legal step is 4000.
       A zero-length tick now HOLDS the force instead of reassigning it, which is what zero elapsed
       time means. */
    dtMs = s_last ? (int)(nowMs - s_last) : 0;
    /* THE CLOCK ONLY ADVANCES WHEN IT MOVED. With a 15.6 ms clock and a 15.4 ms tick, roughly one
       call in three reads a delta of zero; advancing `s_last` on those would throw that tick's
       time away and the next delta would understate the interval. Carrying it forward instead
       means no elapsed time is ever lost, so the slew ceiling is enforced against real seconds. */
    if (dtMs > 0 || !s_last) s_last = nowMs;
    if (dtMs < 0) dtMs = 0;
    if (dtMs > GND_DT_MAX_MS) dtMs = GND_DT_MAX_MS;
    float slip = 0.0f, spd = 0.0f, sat = 0.0f, detail = 0.0f, caster = 0.0f;
    float want, weight, velFac, step, d, a, satMult = 1.0f;
    *outLoad = 1.0f;
    g_gndCalls++;
    if (!g_gndOn) return 0.0f;
    /* NEUTRAL, NEVER SILENT. This used to leave the last load standing, which meant a stale 0.02
       from the instant a pointer went bad would have gone on gating the damper and the spring
       for the rest of the drive - and a wheel that has gone limp looks exactly like a broken
       device. A gate that does not know says 1.0. */
    if (!GroundRead(veh, &g)) { g_gndLastLoad = 1.0f; return 0.0f; }
    g_gndReads++;
    g_gndLastLoad = g.loadAll;
    if (g.loadFront < 0.1f) g_gndAirTicks++;
    *outLoad = g.loadAll;
    if (!GroundSlip(veh, &slip, &spd)) return 0.0f;

    /* ---- THE GAME STOPS UPDATING THE CAR AND WE KEEP READING IT -------------------------------
       Three windows of drive ours-019 held the sent force at a constant value for 11.9, 12.4 and
       3.9 seconds: the main menu, the Car Controls screen and the exit dialog. The car object is
       simply not written while the game is paused, so every field we read is last frame's, the
       model computes the same answer for ever, and the wheel sits pulled to one side. It is a
       small force - 151 and -438 units in his log - and it is exactly the feel of a dead wheel.
       A frozen input is DETECTED rather than assumed: the speed, the body slip and the steering
       angle bit-identical for `ground_stale_ticks` calls in a row. A genuinely parked car trips
       the same test and nothing is lost by it - at a standstill this channel's force is already
       zero, and the standstill DAMPER, which is the part he approves of, is a separate effect that
       this does not touch. */
    /* MOVED BELOW - it needs the yaw rate, which is read in the next block. The first version
       compared the speed, the body slip and the steering angle only, and a test that turned the
       car harder without changing any of those three was called frozen. A detector is only as
       wide as the inputs it watches. */

    /* The body slip is kept separately: it is what the old channel used, it is what the log
       compares against, and the corner test - a loaded wheel with NO slide - is exactly the case
       where these two numbers must disagree. */
    { float yaw = 0.0f, delta = 0.0f;
      int haveSteer = 0;
      static float s_dMin = 1e9f, s_dMax = -1e9f;
      GroundYawSteer(veh, &yaw, &delta, &haveSteer);
      /* Is the steering field alive? The listing could not say, so measure it: a field that has
         swung more than a degree and a half over the session is being written by something. */
      if (haveSteer) {
          if (delta < s_dMin) s_dMin = delta;
          if (delta > s_dMax) s_dMax = delta;
          if ((s_dMax - s_dMin) > 0.026f) g_gndSteerLive = 1;
      }
      g_gndLastBodySlip = slip;
      g_gndLastYaw      = yaw;
      g_gndLastDelta    = g.nSteer ? g.deltaF : delta;
      /* THE ENGINE'S OWN SLIP ANGLE, once it has proved it moves. `wheel+0x108` is sin of the
         angle between the contact patch's velocity and the wheel's heading - the quantity every
         line of this channel has been estimating since July, measured by the solver that also
         decides grip. Liveness is measured, not assumed: a field read out of a listing is a
         hypothesis until a running game disagrees with zero, and the estimate stays in charge
         until then. sin against our tangent costs 8% at 20 degrees of slip, well inside the
         fade, so no conversion is worth the code. */
      if (g.nSteer) {
          float s = g.sinSlipF < 0.0f ? -g.sinSlipF : g.sinSlipF;
          if (s > 0.002f) g_gndSlipLive = 1;
      }
      g_gndLastSinSlip = g.sinSlipF;
      if (g_gndSlipSrc == 3 || (g_gndSlipSrc == 0 && g.nSteer && g_gndSlipLive)) {
          /* MAGNITUDE FROM THE ENGINE, SIGN FROM THE KINEMATIC ANGLE - see g_gndSignBand for the
             measurement that forced this apart. `-alphaKin` is what our own convention calls the
             slip: positive when the wheel is steered right, which is the direction the force has
             to oppose. Inside the band it fades through zero instead of flipping sides. */
          float mag = g.sinSlipF < 0.0f ? -g.sinSlipF : g.sinSlipF;
          if (g_gndSignBand > 0 && g.nSteer) {
              float w = -GroundAlphaKin(slip, spd, yaw, g.deltaF)
                        / ((float)g_gndSignBand * 0.001f);
              if (w >  1.0f) w =  1.0f;
              if (w < -1.0f) w = -1.0f;
              slip = mag * w;
          } else {
              slip = g.sinSlipF;          /* the old device-frame sign, kept as the escape hatch */
          }
      } else {
          slip = GroundAlphaEff(slip, spd, yaw, delta, haveSteer);
      }
    }

    /* ---- IS THE CAR BEING UPDATED AT ALL? Every input it has, compared with last tick's -------
       Placed here because this is the first point where all four are known. Four, not three: the
       yaw rate can change while the speed, the body slip and the steering angle do not, and the
       first version of this check called that frozen. */
    {
        static float s_pSpd = -1.0f, s_pSlip = -1.0f, s_pDelta = -1.0f, s_pYaw = -1.0f;
        static int   s_staleN = 0;
        if (spd == s_pSpd && g_gndLastBodySlip == s_pSlip &&
            g_gndLastDelta == s_pDelta && g_gndLastYaw == s_pYaw) {
            if (s_staleN < 1000000) s_staleN++;
            if (g_gndStaleTicks > 0 && s_staleN == g_gndStaleTicks)
                Log(K_INIT, 0x26, 1, (DWORD)s_staleN, (DWORD)(LONG)g_gndLastForce);
        } else {
            if (g_gndStaleTicks > 0 && s_staleN >= g_gndStaleTicks)
                Log(K_INIT, 0x26, 0, (DWORD)s_staleN, (DWORD)(LONG)g_gndLastForce);
            s_staleN = 0;
        }
        s_pSpd = spd; s_pSlip = g_gndLastBodySlip;
        s_pDelta = g_gndLastDelta; s_pYaw = g_gndLastYaw;
        g_gndStale = (g_gndStaleTicks > 0 && s_staleN >= g_gndStaleTicks) ? 1 : 0;
    }

    /* ---- SAT: the steering weight, and where the wheel wants to return to ---- */
    if (g_gndSat) {
        float aSlip = slip < 0.0f ? -slip : slip;
        float optimal = (float)g_gndOptDeg * 0.017453f;   /* degrees -> tan, small angle */
        float ratio = GroundSlipCurve(aSlip, optimal);
        weight = g.loadFront;
        if (weight < 1.0f) weight = GroundPow25(weight);  /* theirs, and it is the airborne fade */
        velFac = ((spd * 100.0f) - (float)g_gndVmin) / (float)(g_gndVfull - g_gndVmin);
        if (velFac < 0.0f) velFac = 0.0f;
        if (velFac > 1.0f) velFac = 1.0f;
        /* GTA MT's last multiplier, and the one we were told could not be ported. Smoothed, so a
           single tick of wheelspin is a fade rather than a step in the driver's hands. */
        { float target = GroundLongMult(veh, &g);
          float aL = (g_gndLongTauMs > 0 && dtMs > 0)
                   ? ((float)dtMs / (float)(dtMs + g_gndLongTauMs)) : 1.0f;
          s_longMult += (target - s_longMult) * aL;
          g_gndLastLong = s_longMult; }
        /* v7.96: THE SPEED CURVE. Measured on ours-028 inside one preset, so no preset change
           pollutes it: what one g of cornering COSTS in force at each speed, which would be flat
           if the model followed the physics and instead varies by a factor of eight -
           10-20 km/h 19187, 20-30 10158, 30-40 5112, 40-50 4192, 50-70 3137, 70-90 2405.
           The aligning torque is Fy x trail and Fy = m x ay, so the same g must weigh the same
           whatever the speed; ours does not, because the force comes from the slip ANGLE and at
           crawling speed the same corner produces a large angle at a tiny acceleration.
           The remark: otherwise right now it still seems the same regardless of speed
           - and the measurement says worse than that, below 40 it is heavier per g, not equal.
           The flattening multipliers are 0.22 / 0.41 / 0.82 / 1.00 / 1.34 / 1.74; the shipped
           curve is the choice of a softer low end (physics 0.22 would be too little). */
        satMult = GroundSpeedMult(spd * 3.6f);
        g_gndLastKmh = spd * 3.6f;
        TrueSpeedRingPush(g_gndLastKmh, GetTickCount());
        g_gndLastSatMult = satMult;
        sat = (float)g_gndSatK * ratio * velFac * weight * s_longMult * satMult;
        if (slip > 0.0f) sat = -sat;                      /* opposes the slide: the counter-steer */
        /* v7.92: HOW MUCH GRIP THE FRONT STILL HAS, 1 = full, 0 = gone. The tyre curve past its
           peak and the longitudinal fade are the two ways this model knows the front has let go,
           and it is the same quantity the damper now follows - his ours-023 complaint was that
           the wheel goes light in FORCE but stays thick in DAMPING at exactly that moment:
           on F5 the wheel is too damped on loss of slip... a lighter
           wheel wanted. Kept separate from the load gate, which answers a different question (wheels in
           the air), because a sliding tyre is still carrying its load. */
        /* v811: below the peak the tyre is HOLDING, whatever the force curve says about it.
           Its own function so it can be tested without a car - the defect it fixes lived for
           eight builds behind 37 green checks precisely because every one of them handed
           DamperCoeffFor a grip of 1.0 and never asked where a real grip comes from. */
        g_gndLastGrip = GroundGripForDamper(aSlip, optimal, ratio, s_longMult);
    } else {
        g_gndLastGrip = 1.0f;   /* NEUTRAL when it does not know - a grip term that failed shut
                                   would silence the damper and read as a broken wheel */
    }

    /* ---- CASTER: the wheels straighten themselves as the car rolls ----------------------------
       Proportional to the steering angle over the vehicle's own maximum lock (`veh+0x680`), to
       the front load, and to nothing else once the car is rolling at all. This is the geometry
       term, not a spring: a spring pulls towards a fixed centre whatever the tyres are doing,
       while this one dies with the load exactly like SAT - in the air the wheels do not
       straighten either. */
    if (g_gndCasterK > 0 && g.nSteer) {
        float lock = 0.0f, norm, roll;
        if (!SafeReadF(veh + 0x680u, &lock) || lock < 0.05f || lock > 2.0f) lock = 0.7f;
        norm = g.deltaF / lock;
        if (norm >  1.0f) norm =  1.0f;
        if (norm < -1.0f) norm = -1.0f;
        /* the square law, sign preserved: weak off centre, strongest at the lock. 100 = the old
           linear behaviour, exactly. */
        if (g_gndCasterExp >= 150) {
            float an = norm < 0.0f ? -norm : norm;
            if (g_gndCasterExp >= 250) norm = norm * an * an;   /* cube  */
            else                       norm = norm * an;        /* square */
        }
        roll = (spd * 100.0f) / (float)g_gndCasterVcms;
        if (roll > 1.0f) roll = 1.0f;
        if (roll < 0.0f) roll = 0.0f;
        caster = -(float)g_gndCasterK * norm * roll * g.loadFront;
        /* the rate term - see g_gndCasterD. Clamped to the caster's own scale so a single noisy
           sample of the steering angle cannot become a jolt in his hands. */
        if (g_gndCasterD > 0 && dtMs > 0) {
            static float s_prevDelta = 0.0f;
            static int   s_havePrev = 0;
            if (s_havePrev) {
                float rate = (g.deltaF - s_prevDelta) / ((float)dtMs * 0.001f);   /* rad/s */
                float dmp  = -(float)g_gndCasterD * rate;
                if (dmp >  (float)g_gndCasterK) dmp =  (float)g_gndCasterK;
                if (dmp < -(float)g_gndCasterK) dmp = -(float)g_gndCasterK;
                dmp *= roll * g.loadFront;
                /* KEPT APART FOR THE LOG, and the audit is why. These two terms have opposite
                   jobs - the spring opposes the ANGLE, the damper opposes the RATE - so their sum
                   routinely carries the sign of neither. With the square law the spring at a
                   quarter of the lock is 150 units while the damper can be 2400, and the audit's
                   "the caster restores" check read the sum and called 19% of the rows wrong on
                   drive ours-019. It was measuring an addition, not a defect. */
                g_gndLastCasterD = dmp;
                caster += dmp;
            } else {
                g_gndLastCasterD = 0.0f;
            }
            s_prevDelta = g.deltaF; s_havePrev = 1;
        }
        g_gndLastCaster = caster;
    } else {
        g_gndLastCaster = 0.0f;
        g_gndLastCasterD = 0.0f;
    }

    /* ---- Detail: one front wheel over a kerb pulls the wheel, both over it do not ---- */
    if (g_gndDetail && g.haveSusp && dtMs > 0) {
        if (s_haveSusp) {
            /* THE MOVING AVERAGE IS THEIRS - GTA MT keeps DetailMAW (3) frames of per-wheel
               compression speed and averages them before the force is built
               (`VehicleData.cpp:132-153`). A first difference of a position sampled every 15 ms is
               a noise amplifier, and ours had nothing between that difference and the driver's
               hands. */
            static float s_maw[GND_MAW_MAX];
            static int   s_mawN = 0, s_mawI = 0;
            float vFL = (g.suspFL - s_suspFL) / ((float)dtMs * 0.001f);
            float vFR = (g.suspFR - s_suspFR) / ((float)dtMs * 0.001f);
            int   n = (int)g_gndDetailMaw, k;
            float acc = 0.0f;
            if (n < 1) n = 1;
            if (n > GND_MAW_MAX) n = GND_MAW_MAX;
            s_maw[s_mawI] = vFR - vFL;
            s_mawI = (s_mawI + 1) % GND_MAW_MAX;
            if (s_mawN < GND_MAW_MAX) s_mawN++;
            if (n > s_mawN) n = s_mawN;
            for (k = 1; k <= n; k++)
                acc += s_maw[(s_mawI - k + GND_MAW_MAX) % GND_MAW_MAX];
            detail = (float)g_gndDetailK * (acc / (float)n);
            /* their DetailLim, an explicit number, instead of borrowing the SAT scale */
            if (detail >  (float)g_gndDetailLim) detail =  (float)g_gndDetailLim;
            if (detail < -(float)g_gndDetailLim) detail = -(float)g_gndDetailLim;
            detail *= g.loadFront;                        /* no texture through wheels in the air */
        }
        s_suspFL = g.suspFL; s_suspFR = g.suspFR; s_haveSusp = 1;
    }
    g_gndLastDetail = detail;

    /* ---- THE DEVICE'S SIGN IS THE OPPOSITE OF OURS, and drive ours-017 is what proved it -------
       Everything above is computed in the VEHICLE's frame: positive means "towards the car's
       right", taken from the body's right axis at `veh+0xCB8`. The DirectInput axis this module
       writes to runs the other way, so a force this file calls restoring physically drove the
       wheel further from centre - positive feedback, and he felt it in the first ten seconds:
       *"SAT feels inverted and pulls to the side. It does not pull to the centre, it pulls away.
       At the centre it disappears. The moment it moves a little it accelerates and accelerates."*

       The log carries the same thing as DYNAMICS rather than as opinion: five excursions from
       under 0.15 rad to past 0.75 rad, each in under two seconds, and the wheel spent 12% of that
       drive sitting at full lock. In every one of them our force grew as the angle grew.

       ONE NEGATION HERE, not a flipped setting: `ground_invert` stays at 0 and stays an escape
       hatch, because a default that has to be 1 to be correct is a bug wearing a setting's
       clothes. Detail and caster flip with SAT deliberately - all three are built in the same
       frame, so one sign serves all three and they cannot disagree later. */
    /* v824: the centring pair grows above 600 deg - see g_centreK. 1.0 at 600 and below. */
    {
        float cg = CentreGain();
        want = -(sat * cg + detail + caster * cg);
    }
    if (g_gndInvert) want = -want;
    /* a frozen car is not a car: fade out rather than hold. It goes through the filter and the
       slew like everything else, so the wheel relaxes instead of dropping. */
    if (g_gndStale) want = 0.0f;

    /* Smoothing and a slew ceiling. Theirs is recomputed every frame and sent raw; ours is
       filtered because a direct drive executes a step literally - the lesson of run 011. */
    if (dtMs > 0) {
        a = (g_gndTauMs > 0) ? ((float)dtMs / (float)(dtMs + g_gndTauMs)) : 1.0f;
        d = (want - s_force) * a;
        step = (float)g_gndSlew * (float)dtMs * 0.001f;
        /* v825: THE RATE CEILING SCALES WITH THE CHANNEL. A ceiling on how fast the force may
           change is a ceiling like any other: at 400% the same corner asks for four times the
           change, and a fixed 40000/s would deliver it four times slower - a slider that also
           made the wheel lazier. Scaled by the channel's own size (GroundScalePct), so the shape
           in time is his at every setting. Skipped at 100%: the v824 arithmetic, to the bit. */
        {
            LONG gs = GroundScalePct();
            if (gs != 100) step = step * (float)gs * 0.01f;
        }
        if (step > 0.0f) { if (d > step) d = step; if (d < -step) d = -step; }
        s_force += d;
    }
    /* NO `else s_force = want`. Zero measured time means nothing has happened yet, so the force
       is held. The branch that used to be here is the one that let a hitched tick step the wheel
       by eleven thousand units - see the dt clamp at the top of this function. */

    /* ---- THE SELF-TEST: push the wheel off centre, let go, watch what the HARDWARE does -------
       Four phases, and the only thing it decides is whether the angle came back. See the comment
       on g_gndSelfTest for why no check made in our own frame could have caught the sign. */
    if (g_gndSelfTest > 0 && g.nSteer) {
        static int   s_stPhase = 0;      /* 0 wait, 1 push, 2 measure, 3 done with this run     */
        static DWORD s_stT0 = 0;
        static int   s_stRun = 0, s_stDir = 1;
        static float s_stPeak = 0.0f;
        float ang = g.deltaF;
        float aAbs = ang < 0.0f ? -ang : ang;
        if (s_stRun < g_gndSelfTest) {
            if (s_stPhase == 0) {
                /* Only from a straight wheel on a car that is properly MOVING - 4 m/s, not the
                   1.5 it used to be. Below the damper's fade speed the standstill damper is near
                   20000 and it simply holds the wheel: the first release of the straight-line run
                   was logged as "pushed to 0.000 rad", which reads like a failed return and was
                   actually a push that never moved anything. A test whose null result looks like
                   a defect is worse than no test. */
                if (spd > 4.0f && aAbs < 0.10f) {
                    s_stPhase = 1; s_stT0 = nowMs; s_stPeak = 0.0f; g_gndSelfActive = 1;
                    Log(K_INIT, 0x21, 0, (DWORD)(LONG)(ang * 10000.0f),
                        (DWORD)(((DWORD)(LONG)(spd * 100.0f) & 0xFFFFu) | ((DWORD)s_stRun << 16)));
                }
            } else if (s_stPhase == 1) {
                s_force = (float)(s_stDir * g_gndSelfMag);   /* the push, past the filter */
                if (aAbs > s_stPeak) s_stPeak = aAbs;
                if ((DWORD)(nowMs - s_stT0) > 500u) {
                    s_stPhase = 2; s_stT0 = nowMs;
                    Log(K_INIT, 0x21, 1, (DWORD)(LONG)(ang * 10000.0f),
                        (DWORD)(((DWORD)(LONG)(s_stPeak * 10000.0f) & 0xFFFFu) |
                                ((DWORD)s_stRun << 16)));
                }
            } else if (s_stPhase == 2) {
                /* released - the model alone now moves the wheel. Every row is logged, because
                   the SHAPE of the return is the evidence, not its endpoint. */
                Log(K_INIT, 0x21, 2, (DWORD)(LONG)(ang * 10000.0f),
                    (DWORD)(((DWORD)(LONG)(s_force) & 0xFFFFu) | ((DWORD)s_stRun << 16)));
                if ((DWORD)(nowMs - s_stT0) > 3000u) {
                    Log(K_INIT, 0x21, 3, (DWORD)(LONG)(ang * 10000.0f),
                        (DWORD)(((DWORD)(LONG)(s_stPeak * 10000.0f) & 0xFFFFu) |
                                ((DWORD)s_stRun << 16)));
                    s_stPhase = 0; s_stRun++; s_stDir = -s_stDir; g_gndSelfActive = 0;
                }
            }
        }
    }

    g_gndLastForce = s_force;

    /* ---- 0x29 THE SPIKE: what the force was MADE OF at the moment it jumped -------------------
       Written on the tick it happens, never one in four, because a bang is the one event in this
       channel that a sampled log cannot represent. The three terms are packed as signed 16-bit
       values, which is safe: SAT, detail and caster are each bounded well inside +-10000 by
       ground_sat_k, ground_detail_lim and the caster's own ramp, and the packer clamps anyway
       rather than letting a future constant wrap the field silently.
         b = SAT       | detail << 16
         c = caster    | (true km/h x10) << 16
         d = the change in the SENT force across the last 60 ms, signed - what a hand feels as
             a blow. NOT a one-tick difference: the slew ceiling bounds a tick to 640 units.
       It answers the question the offline attribution can only guess at: a step whose detail term
       moved with it is the suspension channel, one whose SAT term moved with it is the tyre
       model, and one where neither moved is not ours at all and belongs to the impact family. */
    if (g_gndSpike > 0 && dtMs > 0) {
        /* THE WINDOW IS 60 ms, NOT ONE TICK, and that is the whole design of this row.
           The slew ceiling allows ground_slew * dt = 40000 * 0.016 = 640 units in a 16 ms tick,
           so a single-tick step can almost never reach a threshold worth calling a bang - a
           per-tick test would have fired only on a hitch and reported the channel as silent
           through every hit he actually felt. What a hand feels as a blow is the force ARRIVING
           inside a few tens of milliseconds, which is this: the change against the oldest sample
           still inside the window. Rate-limited to one row per 80 ms so a long ramp writes its
           onset rather than a row per tick. */
        static float s_hist[GND_SPIKE_HIST];
        static DWORD s_histT[GND_SPIKE_HIST];
        static int   s_histN = 0, s_histI = 0;
        static DWORD s_lastSpike = 0;
        float oldest = s_force;
        int   k;
        for (k = 0; k < s_histN; k++) {
            int idx = (s_histI - 1 - k + GND_SPIKE_HIST) % GND_SPIKE_HIST;
            if ((DWORD)(nowMs - s_histT[idx]) > 60u) break;
            oldest = s_hist[idx];
        }
        float stepSent = s_force - oldest;
        float stepAbs  = stepSent < 0.0f ? -stepSent : stepSent;
        if (s_histN > 0 && stepAbs >= (float)g_gndSpike &&
            (DWORD)(nowMs - s_lastSpike) >= 80u) {
            s_lastSpike = nowMs;
            LONG satI = (LONG)sat, detI = (LONG)detail, casI = (LONG)g_gndLastCaster;
            LONG spdI = (LONG)(spd * 36.0f);            /* km/h x10 */
            if (satI >  32767) satI =  32767; if (satI < -32768) satI = -32768;
            if (detI >  32767) detI =  32767; if (detI < -32768) detI = -32768;
            if (casI >  32767) casI =  32767; if (casI < -32768) casI = -32768;
            if (spdI >  32767) spdI =  32767; if (spdI < 0)      spdI = 0;
            Log(K_INIT, 0x29,
                (DWORD)(((DWORD)satI & 0xFFFFu) | (((DWORD)detI & 0xFFFFu) << 16)),
                (DWORD)(((DWORD)casI & 0xFFFFu) | (((DWORD)spdI & 0xFFFFu) << 16)),
                (DWORD)(LONG)stepSent);
        }
        s_hist[s_histI] = s_force;
        s_histT[s_histI] = nowMs;
        s_histI = (s_histI + 1) % GND_SPIKE_HIST;
        if (s_histN < GND_SPIKE_HIST) s_histN++;
    }

    if (g_gndLog) {
        static DWORD s_n = 0;
        /* THE ROW THAT SEPARATES A CORNER FROM A SLIDE. Without the body slip beside the effective
           angle, a loaded wheel cannot be told from a sliding one, and "the corner now loads the
           wheel" would be a claim rather than a measurement. */
        if ((s_n & 3u) == 0)
            Log(K_INIT, 0x1C, (DWORD)(LONG)(g_gndLastBodySlip * 10000.0f),
                (DWORD)(LONG)(g_gndLastYaw * 10000.0f),
                (DWORD)((((DWORD)(LONG)(g_gndLastDelta * 10000.0f)) & 0xFFFFu) |
                        ((DWORD)(LONG)(GroundGate() * 10000.0f) << 16)));
        /* THE ENGINE'S OWN TYRE STATE, beside ours. a = its sin(slip angle) x10000 signed;
           b = the steered wheels' flags, so 0x20 (locked) and 0x80 (wheelspin) can be counted
           against the fade; c = the fade x10000 with the measured steering angle above it.
           This is the row that says whether the fields found in the decompilation are alive in a
           running game - which is a claim no listing can settle. */
        if ((s_n & 3u) == 0)
            Log(K_INIT, 0x20, (DWORD)(LONG)g_gndLastCaster, (DWORD)(LONG)sat,
                (DWORD)(LONG)(spd * 100.0f));
        /* 0x22 - THE THREE THINGS ours-018b COULD NOT ANSWER, each of which cost a paragraph of
           "we cannot separate this inside one log":
             a = the detail channel AS SENT. It was never logged, so "is it the suspension or is it
                 SAT" had to be estimated as a residual against two other rows written on a
                 different tick. Now it is a column.
             b = the MEASURED tick in ms. A hitch and a quiet tick looked identical from outside.
             c = the TRUE speed, km/h x100, from the physics velocity vector at `veh+0x2000` - the
                 one the model has always used. Every other speed in this log comes from
                 `car+0x2A0C`, which halves ([[speed-field-double-store]]): on his drive it never
                 passed 20 km/h while the speedometer in the frame read 35 mph and the coordinates
                 gave 71. The force paths deliberately keep the old field - the impulse constants
                 were fitted to it over 165 drives - but nothing that is only MEASURED should read
                 it again. */
        if ((s_n & 3u) == 0)
            Log(K_INIT, 0x22, (DWORD)(LONG)g_gndLastDetail, (DWORD)dtMs,
                (DWORD)(LONG)(spd * 360.0f));
        /* 0x28 - THE CASTER, SPLIT. 0x20 keeps carrying the SUM and keeps its meaning, because a
           log code that changes meaning between builds is a trap this project has already paid
           for. a = the spring term (opposes the angle), b = the rate term (opposes the movement),
           c = the stale flag, so a frozen car is visible in the same row that shows the force. */
        if ((s_n & 3u) == 0)
            Log(K_INIT, 0x28, (DWORD)(LONG)(g_gndLastCaster - g_gndLastCasterD),
                (DWORD)(LONG)g_gndLastCasterD, (DWORD)g_gndStale);
        if ((s_n & 3u) == 0)
            Log(K_INIT, 0x1E, (DWORD)(LONG)(g_gndLastSinSlip * 10000.0f), g.flagsF,
                (DWORD)((((DWORD)(LONG)(g_gndLastLong * 10000.0f)) & 0xFFFFu) |
                        ((((DWORD)(LONG)(g.deltaF * 10000.0f)) & 0xFFFFu) << 16)));
        if ((++s_n & 3u) == 0)
            Log(K_GROUND, (DWORD)(LONG)(g.loadAll * 10000.0f),
                g.planted | ((DWORD)g.n << 16) | ((DWORD)(LONG)(g.loadFront * 100.0f) << 20),
                (DWORD)(LONG)(slip * 10000.0f), (DWORD)(LONG)s_force);
    }
    return s_force;
}

/* ---- the spring, extracted so it can be tested without a wheel, and then switched off --------
   His decision, 2026-09-13: *"remove the spring, everything through SAT"*. A spring pulled to a
   FIXED centre cannot express the front letting go - it pulls hardest exactly when the tyres have
   stopped holding - and it is what he called dishonest in a slide. With the ground model running,
   centring is the self-aligning torque's job and this effect stays silent.
   The old arithmetic is kept intact behind `groundOn = 0`, so switching the channel off in the
   game restores the approved feel bit for bit. */
static LONG SpringCoeffFor(float speedKmh, float springX, int groundOn)
{
    float centerFactor, hiSpeedFactor;
    if (groundOn) return 0;
    centerFactor  = Clampf(speedKmh / CENTER_FULL_KMH, 0.0f, 1.0f);
    hiSpeedFactor = Clampf((speedKmh - HISPEED_LO_KMH) / (HISPEED_HI_KMH - HISPEED_LO_KMH),
                           0.0f, 1.0f);
    return (LONG)((K_CENTER_DI * centerFactor + K_HISPEED_DI * hiSpeedFactor) * springX);
}

/* ---- the damper: halved in motion, no highway term, and it dies with the tyre load ----------
   GTA MT's damper is map(speed, 0..DamperMinSpeed, DamperMax..DamperMin) and ours keeps its OWN
   threshold: DAMP_FADE_KMH = 10 km/h, checked in this file rather than remembered, because that
   small-speed range is the part of the current feel he likes.
   Two changes, both his: the moving value halves because SAT now carries the weight that the
   damper was standing in for, and the 80-120 km/h term goes entirely for the same reason. The
   load multiplier is the empty wheel - in the air there is nothing to damp against. */
static LONG DamperCoeffFor(float speedKmh, float dStatic, float dMoving, int groundOn, float gate,
                           float grip)
{
    float fade, v;
    /* THE MOVING VALUE IS SCALED HERE, NOT IN K_DAMP_DI_MOVING. Changing the constant would move
       every feel profile and the truck factors with it, and the rule this project lives by is
       that switching the channel off must return the approved build bit for bit. */
    /* v811: the F1/F2 ladder names the moving damper OUTRIGHT, so what he reports back is the
       number itself rather than a percentage of a base he cannot see. 0 = the percentage path,
       which is what a shipped ini and the F6 reset both use. */
    if (groundOn) {
        if (g_gndDampMoveAbs > 0) dMoving = (float)g_gndDampMoveAbs;
        else                      dMoving *= (float)g_gndDampMovePct * 0.01f;
    }
    fade = Clampf(speedKmh / DAMP_FADE_KMH, 0.0f, 1.0f);
    v = dStatic + (dMoving - dStatic) * fade;
    if (!groundOn) {
        float hi = Clampf((speedKmh - HISPEED_DAMP_LO_KMH)
                          / (HISPEED_DAMP_HI_KMH - HISPEED_DAMP_LO_KMH), 0.0f, 1.0f);
        float hispeed = (float)K_DAMP_DI_HISPEED * (dMoving / (float)K_DAMP_DI_MOVING);
        v += (hispeed - dMoving) * hi;
    } else {
        float floorV = (float)g_gndDampAir;
        if (floorV > v) floorV = v;      /* never MORE damper than the planted wheel would have */
        v *= gate;
        /* v7.92: THE DAMPER GOES WITH THE GRIP. His ours-023 verdict, in his own words: the force
           does lighten when the front lets go and that is liked, but the wheel is still too
           damped... on loss of slip - a wheel that is light but thick, which is not
           how a sliding car feels. `ground_damp_slip` is the percentage removed at FULL slide;
           0 is the behaviour every drive before ours-023 was measured on. */
        if (g_gndDampSlip > 0) {
            float g = grip;
            if (g < 0.0f) g = 0.0f;
            if (g > 1.0f) g = 1.0f;
            v *= 1.0f - ((float)g_gndDampSlip * 0.01f) * (1.0f - g);
        }
        if (v < floorV) v = floorV;      /* the thin airborne damper - see g_gndDampAir */
        /* v811: THE STANDSTILL DAMPER IS HIS OWN KNOB, on the J key. His instruction, 2026-09-14:
           leave as it is now: it is 4000, then try, say, 8000, 12000 and 20000.
           Just switching, and a verdict in the end on what feels normal.
           It names the FINISHED value at a dead stop and fades out by DAMP_FADE_KMH, so it cannot
           reach into the moving feel he has already approved: at 10 km/h this term is exactly 0.
           0 means "do not intervene", which is what a shipped ini carries. */
        if (g_gndDampStand > 0 && speedKmh < DAMP_FADE_KMH) {
            float blend = 1.0f - (speedKmh / DAMP_FADE_KMH);
            if (blend < 0.0f) blend = 0.0f;
            if (blend > 1.0f) blend = 1.0f;
            v += ((float)g_gndDampStand - v) * blend;
        }
    }
    if (v < 0.0f) v = 0.0f;
    return (LONG)v;
}

/* THE COEFFICIENTS AS SENT, one tick in eight. 0xDC carries the same three numbers but only on a
   mark key, so run 016 could not answer "did the damper stay heavy while I was in the air" from a
   drive where he pressed nothing. This row makes the whole model checkable off ONE log: the spring
   must be 0 with the channel on, the damper must follow the gate down to its thin airborne floor,
   and friction must follow it to zero.

   IT IS ITS OWN FUNCTION ON PURPOSE. Written inline in the FFB thread it pushed that frame past
   4 KB and clang emitted a call to __alloca, which does not exist under -nostdlib: the build broke
   loudly, which is the good case, but the lesson is that the thread's frame has no room left. */
static void LogCoeffsSent(LONG spring, LONG damp, LONG fric)
{
    static DWORD s_coefN = 0;
    if (!g_gndLog) return;
    if ((s_coefN++ & 7u) != 0) return;
    Log(K_INIT, 0x1D, (DWORD)spring,
        (DWORD)(((DWORD)damp & 0xFFFFu) | ((DWORD)fric << 16)),
        (DWORD)((((DWORD)(LONG)(GroundGate() * 10000.0f)) & 0xFFFFu) |
                ((DWORD)g_spdX100 << 16)));
}

/* THE GATE. Every other force reads this one number: our own continuous force, and from v7.8 the
   damper and whatever is left of the spring. It is deliberately the only way out of the ground
   channel, so there is one place to look when the wheel goes light and one place to test.

   It is NEUTRAL whenever it does not know - no channel, no gate setting, no vehicle - because the
   two failure modes are not symmetrical: a gate stuck open costs the empty wheel, a gate stuck
   shut costs the whole wheel and reads to the driver as broken hardware. */
static float GroundGate(void)
{
    float L;
    if (!g_gndOn || !g_gndGate) return 1.0f;
    L = g_gndLastLoad;
    if (L < 0.0f) return 0.0f;
    if (L > 1.0f) return 1.0f;
    return L;
}

/* Assert a state. Called by both keys and by the offline test, which is why the key reading and
   the state change are separate functions - the half that matters is testable on a host with no
   keyboard and no wheel.

   IT ANSWERS IN THE WHEEL, because there is nowhere else to answer. He is driving, the game is
   full screen and pauses if it loses focus, so a key that silently did nothing would be
   indistinguishable from a key whose effect he could not feel. The two answers are DIFFERENT
   shapes, so the wheel says which state he is now in rather than merely that something happened:

     ON  - one nudge out and back, about 160 ms. One push.
     OFF - two short taps the same way, about 200 ms. Unmistakably not one push.

   Every press is logged (0x6B) even when it changes nothing, so the drive's own log says which
   stretch was driven with the channel and which without - and a press that produced no row is a
   press the module never saw, which is a different fault from a channel he could not feel. */
static void GroundSetEnabled(LONG want)
{
    g_gndOverride = want ? 1 : 0;
    g_gndOn       = g_gndOverride;
    if (g_gndAckMag > 0) {
        g_gndAckPat = want ? 0 : 1;
        g_gndAck    = want ? 20 : 25;
    }
    Log(K_INIT, 0x6B, (DWORD)g_gndOn, (DWORD)g_gndReads,
        (DWORD)(LONG)(g_gndLastLoad * 10000.0f));
}

/* v821: THE HOTKEYS ANSWER ONLY WHILE THE GAME IS IN FRONT.
   ours-038, 2026-09-30: while the game was loading he typed "profil" into the Windows search box
   (the frame at 16:32:53 shows it), and GetAsyncKeyState - which reads the WHOLE keyboard,
   whatever window has focus - handed the module P, O, I and L, then an N. N is the ground
   channel's OFF key, so the entire drive ran without the ground model and he reported "no
   centring spring". Nothing was pressed in the game: the key recorder, which records only while
   Game.exe is in front, has none of those keys.
   So every key that CHANGES something here needs Game.exe's own window in front. A key held while
   another window is in front reads as up, and is logged ONCE per key-down as 0x3C (a = the VK), so
   a debrief sees the typing instead of having to infer it. The input telemetry further down keeps
   its raw reads on purpose - it records what was held, which is true whatever had focus. */
static int GameIsForeground(void)
{
    static DWORD s_at = 0xFFFFFFFFu;
    static int   s_ok = 0;
    DWORD now = GetTickCount();
    if (now != s_at) {                  /* once per timer tick, not once per key */
        HWND  fg  = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        s_ok = (fg != NULL && pid == GetCurrentProcessId());
        s_at = now;
    }
    return s_ok;
}

static int KeyDown(int vk)
{
    static unsigned char s_refused[256];
    int raw;
    if (vk < 1 || vk > 255) return 0;   /* a retired key of 0 must never reach GetAsyncKeyState */
    raw = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (!raw) { s_refused[vk] = 0; return 0; }
    if (GameIsForeground()) { s_refused[vk] = 0; return 1; }
    if (!s_refused[vk]) {
        s_refused[vk] = 1;
        Log(K_INIT, 0x3C, (DWORD)vk, 0, 0);
    }
    return 0;
}

static void PollGroundKeys(void)
{
    static int downOn = 0, downOff = 0;
    int on  = KeyDown((int)g_gndKeyOn);
    int off = KeyDown((int)g_gndKeyOff);
    if (on  && !downOn)  GroundSetEnabled(1);
    if (off && !downOff) GroundSetEnabled(0);
    downOn = on; downOff = off;
}

/* ---- v811: the two damper keys, and they are two because they answer two different questions --
   J walks the standstill bank he asked for - 4000, 8000, 12000, 16000, 20000 - and the wheel
   answers with that many pulses, so the acknowledgement names the STEP instead of merely
   confirming a press ([[no-numpad-keyboard]]: he cannot see a toggle's state, so the wheel has to
   say it). B switches between the old and the new meaning of "the front has let go", which is the
   fix described at g_gndDampGripMode: one pulse for the new arithmetic, two for the v810 one.

   Both are logged on every press (0x34), including a press that changes nothing, because a press
   that produced no row is a press the module never saw - a different fault from one he could not
   feel. B and J are two of the nine letters the game's own defaults leave alone (docs\KEY-MAP.md). */
/* THE STANDSTILL DAMPER as a ladder rather than a cycle, his correction of 2026-09-14: two keys,
   one step each way, so he can walk it the same way as the moving one. Step 2 (4000) is what the
   v810 arithmetic actually delivers parked; step 10 is the 20000 the setting has always claimed. */
#define DAMP_STAND_STEPS 10
static const LONG g_dampStandBank[DAMP_STAND_STEPS] =
    { 2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000, 18000, 20000 };

/* THE DAMPER WHILE THE TYRES HOLD - what he feels driving straight, which is the half he asked to
   walk step by step. Absolute coefficients rather than percentages, because a percentage of a
   base he cannot see is not a number he can report back. Step 5 (2625) is where the approved
   build sits once the grip fix is in; the published release sent 5250, which is step 9. */
#define DAMP_GRIP_STEPS 10
static const LONG g_dampGripBank[DAMP_GRIP_STEPS] =
    { 500, 1000, 1500, 2000, 2625, 3250, 4000, 4750, 5500, 6500 };

/* THERE IS NO LADDER FOR THE SLIDE, and that is the decision of 2026-09-14: after the breakaway no
   damper is wanted at all. The current feel is liked. So `damp_slip`
   stays at 78 and nothing in this build offers to move it - the half he likes is left alone, and
   only the half he has never actually been able to judge gets a knob. */
static volatile LONG g_dampGripIdx = 4;    /* 2625 */

static void DampKeyAck(int pulses, int atEdge)
{
    if (g_gndAckMag <= 0) return;
    if (atEdge) { g_gndAckPat = 1; g_gndAck = 25; return; }   /* the double tap: no further */
    g_gndAckPat = 2;
    g_gndAck    = (LONG)pulses * 20;
}

static void DampLog(void)
{
    g_dampKeyUsed   = 1;
    g_dampKeepMode  = g_gndDampGripMode;
    g_dampKeepAbs   = g_gndDampMoveAbs;
    g_dampKeepStand = g_gndDampStand;
    Log(K_INIT, 0x34, (DWORD)g_gndDampMoveAbs,
        (DWORD)((DWORD)g_dampGripIdx | ((DWORD)g_gndDampStandIdx << 8)
                | ((DWORD)g_gndDampGripMode << 16)),
        (DWORD)g_gndDampStand);
}

static void DampApplyGrip(void)
{
    g_gndDampMoveAbs = g_dampGripBank[g_dampGripIdx];
}

/* ---- v811: SIX KEYS, his own layout, 2026-09-14 ----------------------------------------------
   two of them to run the damper ladder before breakaway... and also a ladder of two keys
   on the static damper, to run along it too and report which is liked...
   F5 to reset everything to the settings with the damper fixed, and F6 to reset the settings to how
   driving is set up - the latest references now.

     F1 / F2   the damper while the tyres HOLD (driving straight) - one step down / up, ten steps
               500 1000 1500 2000 [2625] 3250 4000 4750 5500 6500
     F3 / F4   the damper at a STANDSTILL - one step down / up, ten steps
               2000 [4000] 6000 8000 10000 12000 14000 16000 18000 20000
     F5        everything to the FIXED damper: the grip fix on, 2625 holding, 20000 parked
     F6        everything to the feel he is driving TODAY: the v810 arithmetic, bit for bit

   The slide is NOT on a key. His words the same day: after the breakaway he wants no damper at
   all and likes what he has, so `damp_slip` stays at 78 and this build does not offer to move it.

   The wheel answers every press, because he cannot see a number while he drives: one pulse per
   rung he has landed on, and the OFF pattern - two short taps - when the ladder has no further
   rung that way. F5 answers with three pulses and F6 with five, so the two resets cannot be
   confused with each other or with a rung.

   THE PRESET BANK GIVES UP F1..F5 FOR THIS. It is not a loss: since v810 all five restore the
   same approved base, so those keys assert one thing five times. U I O P keep the curve bank. */
/* v814: the roll bank's keys. One key per state, never a cycle - the same shape as every other
   bank here, and for the same reason: he cannot see which state he is in, so a key has to ASSERT
   one rather than advance. The wheel answers with that many pulses. Every press is logged (0x35)
   including one that changes nothing, because a press that produced no row is a press the module
   never saw, which is a different fault from a state he could not tell apart. */
static void RollSelect(int idx)
{
    if (idx < 0 || idx >= ROLL_PRESETS) return;
    g_rollPresetCur = (LONG)(idx + 1);
    g_rollGainPct   = g_rollBankGain[idx];
    g_rollCap       = g_rollBankCap[idx];
    g_rollKeyUsed   = 1;
    if (g_gndAckMag > 0) {
        g_gndAckPat = 2;
        g_gndAck    = (LONG)(idx + 1) * 20;
    }
    Log(K_INIT, 0x35, (DWORD)g_rollPresetCur, (DWORD)g_rollGainPct, (DWORD)g_rollCap);
}

static void PollRollKeys(void)
{
    static int down[ROLL_PRESETS];
    static const int vk[ROLL_PRESETS] = { 0x70, 0x71, 0x72 };   /* F1 F2 F3 */
    int i;
    if (!g_rollKeys) return;
    for (i = 0; i < ROLL_PRESETS; i++) {
        int now = KeyDown(vk[i]);
        if (now && !down[i]) RollSelect(i);
        down[i] = now;
    }
}

static void CrashSelect(int idx)
{
    if (idx < 0 || idx >= CRASH_PRESETS) return;
    g_crashPresetCur = (LONG)(idx + 1);
    g_crashTrimPct   = g_crashBankTrim[idx];
    g_crashCap       = g_crashBankCap[idx];
    g_crashRampLo    = g_crashBankRampLo[idx];
    g_crashRampHi    = g_crashBankRampHi[idx];
    g_crashKeyUsed   = 1;
    if (g_gndAckMag > 0) {
        g_gndAckPat = 2;
        g_gndAck    = (LONG)(idx + 1) * 20;
    }
    Log(K_INIT, 0x36, (DWORD)g_crashPresetCur, (DWORD)g_crashTrimPct, (DWORD)g_crashCap);
    /* v820: 0x3A - the speed ramp of the state just selected: a = state, b = ramp start km/h,
       c = ramp end km/h (0 / 0 = flat). Its own code, so 0x36 keeps the meaning it had. */
    Log(K_INIT, 0x3A, (DWORD)g_crashPresetCur, (DWORD)g_crashRampLo, (DWORD)g_crashRampHi);
}

static void PollCrashKeys(void)
{
    static int down[CRASH_PRESETS];
    static const int vk[CRASH_PRESETS] = { 0x73, 0x74, 0x75 };   /* F4 F5 F6 */
    int i;
    if (!g_crashKeys) return;
    for (i = 0; i < CRASH_PRESETS; i++) {
        int now = KeyDown(vk[i]);
        if (now && !down[i]) CrashSelect(i);
        down[i] = now;
    }
}

/* v822 debug: see g_rangeKeys. The wheel answers with 1..5 pulses so he knows which range the
   module now assumes; 0x3D a = degrees, b = the key (1..5), c = the resulting DorScale x1000. */
static void RangeSelect(int idx)
{
    int s;
    if (idx < 0 || idx >= RANGE_KEYS_N) return;
    for (s = 0; s < N_DOR_SLOTS; s++) {
        if (g_dorDegTab[s] != g_rangeKeyScaleAs[idx]) continue;   /* v823: the slot that SCALES */
        g_dorSlot      = s;
        g_dorNow       = g_rangeKeyDeg[idx];                      /* the degrees he set, logged */
        g_dorTrim      = 100;
        g_wallowTrim   = 100;
        g_rangeKeyUsed = 1;
        if (g_gndAckMag > 0) {
            g_gndAckPat = 2;
            g_gndAck    = (LONG)(idx + 1) * 20;
        }
        Log(K_INIT, 0x3D, (DWORD)g_dorNow, (DWORD)(idx + 1), (DWORD)(LONG)(DorScale() * 1000.0f));
        return;
    }
}

static void PollRangeKeys(void)
{
    static int down[RANGE_KEYS_N];
    int i;
    if (!g_rangeKeys) return;
    for (i = 0; i < RANGE_KEYS_N; i++) {
        int now = KeyDown(g_rangeKeyVk[i]);
        if (now && !down[i]) RangeSelect(i);
        down[i] = now;
    }
}

static void PollDamperKeys(void)
{
    static int down[6] = { 0, 0, 0, 0, 0, 0 };
    static const int vk[6] = { 0x70, 0x71, 0x72, 0x73, 0x74, 0x75 };
    int now[6];
    int i;
    /* OFF BY DEFAULT SINCE ours-034 SETTLED BOTH LADDERS. Instruction: leave what is there for now,
       6000, and the damper is not touched any more. A ladder left live on F1..F4 turns one stray F-key
       into a silent change of the value he approved, and the log would record the new number as
       though he had chosen it. `ground_damp_keys = 1` in the ini brings all six back for the next
       time a damper question needs his hands - nothing was deleted. */
    if (!g_gndDampKeys) return;
    for (i = 0; i < 6; i++) now[i] = KeyDown(vk[i]);

    if (now[0] && !down[0]) {                       /* F1: holding damper one step DOWN */
        int edge = (g_dampGripIdx <= 0);
        if (!edge) g_dampGripIdx--;
        DampApplyGrip(); DampKeyAck((int)g_dampGripIdx + 1, edge); DampLog();
    }
    if (now[1] && !down[1]) {                       /* F2: holding damper one step UP */
        int edge = (g_dampGripIdx >= DAMP_GRIP_STEPS - 1);
        if (!edge) g_dampGripIdx++;
        DampApplyGrip(); DampKeyAck((int)g_dampGripIdx + 1, edge); DampLog();
    }
    if (now[2] && !down[2]) {                       /* F3: standstill damper one step DOWN */
        int edge = (g_gndDampStandIdx <= 0);
        if (!edge) g_gndDampStandIdx--;
        g_gndDampStand = g_dampStandBank[g_gndDampStandIdx];
        DampKeyAck((int)g_gndDampStandIdx + 1, edge); DampLog();
    }
    if (now[3] && !down[3]) {                       /* F4: standstill damper one step UP */
        int edge = (g_gndDampStandIdx >= DAMP_STAND_STEPS - 1);
        if (!edge) g_gndDampStandIdx++;
        g_gndDampStand = g_dampStandBank[g_gndDampStandIdx];
        DampKeyAck((int)g_gndDampStandIdx + 1, edge); DampLog();
    }
    if (now[4] && !down[4]) {                       /* F5: the FIXED damper, all of it */
        g_gndDampGripMode = 1;
        g_dampGripIdx = 4; DampApplyGrip();
        g_gndDampStandIdx = DAMP_STAND_STEPS - 1;
        g_gndDampStand = g_dampStandBank[g_gndDampStandIdx];
        DampKeyAck(3, 0); DampLog();
    }
    if (now[5] && !down[5]) {                       /* F6: back to what he drives today */
        g_gndDampGripMode = 0;
        g_gndDampMoveAbs = 0;                       /* the percent path, i.e. move_pct = 50 */
        g_dampGripIdx = 4;
        g_gndDampStand = 0;                         /* whatever the arithmetic produces: 4399 */
        g_gndDampStandIdx = 1;
        DampKeyAck(5, 0); DampLog();
    }
    for (i = 0; i < 6; i++) down[i] = now[i];
}

/* v7.90. The preset bank's keys. One key per preset, each ASSERTING a whole feel, and the wheel
   answers with as many pulses as the preset's number - so the acknowledgement itself says which
   of the five he is now in, rather than merely that a key arrived. A press is logged even when it
   changes nothing (0x2C + 0x2D), because a press that produced no row is a press the module never
   saw, which is a different fault from a preset he could not feel.

   A preset press also clears the ground channel's I/O override to the preset's own value. Without
   that, an O pressed earlier in the drive would keep the channel silent through every later preset
   press - a key that does nothing, which is the worst kind. */
static void PresetSelect(int idx)
{
    if (idx < 0 || idx >= GND_PRESETS) return;
    g_presetCur   = (LONG)(idx + 1);
    PresetApply(idx);
    g_gndOverride = g_gndOn ? 1 : 0;
    if (g_gndAckMag > 0) {
        /* One pulse per RUNG, not per preset number: with two ladders the ninth preset would
           otherwise pulse for a second and a half, which is long enough to be felt as an effect
           rather than an acknowledgement. F1..F5 pulse 1..5 as before; U I O P pulse 1..4. He
           knows which key he pressed - what the ack has to tell him is that the module saw it
           and which rung it landed on. The log row 0x2C still carries the true preset number. */
        int rung = idx < 5 ? idx + 1 : idx - 4;
        g_gndAckPat = 2;                       /* count the pulses                        */
        g_gndAck    = (LONG)rung * 20;         /* 20 ticks = 160 ms each, out and back    */
    }
    Log(K_INIT, 0x2C, (DWORD)g_presetCur, (DWORD)g_gndOptDeg, (DWORD)g_gndFalloff);
    /* v7.95 0x32 - BOTH AXES AS THEY NOW STAND, in one row. Without it a debrief cannot say which
       breakaway a return verdict was given on, which is the exact confusion this build removes. */
    Log(K_INIT, 0x32, (DWORD)g_curBreak, (DWORD)g_curRetK, (DWORD)g_gndSatK);
    Log(K_INIT, 0x33, (DWORD)(LONG)(g_gndLastKmh * 100.0f),
        (DWORD)(LONG)(g_gndLastSatMult * 1000.0f),
        (DWORD)((g_gndSpdOn ? 1u : 0u) | ((DWORD)g_gndCurveKey << 8)));
    Log(K_INIT, 0x2D, (DWORD)g_gndBend, (DWORD)g_gndPostRatio, (DWORD)g_gndSatK);
    /* v7.92: the damper half of the preset. Its own row rather than crowded into the two above,
       because a log code that changes meaning between builds is a trap already paid for here. */
    Log(K_INIT, 0x31, (DWORD)g_gndDampMovePct, (DWORD)g_gndDampSlip,
        (DWORD)(LONG)(g_gndLastGrip * 10000.0f));
}

/* DOES AN ARMED F-KEY BANK OWN THIS KEY? Then a ground preset bound to the same key stays silent,
   because one press doing two of OUR things is a drive that answers the wrong question.
   A function of its own, and not three lines inside the poll loop, so a host test can ask it
   about every key without a keyboard - the poll itself calls GetAsyncKeyState and cannot be.

   v811: F1..F6 belong to the damper ladders while those are armed.
   v814: F1..F3 belong to the roll bank while THAT is armed.
   v816: F4..F6 belong to the CRASH bank while that is armed - the line v814 forgot. The crash bank
         was added on F4..F6 in the same build as the roll bank, and only the roll bank was given
         its guard. Found 2026-09-30 before ours-035 was driven: the deployed ini binds preset 4 to
         F4 and preset 5 to F5, so F5 would have capped the crashes AND swapped the steering to the
         old reference (breakaway 4 deg instead of 9, SAT 5000 instead of 7875, damper slip 0) for
         the rest of the drive. The wheel answered with the crash bank's pulses, because
         PollCrashKeys runs after this and writes the acknowledgement last - so nothing would have
         told him that a second thing had changed.

   Refused here rather than in the ini, because the ini is written by three different scripts and
   any one of them could put the key back. */
static int FKeyOwnedByBank(int k)
{
    if (g_gndDampKeys && k >= 0x70 && k <= 0x75) return 1;   /* the damper ladders, F1..F6 */
    if (g_rollKeys    && k >= 0x70 && k <= 0x72) return 1;   /* the roll bank, F1..F3      */
    if (g_crashKeys   && k >= 0x73 && k <= 0x75) return 1;   /* the crash bank, F4..F6     */
    if (g_rangeKeys   && k >= 0x70 && k <= 0x74) return 1;   /* v822 debug range keys, F1..F5 */
    return 0;
}

static void PollPresetKeys(void)
{
    static int down[GND_PRESETS];
    int i;
    for (i = 0; i < GND_PRESETS; i++) {
        int k = (int)g_presetKey[i];
        int now;
        if (k < 1 || k > 254) { down[i] = 0; continue; }
        if (FKeyOwnedByBank(k)) { down[i] = 0; continue; }
        now = KeyDown(k);
        if (now && !down[i]) PresetSelect(i);
        down[i] = now;
    }
}

/* v7.88. The same two-key shape for the IMPULSE channel - K asserts it on, L asserts it off -
   so the A/B that settled the ground channel's question can settle this one inside one drive.
   The answer is in the wheel because there is nowhere else: he is driving and Mafia pauses the
   moment it loses focus. Logged on every press (0x2A), even one that changes nothing, so a press
   the module never saw is distinguishable from a change he could not feel. */
static void ImpulseSetEnabled(LONG want)
{
    g_impOverride = want ? 1 : 0;
    if (g_gndAckMag > 0) {
        g_gndAckPat = want ? 0 : 1;
        g_gndAck    = want ? 20 : 25;
    }
    Log(K_INIT, 0x2A, (DWORD)g_impOverride, (DWORD)g_impulseKick, (DWORD)g_impWitness);
}

static void PollImpulseKeys(void)
{
    static int downOn = 0, downOff = 0;
    int on  = KeyDown((int)g_impKeyOn);
    int off = KeyDown((int)g_impKeyOff);
    if (on  && !downOn)  ImpulseSetEnabled(1);
    if (off && !downOff) ImpulseSetEnabled(0);
    downOn = on; downOff = off;
}

/* GLOB->CAR HUNT (diagnostic, kept for cross-check): the collision-loop hook (0x5E2061) is not firing this session, but the
   global at 0x65115C IS populated. Hunt a car struct reachable from it by signature:
   +0x2A0C = speed in [0.05,40], +0x38B0 = steer in [-1.2,1.2], +0x2A18 = world X (|..|>1).
   Logs candidates (K_INIT 0x60: a=source, b=candidate ptr, c=speed*100, d=steer*1000).
   Source encoding: 0xFFFFFFFF = glob itself; else = byte offset within glob where the
   pointer was found. While driving, the real car's speed is nonzero and tracks the pedal. */
static void GlobCarHunt(void)
{
    DWORD glob = *(volatile DWORD *)VA_GAMEPTR;   /* 0x65115C */
    if (!Sane(glob)) return;

    /* read glob+0..0x800 as a block of candidate pointers */
    static BYTE gbuf[0x800];
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)glob, &mbi, sizeof(mbi)) != sizeof(mbi)) return;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return;
    DWORD regionEnd = (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize;
    DWORD avail = regionEnd - glob;
    DWORD n = avail < sizeof(gbuf) ? avail : sizeof(gbuf);
    memcpy(gbuf, (const void *)glob, n);

    int logged = 0;
    for (int i = -1; (DWORD)(i * 4 + 4) <= n && logged < 8; i++) {
        DWORD cand;
        DWORD src;
        if (i < 0) { cand = glob; src = 0xFFFFFFFFu; }        /* test glob itself */
        else       { cand = *(DWORD *)(gbuf + i * 4); src = (DWORD)(i * 4); }
        if (!Sane(cand)) continue;
        float spd, steer, wx;
        if (!SafeReadF(cand + 0x2A0Cu, &spd))   continue;
        if (spd < 0.05f || spd > 40.0f)         continue;
        if (!SafeReadF(cand + 0x38B0u, &steer)) continue;
        if (steer < -1.2f || steer > 1.2f)      continue;
        if (!SafeReadF(cand + 0x2A18u, &wx))    continue;
        if (wx > -1.0f && wx < 1.0f)            continue;      /* need a real world position */
        if (wx < -1e6f || wx > 1e6f)            continue;
        Log(K_INIT, 0x60, src, cand,
            (DWORD)(LONG)(spd * 100.0f));
        logged++;
    }
}

/* ---- v7.5 MARK keys ----------------------------------------------------------
   The whole F3 problem is that the archived data is UNLABELLED: we can see flag fields
   twitch but cannot tell which twitch flagged a pedestrian being run over. Reaction time
   does not matter much - the logs keep full context around every tick, so a press up to
   a second late still identifies the event.

   Log 0x74: a = mark id, b = press counter. Same GetTickCount timebase as car_snap.bin
   and the flag watch, so offline every mark slices straight into all three logs.

   Top-row digits (this keyboard has no numpad):
   5 = pedestrian     6 = light object (box/bin/phone booth)
   7 = rammed hard    8 = gentle push/nudge
   9 = anything else worth looking at

   v7.8b - WEAPON-SLOT CONFOUND, found in the v7.7 drive: 1-0 are the game's weapon keys,
   and that drive logged 43 bullet taps, i.e. the user was shooting a lot. Two of its three
   "marks" landed on nothing at all (no tap, no crash) - stray weapon presses that look
   exactly like deliberate labels. That corrupts precisely the measurement a labelled drive
   exists to make. So each mark now ALSO has an alias key that no game action uses; either
   key logs the same id, and the log records which one was pressed (so strays stay
   identifiable). Aliases:  [ = ped   ] = object   ; = rammed hard   ' = gentle   \ = other

   v7.18 - THIRD BANK, LETTERS. The punctuation aliases are unusable in practice: the user
   types on a Russian layout, where [ ] ; ' \ sit under different legends and he cannot find
   them while driving. Letter keys do not have that problem, because GetAsyncKeyState works
   on VIRTUAL-KEY codes and Windows maps the LETTER keys by POSITION - the physical key that
   prints Cyrillic ghe on a Russian layout still reports VK 0x55 ('U'). So the letter bank is
   layout-proof, which neither of the other two banks is.
   The letter bank carries its OWN ids 6..10, disjoint from the old 1..5, so no archived
   analysis script can silently mistake a new mark for "pedestrian" or "light object".
   F7 GUNFIRE DRIVE meaning:  U(6) = incoming hit ON the car (each police shot),
   I(7) = MY OWN shot fired from inside the car (the negative control that killed 0x29E4),
   O(8) = segment marker / anything else.  P(9), J(10) spare. */
#define MARK_N 15
static const int g_markVk[MARK_N] = {
    0x35, 0x36, 0x37, 0x38, 0x39,          /* 5 6 7 8 9 - as before            */
    0xDB, 0xDD, 0xBA, 0xDE, 0xDC,          /* [ ] ; ' \ - alias, never in-game */
    'U',  'I',  'O',  'P',  'J'            /* letter bank - layout-proof       */
};
static const int g_markId[MARK_N] = { 1,2,3,4,5,  1,2,3,4,5,  6,7,8,9,10 };

/* v7.65: THE MARK BANKS ARE RETIRED. Reported 2026-08-07: the ones for the FFB marks are not needed.
   Clear them, they can be reused.
   Fifteen keys went with them - the digits 5-9, the punctuation aliases [ ] ; ' \ that were
   already known unusable on a Russian layout, and the letter bank U I O P J. F7 and F8 are the
   only marks he actually uses, and they are untouched.
   0 rather than deletion, like every other retired key block here, so archived logs carrying
   0x74 still have the code that wrote them. */
#define MARK_KEYS 0

static void PollMarks(void)
{
#if MARK_KEYS
    static int down[MARK_N];
    static DWORD count = 0;
    for (int i = 0; i < MARK_N; i++) {
        int now = (GetAsyncKeyState(g_markVk[i]) & 0x8000) != 0;
        /* d = which bank pressed it: 0 = digit, 1 = punctuation alias, 2 = letter alias,
           so the offline pass can tell a deliberate mark from a weapon-key stray */
        if (now && !down[i])
            Log(K_INIT, 0x74, (DWORD)g_markId[i], ++count, (DWORD)(i / 5));
        down[i] = now;
    }
#endif  /* MARK_KEYS */
}

/* v7.24: profile switching, friction trim, and the LIKE marker.
     F1..F4      select a feel profile          -> 0xC3 (profile, trim, effective friction)
     F5          "THIS is what I want"          -> 0xCF (profile, trim, effective friction)
     F6          "this is wrong"                -> 0xD4 (same three fields)
     PageUp/Dn   trim the active friction +-20% -> 0xC3, same record shape
   F5 is the point of the whole build: he switches and trims until something feels right and
   presses it, and afterwards the numbers say what "right" was. It logs the EFFECTIVE
   saturation actually on the wheel at that moment (profile x trim x speed fade x vehicle
   class), not the profile's nominal value - those differ, and the nominal one would be a
   confident wrong answer. Vehicle class and speed come from the 0xCE and 0x70 records at the
   same tick. */
/* ---- v7.32-DOR: the profile ladder is RETIRED, F1..F5 are ROTATION-RANGE LABELS ----
   He drove the eight v7.30 profiles and chose F1; v7.31 folded that choice together with the
   soft slide nudge into the one build he approved and asked that it become the base with no
   racing profiles left to pick. So g_prof_i is PINNED to 0 and no key can move it. Everything
   downstream still reads g_prof[0], so the FEEL of this build is v7.31-on-F1 exactly.

   THE MOD CANNOT SET OR READ THE WHEEL'S ROTATION RANGE. DirectInput has no such property;
   the range lives in the CMagic driver on his second monitor and he changes it by hand. The
   key press is therefore the ONLY way the log ever learns which range was live, and an
   unpressed key makes that whole stretch of the drive unattributable. Slider first, key
   second, every time.

   0xD5: a = degrees, b = slot 1..5, c = raw steering axis at that instant. It changes nothing
   about the forces - it is a label and only a label.

   PageUp/PageDown went with friction: every surviving profile has fricStatic 0, so the trim
   scaled zero by a percentage and did nothing but write a misleading number into the log.

   The keys cover the FIRST FIVE slots only - 90/360/600/900/1080. The three added later for the
   tuner utility (1440, then 540 and 720) are reachable from the INI and nowhere else: F6 is
   already "new run starts here" and the loop below is deliberately bounded at 5 so that adding
   a slot can never silently rebind a key an archived drive was recorded with. */
/* ---- v7.61, 2026-08-06: THE F1..F5 RANGE KEYS ARE GONE. Reported: testing steering wheel
   angles served no purpose any more, and it was decided explicitly that the range keys
   should stop changing the wheel range mid-drive: F1 and F5 no longer do that.

   Not merely a label, which is why removing it is a FEEL change and has to be said out loud:
   the key wrote `g_dorSlot`, and `DorScale()` reads that slot to multiply every kick (0.510 at
   90 deg against the 600 deg reference), `g_kickFloor[g_dorSlot]` picks the per-range floor, and
   the press also reset `g_dorTrim` and `g_wallowTrim` to 100. So a stray F-key press in the
   middle of a measured drive rescaled every force from that moment on and the log only said so
   if the press was deliberate.

   NOTHING IS LOST FROM THE LOG. The range now comes from `[ffb] range` in the ini, exactly as it
   did before the first key was ever pressed, and `FfbLoadSettings` already writes it out at
   startup as `K_SETTINGS 0xBAD` (a = the requested degrees, b = what was accepted, c = the slot).
   That record is what an offline pass should read; `0xD5` from this function is now never
   written and its absence is not a defect.

   The loop is kept behind a 0 rather than deleted so the archived drives that DO carry `0xD5`
   still have their producing code to be read against. */
#define DOR_RANGE_KEYS 0

static void PollFeelKeys(void)
{
    /* F7/F8 = marks. NOT F9/F10 - the shifter's AHK script binds those as emergency hotkeys
       (`F9::`, `F10::`) and its keyboard hook swallows them, which is why every mark he pressed
       on the v7.30 drive is missing from that log. */
    static int downLike, downNope, downRun, downUp, downDn, downRs;

#if DOR_RANGE_KEYS
    static const int vk[5] = { 0x70, 0x71, 0x72, 0x73, 0x74 };   /* VK_F1..VK_F5 */
    static int downD[5];

    for (int i = 0; i < 5; i++) {
        int now = (GetAsyncKeyState(vk[i]) & 0x8000) != 0;
        if (now && !downD[i]) {
            g_dorNow  = g_dorDegTab[i];
            g_dorSlot = i;
            /* v7.33b: a range switch RESETS the trim to its baseline, on his instruction. Two
               reasons it matters: a trim carried over from the previous range would silently
               shift the new one, and a mark pressed after the switch would then stamp a value
               he never chose for THIS range. Pressing the same key again is therefore also an
               "undo the trim" gesture, which is why there is no "already selected" guard.
               The reset happens BEFORE the log line so the scale recorded is the one now in
               force, not the one that just expired. */
            g_dorTrim    = 100;
            g_wallowTrim = 100;   /* v7.35: both trims reset, same reason */
            /* d carries the EFFECTIVE scale x1000, so the log never has to re-derive it from
               the range and the trim - the two could drift apart in a later build. */
            Log(K_INIT, 0xD5, (DWORD)g_dorDegTab[i], (DWORD)(i + 1),
                (DWORD)(LONG)(DorScale() * 1000.0f));
        }
        downD[i] = now;
    }
#endif

/* v7.65: THE LIVE DETECTION GATE IS RETIRED. Reported 2026-08-07: page up, page down, end -
   remove the force feedback by road-detection too, it is not needed.
   Kept behind a 0 rather than deleted, exactly as DOR_RANGE_KEYS and RUN_BOUNDARY_KEY are, so
   archived logs carrying 0xDA and 0xDF still have the code that produced them. `g_gateFrac`
   itself stays and keeps its baked default - only the keys go.
   This also frees the whole nav cluster, which mattered immediately: the camera's scene dump
   had been put on END, and END was this block's RESET, so one keypress would have re-baked the
   detector's sensitivity in the middle of the drive that measures it. */
#define GATE_KEYS 0
#if GATE_KEYS
    int up = (GetAsyncKeyState(0x21) & 0x8000) != 0;    /* VK_PRIOR */
    int dn = (GetAsyncKeyState(0x22) & 0x8000) != 0;    /* VK_NEXT  */
    int rs = (GetAsyncKeyState(0x23) & 0x8000) != 0;    /* VK_END - reset to default */
    /* v7.40: the force is the locked reference curve and has NO knob. The keys tune only the
       DETECTION sensitivity - the entry-speed gate fraction - so a missed low-speed hit can be
       coaxed in live without ever touching the force. PageUp = catch MORE (smaller share of the
       entry speed required), PageDown = stricter, End = back to the baked 20%.
       0xDA: b = gate%, c = what it demands at 3 m/s x1000, d = range. */
    if (up && !downUp) {
        g_gateFrac -= 5; if (g_gateFrac < 10) g_gateFrac = 10;
        Log(K_INIT, 0xDA, (DWORD)g_gateFrac, (DWORD)(LONG)(CrashDropNeeded(3.0f)*1000.0f), (DWORD)g_dorNow);
    }
    if (dn && !downDn) {
        g_gateFrac += 5; if (g_gateFrac > 100) g_gateFrac = 100;
        Log(K_INIT, 0xDA, (DWORD)g_gateFrac, (DWORD)(LONG)(CrashDropNeeded(3.0f)*1000.0f), (DWORD)g_dorNow);
    }
    if (rs && !downRs) {
        g_gateFrac = GATE_FRAC_DEF;
        Log(K_INIT, 0xDF, (DWORD)g_gateFrac, (DWORD)(LONG)(CrashDropNeeded(3.0f)*1000.0f), (DWORD)g_dorNow);
    }
    downUp = up; downDn = dn; downRs = rs;
#endif  /* GATE_KEYS */

    /* ---- v7.61: F6 "NEW RUN STARTS HERE" IS RETIRED TOO. Reported 2026-08-06: settled on not starting a new
       run from here at all, taking it instead from the recorded voice, what happened and how.
       The key existed because he replays the same mission three times from the menu and all
       three land in ONE log file, so a restart had to be marked. His voice already marks it -
       every drive is recorded, and on the v7.59 drive he said "restarting the run" out loud
       three separate times, each of them a cleaner boundary than a keypress because it carries
       WHY. The indirect signal is still there as a cross-check: the car pointer changes and the
       world position jumps (do not read that jump as speed - it was misread once as 438 m/s).
       `0xD7` is therefore never written from here on. F6 belongs to the camera's seat now.

       The old body is kept behind a 0 so archived logs that DO carry `0xD7` still have their
       producing code to be read against. */
#define RUN_BOUNDARY_KEY 0

#if RUN_BOUNDARY_KEY
    static LONG s_runNo = 0;
    int run = (GetAsyncKeyState(0x75) & 0x8000) != 0;   /* VK_F6 = "new run starts here" */
    if (run && !downRun) {
        s_runNo++;
        Log(K_INIT, 0xD7, (DWORD)s_runNo, (DWORD)g_dorNow, (DWORD)g_axRaw[0]);
    }
    downRun = run;
#else
    (void)downRun;
#endif

    /* The marks now stamp the RANGE where they used to stamp the profile index: the profile is
       a constant in this build and the range is the only thing that varies across the drive.
       90/360/600/900/500 cannot be mistaken for the old 1..6 profile numbers if an old and a
       new log are ever read by the same script. Field b carries the axis so a mark is also a
       position sample - useful for marking that the wheel is at the stop again. */
    /* v7.33: a mark stamps the EFFECTIVE SCALE rather than the axis. The scale is the quantity
       under test now, and the axis at that instant is recoverable from the 8 ms K_WHEEL stream
       by timestamp anyway - the scale would not be, once he has trimmed it a few times. */
    /* v7.35: a mark stamps BOTH scales, so it is readable whichever end of the range he is on
       and no later script has to guess which trim was live. */
    /* ---- v7.36: a mark now carries its own CONTEXT ----
       His instruction, 2026-07-24: "I might like a weak crash or a tap on a pedestrian,
       but I might not like a big crash... I want you to always track when
       exactly I tap, on which event, at which force values", and "if I tap
       repeatedly, that means: I like it a lot or I really do not like it".

       Reconstructing this after the fact worked but was not safe: on the 24 July drive 12 of
       32 verdicts had no kick within six seconds, and at least one NOPE was aimed at SILENCE -
       an impact that never fired - while the reconstruction blamed whatever had fired 2.1 s
       earlier. A verdict about a missing effect must never be filed against a present one.

       Three records per press, so nothing has to be inferred later:
         0xCF/0xD4  the verdict: range, range scale, burst index (1, 2, 3... within 2.5 s)
         0xDB       the event:   last kick code, its magnitude, ms since it fired
         0xDC       the state:   speed x100, spring, damper - "standing" vs "moving" is a
                                 different question from "which effect", and he said so */
    static DWORD s_markTick = 0; static int s_burst = 0, s_lastKind = -1;
    int like = KeyDown(0x76);  /* VK_F7 = "this is what I want" */
    int nope = KeyDown(0x77);  /* VK_F8 = "this is wrong"       */
    int fired = (like && !downLike) ? 0xCF : ((nope && !downNope) ? 0xD4 : 0);
    if (fired) {
        DWORD now = GetTickCount();
        if (s_lastKind == (int)fired && (now - s_markTick) <= 2500) s_burst++;
        else s_burst = 1;
        s_lastKind = (int)fired; s_markTick = now;

        Log(K_INIT, fired, (DWORD)g_dorNow, (DWORD)(LONG)(DorScale() * 1000.0f),
            (DWORD)s_burst);
        Log(K_INIT, 0xDB, g_lastKickCode, g_lastKickMag,
            g_lastKickTick ? (now - g_lastKickTick) : 0xFFFFFFFFu);
        Log(K_INIT, 0xDC, (DWORD)g_spdX100, (DWORD)g_curSpring, (DWORD)g_curDamper);
    }
    downLike = like; downNope = nope;
}

static DWORD WINAPI SnapThread(LPVOID p)
{
    (void)p;
    DWORD cyc = 0;
    for (;;) {
        Sleep(SNAP_MS);
        PollMarks();
        PollGroundKeys();
        PollImpulseKeys();
        PollPresetKeys();
        PollDamperKeys();
        PollRollKeys();
        PollCrashKeys();
        PollRangeKeys();
        /* The hunt runs HERE and not in the FFB thread: it walks 16 KB of the actor through
           VirtualQuery-guarded reads, which is a few milliseconds, and the FFB thread has an
           8 ms tick to keep. Once a second, and it stops scanning the moment it has a pointer
           that still passes its signature. */
        if ((cyc % 20u) == 0u) VehHunt();
        if ((cyc % 5u)  == 0u) LogModelInputs();   /* 4 Hz - enough to see them move */
        if (PROFILE_ENABLE) PollFeelKeys();
        DWORD car = GetCarPtr();   /* v7.1: car from the glob, not the (dead) hook */
        if (car) {
            SnapOne(0, car, 0x0000, 0x2000);
            SnapOne(2, car, 0x2000, 0x4000);
            SnapOne(3, car, 0x4000, 0x6000);
            SnapOne(4, car, 0x6000, 0x8000);

            /* v7.19 - F7: FOLLOW THE POINTER. Every F7 scan so far searched car+0x0000..
               0x8000 and every one came back negative, most recently on a drive with 19
               labelled incoming hits. But what the user actually SEES is a bullet HOLE:
               per-part visual damage, which is a property of the render/scene object, not
               of the physics struct. That object is [car+0x58], the LS3DF frame we already
               dereference for the orientation matrix at frame+0x164 - and we have never
               dumped anything else in it.
               The pointer is PERIODICALLY NULLED while driving (it went to 0 for ~4 s twice
               mid-drive; that is why ReadHeadingMatrix guards it), so Sane() here is not
               belt-and-braces - it fires in normal play. Cycles where it is null simply
               write no root-5/6 record, and the offline reader already tolerates a root
               being absent from a cycle.
               SnapOne records `start` in its header, so a frame pointer that MOVES between
               cycles is visible offline instead of silently corrupting the series. */
            DWORD frame = *(volatile DWORD *)(car + FRAME_PTR_OFF);
            if (Sane(frame)) {
                SnapOne(5, frame, 0x0000, 0x2000);
                SnapOne(6, frame, 0x2000, 0x4000);
            }

            /* v7.20 - follow the other two vetted pointers as well, so ONE drive can settle
               all three candidates instead of costing the user a drive each. Diagnostic
               only: no FFB constant is touched, the feel stays v7.17-reference.
               Roots 7/8 = [car+0x54], the thrice-cached object; root 9 = [car+0x59C], the
               begin pointer of the begin/end pair (its end is car+0x5A0, and both raw values
               are already captured by root 0, so the list LENGTH is readable offline without
               any extra logging).
               All four pointers NULL TOGETHER for 8-13% of every drive (measured: 401/4713,
               678/4933, 404/6325, 122/5203 records), so missing records are NORMAL, exactly
               as for roots 5/6. Align roots by the TICK in each record header, never by
               index - they do not share a sample count. */
            DWORD comp = *(volatile DWORD *)(car + COMP_PTR_OFF);
            if (Sane(comp)) {
                SnapOne(7, comp, 0x0000, 0x2000);
                SnapOne(8, comp, 0x2000, 0x4000);
            }
            DWORD lbeg = *(volatile DWORD *)(car + LIST_BEG_OFF);
            if (Sane(lbeg)) SnapOne(9, lbeg, 0x0000, 0x2000);

            if ((cyc % MTX_EVERY) == 0) MatrixLog(car);
        }
        DWORD glob = *(volatile DWORD *)VA_GAMEPTR;
        SnapOne(1, glob, 0x0000, 0x2000);
        /* v7.21 - F9, the in-vehicle gate. Only the FIRST 8 KB of the game global has ever
           been captured, and the exhaustive 2026-07-22 hunt proved the flag is not in it, nor
           anywhere in the car struct, nor in any of the three dereferenced objects. Player
           state belongs in the global rather than in a vehicle, so dump the REST of it.
           SnapOne clips at the end of the committed region, so if the global is smaller than
           0x8000 these simply come back with valid < SNAP_BYTES - harmless, and the header's
           `valid` field says how much was really readable. */
        SnapOne(10, glob, 0x2000, 0x4000);
        SnapOne(11, glob, 0x4000, 0x6000);
        SnapOne(12, glob, 0x6000, 0x8000);
        /* Confirm capture: a=carOff found, b=car ptr, c=speed*100 (varies while driving). */
        if ((cyc % 20) == 0) {
            float spd = 0;
            if (car) SafeReadF(car + SPEED_OFF, &spd);
            Log(K_INIT, 0x51, CAR_PTR_OFF, car, (DWORD)(LONG)(spd * 100.0f));

            /* ---- gog-patcher: why is the LS3DF-frame path dead on this build? ----
               Every channel that needs the frame is silent here: car_mtx_*.bin is 0 bytes
               and car_snap.bin carries no car roots. Three different faults look identical
               from outside, so log the three values that tell them apart.
               K_INIT 0x62: b = g_hookHits    - 0 means the spliced call never executes
                            c = [car+0x58]    - 0 means the frame pointer itself is null
                            d = m[0] x 1000   - garbage means the matrix offset is wrong
               A hook that never fires is a Game.exe problem; a null frame pointer is a car
               struct problem; a bad matrix is an LS3DF layout problem. They need different
               fixes, and guessing between them is how a day disappears. */
            {
                DWORD frame = 0; float m0 = 0.0f;
                if (car) SafeReadD(car + FRAME_PTR_OFF, &frame);
                if (frame && Sane(frame)) SafeReadF(frame + MATRIX_OFF, &m0);
                Log(K_INIT, 0x62, g_hookHits, frame, (DWORD)(LONG)(m0 * 1000.0f));

                /* The 0x62 channel already answered its question: the hook fires and the
                   frame pointer is good, but frame+0x164 reads 0 - so the MATRIX OFFSET is
                   wrong on this LS3DF build. Rather than guess a new constant, find it.
                   Scan the frame object for a 3x3 block that is actually a rotation matrix:
                   unit-length rows, mutually perpendicular, and not the identity (identity
                   is what an untouched default frame holds, so it proves nothing).
                   Both strides are tried because a 4x4 matrix puts its rows 4 floats apart
                   and a packed 3x3 puts them 3 apart - assuming one of those is what made
                   the camera probe fail earlier.
                   K_INIT 0x63: b = offset into the frame, c = stride, d = m[0] x 1000.
                   Logged ONCE per launch: this is a hunt, not telemetry. */
                if (frame && Sane(frame) && !g_mtxScanDone) {
                    g_mtxScanDone = 1;
                    int found = 0;
                    for (DWORD off = 0; off <= 0x200u && found < 6; off += 4) {
                        for (int st = 3; st <= 4 && found < 6; st++) {
                            float m[9]; int ok = 1;
                            for (int r = 0; r < 3 && ok; r++)
                                for (int cc = 0; cc < 3 && ok; cc++)
                                    if (!SafeReadF(frame + off + (DWORD)((r * st + cc) * 4), &m[r * 3 + cc])) ok = 0;
                            if (!ok) continue;
                            if (!MtxOrtho3(m)) continue;
                            if (MtxIdentity3(m)) continue;
                            Log(K_INIT, 0x63, off, (DWORD)st, (DWORD)(LONG)(m[0] * 1000.0f));
                            if (g_mtxNCand < MTX_CAND_MAX) {
                                g_mtxOff[g_mtxNCand] = off;
                                g_mtxStride[g_mtxNCand] = st;
                                g_mtxNCand++;
                            }
                            found++;
                        }
                    }
                    if (!found) Log(K_INIT, 0x64, 0, 0, 0);   /* nothing matrix-shaped here */
                }

                /* Watch every candidate's heading elements while the car drives. The world
                   rotation matrix is the one whose atan2(m[6], m[8]) tracks the direction
                   of travel; a bounding volume or a static local transform will not move.
                   K_INIT 0x65: b = candidate offset, c = m[6] x1000, d = m[8] x1000. */
                if (frame && Sane(frame)) {
                    for (int ci = 0; ci < g_mtxNCand; ci++) {
                        float m6 = 0.0f, m8 = 0.0f;
                        int st = g_mtxStride[ci];
                        SafeReadF(frame + g_mtxOff[ci] + (DWORD)((2 * st + 0) * 4), &m6);
                        SafeReadF(frame + g_mtxOff[ci] + (DWORD)((2 * st + 2) * 4), &m8);
                        Log(K_INIT, 0x65, g_mtxOff[ci],
                            (DWORD)(LONG)(m6 * 1000.0f), (DWORD)(LONG)(m8 * 1000.0f));
                    }
                }
            }
        }
        cyc++;
    }
}

static int CarField(DWORD off, float *out)
{
    DWORD car = g_car;   /* v7.1d: cached once/tick in FFBThread, no per-field re-capture */
    if (!car) return 0;
    return SafeReadF(car + off, out);
}

/* v7.10 guarded block read for the vector watch */
static int ReadWindow(DWORD addr, DWORD *dst, DWORD ndw)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if (addr + ndw * 4u > (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize) return 0;
    const volatile DWORD *s = (const volatile DWORD *)addr;
    for (DWORD i = 0; i < ndw; i++) dst[i] = s[i];
    return 1;
}

static int PtrLike(DWORD v)
{
    return v >= 0x10000u && v < 0x7FFF0000u && (v & 3u) == 0u;
}

static volatile DWORD g_vwCount = 0;

static void VectorWatch(DWORD car, DWORD *shadow, int *init)
{
    DWORD ndw = (VW_HI - VW_LO) / 4u;
    static DWORD cur[(VW_HI - VW_LO) / 4];
    if (!ReadWindow(car + VW_LO, cur, ndw)) return;
    if (!*init) {
        for (DWORD i = 0; i < ndw; i++) shadow[i] = cur[i];
        *init = 1;
        return;
    }
    int emitted = 0;
    for (DWORD i = 0; i < ndw; i++) {
        DWORD o = shadow[i], n = cur[i];
        if (o == n) continue;
        shadow[i] = n;                       /* always resync, even when not logged */
        int interesting;
        if (PtrLike(o) && PtrLike(n)) {
            DWORD d = (n > o) ? (n - o) : (o - n);
            interesting = (d <= VW_MAX_DELTA);
        } else {
            interesting = ((o == 0 && PtrLike(n)) || (n == 0 && PtrLike(o)));
        }
        if (!interesting) continue;
        if (g_vwCount >= VW_BUDGET) continue;
        if (emitted >= VW_PER_TICK) continue;
        g_vwCount++;
        emitted++;
        Log(K_INIT, 0xCA, VW_LO + i * 4u, o, n);
    }
}

/* v7.23 F8: is the current vehicle HEAVY? Voted from the 29 class fields in the scene node.
   Runs only when the scene-node pointer CHANGES (a new vehicle object), so the cost is 29
   guarded dword reads per vehicle, not per tick. Defaults to NOT heavy: a car that wrongly
   felt like a truck would be a regression on the reference feel, while a truck that wrongly
   felt like a car is merely the behaviour of every build before this one. */
static void UpdateVehicleClass(DWORD car)
{
    static DWORD lastFrame = 0;
    static DWORD pendFrame = 0;
    static int   pendTicks = 0;

    if (!car) return;
    DWORD frame = *(volatile DWORD *)(car + FRAME_PTR_OFF);
    if (!Sane(frame)) return;              /* on foot / node dead - keep the last verdict */
    if (frame == lastFrame) return;        /* same vehicle object, already classified     */

    /* v7.24c: WAIT FOR THE POINTER TO SETTLE before reading 8 KB out of the object.
       Precaution, not a proven fix. The 2026-07-23 03:06 crash died 10.2 s into the session
       and the last meaningful record was this function classifying a freshly appeared scene
       node. Game.exe has been crashing with the same signatures since 20 July, including in
       ntdll at this exact offset, so the correlation is probably innocent - but the mechanism
       is real: the pointer can be published before the object is fully constructed, and
       VirtualQuery only proves the PAGE is mapped, never that the OBJECT is finished.
       Costs nothing: the class is needed once per vehicle, not once per tick. */
    if (frame != pendFrame) { pendFrame = frame; pendTicks = 0; return; }
    if (++pendTicks < VCLASS_SETTLE) return;

    /* one guarded probe covering the whole span we are about to read */
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)frame, &mbi, sizeof(mbi)) != sizeof(mbi)) return;
    if (mbi.State != MEM_COMMIT) return;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return;
    if (frame + 0x2000u > (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize) return;

    int votes = 0;
    for (int i = 0; i < VCLASS_N; i++)
        if (*(volatile DWORD *)(frame + kVClassOff[i]) == kVClassHeavy[i]) votes++;

    lastFrame = frame;
    g_heavy = (votes >= VCLASS_NEED) ? 1 : 0;
    Log(K_INIT, 0xCE, frame, (DWORD)votes, (DWORD)g_heavy);
}

/* v7.22 F9: decide whether the player is in a vehicle, from the frame-pointer presence
   fraction over a 1 s sliding window, with dwell so the state cannot flicker.

   FAILS OPEN, and this matters more than the gate itself. The scene node can be DEAD for an
   entire session - it happened once, logs/2026-07-22-v721-ONFOOT-SCHUBERT, where the pointer
   was null for all 192 s and the orientation matrix never changed. A naive gate would then
   read "on foot" forever and leave the wheel permanently light for the whole session, which
   is a far worse bug than the one being fixed. So until the pointer has been seen AT LEAST
   ONCE since launch, the gate stays IN VEHICLE unconditionally. */
static void UpdateVehicleGate(DWORD car, DWORD now)
{
    static BYTE  win[GATE_WIN_TICKS];
    static int   idx = 0, filled = 0, count = 0;
    static int   everSeen = 0;
    static int   want = 1;
    static DWORD wantSince = 0;

    BYTE present = 0;
    if (car) {
        DWORD frame = *(volatile DWORD *)(car + FRAME_PTR_OFF);
        if (Sane(frame)) { present = 1; everSeen = 1; }
    }

    count -= win[idx];
    win[idx] = present;
    count += present;
    idx = (idx + 1) % GATE_WIN_TICKS;
    if (filled < GATE_WIN_TICKS) filled++;

    if (!everSeen) { g_inVehicle = 1; return; }   /* fail open - see the note above */
    if (filled < GATE_WIN_TICKS) return;          /* not enough history yet, hold the state */

    int wantNow = (count * 100 >= GATE_NEED_PCT * GATE_WIN_TICKS) ? 1 : 0;
    if (wantNow != want) { want = wantNow; wantSince = now; }
    if (want != g_inVehicle && (now - wantSince) >= GATE_DWELL_MS) {
        g_inVehicle = want;
        /* 0xCD: gate transitions. a = new state (1 = in vehicle), b = present count in the
           window, c = window size. NOT 0xCA - that is already the value-watch channel, and
           reusing it would make two unrelated things indistinguishable offline. This project
           has been bitten by a log code changing meaning between builds before. */
        Log(K_INIT, 0xCD, (DWORD)want, (DWORD)count, GATE_WIN_TICKS);
    }
}

static DWORD WINAPI FFBThread(LPVOID p)
{
    (void)p;
    Sleep(10000);
    /* v7.4j: retry the WHOLE init on failure. A failed InitFFB (Acquire hang) bails before
       the deadlocking CreateEffect and returns 0; here we recreate the DI stack fresh (old
       globals just leak - safe, a rare path) and try again. A transient Acquire block often
       clears on the next try; good launches still succeed on attempt 0, unchanged. */
    int ok = 0;
    for (int it = 0; it < INIT_TRIES && !ok; it++) {
        if (it) { Log(K_INIT, 0x57, (DWORD)it, 0, 0); g_found = 0; g_dev = 0; g_di = 0; Sleep(1500); }
        ok = InitFFB();
    }
    if (!ok) { Log(K_INIT, 99, 0, 0, 0); return 0; }
    Log(K_INIT, 100, 0, 0, 0);

    /* Single test pulse on ConstantForce to confirm path works */
    SetMag(8000);  Sleep(TEST_PULSE_MS); SetMag(0); Log(K_FIRE, 1, 8000, 0, 0);

    /* Damper starts at static value immediately (will be updated in loop) */
    /* gog-patcher: scaled here rather than inside SetDamper - see the comment there. This is the
       STANDING damper by definition, so it takes the standing percent. No truck factor: the
       vehicle class has not been voted on yet at init, and guessing is worse than a car. */
    SetDamper(Mul(K_DAMP_DI_STATIC, g_pctDampStatic));

    float spdEma = 0;
    float hist[4]     = {0, 0, 0, 0};
    /* v7.37: eight slots, not four. `calm` is evaluated over the OLDER six, so the crash's own
       onset - which lands in the newest two - can no longer declare the road un-calm and veto
       the crash that produced it. See the note at the calm test. */
    float dropHist[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    /* v7.37 persistence-confirmed bypass state */
    DWORD pendTick = 0; float pendDrop = 0.0f, pendEntry = 0.0f;
    /* v7.42: the speedometer's own recent peak, over the SAME 19-tick window the
       ground-speed entry is taken from, so the two are comparable by construction.
       pendEma records it at arm time for the confirm-side diagnostic. */
    float emaRing[GS_SHORT_N] = {0}; int emaRn = 0; float pendEma = 0.0f;
    /* v7.48 phantom cut: the speed the crash started FROM, and when to re-check it. */
    DWORD cutAt = 0; float cutPre = 0.0f;
    int hn = 0, dn = 0;
    int prevDmg = -1;
    DWORD kickUntil = 0;

    /* F1 lateral-slip state (persists across ticks) */
    float posRingX[16] = {0}, posRingY[16] = {0};
    float gsRing[GS_RING_SZ] = {0}; int gsRn = 0;   /* ground-speed history for crash veto */
    /* v7.49: a SECOND spdEma ring. `emaRing` above is v7.42's 19-slot (~300 ms) peak
       window for the entry-sanity test; the shape gate needs ~780 ms, so it gets its own
       rather than lengthening a ring another gate's calibration depends on. */
    float stepRing[EMA_RING_SZ] = {0}; int stepRn = 0;
    int   posCount = 0;
    float slipOffset = SLIP_HEAD_OFFSET;

    /* v7 bump-from-behind state */
    float headingPrev = 0; int haveHeadPrev = 0;
    float disturbPrev = 0;

    /* v7.1g lateral-slip smoothing state (applied mult, EMA) */
    float slipMultSmooth = 1.0f;

    /* v7.4 F2 road-wallow state */
    float r7slow = 0.0f, p1slow = 0.0f; int wallowInit = 0;  /* v7.4f roll(m7)+pitch(m1) HP state */
    float shkP1Prev = 0.0f, shkM7Prev = 0.0f;  /* v7.57 DIAG 0xED: previous tick's HP attitude */
    float entryRingV[ENTRY_RING_SZ] = {0};     /* v7.58: spdEma per tick, for the entry peak  */
    DWORD entryRingT[ENTRY_RING_SZ] = {0};     /* ...and the tick it was taken at              */
    int   entryRn = 0;
    float rollEnv = 0.0f; int slipCool = 0;                   /* v7.4g roll load-envelope (slide jolt fix) */
    float gsDropPrev = 0.0f;                                  /* last tick's ground-speed drop (brake gate) */
    DWORD lastBulletTap = 0;                                  /* v7.4h bullet-tap rate limiter */
    float wallowForce = 0.0f;

    /* v7.23 F6 (PIT): trailing 2 s ring of per-tick heading change and commanded yaw.
       Summing dHead telescopes to the exact net heading change over the window, at any
       sample rate - which is why this is safe where the per-tick bump detector was not.
       pitOverCount is the O(1) form of tracking max |steer| over the window: if no sample in the
       window exceeded PIT_STEER_MAX, the count is zero. */
    float pitHeadRing[PIT_WIN_TICKS], pitExpRing[PIT_WIN_TICKS];
    BYTE  pitOverRing[PIT_WIN_TICKS];
    int   pitIdx = 0, pitFill = 0, pitOverCount = 0;
    float pitSumHead = 0.0f, pitSumExp = 0.0f;
    for (int i = 0; i < PIT_WIN_TICKS; i++) {
        pitHeadRing[i] = 0.0f; pitExpRing[i] = 0.0f; pitOverRing[i] = 0;
    }
    /* v7.25 spin channel: smoothed rate, its output force, and the hold counter */
    float spinRate = 0.0f, spinForce = 0.0f;
    int   spinHold = 0;
    float satForce = 0.0f;      /* v7.28 self-aligning torque channel */
    DWORD sideRearm = 0;        /* v7.25 F10 side-impact rate limit */
    float sideRX[64] = {0}, sideRY[64] = {0};   /* v7.26: own 512 ms position history */
    int   sideN = 0;

    /* v7.1d anti-judder capture: get the car ONCE per tick and hold it across brief
       GetCarPtr() dropouts (the glob pointer is transiently invalid a few % of ticks;
       without a grace window that snapped SetSpring(0)+SetDamper(static) for isolated
       ticks -> ~6 violent kicks/sec = the "very juddery" wheel). */
    DWORD lastCar = 0, lastCarTick = 0;

    /* v7.5 F5 state: how long we have been standing, and the one-kick-per-shove rearm */
    int   f5Hold  = 0;
    DWORD f5Rearm = 0;

    /* v7.6 F3 contact-list state: last seen END pointer, its car, and the tap rate limit */
    DWORD f3Prev = 0, f3Car = 0, f3Last = 0;
    int   f3Have = 0;
    /* v7.11 pedestrian contact state */
    DWORD pedPrev = 0, pedCar = 0, pedLastGrow = 0, pedLastTap = 0;
    int   pedHave = 0, pedCount = 0, pedFired = 0;

    /* v7.10 vector-watch shadow (hunting the pedestrian event) */
    static DWORD vwShadow[(VW_HI - VW_LO) / 4];
    int   vwInit = 0;
    DWORD vwCar  = 0;

    /* v7.9 tap envelope state: shaped per tick while the tap is the active kick */
    DWORD f3TapStart = 0, f3TapDur = 0, f3TapAtk = 0;
    LONG  f3TapPeak = 0; int f3TapSign = 1; int f3TapSmooth = 0;
    /* v7.6: set while the ACTIVE kick is only a light F3 tap. A real crash is allowed to
       preempt a tap - without this, tapping a phone booth would deafen the wheel to a
       crash for F3_TAP_MS, and crashes are the effect this project protects hardest. */
    int   kickIsTap = 0;
    /* v7.41: set while the active kick is the SOFT brake/slide nudge. A real sharp crash may
       preempt it exactly as it preempts a tap - the 267.4/328.4 s pairs showed the nudge's
       130 ms hold DELAYING a genuine impact by 218 ms, which he felt as a separate late shove
       (recorded as a delayed jolt). Preemption fires the crash at the moment of the crash. */
    int   kickIsSoft = 0;

    for (;;) {
        Sleep(POLL_MS);
        DWORD now = GetTickCount();

        /* M1: hot reload, gated to ~1/s - GetFileAttributesExA is a real syscall and this
           loop ticks every POLL_MS (8ms). */
        {
            static DWORD s_lastSettingsCheck = 0;
            if (now - s_lastSettingsCheck >= 1000) {
                CheckFFBSettingsReload();
                /* M1b: same 1 Hz tick. WriteFFBStatus suppresses its own write unless the
                   state changed or 10 s passed, so this is a compare, not a file write. */
                WriteFFBStatus();
                s_lastSettingsCheck = now;
            }
        }

        /* ---- the ground channel's own setup, once, then its ini once a second ----
           The detour is installed from the FFB thread rather than from DllMain: the game's code
           is certainly mapped by the time forces are being computed, and a patch attempted too
           early is the kind of failure that reports success. The result is logged either way -
           `a=0x68, b=1` means the call site was this build's and the redirect took.

           0x68 AND NOT 0xE0, which is what this line carried when it was written: 0xE0 on the
           INIT channel is already the v7.41 impulse veto (`:5717`), and a code with two producers
           is what cost the v7.64 rollback - 1091 records read as one thing when 10 of them were
           ([[tear-guard-refuted]], [[log-field-trap]]). One code, one meaning.

           0x69 / 0x6A are the heartbeat, and they fire every second whether or not the channel
           did anything: `reads` staying at 0 with a valid `veh` pointer is a different failure
           from `veh` never arriving, and both are invisible in the wheel. */
        {
            static int   s_gndInit = 0;
            static DWORD s_gndIni  = 0;
            if (!s_gndInit) {
                s_gndInit = 1;
                GroundReadIni();
                Log(K_INIT, 0x68, (DWORD)PatchVehicleTick(), VA_TICK_CALL, (DWORD)g_gndOn);
                /* Which key is live this session, in the log, because the answer "he pressed it
                   and nothing happened" has two causes and only one of them is the channel. */
                Log(K_INIT, 0x6C, (DWORD)g_gndKeyOn, (DWORD)g_gndKeyOff, (DWORD)g_gndAckMag);
                /* v7.90: the preset bank AS LOADED, one pair of rows each. Without this, a drive
                   where he says "the third one" can only be read against whatever the ini says
                   today - and the ini is edited between drives, sometimes during them. */
                {
                    int pi;
                    for (pi = 0; pi < GND_PRESETS; pi++) {
                        Log(K_INIT, 0x2E, (DWORD)(pi + 1), (DWORD)g_presetKey[pi], (DWORD)g_presetOpt[pi]);
                        Log(K_INIT, 0x2F, (DWORD)g_presetBend[pi], (DWORD)g_presetFall[pi], (DWORD)g_presetSatK[pi]);
                    }
                }
                s_gndIni = now;
            } else if ((now - s_gndIni) > 1000u) {
                s_gndIni = now;
                GroundReadIni();          /* live tuning: edit the ini, feel it next second */
                /* v7.90: which preset is live, every second. A press logs 0x2C once; this row is
                   what lets ANY stretch of the drive be attributed, including the stretch before
                   he pressed anything and any stretch where a press was missed. */
                Log(K_INIT, 0x30, (DWORD)g_presetCur, (DWORD)g_gndOptDeg, (DWORD)g_gndBend);
                /* v7.95: the two axes beside it, every second, so a stretch can be attributed to
                   a PAIR rather than to the last key pressed. */
                Log(K_INIT, 0x32, (DWORD)g_curBreak, (DWORD)g_curRetK, (DWORD)g_gndSatK);
                /* v7.96: the speed curve as it stood this second - the speed it read, the
                   multiplier it produced x1000, and whether the curve is on at all. Without
                   this row a verdict on the steering weight cannot be attributed to a speed,
                   which is the whole question the curve exists to answer. */
                Log(K_INIT, 0x33, (DWORD)(LONG)(g_gndLastKmh * 100.0f),
                    (DWORD)(LONG)(g_gndLastSatMult * 1000.0f),
                    (DWORD)((g_gndSpdOn ? 1u : 0u) | ((DWORD)g_gndCurveKey << 8)));
                Log(K_INIT, 0x69, (DWORD)g_gndReads, g_vehTick,
                    (DWORD)(LONG)(g_gndLastLoad * 10000.0f));
                Log(K_INIT, 0x6A, (DWORD)g_gndCalls, (DWORD)g_gndAirTicks,
                    (DWORD)(LONG)g_gndLastForce);
            }
        }

        /* ---- DID THE DEVICE TAKE THE LAST FORCES? If not, take the wheel back. --------------
           Drive ours-019 ended with thirty-five seconds of a wheel that received nothing while
           this loop went on computing. The refusals are counted in SetMag; this is the only place
           that acts on them, it is rate-limited, and it never runs unless the refusals are
           CONSECUTIVE - a single failure during a mode change is not a lost device. */
        {
            static DWORD s_lastRecov = 0;
            DWORD nowR = GetTickCount();
            if (RecoverDue(g_effFailN, s_lastRecov, nowR)) {
                s_lastRecov = nowR;
                DeviceRecover();
            }
        }

        /* ---- v7.32-DOR: read the wheel back, every tick, before anything else ----
           Unconditional on purpose. The car pointer gates every FORCE in this loop, but the
           wheel has a position whether or not he is seated, and a range test wants the menu
           and on-foot stretches too - that is where an un-commanded wheel shows its own
           idle behaviour with no force of ours on top.

           NEVER call Acquire from here. An alt-tab to the CMagic app can lose the device, and
           re-acquiring on the FFB thread is how this project deadlocked itself in v7.4j. A
           failure just marks the sample bad and the existing init path does the recovery. */
        {
            LONG ax[3] = { 0, 0, 0 };
            HRESULT axhr = -1;
            if (g_dev)
                axhr = ((GetDeviceState_t)VT(g_dev, DEV_GETDEVICESTATE))(g_dev, 12, ax);
            LONG okNow = (axhr >= 0) ? 1 : 0;
            if (okNow) { g_axRaw[0] = ax[0]; g_axRaw[1] = ax[1]; g_axRaw[2] = ax[2]; }
            if (okNow != g_axOk) {          /* transitions only - this must not spam */
                g_axOk = okNow;
                Log(K_INIT, 0xD6, (DWORD)okNow, (DWORD)axhr, (DWORD)g_dorNow);
            }
            /* a,b,c = the three raw axes (which one is the wheel is identified offline),
               d = the constant force COMMANDED at this instant. Pairing those two columns is
               the only objective measurement this drive can produce: the same commanded
               impulse displaces the axis differently at 90 vs 900 degrees. */
            /* Full 8 ms rate while seated; 1 in 8 (64 ms) in menus and on foot. He replays one
               mission three times without closing the game, so the menu stretches land in the
               SAME log as the driving and would otherwise eat the record budget for nothing. */
            static DWORD s_axn = 0;
            if (okNow && (g_inVehicle || ((++s_axn & 7u) == 0)))
                Log(K_WHEEL, (DWORD)ax[0], (DWORD)ax[1], (DWORD)ax[2],
                    (DWORD)g_cf.lMagnitude);
        }

        /* one capture per tick + grace hold */
        DWORD carNow = GetCarPtr();
        if (carNow) { lastCar = carNow; lastCarTick = now; }
        DWORD car = carNow ? carNow
                  : ((now - lastCarTick) < CAR_GRACE_MS ? lastCar : 0);
        g_car = car;              /* damage/heading/F1/CarField all read this cached ptr */
        g_car_tick = now;

        UpdateVehicleGate(car, now);   /* v7.22 F9 - sets g_inVehicle */
        if (VCLASS_ENABLE) UpdateVehicleClass(car);   /* v7.23 F8 - sets g_heavy */

        float spd = 0, steer = 0;
        int haveCar = (car != 0) &&
                      SafeReadF(car + SPEED_OFF, &spd) &&
                      SafeReadF(car + STEER_OFF, &steer);

        /* ---- v7.64: DROP THE TORN SAMPLE. This is not a filter on the consequences ----
           `car+0x2A0C` is written TWICE per car update (FUN_005a51c0, GOG 0x5A51C0):
             0x5A558C  fsts   0x2a0c(%ebp)   <- the raw DISTANCE, in metres
             0x5A55AE  fstps  0x2a0c(%ebp)   <- distance * 1000/dt, the real speed
           We poll from our own thread with no synchronisation, so we sometimes read the
           intermediate and see metres where we expect metres per second. Confirmed in the
           listing, in Ghidra's decompilation and by arithmetic: four logged dropouts across
           two drives all sit at exactly dt/1000 of their neighbours (15.5, 15.9, 15.9,
           18.4 ms). See docs\ROOT-CAUSE-SPEED-FIELD.md.

           A THRESHOLD ALONE IS NOT ENOUGH, and that was measured rather than assumed.
           Priced over three drives, 6004 samples (tests\offline\tear_fix_verify.py): the
           worst tear sits at ratio 0.035 and the steepest GENUINE one-tick collapse at 0.063
           - a crash he pressed F7 on. That is 1.8x of total room, so any single threshold is
           a squeeze, and squeezing this one would eventually delete an approved crash.

           SO THE TEST IS RECOVERY, WHICH IS EXACT. A tear is the intermediate of one store;
           the very next poll reads the settled value and the series is back where it was. A
           real impact stays down - every one of the ten steepest in the archive reads
           `14.61 -> 1.43 -> 1.43`, never recovering. So a suspicious sample is HELD for one
           tick and judged on the next:
             recovered -> it was a tear, and nothing was ever fired on it;
             still down -> it was real, and it fires 15 ms late out of a 161 ms kick.
           Normal samples never enter the band, so ordinary driving and ordinary crashes take
           no latency at all. `tear_fix` is the width of the suspicion band, not a verdict.

           WHY THIS IS NOT A TENTH PATCH: it removes the bad INPUT rather than compensating
           for its output. Every filter from v7.3 to v7.63 exists because this sample reached
           the detector. They come out next, one at a time, each replayed over the archive.

           Fails OPEN in the sense that matters: if the guard is off (`tear_fix = 0`) the old
           value passes through untouched, so v7.63 behaviour is recoverable byte for byte. */
        /* ---- v7.64 CONTACT PROBE: ask the ENGINE what it hit, logging only ----------
           `FUN_005a8e70` (GOG 0x5A8E70), the car's collision-response solver, walks a plain
           begin/end vector of contact records on the car and pops them from the end:
               begin = [car+0x1D8]   end = [car+0x1DC]   stride 0x5C   count = (end-begin)/0x5C
           Per record, read off that function: position at +0x38..0x40, a vector at
           +0x44..0x4C, a float at +0x18 the solver thresholds on, an effect id at +0x54, a
           material index at +0x58. See docs\COLLISION-CONTACT-RECORD.md.

           THE ONE QUESTION THIS ANSWERS: is `+0x44..0x4C` a MAGNITUDE or a unit normal. If
           its length tracks the speed the car loses, the entire nine-layer detection stack is
           replaceable by asking the engine. If it is always ~1.0, the force lives elsewhere in
           the record and +0x18 is the next candidate.

           Nothing here changes a force, a threshold or a duration. It reads memory the solver
           has already written and writes one log record per contact. Capped so a pile-up
           cannot flood the file. */
        if (car && CONTACT_PROBE) {
            DWORD cbeg = 0, cend = 0, n = 0, ok;
            /* THE COUNT IS THE POINT, and the first version of this probe left it out.
               The rule applied: no contact means no collision, so the number of live
               contacts on the tick a kick fires IS the discriminator - a phantom fires
               with an EMPTY array.

               v7.65: THE ZERO IS NOW REACHABLE, and it was not before. The whole block used
               to sit inside `cbeg && cend > cbeg`, so an EMPTY array wrote nothing at all -
               the one state the probe exists to report was the one state it could not say.
               Drive 8 then read as a single 204-second contact when the truth was 32 separate
               runs, 19 of them one or two polls long. An instrument that cannot print "0 hits"
               is the failure this domain's own test methodology names first.
               `c` distinguishes the three cases so silence is never re-interpreted: 1 = the
               pointers were read and the array is as reported, 0 = the pointers themselves
               could not be read, and no record at all = the probe did not run. */
            ok = (SafeReadD(car + 0x1D8u, &cbeg) && SafeReadD(car + 0x1DCu, &cend)
                  && cend >= cbeg) ? 1u : 0u;
            if (ok && cbeg && cend > cbeg) n = (cend - cbeg) / 0x5Cu;
            {
                static DWORD s_lastN = 0xFFFFFFFFu, s_saidAt = 0;
                if (n != s_lastN || (now - s_saidAt) >= 1000u) {
                    s_lastN = n; s_saidAt = now;
                    Log(K_INIT, 0xF0, n, ok ? (cend - cbeg) : 0u, ok);
                }
            }
            if (n) {
                if (n > CONTACT_MAX) n = CONTACT_MAX;
                for (DWORD ci = 0; ci < n; ci++) {
                    DWORD rec = cbeg + ci * 0x5Cu;
                    float vx = 0, vy = 0, vz = 0, thr = 0;
                    DWORD eff = 0;
                    if (!SafeReadF(rec + 0x44u, &vx)) continue;
                    SafeReadF(rec + 0x48u, &vy);
                    SafeReadF(rec + 0x4Cu, &vz);
                    SafeReadF(rec + 0x18u, &thr);
                    SafeReadD(rec + 0x54u, &eff);
                    {
                        float len = Sqrtf(vx*vx + vy*vy + vz*vz);
                        /* a = |v| x1000, b = the +0x18 float x1000, c = the +0x54 effect id.
                           Compare `a` against the speed lost around the same tick: if it
                           tracks, the engine is telling us how hard. */
                        Log(K_INIT, 0xEF, (DWORD)(LONG)(len * 1000.0f),
                            (DWORD)(LONG)(thr * 1000.0f), eff);
                        /* WHERE it was hit. World coordinates, deliberately: `car_mtx` already
                           records the car's own world position and orientation every 50 ms, so
                           the decoder can put the contact into the CAR's frame offline and say
                           front / rear / left / right. Doing that transform in the mod would
                           bake a convention into the log that could not be revisited, and this
                           project has been burned by exactly that (see the basis-swap trap in
                           the camera work). a/b/c = x/y/z x100. */
                        {
                            float px = 0, py = 0, pz = 0;
                            SafeReadF(rec + 0x38u, &px);
                            SafeReadF(rec + 0x3Cu, &py);
                            SafeReadF(rec + 0x40u, &pz);
                            Log(K_INIT, 0xF1, (DWORD)(LONG)(px * 100.0f),
                                (DWORD)(LONG)(py * 100.0f), (DWORD)(LONG)(pz * 100.0f));
                        }
                    }
                }
            }
        }

        if (haveCar && g_tearFix > 0.0f) {
            static float s_good = 0.0f;    /* last sample believed to be real          */
            static float s_held = -1.0f;   /* a suspicious sample awaiting its verdict */
            if (s_held >= 0.0f) {
                /* The verdict tick. `spd` is the sample AFTER the suspicious one. */
                if (spd >= s_good * TEAR_RECOVER) {
                    /* Recovered - the held sample was the intermediate of the double store.
                       Nothing was fired on it, and this tick continues from the real value. */
                    /* v7.65: 0xF5, NOT 0xED. It shared 0xED with v7.57's chassis-shock
                       diagnostic, and reading the two as one is what pulled v7.64 off the
                       bench: 1091 records were counted as suppressions when 10 of them were.
                       The decoder can separate them by arithmetic, but a channel that needs
                       arithmetic to know what it is will be misread again by whoever is in a
                       hurry. One producer, one code. */
                    Log(K_INIT, 0xF5, (DWORD)(LONG)(s_held * 1000.0f),
                        (DWORD)(LONG)(s_good * 1000.0f),
                        (DWORD)(LONG)((s_held / s_good) * 10000.0f));
                    s_good = spd;
                } else {
                    /* Still down - it was a real collapse. Let it through now, 15 ms late. */
                    spd = s_held;
                    s_good = s_held;
                }
                s_held = -1.0f;
            } else if (s_good > TEAR_MOVING_MS && spd < s_good * g_tearFix) {
                s_held = spd;             /* suspicious: hold it, decide next tick */
                spd = s_good;
            } else {
                s_good = spd;
            }
        }

        if (haveCar) {
            /* ---- Speed EMA ---- */
            float alpha;
            if (spd < EMA_LO_SPD) {
                alpha = EMA_FAST;
            } else if (spd < EMA_HI_SPD) {
                alpha = EMA_FAST - (EMA_FAST - EMA_SLOW)
                        * (spd - EMA_LO_SPD) / (EMA_HI_SPD - EMA_LO_SPD);
            } else {
                alpha = EMA_SLOW;
            }
            spdEma = spdEma * (1.0f - alpha) + spd * alpha;
            float speedKmh = spdEma * 3.6f;
            g_spdX100 = (LONG)(speedKmh * 100.0f);   /* v7.36: mirrored out for mark context */

            /* ---- Spring: centering (zero at standstill, full by CENTER_FULL_KMH) ---- */
            float centerFactor = Clampf(speedKmh / CENTER_FULL_KMH, 0.0f, 1.0f);
            float hiSpeedFactor = Clampf(
                (speedKmh - HISPEED_LO_KMH) / (HISPEED_HI_KMH - HISPEED_LO_KMH), 0.0f, 1.0f);
            /* v7.23 F8: a heavy vehicle scales the spring by the MOVING damper factor, so
               the spring/damper ratio - the wheel's return speed - is unchanged at speed. */
            /* gog-patcher: through TruckX now, which is the same 1.0f for a car (it returns on
               !g_heavy before touching the trim) and the trim-scaled surplus for a truck. */
            float springX = TruckX(HEAVY_SPRING_X);
            (void)centerFactor; (void)hiSpeedFactor;   /* SpringCoeffFor recomputes them */
            LONG springCoeff = SpringCoeffFor(speedKmh, springX, g_gndOn && g_gndSat);

            /* ---- Damper: heavy->moving (by 10 km/h)->heavy again at highway ---- */
            float dampFade = Clampf(speedKmh / DAMP_FADE_KMH, 0.0f, 1.0f);
            float hispeedDampF = Clampf(
                (speedKmh - HISPEED_DAMP_LO_KMH) / (HISPEED_DAMP_HI_KMH - HISPEED_DAMP_LO_KMH),
                0.0f, 1.0f);
            /* v7.23 F8: static and moving get DIFFERENT factors (x2 standing, x1.75 moving),
               so they are applied to their own terms rather than to the blended result. */
            /* v7.24: the base numbers now come from the LIVE PROFILE, not the #defines. The
               hispeed damper follows the profile's MOVING damper proportionally, so a profile
               that removes the damper (F2) removes its hispeed part too, instead of leaving
               8250 standing on a wheel that is meant to have no damper at all. */
            const FeelProfile *pf = &g_prof[g_prof_i];
            float hvS = TruckX(HEAVY_DAMP_STATIC_X);   /* gog-patcher: see springX above */
            float hvM = TruckX(HEAVY_DAMP_MOVING_X);
            /* gog-patcher 2026-07-27: the user's two truck damper percents, applied to the DAMPER
               terms only. hvS/hvM stay unscaled because the friction lines below share them and
               friction on trucks is refused - see TruckDampX for the whole reasoning. The hispeed
               term follows the MOVING slider, the same way it already follows the moving damper
               proportionally, so a truck damper turned down does not leave 8250 standing at
               highway speed on a wheel that is meant to be lighter. */
            float tdS = TruckDampX(g_pctTruckStatic);
            float tdM = TruckDampX(g_pctTruckMoving);
            /* ...and the user's own two car percents, on the same terms. THIS is where the whole
               damper chain composes, and the order is car-then-truck on each term separately:
                   coefficient = reference * heavy-class * car% * truck%
               A car never reaches the truck factor (TruckDampX returns 1.0f before reading it),
               and every factor is an exact 1.0f at its default, so a stock install is
               bit-identical to the build that had none of these controls. */
            float cdS = PctF(g_pctDampStatic);
            float cdM = PctF(g_pctDampMoving);
            float dStatic  = pf->dampStatic  * hvS * cdS * tdS;
            float dMoving  = pf->dampMoving  * hvM * cdM * tdM;
            (void)dampFade; (void)hispeedDampF;   /* DamperCoeffFor recomputes both */
            LONG dampCoeff = DamperCoeffFor(speedKmh, dStatic, dMoving,
                                            g_gndOn && g_gndGate, GroundGate(),
                                            g_gndLastGrip);

            /* ---- v7: heading + yaw rate this tick (bump detection + F1 share it) ----
               read the FULL matrix once: heading (m6,m8) for F1, m[7]=roll + m[1]=pitch for F2 */
            float mmx[9]; int haveHead = ReadHeadingMatrix(g_car, mmx);
            float headNow  = haveHead ? Atan2f(mmx[6], mmx[8]) : 0.0f;
            float rollNow  = haveHead ? mmx[F2R_M] : 0.0f;   /* m[7] roll  (tram/offroad) */
            float pitchNow = haveHead ? mmx[F2P_M] : 0.0f;   /* m[1] pitch (curbs/hills)  */
            float yawRate = 0.0f;
            if (haveHead && haveHeadPrev) {
                float dh = headNow - headingPrev;
                while (dh >  3.14159265f) dh -= 6.28318531f;
                while (dh < -3.14159265f) dh += 6.28318531f;
                yawRate = dh / (POLL_MS * 0.001f);
            }
            float expectedYaw = spdEma * steer * YAW_GAIN;
            float disturb = yawRate - expectedYaw;

            /* ---- F1: LATERAL slip (heading vs course) -> light wheel on slide onset ----
               course = direction of travel from d/dt(world pos); heading = car facing from
               the orientation matrix. Their difference is the slip angle: ~0 driving straight
               or in a clean corner, large the instant the car breaks into a slide. This is the
               signal the raw/EMA delta could never see (that one is blind to lateral slides). */
            float latSlip = 0.0f, slipDeg = 0.0f, latSlipMult = 1.0f;
            {
                float wx = 0, wy = 0;
                CarField(CARPOS_X_OFF, &wx);
                CarField(CARPOS_Y_OFF, &wy);
                posRingX[posCount & 15] = wx;
                posRingY[posCount & 15] = wy;
                posCount++;

                /* v7.65 the tick log - see g_tickLog. Placed here because this is the one
                   point in the tick where the speedometer field and the world position have
                   both been read, so the two channels describe the SAME instant. Splitting
                   them across the tick would put an unknown offset between the suspect signal
                   and the witness, which is the one thing this instrument must not do.
                     0xF2  a = spd x1000 (the raw 0x2A0C field, the suspect)
                           b = the QPC delta since the previous tick, RAW counts
                           c = 0, reserved
                     0xF3  a = world X x100   b = world Y x100   (x100, not x1000: the map is
                           kilometres across and x1000 overflows a signed 32-bit past 2147 m)
                           c = heading x10000, or 0x7FFFFFFF when the matrix was not readable
                   Absolute position rather than the step, deliberately: from position and dt
                   the step, the speed, the acceleration and the DIRECTION of an impulse
                   relative to the car's facing are all derivable, which is what the rear and
                   side misses need. From a pre-divided step, none of them are. */
                if (g_tickLog) {
                    static LARGE_INTEGER s_prevQpc;
                    static int s_haveQpc = 0;
                    LARGE_INTEGER qnow;
                    DWORD dtq = 0;
                    if (QueryPerformanceCounter(&qnow)) {
                        if (s_haveQpc) {
                            LONGLONG d = qnow.QuadPart - s_prevQpc.QuadPart;
                            /* A 32-bit field holds ~7 minutes at 10 MHz; anything past that is
                               a suspended thread, not a tick, and is recorded as saturated
                               rather than wrapped into a plausible small number. */
                            dtq = (d < 0) ? 0u : (d > 0xFFFFFFFF ? 0xFFFFFFFFu : (DWORD)d);
                        }
                        s_prevQpc = qnow;
                        s_haveQpc = 1;
                    }
                    Log(K_INIT, 0xF2, (DWORD)(LONG)(spd * 1000.0f), dtq, 0);
                    Log(K_INIT, 0xF3, (DWORD)(LONG)(wx * 100.0f), (DWORD)(LONG)(wy * 100.0f),
                        haveHead ? (DWORD)(LONG)(headNow * 10000.0f) : 0x7FFFFFFFu);
                }

                /* ---- v7.66 IMPULSE CANDIDATE LOG (0xD9) - measurement only, NO force ----
                   The offline replay of drive 9 (src\impulse_replay.py) says a detector armed
                   on the change of the VELOCITY VECTOR keeps all 3 of his approved kicks and
                   answers 14 of his 25 complaints at once: it fires on all 6 hits the current
                   detector misses, and produces nothing on the 8 that were phantoms. A drop-in-
                   scalar-speed detector cannot do that, because a ram from behind ACCELERATES
                   the car.
                   That result rests on ONE drive, and the tick log only exists from v7.65, so
                   no other drive can confirm it. This logs the same quantity live so the next
                   ordinary drive settles it. It touches no force, no threshold and no duration:
                   the wheel behaves exactly as the approved build does.

                   Geometry: v = (pos[now] - pos[now-IMP_VEL_WIN]) / dt over that span, and the
                   impulse is |v(now) - v(now-IMP_LAG)|. Adjacent-tick differences were tried
                   first and are useless - any smoothing flattens them, which is why the replay
                   read 0 of 3 until the lag was introduced.
                   Its own 32-slot ring, because posRing is 16 and IMP_VEL_WIN+IMP_LAG needs 20.
                     a = |dV| x1000
                     b = angle of dV relative to the car's heading, degrees x100, 0..35999
                         (0 = shove straight ahead = rammed from BEHIND, 18000 = head-on)
                         0x7FFFFFFF when the heading was not readable
                     c = speed x100 */
                if (g_qpcHz > 0.0) {
                    static float rx[IMP_RING], ry[IMP_RING], rt[IMP_RING];
                    static int   rn = 0;
                    static LARGE_INTEGER s_impPrev;
                    static int   s_impHave = 0;
                    LARGE_INTEGER qn;
                    float dtSec = 0.0f;
                    if (QueryPerformanceCounter(&qn)) {
                        if (s_impHave) {
                            LONGLONG dd = qn.QuadPart - s_impPrev.QuadPart;
                            if (dd > 0) dtSec = (float)((double)dd / g_qpcHz);
                        }
                        s_impPrev = qn;
                        s_impHave = 1;
                    }
                    /* A tick with no usable dt would divide by zero further down; skipping it
                       loses one sample out of ~49000 and keeps the ring honest. */
                    if (dtSec <= 0.0f) goto imp_done;

                    /* ---- resolve a HELD impulse kick ----
                       Runs every tick, before anything else, so a pending kick is answered even
                       on ticks where no new impulse is seen. The speedometer is the independent
                       witness: if it did not move, nothing hit the car and the impulse was
                       position noise. 0xA1 records the veto, so a phantom that was PREVENTED
                       leaves a trace - otherwise the fix would be indistinguishable from the
                       channel simply going quiet. */
                    if (s_impPend && (now - s_impPendT)
                            >= (DWORD)(g_impWitness ? g_impConfirm2 : (LONG)IMP_CONFIRM_MS)) {
                        /* v7.88. Two witnesses, one of which is honest. The POSITION one is the
                           default; the old speedometer one stays reachable through
                           impulse_witness = 0 so the two can be driven against each other. */
                        float ds;
                        int agreed;
                        if (g_impWitness) {
                            float vA = ImpRingSpeed(rx, ry, rt, rn, IMP_WIT_SPAN);
                            float vB = ImpRingSpeed(rx, ry, rt, s_impPendCount, IMP_WIT_SPAN);
                            /* -1 means the ring cannot answer yet. Log a 0 rather than a
                               difference of two error codes, which would read as a measurement. */
                            ds = (vA < 0.0f || vB < 0.0f) ? 0.0f : (vA - vB);
                            if (ds < 0.0f) ds = -ds;
                            agreed = ImpulseConfirmed(rx, ry, rt, s_impPendCount, rn,
                                                      IMP_WIT_SPAN,
                                                      (float)g_impDvMin * 0.01f);
                        } else {
                            ds = spd - s_impPendSpd;
                            if (ds < 0.0f) ds = -ds;
                            agreed = (ds >= IMP_SPD_CONFIRM);
                        }
                        if (agreed && g_inVehicle
                            && (!kickUntil || kickIsTap)) {
                            LONG mag = (LONG)(IMP_FORCE_A * s_impPendDv + IMP_FORCE_B);
                            if (mag > MAX_MAG) mag = MAX_MAG;
                            mag = Mul(mag, g_pctCrash);
                            g_flip = !g_flip;
                            SetMag(g_flip ? mag : -mag);
                            kickUntil = now + IMP_KICK_MS;
                            kickIsTap = 0;
                            s_impLastKick = now;
                            Log(K_FIRE, 0xDE, (DWORD)mag,
                                (DWORD)(LONG)(s_impPendDv * 1000.0f),
                                (DWORD)(LONG)(s_impPendAlong * 1000.0f + 100000.0f));
                        } else {
                            Log(K_INIT, 0xA1, (DWORD)(LONG)(s_impPendDv * 1000.0f),
                                (DWORD)(LONG)(ds * 100.0f), (DWORD)(LONG)(spd * 100.0f));
                        }
                        s_impPend = 0;
                    }

                    /* ---- v7.88 A PAUSE MUST NOT LOOK LIKE AN IMPULSE -------------------------
                       While the game is in a menu or loading, the car object is not updated: the
                       position stands still and our clock keeps running. When play resumes, a
                       velocity window that SPANS that gap sees the whole speed appear in one
                       step, which is the largest impulse a drive can contain and means nothing.
                       The ring is RESET rather than the still ticks being skipped - skipping was
                       measured offline and it loses real crashes, because a car stopped against a
                       wall also holds bit-identical positions. A reset only refuses to compare
                       across the gap. */
                    {
                        static float s_lastX = 0.0f, s_lastY = 0.0f;
                        static float s_stillSec = 0.0f;
                        static int   s_haveLast = 0;
                        if (s_haveLast && wx == s_lastX && wy == s_lastY) {
                            s_stillSec += dtSec;
                        } else {
                            /* The RESET itself now lives in the ring fill below, where the gap is
                               measured as the accumulated time between two real updates - one
                               rule instead of two that could disagree. This half only records
                               it, because a reset nobody logged is a reset nobody can count. */
                            if (s_stillSec >= 0.20f)
                                Log(K_INIT, 0x2B, (DWORD)(LONG)(s_stillSec * 1000.0f), 0, 0);
                            s_stillSec = 0.0f;
                        }
                        s_lastX = wx; s_lastY = wy; s_haveLast = 1;
                    }

                    /* ONE SAMPLE PER GAME UPDATE. The accumulator carries the wall time of every
                       poll since the last real movement, so a skipped update lengthens the next
                       sample's dt instead of inserting a zero-travel one. */
                    {
                        static float s_accDt = 0.0f;
                        static float s_prevX = 0.0f, s_prevY = 0.0f;
                        static int   s_havePrevPos = 0;
                        s_accDt += dtSec;
                        if (s_havePrevPos && wx == s_prevX && wy == s_prevY) goto imp_done;
                        s_prevX = wx; s_prevY = wy; s_havePrevPos = 1;
                        /* A GAP LONGER THAN ANY UPDATE IS NOT AN UPDATE. A menu, a load or a
                           standstill leaves the position untouched for a long stretch; the first
                           movement afterwards would otherwise be written as one sample carrying
                           seconds of dt, and every window containing it would be nonsense. Start
                           the ring again instead - the samples either side are never compared. */
                        if (s_accDt > 0.25f) {
                            rn = 0;
                            s_impPend = 0;
                            s_accDt = 0.0f;
                            goto imp_done;
                        }
                        rx[rn & IMP_RMASK] = wx; ry[rn & IMP_RMASK] = wy;
                        rt[rn & IMP_RMASK] = s_accDt;
                        s_accDt = 0.0f;
                    }
                    rn++;
                    if (rn > IMP_VEL_WIN + IMP_LAG) {
                        int iNow = (rn - 1) & IMP_RMASK;
                        int iOld = (rn - 1 - IMP_VEL_WIN) & IMP_RMASK;
                        int jNow = (rn - 1 - IMP_LAG) & IMP_RMASK;
                        int jOld = (rn - 1 - IMP_LAG - IMP_VEL_WIN) & IMP_RMASK;
                        float tA = 0.0f, tB = 0.0f;
                        int k;
                        for (k = 0; k < IMP_VEL_WIN; k++) {
                            tA += rt[(rn - 1 - k) & IMP_RMASK];
                            tB += rt[(rn - 1 - IMP_LAG - k) & IMP_RMASK];
                        }
                        if (tA > 0.0f && tB > 0.0f) {
                            float ax = (rx[iNow] - rx[iOld]) / tA;
                            float ay = (ry[iNow] - ry[iOld]) / tA;
                            float bx = (rx[jNow] - rx[jOld]) / tB;
                            float by = (ry[jNow] - ry[jOld]) / tB;
                            float dvx = ax - bx, dvy = ay - by;
                            float dv  = Sqrtf(dvx * dvx + dvy * dvy);
                            /* Peak inside the confirmation window, for the manifold tap below.
                               Updated on EVERY tick, not only above IMP_LOG_MIN, or the tap
                               would only ever see impulses large enough to be logged. The
                               teleport clamp applies here too - a mission load must not leave
                               a 33966 m/s peak standing for 600 ms. */
                            if (dv <= IMP_TELEPORT
                                && (dv > g_impPeak || (now - g_impPeakT) > IMP_CONF_WIN)) {
                                g_impPeak  = dv;
                                g_impPeakT = now;
                            }
                            /* A mission load moves the car discontinuously; that is a teleport,
                               not a hit, and must be dropped rather than recorded as the largest
                               impact of the drive. */
                            if (dv >= IMP_LOG_MIN && dv <= IMP_TELEPORT) {
                                /* SIDE OF THE IMPACT, from geometry rather than from the
                                   orientation matrix. v7.66 took the angle between dV and
                                   `headNow`, and drive 10 showed it does not separate the
                                   sides: field notes reported the hits as mostly from behind while that
   angle called 40 of 41 lateral, and the felt impact, reported as head-on,
   landed at 86 deg where a head-on belongs at 180.
                                   `headNow` is Atan2(m[6], m[8]) while the impulse angle was
                                   Atan2(dy, dx) - two different conventions, ~90 deg apart.
                                   Projecting dV onto the direction the car was ALREADY moving
                                   needs no matrix and no convention: positive = pushed along
                                   its own travel = rammed from BEHIND, negative = stopped =
                                   head-on. Measured on drive 10 it splits 20 rear / 18 front /
                                   2 side with the cross term near zero, which matches what he
                                   described. */
                                float sb = Sqrtf(bx * bx + by * by);
                                float along = 0.0f, across = 0.0f;
                                if (sb > 1.0f) {
                                    float ux = bx / sb, uy = by / sb;
                                    along  =  dvx * ux + dvy * uy;
                                    across = -dvx * uy + dvy * ux;
                                }
                                /* b keeps a signed quantity now: along x1000, biased by 100000
                                   so it survives the DWORD. c carries |across| x1000. */
                                Log(K_INIT, 0xD9, (DWORD)(LONG)(dv * 1000.0f),
                                    (DWORD)(LONG)(along * 1000.0f + 100000.0f),
                                    (DWORD)(LONG)(across * 1000.0f));

                                /* ---- v7.66 THE IMPULSE KICK ITSELF (0xDE) ----
                                   Off unless `impulse_kick = 1`. This is the first channel in
                                   this project that fires on a ram FROM BEHIND, because it
                                   reads the change of the velocity VECTOR: being shoved
                                   forwards is as large a dV as being stopped, while every
                                   existing detector looks for a speed DROP and is blind to it.

                                   The force line is not invented - it is fitted to the three
                                   kicks HE approved on drive 9 (impulse 12.21/15.27/20.06 m/s
                                   against 18861/18480/25000). Under it the six rear hits he
                                   complained about would land at 13957-18093, inside his own
                                   approved band.

                                   It never pre-empts a crash or a damage kick: it fires only
                                   into silence or over a light tap, so nothing he already
                                   approved is interrupted. On foot it cannot be felt anyway -
                                   SetMag zeroes every force when !g_inVehicle - but the gate is
                                   named here too so the LOG does not fill with the position
                                   discontinuities that walking produces.
                                   0xDE (K_FIRE): a = force, b = |dV| x1000, c = angle deg x100. */
                                if ((g_impOverride >= 0 ? g_impOverride : g_impulseKick)
                                    && g_inVehicle
                                    && dv >= IMP_KICK_MIN
                                    && (now - s_impLastKick) >= IMP_REFRACT_MS
                                    && !s_impPend) {
                                    if (g_impConfirm) {
                                        /* Hold it. The speedometer has not had time to show the
                                           consequence yet - that is the whole reason a
                                           backward-looking test does not work. */
                                        s_impPend      = 1;
                                        s_impPendT     = now;
                                        s_impPendDv    = dv;
                                        s_impPendSpd   = spd;
                                        s_impPendAlong = along;
                                        s_impPendCount = rn;   /* where the ring stood, for the
                                                                  position witness at confirm */
                                        Log(K_INIT, 0xA0, (DWORD)(LONG)(dv * 1000.0f),
                                            (DWORD)(LONG)(spd * 100.0f), 0);
                                    } else if (!kickUntil || kickIsTap) {
                                        LONG mag = (LONG)(IMP_FORCE_A * dv + IMP_FORCE_B);
                                        if (mag > MAX_MAG) mag = MAX_MAG;
                                        mag = Mul(mag, g_pctCrash);
                                        g_flip = !g_flip;
                                        SetMag(g_flip ? mag : -mag);
                                        kickUntil = now + IMP_KICK_MS;
                                        kickIsTap = 0;
                                        s_impLastKick = now;
                                        Log(K_FIRE, 0xDE, (DWORD)mag,
                                            (DWORD)(LONG)(dv * 1000.0f),
                                            (DWORD)(LONG)(along * 1000.0f + 100000.0f));
                                    }
                                }
                            }
                        }
                    }
                imp_done: ;
                }
                if (posCount > SLIP_WIN) {
                    /* Gate on real GROUND displacement, not wheel speed: a handbrake slide
                       locks the wheels (0x2A0C -> ~0) while the car still moves fast, so a
                       wheel-speed gate would miss exactly the slides we care about. */
                    int oldIdx = (posCount - 1 - SLIP_WIN) & 15;
                    float dx = wx - posRingX[oldIdx];
                    float dy = wy - posRingY[oldIdx];
                    float disp2 = dx*dx + dy*dy;
                    if (disp2 > SLIP_MIN_DISP2 && disp2 < 2500.0f) {  /* lo=enough speed, hi=teleport */
                        if (haveHead) {
                            float head = headNow;
                            float course = Atan2f(dy, dx);
                            float raw = head - course - slipOffset;
                            while (raw >  3.14159265f) raw -= 6.28318531f;
                            while (raw < -3.14159265f) raw += 6.28318531f;
                            if (Absf(raw) < SLIP_CAL_GATE)
                                slipOffset += SLIP_CAL_RATE * raw;  /* auto-zero on smooth driving */
                            latSlip = raw;
                            slipDeg = Absf(raw) * 57.29578f;
                            /* bell: rise to PEAK, then fall (see defines above) */
                            if (slipDeg > SLIP_PEAK_DEG)
                                latSlipMult = SLIP_PEAK_MULT
                                    - (slipDeg - SLIP_PEAK_DEG) * SLIP_FALL_K;
                            else if (slipDeg > SLIP_RISE_DEG)
                                latSlipMult = 1.0f + (SLIP_PEAK_MULT - 1.0f)
                                    * (slipDeg - SLIP_RISE_DEG)
                                    / (SLIP_PEAK_DEG - SLIP_RISE_DEG);
                            /* v7.29: the floor is per-PROFILE now. Measured on his v7.28
                               drive, at 35-70 deg of slip the spring is down to 558 and the
                               damper to 1244, against 2652/5254 driving straight - about 15%
                               of the wheel's normal weight. He suspected that is too light
                               ("maybe the wheel turns too easily at the moment of
                               maximum slide") and physically he is right: a real wheel
                               in a deep slide loses its self-aligning torque but keeps caster
                               trail and mechanical friction, so it does not go nearly empty.
                               F1 and F2 keep the confirmed-good 0.15; F3/F4 raise it. */
                            if (latSlipMult < g_prof[g_prof_i].slipFloor)
                                latSlipMult = g_prof[g_prof_i].slipFloor;
                        }
                    }
                }
            }

            /* ---- Longitudinal wheelspin (raw > EMA), kept as a secondary grip-loss cue ---- */
            float slipExcess = spd - spdEma;   /* + = spin faster than vehicle, - = lockup/crash */
            float longMult = 1.0f;
            if (spdEma > 1.0f && slipExcess > SLIP_EXCESS_FLOOR && slipExcess < SLIP_EXCESS_MAX)
                longMult = Clampf(1.0f - (slipExcess - SLIP_EXCESS_FLOOR) * SLIP_REDUCTION_K,
                                  SLIP_MULT_MIN, 1.0f);

            /* v7.1g: LATERAL slip RE-ENABLED (offline-verified). longMult (longitudinal)
               stays OFF - it tripped on the wheel-speed oscillation. latSlip now fires only
               above ~25 km/h GROUND speed (SLIP_MIN_DISP2 gate) with onset 10deg, and the
               applied multiplier is EMA-smoothed so onset/offset are gentle (verified: 0
               rapid flips, engages only in real slides). */
            (void)longMult;
            slipMultSmooth = slipMultSmooth * (1.0f - SLIP_SMOOTH_ALPHA)
                           + latSlipMult    * SLIP_SMOOTH_ALPHA;
            float slipMult = slipMultSmooth;
            /* spring takes the full bell (load-up + falloff); damper only the falloff -
               the approach-to-limit should load the CENTERING, not the rotation viscosity */
            float dampMult = slipMult < 1.0f ? slipMult : 1.0f;
            LONG finalSpring = (LONG)(springCoeff * slipMult);
            LONG finalDamp   = (LONG)(dampCoeff   * dampMult);
            /* v7.22 F9: on foot the wheel must go LIGHT. This is the symptom he minded most -
               observed as the wheel staying heavy the whole time, so the gate releases the spring and the
               damper, not merely the kicks. SetMag is gated inside itself. */
            /* v7.24 friction: heavy vehicles only, fading with speed on the same 10 km/h ramp
               as the standstill damper, because tyre scrub disappears once the wheels roll.
               A car computes K_FRIC_CAR = 0 on both ends, so this line cannot change the
               reference feel - it can only ever act in a truck. Slip lightening applies to it
               too: if the wheels have broken traction there is no scrub to fight.
               v7.24: the profile decides, not the vehicle class - F1 keeps friction at 0 in
               every vehicle (so the reference feel is still exactly reproducible on one key),
               while F2/F3/F4 give it to cars as well. The class multipliers stack ON TOP, so
               a truck on F4 is the heaviest combination in the build. */
            float fS = pf->fricStatic * (g_fricTrim * 0.01f) * hvS;
            float fM = pf->fricMoving * (g_fricTrim * 0.01f) * hvM;
            LONG fricSat = (LONG)Clampf((fS + (fM - fS) * dampFade) * dampMult,
                                        0.0f, (float)MAX_STEER_SAT);
            /* v7.8: friction fades with the tyre load too. It is zero for a car on the reference
               profile, so this line cannot move the approved feel - but a truck on F4 carries
               real scrub, and scrub through wheels that are in the air is exactly the weight he
               asked to have disappear. The damper keeps its thin floor; friction does not need
               one, because friction is what resists TURNING, which is the feel being removed. */
            if (g_gndOn && g_gndGate) fricSat = (LONG)((float)fricSat * GroundGate());
            g_fricNow = fricSat;   /* what the F5 "I like this" marker records */
            if (!g_inVehicle) { finalSpring = 0; finalDamp = 0; fricSat = 0; }
            /* THE BENCH-ONLY CENTRING PULSE. Off in every shipped ini; see g_gndCentreMs. It is
               placed after the `!g_inVehicle` line on purpose - the wheel has to be centred while
               the game is still in its menus, which is exactly when there is no vehicle. */
            if (g_gndCentreMs > 0) {
                static DWORD s_centreT0 = 0;
                DWORD nowC = GetTickCount();
                if (!s_centreT0) { s_centreT0 = nowC; Log(K_INIT, 0x1F, (DWORD)g_gndCentreMs, 0, 0); }
                if ((DWORD)(nowC - s_centreT0) < (DWORD)g_gndCentreMs) {
                    finalSpring = K_CENTER_DI;
                    finalDamp   = K_DAMP_DI_STATIC / 2;
                } else if (g_gndCentreMs > 0 && (DWORD)(nowC - s_centreT0) < (DWORD)g_gndCentreMs + 200u) {
                    Log(K_INIT, 0x1F, 0, 1, 0);        /* the pulse is over, and it says so */
                }
            }
            /* THE BENCH HOLDS THE WHEEL so a recorded route replays into the car it was recorded
               for - see g_gndHoldCentre. Lifted for the seconds the self-test is measuring, which
               are the only seconds where the wheel's own movement is the answer. */
            if (g_gndHoldCentre && !g_gndSelfActive) {
                finalSpring = K_CENTER_DI;
                if (finalDamp < K_DAMP_DI_MOVING) finalDamp = K_DAMP_DI_MOVING;
            }
            SetSpring(finalSpring);
            SetDamper(finalDamp);
            SetFriction(fricSat);

            /* THE COEFFICIENTS AS SENT, one tick in eight. 0xDC carries the same three numbers but
               only on a mark key, so run 016 could not answer "did the damper stay heavy while I
               was in the air" from a drive where he pressed nothing. This row makes the whole
               model checkable off ONE log: spring must be 0 with the channel on, the damper must
               follow the gate down to the airborne floor, and friction must follow it to zero. */
            LogCoeffsSent(finalSpring, finalDamp, fricSat);

            /* ---- v7.4g F2 road wallow: ROLL(m7, load-envelope) + PITCH(m1, slip-gated) ---- */
            float rollHP = 0.0f, pitchHP = 0.0f;
            float shkD1 = 0.0f, shkD7 = 0.0f;   /* v7.57: per-tick JOLT of pitch / roll */
            {
                float target = 0.0f;
                if (haveHead) {
                    if (!wallowInit) { r7slow = rollNow; p1slow = pitchNow; wallowInit = 1; }

                    /* HP state updated EVERY tick (never stale -> no gate re-entry spike) */
                    r7slow += (rollNow  - r7slow) * F2R_ALPHA;   rollHP  = rollNow  - r7slow;
                    p1slow += (pitchNow - p1slow) * F2P_ALPHA;   pitchHP = pitchNow - p1slow;

                    /* v7.57 DIAG 0xED (ported 2026-08-03): the per-tick JOLT of the chassis
                       attitude. Updated EVERY tick that has a heading, so the difference always
                       spans one tick and never an arbitrary gap - the same discipline the HP
                       state above already follows. Nothing is gated on it. */
                    shkD1 = pitchHP - shkP1Prev;  shkP1Prev = pitchHP;
                    shkD7 = rollHP  - shkM7Prev;  shkM7Prev = rollHP;

                    /* ROLL envelope: cut instantly on a slide, hold through the cooldown,
                       then ramp back gently. Kills the end-of-slide/spinout jolt; the gentle
                       ramp leaves only a soft swell. Low-slip side-rock: env stays 1. */
                    if (slipDeg > F2R_SLIP_ON) { rollEnv = 0.0f; slipCool = F2R_COOLDOWN_TICKS; }
                    else if (slipCool > 0)     { slipCool--;     rollEnv = 0.0f; }
                    else                       { rollEnv += (1.0f - rollEnv) * F2R_ATTACK_A; }

                    float rollF = 0.0f;
                    if (Absf(rollHP) > F2R_DEADZONE) {
                        float e = Absf(rollHP) - F2R_DEADZONE;         /* magnitude past dz */
                        /* v813: THE ROLL GAIN IS A LIVE PERCENT, and it comes down. His verdict on
                           ours-034: curbs... the effect needs to be reduced somehow, because now
                           it is too large in amplitude - and measured on that drive, what is called
                           curbs is 98% THIS term ([[the-curb-effect-is-almost-entirely-roll]]).
                           Cutting the ceiling was priced and REFUSED by the measurement: at cap
                           2000 the share of rows pinned there rises from 54% to 67%, i.e. quieter
                           AND flatter, which is the opposite of what he asked for. Cutting the
                           gain is the only move that lowers the force and gives the gradation
                           back - at 25% nothing reaches the ceiling at all.
                           v813 SHIPPED 25 and ours-035 REFUSED IT: at gain 25 the roll was, in
                           his words, weak. The 25 was a prediction (655/2625, from a damper A/B),
                           and it delivered 44% of the original's mean force rather than 25%,
                           because the original is clipped at its ceiling in 54% of rows. Since
                           v818 the original (100) is the base again and the two ways down live on
                           F2/F3 - see g_rollBankGain. `road_roll_gain_pct` still moves it live. */
                        float mag = e * F2R_GAIN1;                     /* below-knee slope  */
                        if (e > F2R_KNEE_E)
                            mag = F2R_KNEE_E * F2R_GAIN1 + (e - F2R_KNEE_E) * F2R_GAIN2;
                        mag *= (float)g_rollGainPct * 0.01f;
                        if (mag > (float)g_rollCap) mag = (float)g_rollCap;
                        float sgn = (rollHP > 0.0f ? F2R_SIGN : -F2R_SIGN);
                        rollF = sgn * mag * rollEnv;
                    }

                    /* PITCH channel (m1): sharp curbs - UNCHANGED from v7.4f (perfect).
                       Hard slip-gate (F1 owns the wheel in a slide), speed-atten + brake gate. */
                    float pitchF = 0.0f;
                    if (slipDeg < F2_SLIP_GATE && Absf(pitchHP) > F2P_DEADZONE) {
                        float e = pitchHP - (pitchHP > 0.0f ? F2P_DEADZONE : -F2P_DEADZONE);
                        float spdScale = Clampf(1.0f - (speedKmh - F2P_SPD_FULL_KMH)
                                          / (F2P_SPD_MIN_KMH - F2P_SPD_FULL_KMH)
                                          * (1.0f - F2P_SPD_MINSCALE), F2P_SPD_MINSCALE, 1.0f);
                        float brakeScale = (gsDropPrev > F2P_BRAKE_GS) ? F2P_BRAKE_SCALE : 1.0f;
                        pitchF = Clampf(F2P_SIGN * e * F2P_GAIN * spdScale * brakeScale,
                                        -F2P_CAP, F2P_CAP);
                    }

                    target = Clampf(rollF * F2R_ISO_SCALE + pitchF * F2P_ISO_SCALE,
                                    -F2_TOTAL_CAP, F2_TOTAL_CAP);
                }
                wallowForce += (target - wallowForce) * F2_SMOOTH_A;   /* smudge in/out */
            }

            /* ---- v7.25 F6: BEING SPUN, as a sustained steer instead of a kick ----
               His correction after driving v7.23, and he called it important: a police spin
               should feel like "a confident and fairly smooth turn of the wheel to the side that
               matches our physics", NOT "a sharp kick, as if we had crashed
               into a wall". So the 500 ms fixed pulse is gone and the spin now lives on the
               CONTINUOUS channel beside the road wallow.

               TWO SIGNALS, TWO JOBS - this is the whole design:
                 ARM  on the trailing 2 s integral of uncommanded rotation. Robust: on his
                      labelled control run it caught 2 of 3 spins with ZERO false fires,
                      and separated spins (ratio 1.13-38.7) from plain rear hits (0.32-1.37).
                 DRIVE the force from a SMOOTHED rotation RATE, in its direction, so the wheel
                      leads where the car is actually going and fades out as the spin does.
               Never drive the wheel from the raw 8 ms rate: the v7 BUMP branch did exactly
               that, false-fired ~2x/sec on yaw noise and yanked the wheel left-right, and
               sits disabled below as `if (0 && ...)`.

               No timer and no re-arm: the force ends when the rotation ends. It is summed
               into wallowForce, so a real crash preempts it for free (kicks own the wheel
               while kickUntil holds) and no priority code is needed. */
            if (PIT_ENABLE && haveHead && haveHeadPrev) {
                spinRate += (disturb - spinRate) * SPIN_RATE_A;   /* smoothed, always */

                /* the arming integral, same trailing window as the v7.24 detector */
                float dtS = POLL_MS * 0.001f;
                pitSumHead   -= pitHeadRing[pitIdx];
                pitSumExp    -= pitExpRing[pitIdx];
                pitOverCount -= pitOverRing[pitIdx];
                pitHeadRing[pitIdx] = yawRate * dtS;
                pitExpRing[pitIdx]  = expectedYaw * dtS;
                pitOverRing[pitIdx] = (Absf(steer) > PIT_STEER_MAX) ? 1 : 0;
                pitSumHead   += pitHeadRing[pitIdx];
                pitSumExp    += pitExpRing[pitIdx];
                pitOverCount += pitOverRing[pitIdx];
                pitIdx = (pitIdx + 1) % PIT_WIN_TICKS;
                if (pitFill < PIT_WIN_TICKS) pitFill++;

                int armed = 0;
                float netDeg = 0.0f, ratio = 0.0f;
                if (pitFill >= PIT_WIN_TICKS) {
                    netDeg = (pitSumHead - pitSumExp) * 57.2957795f;
                    ratio  = Absf(netDeg) / (Absf(pitSumExp * 57.2957795f) + 0.001f);
                    armed  = (Absf(netDeg) > PIT_THRESH_DEG) && (pitOverCount == 0)
                             && (ratio > PIT_RATIO_MIN) && (spdEma > PIT_SPD_MIN);
                }
                /* HOLD once armed: the spin outlives the 2 s window that detected it, and
                   dropping the force the moment the integral falls back under the threshold
                   would produce exactly the abrupt edge he objected to. Release only when the
                   rotation itself has died away. */
                if (armed) spinHold = SPIN_HOLD_TICKS;
                else if (spinHold > 0 && Absf(spinRate) > SPIN_RELEASE_RATE) spinHold = SPIN_HOLD_TICKS;
                else if (spinHold > 0) spinHold--;

                float spinTarget = 0.0f;
                if (spinHold > 0 && Absf(spinRate) > SPIN_DEADZONE) {
                    float e = Absf(spinRate) - SPIN_DEADZONE;
                    spinTarget = Clampf(e * SPIN_GAIN, 0.0f, (float)SPIN_CAP);
                    if (spinRate < 0.0f) spinTarget = -spinTarget;
                    spinTarget *= PIT_SIGN;
                }
                /* the ramp is what makes it a lean, not a punch: ~250 ms to full */
                spinForce += (spinTarget - spinForce) * SPIN_SMOOTH_A;

                /* every 2nd tick (16 ms) while the channel is live - the force is a CURVE
                   now, not a single kick magnitude, so it has to be sampled densely enough
                   to see its shape. Silent when the channel is idle. */
                { static DWORD s_spin = 0;
                  if (((++s_spin & 1) == 0) && (Absf(spinForce) > 100.0f || spinHold > 0))
                    Log(K_FIRE, 0xC2, (DWORD)(LONG)spinForce,
                        (DWORD)(LONG)(netDeg * 100.0f), (DWORD)(LONG)(ratio * 100.0f)); }

            } else {
                spinForce += (0.0f - spinForce) * SPIN_SMOOTH_A;
                spinRate = 0.0f;
            }

            /* ---- v7.30 REGRESSION FIX: the 8 ms channel is INDEPENDENT of the PIT branch ----
               It was written inside `if (PIT_ENABLE && ...)` from v7.25, and PIT_ENABLE has
               been 0 since v7.27 - so log code 0x75 has not been written for two versions,
               and it is the only record of yawRate/steer/speed at the rate the build actually
               runs. This channel was requested specifically; it exists because analysis showed
               that designing at 8 ms off 50 ms archives is guesswork.
               Nothing new is computed: yawRate and expectedYaw are already set unconditionally
               further up, and `haveHead && haveHeadPrev` is exactly the condition under which
               yawRate is meaningful. */
            if (TELE8_ENABLE && haveHead && haveHeadPrev) {
                LONG sPack = (LONG)(steer * 1000.0f);
                DWORD packed = ((DWORD)(sPack & 0xFFFF))
                             | (((DWORD)(LONG)(spdEma * 100.0f) & 0xFFFF) << 16);
                Log(K_TELEM, 0x75, (DWORD)(LONG)(yawRate * 10000.0f),
                    (DWORD)(LONG)(expectedYaw * 10000.0f), packed);
            }

            /* ---- 2026-07-24 GEAR PROBE: log code 0x76, 8 ms, for the auto-vs-manual test ----
               The question this settles: does the game shift by ITSELF, or only when the
               player presses a gear key? So every tick we log the gear field AND the state of
               the keys the player will press. A gear change with NO key down on the preceding
               ticks is the game's own doing -> automatic gearbox. A change that always follows
               a key press -> manual, driven by input.

               Layout of the 0x76 record - READ THIS AGAINST THE CALL, not from memory. The
               call spends `a` on the sub-code, exactly like 0x75 does, so everything shifts
               one field along and a decoder that trusts a naive a/b/c/d reading gets garbage.
               That mistake has already been made twice in this project ([[log-field-trap]]):
                 a = 0x76   (the sub-code itself)
                 b = gear   at [car+0x58]+GEAR_OFF   (int32, -1..N; 0x7FFFFFFF = unreadable)
                 c = gear2  at [car+0x58]+GEAR_OFF+4 (the shadow/target copy)
                 d = speed and keys PACKED: low 16 bits = speed m/s x100, high 16 = key bits
                       bit0 'A'  bit1 'Z'   - manual up/down keys for this test
                       bit2 'S'  bit3 'X'   - spares in case A/Z clash with a game binding
                       bit4 '['  bit5 ']'   - the shifter script's legacy keys
                       bit6 'B'  - the manual/automatic gearbox MODE toggle for this test.
                                  To find the mode field: correlate the 50 ms snapshot of
                                  [car+0x58] (car_snap roots 5,6) against the ticks a B bit is
                                  set here - the field that flips right after a B press and
                                  stays flipped is the manual/auto switch. The mode is a
                                  persistent state, so 50 ms catches it; no faster channel
                                  needed.
                 GetAsyncKeyState high bit = currently down. Keys are read regardless of which
                 window has focus, which is fine here: nothing else is meant to run during the
                 test, and a stray keypress only ever ADDS a candidate explanation to inspect,
                 it cannot manufacture a gear change. */
            if (GEAR_ENABLE && GEAR_OFF && car) {
                DWORD frameG = 0;
                DWORD gear = 0x7FFFFFFFu, gear2 = 0x7FFFFFFFu;
                if (SafeReadD(car + FRAME_PTR_OFF, &frameG) && Sane(frameG)) {
                    SafeReadD(frameG + GEAR_OFF,      &gear);
                    SafeReadD(frameG + GEAR_OFF + 4u, &gear2);
                }
                DWORD keys = 0;
                if (GetAsyncKeyState('A')      & 0x8000) keys |= 1u << 0;
                if (GetAsyncKeyState('Z')      & 0x8000) keys |= 1u << 1;
                if (GetAsyncKeyState('S')      & 0x8000) keys |= 1u << 2;
                if (GetAsyncKeyState('X')      & 0x8000) keys |= 1u << 3;
                if (GetAsyncKeyState(0xDB)     & 0x8000) keys |= 1u << 4;  /* VK_OEM_4 '[' */
                if (GetAsyncKeyState(0xDD)     & 0x8000) keys |= 1u << 5;  /* VK_OEM_6 ']' */
                if (GetAsyncKeyState('B')      & 0x8000) keys |= 1u << 6;  /* manual/auto mode toggle */
                Log(K_TELEM, 0x76, gear, gear2,
                    ((DWORD)(LONG)(spd * 100.0f) & 0xFFFFu) | (keys << 16));
            }

            /* ---- v7.28 F6: self-aligning torque from the SLIP ANGLE ----
               No detector, no arming, no duration: the force simply exists while the car is
               travelling sideways, points the way a real self-aligning torque would (toward
               the direction of travel), and fades out with the slide. See the constants block
               for why every event-detecting version failed.
               `slipDeg` is already computed by F1 every tick; `latSlip` carries its sign. */
            if (SAT_ENABLE) {
                float satTarget = 0.0f;
                if (spdEma > SAT_MIN_SPD && slipDeg > SAT_DZ_DEG && slipDeg < SAT_MAX_DEG) {
                    float shape;
                    if (slipDeg <= SAT_PEAK_DEG)
                        shape = (slipDeg - SAT_DZ_DEG) / (SAT_PEAK_DEG - SAT_DZ_DEG);
                    else
                        shape = 1.0f - (1.0f - pf->satTail)
                                * (slipDeg - SAT_PEAK_DEG) / (SAT_MAX_DEG - SAT_PEAK_DEG);
                    satTarget = Clampf(shape, 0.0f, 1.0f) * (float)pf->satCap;
                    /* toward the direction of travel = the sign of the slip angle */
                    if (latSlip < 0.0f) satTarget = -satTarget;
                    satTarget *= SAT_SIGN;
                }
                /* rising toward a bigger force = attack; heading back to zero = release */
                float a = (Absf(satTarget) > Absf(satForce)) ? SAT_SMOOTH_A : SAT_RELEASE_A;
                satForce += (satTarget - satForce) * a;
                if (Absf(satForce) < 20.0f) satForce = 0.0f;   /* no infinite EMA crawl */
                { static DWORD s_sat = 0;
                  if (((++s_sat & 15) == 0) && Absf(satForce) > 50.0f)
                    Log(K_FIRE, 0xC2, (DWORD)(LONG)satForce,
                        (DWORD)(LONG)(slipDeg * 100.0f), (DWORD)(LONG)(spdEma * 100.0f)); }
            }
            /* F2 wallow log every 8 ticks (~64ms): a=0x72 b=rollHP*10000(signed)
               c=pitchHP*10000(signed) d=wallowForce(signed). Lets f2_test.py verify both
               channels + applied force from the log instead of a manual feel-test. */
            g_wallowNow = (LONG)wallowForce;   /* v7.36: mirrored out for mark context */
            { static DWORD s_w = 0;
              if ((++s_w & 7) == 0)
                Log(K_INIT, 0x72, (DWORD)(LONG)(rollHP * 10000.0f),
                    (DWORD)(LONG)(pitchHP * 10000.0f), (DWORD)(LONG)wallowForce); }

            /* ---- v7.6 F3: light-object tap from the contact list ----
               Polled at 8ms rather than read from the 50ms snapshot: the pointer is
               monotone so 50ms cannot lose an event, but it does blur when it happened.
               Re-baselined whenever the car pointer changes, so getting into a different
               car is never mistaken for a collision. */
            {
                DWORD cend;
                if (car != f3Car) { f3Car = car; f3Have = 0; }
                if (SafeReadD(car + CONTACT_END_OFF, &cend)) {
                    if (!f3Have) { f3Prev = cend; f3Have = 1; }
                    else if (cend != f3Prev) {
                        DWORD old = f3Prev;
                        f3Prev = cend;
                        if (!kickUntil && (now - f3Last) > F3_MIN_GAP_MS) {
                            /* two blends: fp drives the PEAK, fd the DURATION+ATTACK.
                               0 = slow knock (long, soft), 1 = fast hit (short, sharp). */
                            float fp = Clampf((speedKmh - F3_SPD_LO)
                                              / (F3_SPD_HI - F3_SPD_LO), 0.0f, 1.0f);
                            float fd = Clampf((speedKmh - F3_SPD_LO)
                                              / (F3_SPD_HI_DUR - F3_SPD_LO), 0.0f, 1.0f);
                            f3TapDur  = (DWORD)(F3_MS_LO  + (F3_MS_HI  - F3_MS_LO)  * fd);
                            f3TapAtk  = (DWORD)(F3_ATK_LO + (F3_ATK_HI - F3_ATK_LO) * fd);
                            f3TapPeak = (LONG)(F3_TAP_MAG *
                                        (F3_MAG_LO_SCALE + (1.0f - F3_MAG_LO_SCALE) * fp));
                            /* M1: slider #3 "hitting objects" (handover 2.3 row 3) */
                            f3TapPeak = Mul(f3TapPeak, g_pctObjects);
                            g_flip = !g_flip;
                            f3TapSign   = g_flip ? 1 : -1;
                            f3TapSmooth = 0;              /* approved triangle, untouched */
                            f3TapStart  = now;
                            SetMag(0);                    /* envelope starts from zero */
                            kickUntil = now + f3TapDur;
                            kickIsTap = 1;
                            f3Last = now;
                            Log(K_FIRE, 0xC7, (DWORD)f3TapPeak, f3TapDur,
                                (DWORD)(LONG)(speedKmh * 100.0f));
                            /* v7.8 diag 0xC9: WHAT did we hit? The list stores pointers, so
                               the newly appended entry is at [end-4]; its first dword is the
                               object's C++ vtable, and different classes have different
                               vtables. If pedestrians and props come out as two distinct
                               vtables, they can get two different forces - which is the
                               open question after the v7.7 drive (peds were not felt at all
                               while props were). Purely a read; guarded, never fatal. */
                            if (cend > old) {
                                DWORD entry = 0, vtbl = 0;
                                if (SafeReadD(cend - 4u, &entry) && Sane(entry))
                                    SafeReadD(entry, &vtbl);
                                Log(K_INIT, 0xC9, entry, vtbl,
                                    (DWORD)(LONG)(speedKmh * 100.0f));
                            }
                        } else {
                            /* suppressed (crash kick owns the wheel, or too soon after the
                               last tap) - logged so the offline pass sees the real event
                               rate, not just what reached the wheel */
                            Log(K_INIT, 0xC8, cend - old, kickUntil ? 1u : 0u,
                                now - f3Last);
                        }
                    }
                }
            }

            /* ---- v7.11 F4: pedestrian tap from the 24-byte contact vector ---- */
            {
                DWORD pend;
                if (car != pedCar) { pedCar = car; pedHave = 0; pedCount = 0; }
                if (SafeReadD(car + PED_END_OFF, &pend)) {
                    if (!pedHave) { pedPrev = pend; pedHave = 1; }
                    else if (pend != pedPrev) {
                        DWORD pold = pedPrev;
                        int grew = (pend > pedPrev);
                        pedPrev = pend;
                        if (grew) {
                            if ((now - pedLastGrow) > PED_EPI_MS) { pedCount = 1; pedFired = 0; }
                            else pedCount++;
                            pedLastGrow = now;
                            /* v7.66 diag 0xD8: WHAT grew the manifold? The 0xCB channel has been
                               called PEDESTRIAN since v7.11 on an assumption that was never
                               checked, and 0xCC shows the same vector growing while he is ON
                               FOOT - so the name is the one thing about it we know to be
                               unreliable. 0xC9 already answers this for the OBJECT tap by
                               reading the struck object's C++ vtable; this is the same read.

                               The element size is DERIVED, not assumed: `pend - pold` is how
                               many bytes this growth added, which is one element when the engine
                               appends one. A value outside 4..64 means several were appended at
                               once (or the pointer moved for another reason), and 24 - the size
                               this vector has always been described as - is used as the
                               fallback. `c` carries both numbers so the offline pass can see
                               which case it was rather than trusting either.

                               Scanning for the FIRST dword in that element that is a plausible
                               pointer whose target is itself a plausible pointer = the classic
                               shape of an object with a vtable. Purely reads, all guarded, and
                               it runs on EVERY growth including the 41 that fired nothing - the
                               single contacts are exactly the ones we cannot currently name.
                                 a = candidate object pointer (0 = none found)
                                 b = its vtable
                                 c = (offset within the element << 8) | element size */
                            {
                                DWORD grow = pend - pold;
                                DWORD elem = (grow >= 4u && grow <= 64u) ? grow : 24u;
                                DWORD best = 0, bestV = 0, off = 0xFFu, k;
                                for (k = 0; k + 4u <= elem; k += 4u) {
                                    DWORD cand = 0, v = 0;
                                    if (!SafeReadD(pend - elem + k, &cand)) continue;
                                    if (!Sane(cand)) continue;
                                    if (!SafeReadD(cand, &v) || !Sane(v)) continue;
                                    best = cand; bestV = v; off = k; break;
                                }
                                Log(K_INIT, 0xD8, best, bestV, (off << 8) | (elem & 0xFFu));

                                /* v7.70: THE VTABLE CANNOT NAME ANYTHING, and that is measured
                                   rather than suspected. All 176 0xD8 records on drive 11 carry
                                   the SAME vtable 1009CE78, zero failures to find a pointer,
                                   offset 0, element 24. The constructor that installs it
                                   (gog-LS3DF-dll.c:37717) takes an I3D_frame * - the thing in
                                   the manifold is a SCENE NODE, and every collidable in this
                                   game is one. Grouping by vtable therefore cannot separate a
                                   kerb from a lamppost from a car and never could.

                                   The frame itself does carry the answer: an enum at +0x110, a
                                   four-character tag at +0x1D4, and +0x100 which is a POINTER
                                   to a C string, NOT an inline buffer - printing +0x100 as
                                   characters prints an address. See the LS3D frame layout note.
                                   Reads only, every one guarded, and it costs two records per
                                   manifold growth - 17 per minute on drive 11. */
                                if (best) {
                                    DWORD ty = 0, tag = 0, nptr = 0, n0 = 0, n1 = 0;
                                    SafeReadD(best + 0x110u, &ty);
                                    SafeReadD(best + 0x1D4u, &tag);
                                    Log(K_INIT, 0xA2, ty, tag, best);
                                    if (SafeReadD(best + 0x100u, &nptr) && Sane(nptr)) {
                                        SafeReadD(nptr,       &n0);
                                        SafeReadD(nptr + 4u,  &n1);
                                    }
                                    Log(K_INIT, 0xA3, n0, n1, nptr);
                                }
                            }
                            /* v7.66: EITHER the old wait-for-a-SECOND-growth rule, or - when
                               tap_needs_impulse is on - the FIRST growth with an impulse behind
                               it. The wait WAS the delay he complained about, so removing it is
                               the fix; the impulse is what keeps the count from tripling.
                               See PED_IMP_CONFIRM for the measured trade. */
                            if ((g_tapNeedsImpulse
                                    ? ((now - g_impPeakT) <= IMP_CONF_WIN
                                       && g_impPeak >= PED_IMP_CONFIRM)
                                    : (pedCount >= PED_MIN_CONTACTS))
                                && !pedFired && !kickUntil
                                && (now - pedLastTap) > PED_GAP_MS) {
                                float fp = Clampf((speedKmh - F3_SPD_LO)
                                                  / (F3_SPD_HI - F3_SPD_LO), 0.0f, 1.0f);
                                float fd = Clampf((speedKmh - F3_SPD_LO)
                                                  / (F3_SPD_HI_DUR - F3_SPD_LO), 0.0f, 1.0f);
                                f3TapDur  = (DWORD)(PED_MS_LO  + (PED_MS_HI  - PED_MS_LO)  * fd);
                                f3TapAtk  = (DWORD)(PED_ATK_LO + (PED_ATK_HI - PED_ATK_LO) * fd);
                                f3TapPeak = (LONG)(PED_MAG *
                                            (PED_MAG_LO_SCALE + (1.0f - PED_MAG_LO_SCALE) * fp));
                                /* M1: slider #4 "pedestrians" (handover 2.3 row 4) */
                                f3TapPeak = Mul(f3TapPeak, g_pctPed);
                                g_flip = !g_flip;
                                f3TapSign   = g_flip ? 1 : -1;
                                f3TapSmooth = PED_SMOOTH;
                                f3TapStart  = now;
                                SetMag(0);
                                kickUntil  = now + f3TapDur;
                                kickIsTap  = 1;      /* a crash still preempts a body */
                                pedLastTap = now;
                                pedFired   = 1;
                                Log(K_FIRE, 0xCB, (DWORD)f3TapPeak, (DWORD)pedCount,
                                    (DWORD)(LONG)(speedKmh * 100.0f));
                            } else if (pedCount == 1) {
                                /* first touch of an episode: logged, not felt - this is
                                   what a DODGE looks like when it stops here */
                                Log(K_INIT, 0xCC, (DWORD)pedCount,
                                    (DWORD)(LONG)(speedKmh * 100.0f), 0);
                            }
                        }
                    }
                }
            }

            /* v7.10 vector watch (8ms) - re-baselined on a car change so swapping cars
               is never logged as a burst of list activity. */
            if (car != vwCar) { vwCar = car; vwInit = 0; }
            VectorWatch(car, vwShadow, &vwInit);

            /* HI-RES judder log every 4 ticks (~32ms): a=finalSpring b=finalDamp
               c=(carNow==0) dropout flag  d=spd*100. Lets the offline judder test measure
               per-tick spring/damper deltas and capture-dropout rate (256ms telemetry aliases
               fast judder). */
            {
                static DWORD s_hr = 0;
                if ((++s_hr & 3) == 0)
                    Log(K_INIT, 0x70, (DWORD)finalSpring, (DWORD)finalDamp,
                        (carNow == 0) ? 1u : 0u);
            }

            /* K_STEER: every ~256ms baseline + on every slip activation.
               a=raw*100(cm/s)  b=ema*100
               c=latSlipDeg*100 SIGNED (+=one way, -=other)  d=slipMult*1000 (combined)
               K_TELEM: what actually reached the wheel + speed
               a=finalSpring  b=finalDamp  c=speedKmh*100  d=latSlipMult*1000 (lateral alone) */
            {
                static DWORD s_lp = 0; static int s_la = 0;
                int active = slipMult < 1.0f;
                if ((++s_lp & 0x1F) == 0 || (active && !s_la)) {
                    Log(K_STEER,
                        (DWORD)(LONG)(spd    * 100.0f),
                        (DWORD)(LONG)(spdEma * 100.0f),
                        (DWORD)(LONG)(latSlip * 5729.578f),
                        (DWORD)(slipMult * 1000.0f));
                    Log(K_TELEM,
                        (DWORD)finalSpring,
                        (DWORD)finalDamp,
                        (DWORD)(LONG)(speedKmh * 100.0f),
                        (DWORD)(latSlipMult * 1000.0f));
                    /* K_BUMP telemetry: yawRate, expectedYaw, disturb (rad/s x10000), 0 */
                    Log(K_BUMP,
                        (DWORD)(LONG)(yawRate     * 10000.0f),
                        (DWORD)(LONG)(expectedYaw * 10000.0f),
                        (DWORD)(LONG)(disturb     * 10000.0f),
                        0);
                }
                s_la = active;
            }

            /* ---- Damage kick (v7.4h: split real-collision vs bullet storm) ---- */
            DWORD car = g_car;
            int dmg = *(volatile int *)(car + DMG_OFF);
            /* ---- v7.58-gog1, 2026-08-04: THIS CHANNEL HAS TO SAY WHEN IT SEES NOTHING ----
             * The drive covered 534 s on GOG, shot at until dying in the car, and no bullet
             * taps. The log agreed - 0 events - and that was the whole of what it said. Measured
             * afterwards from the same run's car_snap: `car+0x29E4` is CONSTANT ZERO on GOG,
             * 8439 samples, one distinct value. It is the counter on the CUSTOM build only.
             *
             * The channel could not report that, and could not have: every branch below is
             * conditional and none of them has an else. A channel that is DEAD on a whole build
             * and a channel with nothing to report produce identical logs - which is precisely
             * the failure this project has a rule against ("an instrument must print 0 hits out
             * loud"). Three lines, all K_INIT so they cost nothing in the FIRE channel:
             *
             *   0xD2  once a second: the raw field, so "always zero" is visible in the log at
             *         the time rather than in a snapshot afterwards. a=dmg b=prevDmg
             *         c=the offset being read, so a log alone says WHICH field was watched.
             *   0xD3  a change was seen but REJECTED, with the reason. This is the line whose
             *         absence made a wrong offset look like a quiet drive.
             *         a=prevDmg b=dmg c=delta d=reason (1 = delta >= 50 i.e. not a counter,
             *         2 = the field went DOWN, 3 = first sample after a car change)
             */
            {
                static DWORD s_dmgSay = 0;
                DWORD nowMs = GetTickCount();
                if (nowMs - s_dmgSay >= 1000u) {
                    s_dmgSay = nowMs;
                    Log(K_INIT, 0xD2, (DWORD)dmg, (DWORD)prevDmg, (DWORD)DMG_OFF);
                }
                if (prevDmg >= 0 && dmg != prevDmg) {
                    int d = dmg - prevDmg;
                    /* a ring wrap is NOT a rejection any more - it is corrected below, and
                       reporting it here would put a defect line in the log for the fix */
                    int wrap = (d < 0 && prevDmg >= 95 && dmg <= 4);
                    DWORD why = wrap ? 0u : ((d < 0) ? 2u : ((d >= 50) ? 1u : 0u));
                    if (why)
                        Log(K_INIT, 0xD3, (DWORD)prevDmg, (DWORD)dmg, why);
                } else if (prevDmg < 0) {
                    Log(K_INIT, 0xD3, 0xFFFFFFFFu, (DWORD)dmg, 3u);
                }
            }
            /* ---- v7.58-gog2, 2026-08-04: THE FIELD IS A RING INDEX AND IT WRAPS AT 100 ----
             * Not a counter, and the difference has been eating hits on BOTH builds since v7.4h.
             * Verified two independent ways:
             *   - the routine at GOG 0x5B1D50 is byte-identical to CUSTOM 0x5ED9D0 and reads
             *     `inc; cmp 100; jl; mov 0` - it indexes 100 records of 88 bytes at car+0x075C,
             *     which end at car+0x29BC, immediately before the index itself;
             *   - archived drives on the custom install: 187 damage events in
             *     one log, values 1..99, never 100, and the value goes DOWN twice. Those two
             *     descents are wraps, and the old code dropped both as "the field decreased".
             * So one hit in a hundred was silently lost per wrap, on the install where this
             * channel works. Corrected here rather than upstream because upstream has formally
             * stopped; it is written up in FORK.md and sent to them.
             * STRICT on purpose: only a near-top -> near-bottom step on the SAME car counts as a
             * wrap. A new car starts its ring at 0, which looks identical to a wrap, and inventing
             * a bullet tap when the player gets into a fresh car is worse than losing one. */
            int delta = 0;
            {
                static DWORD s_dmgCar = 0;
                if (prevDmg >= 0) {
                    delta = dmg - prevDmg;
                    /* The correction goes into the DELTA, never into `dmg` itself: `prevDmg`
                       must keep holding the raw ring value, or the next comparison is against a
                       number the field can never reach again. */
                    if (delta < 0 && car == s_dmgCar && prevDmg >= 95 && dmg <= 4) {
                        delta += 100;
                        Log(K_INIT, 0xD5, (DWORD)prevDmg, (DWORD)dmg, (DWORD)delta);
                    }
                }
                s_dmgCar = car;
            }
            if (prevDmg >= 0 && delta > 0 && delta < 50) {
                if (delta >= DMG_CRASH_MIN_DELTA) {
                    /* genuine collision damage: full kick (unchanged) */
                    LONG mag = DMG_MIN_MAG + delta * DMG_SCALE;
                    if (mag > MAX_MAG) mag = MAX_MAG;
                    /* M1: slider #2 "crashes and rams" (handover 2.3 row 2) - the reference
                       is the ceiling here, so the MAX_MAG clamp above runs first at 100%
                       and stays bit-identical; the multiplier can only move it once he
                       deliberately leaves the reference (open decision #1). */
                    mag = Mul(mag, g_pctCrash);
                    g_flip = !g_flip;
                    SetMag(g_flip ? mag : -mag);
                    kickUntil = now + DMG_PULSE_MS; kickIsTap = 0;
                    Log(K_FIRE, 0xD0, (DWORD)mag, (DWORD)delta, (DWORD)dmg);
                } else if (DMG_BULLET_ENABLE && !kickUntil
                           && (now - lastBulletTap) >= DMG_BULLET_MIN_MS) {
                    /* bullets / light damage: faint RATE-LIMITED tap, never a violent slam.
                       Extra increments inside the min-interval are ignored (prevDmg still
                       advances) so a Tommy-gun burst = a gentle tick-tick, not a shake. */
                    LONG bmag = Mul((LONG)DMG_BULLET_MAG, g_pctGun);   /* M1: slider #5 "gunfire" */
                    g_flip = !g_flip;
                    SetMag(g_flip ? bmag : -bmag);
                    kickUntil = now + DMG_BULLET_MS; kickIsTap = 0;
                    lastBulletTap = now;
                    Log(K_FIRE, 0xD1, (DWORD)bmag, (DWORD)delta, (DWORD)dmg);
                }
            }
            prevDmg = dmg;

            /* ---- v7 bump-from-behind kick ----
               yaw the driver did not command (disturb) that arrives as a sudden STEP (jerk),
               while not already sliding, = an external hit rotating the car. Kick the wheel
               in the rotation direction. Shares ConstantForce/kickUntil with crash+damage. */
            {
                float jerk = disturb - disturbPrev;
                if (jerk < 0.0f) jerk = -jerk;
                /* v7.1e: bump kick DISABLED. It false-fired ~2x/sec on heading/yaw noise
                   (yawRate from d(heading)/dt at 125 Hz is noisy) -> directional SetMag(+-)
                   yanked the wheel violently L/R (reported as tearing left and right). judder_test now
                   flags bump-rate > 0.3/sec. Re-enable only with a smoothed yaw signal and
                   calibrated YAW_GAIN/BUMP_THRESH. Telemetry (periodic K_BUMP) still logs. */
                if (0 && haveHead && haveHeadPrev && !kickUntil &&
                    Absf(disturb) > BUMP_THRESH && jerk > BUMP_JERK &&
                    Absf(latSlip) < BUMP_BODYSLIP_MAX) {
                    LONG bmag = (LONG)(Absf(disturb) * BUMP_MAG_SCALE);
                    if (bmag > MAX_MAG) bmag = MAX_MAG;
                    g_flip = !g_flip;
                    SetMag(disturb > 0.0f ? bmag : -bmag);
                    kickUntil = now + BUMP_PULSE_MS; kickIsTap = 0;
                    Log(K_BUMP, (DWORD)(LONG)(yawRate * 10000.0f),
                                (DWORD)(LONG)(disturb * 10000.0f),
                                (DWORD)bmag, 0xB0);
                }
            }

            /* ---- v7.23 F6: being SPUN (police PIT maneuver) ----
               Fires on rotation the driver did not command, once the trailing 2 s window
               has accumulated enough of it AND the three gates agree it was not just a
               hard corner. Deliberately LATE by design: the window has to fill before the
               classes separate at all (see the constants block and f6_late.py). */
            /* (v7.25: the F6 kick that used to sit here is gone - the spin is now a
               sustained force on the wallow channel, further down. See the block marked
               `BEING SPUN, as a sustained steer instead of a kick`.) */

            /* ---- Ground speed + its recent drop (for the crash veto) ---- */
            float gsNow = 0.0f, gsDrop = 0.0f;
            if (posCount > GS_LOOK) {
                int i0 = (posCount - 1) & 15;
                int ib = (posCount - 1 - GS_LOOK) & 15;
                float gdx = posRingX[i0] - posRingX[ib];
                float gdy = posRingY[i0] - posRingY[ib];
                float gd  = Sqrtf(gdx*gdx + gdy*gdy);
                if (gd < 50.0f)                    /* ignore level-load teleports */
                    gsNow = gd / (GS_LOOK * POLL_MS * 0.001f);
            }
            gsRing[gsRn % GS_RING_SZ] = gsNow; gsRn++;

            /* v7.60 fire trace, part 1 of 2: keep the last TRACE_PRE ticks, and emit the tail
               of a trace armed on an earlier tick. Runs BEFORE this tick's detectors, so the
               firing tick itself lands in the ring and is dumped as index 0. Reads nothing the
               detectors read and writes nothing they read - it is a tap on the wire. */
            {
                /* v7.60b, after asking whether the 50 ms window should become 8 or 15.
                   `gsNow` is NOT instantaneous - it is a distance over GS_LOOK = 6 ticks,
                   about 92 ms of smoothing, which is the same order as the ~130 ms shape the
                   jab class lives on. A trace built on it alone would have blurred exactly
                   what it was built to see. `trRaw` is the SINGLE-TICK step instead: the
                   distance the car moved since the previous tick, divided by the tick, which
                   is the finest ground truth this build can produce without touching the poll
                   rate. Both are kept, because gsNow is what the DETECTOR reads and trRaw is
                   what the CAR did, and telling those apart is the whole exercise. */
                static float trSpd[TRACE_PRE], trEma[TRACE_PRE], trGs[TRACE_PRE], trRaw[TRACE_PRE];
                static DWORD trN = 0;
                float rawStep = 0.0f;
                if (posCount > 1) {
                    float rdx = posRingX[(posCount - 1) & 15] - posRingX[(posCount - 2) & 15];
                    float rdy = posRingY[(posCount - 1) & 15] - posRingY[(posCount - 2) & 15];
                    float rd  = Sqrtf(rdx*rdx + rdy*rdy);
                    if (rd < 50.0f) rawStep = rd / (POLL_MS * 0.001f);
                }
                trSpd[trN % TRACE_PRE] = spd;
                trEma[trN % TRACE_PRE] = spdEma;
                trGs [trN % TRACE_PRE] = gsNow;
                trRaw[trN % TRACE_PRE] = rawStep;
                trN++;

                /* v7.63: the same step, kept deeper and at file scope so the ceiling guard can
                   read it. Deliberately NOT reusing trRaw: that ring is 16 deep and the guard
                   needs ticks -16..0, i.e. seventeen of them. Written here so the guard sees
                   the CURRENT tick - this block runs earlier in the same tick than the fire. */
                CeilPush(&g_ceil, rawStep);

                /* ---- v7.62, 2026-08-06: COUNT THE ONE-TICK SPEEDOMETER DROPOUTS -------------
                 * Found the same day in the v7.60 fire trace: the two 25000-ceiling fires of
                 * that drive were BOTH armed by a single raw sample at ~0.3 m/s with the samples
                 * either side intact, and on the one flagged as unwanted the twelve ticks before it
                 * were smooth and RISING - the car was accelerating through its own "crash".
                 *
                 * WHY THIS COUNTER HAD TO EXIST BEFORE ANY FIX. The obvious next move was to
                 * price the class against the archive, and the archive CANNOT answer: the only
                 * per-tick speed it carries is `K_TELEM 0x75`, and that column is the EMA, not
                 * the raw reading. Checked rather than assumed - at t+596328 ms of the
                 * 2026-08-06 drive the telemetry says 14.84, which is exactly `spdEma` from the
                 * trace, while the raw 0.30 that caused it appears nowhere. So a scan over
                 * 846975 archived ticks returned zero, and that zero is a property of the
                 * INSTRUMENT, not of the game. `src\price_dropout.py` records that dead end.
                 *
                 * LOGGING ONLY. No detector, constant, force or duration moves - the shape is
                 * counted, nothing is refused. A fix cannot be proposed anyway until the two
                 * ceiling fires are separable, and one of them is an effect that was approved.
                 *
                 *   0xEA  one dropout: a = the sample x100, b = the one before, c = the one after
                 *   0xEB  every 30 s: a = dropouts so far, b = moving ticks so far
                 */
                {
                    static float s_d0 = 0.0f, s_d1 = 0.0f;   /* two ticks back, one tick back */
                    static DWORD s_dropN = 0, s_moveN = 0, s_daySay = 0;
                    if (spd > 5.0f) s_moveN++;
                    if (s_d1 < 1.0f && s_d0 > 5.0f && spd > 5.0f) {
                        s_dropN++;
                        Log(K_INIT, 0xEA, (DWORD)(LONG)(s_d1 * 100.0f),
                            (DWORD)(LONG)(s_d0 * 100.0f), (DWORD)(LONG)(spd * 100.0f));
                    }
                    s_d0 = s_d1; s_d1 = spd;
                    /* The rate is printed OUT LOUD on a timer, so a drive with no dropouts says
                       so in its own log instead of looking like a drive nobody instrumented. */
                    DWORD nowT = GetTickCount();
                    if (nowT - s_daySay >= 30000u) {
                        s_daySay = nowT;
                        Log(K_INIT, 0xEB, s_dropN, s_moveN, 0);
                    }
                }
                if (g_trLeft) {
                    Log(K_INIT, 0xE7, (DWORD)(LONG)(LONG)(TRACE_POST - g_trLeft),
                        (DWORD)(LONG)(spd * 100.0f), (DWORD)(LONG)(spdEma * 100.0f));
                    Log(K_INIT, 0xE9, (DWORD)(LONG)(TRACE_POST - g_trLeft),
                        (DWORD)(LONG)(gsNow * 100.0f), (DWORD)(LONG)(rawStep * 100.0f));
                    g_trLeft--;
                }
                if (g_trArm) {                 /* a fire on the PREVIOUS tick asked for a dump */
                    g_trArm = 0;
                    DWORD have = trN < TRACE_PRE ? trN : TRACE_PRE;
                    for (DWORD q = have; q >= 1; q--) {
                        DWORD idx = (trN - q) % TRACE_PRE;
                        Log(K_INIT, 0xE7, (DWORD)(LONG)(-(LONG)q),
                            (DWORD)(LONG)(trSpd[idx] * 100.0f),
                            (DWORD)(LONG)(trEma[idx] * 100.0f));
                        Log(K_INIT, 0xE9, (DWORD)(LONG)(-(LONG)q),
                            (DWORD)(LONG)(trGs[idx] * 100.0f),
                            (DWORD)(LONG)(trRaw[idx] * 100.0f));
                    }
                    g_trLeft = TRACE_POST;
                }
            }

            float gsMax = gsNow;
            for (int gi = 0; gi < GS_RING_SZ; gi++) if (gsRing[gi] > gsMax) gsMax = gsRing[gi];
            gsDrop = gsMax - gsNow;   /* how much real ground speed fell vs its recent peak */
            gsDropPrev = gsDrop;      /* v7.4f: next tick's PITCH brake gate reads this */

            /* v7.4o crash gate: TIGHT short-window ground-speed drop. The old veto (gsDrop over
               320ms > 2.5) let false kicks through - a clean single-instance drive showed 8
               crash kicks all with the GROUND speed STEADY (decel <=2.2 m/s), i.e. braking/bumps
               misread via noisy WHEEL speed, not real impacts. A real crash collapses ground
               speed within ~150ms. So require a big drop over a SHORT window; excludes all 8
               false kicks, keeps real crashes. */
            float gsShortMax = gsNow;
            {
                int lim = gsRn < GS_SHORT_N ? gsRn : GS_SHORT_N;
                for (int gi = 1; gi <= lim; gi++) {
                    float v = gsRing[(gsRn - gi) % GS_RING_SZ];
                    if (v > gsShortMax) gsShortMax = v;
                }
            }
            float gsShortDrop = gsShortMax - gsNow;

            /* ---- v7.41: is the ENTRY SPEED real, or a 2-3 sample engine spike? ----
               The 170.8 s phantom (three F8 presses): spdEma 10.7 and RISING, yet gsShortMax
               read 26.47 - a speed the car never travelled at. The zero-delay "entry vs EMA"
               consistency gate was tried first and REFUTED by measurement: real liked crashes
               sit at ratio 1.7-2.5 and the phantom at 2.47, inside the real distribution
               (src/ ratio sweep, 73 crashes, 3 logs). What DOES differ is structural:
               [[phantom-crash-spikes]] records the spike lasts 16-24 ms = 2-3 samples of this
               ring, while a real entry speed is the car's actual travel and OCCUPIES the
               window. So: count how many short-window samples lie within 80% of the claimed
               entry. A spike scores 2-3; a real crash scores 6-19 even under hard pre-impact
               braking. Threshold 4, checked at fire time only - zero added latency.
               NOT verifiable from old logs (per-sample gs was never logged), so every fire and
               every veto logs the count: 0xE0 = vetoed(a=occupancy b=entry*100 c=drop*100),
               and the next drive confirms or retunes the threshold. */
            int gsOcc = 0;
            {
                int lim = gsRn < GS_SHORT_N ? gsRn : GS_SHORT_N;
                float occFloor = gsShortMax * 0.8f;
                for (int gi = 1; gi <= lim; gi++)
                    if (gsRing[(gsRn - gi) % GS_RING_SZ] >= occFloor) gsOcc++;
            }

            /* v7.42: the speedometer's peak over the same window, for the bypass's
               position-glitch test. Written before the test, exactly like gsRing, so
               the current sample counts in both. Read by the bypass ONLY. */
            emaRing[emaRn % GS_SHORT_N] = spdEma; emaRn++;
            float emaShortMax = spdEma;
            {
                int lim = emaRn < GS_SHORT_N ? emaRn : GS_SHORT_N;
                for (int ei = 1; ei <= lim; ei++) {
                    float v = emaRing[(emaRn - ei) % GS_SHORT_N];
                    if (v > emaShortMax) emaShortMax = v;
                }
            }

            /* ---- v7.5 F5: INCOMING impact while parked ----
               Deliberately a SEPARATE branch from the crash detector below: it shares no
               constant with it, so tuning F5 can never regress crashes (the most valued
               effect). The crash branch requires gsNow > CRASH_MIN_GS (car moving) and a
               DROP in own speed; F5 requires the opposite (own speed ~0, ground speed
               RISING), so the two can never both fire on the same event. */
            if (spdEma < F5_PARK_W) {
                if (f5Hold < F5_HOLD_TICKS) f5Hold++;
            } else {
                f5Hold = 0;
            }
            if (f5Hold >= F5_HOLD_TICKS && !kickUntil && now >= f5Rearm
                && spd < F5_MOVE_W                 /* we are still not driving      */
                && gsNow > F5_TRIG                 /* but the world moved us        */
                && gsNow < F5_TELE_VETO) {         /* and it was not a level load   */
                float e = gsNow - F5_TRIG;
                LONG mag = F5_MIN_MAG + (LONG)(e * F5_SCALE);
                if (mag > F5_MAX_MAG) mag = F5_MAX_MAG;
                mag = Mul(mag, g_pctCrash);   /* M1: slider #2, "the F5 parked shove" */
                DWORD ms = F5_BASE_MS + (DWORD)(e * F5_PER_GS);
                if (ms > F5_CAP_MS) ms = F5_CAP_MS;
                g_flip = !g_flip;
                SetMag(g_flip ? mag : -mag);
                kickUntil = now + ms;
                kickIsTap = 0;
                f5Rearm  = now + F5_REARM_MS;
                f5Hold   = 0;
                Log(K_FIRE, 0xC5, (DWORD)mag, (DWORD)(LONG)(gsNow * 1000.0f),
                    (DWORD)(LONG)(spd * 1000.0f));
            }
            /* DIAG 0xC6: every near-miss while parked (ground speed moved at all but did
               not clear the trigger). Tells us offline whether a GENTLE push the user
               felt cheated on was below F5_TRIG or vetoed, without another drive. */
            if (f5Hold >= F5_HOLD_TICKS && gsNow > 0.15f && gsNow <= F5_TRIG) {
                static DWORD s_nm = 0;
                if ((++s_nm & 15) == 0)
                    Log(K_INIT, 0xC6, (DWORD)(LONG)(gsNow * 1000.0f),
                        (DWORD)(LONG)(spd * 1000.0f), f5Hold);
            }

            /* ---- Crash kick ---- */
            float oldest = hist[(hn + 1) & 3];
            hist[hn & 3] = spdEma;
            hn++;
            float drop = oldest - spdEma;

            /* v7.49: spdEma history for the bypass shape gate. One slot per FFB tick, which
               is exactly the sampling of the 0x75 telemetry the gate was calibrated on
               offline - so the runtime metric and the archive metric are the same number,
               not two things that resemble each other. */
            stepRing[stepRn % EMA_RING_SZ] = spdEma; stepRn++;

            /* v7.58 (ported 2026-08-03): the same sample, kept long enough to see the speed the
               car ARRIVED with. stepRing only holds ~780 ms; complaint #4 needs 1500. Stamped
               with the tick so the lookback is real milliseconds rather than a tick count.
               A THIRD spdEma ring, deliberately: `emaRing` is v7.42's 19-slot entry-sanity
               window and `stepRing` is v7.49's 48-slot shape window. Lengthening either one
               would move a gate whose calibration depends on its length. */
            entryRingV[entryRn % ENTRY_RING_SZ] = spdEma;
            entryRingT[entryRn % ENTRY_RING_SZ] = now;
            entryRn++;

            /* ---- v7.37: THE CALM FILTER WAS VETOING THE CRASHES IT WAS MEANT TO PASS ----
               Found on the 2026-07-24 drive. He hit a pole at 24.5 m/s ground speed after
               riding a curb; the raw ground drop reached 23.6 m/s and NOTHING came out.
               Both speed gates passed from the second tick onward. `calm` was what blocked it.

               The ring is written AFTER this test, so it holds ticks -1..-4 - which means the
               crash's OWN first tick is in the window. That tick measured a drop of 1.78,
               above CALM_THRESH 1.5, and the raw gate does not pass until tick 2. The two can
               therefore never agree on a violent impact: by the time the drop is big enough to
               qualify, its own onset has already declared the road un-calm.

               The rule this produced in the game is exactly backwards - a crash with a gentle
               onset fires, a crash with a violent one does not. Confirmed on the same drive:
               the crash at 663.3 s that DID fire started at 0.88, under the threshold.

               `calm` means the road was quiet BEFORE this event, so it must look before the
               event. Skipping the newest two slots does that, and eight slots keep six samples
               of history, so the filter is not weakened - only moved off the onset. */
            int calm = 1;
            for (int q = 3; q <= 8; q++)
                if (dropHist[(dn - q) & 7] >= CALM_THRESH) { calm = 0; break; }
            float thresh = CRASH_BASE + CRASH_K * spdEma;

            /* DIAG 0x71: any speed drop over ~0.6 m/s (near-crash) - shows whether the crash
               signal exists and why it did/didn't fire. a=drop*1000 b=thresh*1000 c=spdEma*1000
               d=(calm | kickUntil<<1). Lets me confirm crashes are detectable from the log. */
            /* v7.36: d now carries the GATE's own numbers - what the short window actually
               dropped and what it had to beat - so a silent impact can be told apart from an
               impact that was never seen. Packed: gsShortDrop*100 | (gsNeed*100)<<16. */
            if (drop > 0.6f)
                Log(K_INIT, 0x71, (DWORD)(LONG)(drop*1000.0f), (DWORD)(LONG)(thresh*1000.0f),
                    (DWORD)(LONG)(spdEma*1000.0f));
            if (drop > 0.6f)
                Log(K_INIT, 0x73, (DWORD)(LONG)(gsShortDrop * 100.0f),
                    (DWORD)(LONG)(CrashDropNeeded(gsShortMax) * 100.0f),
                    (DWORD)(LONG)(gsShortMax * 100.0f));
            /* v7.57 DIAG 0xED (ported 2026-08-03): the CHASSIS SHOCK at the classification tick.
               Seven discriminators have been refuted against the braking misclassification
               (#7/#8/#9) and every one was a function of the SPEED TRACE, which is exactly
               why they failed: braking to a stop and braking INTO something overlap on that
               trace - the genuine brakes measure gsDrop/gsShortDrop 1.56-1.82, straddling
               BRAKE_RATIO 1.6, and a real impact of his sits at 1.72 inside them. So the
               signal has to come from CONTACT instead, and the only unused channel is the
               attitude the wallow code already reads every tick and has never written down.
               `car_mtx` samples it at ~50 ms, far too coarse for an impact transient against
               a 15.4 ms loop, so the archive cannot settle it and this can.
               a = shkD1*100000 (pitch jolt, signed), b = shkD7*100000 (roll jolt, signed),
               c = |pitchHP|*10000 | (|rollHP|*10000)<<16, both clamped to 16 bits.
               NOTHING is gated on it - diagnostics only, and it is the ONE reason the 0xEB and
               0xEC diagnostics of v7.54/v7.55 were left upstream: those price a detection
               change this fork was told not to make, while 0xED is what makes the ported
               entry gate auditable from a drive. */
            if (drop > 0.6f) {
                float ap = Absf(pitchHP) * 10000.0f, ar = Absf(rollHP) * 10000.0f;
                if (ap > 65535.0f) ap = 65535.0f;
                if (ar > 65535.0f) ar = 65535.0f;
                Log(K_INIT, 0xED, (DWORD)(LONG)(shkD1 * 100000.0f),
                    (DWORD)(LONG)(shkD7 * 100000.0f),
                    ((DWORD)(LONG)ap) | (((DWORD)(LONG)ar) << 16));
            }

            /* v7.36: the gate now reads the speed the car CARRIED INTO the event, not the one
               it was left with. gsShortMax is the peak of the same ~150 ms window gsShortDrop
               is measured over, so the two are consistent by construction. */
            float gsNeed = CrashDropNeeded(gsShortMax);
            /* v7.41: a sharp crash may PREEMPT an active soft nudge (kickIsSoft), same as it
               preempts taps. `preempting` guards the soft path below so the nudge cannot
               machine-gun itself by re-entering its own window. */
            int preempting = (kickUntil && kickIsSoft);
            /* v7.54 (ported 2026-08-03): `drop > thresh` OR an unmistakable ground-speed
               collapse. See GS_ARM_K.
               `gsShortDrop > gsNeed` is still required on BOTH paths, so this only ever
               relaxes the speedometer half of a two-part test - it can never arm on a tick
               the ground gate rejected. */
            if (calm && (!kickUntil || kickIsTap || kickIsSoft)
                && (drop > thresh
                    || (gsShortDrop > gsNeed * GS_ARM_K && drop > thresh * GS_ARM_AGREE))
                && drop < TELE_DROP
                && gsShortDrop > gsNeed && gsShortMax > CRASH_MIN_GS) {
                /* v7.4p: classify the event.
                   - Nearly stopped (gsNow<=CRASH_MIN_GS): blocked above = kills parked wheel-
                     speed-noise false kicks.
                   - SUSTAINED decel (long-window drop >> short-window = braking) OR in a SLIDE
                     (high slip): a SOFT curb-like lockup nudge, NOT a slam. This is the user's
                     reported as braking to the floor producing a soft nod like a curb (also covers the race car's very
                     hard braking and slide-scrub). Real crashes still register (soft in a slide
                     is acceptable; sharp when cruising).
                   - SUDDEN drop while cruising (not braking, not sliding) = a real impact: sharp. */
                int braking = (gsDrop > gsShortDrop * BRAKE_RATIO);
                int soft = braking || (slipDeg > SLIP_SOFT_DEG);
                if (soft && !preempting) {
                    /* ---- v7.31: the slide nudge now follows the wheel's OWN weight ----
                       Found on the v7.30 drive: this 2200 nudge fires inside every hard
                       slide (8 of 23), in pairs 150-300 ms apart, because a slide scrubs
                       speed hard enough to pass both deceleration gates and `slipDeg > 20`
                       routes it here by construction. It reads as an unexplained kick for a
                       reason that is not its magnitude: in a deep slide the slip lightening
                       has already dropped the spring to 558 and the damper to 1244, about
                       15% of their normal 2652/5254 - but the kick is a ConstantForce and
                       never saw that multiplier. Full-strength force into an almost empty
                       wheel.
                       `dampMult` is exactly the factor the damper already gets: min(bell, 1),
                       so it NEVER amplifies. Braking in a straight line has no slip, the
                       multiplier is 1.0, and his "nod like a curb" is untouched to the byte.
                       Scoped to this branch alone - the sharp crash 0xC0, the object tap, the
                       ped tap, damage, bullets and the curb channel are all left alone, which
                       is what he asked for: only the slide kick changes. */
                    LONG mag = pf->slideScale
                             ? (LONG)(BRAKE_SOFT_MAG * dampMult)
                             : BRAKE_SOFT_MAG;
                    g_lastSoftMag = mag;
                    g_lastSoftTick = now;   /* v7.49: the bypass shape gate reads this */
                    g_flip = !g_flip;
                    SetMag(g_flip ? mag : -mag);
                    kickUntil = now + BRAKE_SOFT_MS; kickIsTap = 0; kickIsSoft = 1;
                    /* d now carries the applied multiplier x1000, so the next log says what
                       the wheel actually got instead of leaving it to be inferred. */
                    Log(K_FIRE, 0xC1, (DWORD)mag, (DWORD)(LONG)(gsShortDrop*1000.0f),
                        (DWORD)(LONG)(dampMult*1000.0f));
                    Log(K_INIT, 0xE8, 0xC1u, (DWORD)mag, (DWORD)(LONG)(spdEma*1000.0f));
                    g_trArm = 1;   /* v7.60 fire trace */
                } else if (!soft && (now - g_lastCrashTick) >= CRASH_REFRACTORY_MS) {
                    if (gsOcc < GS_OCC_MIN) {
                        /* v7.41: the claimed entry speed was never actually travelled - a
                           2-3 sample engine spike (the 170.8 s phantom read 26.5 m/s while
                           the car cruised at 10.7). Veto and log; zero latency. */
                        Log(K_INIT, 0xE0, (DWORD)gsOcc,
                            (DWORD)(LONG)(gsShortMax * 100.0f),
                            (DWORD)(LONG)(gsShortDrop * 100.0f));
                    /* v7.58, DELIBERATE ASYMMETRY - do not "fix" this to spdForce.
                       This guard decides whether the shape veto is ALLOWED to silence the
                       kick, and it keeps reading the old `spdEma` on purpose, so the veto
                       behaves exactly as it does today. Feeding it the raised entry-speed
                       force would let more kicks clear RAMP_NEVER_MAG and change WHICH
                       events get silenced - a detection change smuggled in behind a force
                       change. Invariant I6 in upstream src/v751_audit.py exists to catch
                       precisely that, and it passes only because this line was left alone.
                       It is also why this call site alone carries no `g_pctCrash`: it is a
                       CLASSIFIER, not a force, and a slider that moved it would move which
                       crashes are silenced (see memory\slider-vs-detector.md). */
                    } else if (RefCrashForce(drop - thresh, spdEma) < RAMP_NEVER_MAG
                               && RampDup(now, StepRatio(stepRing, stepRn))) {
                        /* ---- v7.50: THE SAME LURCH REACHES THE NORMAL BRANCH ----
                           v7.49 gated the bypass and his very next drive proved that was
                           only half of it. Three straight-line brakes to a stop:
                             61.8 s, 60->0 km/h : nudge only, two bypass jabs vetoed. Right.
                             73.1 s, 50->0 km/h : nudge, then a NORMAL sharp of 10730 at
                                                  +31 ms.
                             85.8 s, 53->0 km/h : no nudge at all, sharps of 13093 and 6497
                                                  316 ms apart.
                           Same artefact, other branch. The end-of-stop deceleration ramp can
                           flip `braking` false on its own, and then the normal branch fires
                           sharp with nothing to gate it.

                           Scope, measured over the whole archive (src/normal_branch_ramp.py,
                           201 normal-branch sharps with telemetry): only 6 of them land
                           within 400 ms of a nudge and only 9 within 400 ms of a previous
                           sharp. Requiring step < 10 on top leaves FIVE vetoed fires in 48
                           drives, and BOTH of his 25000 crashes-during-braking survive at
                           step 59.3 and 24.4 - those are the ones [[crash-detection-v741]]
                           records as legitimately sharp while braking, and they must never
                           be lost. Margin: highest vetoed 9.4 vs lowest kept 14.0 = 1.49x.

                           Raising CRASH_REFRACTORY_MS instead was considered and REJECTED by
                           the same measurement: 9 fires sit within 400 ms of a previous
                           sharp, but 7 of them have an INFINITE step ratio - no deceleration
                           at all beforehand - i.e. they are genuine second impacts in a
                           multi-object crash. A blanket refractory would eat them; the shape
                           test takes only the two that are ramps. */
                    } else {
                        /* v7.40: the LOCKED reference force. v7.58 (ported 2026-08-03) changes
                           ONE input to it, and only above ENTRY_GATE_MS - see the constants
                           block. Below the gate `spdForce` IS `spdEma`, so the low-speed range
                           tuned by hand is bit-identical, not merely close. */
                        float excess = drop - thresh;
                        float entrySpd = spdEma;
                        for (int ei = 0; ei < ENTRY_RING_SZ; ei++) {
                            if (entryRingT[ei] == 0) continue;
                            if ((DWORD)(now - entryRingT[ei]) > ENTRY_LOOK_MS) continue;
                            if (entryRingV[ei] > entrySpd) entrySpd = entryRingV[ei];
                        }
                        /* a peak this high is an engine spike, not a speed: fall back to
                           today's behaviour rather than slamming the wheel on a glitch. */
                        if (entrySpd > ENTRY_SANITY_MS) entrySpd = spdEma;
                        float spdForce = (entrySpd > ENTRY_GATE_MS) ? entrySpd : spdEma;
                        /* v813: the mod's own trim goes FIRST and the user's slider second, so
                           `crash = 100` still means "the mod as designed". See g_crashTrimPct -
                           and note it is applied here rather than inside RefCrashForce for the
                           reason written in that function: its third caller is a DETECTOR. */
                        /* v820: the trim follows the TRUE entry speed - see CrashTrimFor. With
                           no ramp (F4, or an ini without the keys) it is g_crashTrimPct exactly. */
                        float trueEntry = TrueEntryKmh(now);
                        LONG trimNow = CrashTrimFor(trueEntry, g_crashTrimPct,
                                                    g_crashRampLo, g_crashRampHi);
                        /* v825: capped at the v814 ceiling BEFORE the slider (M1 #2), so the
                           ceiling scales with it - see CrashKickMag. magRef is the same kick at
                           100%, and it is what the ceiling guard below reads. */
                        LONG magRef;
                        LONG mag = CrashKickMag(RefCrashForce(excess, spdForce), trimNow, &magRef);
                        /* v820 0x3B, one per kick: a = the true entry speed km/h x100, b = the
                           trim applied, c = the magnitude sent. What the next drive is judged by. */
                        Log(K_INIT, 0x3B, (DWORD)(LONG)(trueEntry * 100.0f), (DWORD)trimNow,
                            (DWORD)mag);
                        /* v7.58 DIAG 0xEE: everything needed to audit this change from the
                           log alone - what the car arrived with, what the old code would have
                           used, whether the gate applied, and both magnitudes side by side.
                           a = entrySpd*100, b = spdEma*100,
                           c = magOld | magNew<<16 (both <= MAX_MAG 25000, so both fit).
                           Fork: the two fields go through the SAME arithmetic, so they stay
                           comparable at any slider setting. v825: both are now the magnitude
                           AT 100% (CrashKickMag's guardMag) - the slider is the same factor on
                           both sides, and at 400% a scaled 100000 no longer fits 16 bits. At
                           100% the row is exactly the v824 one. */
                        {
                            LONG magOld;
                            CrashKickMag(RefCrashForce(excess, spdEma), trimNow, &magOld);
                            Log(K_INIT, 0xEE, (DWORD)(LONG)(entrySpd * 100.0f),
                                (DWORD)(LONG)(spdEma * 100.0f),
                                ((DWORD)magOld & 0xFFFF) | (((DWORD)magRef & 0xFFFF) << 16));
                        }
                        /* ---- v7.63: a MAXIMUM-force kick has to be corroborated ----
                           0xEC is written at every ceiling fire whether it vetoes or not, so
                           the threshold keeps being priced by ordinary drives.
                             b = ratio x1000 (0xFFFFFFFF = no history, guard stood down)
                             c = the world's step at this tick x100
                             d = the magnitude, with bit 31 set if the kick was VETOED
                                 (at most MAX_MAG x the slider, so the top bit is free)
                           v825: WHICH kicks count as ceiling kicks is decided at 100% (magRef),
                           never by the slider - memory\slider-vs-detector.md. */
                        int ceilVeto = 0;
                        if (magRef >= MAX_MAG && g_ceilAgree > 0.0f) {
                            float ratio = CeilRatio(&g_ceil);
                            ceilVeto = CeilVeto(&g_ceil, g_ceilAgree);
                            Log(K_INIT, 0xEC,
                                (ratio < 0.0f) ? 0xFFFFFFFFu : (DWORD)(LONG)(ratio * 1000.0f),
                                (DWORD)(LONG)(g_ceil.now * 100.0f),
                                (DWORD)mag | (ceilVeto ? 0x80000000u : 0u));
                        }
                        if (!ceilVeto) {
                        DWORD ms = DUR_BASE_MS + (DWORD)(drop * DUR_PER_DROP);
                        if (ms > DUR_CAP_MS) ms = DUR_CAP_MS;
                        SetMag(KickSigned(mag));   /* v7.56: reversal, not more of the same */
                        kickUntil = now + ms;
                        kickIsTap = 0; kickIsSoft = 0;
                        g_lastCrashTick = now;
                        Log(K_FIRE, 0xC0, (DWORD)mag, (DWORD)(drop * 1000.0f),
                            (DWORD)(spdEma * 1000.0f));
                        Log(K_INIT, 0xE8, 0xC0u, (DWORD)mag, (DWORD)(LONG)(drop * 1000.0f));
                        g_trArm = 1;   /* v7.60 fire trace */
                        /* v7.48: arm the phantom cut. `oldest` is spdEma BEFORE this drop -
                           the speed the car is claimed to have lost. Re-checked at +CUT_MS. */
                        cutPre = oldest; cutAt = now + CUT_MS;
                        }
                        /* occupancy diag for threshold tuning: a=occ b=entry c=drop (x100) */
                        Log(K_INIT, 0xE1, (DWORD)gsOcc,
                            (DWORD)(LONG)(gsShortMax * 100.0f),
                            (DWORD)(LONG)(gsShortDrop * 100.0f));
                    }
                }
            }
            dropHist[dn & 7] = drop > 0 ? drop : 0;
            dn++;

            /* ---- v7.48: THE PHANTOM CUT ----
               The kick already fired at full force; this only decides whether its TAIL is
               justified. If the speed the crash claimed to remove has come back, no
               collision happened - stop the force now instead of holding it for the rest of
               the 161 ms. A real crash is still down well beyond CUT_FRAC and is untouched.
               Guarded so it can only ever cut the kick it armed on: cutAt is cleared by any
               later kick taking the wheel (kickUntil moves past it), and by the cut itself. */
            if (cutAt && now >= cutAt) {
                if (kickUntil && cutPre > 1.0f
                    && (cutPre - spdEma) < cutPre * CUT_FRAC) {
                    SetMag(0);
                    kickUntil = 0; kickIsTap = 0; kickIsSoft = 0;
                    /* a=pre*100 b=spdEma*100 c=fraction*10000, so the next drive can confirm
                       nothing but phantoms were cut. */
                    Log(K_INIT, 0xE6, (DWORD)(LONG)(cutPre * 100.0f),
                        (DWORD)(LONG)(spdEma * 100.0f),
                        (DWORD)(LONG)(((cutPre - spdEma) / cutPre) * 10000.0f));
                }
                cutAt = 0;
            }

            /* ---- v7.37: PERSISTENCE-CONFIRMED BYPASS ----
               The second miss he found, and a different mechanism from the calm veto. Hitting
               a LIGHTER car that rolls away costs him almost no speed: the raw ground drop was
               4.4-6.1 m/s against a requirement of 1.7-2.2, while the EMA-smoothed drop sat at
               0.6-0.7 against a threshold of ~0.9. The impact is unmistakable in the raw signal
               and invisible in the smoothed one, because smoothing is precisely what destroys a
               short sharp event. There is no backup signal either - the contact list (0xC8)
               fired zero times in the whole drive, so it sees props, not vehicles.

               So a raw drop far above its requirement gets to fire on its own. The phantom
               spikes that CALM_THRESH and the EMA gate were built against are handled the way
               this project already proved works: [[phantom-crash-spikes]] records that they
               last 16-24 ms and RECOVER FULLY, that median filtering is refuted, and that a
               persistence confirm separates them perfectly, so the bypass waits and checks
               that the speed is still down.

               Cost, stated plainly: a crash that comes through this path arrives PERSIST_MS
               late. 48 ms is two phantom-spike lifetimes and about three physics steps. */
            {
                float gsNeedNow = CrashDropNeeded(gsShortMax);
                int strongRaw = (gsShortDrop > gsNeedNow * BYPASS_X);
                /* v7.39b: do not arm if a crash just fired - the normal branch already handled
                   this impact, and arming here is what produced the 200 ms double-kick. */
                /* v7.42: is the claimed entry speed one the SPEEDOMETER also saw?
                   See BYPASS_ENTRY_MAX / BYPASS_ENTRY_OVER for the measurement. */
                int entrySane = (gsShortMax <= BYPASS_ENTRY_MAX)
                             && (gsShortMax <= emaShortMax + BYPASS_ENTRY_OVER);
                /* v7.43: the bypass may arm while a SOFT nudge or a TAP is holding the
                   wheel, exactly as the normal crash branch already may (see the
                   `(!kickUntil || kickIsTap || kickIsSoft)` mask on the calm test above).
                   Without this the nudge's 130 ms kickUntil blocked the arm, and the arm
                   fell on the first free tick at 156 ms - measured as the 219 ms cluster
                   (156 + PERSIST_MS 48, quantised up by the 15.6 ms tick). It delayed 9 of
                   11 bypass crashes in the v7.41 drive: the same delayed-jolt pattern
                   the v7.41 fix killed on the normal branch, still alive here. A real crash
                   must own the wheel over a soft nudge; the nudge is 2200, the crash 12000+. */
                /* v7.47: the v7.43 preemption is REVERTED for the bypass. It was built on a
                   misreading: the "219 ms cluster" I attributed to real crashes being delayed
                   by the soft nudge was in fact the bypass firing a REDUNDANT sharp on events
                   the normal branch had already classified as BRAKING and answered with the
                   2200 nudge. Proof, his v7.46 drive at t=64.4 s: speed 66->62->56->49->44->
                   34->22->14->7->3->1 km/h, a clean straight-line brake to a stop with no
                   collision at all; the nudge fired at -119 ms, the bypass ARMED at -56 ms
                   INSIDE that nudge and fired 17325 sharp at +6 ms. dampMult was 1.000 (no
                   slide) so v7.46's scaling could not touch it.
                   In v7.41/v7.42 the arm required an unqualified !kickUntil and the confirm
                   DROPPED the pending fire (0xDE) if a kick was active - so this could not
                   happen. v7.41 is the build he loves. Going back to exactly that. */
                int softOrTap = 0; (void)kickIsTap; (void)kickIsSoft;
                /* v7.44 and v7.45 were both tried here and BOTH REFUTED BY MEASUREMENT - do
                   not propose either again:
                     v7.44 `!(braking || slipDeg>20)` - suppressed the bypass in EVERY slide.
                       The bypass is the only live ram-catcher in a slide (side detector OFF,
                       bump OFF, damage field 3% coverage - src/ram_signal_audit.py), so it
                       risked a missed police ram in a chase. That risk was ruled unacceptable.
                     v7.45 `!braking` alone - the "a scrub is SUSTAINED, a ram is a SUDDEN
                       step" idea. Calibrated on 23 KNOWN-REAL normal-branch crashes
                       (src/bypass_braking_check.py): 20 of them measure as SUSTAINED too,
                       187-1078 ms, including the hardest 25000 hits - because after any
                       impact the car keeps decelerating. A sustained-deceleration gate would
                       therefore swallow real crashes, not just scrub. REFUTED.
                   What is left is the one thing that gates NOTHING, so no ram or crash can
                   ever be lost: scale the bypass FORCE by the wheel's own weight - see the
                   SetMag below. */
                if (!pendTick && strongRaw && (!kickUntil || softOrTap) && !calm
                    && (now - g_lastCrashTick) >= CRASH_REFRACTORY_MS
                    && gsOcc >= GS_OCC_MIN     /* v7.41: spike entries never arm the bypass */
                    && drop < TELE_DROP && gsShortMax > CRASH_MIN_GS) {
                    if (!entrySane) {
                        /* logged, never armed: a=entry*100 b=emaShortMax*100 c=rawdrop*100.
                           0xE2 - free channel; 0xDC/0xDF are taken (damper state, gate
                           frac). See [[analysis-traps]]: a new code on an occupied
                           channel has cost this project a debugging session before. */
                        Log(K_INIT, 0xE2, (DWORD)(LONG)(gsShortMax * 100.0f),
                            (DWORD)(LONG)(emaShortMax * 100.0f),
                            (DWORD)(LONG)(gsShortDrop * 100.0f));
                    } else {
                        pendTick = now; pendDrop = gsShortDrop; pendEntry = gsShortMax;
                        pendEma  = spdEma;
                        Log(K_INIT, 0xDD, (DWORD)(LONG)(gsShortDrop * 100.0f),
                            (DWORD)(LONG)(gsNeedNow * 100.0f),
                            (DWORD)(LONG)(gsShortMax * 100.0f));
                    }
                }
                if (pendTick && (now - pendTick) >= PERSIST_MS) {
                    /* still down = a real impact; back where it was = a phantom spike */
                    int stillDown = (gsNow < pendEntry - pendDrop * 0.5f);
                    /* ---- v7.49: THE END-OF-STOP JAB GATE ----
                       Only ever consulted when a soft nudge answered this same event within
                       the last BYPASS_NUDGE_MS. On an open road g_lastSoftTick is seconds
                       old and this cannot run at all, so no ram and no crash can be lost to
                       it - the failure mode that killed v7.44. See the constants block for
                       the 48-drive measurement behind the threshold. */
                    int rampOnly = 0;
                    if (g_lastSoftTick && (now - g_lastSoftTick) <= BYPASS_NUDGE_MS) {
                        float ratio = StepRatio(stepRing, stepRn);
                        if (ratio < BYPASS_STEP_MIN) {
                            rampOnly = 1;
                            /* a=ratio*100 b=ms since the nudge c=pendDrop*100 */
                            Log(K_INIT, 0xE7, (DWORD)(LONG)(ratio * 100.0f),
                                (DWORD)(now - g_lastSoftTick),
                                (DWORD)(LONG)(pendDrop * 100.0f));
                        }
                    }
                    /* v7.47: reverted with the arm above - a pending bypass whose confirm
                       lands while ANY kick is active is DROPPED (0xDE), exactly as in v7.41.
                       That is what stopped the braking jab, and v7.41 is his reference. */
                    if (stillDown && !rampOnly && !kickUntil && g_inVehicle
                        && (now - g_lastCrashTick) >= CRASH_REFRACTORY_MS) {
                        /* v7.40: force from the LOCKED reference formula at this SPEED, excess
                           ZERO. Why zero and not the raw drop: the reference computes severity
                           from the SMOOTHED drop, which for a bypass hit is near nothing - that
                           is the whole reason the reference missed it. Feeding the RAW drop into
                           the 7600/m·s severity term instead pegs every hit at 25000 (verified
                           offline on the silent marks). Excess 0 collapses the formula to its
                           speed baseline MAG_MIN + spd*MAG_SPD_SCALE - exactly the reference's
                           feedback value at the speed the crash happened, which is what he asked
                           for, with no invented constant and no overshoot. */
                        /* v820: the same speed-following trim as the main kick */
                        float trueEntryB = TrueEntryKmh(GetTickCount());
                        LONG trimNowB = CrashTrimFor(trueEntryB, g_crashTrimPct,
                                                     g_crashRampLo, g_crashRampHi);
                        /* v825: the same CrashKickMag as the main kick - the ceiling scales with
                           the slider, and magRefB (the kick at 100%) is what the guard reads. */
                        LONG magRefB;
                        LONG mag = CrashKickMag(RefCrashForce(0.0f, pendEntry), trimNowB, &magRefB);
                        Log(K_INIT, 0x3B, (DWORD)(LONG)(trueEntryB * 100.0f), (DWORD)trimNowB,
                            (DWORD)mag);
                        /* ---- v7.46: the bypass kick follows the wheel's OWN weight ----
                           Feedback on the v7.43 drive described the scrub jab as too strong at
                           the end of a handbrake slide. Every attempt to DETECT and suppress
                           the scrub was refuted (see the arm block above), because a real
                           crash and a slide-scrub are not separable by any ground-speed
                           statistic in these logs.
                           So do not detect anything - do exactly what v7.31 already did for
                           the soft nudge, which he approved: in a deep slide the slip
                           lightening has already cut the spring and damper to ~15% of normal,
                           but this ConstantForce never saw that multiplier and fired
                           full-strength into an almost empty wheel. `dampMult` = min(bell, 1)
                           is the very factor the damper gets, so it NEVER amplifies.
                           Consequences, and this is why it is safe:
                             - a scrub jab in a deep slide: 15245 -> ~2287. Gentle, in
                               proportion to the light wheel. That is the fix.
                             - a ram or crash while NOT sliding: dampMult is 1.0, so the force
                               is BIT-IDENTICAL to the reference. Crashes and rams on the
                               street are untouched.
                             - a ram DURING a slide: scaled the same way, so it is still felt,
                               just in proportion to a wheel that is genuinely light there.
                               Nothing is gated off, so no ram is ever LOST - the failure mode
                               that killed v7.44.
                           Scoped to this one SetMag. The normal crash branch, the taps, damage,
                           bullets and the curb channel are all untouched. */
                        LONG bypMag = (LONG)(mag * dampMult);
                        if (bypMag < 1) bypMag = 1;
                        /* ---- v7.63a: THE GUARD LEAKED, and the drive that found it is the
                           one it was shipped for. On 2026-08-06 drive 7 the normal branch
                           vetoed a ceiling kick at t+203515 (ratio 1.02, a dropout 203 ms
                           earlier) and THIS branch put the same 25000 back at t+203734, 219 ms
                           later, with no 0xEC of its own. The whole drive therefore said
                           nothing about whether the guard helps: its only veto was undone.
                           Tested on `mag` BEFORE dampMult, which is what makes it the same
                           population as the normal branch - after the slide scaling a ceiling
                           kick can read as 2287 and would never be looked at.
                           v825: and on the magnitude AT 100% (magRefB), for the same reason one
                           level up - after the crash slider a ceiling kick can read as anything
                           from 0 to 100000, and the slider must not choose the population. */
                        int bypVeto = 0;
                        if (magRefB >= MAX_MAG && g_ceilAgree > 0.0f) {
                            float ratio = CeilRatio(&g_ceil);
                            int veto = CeilVeto(&g_ceil, g_ceilAgree);
                            Log(K_INIT, 0xEC,
                                (ratio < 0.0f) ? 0xFFFFFFFFu : (DWORD)(LONG)(ratio * 1000.0f),
                                (DWORD)(LONG)(g_ceil.now * 100.0f),
                                (DWORD)mag | (veto ? 0x80000000u : 0u));
                            bypVeto = veto;
                        }
                        if (!bypVeto) {
                        mag = bypMag;
                        DWORD ms = DUR_BASE_MS + (DWORD)(pendDrop * DUR_PER_DROP);
                        if (ms > DUR_CAP_MS) ms = DUR_CAP_MS;
                        SetMag(KickSigned(mag));   /* v7.56: reversal, not more of the same */
                        kickUntil = now + ms; kickIsTap = 0; kickIsSoft = 0;
                        g_lastCrashTick = now;
                        Log(K_FIRE, 0xC0, (DWORD)mag, (DWORD)(LONG)(pendDrop * 1000.0f),
                            (DWORD)(LONG)(gsNow * 1000.0f));
                        Log(K_INIT, 0xE8, 0xC0u, (DWORD)mag, (DWORD)(LONG)(pendDrop * 1000.0f));
                        g_trArm = 1;   /* v7.60 fire trace - bypass branch */
                        /* v7.42 DIAGNOSTIC, not a gate: how far the speedometer fell
                           across the 48 ms confirm. Measured on the v7.41 drive it is
                           -0.27..-1.04 for real hits and +0.24 for the 1032 s phantom -
                           a sign flip with only 0.5 m/s between them, and ordinary
                           driving swings +-0.5 over the same 48 ms. Too thin to gate on
                           from eight events, so it is LOGGED to build the sample and the
                           entry-sanity test above does the actual work.
                           a=pendEma*100 b=spdEma*100 c=(fall+100)*100, offset to stay
                           positive in the unsigned field. */
                        Log(K_INIT, 0xE3, (DWORD)(LONG)(pendEma * 100.0f),
                            (DWORD)(LONG)(spdEma * 100.0f),
                            (DWORD)(LONG)((100.0f + spdEma - pendEma) * 100.0f));
                        }
                    } else {
                        Log(K_INIT, 0xDE, (DWORD)(LONG)(gsNow * 100.0f),
                            (DWORD)(LONG)(pendEntry * 100.0f), (DWORD)(LONG)(pendDrop * 100.0f));
                    }
                    pendTick = 0;
                }
            }

            /* ---- v7.25 F10: SIDE IMPACT kick ----
               Placed AFTER the crash branch and gated on !kickUntil, so a hit that is both a
               crash and a shunt is reported once, as the crash - the bigger event keeps the
               wheel. See the constants block for why this exists: 4 of his 6 labelled
               sideswipes produced no feedback at all. */
            /* Its OWN position ring, 64 deep. The shared posRingX/Y is only 16 samples = 128 ms
               and is load-bearing for F1 slip and the crash veto, both confirmed-good; a
               160 ms window needs 41 samples, so widening the shared ring would mean touching
               every mask in code that must not be disturbed. */
            sideRX[sideN & 63] = posRingX[(posCount - 1) & 15];
            sideRY[sideN & 63] = posRingY[(posCount - 1) & 15];
            sideN++;

            if (sideN > SIDE_WIN * 2 + 1) {
                float dtW = SIDE_WIN * POLL_MS * 0.001f;
                int iN = (sideN - 1) & 63;
                int iM = (sideN - 1 - SIDE_WIN) & 63;
                int iO = (sideN - 1 - SIDE_WIN * 2) & 63;
                float vnx = (sideRX[iN] - sideRX[iM]) / dtW;   /* velocity now      */
                float vny = (sideRY[iN] - sideRY[iM]) / dtW;
                float vox = (sideRX[iM] - sideRX[iO]) / dtW;   /* one window earlier */
                float voy = (sideRY[iM] - sideRY[iO]) / dtW;
                float sp0 = Sqrtf(vox * vox + voy * voy);
                /* the 50 m guard is the same level-load teleport veto the crash branch uses */
                if (sp0 > SIDE_MIN_SPD && sp0 < 50.0f) {
                    float ux = vox / sp0, uy = voy / sp0;          /* old direction of travel */
                    float dvx = vnx - vox, dvy = vny - voy;
                    float lon = dvx * ux + dvy * uy;               /* along travel  = a crash */
                    float lat = dvx * (-uy) + dvy * ux;            /* across travel = a shunt */
                    float aLat = Absf(lat), aLon = Absf(lon);

                    /* EVERY event, not every fourth: this diagnostic is now the whole reason
                       the block exists, and a 1-in-4 sample of a threshold-setting
                       distribution is a threshold set on a quarter of the evidence. */
                    if (aLat > SIDE_LOG_FLOOR)
                        Log(K_INIT, 0xC4, (DWORD)(LONG)(lat * 1000.0f),
                            (DWORD)(LONG)(lon * 1000.0f), (DWORD)(LONG)(sp0 * 1000.0f));

                    if (SIDE_ENABLE_UNCALIBRATED_TESTONLY && !kickUntil && now >= sideRearm
                        && aLat > SIDE_TRIG_UNCALIBRATED_TESTONLY
                        && aLat > aLon * SIDE_RATIO) {
                        LONG mag = SIDE_MAG_BASE
                                 + (LONG)((aLat - SIDE_TRIG_UNCALIBRATED_TESTONLY)
                                          * SIDE_MAG_PER_MS);
                        if (mag > SIDE_MAG_CAP) mag = SIDE_MAG_CAP;
                        /* direction from the shunt itself, not the alternating flip */
                        SetMag(lat > 0.0f ? mag : -mag);
                        kickUntil = now + SIDE_MS; kickIsTap = 0;
                        sideRearm = now + SIDE_REARM_MS;
                        Log(K_FIRE, 0xC4, (DWORD)mag, (DWORD)(LONG)(lat * 1000.0f),
                            (DWORD)(LONG)(lon * 1000.0f));
                    }
                }
            }

            /* ---- ConstantForce kick management ---- */
            if (kickUntil && now >= kickUntil) { kickUntil = 0; kickIsSoft = 0; }
            /* v7.9: an F3 tap is SHAPED, not a flat block - rise to peak over the attack,
               then decay to zero. At speed the attack is 10ms so it still reads as one
               sharp tuk; at walking pace it is a 60ms swell inside a 220ms pulse, which is
                            the curb-like "wallowing" the user asked for. Other kicks (crash, damage,
               F5) keep their original flat behaviour - untouched. */
            if (kickUntil && kickIsTap && f3TapDur) {
                DWORD el = now - f3TapStart;
                float env;
                if (el < f3TapAtk)  env = (float)el / (float)f3TapAtk;
                else if (el < f3TapDur)
                    env = 1.0f - (float)(el - f3TapAtk) / (float)(f3TapDur - f3TapAtk);
                else env = 0.0f;
                /* smoothstep for the pedestrian pulse: 3e^2-2e^3 rounds the corners so the
                   force swells and fades instead of ramping linearly into a point. */
                if (f3TapSmooth) env = env * env * (3.0f - 2.0f * env);
                SetMag((LONG)(f3TapSign * env * (float)f3TapPeak));
            }
            /* Between kicks the ConstantForce carries the F2 road wallow; a crash/damage
               kick (kickUntil set) overrides it transiently.
               v7.25: the SPIN force rides here too. Summing it with the wallow rather than
               giving it its own kick is what makes it a steady lean instead of a jolt, and it
               inherits the crash preemption for free - while kickUntil holds, the wheel
               belongs to the impact, exactly as before. */
            if (!kickUntil) {
                /* v7.35: the range boost rides on the WALLOW TERM ONLY. Spin and SAT keep the
                   v7.31 values at every range - one variable per drive. */
                /* M1: slider #6 controls road surface, scaling the wallow term; slider #8 controls slide feel
                   scales spin+SAT (handover 2.3 rows 6 and 8).
                   The default path is the ORIGINAL EXPRESSION, character for character, and
                   that is not pedantry: splitting it into roadTerm + slideTerm re-associates
                   the sum from (a+b)+c to a+(b+c), and float addition is not associative.
                   Measured at -O2 -m32 over 4,000,000 random (wallow, gain, spin, sat) draws
                   in the real magnitude range: the float differs in 17.01% of cases and the
                   value actually handed to SetMag differs in 494 of them (0.0124%), by 1.
                   Imperceptible in the wheel, fatal to the acceptance test - "defaults inert
                   to the byte" is how M1 gets signed off, so it has to be literally true. */
                float total;
                if (g_pctRoad == 100 && g_pctSat == 100) {
                    total = wallowForce * WallowGain() + spinForce + satForce;
                } else {
                    float roadTerm = wallowForce * WallowGain();
                    if (g_pctRoad != 100) roadTerm *= (float)g_pctRoad * 0.01f;
                    float slideTerm = spinForce + satForce;
                    if (g_pctSat != 100) slideTerm *= (float)g_pctSat * 0.01f;
                    total = roadTerm + slideTerm;
                }
                /* v7.52: this is the ONE continuous SetMag in the file, and the small-range
                   kick floor below must never touch it. A floor on a channel that idles
                   near zero would turn silence into a permanent hum. */
                /* ---- THE GROUND CHANNEL, 2026-09-13 - his two tasks, in this order ----
                   1. our own forces fade with the tyre load, so the wheel goes empty when the
                      wheels leave the ground - the one thing the stock game does better;
                   2. SAT rides on top and carries the steering weight, the self-steer and the
                      counter-steer. Both default ON because this is a test build. */
                {
                    float gndLoad = 1.0f;
                    /* The detour's pointer when the game's own FFB tick runs, and the hunted one
                       when it does not - which, since run 016, is always. Neither is trusted
                       blind: GroundRead re-validates every field it uses, every tick. */
                    float gnd = GroundForce(g_vehTick ? g_vehTick : g_vehFound, &gndLoad);
                    if (g_gndOn && g_gndGate) total *= gndLoad;
                    total += gnd;
                    /* The key's answer, in the only channel he can hear while driving. ON is one
                       push out and back; OFF is two short taps - different shapes, so the wheel
                       says WHICH state he is in, not merely that a key arrived. Deliberately
                       OUTSIDE the load gate above: it must arrive when the channel was just
                       switched off, and with the front wheels in the air too. */
                    if (g_gndAck > 0) {
                        float m = (float)g_gndAckMag;
                        if (g_gndAckPat == 0) {
                            total += (g_gndAck > 10) ? m : -m;            /* ON: one nudge      */
                        } else if (g_gndAckPat == 2) {
                            /* v7.90: COUNT the pulses - preset N answers with N nudges out and
                               back, 160 ms each. The count IS the answer: it says which of the
                               five he is now in, not merely that a key arrived. */
                            LONG phase = (g_gndAck - 1) % 20;
                            total += (phase >= 10) ? m : -m;
                        } else if (g_gndAck > 19 || (g_gndAck <= 13 && g_gndAck > 7)) {
                            total += m;                                   /* OFF: tap, gap, tap */
                        }
                        g_gndAck--;
                    }
                }
                g_continuous = 1;
                /* v825: the ceiling scales with the largest slider in the sum (ContinuousCap);
                   at 100% everywhere it is MAX_MAG exactly, the v824 clamp to the bit. */
                {
                    float cap = ContinuousCap();
                    SetMag((LONG)Clampf(total, -cap, cap));
                }
                g_continuous = 0;
            }

            /* v7 bump state advance */
            if (haveHead) { headingPrev = headNow; haveHeadPrev = 1; }
            disturbPrev = disturb;

        } else {
            /* No car: silence ConstantForce, kill Spring, keep small Damper */
            hn = 0;
            hist[0] = hist[1] = hist[2] = hist[3] = 0;
            spdEma = 0; prevDmg = -1;
            haveHeadPrev = 0; disturbPrev = 0;
            if (kickUntil && now >= kickUntil) { kickUntil = 0; kickIsSoft = 0; }
            SetMag(0);
            SetSpring(0);
            /* keep heavy when parked in menus. gog-patcher: the standing percent, scaled here
               rather than in SetDamper; no truck factor, because out of a vehicle there is no
               vehicle class to read. */
            SetDamper(Mul(K_DAMP_DI_STATIC, g_pctDampStatic));
        }
    }
}

/* v7.4l: build "<base><id>.bin" without CRT (nostdlib). Per-launch unique names so a relaunch
   never overwrites the previous drive's log - keeps history N deep for later diagnosis. */
static void BuildName(char *out, const char *base, DWORD id)
{
    int i = 0; while (base[i]) { out[i] = base[i]; i++; }
    char tmp[12]; int n = 0; DWORD v = id;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) out[i++] = tmp[--n];
    out[i++] = '.'; out[i++] = 'b'; out[i++] = 'i'; out[i++] = 'n'; out[i] = 0;
}

/* ===================== gog-patcher fork: paths and build selection ====================
   The originals were hardcoded to <your Mafia install>. Shipped in a GOG install that would make
   the mod write its logs into another working copy of the game - the one install
   this project is forbidden to touch. Worse, it would look like it worked. Paths are now
   derived from the running exe, which is also what the final patcher needs: it will be
   installed into folders nobody here has seen. */
static char g_gameDir[MAX_PATH];      /* folder of the running Game.exe, with a trailing \ */

static void InitDir(void)
{
    DWORD n = GetModuleFileNameA(NULL, g_gameDir, MAX_PATH);
    while (n > 0 && g_gameDir[n - 1] != '\\' && g_gameDir[n - 1] != '/') n--;
    g_gameDir[n] = 0;                 /* keep the separator, drop the file name */
}

/* Build a path next to the game exe: "<gamedir><base>" */
static void GamePath(char *out, const char *base)
{
    int i = 0, j = 0;
    while (g_gameDir[i]) { out[i] = g_gameDir[i]; i++; }
    while (base[j]) { out[i + j] = base[j]; j++; }
    out[i + j] = 0;
}

/* gog-patcher M1: INI-driven percent multipliers for the FFB tuner utility. New file, zero
   lines taken from any other module - the merge discipline this project keeps. Needs GamePath
   and Log, both defined above this point. */
#include "ffb_settings.c"

/* gog-patcher M1b: the status file the utility reads to light its connected-and-effective
   lamp. After ffb_settings.c because it echoes that file's generation counter. */
#include "ffb_status.c"

/* Identify the build by the CODE at the probe site, not by a file hash - see the build
   table. Returns the index, or -1 if nothing matches.

   The comparison is over the md5 of the 40 bytes rather than the bytes themselves; the
   memory guards below are unchanged and still decide whether those 40 bytes may be read
   at all. Reading past a region boundary would be a crash in someone else's game, which
   is the one failure this function must never have. */
static int SelectBuild(void)
{
    for (int b = 0; b < N_BUILDS; b++) {
        DWORD start = g_builds[b].carProbe - PROBE_SIG_BACK;
        MEMORY_BASIC_INFORMATION mbi;
        char here[33];
        if (VirtualQuery((const void *)start, &mbi, sizeof(mbi)) != sizeof(mbi)) continue;
        if (mbi.State != MEM_COMMIT) continue;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) continue;
        if (start + PROBE_SIG_LEN > (DWORD)mbi.BaseAddress + (DWORD)mbi.RegionSize) continue;
        md5_bytes((const void *)start, PROBE_SIG_LEN, here);
        if (md5_hex_same(here, PROBE_SIG_MD5)) return b;
    }
    return -1;
}

/* The developers' own force feedback, compiled beside this file as a second translation unit
   (src\spike\origffb_probe.c, built with -DORIGFFB_AS_MODE so its DllMain is dropped). One
   symbol, because everything else in that file is static. See the mode 2 block in DllMain.
   Declared only in that build: the offline test hosts compile this file on its own. */
#ifdef ORIGFFB_AS_MODE
void OrigFfbStart(void);
#endif

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID rr)
{
    (void)h; (void)rr;
    if (reason == DLL_PROCESS_ATTACH) {
        /* v7.25: the log buffer's lock, before ANY Log() call can happen. */
        InitializeCriticalSection(&g_logcs);
        g_logcs_ready = 1;

        DWORD lid = GetTickCount();   /* per-launch id */
        char nm[MAX_PATH], stem[MAX_PATH];
        int  diagOn;
        InitDir();

        /* ---- THE DIAGNOSTIC LOGS ARE OFF FOR A PLAYER, 2026-08-08 -------------------------
         * They used to open unconditionally, and nothing in the shipped documentation said so.
         * github-project asked the question that found it: "do any diagnostic log streams ship
         * enabled?" They did - TWO, a fresh pair per launch of the game, never cleaned up.
         * Measured on this machine: 5-12 MB of ffb_diag_ and 0.5-1.3 MB of car_mtx_ per drive,
         * and the working bench had accumulated 33 pairs, 68 MB, in a game folder.
         *
         * That is right for us and wrong for somebody who installed a mod. `diag = 1` in
         * mafia_ffb.ini turns them back on; both benches here carry that line. Default 0 for
         * everybody else, the same rule car_snap already follows below.
         *
         * The handles stay INVALID_HANDLE_VALUE when it is off, and Log() returns on exactly
         * that - so nothing else in this file changes and no call site needs a guard. */
        {
            char diagIni[MAX_PATH];
            FFBIniPath(diagIni);
            diagOn = (int)GetPrivateProfileIntA("ffb", "diag", 0, diagIni);
        }
        if (diagOn) {
            GamePath(stem, "ffb_diag_");
            BuildName(nm, stem, lid);
            g_log = CreateFileA(nm, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            GamePath(stem, "car_mtx_");
            BuildName(nm, stem, lid);
            g_mtx = CreateFileA(nm, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }

        /* v7.4n SINGLETON (robust ownership test): our ASI is loaded TWICE per process -> two
           FFB instances fought for the EXCLUSIVE device (the "Acquire hang" + heavy/layered
           wheel). Only the instance that ACQUIRES this mutex runs FFB/patches; the other logs
           0x59 and bails (no thread, no device contention). Uses WaitForSingleObject, NOT the
           v7.4m GetLastError()==ERROR_ALREADY_EXISTS check (that failed to bail the 2nd copy). */
        {
            HANDLE mtx = CreateMutexA(NULL, FALSE, "MafiaFFB_Singleton_v1");
            DWORD own = mtx ? WaitForSingleObject(mtx, 0) : WAIT_OBJECT_0;
            if (own != WAIT_OBJECT_0) { Log(K_INIT, 0x59, own, (DWORD)mtx, lid); return TRUE; }
            Log(K_INIT, 0x58, own, (DWORD)mtx, lid);   /* PRIMARY - we own the wheel */
        }

        /* car_snap: single fixed file, opened PRIMARY-only (avoid a 2-instance truncation race).
           v7.66: OFF BY DEFAULT. The dump is 1.2-1.3 GB per drive and the log guard archives a
           second copy, so an ordinary drive cost about 2.4 GB of disk for a file nothing read.
           It is a FIELD-FINDING tool - it answers where in the car struct a given field lives, without
           a drive, and it earned the third position component and the damage-channel proof that
           way - but since the whole engine was decompiled that question is answered better by
           reading the function that WRITES the field. Turn it back on for a drive whose purpose
           is finding a field, and say so on the run card.

           Read here rather than in ReadFFBSettings() because that runs ~50 lines later, after
           this handle would already exist - the same reason the preferred device is read early.
           The writer at CarSnapTick() already returns on INVALID_HANDLE_VALUE, so leaving the
           handle closed disables the dump with no other change.

           0x5A says out loud which way it went, because a diagnostic that is silently off is
           indistinguishable from one that ran and found nothing:
             a = the cfg value, b = 1 if the file is actually open. */
        {
            char snapIni[MAX_PATH];
            FFBIniPath(snapIni);
            g_carSnap = (LONG)GetPrivateProfileIntA("ffb", "car_snap", 0, snapIni);
        }
        if (g_carSnap) {
            GamePath(nm, "car_snap.bin");
            g_snap = CreateFileA(nm, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        Log(K_INIT, 0x5A, (DWORD)g_carSnap,
            (DWORD)(g_snap != INVALID_HANDLE_VALUE ? 1u : 0u), 0);

        /* ---- gog-patcher fork: identify the build BEFORE touching any address ----
           K_INIT 0x60: a = selected build index (0 custom, 1 gog, 0xFFFFFFFF = none),
                        b = VA_GAMEPTR chosen, c = VA_CAR_PROBE chosen.
           On no match the mod stops here: no patch, no threads, no DirectInput. A wrong
           guess would splice a call into the middle of an unidentified instruction. */
        g_buildIdx = SelectBuild();
        if (g_buildIdx < 0) {
            Log(K_INIT, 0x60, 0xFFFFFFFFu, 0, 0);
            Log(K_INIT, 0x61, (DWORD)GetModuleHandleA(NULL), GetCurrentProcessId(), 0);
            return TRUE;              /* unrecognised build - stay inert */
        }
        VA_GAMEPTR   = g_builds[g_buildIdx].gamePtr;
        VA_CAR_PROBE = g_builds[g_buildIdx].carProbe;
        MATRIX_OFF   = g_builds[g_buildIdx].mtxOff;
        GEAR_OFF     = g_builds[g_buildIdx].gearOff;
        Log(K_INIT, 0x60, (DWORD)g_buildIdx, VA_GAMEPTR, VA_CAR_PROBE);
        Log(K_INIT, 0x66, MATRIX_OFF, 0, 0);   /* which frame matrix offset is in use */
        /* v7.65: the QPC frequency, written ONCE, so the tick log's raw counter deltas can be
           turned into microseconds offline. Logged rather than assumed: it is 10 MHz on this
           machine and need not be on another, and a decoder that hardcodes it would silently
           misreport every dt on a machine where it differs. a = low half, b = high half,
           c = 1 if the call succeeded. */
        {
            LARGE_INTEGER qf;
            int ok = QueryPerformanceFrequency(&qf) ? 1 : 0;
            /* cached for the 0xD9 impulse log, which needs seconds rather than raw counts */
            if (ok && qf.QuadPart > 0) g_qpcHz = (double)qf.QuadPart;
            Log(K_INIT, 0xF4, ok ? (DWORD)(qf.QuadPart & 0xFFFFFFFF) : 0,
                ok ? (DWORD)((qf.QuadPart >> 32) & 0xFFFFFFFF) : 0, (DWORD)ok);
        }
        Log(K_INIT, 0x67, GEAR_OFF, 0, 0);     /* which gear offset the 0x76 channel probes */
        /* DIAG: which process are we in, and is 0x5E2061 the expected game code?
           K_INIT 0x50: a=module base (expect 0x400000), b=pid, c=DWORD at hook site
                        (expect 0x8b508b00 = the collision-probe bytes; garbage = wrong
                        process/version, BADBAD00 = unreadable). */
        {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            DWORD pid  = GetCurrentProcessId();
            MEMORY_BASIC_INFORMATION dmbi;
            DWORD siteBytes = 0xBADBAD00u;
            if (VirtualQuery((const void *)VA_CAR_PROBE, &dmbi, sizeof(dmbi)) == sizeof(dmbi)
                && dmbi.State == MEM_COMMIT && !(dmbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                siteBytes = *(volatile DWORD *)VA_CAR_PROBE;
            Log(K_INIT, 0x50, base, pid, siteBytes);
        }
        /* ---- MODE 2: THE DEVELOPERS' OWN FORCE FEEDBACK, his ask of 2026-09-14 --------------
         * the option to also enable the basic feedback for the owners of new
         * Direct Drive wheels to try. The game's own feedback exists, the project proved it and switched
         * it on ([[game-has-its-own-ffb]]); that code is origffb_probe.c, linked into this .asi
         * as a second translation unit rather than shipped as a second module, because both take
         * the wheel DISCL_EXCLUSIVE and two exclusive owners is a fight.
         *
         * WE STOP HERE when it is chosen. Not "stop sending forces" - stop entirely: no hook
         * into the car probe, no FFB thread, no device of ours. Anything less would leave this
         * module holding the wheel the other one is trying to take, which is the one failure
         * mode that looks like a broken wheel rather than a mod that is off.
         *
         * After the singleton claim on purpose, so a second copy of the .asi cannot start it
         * twice; before every patch, so a mode 2 install touches no game code at all.
         *
         * 0x32: a = the mode, b = 1 if the other side was started. */
        {
            char modeIni[MAX_PATH];
            FFBIniPath(modeIni);
            g_ffbMode = (LONG)GetPrivateProfileIntA("ffb", "ffb_mode", (UINT)-1, modeIni);
        }
        if (g_ffbMode == 2) {
            /* b says whether the other side is actually in this binary. It is a separate
               translation unit, so a build made without it (the offline test hosts, which compile
               this file alone) must say "asked for, not available" rather than quietly running as
               legacy - the difference between those two is the whole of a bug report. */
            /* 0x39, not 0x32: 0x32 is the two-axes row, written every second since v7.95. */
#ifdef ORIGFFB_AS_MODE
            Log(K_INIT, 0x39, (DWORD)g_ffbMode, 1, 0);
            LogFlush();
            OrigFfbStart();
            return TRUE;
#else
            Log(K_INIT, 0x39, (DWORD)g_ffbMode, 0, 0);
#endif
        }
        PatchWithCall(VA_CAR_PROBE, (DWORD)HookCar, 8);
        /* K_INIT 0x52: a=first byte at hook site after patch (expect 0xE8 = call). */
        Log(K_INIT, 0x52, *(volatile BYTE *)VA_CAR_PROBE, VA_CAR_PROBE, 0);
        ReadFFBSettings();   /* M1: load mafia_ffb.ini before the FFB thread can fire a kick */
        DWORD tid;
        CreateThread(NULL, 0, FFBThread, NULL, 0, &tid);
        CreateThread(NULL, 0, SnapThread, NULL, 0, &tid);
    } else if (reason == DLL_PROCESS_DETACH) {
        /* v7.4n: FREE THE WHEEL on exit (only the primary has g_dev). Do it on ANY detach,
           including process termination - the game quitting IS termination, which is exactly
           when the wheel was staying heavy. A fault here is harmless (the process is dying). */
        if (g_dev) {
            if (g_eff)    ((Stop_t)VT(g_eff,    EFF_STOP))(g_eff);
            if (g_spring) ((Stop_t)VT(g_spring, EFF_STOP))(g_spring);
            if (g_damper) ((Stop_t)VT(g_damper, EFF_STOP))(g_damper);
            if (g_friction && g_fricStarted) ((Stop_t)VT(g_friction, EFF_STOP))(g_friction);
            ((Acquire_t)VT(g_dev, DEV_UNACQUIRE))(g_dev);
        }
        if (g_mtx  != INVALID_HANDLE_VALUE) { FlushFileBuffers(g_mtx);  CloseHandle(g_mtx);  }
        if (g_snap != INVALID_HANDLE_VALUE) { FlushFileBuffers(g_snap); CloseHandle(g_snap); }
        LogFlush();   /* v7.25: buffered logging - write the tail before closing */
        if (g_log  != INVALID_HANDLE_VALUE) { FlushFileBuffers(g_log);  CloseHandle(g_log);  }
    }
    return TRUE;
}
