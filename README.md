# redch3psp

Reconstruction and continuation of the experimental GTA III/re3 PSP port.

Current known-good milestone from build 9Y:
- RenderWare PSP initialization completes.
- GTA data files and TXDs load.
- PED.IFP loads 236 vanilla animations.
- Streaming reaches the first real game frame.
- Intro/cutscene begins.
- Remaining blockers: PSP renderer color packing, corrupted geometry/skinning during the intro, and an invalid-memory-access crash after the chase cutscene begins.

## Project goal

Produce a reproducible PSP build from source instead of continuing with isolated EBOOT binaries. Every meaningful fix should be committed separately so regressions can be bisected and reverted.

## Upstream reference

The reconstruction is based on the public SugaryHull/re3 tree plus the behavior and diagnostics recovered from the 9Y PSP build.

## Immediate priorities

1. Reconstruct PSP platform/bootstrap code.
2. Reconstruct native PSP I/O used by the known-good build.
3. Reconstruct the PSP RenderWare backend.
4. Fix RGBA/ABGR color packing.
5. Validate vertex layout/stride/index handling.
6. Validate skinned ped/cutscene rendering.
7. Instrument and fix the crash immediately after `S110 persecucion cinematica inicia`.
8. Optimize for real PSP hardware only after correctness is restored.
