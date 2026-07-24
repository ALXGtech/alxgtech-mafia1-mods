# How it works

This document explains the parts of the mod that are not obvious from the README: the
closed loop, the four bugs that shaped it, and how transmission-mode intent is decided.
All of it lives in `src/gearbox_logic.h` (the decision core, shared with the test suite)
and `src/gearbox_hook8.c` (the DirectInput hook that drives it).

## The problem in one sentence

An H-pattern shifter reports an absolute position - "the lever is in third" - with no
history. Mafia's gearbox is sequential: it only knows "shift up" and "shift down".
Closing that gap is the whole mod.

## Delivering a shift: key injection, not a virtual controller

The game reads its keyboard through DirectInput 8. The mod creates a keyboard device of
its own purely to reach the device class vtable, patches `GetDeviceState`, and sets the
bit of the configured DIK scancode in the 256-byte buffer the game is about to read.
Nothing is emulated and no virtual joystick exists: the game asks Windows for its own
keyboard state and receives that state with one extra bit set. A 256-byte buffer is what
identifies the keyboard among the DirectInput devices the game opens; the mod's own
devices are excluded so its internal bookkeeping never loops back on itself.

Setting a bit and clearing one are the same code path, which is also how button
suppression works: a lever button bound in `gearbox.ini` is zeroed out of the game's own
device state so it does not also fire whatever the game's control-binding screen has it
mapped to.

## The closed loop: why it cannot drift

Every cycle reads the game's own current gear at `[car+0x58]+0x5D0`, compares it to the
lever's target, and emits **one** sequential step toward that target - never more. It
then waits for the read gear to actually move before considering the step done.

Nothing in the mod counts its own shifts. The target comes from the lever, the current
value comes from the game, and there is no third number that can fall out of sync with
either. That closed loop is what makes a car with two gears safe: asking a two-gear car
for 4th shifts up, reads 2, shifts up again, reads 2 again unmoved, and stops - the mod
never needed to know the car only has two gears, and it can never desync a counter it
does not keep.

The mod also reads the manual/automatic flag at `[car+0x58]+0x53C` every cycle, and
never writes it (see "Transmission mode intent" below).

## The four bugs, and what each one taught

Four drives on real hardware found four ways a momentary condition can get recorded as a
permanent fact. All four are fixed in the shipped logic; they are listed here because
each is a specific trap and a specific reason a naive closed loop is not enough.

1. **A single refusal is not a permanent limit.** The very first version latched "this
   gear does not exist" after one rejected shift - which happens whenever the box is in
   automatic, or momentarily too fast for a downshift, and has nothing to do with the
   car's real gear count. A refusal now backs that direction off for `retry_ms`, and any
   gear movement or a new lever position clears it immediately. The gear-count clamp
   never needed a latch to work: the loop already reads the game's real gear, so it
   cannot drift regardless.
2. **A momentary button is not a lever.** Testing with wheelbase buttons standing in for
   an H-pattern shifter, a multi-step move (say neutral to 4th) only ever completed one
   step, because the button read as pressed for about 150 ms and then let go. A real
   lever holds its position, so the target it sets is now sticky until the lever
   physically moves again, not just while a button happens to be held.
3. **Refusals are always temporary.** Once retries worked, the box refusing a downshift
   because the car was going too fast (ordinary behaviour, not a limit) briefly cost
   first and then second gear before landing the car stuck in third with no way back to
   neutral or reverse. The fix is the same back-off as bug 1, generalised: nothing the
   gearbox refuses is ever assumed permanent.
4. **The lever passes through nothing on its way to something.** With neutral configured
   as the shifter's rest position, every gate-to-gate move passes through "no gate" for
   an instant, and the mod shifted to neutral and back on every single gear change.
   Measured from a real drive: gate to gate takes 0.22-0.94 s, while a deliberate neutral
   dwell runs 1.1 s or longer. So a gate is acted on the instant it is reached, and only
   the rest position waits out `neutral_delay_ms` (1100 ms default) before the mod
   believes it. Leaving reverse is exempt from that wait, because that move genuinely
   passes through neutral and neutral is what the driver wants there.

The common shape of bugs 1, 3 and 4 is the same lesson from three different angles: a
momentary condition (one refusal, a busy box, a lever mid-travel) must never be recorded
as a permanent fact about the car or the lever.

## Transmission mode intent

Alex's rule, and the reason mode handling reads a flag instead of a button:

> If the player switches to automatic, the mod's own auto-return-to-manual must not
> fight them. If they shift a gear while in automatic, that switches back to manual with
> the correct gear already selected. Switching to automatic must always work cleanly.

An early version only trusted the wheelbase's own mode control as a deliberate choice,
so a keyboard press of the game's own automatic key read as an accident, and the mod
forced manual back and pinned the driver in first gear. The fix reads intent from the
mode flag itself, not from which control moved it: any switch into automatic that the
mod itself did not cause is the driver's decision, whatever pressed it, and the mod goes
hands-off until the lever moves again. Moving the lever is the one action that always
means "give me this gear", so it is the only thing that returns control to manual.

If the wheelbase's mode control is a latching switch rather than a momentary button, the
switch's own transitions are what the mod acts on (`mode_hold` in `gearbox.ini`); holding
a position is never re-asserted on every cycle, only the edges are.

## Configuration reference (`gearbox.ini`)

Set by `gearbox-setup.exe`, read once at load and hot-reloaded after. See
[`gearbox.ini.example`](gearbox.ini.example) for a complete annotated file.

| Key | Effect |
|---|---|
| `closed_loop` | 1 reads the gear back and clamps to the real gearbox; 0 injects keys open loop while the log records whether the address chain resolved. Verify the log shows the chain resolving before setting 1 on an unverified build. |
| `hold_ms` | How long an injected key stays pressed. Must survive at least one game input poll. |
| `gap_ms` | Gap between consecutive injected presses in a multi-step move. |
| `retry_ms` | How long a refused direction backs off before being tried again. |
| `neutral_delay_ms` | Grace window before the rest position is believed to mean neutral, not a gate in transit. |
| `mode_hold` | 1 if the mode control is a latching switch (act on transitions only); 0 if it is a momentary button. |

## Building

```
i686-w64-mingw32-clang -O2 -m32 -mwindows -o gearbox-setup.exe gearbox_gui.c -lkernel32 -luser32 -lcomdlg32
```

32-bit only: the mod loads into a 32-bit process.
