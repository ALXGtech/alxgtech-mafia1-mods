# How it works

The whole mod is one AutoHotkey v2 file, `src/HShifter_to_vJoy.ahk`. This document
explains the three things in it that are not obvious, because they are the ones that
cost the most work and the ones you will break first if you change the timings.

## The problem in one sentence

An H-pattern shifter reports an absolute position; the game accepts only relative
commands. Translating one into the other is easy while the lever is in a detent, and
hard for every millisecond it is between two of them.

## The polling loop

A 10 ms timer reads the shifter. `DetectGear()` returns the first pressed button as a
gear number: 1 to 6 for the forward gears, `-1` for the dedicated reverse button, `99`
for the hard-reset button, and `0` when no button is pressed at all.

`CheckShifter()` acts only on a change, so holding a gear costs nothing. When the
target differs from the last one it either drives to a gear (`ShiftTo`), brute-forces
to reverse (`GoToReverse`), or resets through reverse to neutral (`ResetViaReverse`).

`ShiftTo` is the counting path: it takes the difference between the target and its own
`CurrentGear` and pulses gear-up or gear-down that many times, 30 ms per pulse with a
40 ms gap. This is the only path that trusts the internal counter.

## 1. Neutral is a timed decision, with two different windows

A reading of "no gear" means one of two things, and they are indistinguishable at the
moment you see them:

- the lever is parked in neutral, and the game should be put in neutral;
- the lever is between two detents on its way somewhere, and the game should be left
  alone.

You cannot tell them apart, so do not try. What matters is not guessing right but
making a wrong guess harmless. The mod waits for the gap to persist before engaging
neutral, and the length of the wait depends on where the lever came from
(`LastRealGear`):

| Came from | Wait | Why |
|---|---|---|
| A forward gear | 600 ms | The gap while crossing from 1st to 2nd is short. Engaging neutral there would be wrong. |
| Reverse | 1500 ms | Reverse to 1st is a long traverse across the whole gate. A short window would fire in the middle of it. |

If you widen only the forward window you will make normal shifting feel dead. If you
shorten the reverse window, moving out of reverse trips neutral halfway.

## 2. The shift sequences are atomic, and that is the actual bug fix

The original symptom was that pushing the lever into reverse from a gear frequently
left the game in neutral instead. It looked like a timing problem. It was not: it was
reentrancy.

The sequence was this. A slow push toward reverse produced a long no-gear gap, which
started the auto-neutral reset. That reset spams gear-down toward reverse and then
sends one gear-up to land on neutral. While it was still running, the 10 ms poller saw
the reverse button, fired `GoToReverse`, and drove the game into reverse. Then the
interrupted reset resumed, sent its trailing gear-up, and bumped reverse into neutral.

The fix is `Critical` on `ShiftTo`, `GoToReverse` and `ResetViaReverse`: while a press
sequence runs, the poller cannot interrupt it. Widening the grace window alone does
not fix this, because the race is not about how long you wait, it is about two
sequences interleaving.

If reverse handling ever misbehaves again, check that `Critical` is still in effect
before touching any delay.

## 3. Reverse-aware automatic neutral

The auto-neutral path calls `ResetViaReverse(rAware := true)`. The reset always drives
down to reverse first, because reverse is the one position it can reach from anywhere
without trusting the counter. At that moment it checks the lever again: if the lever
is now on reverse, the game is already exactly where the driver wants it, so the reset
skips its trailing gear-up and stays in reverse. It also sets `LastTarget` to `-1` so
that the poller does not immediately re-fire `GoToReverse`.

The hard-reset button and `F10` call the same routine with `rAware := false`, so they
always land on neutral regardless of where the lever is. That is what makes them
usable as a resynchronisation command.

## Why brute force to reverse instead of counting

`CurrentGear` is an open-loop estimate. The mod cannot read the game's gearbox, so any
refused or dropped shift makes the estimate wrong, and every counted shift after that
is wrong too. Reverse is reachable without the estimate: send more gear-downs than the
gearbox has gears (`MaxShiftDownToReverse = 8`, one more than the seven downs needed
from 6th) and the game is in reverse no matter where it started. Every recovery path
in the mod goes through that known state, which is why they always work and the
counting path only usually does.

## Tuning constants

| Constant | Default | Effect |
|---|---|---|
| `ShiftDelay` | 40 ms | Gap between consecutive pulses in a sequence. Too short and the game drops shifts. |
| `ButtonPulse` | 30 ms | How long the virtual button stays pressed. Must survive at least one game input poll. |
| `NeutralDelay` | 600 ms | Grace window after a forward gear. |
| `ReverseNeutralDelay` | 1500 ms | Grace window after reverse. |
| `MaxShiftDownToReverse` | 8 | Down-pulses used to force reverse. Must exceed the gear count. |

Change one at a time and drive a full lap. A gearbox mod that is wrong once in fifty
shifts feels broken, and once in fifty is not visible in a two-minute test.
