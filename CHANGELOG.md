# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-07-24

First public release.

### Added

- H-pattern shifter to sequential gearbox translation for Mafia: The City of Lost
  Heaven, via vJoy virtual DirectInput device (`src/HShifter_to_vJoy.ahk`).
- Gears 1 to 6 on shifter buttons 1 to 6; reverse on button 8; hard reset to neutral
  on button 7 and on `F10`.
- Automatic neutral after a no-gear gap, with an asymmetric grace window: 600 ms after
  a forward gear, 1500 ms when leaving reverse.
- Reverse-aware automatic neutral: if the lever reaches reverse during the reset
  sequence, the mod stays in reverse instead of returning to neutral.
- Atomic shift sequences, so the 10 ms poller cannot interrupt a running sequence.
- `F9` hotkey reporting the internally tracked gear.
- Minimal vJoy diagnostic script (`src/test_vjoy_minimal.ahk`).
- Launcher that starts the script and the game together and stops the script when the
  game exits (`tools/Launch Mafia with Shifter.ps1`).
