# re3 PSP port — 10C source tree

This branch contains the complete source changes used to build **GTA III PSP
re3 Full Engine 10C STABLE INDEXED**. It is based on the `dca3-game` re3/librw
tree and targets PSP-2000/3000 class hardware through PSPSDK.

This repository intentionally does **not** include Grand Theft Auto III game
data, an EBOOT, ELFs, PPSSPP logs, or intermediate build products. A legitimate
PC copy of GTA III is required to supply the runtime data.

## Current state

- Full re3 engine links as a PSP user module.
- Native PSP entry point, controller input, display and exit handling.
- PSP file and asynchronous CD streaming backends.
- PSP GU RenderWare backend with 16-bit indexed geometry.
- Separate texture-coordinate handling for normalized 3D geometry and pixel
  coordinates used by 2D sprites and the frontend.
- D3D8 TXD conversion path with guarded PSP texture allocation.
- PCM effects mixer with a bounded 12-channel / 128 KiB-per-sample budget.
- PSP-specific memory, population, vehicle, LOD and far-clip limits.
- Frontend-first boot and PSP-style Cross/Circle controls.
- Boot diagnostics written to `ms0:/PSP/GAME/GTA3PSP/BOOT10C.TXT`, capped at
  512 lines and including free/largest-block memory checkpoints.

The port remains experimental. Compressed radio, music and dialogue playback
is not complete, and runtime correctness depends on the supplied game-data
edition and layout.

## Source map

| Area | Main files |
| --- | --- |
| PSP platform entry/input | `src/liberty/skel/psp/psp.cpp` |
| PSP compatibility headers | `src/liberty/skel/psp/` |
| File streaming | `src/liberty/core/CdStreamPsp.cpp` |
| Audio backend | `src/liberty/audio/sampman_psp.cpp` |
| PSP GU / indexed renderer | `vendor/librw/src/psp/psp.cpp` |
| Renderer declarations | `vendor/librw/src/psp/rwpsp.h` |
| TXD loading/conversion | `src/liberty/rw/TexRead.cpp`, `vendor/librw/src/texture.cpp` |
| Frontend and controls | `src/liberty/core/Frontend.cpp`, `src/liberty/core/Pad.cpp` |
| Memory/load profile | `src/liberty/core/config.h`, `src/liberty/core/Game.cpp`, `src/liberty/core/Streaming.cpp` |
| PSP build | `liberty/Makefile.psp` |

## Build requirements

1. Install a current PSPSDK/PSPDEV toolchain.
2. Clone this repository with its submodules.
3. Set `PSPDEV` if the toolchain is not installed in `/usr/local/pspdev`.
4. Build from the `liberty` directory.

```sh
git submodule update --init --recursive
cd liberty
PSPDEV=/path/to/pspdev make -f Makefile.psp -j4 eboot
```

The output is:

```text
liberty/build-psp/package/GTA3PSP/EBOOT.PBP
```

The generated `PARAM.SFO` enables 64 MiB memory mode (`MEMSIZE=2`).

## Runtime layout

Install the EBOOT as:

```text
ms0:/PSP/GAME/GTA3PSP/EBOOT.PBP
```

Place the legally obtained GTA III data directories/files alongside it using
the layout expected by the original re3 project. Do not load savestates made
with earlier experimental PSP builds when validating a new build.

## 10C renderer and memory changes

Earlier PSP experiments expanded triangle strips into duplicated, non-indexed
triangle vertices. That made the real render heap substantially larger than
the streaming budget reported. 10C remaps each mesh to compact vertices and a
16-bit index list, while preserving strip winding and rejecting degenerate
triangles.

10C also removes the experimental global 128-pixel texture cap. World geometry
uses normalized UVs with texture scale 1:1; 2D RenderWare vertices convert their
normalized UVs to raster pixel coordinates because PSP GU ignores texture scale
for `GU_TRANSFORM_2D` draws.

The default PSP profile uses 12 ambient pedestrians, 8 ambient cars, at most 16
loaded vehicle models, LOD scale 0.40, far clip 150 and fog start 110. These are
deliberately conservative starting values for PSP-2000 memory and can be tuned
after stable gameplay is established.

## Verification used for 10C

The complete target was rebuilt with `make -f Makefile.psp -j4 eboot`, the ELF
contained zero unresolved symbols according to `psp-nm -u`, and the generated
PBP contained the expected 10C title and diagnostic-log path. Final runtime
testing still requires the external GTA III data and either real PSP hardware
or PPSSPP.
