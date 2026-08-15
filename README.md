English | [Русский](README.ru.md) | [Čeština](README.cs.md)

# ALXGtech Mafia 1 Mods

Force feedback, a first-person driving camera and an H-shifter gearbox for the GOG release of
Mafia: The City of Lost Heaven, in one utility that installs and removes them.

<!-- docs/img/banner.png - not in this release yet -->

## 🚀 Quick start

Three mods for **Mafia: The City of Lost Heaven (2002)** - the OG Mafia 1 - in a single small program.
Too long to read? This is all of it:

1. Download the `.zip` from **[Releases](../../releases)**.
2. Unpack it.
3. Copy `ALXGtech Mafia 1 Mods.exe` into your Mafia folder, next to `Game.exe`.
4. Run it.
5. Switch on the mods you want, inside the program, and play.

| | mod | in this release |
|:--:|---|---|
| 🎮 | **Force feedback** - a full force model for a direct drive wheel | ✅ ready |
| 👁️ | **First-person driving camera** - the driver's seat, and a field of view that fits 16:9 | ✅ ready |
| 🕹️ | **H-shifter gearbox** - a real H-pattern shifter drives the game's own gears | ✅ ready |
| 🥽 | **VR** | 🚧 work in progress - its tab is in the window, it installs nothing yet |

Everything below is detail: what each mod does, what it writes into your game folder, and how to
take it back out.

## Before you install

