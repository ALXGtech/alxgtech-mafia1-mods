English | [Русский](README.ru.md) | [Čeština](README.cs.md)

# H-Shifter for Mafia: The City of Lost Heaven (2002) in 2026

Drive Mafia (2002) with a real H-pattern shifter. No AutoHotkey, no vJoy, no virtual
controller: a plugin inside the game plus a small tool to bind your lever.

<!-- ![banner](docs/img/banner.png) -->

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Before you install

- **Back up your saves.** Back up `<game folder>\savegame\` before installing. This mod
  does not modify `Game.exe` or any game data file, but your own backup is the one you
  control.
- **Game version.** Developed and tested on the GOG release of Mafia v1.3 (English).
  The gear offsets it reads are build specific, so on any other version or store
  release the closed loop will not resolve. Use at your own risk.
- **Other mods.** Compatibility with other mods has not been tested. Combining mods is
  at your own risk. The mod is loaded by Ultimate ASI Loader, which loads any other
  `.asi` mods you have alongside it; that is intended, but untested territory.
- **Antivirus.** `gearbox-setup.exe` is an unsigned executable that writes into your
  game folder, and the mod is a DLL loaded into the game. Some antivirus software may
  flag either. Verify your download against the SHA-256 checksum published with the
  release.

## What it is

Mafia's gearbox is sequential: the game knows "shift up" and "shift down" and nothing
else. An H-pattern shifter reports the opposite - an absolute position, "the lever is
in third", with no history. That mismatch is why an H-shifter cannot simply be bound in
the game's options.

This mod closes the gap inside the game. It reads your shifter through DirectInput,
works out which gear you are asking for, and presses the gear keys the game itself has
bound until the gearbox is in that gear. Nothing is emulated: no virtual joystick is
created, no synthetic keystroke is sent to Windows.

The important part is that it **reads the gear back from the game** after every step,
at `[car+0x58]+0x5D0` on the GOG build. That closed loop is what makes it reliable.
There is no counter of its own to drift, so a refused shift cannot desynchronise it,
and a car with two gears simply stops moving when it runs out - selecting fourth on a
two-gear truck leaves it in second instead of corrupting the box. No per-vehicle gear
table is needed, and none exists.

It also reads the game's manual/automatic flag (`[car+0x58]+0x53C`) and never writes
it. If you switch to automatic - with the wheel, with the keyboard, however - that is
your decision, and the mod goes hands off until you move the lever again.

Two files land in your game folder and `Game.exe` is not modified. An off switch in the
tool renames the plugin, so the next launch is completely stock.

## Requirements

| | |
|---|---|
| Game | Mafia: The City of Lost Heaven, GOG release v1.3 (English) - the only tested build |
| Hardware | An H-pattern shifter Windows sees as a DirectInput device. A wheelbase can host the gearbox-mode control; up to four devices are supported |
| Software | An ASI loader in the game folder - Ultimate ASI Loader as `dinput8.dll`. The Force Feedback mod's patcher installs one; otherwise install it yourself |
| In-game | Gear up, gear down and gearbox mode must be bound to keyboard keys in Mafia's own control options. The mod presses those keys |

## Installation

1. Make sure an ASI loader is present: `dinput8.dll` next to `Game.exe`.
2. Download the release archive and unpack the `gearbox hshifter setup` folder into
   your game folder, so it sits next to `Game.exe`.
3. Run `gearbox-setup.exe` from inside that folder. It finds `Game.exe` one level up,
   so nothing has to be configured.
4. Bind your rig. Click a row, then press what you want on the device:
   - rows for gears 1 to 6, reverse, neutral and the gearbox-mode control capture
     **device buttons**, from any attached device;
   - the last three rows capture **keyboard keys** - the gear up, gear down and mode
     keys as they are bound inside Mafia. The mod presses these, so they must match.
   - Neutral is an explicit choice: bind a button, or declare that neutral is your
     lever's rest position. Both kinds of hardware exist and there is no default.
5. Press **Install**. It copies `gearbox_hook.asi` into the game root and writes your
   settings to `gearbox hshifter setup\gearbox.ini`.
6. Launch the game and drive.

The resulting layout:

```
<game folder>\
    Game.exe
    dinput8.dll                       ASI loader, not part of this mod
    gearbox_hook.asi                  the mod
    gearbox hshifter setup\
        gearbox-setup.exe             the binding tool
        gearbox.ini                   your settings
        gearbox_hook.bin              the mod's log, written while you play
