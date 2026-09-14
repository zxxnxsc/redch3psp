#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <psprtc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "common.h"
#include "rwcore.h"
#include "skeleton.h"
#include "platform.h"
#include "main.h"
#include "FileMgr.h"
#include "Game.h"
#include "Pad.h"
#include "Frontend.h"
#include "ControllerConfig.h"
#include "Text.h"

PSP_MODULE_INFO("GTA III PSP re3", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(24 * 1024);

static volatile bool g_exitRequested = false;
static unsigned int g_tickResolution = 1000000;

static int exitCallback(int, int, void*)
{
    g_exitRequested = true;
    RsGlobal.quit = TRUE;
    return 0;
}

static int callbackThread(SceSize, void*)
{
    const int cb = sceKernelCreateCallback("redch3psp_exit", exitCallback, NULL);
    if (cb >= 0)
        sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static void setupCallbacks()
{
    const int th = sceKernelCreateThread("redch3psp_callbacks", callbackThread,
                                         0x11, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
}

static void openBootLog()
{
    std::freopen("BOOT10A.TXT", "w", stdout);
    std::freopen("BOOT10A.TXT", "a", stderr);
    std::setvbuf(stdout, NULL, _IOLBF, 0);
    std::setvbuf(stderr, NULL, _IOLBF, 0);
}

static inline int16 axisFromByte(unsigned char v)
{
    int x = int(v) - 128;
    if (x > -14 && x < 14)
        return 0;
    x *= 256;
    if (x < -32767) x = -32767;
    if (x >  32767) x =  32767;
    return (int16)x;
}

static void pollPspPad()
{
    SceCtrlData c;
    std::memset(&c, 0, sizeof(c));
    sceCtrlPeekBufferPositive(&c, 1);

    CControllerState &s = CPad::GetPad(0)->PCTempJoyState;
    s.Clear();
    s.LeftStickX = axisFromByte(c.Lx);
    s.LeftStickY = axisFromByte(c.Ly);
    s.DPadUp    = (c.Buttons & PSP_CTRL_UP)    ? 255 : 0;
    s.DPadDown  = (c.Buttons & PSP_CTRL_DOWN)  ? 255 : 0;
    s.DPadLeft  = (c.Buttons & PSP_CTRL_LEFT)  ? 255 : 0;
    s.DPadRight = (c.Buttons & PSP_CTRL_RIGHT) ? 255 : 0;
    s.Start     = (c.Buttons & PSP_CTRL_START)  ? 255 : 0;
    s.Select    = (c.Buttons & PSP_CTRL_SELECT) ? 255 : 0;
    s.Square    = (c.Buttons & PSP_CTRL_SQUARE)   ? 255 : 0;
    s.Triangle  = (c.Buttons & PSP_CTRL_TRIANGLE) ? 255 : 0;
    s.Cross     = (c.Buttons & PSP_CTRL_CROSS)    ? 255 : 0;
    s.Circle    = (c.Buttons & PSP_CTRL_CIRCLE)   ? 255 : 0;
    s.LeftShoulder1  = (c.Buttons & PSP_CTRL_LTRIGGER) ? 255 : 0;
    s.RightShoulder1 = (c.Buttons & PSP_CTRL_RTRIGGER) ? 255 : 0;
}

extern "C" double psTimer(void)
{
    u64 tick = 0;
    sceRtcGetCurrentTick(&tick);
    if (g_tickResolution == 0)
        g_tickResolution = sceRtcGetTickResolution();
    return double(tick) * 1000.0 / double(g_tickResolution);
}

extern "C" RwBool psInitialize(void)
{
    RsGlobal.ps = NULL;
    RsGlobal.maximumWidth = 480;
    RsGlobal.maximumHeight = 272;
    RsGlobal.width = 480;
    RsGlobal.height = 272;
    RsGlobal.maxFPS = 30;

    CFileMgr::Initialise();
    gGameState = GS_START_UP;
    TheText.Unload();
    std::printf("S4 rs inicializado 480x272\n");
    return TRUE;
}

extern "C" void psTerminate(void) {}

extern "C" RwBool psInstallFileSystem(void) { return TRUE; }
extern "C" RwBool psNativeTextureSupport(void) { return TRUE; }
extern "C" RwMemoryFunctions *psGetMemoryFunctions(void) { return NULL; }

extern "C" RwBool psCameraBeginUpdate(RwCamera *camera)
{
    return RwCameraBeginUpdate(camera);
}

extern "C" void psCameraShowRaster(RwCamera *camera)
{
    RwCameraShowRaster(camera, NULL, rwRASTERFLIPWAITVSYNC);
}

extern "C" RwImage *psGrabScreen(RwCamera*) { return NULL; }
extern "C" void psMouseSetPos(RwV2d*) {}
extern "C" void _InputTranslateShiftKeyUpDown(RsKeyCodes*) {}
extern "C" long _InputInitialiseMouse() { return 0; }
extern "C" void _InputInitialiseJoys() {}
extern "C" void HandleExit() {}

extern "C" RwBool psSelectDevice()
{
    const int n = RwEngineGetNumVideoModes();
    for (int i = 0; i < n; i++) {
        RwVideoMode vm;
        if (RwEngineGetVideoModeInfo(&vm, i) && vm.width == 480 && vm.height == 272)
            return RwEngineSetVideoMode(i);
    }
    return n > 0 ? RwEngineSetVideoMode(0) : TRUE;
}

extern "C" void _psSelectScreenVM(RwInt32 videoMode)
{
    RwEngineSetVideoMode(videoMode);
}

extern "C" RwBool _psSetVideoMode(RwInt32, RwInt32 videoMode)
{
    return RwEngineSetVideoMode(videoMode);
}

extern "C" RwInt32 _psGetNumVideModes() { return RwEngineGetNumVideoModes(); }
extern "C" RwChar **_psGetVideoModeList() { return NULL; }

extern "C" void InitialiseLanguage()
{
    CGame::frenchGame = false;
    CGame::germanGame = false;
    CGame::nastyGame = true;
    CMenuManager::m_PrefsAllowNastyGame = true;
    CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_AMERICAN;
    TheText.Unload();
    TheText.Load();
}

int main(int, char**)
{
    setupCallbacks();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    chdir("ms0:/PSP/GAME/GTA3PSP");
    openBootLog();
    std::printf("S0 constructor PSP entra\n");
    std::printf("S1 main PSP entra\n");
    std::printf("S2 cwd GTA3PSP listo\n");

    if (RsEventHandler(rsINITIALIZE, NULL) == rsEVENTERROR) {
        std::printf("E1 rsINITIALIZE fallo\n");
        sceKernelExitGame();
        return 1;
    }

    std::printf("S5 antes de RwInit\n");
    if (RsEventHandler(rsRWINITIALIZE, NULL) == rsEVENTERROR) {
        std::printf("E2 rsRWINITIALIZE fallo\n");
        RsEventHandler(rsTERMINATE, NULL);
        sceKernelExitGame();
        return 2;
    }

    std::printf("S29 despues de RwInit\n");
    ControlsManager.MakeControllerActionsBlank();
    ControlsManager.InitDefaultControlConfiguration();
    ControlsManager.InitDefaultControlConfigJoyPad(16);

    std::printf("S32 antes de InitialiseOnceAfterRW\n");
    if (!CGame::InitialiseOnceAfterRW()) {
        std::printf("E3 InitialiseOnceAfterRW fallo\n");
        RsEventHandler(rsRWTERMINATE, NULL);
        RsEventHandler(rsTERMINATE, NULL);
        sceKernelExitGame();
        return 3;
    }

    std::printf("S35 antes de InitialiseGame\n");
    InitialiseGame();
    std::printf("S36 primer frame de juego\n");

    while (!g_exitRequested && !RsGlobal.quit) {
        pollPspPad();
        RsEventHandler(rsIDLE, reinterpret_cast<void*>(1));
        sceDisplayWaitVblankStart();
    }

    std::printf("S99 salida\n");
    CGame::ShutDown();
    RsEventHandler(rsRWTERMINATE, NULL);
    RsEventHandler(rsTERMINATE, NULL);
    sceKernelExitGame();
    return 0;
}
