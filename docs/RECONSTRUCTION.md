# Reconstruction plan

This repository starts from a recovered executable/log milestone rather than a preserved source tree.

## Baseline

Reference upstream: `SugaryHull/re3` (`main`).

Recovered PSP build 9Y behavior is the compatibility target for the first reconstruction pass. The first objective is not new features; it is to reproduce the same initialization milestone from source, then fix rendering and the intro crash.

## Reconstruction order

### Phase A — reproducible PSP bootstrap

Recreate the PSP target/build wiring and enough platform code to enter the game main loop. Add diagnostic markers matching the recovered 9Y naming where useful.

### Phase B — native file I/O and asset loading

Restore PSP-native paths used by the working build, especially large archive/streaming reads and `PED.IFP` loading. Success criterion: 236 PED animations and initial streaming complete.

### Phase C — renderer correctness

Audit PSP GU vertex declarations and every CPU-to-GU packing conversion. Specifically verify:

- color byte/channel order (RGBA vs ABGR),
- vertex stride and alignment,
- indexed vs non-indexed draw paths,
- UV format,
- normal format,
- matrix upload order,
- skin weights and bone indices,
- temporary/transformed vertex buffer lifetime,
- cache writeback/invalidation before GU consumption.

### Phase D — cutscene crash

Instrument `CutsceneMgr`, `CutsceneObject`, vehicle rendering and skinned atomic rendering around the first intro chase. Add markers before/after animation update, matrix update, skin transform, atomic render and temporary buffer submission.

### Phase E — optimization

Only after visual correctness and stability: reduce draw distance, effects, particle load, LOD thresholds, streaming pressure and other PSP costs.

## Known diagnostic target

The rebuild should reach, in order:

`CGame Initialise completo` -> `primer frame de juego` -> `persecucion cinematica inicia`

without introducing regressions in data/animation loading.
