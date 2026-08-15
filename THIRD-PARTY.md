# Third-party components

This release bundles one component that is not ours.

## Ultimate ASI Loader

- Shipped as: `dinput8.dll` (2 152 448 bytes, md5 `b8c51891352e3e7bfc2c30f2c903b46c`)
- Author: ThirteenAG
- Home: https://github.com/ThirteenAG/Ultimate-ASI-Loader

It is a generic ASI plugin loader. Mafia's `LS3DF.dll` imports `DINPUT8.dll`, so Windows
resolves that import from the game folder before the system copy, the loader starts, and it
loads every `.asi` file next to it. Every mod in this release is one of those `.asi` files.
This project has not modified it in any way; it is redistributed exactly as it was obtained.

The loader is redistributed with attribution. Its own license terms are the upstream
project's, not this project's MIT license, and they apply to that file alone. They are
included in full as `licenses\Ultimate-ASI-Loader-LICENSE.txt`.

The copyright year in that notice is **2018**, not the 2023 on upstream's current `master`:
the copy shipped here has a PE link timestamp of 2019-07-24, so it was built under the 2018
notice, and the year that belongs with a binary is the one it was built under. The MIT terms
themselves are identical between the two.

Everything else in this archive is this project's own work.
