# Porting this to another game

This document is method, not disassembly. Everything below generalizes past Mafia: the
DirectInput hook shape, how to find your own game's equivalent of "current gear," and
how to build the decision core before you ever touch the real game. It does not
reproduce anything from Mafia's disassembly - only our own code, publicly documented
DirectInput behaviour, and the structure of the search itself. If your target game's
license or EULA restricts reverse engineering, that restriction is yours to check first;
nothing here overrides it.

## What actually generalizes

Three things transfer to almost any sequential-state mechanic in almost any DirectInput
game - not just gearboxes:

1. **The injection technique.** Patching a DirectInput device's own vtable slot
   (`GetDeviceState` / `GetDeviceData`) is a Microsoft-documented COM interface layout,
   not something specific to Mafia. It works on any DirectInput 2 through 8 game, because
   the vtable shape is the same across that whole family.
2. **The closed-loop design.** Read the game's own state after every emitted input, step
   toward the target one unit at a time, and treat "it did not move" as a temporary
   refusal, not a permanent limit. This applies to any mechanic the game exposes as a
   readable value plus a "next/previous" input pair: gears, camera zoom stops, weapon
   selection wheels, radio presets.
3. **The bug shape.** Every bug this project hit was the same shape from a different
   angle: a momentary condition recorded as a permanent fact (one refusal treated as "this
   gear does not exist," a button's release treated as "the lever let go," a lever's
   transit treated as "the driver wants neutral"). Assume you will hit this exact shape in
   a new game and design the retry/debounce logic for it up front.

## How the injection works, generically

The game reads an input device through a small number of virtual-function calls:
`GetDeviceState` (polled) or `GetDeviceData` (buffered), reached through the COM vtable
DirectInput hands back from `CreateDevice`. Any process - including a DLL loaded into the
game - can create a device of its own on the same interface, which gives it the SAME
vtable pointer the game's own device uses, because DirectInput shares one vtable per
device class rather than allocating one per instance. Patch that one shared vtable slot
and every device of that class is affected, including ones you never created.

Practical steps, engine-agnostic:

1. Load a small DLL into the process (an ASI loader is one common way; any DLL injection
   method works).
2. Create a device of the class you care about (keyboard, joystick) purely to obtain its
   vtable pointer. Never poll this device - it exists only to reach the vtable.
3. Overwrite the vtable slot for `GetDeviceState`/`GetDeviceData` with your own function,
   after making the page writable (`VirtualProtect`) and saving the original pointer.
4. Your replacement calls the original first, then edits the buffer it returned before
   handing it back: OR in a bit to inject a press, AND out a bit to suppress one.
5. Identify which buffer is the keyboard versus a joystick versus a mouse by its `cb`
   (buffer size) parameter - this is a DirectInput SDK constant (256 for the keyboard
   state array, 272 for `DIJOYSTATE2`, 16 or 20 for a mouse), not something you need to
   discover per-game.

## What to look for in another game - finding your "current gear"

You will not have a disassembly-derived offset table for a new game on day one. The
method that found Mafia's is a measurement technique, and it transfers:

1. **Correlate, don't guess.** Capture a memory region around a plausible object (the
   player/vehicle/weapon pointer chain) at a fixed interval while recording a known
   external signal - speed, ammo count, whatever your target state should track. Rank
   every candidate field by how well its value explains variance in that signal (an
   eta-squared or similar statistic works; the exact statistic matters less than testing
   many candidates automatically instead of eyeballing a hex dump).
2. **A weak result is not a negative result.** A field can genuinely be the value you
   want and still score low if the correlation you chose is a blunt instrument for it -
   this project's own first pass ranked gear low against raw speed, because speed varies
   continuously within one gear. Always know what would make your test miss the right
   field before trusting it to rule one out.
3. **Prefer a controlled measurement over a passive capture.** The single decisive drive
   for this project was not the statistical scan - it was a purpose-built diagnostic that
   logged the candidate field alongside the actual keys pressed, second by second, on a
   short deliberate drive that exercised every state transition once. Two independent
   transitions at matching values is a strong result; a thousand-sample passive log is a
   weak one, because you cannot see what caused each value.
