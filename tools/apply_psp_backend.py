#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: apply_psp_backend.py <re3-tree>')

root = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parent.parent


def patch(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding='utf-8')
    n = text.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected one match in {path}, found {n}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')
    print(f'[redch3psp] {label}')

pspdir = root / 'vendor' / 'librw' / 'src' / 'psp'
pspdir.mkdir(parents=True, exist_ok=True)
shutil.copy2(repo / 'platform' / 'psp' / 'rwpsp.h', pspdir / 'rwpsp.h')
shutil.copy2(repo / 'platform' / 'psp' / 'psp.cpp', pspdir / 'psp.cpp')

rwbase = root / 'vendor' / 'librw' / 'src' / 'rwbase.h'
patch(
    rwbase,
    '#ifdef RW_PS2\n#define RWHALFPIXEL\n#define RWDEVICE ps2\n#endif\n',
    '#ifdef RW_PS2\n#define RWHALFPIXEL\n#define RWDEVICE ps2\n#endif\n\n#ifdef RW_PSP\n#define RWDEVICE psp\n#endif\n',
    'rwbase: select PSP device',
)
patch(
    rwbase,
    '\tPLATFORM_D3D9 = 9,\n\t// PSP\n\n\t// non-stock-RW platforms\n',
    '\tPLATFORM_D3D9 = 9,\n\tPLATFORM_PSP  = 10,\n\n\t// non-stock-RW platforms\n',
    'rwbase: register PSP platform id 10',
)
patch(
    rwbase,
    '\tID_RASTERD3D9    = MAKEPLUGINID(VEND_RASTER, PLATFORM_D3D9),\n\tID_RASTERWDGL    = MAKEPLUGINID(VEND_RASTER, PLATFORM_WDGL),\n',
    '\tID_RASTERD3D9    = MAKEPLUGINID(VEND_RASTER, PLATFORM_D3D9),\n\tID_RASTERPSP     = MAKEPLUGINID(VEND_RASTER, PLATFORM_PSP),\n\tID_RASTERWDGL    = MAKEPLUGINID(VEND_RASTER, PLATFORM_WDGL),\n',
    'rwbase: add PSP raster plugin id',
)

rwh = root / 'vendor' / 'librw' / 'rw.h'
patch(
    rwh,
    '#include "src/ps2/rwps2plg.h"\n',
    '#include "src/ps2/rwps2plg.h"\n#include "src/psp/rwpsp.h"\n',
    'rw.h: expose PSP backend',
)

engine = root / 'vendor' / 'librw' / 'src' / 'engine.cpp'
patch(
    engine,
    '#include "ps2/rwps2.h"\n',
    '#include "ps2/rwps2.h"\n#include "psp/rwpsp.h"\n',
    'engine: include PSP backend',
)
patch(
    engine,
    '\tps2::registerPlatformPlugins();\n\txbox::registerPlatformPlugins();\n',
    '\tps2::registerPlatformPlugins();\n\tpsp::registerPlatformPlugins();\n\txbox::registerPlatformPlugins();\n',
    'engine: register PSP driver plugin',
)
patch(
    engine,
    '#ifdef RW_PS2\n\tengine->device = ps2::renderdevice;\n#elif RW_GL3\n',
    '#ifdef RW_PSP\n\tengine->device = psp::renderdevice;\n#elif defined RW_PS2\n\tengine->device = ps2::renderdevice;\n#elif RW_GL3\n',
    'engine: select PSP render device',
)

controller = root / 'src' / 'core' / 'ControllerConfig.h'
patch(
    controller,
    '#ifdef RW_GL3\nstruct GlfwJoyState {\n\tint8 id;\n\tbool isGamepad;\n\tuint8 numButtons;\n\tuint8* buttons;\n\tbool mappedButtons[17];\n};\n#endif\n',
    '#ifdef RW_GL3\nstruct GlfwJoyState {\n\tint8 id;\n\tbool isGamepad;\n\tuint8 numButtons;\n\tuint8* buttons;\n\tbool mappedButtons[17];\n};\n#endif\n\n#ifdef PSP\nstruct PspJoyState {\n\tuint8 rgbButtons[32];\n};\n#endif\n',
    'controller: add PSP joy state',
)
patch(
    controller,
    '#if defined RW_GL3\n\tGlfwJoyState           m_OldState;\n\tGlfwJoyState           m_NewState;\n#else\n\tDIJOYSTATE2           m_OldState;\n\tDIJOYSTATE2           m_NewState;\n#endif\n',
    '#if defined RW_GL3\n\tGlfwJoyState           m_OldState;\n\tGlfwJoyState           m_NewState;\n#elif defined PSP\n\tPspJoyState            m_OldState;\n\tPspJoyState            m_NewState;\n#else\n\tDIJOYSTATE2            m_OldState;\n\tDIJOYSTATE2            m_NewState;\n#endif\n',
    'controller: select PSP joy state',
)

# GCC for the PSP target treats the table fields as a different integral type
# from re3's int32 typedef. Make the intended integer overload explicit.
pedchat = root / 'src' / 'peds' / 'PedChat.cpp'
patch(
    pedchat,
    'CGeneral::GetRandomNumberInRange(0, CommentWaitTime[m_queuedSound - SOUND_PED_DEATH].m_nOverrideFixedDelayTime)',
    'CGeneral::GetRandomNumberInRange((int32)0, (int32)CommentWaitTime[m_queuedSound - SOUND_PED_DEATH].m_nOverrideFixedDelayTime)',
    'ped chat: disambiguate fixed-delay RNG',
)
patch(
    pedchat,
    'CGeneral::GetRandomNumberInRange(0, CommentWaitTime[audio - SOUND_PED_DEATH].m_nMaxRandomDelayTime)',
    'CGeneral::GetRandomNumberInRange((int32)0, (int32)CommentWaitTime[audio - SOUND_PED_DEATH].m_nMaxRandomDelayTime)',
    'ped chat: disambiguate max-delay RNG',
)

print('[redch3psp] PSP backend injected')
