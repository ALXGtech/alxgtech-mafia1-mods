# How it works

Developer notes for ALXGtech Mafia 1 Mods, in English and not translated - this is the file for someone
who wants to read the code, build it, or port the technique to another game. Everything in this file is
our own research or a property of our own build; none of it is copied from the game's own code. What the
repository does and does not contain of the game's is stated exactly under "What is ours and what is not".

Facts in this file are taken from the release manifest and from the project that builds the
binaries, on 2026-08-08. Where something was measured rather than reasoned about, it says so.

**Looking for the addresses?** They are in [ENGINE-NOTES.md](ENGINE-NOTES.md): where each mod
attaches on GOG v1.3 build 16073, what the vehicle-object offsets mean, which routines we
identified and what they do, and the discipline that keeps a patch from landing in the wrong place.
This file explains the architecture; that one is the map you need to build something of your own on
top of it.

## The shape of it

One window, `ALXGtech Mafia 1 Mods.exe`, with four tabs. Three install ordinary ASI plugins; the fourth,
VR, installs nothing - it is a coming-soon placeholder for the VR mod, still being built.

| binary | mod | what it hooks |
|---|---|---|
| `mafia_ffb.asi` | Force feedback | DirectInput8 device state and effect output |
| `mafia_fp.asi` | First-person camera | the engine's camera pose, per frame |
| `gearbox_hook.asi` | H-shifter | `IDirectInputDevice8::GetDeviceState` on the keyboard device class |
| none | VR | placeholder tab only - installs nothing |
| `dinput8.dll` | none - it is the loader | Ultimate ASI Loader by ThirteenAG, unmodified. MIT, see `THIRD-PARTY.md` and `licenses\` |

### Why a `dinput8.dll` in the game folder runs our code

The game's `LS3DF.dll` imports `DINPUT8.dll`. Windows resolves an import from the executable's
own folder before the system directory, so a `dinput8.dll` next to `Game.exe` is loaded instead
of the system one. Ultimate ASI Loader is that DLL: it forwards every DirectInput export on to
the real system library and, on load, loads every `.asi` file beside it. Each `.asi` is a plain
32-bit DLL whose `DllMain` installs its own hooks.

Nothing about this is specific to Mafia. Any game that imports a DLL you are allowed to place
next to it can be extended the same way, which is why the loader is a generic project rather
than part of this one.

## What each mod does at a high level

**Force feedback** reads live car state every frame and writes a DirectInput8 force set:
speed-dependent centering, a damper at a standstill, lightening as grip is lost, curbs, tram
rails, the roll of leaving the road, and impacts. Settings are re-read from disk once a second,
so a change on the Force Feedback tab is felt on the next corner rather than after a restart.
The stock game does knock the wheel on a collision; this replaces that with a full model, and it
is built for modern direct drive bases. The gunfire channel ships off, and 0 is the recommended
value rather than a not-yet-supported one: the tap fires for every shot leaving the car, including
shots the player did not fire, which reads as the wheel misbehaving.

**First-person camera** moves the camera into the driver's seat while driving and leaves the
on-foot camera alone. Switching it on also widens the field of view from 70 to 86 degrees, which
is the only thing in this project that writes to `Game.exe`: three four-byte floats, same length,
original bytes recorded in the journal, written back when the mod is switched off. Seat position,
near clipping plane, horizon lock and aim are read from `mafia_fp.ini` and can be changed while
driving; where you leave the seat is where it stays.

**H-shifter** reads the shifter through DirectInput and drives the game's OWN gear controls
rather than writing a gear anywhere. A shift is performed by patching
`IDirectInputDevice8::GetDeviceState` on the keyboard device class and setting the bit of the
scancode the game itself has bound, so the game shifts exactly as it would for a key press. The
loop is CLOSED: after every step it reads the gear the game reports at `[car+0x58]+0x5D0`, so it
cannot drift out of step, and it clamps to each car's real gear count without a per-car table.
The manual/automatic flag at `[car+0x58]+0x53C` is read and never written directly. Since 1.1.0
the module drives the mode itself when it needs to: it presses the game's own mode key on the
player's behalf, an owed engage-a-gear-then-switch-back manoeuvre that expires after three
seconds if it cannot be completed. Binding the A/M control in the setup utility auto-detects
whether it is a latching switch or a momentary button, deciding after one second of input.

Addresses and struct offsets like those are our own research on a specific build, published as data.

## What is ours and what is not

This section used to be one sentence - "no original game code is reproduced anywhere in this
repository" - and that sentence was not true. It is replaced with the exact position, because an
overclaim in your own documentation is a worse problem than the thing it denies.

**Not here, and never will be:** game assets of any kind, the game executable or its archives, any
decompiled or reconstructed game source, and any disassembly listing of it. None of that is ours to
publish, and none of it is in this repository or in any release.

**Here, deliberately, and this is the whole of it:** one set of short runs of the stock image's own
bytes, used to find the places this project patches.

| where | how much | what it is for |
|---|---|---|
| `FOV_SITES`, `src/patcher/install_core.h` | three pairs of 8 bytes, 48 in total | brackets the three field-of-view floats, so the patch site is found by the code around it rather than by a fixed offset |

They are locators, they carry none of the game's logic, and they exist for a safety reason rather
than a convenience one: a mod that patches the wrong address in somebody else's game is worse than a
mod that refuses to run. They are annotated as deliberate at their definition, so the line above can
be checked against the code rather than believed.

**Where a byte run was only ever compared for equality, it is no longer the bytes.** The build guard
in `src/ffb/mafia_ffb_v6.c` and the prologue guard in `src/fp/fp_wmatrix.h` both hold an **md5 digest**
of the code they check, and hash the image at run time to compare. Same guarantee, nothing of the
game's carried. A run used as a *search* pattern cannot be converted that way, which is why
`FOV_SITES` is still bytes and is the only entry in the table.

## Building from source

Toolchain: **LLVM-MinGW**, `i686-w64-mingw32-clang`, 32-bit, from the
`llvm-mingw-20260616-ucrt-x86_64` distribution. Every mod is a `-nostdlib` DLL entered at
`DllMain@12`. The link adds `--no-insert-timestamp --build-id=none`, which is what makes a build
reproducible: without them the PE header carries the build time and unchanged source produces a
new hash every time, so any published checksum is stale before it is read.

| binary | sources |
|---|---|
| `mafia_ffb.asi` | `src\ffb\mafia_ffb_v6.c`, `ffb_settings.c`, `ffb_status.c` |
| `mafia_fp.asi` | `src\fp\fp_camera.c`, `src\shared\car_anchor.h` |
| `gearbox_hook.asi` | `src\spike\gearbox_hook8.c`, `gearbox_logic.h` |
| `ALXGtech Mafia 1 Mods.exe` | 15 files under `src\launcher\`, 4 under `src\patcher\` |
| `mafia-gog-patch.exe` | `src\patcher\patcher.c` over the same install core. A console front end over one engine; it is not in the release archive |

That list is not a description of the tree - it is the build's own record, carried in the release
manifest, and the release script refuses to build if any path in it is missing. It refuses for the
same reason if the version number shown on every tab of the window disagrees with the release
number.

`src\spike\` is a historical folder name, not a statement about the code: `gearbox_hook8.c` and
`gearbox_logic.h` are the live shipping sources of the gearbox mod. The route-finding
experiments that shared that folder are not published, because a reader who studies a
superseded variant learns a design that was abandoned.

### Two things that will surprise you

- **The link's output file NAME is part of the binary.** clang writes it into `.rdata`, so the
  same sources linked under a different name produce a different file - about a kilobyte's worth
  of difference in the case that was measured. The force feedback module is therefore linked to a
  versioned name and copied into place afterwards. If you build `mafia_ffb.asi` directly you get
  a correct mod whose checksum does not match the released one. That is expected, not a
  corrupted download.
- **Reproducibility is measured, not assumed.** A dry-run rebuild of the first-person module
  reproduces the shipped `mafia_fp.asi` byte for byte. The force feedback module reproduces its
  own reference build exactly when linked under the matching name.

The build scripts are not in this repository yet: they currently hardcode an absolute toolchain
path from the machine they were written on, and shipping a script that only runs in one place
would make "build from source" a claim with an asterisk. They are being parameterised.

## What the installer does to your disk

Everything the installer writes is journalled first, and the uninstall works from that record
rather than from a list compiled at removal time.

- **The journal** is `<game>\ALXG mods\install.log`: tab-separated, append-only, flushed per
  line, so an install interrupted by a crash still leaves a usable record. One line per
  operation - `ADD` (path, the md5 written), `REPLACE` (path, md5 before, stash name, md5
  written), `PATCH` (path, offset, original bytes, new bytes), `MKDIR` (path). Paths are relative
  to the game folder, so moving the installation does not break the undo.
- **Displaced files** are kept in `<game>\ALXG mods\original\`.
- **A file you changed yourself** is detected by hashing what is on disk against the md5 the
  journal recorded writing. If they differ, the file is left in place and reported, and its
  journal line survives - the program does not overwrite work it did not do.
- **If a recorded original is missing** from `original\`, the undo refuses that one line, says
  so, and keeps it. It never guesses at what used to be there.
- **An existing `dinput8.dll`** from another mod is copied to `dinput8.dll.bak`, stashed in
  `original\` with a `REPLACE` line, then replaced; uninstalling puts it back. The stash is what
  the program reads; the `.bak` is there for a human.
- **An unrecognised `Game.exe`** installs with a warning rather than a refusal, because the
  journal is a way back out. Only the field-of-view step is skipped, since its signatures are
  exactly what cannot be found on an unknown build. A caller that wants the strict behaviour
  passes `--refuse-unknown`.

Apart from those three floats in `Game.exe`, the installer itself does not touch game data - no
`.dta` archive, no `tables\`, no `sounds\`, nothing localized. A separate write happens outside
the install/uninstall journal: the Force feedback tab's Recommended in-game settings toggle writes
into the player's own profile, `savegame\mafiaNNN.sav` - the car handling, built-in force feedback
and sound levels the force feedback module was tuned against. What was in the profile is recorded
before anything is written, per profile, and given back when the toggle is switched off. The
player's name and mission progress are never touched.

## Diagnostics

**Nothing diagnostic ships enabled.** The force feedback module can write binary trace streams
(one pair of files per launch of the game, and they are not small - megabytes per drive), and
since v7.69 they are behind `diag = 1` in `mafia_ffb.ini`, default `0`. A normal install writes
neither. If a log is ever asked for, the answer is to set that key and reproduce.

There is also **no test force at startup**. The only force the mods apply on purpose without you
driving is the test button on the Force Feedback tab.

## Porting this to another game

The parts that transfer are the shape, not the numbers:

1. Find an import the target game resolves from its own folder, and put a forwarding DLL there -
   or use Ultimate ASI Loader, which already does this for a long list of games.
2. Find the game's own state rather than modelling the car yourself. Every number this project
   uses is read out of the engine: speed, slip, gear, surface, the camera matrix. That is what
   makes the result feel like the game rather than like a simulation bolted onto it.
3. Drive the game's own controls where you can. The gearbox mod presses the key the game has
   bound instead of writing a gear, which is why it cannot desynchronise from the game's state.
4. Close the loop. Read back what the game did and correct against it, rather than counting your
   own actions and trusting the count.
5. Journal every write before making it. An uninstall that reads a record can put back exactly
   what was there; one that reasons about what it probably did cannot.

Pull requests and forks are welcome, including porting the technique to a different game.
