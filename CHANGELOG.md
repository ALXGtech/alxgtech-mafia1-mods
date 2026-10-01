# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project uses semantic
versioning.

## [2.0.0] - 2026-10-01

A major version, on purpose. The force feedback is a new model of the road rather than a retune of
the old one, and the utility no longer offers the old one: there is no "classic" switch and no old
profiles. Whoever wants the 1.4.3 feel back installs 1.4.3, which stays on the Releases page.

### Changed

- **The force feedback is a new road model, tuned by driving.** Steering, damper, body roll, road
  texture and crashes are one model built around the front tyres, instead of a set of separate
  effects. It was tuned on a direct drive wheel at a rotation range of 600 degrees, and that
  tuning is what 100% means on every slider.
- **The wheel is centred by a tyre curve, not by a spring.** The centring is a self-aligning
  torque worked out from the slip angle of the front tyres. It rises in a straight line to its
  peak at 9 degrees of slip and fades in a straight line to nothing at 15.3 degrees, so the wheel
  goes light as the front tyres let go. It acts from the first movement of the car, and it is the
  only centring force there is: the spring is off. A caster self-steer, weak near the centre,
  sits on top of it.
- **The wheel gets heavier with speed.** That torque is scaled by a speed curve: 22% at 10 km/h,
  34% at 20, 42% at 30, 60% at 50, 80% at 60, 100% at 70 km/h (43 mph), 122% at 80 and 190% at
  110 km/h, in straight lines between those points and flat beyond the ends.
- **The damper lets go in a slide.** It is firm at a standstill and has eased to a lighter moving
  value by 10 km/h. At a full slide 78% of it is taken away, and in the air, where the tyres carry
  no load, it drops to a light floor.
- **Body roll and curbs are a quick knock, not a steady lean.** Only the fast rocking of the body
  is felt; the steady lean is filtered out. Roll runs at 38% of its original strength with a
  ceiling of its own, and sharp curbs also come through as pitch, full up to 60 km/h and easing to
  60% from 110 km/h. Both switch off while the car is sliding and ease back in afterwards.
- **Crashes follow the speed you hit at.** The force of a crash is built as before, from how hard
  and how fast the hit was, up to a ceiling. What is new is a trim by the true speed at impact,
  which is the highest speed of the last second and a half: 70% of the force up to 70 km/h
  (43 mph), rising in a straight line to 100% at 90 km/h (56 mph). Slow crashes are gentler than
  in 1.4.3; fast ones are not.
- **Wheel rotation range: full force at 600 degrees and below, stronger centring above.** Nothing
  is turned down for a smaller wheel any more, so 90, 360, 540 and 600 degrees all get the same
  forces. Above 600 the centring (torque and caster) and the roll and pitch forces are raised by
  the cube root of the range over 600: x1.063 at 720, x1.145 at 900, x1.216 at 1080 and x1.339 at
  1440 degrees. Crashes, road texture and the dampers are the same at every range.
- **Every slider runs from 0 to 400%, and 100% is the tuned reference.** A slider scales its whole
  channel, ceiling included, so the shape that was tuned survives on a weaker or a stronger
  setting. Gunfire is the one row whose recommended value is not 100: it stays at 0.

### Added

- **Road texture is a channel of its own, with its own slider.** Fine road detail such as cobbles
  is built from how fast the front suspension moves, left wheel against right, and the roll and
  curb slider never touches it.
- **Sliders for the parts of the model that had none:** steering weight, and how much the damper
  lets go in a slide.

### Removed

- **The 1.4.3 force feedback, and any switch back to it.** There is one model: no "classic" mode,
  no old profiles and no mode for the game's own force feedback. 1.4.3 stays on the Releases page
  for anyone who wants that feel.
- **The Centering spring and Slide feel sliders.** The new model has no spring, and the older slide
  effect that Slide feel scaled has no place in it.

### Fixed

