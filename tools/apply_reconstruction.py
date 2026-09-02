#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: apply_reconstruction.py <re3-tree>')

root = Path(sys.argv[1])
path = root / 'src' / 'animation' / 'CutsceneMgr.cpp'
text = path.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one match, found {count}')
    text = text.replace(old, new, 1)

replace_once(
    '\tms_cutsceneProcessing = true;\n',
    '\tprintf("PSP CUT 120 load begin name=%s\\n", szCutsceneName ? szCutsceneName : "<null>");\n\n\tms_cutsceneProcessing = true;\n',
    'cutscene load marker',
)

replace_once(
    '\t\tms_animLoaded = true;\n\t} else {\n\t\tms_animLoaded = false;\n\t}\n',
    '\t\tms_animLoaded = true;\n\t\tprintf("PSP CUT 121 anim loaded name=%s size=%u\\n", szCutsceneName, size);\n\t} else {\n\t\tms_animLoaded = false;\n\t\tprintf("PSP CUT 121E anim missing name=%s\\n", szCutsceneName);\n\t}\n',
    'animation load trace',
)

replace_once(
    '\t\tCFileMgr::Seek(file, offset << 11, SEEK_SET);\n\t\tTheCamera.LoadPathSplines(file);\n\t}\n',
    '\t\tCFileMgr::Seek(file, offset << 11, SEEK_SET);\n\t\tTheCamera.LoadPathSplines(file);\n\t\tprintf("PSP CUT 122 camera loaded name=%s size=%u\\n", szCutsceneName, size);\n\t}\n',
    'camera trace',
)

replace_once(
    '\t\tint trackId = FindCutsceneAudioTrackId(szCutsceneName);\n\t\tif (trackId != -1) {\n\t\t\tprintf("Start preload audio %s\\n", szCutsceneName);\n\t\t\tDMAudio.PreloadCutSceneMusic(trackId);\n\t\t\tprintf("End preload audio %s\\n", szCutsceneName);\n\t\t}\n',
    '\t\tint trackId = FindCutsceneAudioTrackId(szCutsceneName);\n\t\tprintf("PSP AUD 130 cutscene=%s track=%d\\n", szCutsceneName, trackId);\n\t\tif (trackId != -1) {\n\t\t\tprintf("PSP AUD 131 preload begin track=%d\\n", trackId);\n\t\t\tDMAudio.PreloadCutSceneMusic(trackId);\n\t\t\tprintf("PSP AUD 132 preload returned track=%d\\n", trackId);\n\t\t} else {\n\t\t\tprintf("PSP AUD 130E no mapped cutscene track\\n");\n\t\t}\n',
    'audio preload trace',
)

old_loop = '''\tfor (int i = ms_numCutsceneObjs - 1; i >= 0; i--) {\n\t\tassert(RwObjectGetType(ms_pCutsceneObjects[i]->m_rwObject) == rpCLUMP);\n\t\tif (CAnimBlendAssociation *pAnimBlendAssoc = RpAnimBlendClumpGetFirstAssociation((RpClump*)ms_pCutsceneObjects[i]->m_rwObject)) {\n\t\t\tassert(pAnimBlendAssoc->hierarchy->sequences[0].HasTranslation());\n\t\t\tms_pCutsceneObjects[i]->SetPosition(ms_cutsceneOffset + ((KeyFrameTrans*)pAnimBlendAssoc->hierarchy->sequences[0].GetKeyFrame(0))->translation);\n\t\t\tCWorld::Add(ms_pCutsceneObjects[i]);\n\t\t\tpAnimBlendAssoc->SetRun();\n\t\t} else {\n\t\t\tms_pCutsceneObjects[i]->SetPosition(ms_cutsceneOffset);\n\t\t}\n\t}\n'''

