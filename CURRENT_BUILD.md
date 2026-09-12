# Current build: HW24G FAT MEMORY

Last updated: **2026-09-12**

## Binary identity

The current tested executable supplied for this milestone is a valid PSP PBP containing a MIPS executable.

- File name: `EBOOT.PBP`
- Internal PSP title: `GTA III PSP HW24G FAT MEMORY`
- File size: `3,972,948 bytes`
- SHA-256: `253b3cf1b17c999649269f0ce1b0f84b70721fd1d36283e647268e7854f3ecc2`
- PBP version: `1.0`
- PSP `MEMSIZE`: enabled (`1`)

## Test status

### PPSSPP / emulator

Current project testing reports the HW24G build as playable in the PSP emulator. This supersedes the old 9Y-era documentation that stopped at rendering/cutscene diagnostics.

### Real PSP

Current hardware result: **not playable yet**.

The EBOOT starts, but the real PSP powers off when the game enters the loading process. This is the current primary blocker. Emulator success must not be treated as proof that memory allocation, PSP kernel/user memory behavior, or platform initialization is correct on physical hardware.

## Runtime data

Original GTA III assets are not distributed by this project. Testing has used the original PC game data beside the EBOOT, preserving the expected directory structure. At minimum, existing development logs confirm use of `DATA` and `MODELS`, including `DATA/GTA3.DAT`, map IPL/IDE data, textures/models and the GTA archive.

Recommended project folder:

```text
PSP/GAME/GTA3PSP/
├── EBOOT.PBP
├── DATA/
└── MODELS/
```

If a later build requires additional original GTA III directories, they must also come from the user's own legally obtained copy.

## Next hardware objective

Find the first divergence between PPSSPP and physical PSP during loading, with special attention to PSP memory usage/allocation and hardware-only initialization behavior. Historical project testing has already encountered PSP texture-memory exhaustion, so memory remains a high-priority suspect rather than assuming the shutdown is a renderer-only bug.