- **The H-shifter could drop into automatic by itself.** The game works the gearbox mode out again
  from a setting that its own start-up code clears; the mod now keeps the mode you chose. Not yet
  confirmed on a long drive in manual.

### Known limitations

- Wheel ranges 540, 720, 1080 and 1440 degrees follow the range law above but were not driven;
  90, 360, 600 and 900 degrees were.
- At race-track speed the wheel can occasionally take a full-strength crash jolt on an open
  straight with nothing to hit. It is recorded and not fixed in this release.
- Developed and tested on a direct drive base. Other wheels are untested; the sliders, now up to
  400%, are there for them.

### For developers

- The tuning hotkeys of the development builds, the letter keys and F1 to F6 that switched the
  force feedback module between test settings, are off in the shipped module.

## [1.4.3] - 2026-08-15

### Fixed

- **The Force Feedback tab could show settings it had not read.** Before there is a
  `mafia_ffb.ini` to read - a fresh install, or one where the force feedback was not switched on -
  the page kept whatever numbers happened to be in it and showed those. Pressing **Reset to
  Default** then moved a slider, which is the window contradicting itself about what the default
  is.

  It showed up on one row and only one: **Gunfire**, which is the single slider whose recommended
  value is not 100. It is 0 on purpose - the wheel shakes for every shot fired from your car,
  including your allies', and a jolt you did not cause reads as the wheel going wrong.

  With no file, the page now shows the recommended settings, which is exactly what switching the
  mod on writes and exactly what Reset to Default writes. The three can no longer disagree.

## [1.4.2] - 2026-08-15

### Fixed

- **The wide-screen interface fix did nothing on some machines, with no way to tell why.** The
  camera worked, the switch said ON, the setting was right in the file - and the interface stayed
  stretched. On those machines every one of the mod's drawing hooks had been quietly refused.

  The mod checks that a slot it is about to take really belongs to Direct3D, so that it never calls
  into some other program's hook by mistake. It worked out "which module is Direct3D" from the
  entry point the game imports - and on a machine where something else answers that import first,
  it learned the wrong answer and then refused the genuine Direct3D. It now decides by looking at
  the graphics device itself and taking the module that owns most of it, which is right whether you
  are on stock Windows, behind a wrapper, or on Wine, and does not depend on any module's name.

  If your radar was still an oval with the fix switched on, this is why, and it is fixed.

- **A refusal now says who owns the slot, by name.** The old log recorded seven bare "could not
  probe" lines and named nobody, so the one machine that failed produced a log that could not
  explain it. It now names both modules and how much of the device each owns.

## [1.4.1] - 2026-08-15

### Fixed

- **Switching a mod on left its page showing the settings from before it was installed.** Most
  visibly: enable the first-person camera on a fresh copy of the game and the wide-screen interface
  button still read `OFF`, while the `mafia_fp.ini` the install had just written next to it said the
  fix was on. The switch installed the files and told the page nothing, so the page kept the
  values it had guessed while the folder was still bare.

  It was not only a wrong caption. That page writes one setting at a time out of its own
  variables, so the next change you made on it would have written the stale `OFF` back over the
  file - turning off a fix you never turned off. Both halves are gone: after a mod is switched on,
  its page re-reads what is now in the folder and repaints itself.

  Nothing about the mods themselves changed in this release. If you installed 1.4.0 and the
  interface button read `OFF`, open this version and look again - it will show you what your
  `mafia_fp.ini` actually says.

## [1.4.0] - 2026-08-15

### Changed

- **The wide-screen interface fix is now ON out of the box.** In 1.3.0 it shipped switched off,
  because on a fresh install nobody had yet watched it do anything. It has now been driven: the
  full camera ring, three laps, the radar and the speedometer round in every view, and 14818 draws
  corrected over the drive. The reason for shipping it off is gone, so it ships on.

  Nothing else about the picture changed, and the switch is still there: the button on the
  **First person** tab turns it off again, and it takes effect while the game is running - no
  restart.

### Known limitation

