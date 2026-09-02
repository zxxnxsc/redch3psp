# First cinematic checkpoint (9Y -> 10A)

Known 9Y baseline from BOOT9Y.TXT:

- RenderWare, world, TXD and streaming initialise.
- PED.IFP reaches 236 animations.
- `CGame::Initialise` completes and the first gameplay frame is reached.
- The last marker before the reported crash is `S110 persecucion cinematica inicia`.
- Visible symptoms: global cyan/green tint, malformed/duplicated cutscene geometry, silent cinematic, then invalid memory access.

## What this branch changes first

`0001-cutscene1-safety-audio-trace.patch` deliberately does two things before we touch the PSP GU backend blindly:

1. hardens `CCutsceneMgr::SetupCutsceneToStart()` against null/bad cutscene objects, associations, hierarchies and first keyframes;
2. makes the complete cutscene-audio path observable.

New log markers:

- `PSP CUT 120` cutscene name enters loader
- `PSP CUT 121/121E` cutscene IFP present/missing
- `PSP CUT 122` camera spline data loaded
- `PSP AUD 130` resolved cutscene track id
- `PSP AUD 131/132` preload enters/returns
- `PSP CUT 140..146` per-object setup and completion
- `PSP AUD 151/152` preloaded cutscene playback enters/returns

## How to read the next boot log

- No `PSP AUD 130`: execution died before the audio path.
- `PSP AUD 130 ... track=-1`: the cinematic name is not mapped to a streamed cutscene track.
- `PSP AUD 131` but no `132`: crash/hang is inside preload/backend.
- `PSP AUD 132` and no `151`: cutscene state never advances to playback.
- `PSP AUD 152` but still silent: high-level GTA audio logic is working; the PSP streaming/output backend is the next target.
- `PSP CUT 141E/142E/143E/144E`: malformed cutscene object/animation data is being caught instead of dereferenced.

## Renderer work next

The color/geometry corruption is kept separate from the audio diagnosis. The next renderer patch must be based on the reconstructed PSP `librw` backend, specifically the code that selects GU vertex formats, packs vertex/material colors, uploads skinned vertices and submits `sceGuDrawArray`. Do not change TIMECYC to hide the tint: the 9Y log shows TIMECYC loaded successfully.
