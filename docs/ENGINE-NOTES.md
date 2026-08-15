# Engine notes: where these mods attach, and what the addresses mean

Everything below is **our own finding about the engine**, written so that somebody else can build
their own mod without repeating the measurement. An address, a structure offset, and a sentence
saying what a routine does are ours to publish. The engine's own code is not, and none of it is
here or anywhere else in this repository. See [HOW-IT-WORKS.md](HOW-IT-WORKS.md) for the two short
byte runs that *are* here and why.

If you use any of this, the licence is the same MIT as the code. Attribution is welcome and not
required.

## The build these numbers belong to

**GOG release, v1.3, build 16073**, `Game.exe` as shipped. All eight language editions of that
build carry a byte-identical `Game.exe`, `LS3DF.dll`, `IJoy.dll` and `a2.dta`, so the addresses
hold on every one of them.

**They will not hold anywhere else, and this is the part that bites.** A different retail or
patched release is not "the GOG build plus or minus a few patches"; it is a different compilation.
The reference we develop against is one such build, and against GOG its `.text` is `0x18000` larger
and it carries protection sections the GOG image does not have. Every number below moved. Two of
them moved by different deltas from each other, which is why each was re-derived independently
rather than shifted by a constant.

The mods carry a two-row build table for exactly this reason and refuse to patch a third build
rather than guess at it. If you fork this, keep that behaviour: patching a wrong address in
somebody else's game is worse than not running.

## Entry points

| mod | how it gets in |
|---|---|
| `mafia_ffb.asi`, `mafia_fp.asi`, `gearbox_hook.asi` | loaded by Ultimate ASI Loader as `dinput8.dll` beside `Game.exe`; each is a `-nostdlib` 32-bit DLL entered at `DllMain@12` |
| field of view | a static 4-byte patch to `Game.exe` at three sites, applied by the installer |

The camera mod does not patch code to get in at all. It walks `LS3DF.dll`'s own import directory
and finds the slot for `d3d8.dll!Direct3DCreate8`. On the GOG build that slot lands at **RVA**
`0x9B254` (`0x9721C` on the other build we know), and **the code does not contain either number** -
walking the structure the loader itself uses is correct on every build by construction, where an
address is correct only on the one it was measured against. The numbers are here so you can check
the claim, not so you can seek to them.

### The best seam in this engine is an exported name, not an address

Worth more than any offset on this page. `LS3DF.dll` exports its frame-update routine **by name**:

```
?UpdateWMatrixProc@I3D_frame@@AAEXXZ
```

It is exported on every build we have seen, so it is resolved with `GetProcAddress` and never
sought at a hard-coded offset. The engine's view builder calls it and then transposes the frame's
WORLD matrix into VIEW immediately afterwards, which means writing the eye position into WORLD's
translation row on the RETURN of that call gets you a camera the engine then derives everything
else from - VIEW, PROJECTION and every cached product - consistently, for every render pass.

The alternative we tried first, hooking `IDirect3DDevice8::SetTransform` and rewriting VIEW in
flight, works and is version-independent too, but it sits downstream of the engine having already
decided what the camera is: more than one pass sends VIEW per frame and they are distinguishable
only by return address, and the derived matrices at `camera+0x1E4` and `camera+0x224` are already
built from the old eye. If you are placing a camera in this engine, take the upstream seam.

Before anything is written there, the first six bytes of that function are hashed and compared
against a stored digest. They hold a stack adjustment, a push and a register move - no relative
branch and nothing position-dependent, so they can be copied into a trampoline verbatim. That was
checked rather than assumed, which is why the guard covers all six bytes instead of counting to
five and hoping.

## The runtime address table

These are the four numbers the force feedback and gearbox mods resolve at startup.

| what | GOG 1.3 / 16073 | meaning |
|---|---|---|
| game pointer | `0x63788C` | a global; the player's vehicle is reached from it |
| car probe site | `0x5A6619` | the instruction the mod replaces with a call, to be told when a vehicle exists |
| world matrix offset | `+0x0E4` | inside the LS3DF frame, see below |
| gear offset | `+0x5D0` | inside the same frame; `int32`, `-1..N` |

The vehicle object itself is reached as:

```
car = *( *(gamePtr) + 0x24 )
```

**The probe site is verified before it is patched, and not by hashing the file.** A file hash only
says "this is the file somebody measured once"; it says nothing about whether the address is still
the right one. The mod checks the code at the address instead: it hashes 40 bytes of the loaded
image starting 15 bytes before the probe site, and compares that digest against a constant. Those
40 bytes are identical on both builds we know of, so one digest covers both. A mismatch disables the
mod and logs it, rather than writing anything.