- The vehicle blips inside the radar are drawn by the engine in a way this mod cannot reach, so
  they are not un-stretched with the ring. Ones near the edge of the radar can sit slightly over
  its rim.

## [1.3.0] - 2026-08-14

### Added

- **The wide-screen interface fix, with a switch on the First person tab.** Mafia's interface was
  drawn for a 4:3 screen, and on a 16:9 one the game stretches it - the radar comes out an oval
  rather than a circle. Turn the fix ON and the radar and the speedometer are round again.

  Only those two are touched, and only while you are sitting in a car: the mission text, the health
  and ammo blocks, the menus and everything else are drawn exactly as the game draws them. It
  follows the CAR, not the camera - the instruments stay round in every driving view, inside the
  cabin or behind the car, and pressing C to change the view does not turn it off. The
  correction is worked out from your resolution, so it is right at 1080p, 1440p and 4K alike, and
  on a 4:3 screen it does nothing at all.

  **It ships OFF.** Turn it on if you want it.

### Known limitation

- The vehicle blips inside the radar are drawn by the engine in a way this mod cannot reach, so
  they are not un-stretched with the ring. Ones near the edge of the radar can sit slightly over
  its rim.

## [1.2.2] - 2026-08-14

Nothing in this release changes what the mods do. It exists so that the published archive names
a commit anyone can check out and rebuild: the 1.2.1 archive was assembled from a working tree
that our own release tool reported as modified, which makes its binaries unreproducible even
though nothing was wrong with them. The tool had been measuring the tree after writing to it.

## [1.2.1] - 2026-08-14

### Changed

- **The wide-screen interface correction ships OFF.** It squeezes Mafia's 4:3 interface back to
  the right proportions on a 16:9 screen, and on a fresh install it could not be seen doing
  anything, so it is not something this release claims to do. `hud_aspect_pct = -1` in
  `mafia_fp.ini` turns it on for anyone who wants to try it. Nothing else about the picture is
  touched: with it off, the game draws exactly what it drew before.

### Fixed

- **An uninstall could leave `mafia_fp.ini` behind.** The camera's settings file is only removed
  if it is still one of ours - you edit it every time you nudge the seat, and your file is yours.
  The list of "ours" had not kept up with the file we ship, so a clean uninstall left it in the
  game folder. It now recognises every version that has shipped, this one included.

## [1.2.0] - 2026-08-14

### Changed

- **Neither mod carries the game's own machine code any more.** Both the force feedback and the
  camera recognise your build by comparing a short run of the game's code; they now compare a
  checksum of it instead of a copy. A build that is not the one they were written for still refuses
  to be patched, exactly as before - nothing about the safety of the check has changed.
- The About box, the icons and the window's brand strip are the ones drawn on 2026-08-12.

### For developers

- The published source set now includes every header it needs to compile - nine were missing - and
  the release tool refuses to build an archive whose published set has an unresolved include.

## [1.1.1] - 2026-08-12

### Fixed

- **The gearbox switched itself to automatic while you were driving.** Getting into a car means
  the game is in automatic with no gear engaged, and the module recorded that as a debt - "he
  wants automatic, engage a gear and switch back". Nothing ever cancelled it. You then selected
  manual, and the debt paid itself off by pressing the mode key, putting you back in automatic
  out of a gear you had chosen yourself.
  The debt is now cancelled the moment you are in manual by your own choice, and it expires after
  three seconds in any case. A switch to automatic that you ask for still works exactly as before
  - the offline drives that cover it are green.

## [1.1.0] - 2026-08-12

### Changed

- **Gunfire feedback is off by default now, and that is a recommendation, not a limitation.** The
  wheel jolted for EVERY shot fired from your car - yours, an ally's in the passenger seat, an
  enemy's - and a jolt you did not cause reads as the wheel misbehaving rather than as a gunshot.
  The recommended value for that slider is 0, the mod ships with it silent, and the mark at 100
  is still on the slider so the old behaviour is one step away if you want it.
  The mod cannot yet tell whose shot it is. When it can, this becomes a real setting again.
