# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing has shipped yet. Seven test drives have settled the design and fixed four bugs;
an eighth drive re-checking every fix together on the real shifter has not been signed
off. See [Known issues](README.md#known-issues) in the README before using this.

### Added

- Closed-loop H-pattern shifter to sequential gearbox translation: reads the game's own
  current gear after every step and clamps to it, so a car with fewer gears than the
  lever has positions simply stops moving instead of desyncing (`src/gearbox_hook8.c`,
  `src/gearbox_logic.h`).
- Key injection through a patched DirectInput 8 `GetDeviceState`, so the mod presses the
  same keys the game itself has bound - no vJoy device, no AutoHotkey, `Game.exe`
  untouched.
- Sticky lever target: a multi-step move completes even if the source button is
  momentary.
- Refusal back-off (`retry_ms`): a rejected shift is retried after a delay instead of
  being latched as a permanent limit.
- Asymmetric neutral debounce (`neutral_delay_ms`): a gate is acted on instantly, the
  lever's rest position waits out the grace window before being read as neutral, and
  leaving reverse is exempt.
- Transmission-mode intent read from the game's own manual/automatic flag, not from
  which control moved it, so a keyboard mode change is respected exactly like a
  wheelbase button or switch.
- Multi-device binding: gears on one device, the mode control on another, independent
  suppress lists.
- `gearbox-setup.exe`: portable GUI binder. Click a row, press the button or key you
  want, click again to redo. Install, install-and-launch, and an off switch that renames
  the `.asi` so the next launch is completely stock.
- Offline test suite (`tests/test_gearbox.c`) simulating the shift logic against the
  shipped decision core.
