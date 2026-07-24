English | [Русский](README.ru.md) | [Čeština](README.cs.md)

# H-Shifter for Mafia: The City of Lost Heaven

Use a real H-pattern shifter in a game that only understands sequential gear up and
gear down.

<!-- ![H-shifter to sequential mapping](docs/img/banner.png) -->

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Before you install

- **Back up your saves.** This mod does not modify any game file. Even so, back up
  your save files (`<game folder>\savegame\`) before changing your control bindings.
- **Game version.** Tested on Mafia v1.3 (GOG release, English). Other versions and
  store releases are untested and are used at your own risk. Since no game file is
  modified, the likely failure mode is that the game does not see the virtual
  controller, not that anything breaks.
- **Other mods.** Compatibility with other mods has not been tested. Combining mods
  is at your own risk.

## What it is

Mafia's gearbox has exactly two actions: shift up and shift down. A physical
H-pattern shifter does not work that way - it reports an absolute position, "the
lever is in 3rd", and it says nothing about where it came from. The two models do
not meet, which is why an H-shifter normally cannot be bound to this game at all.

This script sits between them. It polls the shifter every 10 ms, and when the lever
lands in a detent it emits exactly the number of sequential shifts needed to move the
game's gearbox from where it currently is to where the lever now points. Moving from
2nd to 5th sends three shift-ups; moving from 4th to 2nd sends two shift-downs.

Reverse and neutral need more than counting. Between two detents the lever reports no
gear at all, and "parked in neutral" is indistinguishable from "slowly moving toward
reverse" - the same reading, two different intents. The script therefore treats
neutral as a timed decision with two different grace windows, and reaches reverse and
neutral by brute force through a known state rather than by trusting its own count.
Details are in [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).

The output goes through vJoy rather than the keyboard for a specific reason. Mafia
polls DirectInput devices directly and ignores synthetic keyboard events, so
`Send`-style key injection does nothing at all. vJoy creates a real virtual
DirectInput joystick, which Windows and the game treat exactly like a physical
controller.

No game file is touched. Nothing is injected into the game process. The script is an
input translator that runs beside the game.

## Requirements

| | |
|---|---|
| Game | Mafia: The City of Lost Heaven, v1.3 (GOG release, English) - the tested build |
| Hardware | An H-pattern shifter that Windows sees as a joystick with one button per gear |
| Software | [AutoHotkey v2](https://www.autohotkey.com/), [vJoy driver](https://sourceforge.net/projects/vjoystick/) |

Most H-shifters (Logitech, Thrustmaster and clones) already report each gear position
as a separate button, which is what this script expects.

## Installation

1. Install **AutoHotkey v2**. The default path the launcher expects is
   `C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe`.
2. Install the **vJoy driver**, open *Configure vJoy*, and make sure device **1**
   exists and has at least **2 buttons**.
3. Download this repository (green *Code* button, or a
   [release](../../releases) if one is published).
4. Open `src\HShifter_to_vJoy.ahk` in a text editor and check the config block at the
   top:
   - `PhysicalJoystick` - the joystick number of your shifter. Find it with
     AutoHotkey's *Window Spy* or by trial. Default is `3`.
   - `vJoyDeviceID`, `vJoyUpButton`, `vJoyDownButton` - leave at `1`, `1`, `2` unless
     you configured vJoy differently.
   - `vJoyDLL` - path to `vJoyInterface.dll`, default
     `C:\Program Files\vJoy\x64\vJoyInterface.dll`.
5. In Mafia's control settings, bind:
   - **Gear Up** to vJoy device 1, **button 1**
   - **Gear Down** to vJoy device 1, **button 2**
6. Run `src\HShifter_to_vJoy.ahk`, then start the game. Or use
   `tools\Launch Mafia with Shifter.ps1`, which starts the script, launches the game,
   and stops the script when the game exits - edit the paths at the top of that file
   first.

To uninstall: close the script. There is nothing to undo.

## Usage

The shifter's buttons map to gears:

| Shifter button | Result |
|---|---|
| 1 - 6 | Engage gear 1 to 6 |
| 7 | Hard reset: shift down to reverse, then up once to neutral |
| 8 | Reverse |
| none, for 0.6 s | Automatic neutral (1.5 s if the last gear was reverse) |

Two hotkeys are available while the script runs:

| Key | Result |
|---|---|
| `F9` | Show the gear the script currently believes is engaged |
| `F10` | Hard reset to neutral, same as shifter button 7 |

A tooltip shows the engaged gear after each change. If the script's idea of the gear
and the game's ever disagree, press `F10` and both return to a known state.

## Compatibility

| Version | Status |
|---|---|
| Mafia v1.3, GOG release (English) | Tested |
| Steam release | Untested |
| Other language builds, v1.0 - v1.2 | Untested |

Because nothing in the game folder is modified, this mod is compatible with other
mods by construction. It only sends button presses to a virtual controller.

The two related mods below were developed alongside this one and run together with
it in the same installation.

## Known issues

- **Gear tracking is open loop.** The script keeps its own count of the engaged gear
  and cannot read the game's actual gearbox state. If the game refuses a shift, for
  example at a speed where it will not downshift, the two drift apart. Shifter button
  7 or `F10` resynchronises by driving to a known state.
- **The joystick number is configuration, not detection.** If your shifter is not
  joystick 3, the script must be edited before it does anything.
- **Neutral is a timed decision.** Holding the lever between two gears for longer
  than the grace window engages neutral. That is deliberate, and the window out of
  reverse is longer because the reverse-to-first traverse is long, but a very slow
  shift can still trip it.

## Related mods

Three mods for the same game, developed together. They are compatible with each other
in a single installation, but each does a different job:

- **H-Shifter** - this repository.
- **Force Feedback (Real Driving Mod)** - force feedback for steering wheels, which
  the game never sends on its own. Not yet published; the link lands here when it is.
- **VR Mod** - stereoscopic VR with head tracking. Not yet published.

## For developers

The source is open on purpose. If you want to build your own input mod on top of it,
everything is here and there is nothing else to find.

- `src\HShifter_to_vJoy.ahk` - the whole mod, one file, AutoHotkey v2.
- `src\test_vjoy_minimal.ahk` - a minimal vJoy smoke test: load the DLL, acquire the
  device, pulse button 1, log every return value. Run this first when nothing works;
  it separates "vJoy is not set up" from "the shifter logic is wrong".
- [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) - the polling loop, the asymmetric
  neutral grace, why the shift sequences are atomic, and the reverse-aware reset.
  These three are the whole difficulty of the problem; read this before changing
  timings.

There is nothing game specific in the translation layer beyond the two output
buttons, so the same approach ports to any game with a sequential-only gearbox.

Pull requests and forks are welcome.

## Credits

- [vJoy](https://sourceforge.net/projects/vjoystick/) by Shaul Eizikovich - the
  virtual joystick driver that makes this possible.
- [AutoHotkey](https://www.autohotkey.com/) - GPL-2.0.

## License

MIT - see [LICENSE](LICENSE). It covers this project's own code only.

This mod requires a legal copy of Mafia: The City of Lost Heaven. No game files or
assets are included or distributed.

This project is not affiliated with or endorsed by Take-Two Interactive, 2K, or
Illusion Softworks. Mafia is a trademark of its respective owners. The MIT license
covers this project's own code only; the game and its assets remain the property of
their owners.
