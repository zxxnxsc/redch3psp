# PSP port status

## Recovered 9Y behavior

The recovered diagnostic log proves the following sequence completes successfully:

- `rsINITIALIZE`
- `RwEngineInit`
- filesystem/debug installation
- GTA RenderWare plugins
- `RwEngineOpen`
- video mode selection
- `RwEngineStart`
- GTA RenderWare initialization
- GXT text loading
- logical audio initialization
- handling/surface/pedstats loading
- `TIMECYC.DAT` (96 time cycles)
- `DEFAULT.DAT`
- `GTA3.DAT`
- water/world/streaming initialization
- native `PED.IFP` open
- vanilla PED IFP: 236 animations
- initial vehicle/ped/LOD streaming
- scripts/gangs/world systems
- `CGame::Initialise` completion
- first game frame

Last recovered marker before failure:

`S110 persecucion cinematica inicia`

## Visual failure signatures

Observed in PPSSPP:

- Global cyan/green tint affecting world, peds, vehicles and backgrounds.
- Cutscene geometry corruption.
- Repeated/duplicated ped geometry.
- Detached/misaligned vehicle wheel geometry.
- Stretched polygons and invalid-looking surfaces.
- Some frames render as mostly flat cyan/green.

These symptoms currently point first to PSP renderer state/data interpretation rather than GTA asset loading, because the asset/data initialization path completes and the failures become prominent while rendering the intro.

## Crash signature

PPSSPP reports `Invalid Memory Access` after the chase cutscene starts. The shown PC belongs to PPSSPP JIT-generated code, so it is not treated as a source-level re3 address. The rebuild needs explicit source-side markers around cutscene processing/rendering to isolate the bad access.

## Rule for future work

Do not optimize draw distance or visual quality until renderer correctness and cutscene stability are restored. Keep each subsystem fix in a separate commit.