new_loop = '''\tprintf("PSP CUT 140 setup begin objects=%d\\n", ms_numCutsceneObjs);\n\n\tfor (int i = ms_numCutsceneObjs - 1; i >= 0; i--) {\n\t\tCCutsceneObject *obj = ms_pCutsceneObjects[i];\n\t\tif (obj == nil || obj->m_rwObject == nil) {\n\t\t\tprintf("PSP CUT 141E null object slot=%d\\n", i);\n\t\t\tcontinue;\n\t\t}\n\t\tif (RwObjectGetType(obj->m_rwObject) != rpCLUMP) {\n\t\t\tprintf("PSP CUT 142E non-clump slot=%d type=%d\\n", i, RwObjectGetType(obj->m_rwObject));\n\t\t\tcontinue;\n\t\t}\n\n\t\tCAnimBlendAssociation *pAnimBlendAssoc = RpAnimBlendClumpGetFirstAssociation((RpClump*)obj->m_rwObject);\n\t\tif (pAnimBlendAssoc) {\n\t\t\tif (pAnimBlendAssoc->hierarchy == nil || pAnimBlendAssoc->hierarchy->sequences == nil) {\n\t\t\t\tprintf("PSP CUT 143E bad hierarchy slot=%d\\n", i);\n\t\t\t\tcontinue;\n\t\t\t}\n\t\t\tif (pAnimBlendAssoc->hierarchy->sequences[0].HasTranslation()) {\n\t\t\t\tKeyFrameTrans *firstFrame = (KeyFrameTrans*)pAnimBlendAssoc->hierarchy->sequences[0].GetKeyFrame(0);\n\t\t\t\tif (firstFrame == nil) {\n\t\t\t\t\tprintf("PSP CUT 144E null first keyframe slot=%d\\n", i);\n\t\t\t\t\tcontinue;\n\t\t\t\t}\n\t\t\t\tobj->SetPosition(ms_cutsceneOffset + firstFrame->translation);\n\t\t\t} else {\n\t\t\t\tprintf("PSP CUT 144W no translation slot=%d\\n", i);\n\t\t\t\tobj->SetPosition(ms_cutsceneOffset);\n\t\t\t}\n\t\t\tCWorld::Add(obj);\n\t\t\tpAnimBlendAssoc->SetRun();\n\t\t} else {\n\t\t\tobj->SetPosition(ms_cutsceneOffset);\n\t\t}\n\t\tprintf("PSP CUT 145 object ready slot=%d\\n", i);\n\t}\n'''
replace_once(old_loop, new_loop, 'cutscene object safety loop')

replace_once(
    '\tms_running = true;\n\tms_cutsceneTimer = 0.0f;\n}\n',
    '\tms_running = true;\n\tms_cutsceneTimer = 0.0f;\n\tprintf("PSP CUT 146 setup complete\\n");\n}\n',
    'setup completion marker',
)

replace_once(
    '\tcase CUTSCENE_LOADING_AUDIO:\n\t\tSetupCutsceneToStart();\n\t\tif (CGeneral::faststricmp(ms_cutsceneName, "end"))\n\t\t\tDMAudio.PlayPreloadedCutSceneMusic();\n\t\tms_cutsceneLoadStatus++;\n\t\tbreak;\n',
    '\tcase CUTSCENE_LOADING_AUDIO:\n\t\tprintf("PSP CUT 150 loading-audio state name=%s\\n", ms_cutsceneName);\n\t\tSetupCutsceneToStart();\n\t\tif (CGeneral::faststricmp(ms_cutsceneName, "end")) {\n\t\t\tprintf("PSP AUD 151 play preloaded begin\\n");\n\t\t\tDMAudio.PlayPreloadedCutSceneMusic();\n\t\t\tprintf("PSP AUD 152 play preloaded returned\\n");\n\t\t}\n\t\tms_cutsceneLoadStatus++;\n\t\tbreak;\n',
    'audio playback trace',
)

path.write_text(text, encoding='utf-8')
print(f'[redch3psp] reconstructed source edits applied to {path}')