- **The version is visible.** Bottom right of the window, on every tab. The archive refuses to
  build if that number and the release number disagree.

## [1.0.9] - 2026-08-12

### Fixed

- **The wheel row was blank until you pressed Refresh list**, even with the wheel switched on the
  whole time. Nothing was wrong with the device list: the settings load runs before the controls
  on the page exist, so the line that paints the wheel name had nothing to paint on and gave up
  silently. It is painted when the page is finished now.
- **A wheel is chosen for you.** Switching the mod on takes the first force-feedback device
  offered if you have not picked one yourself. That is what the mod already did; the box now says
  which device it is instead of "whichever Windows names first", and a wheel you picked yourself
  is never overridden.

### Changed

- The in-game settings toggle says what it is and stops explaining: **ON: recommended in-game FFB
  settings** / **OFF: your own in-game settings**.

## [1.0.8] - 2026-08-12

### Fixed

- The **Recommended in-game settings** toggle now actually turns green when it is on, like every
  other switched-on control in this window. It was drawing as an ordinary button whose own text
  said ON - a control that contradicts itself, which is the one thing this window is not allowed
  to have.

## [1.0.7] - 2026-08-12

### Added

- **The game's own settings are set to what the force feedback was tuned against.** Mafia's car
  handling, its built-in force feedback and two sound levels all live in your player profile,
  and every force in this mod was built and judged with them at particular values. They are
  applied when the mod is switched on, and there is a green **Recommended in-game settings**
  toggle at the top of the Force feedback tab that says so.
  Press it and you get **your own values back** - not GOG's factory ones. What was in your
  profile is recorded before anything is written, per profile, so a linearity you had set for a
  gamepad comes back exactly as you left it. Anything you changed yourself afterwards is left
  alone rather than overwritten.
  Your player name and your mission progress are never written to.

## [1.0.6] - 2026-08-12

### Fixed

- **The H-shifter gearbox did nothing, however you bound it.** The settings file shipped with the
  module switched off, and no control in the window could switch it back on - so you could bind
  every gear, bind the automatic/manual control, start the game, and the gearbox would load,
  read your wheel, and sit there. It ships switched on now.
  If you already installed an earlier version: open
  `<game>\ALXG mods\gearbox hshifter setup\gearbox.ini` and change `enable = 0` to `enable = 1`,
  or reinstall the H-shifter mod from its tab.
- The utility now warns in its status line whenever the gearbox settings file it just wrote has
  the module switched off, instead of leaving you to find out in the game.

## [1.0.5] - 2026-08-12

### Changed

- When you bind the gearbox A/M control, the utility decides after **one second** whether it is
  a latching switch or a springy button, instead of waiting three. It used to use one second if
  you let the control go and three if you kept holding it, so a switch flicked and held said
  nothing at all until you released it.

## [1.0.4] - 2026-08-12

### Changed

- **Refresh list** now sits between the wheel list and **Test - push the wheel**, which is the
  order the three are used in: this is the list, this is how you rebuild it, this is how you
  test what is in it.
- **Test - push the wheel** is greyed out while no force-feedback device is attached at all.
  It stays available when the list is populated but no particular wheel has been picked - that
  is the shipped default, "first one offered", and testing is the only way to find out which
  wheel that actually is.

## [1.0.3] - 2026-08-12

### Fixed

- **A wheel switched on after the window was already open now appears.** The Force feedback tab
  asked Windows for the list of force-feedback devices once, when the tab was built, so a wheel
  plugged in or powered up afterwards stayed invisible until the whole utility was closed and
  reopened. There is now a **Refresh list** button beside "Test - push the wheel", and it says
  in the status line how many devices it found before and after - so pressing it and finding
  nothing looks different from pressing a button that does nothing.
- The device list no longer just reads empty when there is nothing in it. It says what happened
  and what to do: *No device found - if your wheel is plugged in now, press Refresh list*.

