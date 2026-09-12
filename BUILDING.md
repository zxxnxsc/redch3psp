# Building redch3psp

This document separates three different things that were previously easy to confuse:

1. the current tested **HW24G FAT MEMORY** EBOOT,
2. the reproducible upstream source reconstruction in this repository,
3. a complete game build from PSP-specific source.

## 1. Requirements

For source/reconstruction work you need:

- Git
- Python 3
- PSPDEV / PSPSDK for PSP compilation

GitHub Actions uses the `pspdev/pspdev:latest` container, which is also the simplest reference environment for Linux-based builds.

## 2. Reconstruct the pinned re3 source

### Linux / macOS

```bash
chmod +x tools/bootstrap-upstream.sh
./tools/bootstrap-upstream.sh
```

### Windows PowerShell

```powershell
powershell -ExecutionPolicy Bypass -File tools/bootstrap-upstream.ps1
```

The scripts clone the pinned public `SugaryHull/re3` revision and apply the deterministic reconstruction edits in `tools/apply_reconstruction.py`.

The reconstructed source appears under:

```text
work/re3/
```

Do not commit `work/`; it is generated content.

## 3. Verify your PSPSDK installation

The repository contains a very small PSP packaging test under `ci/psp-smoke`.

With PSPSDK configured:

```bash
make -C ci/psp-smoke clean
make -C ci/psp-smoke
```

A successful test produces:

```text
ci/psp-smoke/EBOOT.PBP
```

This is only a toolchain test. It is **not GTA III**.

## 4. Building the game

The GitHub workflow already knows how to use a game target when one of these exists:

```text
platform/psp/Makefile
Makefile.psp
```

The current public repository does **not** contain the complete PSP backend/build target that produced the supplied `GTA III PSP HW24G FAT MEMORY` binary. Because of that, nobody should claim that cloning this repository alone currently reproduces the exact HW24G EBOOT.

When the complete PSP target is restored/committed, the intended Linux/CI form is:

```bash
make -f platform/psp/Makefile -j2 ROOT="$PWD" SOURCE_TREE="$PWD/work/re3"
```

or, for a root PSP makefile:

```bash
make -f Makefile.psp -j2 SOURCE_TREE="$PWD/work/re3"
```

The output should be a PSP `EBOOT.PBP`.

## 5. Game data is not part of the build

Do not add copyrighted GTA III data to this repository.

Users must supply their own legally obtained GTA III PC files. Runtime testing has used the game's `DATA` and `MODELS` directories beside the EBOOT while preserving their original names/structure.

Typical test layout:

```text
PSP/GAME/GTA3PSP/
├── EBOOT.PBP
├── DATA/
└── MODELS/
```

## 6. Current compatibility target

The current HW24G snapshot is reported playable in PPSSPP, but physical PSP hardware currently powers off as loading begins. A build is therefore not considered hardware-ready merely because it works in PPSSPP.

For new changes, test in this order:

1. build/package successfully with PSPSDK,
2. boot and play in PPSSPP,
3. validate on real PSP hardware,
4. record the first point where hardware behavior differs from PPSSPP.