- **Back up your saves.** They are in `<game>\savegame\`. The installer records every file it
  writes and can undo all of it, but your own backup is the one you control.
- **Version.** Built for the GOG release of Mafia v1.3 (build 16073). All eight language
  editions ship an identical `Game.exe`, so any of them will do; it was play-tested on the
  English one. The utility checks your `Game.exe` and says so when it is not the build
  everything was tested on. Any other version or store release is at your own risk.
- **Other mods.** Compatibility with other mods has not been tested. The bundled ASI loader
  will load any other `.asi` mods you have, which is intended but untested territory.
- **Antivirus.** `ALXGtech Mafia 1 Mods.exe` is an unsigned executable that writes files into a game
  folder, so some antivirus software may flag it. Verify your download against the SHA-256
  checksums published with the release.

## What it is

Four mods in one window, each switched on or off from its own tab. Three of them ship today;
the fourth says when it will not.

**🎮 Force feedback.** A complete DirectInput8 force set driven from the car's live state:
centering that follows speed, a damper at a standstill, lightening as the tyres let go, curbs,
tram rails, the roll of going off the road, and impacts. It is built for modern direct drive
wheels, by people who drive on them, because the original does not behave the way a wheel
should in 2026. The stock game does knock the wheel on a collision; this replaces that with a
full force model. Settings are re-read while the game runs, so a change on the Force Feedback
tab is felt on the next corner and not after a restart.

**👁️ First-person driving camera.** The camera sits in the driver's seat instead of behind the
car. On foot is untouched. The seat position ships at the place it was settled at the wheel and
can be moved with the keys below while you drive; where you leave it is where it stays.
Switching this mod on also widens the game's field of view from 70 to 86 degrees, which is what
fits a 16:9 screen - 70 is at its worst from the driver's seat. The angle is a slider on the
same tab, and switching the mod off puts the original bytes back.

**🕹️ H-shifter gearbox.** A real H-pattern shifter drives Mafia's own gear controls, so a gate is
a gear. It reads the shifter through DirectInput, hides the bound buttons from the game, and
checks the game's own gear after every shift, so it cannot drift out of step and it clamps to
each car's real gear count without a per-car table.

A fourth mod, **🥽 VR**, is **🚧 coming soon**. Its tab is in the window so that nobody has to wonder
whether it was forgotten, and it says the same thing: not finished, installs nothing yet.

## Requirements

- Windows, 32-bit or 64-bit.
- Mafia: The City of Lost Heaven, GOG release, v1.3 build 16073 (`Game.exe`, 2 355 200 bytes,
  md5 `b500437f340b8a2f1e847e10bb974a06`). Any language edition.
- Force feedback: a DirectInput force feedback wheel. Developed and tested on a direct drive
  base.
- H-shifter: an H-pattern shifter that Windows sees as a game controller.
- Nothing else. No AutoHotkey, no vJoy, no virtual controller, no .NET runtime.

## Installation

1. Download the `.zip` from [Releases](../../releases) and unpack it.
2. Copy `ALXGtech Mafia 1 Mods.exe` into your Mafia folder, next to `Game.exe`.
3. Run it. The header line shows which folder it is working on and which build it found; use
   `Choose...` if it picked the wrong one.
4. Open a tab and press `Enable this mod`. The switch is the install; there is no Save button
   and nothing else to press.
5. Start the game.

To remove a mod, press `Disable this mod` on its tab. The ASI loader leaves with the last mod
that needed it, and the utility's own folder goes with it.

### What enabling a mod puts in the game folder

| mod | files |
|---|---|
| every mod | `dinput8.dll` (Ultimate ASI Loader) |
| Force feedback | `mafia_ffb.asi` |
| First person | `mafia_fp.asi`, `mafia_fp.ini`, and 12 bytes inside `Game.exe` (the field of view) |
| H-shifter | `gearbox_hook.asi`, `ALXG mods\gearbox hshifter setup\gearbox.ini` |

The field of view is the only thing here that touches `Game.exe`, and it is three four-byte
floats, same length, nothing moved. `Game.exe.bak` is made before the first one is written, the
original bytes are recorded, and switching the camera off writes them back. No game data is
touched at all: no `.dta` archive, no `tables\`, no `sounds\`, nothing localized.

Everything of ours lives in one folder, `<game>\ALXG mods\` - the journal, the gearbox's
settings and the force feedback's. Only the three `.asi` files and the ASI loader sit in the
game root, because the loader reads no other folder.

Every write is recorded first, in `<game>\ALXG mods\install.log`, with anything it displaced
kept beside it in `original\`. Uninstall works from that record, one line at a time, so it can
put back exactly what was there. A file you have edited yourself is recognised as yours, said
out loud, and left alone.

### Manual install

If you would rather not run the utility, `manual-install\` holds the same files. Copy them into
the game folder in the layout shown there. You then have no journal and no uninstall; delete
the files by hand to remove the mods. The two `.ini` files are settings, so copy them only if
you do not already have your own.

## Usage

While driving, with the first-person camera on:

| key | what |
|---|---|
| F1 / F2 | eye up / down, 2 cm a press |
| F3 / F4 | eye left / right |
| F5 / F6 | eye forward / back |
| F9 / F10 | near clip plane out / in |

The seat you settle on is written back to `mafia_fp.ini`, so it survives a relaunch. The same
values are on the First person tab, together with the near plane, the horizon lock, where the
camera aims, and the field of view. Any of those keys can be rebound there, and more controls exist unbound in
`mafia_fp.ini`.

The H-shifter is bound on its own tab: press the button that corresponds to each gate, then
start the game. Gearbox changes apply after a restart of Mafia, which the tab says on screen.

## Compatibility

| tested | result |
|---|---|
| GOG v1.3 build 16073, English | tested, this is the build everything was measured on |
| GOG v1.3, Russian | installed and started on a fresh install: byte-identical `Game.exe`, all three mods loaded |
| GOG v1.3, the other six languages | identical `Game.exe`, so expected to work, not play-tested |
| Steam and other releases | untested. The utility will say it does not recognise the build |
| other mods | untested |

Only one `dinput8.dll` can live in a game folder. If you already have an ASI loader there, the
existing one is backed up before it is replaced, and put back on uninstall.

## Known issues

- A car ramming you from behind is barely felt. It is measured rather than guessed: no channel
  we have reports it yet, and loosening the impact gate to catch it brings back false kicks that
  were worse. It is a known bug and it is not fixed in this release.
- The game's damage counter on this build gives us nothing usable, so being shot at is not felt
  through the wheel.
- The camera keys are keyboard only in this version. A wheel button cannot be bound to them yet.
- The field of view slider takes effect when Mafia next starts, unlike everything else on that
  tab, which reaches a running game in about a second.
- The VR tab installs nothing - that mod is not finished.
- The utility has no icon of its own yet.

## For developers

The sources are open, including the mods themselves and the installer. Built with LLVM-MinGW
for 32-bit Windows; the build scripts are in `tools\`. The mods are ASI plugins: the game's
`LS3DF.dll` imports `DINPUT8.dll`, Windows resolves that from the game folder, and the bundled
Ultimate ASI Loader loads every `.asi` beside it. From there each mod reads the engine's own
car state at known addresses and either writes forces to the wheel, moves the camera, or drives
the gear controls.

Pull requests and forks are welcome, including porting the technique to a different game.

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, bundled
  as `dinput8.dll`. See `THIRD-PARTY.md`.
- Everything else is this project's own work.

## License and legal

This project's own code is MIT licensed. See `LICENSE`.

This mod requires a legal copy of Mafia: The City of Lost Heaven. No game files or assets are
included or distributed.

This project is not affiliated with or endorsed by Take-Two Interactive, 2K, or the former
Illusion Softworks. Mafia is a trademark of its owners. The MIT license covers this project's
own code only; the game and its assets remain the property of their owners.
