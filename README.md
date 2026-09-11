English | [Русский](README.ru.md) | [Čeština](README.cs.md)

# ALXGtech Mafia 1 Mods

Force feedback, a first-person driving camera and an H-shifter gearbox for the **GOG release,
v1.3 build 16073**, of Mafia: The City of Lost Heaven, in one utility that installs and removes
them.

![ALXGtech Mafia 1 Mods - force feedback, first-person camera, H-shifter, and VR in progress](docs/img/banner.png)

## 🚀 Quick start

Three mods for **Mafia: The City of Lost Heaven (2002)** - the OG Mafia 1 - in a single small program.

> **Built and tested for the GOG release, v1.3 build 16073 - any language edition.** All eight of
> them ship an identical `Game.exe`, so any one of them will do. **The Steam release and every
> other version or store have not been tested, and compatibility with them is not guaranteed.**
> The program checks your `Game.exe` and tells you when it is not the build this was made for.

Too long to read? This is all of it:

1. Download the `.zip` from **[Releases](../../releases)**.
2. Unpack it.
3. Copy `ALXGtech Mafia 1 Mods.exe` into your Mafia folder, next to `Game.exe`.
4. Run it.
5. Switch on the mods you want, inside the program, and play.

**Then set them up - [it is worth five minutes](#-setting-each-mod-up).** Especially the force
feedback, which needs to be told your wheel's rotation range before it can feel right.

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

## ⚙️ Setting each mod up

Every tab has the same three things at the top: the enable switch, your game folder, and a line
telling you whether the `Game.exe` it found is the build all of this was tuned on.

**Run Mafia once before you start.** Let it create a profile and quit. Two things depend on it: the
utility can only write the recommended in-game settings into a profile that exists, and the game
must have seen your wheel at least once.

### 🎮 Force Feedback

![The Force Feedback tab: wheel selection, rotation range, the strength sliders and the wheel-weight column](docs/img/tab-force-feedback.png)

Everything on this tab is **live** - it reaches the game as you move the slider, no restart.

1. **Plug the wheel in, then open the tab.** If it says the wheel is not plugged in, press
   **Refresh list** and pick your wheel from the dropdown. **Test - push the wheel** confirms the
   mod is talking to it.
2. **Press `ON: recommended in-game FFB settings`.** That writes the game's own handling and force
   feedback values this mod was tuned against, into your Mafia profile. Skip it and you are tuning
   against a different baseline than the one every number here assumes.
3. **Set `WHEEL ROTATION RANGE` to whatever your wheel's own driver is set to.** This is the one
   setting you must not guess. It changes nothing on the wheel - it tells the mod what to serve.
   Everything was tuned at **600 degrees**, and if your wheel's range is yours to choose, 600 is
   the recommendation. Eight values are offered - 90, 360, 540, 600, 720, 900, 1080 and 1440 - and
   the tab is honest about which is which: some were actually driven and confirmed, the rest are
   derived by formula and have never been driven. It says so under the selector.

Then the sliders. **100% is the shipped feel**, and the tall mark on each one is the recommended
value - 100 everywhere except **Gunfire, which is 0**. Two reasons, and the second one matters more
on this build. It was set to 0 because the effect fires for every shot from your car, your allies'
and your enemies' alike, and a jolt you did not cause feels like the wheel malfunctioning. And on
the GOG release the channel has nothing to act on anyway: it reads a counter that never moves
there - measured across a whole drive - which is the same reason being shot at is not felt at all.
The tab's own note describes the effect rather than this, so the slider is more dormant than it
looks.

| group | what it is |
|---|---|
| Overall strength | less of everything, in one control |
| Crashes and rams | the reference; 100 is the ceiling |
| Hitting objects | crates, bins, booths, hydrants |
| Pedestrians | what it says |
| Road surface | curbs, tram rails, offroad |
| Slide feel | the slip-angle effect |
| `Centering spring`, `Parking damper`, `Driving damper` | the wheel-weight column: how heavy the wheel feels standing still and moving |

Trucks have their own damper pair that multiplies the car values. Those are arithmetic and nobody
has driven them, which the tab says out loud. **Back to default settings** puts every slider back
to 100%.

**Presets 1, 2, 3.** The one lit green is the one being edited, and every value on the page goes
into it. `Import preset...` and `Export preset...` move them between machines.

**If nothing is reaching the wheel, the tab tells you which of the three it is.** There is a lamp
and a line at the bottom, and it is the first thing to look at before changing any slider:

| the lamp says | what it means |
|---|---|
| driving effects are on, and names your wheel | it is working |
| the game is not running, and names the wheel it last saw | the mod is fine, Mafia is not open |
| no force is reaching the wheel | the mod is loaded and something is stopping it - check the wheel is plugged in and picked |

The same area says so when the mod ended up holding a **different wheel from the one you chose**,
which happens if the chosen one was unplugged at the moment the game started. It names both, so a
wheel that has gone quiet is not a mystery.

**`ON: recommended in-game FFB settings` is reversible, per save profile.** Pressing it again puts
back exactly what that profile had before - not some factory default - and anything you changed
yourself in the meantime is left alone.

### 🕹️ H-shifter

![The H-shifter tab: a binding per gate, the A/M mode button with its two behaviours, and the three keys the game itself uses](docs/img/tab-h-shifter.png)

**This is the one tab whose changes need a restart of Mafia.** It says so at the top.

1. **Bind the gates.** Click a binding, then move the shifter into that gear. Click again to
   change it. Neutral usually needs nothing - on most shifters it is the rest position.
2. **Bind the A/M mode button, and pick how it behaves.** Two buttons under it:
   - **`Hold to switch A/M`** - for a gate on the shifter itself, where the button is held while
     you are in that position.
   - **`One press to switch A/M`** - for a separate button that clicks and springs back.

   **The tool tries to work this out for you**: when you bind the control it times how long it
   stays down and selects the matching behaviour itself. Check that it chose right - both work,
   and only one matches what your hand is actually doing.
3. **The three game keys at the bottom are the game's, not ours.** Set GEAR UP, GEAR DOWN and
   gearbox mode in **Mafia's own Options** first, then press the same keys here so the mod knows
   what the game is listening for.
4. **Restart Mafia.**

Each row has its own `clear` button if you want to unbind one. **A row bound to something that is
not plugged in stays bound** and says so, rather than quietly reverting to unset - so opening the
tab with the shifter unplugged does not lose your work. When the tab opens it also lists every
device it found, which is the fastest answer to "why is my shifter not in the list".

### 👁️ First person

![The First person tab: the wide-screen fix, the seat sliders, the horizon choice and the optional camera keys](docs/img/tab-first-person.png)

Live as well - **except `Field of view`**, which is the one row on this page that patches
`Game.exe` and needs Mafia restarted. The tab says so on that row.

- **`UI wide-screen fix`** un-stretches the radar and the speedometer, which Mafia drew for a 4:3
  screen. On by default; the button turns it off while the game runs.
- **Where the driver's eye sits** - height, forward/back, left/right, near clipping plane, look
  up/down, and field of view. The marked value on each slider is the seat this ships with.
  **Back to the default seat** returns to it.
- **`Field of view`** is 86, which fits a 16:9 screen; the game ships 70. This one patches
  `Game.exe`, and switching the mod off writes the original bytes back.
- **Horizon**: `Locks to horizon` is the recommended setting and what it was driven with.
  `Rolls with the car` is closer to a real head and harder to watch.
- **Keys to adjust the camera while driving** are optional and **keyboard only in this version.
  Wheel buttons are not supported here.** The six seat keys, F1-F6 as shipped, can be rebound on
  this tab. The near-plane pair `F9 / F10` cannot - those live in `mafia_fp.ini` only.
- **Presets 1, 2, 3**, same as the force feedback: the green one is being edited.

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

Setting the mods up is [its own section above](#-setting-each-mod-up). This is what you do once
they are set up.

While driving, with the first-person camera on, the shipped keys are:

| key | what |
|---|---|
| F1 / F2 | eye up / down, 2 cm a press |
| F3 / F4 | eye left / right |
| F5 / F6 | eye forward / back |
| F9 / F10 | near clip plane out / in |

Keyboard only, as the tab says. The seat you settle on is written back to `mafia_fp.ini`, so it
survives a relaunch, and the same values are on the First person tab. More controls exist unbound
in `mafia_fp.ini`.

Everything on the Force Feedback and First person tabs takes effect while the game runs, so you
can leave the utility open beside Mafia and feel a change on the next corner. Two exceptions:
`Field of view`, which patches `Game.exe`, and the whole H-shifter tab, whose bindings are read
when the game starts. Both say so on screen.

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

## For developers

The sources are open, including the mods themselves and the installer - everything under `src\`.
They are built with LLVM-MinGW for 32-bit Windows. **The build scripts are not published yet**;
they still carry machine-specific paths, and cleaning them up is on the list rather than done.
The mods are ASI plugins: the game's
`LS3DF.dll` imports `DINPUT8.dll`, Windows resolves that from the game folder, and the bundled
Ultimate ASI Loader loads every `.asi` beside it. From there each mod reads the engine's own
car state at known addresses and either writes forces to the wheel, moves the camera, or drives
the gear controls.

Pull requests and forks are welcome, including porting the technique to a different game.

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, bundled
  as `dinput8.dll`. See `THIRD-PARTY.md`. This is the only third-party code that ships here.
- Everything else is this project's own work.

### Inspiration - looked at, learned from, not used

The three projects below solved problems this mod also has, and seeing that they could be solved
was worth a great deal. **None of their code is in this repository or in anything it ships.** Each
was read as a reference for what is possible, and the implementation here was worked out
independently and came out different. They are named because they deserve to be, not because
anything was taken.

- **[WidescreenFixesPack - Mafia](https://github.com/ThirteenAG/WidescreenFixesPack/releases/tag/mafia)**
  by ThirteenAG. The reference for the wide-screen interface problem. Our correction is a different
  approach and shares no code with it.
- **[GTAV Manual Transmission](https://github.com/ikt32/GTAVManualTransmission)** by ikt32. A
  different game and a different engine, and what we read it for was its **vehicle physics** - how
  it handles slip angles in particular. Our force feedback works those out its own way, from Mafia's
  own car state, and shares no code with it.
- **[Mafia First Person Shooter Mod](https://www.moddb.com/mods/first-person-camera/downloads/mafia-first-person-shooter-mod)**.
  The reference that a first-person view in this game is achievable at all. Ours is done completely
  differently and shares no code with it.

### If you are not named here and you should be

**This list is almost certainly incomplete.** Work gets read, learned from and half-remembered
years later, and the person who did it never hears about it. If something of yours belongs on this
page and is not on it, say so and it goes on.

The same offer runs the other way for anyone already mentioned: a credit line, the way a technique
is described, a link, the mention itself - tell us what you want different, or gone, and it is
done. In full, without argument, and without you having to justify the request.

Open an issue here, or reach out whatever way is easiest for you. This goes for anyone whose work
is referenced even indirectly. Nobody should have to make a case about how their own work is
described.

## License and legal

This project's own code is MIT licensed. See `LICENSE`.

This mod requires a legal copy of Mafia: The City of Lost Heaven. No game files or assets are
included or distributed.

This project is not affiliated with or endorsed by Take-Two Interactive, 2K, or the former
Illusion Softworks. Mafia is a trademark of its owners. The MIT license covers this project's
own code only; the game and its assets remain the property of their owners.
