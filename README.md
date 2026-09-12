# GTA III PSP — redch3psp

Experimental GTA III / re3 port and reconstruction work for Sony PSP.

> **This repository does not include GTA III game assets.** You must provide your own legally obtained GTA III PC data files.

## Current milestone — HW24G FAT MEMORY

Snapshot date: **2026-09-12**

The current tested EBOOT identifies itself as:

`GTA III PSP HW24G FAT MEMORY`

### Compatibility

| Target | Current status |
| --- | --- |
| **PPSSPP / PSP emulator** | ✅ Playable in current project testing |
| **Real PSP hardware** | ❌ Not yet stable — the console powers off when the game begins the loading process |

The important distinction is that the current build is now usable in the emulator, but **it is not yet a real-PSP release**. The next major target is finding the hardware-only shutdown during loading.

### Current EBOOT identity

- File: `EBOOT.PBP`
- Size: `3,972,948 bytes`
- SHA-256: `253b3cf1b17c999649269f0ce1b0f84b70721fd1d36283e647268e7854f3ecc2`
- Internal title: `GTA III PSP HW24G FAT MEMORY`

See [`CURRENT_BUILD.md`](CURRENT_BUILD.md) for the exact status and test notes.

## Running the port

Use a folder such as:

```text
PSP/GAME/GTA3PSP/
├── EBOOT.PBP
├── DATA/
└── MODELS/
```

Keep the original GTA III directory/file names expected by the port. Development testing has specifically used the game's `DATA` and `MODELS` content, including `DATA/GTA3.DAT` and the original model/archive files.

Only the port executable and project code belong in this repository. Rockstar game data is not redistributed here.

## Building / developing

This repository keeps the reproducible source-reconstruction and PSPSDK tooling instead of treating old isolated binaries as source code.

Read [`BUILDING.md`](BUILDING.md) before compiling.

In short:

1. Install Git, Python 3 and the PSPDEV/PSPSDK toolchain (or use the `pspdev/pspdev` container used by GitHub Actions).
2. Reconstruct the pinned upstream tree with `tools/bootstrap-upstream.sh` on Linux/macOS or `tools/bootstrap-upstream.ps1` on Windows.
3. Use `ci/psp-smoke` to verify that your PSPSDK can produce a valid PSP `EBOOT.PBP`.
4. The exact **HW24G FAT MEMORY** executable cannot yet be reproduced byte-for-byte from this public repository because its complete current PSP backend/build target is not present here. Do not confuse the reconstruction tooling with an exact-source release of HW24G.

That missing exact-build path is now documented explicitly rather than hidden behind old milestone notes.

## Project lineage

The reconstruction work is based on the public `SugaryHull/re3` tree pinned by the bootstrap scripts, plus PSP-specific work and diagnostics developed for this project.

## Current priority

**Make the emulator-playable HW24G build survive the same loading sequence on real PSP hardware.**

Until that is fixed, PPSSPP is the primary reproducible test target and real PSP testing is the hardware validation target.