The digest is the constant, not the bytes: the source carries `PROBE_SIG_MD5` and no copy of the
code it describes. If you want to check that rather than take it, scan your own image for a 40-byte
window that hashes to it. There is exactly one in the GOG build and exactly one in the other build
we know, which is the property the guard depends on. We are deliberately not printing the offset
where it lands, for the same reason given under the field-of-view patch below: an offset is the
output of that search, and publishing it invites somebody to skip the search.

The camera mod guards a function prologue the same way, in `src/fp/fp_wmatrix.h`. A refusal there
logs the bytes it **found**, which is what a fault report needs, and never the bytes it wanted.

## Inside the vehicle object

Offsets are from `car`. All of these were derived from captured snapshots of a labelled drive and
then confirmed against a second run, not read off a structure definition.

| offset | what it is |
|---|---|
| `+0x58` | the LS3DF frame. The world rotation matrix and the gear int both live inside it, at the two offsets in the table above |
| `+0xE4` | leads to the render frame: `[[car+0xE4]+0x68]+0x40` is the position the frame is actually drawn from |
| `+0x2A10` | a once-per-tick **copy** of that drawn position. Convenient, and a trap: it is a copy, so it lags and it is not what the renderer reads |
| `+0x2A0C` | written twice per vehicle update; speed is derived by differencing it against the previous tick |
| `+0x2A18` | the other horizontal component of the same position pair |
| `+0x075C` | 100 records of 88 bytes, ending at `+0x29BC`, immediately before the index that walks them |
| `+0x0014 / +0x0018 / +0x001C` | a begin / end / capacity triple, the layout of a growable array |
| `+0x0154 / +0x0158 / +0x015C` | a second triple of the same shape |
| `+0x02F8 / +0x02FC / +0x0300` | a third |

A snapshot of `0x2000` bytes from `car` covers everything any of these mods reads; the highest
offset touched is `0x1F18`.

### One offset that is a trap, recorded so you do not spend the day we spent

`car+0x29E4` looks like a damage counter and is not one. It increments on events that are not
damage, and on the GOG build it reads **constant zero** for the whole drive. We built a feature on
it, measured it, and threw the feature away. If you need collision, take it from the solver named
below rather than from a counter that looks convenient.

## Routines identified by purpose

Addresses are GOG. What each one does is our conclusion from watching it run, not a transcription
of it.

| GOG address | what it does |
|---|---|
| `0x5A51C0` | the per-vehicle update. This is what writes `car+0x2A0C` twice and refreshes the position copy at `car+0x2A10` |
| `0x5A8E70` | the vehicle's collision-response solver. It walks a plain array of contacts |
| `0x5B1D50` | indexes the 100 x 88-byte records at `car+0x075C`. Byte-identical to the corresponding routine on the reference build |

## The field-of-view patch

Three sites in `Game.exe`, 4 bytes each, a `float` in degrees. The stock value is `70`; this
project recommends `86` for 16:9.

The installer does **not** seek by offset. Each site is found by an 8-byte prefix and an 8-byte
suffix with the four value bytes between them, and the installer refuses unless a pattern matches
**exactly once** in the whole image. That is what makes it safe to write an arbitrary angle rather
than only the two we tested.

For reference, on a stock GOG image the three hits land at file offsets `0x196275`, `0x0C0C10` and
`0x227974`. They are recorded so a log can say where the hit landed. **Do not seek to them.** They
are the output of the search, not its input, and the moment somebody patches the file with anything
else they stop being true.

## Rules worth copying along with the addresses

These are not style preferences. Each one is here because breaking it cost us something.

1. **Verify the code at the site, not the file.** A hash tells you about a file; a signature tells
   you about the address you are about to write to.
2. **Refuse an unrecognised build.** Log and disable. Never fall back to "probably still right".
3. **Same-length patches only** in the mission files. The level format carries size fields that an
   insertion corrupts, and it crashes on load rather than at the point of the mistake.
4. **Back up before the first write, and record that you made the backup.** If the user already had
   a backup, leave it alone: it is theirs, not yours to spend.
5. **A copy of a value is not the value.** `car+0x2A10` is the clearest example in this codebase:
   it is correct, it is convenient, and it is one tick stale in a way that only shows up as a
   subtle wrongness in motion.

## What is deliberately not in this file

No disassembly, no decompiled function bodies, no reconstructed source, and no game data. If you
need to see what a routine does at instruction level, run a disassembler over your own copy of the
game; that is your right with the copy you bought, and it is not ours to hand you.