## [1.0.2] - 2026-08-12

### Fixed

- **The interface is no longer stretched on a wide screen.** Mafia's interface was drawn for a
  4:3 screen and the game spreads it across the whole width, so on 16:9 every horizontal
  distance came out 1.3333 too wide and the round radar was an oval. It is now squeezed back
  about the centre of the screen, by a factor worked out from your resolution - 0.75 at both
  1920x1080 and 2560x1440, and 1.0 on a 4:3 screen, which needs nothing.
  This applies whenever the game draws, in a car or on foot, and it does not depend on the
  first-person camera being switched on.
  Full-screen draws - the menu background, loading screens, a fade to black - are deliberately
  left alone: squeezing one of those would put black bars down the sides rather than fix a shape.
  New key `hud_aspect_pct` in `mafia_fp.ini`: `-1` works it out (default), `0` turns it off and
  gives you exactly the old picture, `25..200` sets it by hand. Re-read while the game runs.

## [1.0.1] - 2026-08-11

Nothing a player controls has changed. 1.0.0 was built but never published; this supersedes it
rather than replacing it in place, so the earlier archive's checksums stay meaningful.

### Added

- The Ultimate ASI Loader's MIT license text, in full, as
  `licenses\Ultimate-ASI-Loader-LICENSE.txt`. 1.0.0 named the loader and linked upstream but did
  not carry the notice, which the license requires of a redistribution. The copyright year in it
  is 2018 - the year the shipped binary was built under - not the 2023 on upstream's current
  `master`.

### Changed

- Force feedback internal build v7.67 to **v7.70**. No force, threshold, envelope or detector
  moved; all 20 approved force constants are unchanged and checked mechanically. The difference
  is diagnostic logging, which is **off unless `diag = 1` is set by hand** and writes nothing at
  all in a normal install.

## [1.0.0] - 2026-08-07 - built, never published

First public release. One utility carrying four mods for the GOG release of Mafia v1.3
(build 16073); three of them ship, and VR is a placeholder that installs nothing.

### Added

- **Force feedback** (`mafia_ffb.asi`, internal build v7.67). A DirectInput8 force set driven
  from live car state: speed-dependent centering, a standstill damper, lightening under slip,
  curbs, tram rails, offroad roll, and impacts. Settings are re-read once a second, so changes
  apply without restarting the game.
- **First-person driving camera** (`mafia_fp.asi`). The camera in the driver's seat while
  driving, the stock camera everywhere else. Seat position, near clipping plane, horizon lock
  and camera aim are adjustable from the window and from keys while driving, and every change
  is written back to `mafia_fp.ini` immediately.
- **Field of view**, 70 to 86 degrees, applied when the first-person camera is switched on and
  adjustable on the same tab. Three four-byte floats inside `Game.exe`, same length; the
  original bytes are recorded and written back when the mod is switched off.
- **H-shifter gearbox** (`gearbox_hook.asi`). An H-pattern shifter read over DirectInput and
  mapped onto the game's own gear controls, with the game's own gear checked after every shift.
  The bound buttons are hidden from the game so they cannot also do something else.
- **ALXGtech Mafia 1 Mods.exe**, one window with a tab per mod. The switch on a tab is the install:
  there is no Save button and no Apply button. Every write is journalled to
  `<game>\ALXG mods\install.log` and uninstall works from that record, so a file you edited
  yourself is recognised, reported and left alone.
- A **VR** tab, marked coming soon. It installs nothing and says so; it is there so an empty
  space is not mistaken for an oversight.
- Manual install files, for anyone who would rather copy them by hand.

### Known issues

- A car ramming you from behind is barely felt.
- Being shot at is not felt through the wheel: the damage counter this build exposes is not
  usable.
- Camera keys are keyboard only; a wheel button cannot be bound to them yet.
- The field of view slider applies on the next start of the game, not live.
- The VR tab installs nothing.