4. **Find the input path before the state.** In parallel, confirm whether the game
   accepts the transition as a key (something you can inject with the technique above) or
   as a distinct internal action id fired through an input-manager singleton. The former
   is almost always faster to get working; the latter is a nicer user experience (nothing
   to bind) but usually needs a real disassembly session to find the call site, which
   this document deliberately stays out of.
5. **Look for a "mode" flag the same way.** Any mechanic with an automatic/manual split
   (a gearbox, an autopilot, aim assist) usually has a single-byte flag nearby that
   changes exactly when the mode does and nothing else. The same controlled-drive method
   that finds the main state value finds this: change the mode deliberately several times
   and see which byte flips exactly then.

## Adapting this codebase to a different game

`gearbox_logic.h` is deliberately engine-agnostic: no Windows headers, no game memory
reads, no I/O. It only knows about button bitmasks, a target/current pair, and time. That
file - and the whole decision core it contains - can very likely be reused unchanged for
a different game's version of the same mechanic; only the caller changes.

What you would actually rewrite:

- The memory-read functions (`ReadGear`-equivalent) - game- and build-specific by
  construction, and the whole reason `RdOk` bounds-checks every pointer before
  dereferencing it: an unverified build must fail closed, never read garbage as if it
  were real.
- The DirectInput class and slot you patch, if the new game's input differs (some games
  read the keyboard buffered via `GetDeviceData` instead of polled via `GetDeviceState` -
  the code already has both hook points for exactly this reason).
- The ini schema and the GUI binder, which are UI, not logic.

What you keep: the decision core, the retry/back-off shape, the sticky-target handling,
and above all the test-first discipline below.

## Getting started

1. Build a mock of the target mechanic first, the way `tests/test_gearbox.c` mocks
   Mafia's gearbox: a tiny struct with the state value and the rules that make it change,
   driven by simulated time. Get the decision core passing scenarios against the mock
   BEFORE writing a single line that touches the real game.
2. Get the injection technique working in isolation - confirm you can suppress or inject
   a bit in the target device's buffer with no game logic attached yet. This project's
   `asi-loadtest.exe`-equivalent (load the DLL outside the game for a few seconds) exists
   so a broken build costs seconds, not a whole test session.
3. Only then look for the state value, using the controlled-drive method above.
4. Wire the mock-tested decision core to the real reads and the real injection. Expect
   the four-bugs shape from "What actually generalizes" to show up in some form; the test
   suite is what lets you fix them without needing a human and a controller for every
   iteration.

## Prompts, if you use an AI assistant for this

Useful ways to phrase the work, and one hard rule for phrasing it safely:

- "I'm hooking a DirectInput8 device's vtable to intercept `GetDeviceState` in a game I
  own a legal copy of, for a personal accessibility/input-mapping mod. Here is my current
  hook code (paste your OWN C, not the game's) - review it for vtable-patching
  correctness and thread safety."
- "I have N candidate memory offsets and a recorded correlation score against a known
  signal for each (describe the fields and scores, not the game's code). Help me design a
  sharper discriminator than the one I used."
- "Here is my decision-logic state machine for a closed-loop input translator (paste your
  OWN state machine, e.g. `gearbox_logic.h`-shaped code). Review it for the
  momentary-condition-recorded-as-permanent-fact bug shape."
- "I want to test this decision core without the real game. Help me design a minimal
  mock of the mechanic it drives, given these rules: <describe the rules in your own
  words>."

**The one rule that matters:** never paste the target game's actual disassembly, decompiled
source, or byte-for-byte memory dumps into a third-party AI service or anywhere else
outside your own private research notes. Describe structure, offsets and behaviour in
your own words instead, exactly as this repository's own source does. That is the same
line `PUBLISHING-GUIDE.md`'s licensing section draws for what goes in this repo, and it
is a good line to hold everywhere else too.