```

To uninstall: the tool's switch turns the mod off by renaming the plugin, and the next
launch is stock. Delete the two items to remove it completely. Nothing else was
touched.

## Usage

Move the lever and the gearbox follows. There is nothing to hold and nothing to time.

| Control | Behaviour |
|---|---|
| A gear gate | Acted on immediately. The mod shifts until the game reports that gear, or until the gearbox refuses to go further |
| Rest position (if neutral is unbound) | Neutral after 1100 ms. The delay exists because the lever passes through the rest position on the way between gates; measured gate-to-gate travel is 0.22 to 0.94 s, a deliberate neutral is 1.1 s or more |
| Leaving reverse | Exempt from that delay - that move goes through neutral by necessity |
| Gearbox mode control | Switches the game between manual and automatic. Works as a button or as a latching switch; the tool has a setting for which one you have |

Buttons you bind are suppressed from the game, so a held neutral button does not
dominate the game's own control-binding screen and gear buttons do not trigger
unrelated actions.

If you switch the game into automatic, the mod stops driving the box until you move the
lever again. Moving the lever always means "I want this gear", so it returns to manual.

## Compatibility

| Version | Status |
|---|---|
| Mafia v1.3, GOG release (English) | Developed and tested here |
| Steam release | Untested. The offsets are build specific and will not resolve |
| Other language builds, v1.0 - v1.2 | Untested |

`Game.exe` is not modified, so nothing needs to be restored and no file verification
will fail. The mod is one `.asi` among however many you have; the loader loads them
all.

## Known issues / FAQ

### Known issues

- **Verification is not finished.** Seven test drives settled the design and fixed four
  bugs; the eighth drive, which re-checks every fix together on real hardware, has not
  been signed off yet. Treat this as a working mod under test, not a finished one.
- **The closed loop is gated per build.** On a build whose gear offset has not been
  verified, `closed_loop=0` in the ini keeps the mod injecting keys open loop while the
  log records whether the address chain resolved. Reliable behaviour needs the loop on.
- **Your in-game key bindings must be correct in the tool.** The mod shifts by pressing
  the keys the game has bound. Bind different keys in Mafia and forget to update the
  tool, and nothing happens at all.
- **Buffered keyboard input is not implemented.** The mod injects into
  `IDirectInputDevice8::GetDeviceState`. If a build reads its keyboard through
  `GetDeviceData` instead, the log shows it and no shift is delivered.
- **Automatic mode fights you by design.** If the game is in automatic and the mod is
  asked for a gear, the log says so plainly rather than hiding it.

### FAQ

**Who is this for?**
Anyone who owns a real H-pattern shifter, or any DirectInput device with enough
buttons, and wants Mafia's gearbox to follow the lever's position directly - no
virtual controller, no scripting language, nothing to configure inside the game
beyond the gear-up/gear-down keys it already has.

**How do I get started?**
Make sure an ASI loader is present, unpack the release into your game folder, run
`gearbox-setup.exe`, bind your lever's gates and confirm the keys Mafia itself uses
for gear up/down, click Install, and drive. Full steps in
[Installation](#installation).

**Which game version does it run on?**
Only the GOG release of Mafia v1.3, English, is tested - it is what the documented
memory offsets were read on. On any other version or store release the closed loop
will not resolve, and the mod will not behave reliably.

**What does it give me?**
Real sequential shifting driven by where your lever actually is, closed against the
game's own gear so nothing can desync it, and automatic clamping to each car's real
gear count - a two-gear truck simply stops responding past 2nd instead of corrupting
the gearbox. `Game.exe` is never modified.

**What does it NOT give me?**
- No force feedback - that is a separate mod, [Force Feedback (Real Driving
  Mod)](#related-mods), not yet published.
- No VR - also a separate mod, [VR Mod](#related-mods).
- No clutch simulation and no engine/RPM modelling: it only presses the gear-up and
  gear-down keys the game already has bound.
- No support beyond GOG v1.3 English (see above).
- Not a finished product yet: the eighth verification drive has not been signed off
  - see Known issues above.

## Related mods

Three mods for the same game, developed together. They are compatible with each other
in a single installation, but each does a different job:

- **H-Shifter** - this repository.
- **Force Feedback (Real Driving Mod)** - force feedback for steering wheels, which the
  game never sends on its own. Its patcher also installs the ASI loader this mod needs.
  Not yet published; the link lands here when it is.
- **VR Mod** - stereoscopic VR with head tracking. Not yet published.

## For developers

The source is open on purpose. Everything the mod knows about the game is in this
repository - the offsets, the injection method, and the decision logic.

| File | What it is |
|---|---|
| `src/gearbox_hook8.c` | the mod: DirectInput hooks, key injection, button suppression, the closed loop |
| `src/gearbox_logic.h` | the decision core, shared with the test suite so tests cover the shipped code |
| `src/gearbox_gui.c` | `gearbox-setup.exe`: the binding tool, plain Win32, no framework |
| `tests/test_gearbox.c` | offline simulation of the shift logic |

Build with LLVM-MinGW, 32-bit:

```
i686-w64-mingw32-clang -O2 -m32 -mwindows -o gearbox-setup.exe gearbox_gui.c -lkernel32 -luser32 -lcomdlg32
```

How the shift is delivered: the game reads its keyboard through DirectInput 8, so the
mod creates a keyboard device of its own purely to reach the device class vtable,
patches `GetDeviceState`, and sets the bit of the configured scancode in the 256-byte
buffer the game is about to read. A 256-byte buffer is what identifies the keyboard;
the mod's own devices are excluded. Setting a bit and clearing one are the same code
path, which is how button suppression works on the wheel side.

Documented offsets on the GOG build, all read only except the input state:

| What | Where |
|---|---|
| current gear (commanded, not speed derived) | `[car+0x58]+0x5D0` |
| gear shadow, one cycle behind - a shift in flight | `[car+0x58]+0x5D4` |
| manual / automatic flag, 0 = manual | `[car+0x58]+0x53C` |
| key-binding action table, 63 records of `char[16]` + `DWORD` | VA `0x624330..0x624808` |
| GEARUP / GEARDOWN action ids | `0x2D` / `0x2F` |

The action ids are there because injecting the action rather than the key is the
planned upgrade: it would free the user from binding gear keys at all. The injection
point for it has not been derived.

See [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) for the closed loop, the refusal
handling, and the four bugs that shaped them. Pull requests and forks are welcome.

Want to build the same kind of closed-loop input translator for a different game?
[docs/PORTING.md](docs/PORTING.md) writes up the method - the injection technique, how to
find your own game's equivalent of "current gear," and how to test the decision logic
before touching the real game - without reproducing anything from Mafia's disassembly.

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by
  ThirteenAG - the loader that makes `.asi` plugins possible. Not distributed here.

## License

MIT - see [LICENSE](LICENSE). It covers this project's own code only.

This mod requires a legal copy of Mafia: The City of Lost Heaven. No game files or
assets are included or distributed.

This project is not affiliated with or endorsed by Take-Two Interactive, 2K, or
Illusion Softworks. Mafia is a trademark of its respective owners. The MIT license
covers this project's own code only; the game and its assets remain the property of
their owners.
