#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <cstdio>
#include <unistd.h>

#include "common.h"
#include "skeleton.h"
#include "platform.h"
#include "Pad.h"
#include "ControllerConfig.h"
#include "FileMgr.h"
#include "PCSave.h"
#include "Frontend.h"
#include "Game.h"
#include "main.h"
#include "crossplatform.h"

PSP_MODULE_INFO("GTA III PSP re3", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);
// Match the proven 8E memory policy: keep only 1 MiB outside the user heap.
// The full engine needs the additional MiB during RenderWare/plugin startup.
PSP_HEAP_SIZE_KB(-1024);

long _dwOperatingSystemVersion = OS_WINXP;
RwUInt32 gGameState = GS_START_UP;
bool doEnvironmentMaps = false;
volatile int gPspBootStage = 0;
static volatile bool gPspSystemExitRequested = false;
static int gPspTraceLines = 0;

static void traceBoot(const char *message, bool reset = false)
{
    if(reset)
        gPspTraceLines = 0;
    // Diagnostics are boot breadcrumbs, not a per-frame logger. This cap
    // protects the real Memory Stick even if a downstream asset loops.
    if(!reset && gPspTraceLines >= 512)
        return;
    const int flags = PSP_O_WRONLY | PSP_O_CREAT |
        (reset ? PSP_O_TRUNC : PSP_O_APPEND);
    SceUID fd = sceIoOpen("ms0:/PSP/GAME/GTA3PSP/BOOT10C.TXT", flags, 0777);
    if(fd < 0)
        return;
    sceIoWrite(fd, message, std::strlen(message));
    sceIoWrite(fd, "\r\n", 2);
    sceIoClose(fd);
    gPspTraceLines++;
}

// Kept tiny and boot-only so lower RenderWare layers can mark their progress
// without introducing continuous Memory Stick writes.
void pspTraceBoot(const char *message)
{
    traceBoot(message);
}

void pspTraceMemory(const char *stage)
{
    char message[128];
    std::snprintf(message, sizeof(message), "MEM %s libre=%uKB bloque=%uKB",
        stage,
        (unsigned)(sceKernelTotalFreeMemSize() / 1024),
        (unsigned)(sceKernelMaxFreeMemSize() / 1024));
    traceBoot(message);
}

__attribute__((constructor(101))) static void traceModuleConstructor()
{
    traceBoot("C0 constructor ejecutado", true);
}

extern "C" const char *getExecutableTag() { return "gta3-psp-re3"; }
void CapturePad(RwInt32) {}

static int exitCallback(int, int, void *)
{
    traceBoot("X1 callback de salida recibido");
    gPspSystemExitRequested = true;
    RsGlobal.quit = TRUE;
    return 0;
}

static int callbackThread(SceSize, void *)
{
    int callback = sceKernelCreateCallback("Exit Callback", exitCallback, nullptr);
    sceKernelRegisterExitCallback(callback);
    sceKernelSleepThreadCB();
    return 0;
}

static void installExitCallback()
{
    int thread = sceKernelCreateThread("Callback Thread", callbackThread, 0x11, 0xFA0, 0, nullptr);
    if(thread >= 0)
        sceKernelStartThread(thread, 0, nullptr);
}

const char *_psGetUserFilesFolder()
{
    return "ms0:/PSP/GAME/GTA3PSP";
}

double psTimer(void)
{
    uint64_t tick = 0;
    sceRtcGetCurrentTick(&tick);
    return double(tick) * 1000.0 / double(sceRtcGetTickResolution());
}

RwBool psInitialize(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    CFileMgr::Initialise();
    CPad::Initialise();
    C_PcSave::SetSaveDirectory(_psGetUserFilesFolder());
    InitialiseLanguage();
    gGameState = GS_START_UP;
    RsGlobal.width = RsGlobal.maximumWidth = 480;
    RsGlobal.height = RsGlobal.maximumHeight = 272;
    return TRUE;
}

void psTerminate(void) {}
void psCameraShowRaster(RwCamera *camera) { RwCameraShowRaster(camera, nullptr, rwRASTERFLIPWAITVSYNC); }
RwBool psCameraBeginUpdate(RwCamera *camera) { return RwCameraBeginUpdate(camera) != nullptr; }
RwImage *psGrabScreen(RwCamera *camera) { return RwCameraGetRaster(camera)->toImage(); }
void psMouseSetPos(RwV2d *) {}
RwBool psSelectDevice(void) { return RwEngineSetVideoMode(0); }
RwMemoryFunctions *psGetMemoryFunctions(void) { return nullptr; }
RwBool psInstallFileSystem(void) { return TRUE; }
RwBool psNativeTextureSupport(void) { return TRUE; }
void _InputTranslateShiftKeyUpDown(RsKeyCodes *) {}
long _InputInitialiseMouse() { return 0; }
void _InputInitialiseJoys() {}
// Desktop/DC implementations use HandleExit while loading to pump platform
// events. It does not mean "quit the game". PSP already receives a real exit
// request through exitCallback(), so this function must not set RsGlobal.quit.
void HandleExit() {}
void _psSelectScreenVM(RwInt32) {}
void InitialiseLanguage() { CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_AMERICAN; }
RwBool _psSetVideoMode(RwInt32, RwInt32) { return TRUE; }
RwChar **_psGetVideoModeList() { return nullptr; }
RwInt32 _psGetNumVideModes() { return 1; }
RwBool IsForegroundApp() { return TRUE; }
void psPostRWinit() {}

static void showBootFailure(const char *message, int returnCode)
{
    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00101018);
    pspDebugScreenSetTextColor(0x00FFFFFF);
    pspDebugScreenClear();
    pspDebugScreenPrintf("GTA III PSP re3 - ARRANQUE DETENIDO\n\n");
    pspDebugScreenPrintf("%s\n", message);
    pspDebugScreenPrintf("Etapa interna: %d\n", int(gPspBootStage));
    pspDebugScreenPrintf("Codigo: PSP-%02d\n\n", returnCode);
    pspDebugScreenPrintf("No es un cierre de PPSSPP.\n");
    pspDebugScreenPrintf("Anota esta etapa para corregir el siguiente bloqueo.\n\n");
    pspDebugScreenPrintf("Pulsa CIRCULO para salir.\n");

    SceCtrlData pad{};
    do {
        sceCtrlReadBufferPositive(&pad, 1);
        sceDisplayWaitVblankStart();
    } while(!(pad.Buttons & PSP_CTRL_CIRCLE));
}

static void runGameState(void)
{
    switch(gGameState) {
    case GS_START_UP:
        gPspBootStage = 31;
        traceBoot("S31 GS_START_UP");
        // The PSP build has no MPEG player. Go straight into the real engine
        // bootstrap; the normal GTA loading screens are used from here on.
        gGameState = GS_INIT_ONCE;
        break;

    case GS_INIT_ONCE:
        gPspBootStage = 32;
        traceBoot("S32 antes de InitialiseOnceAfterRW");
        if(!CGame::InitialiseOnceAfterRW()) {
            traceBoot("E32 InitialiseOnceAfterRW devolvio false");
            RsGlobal.quit = TRUE;
            break;
        }
        traceBoot("S32 InitialiseOnceAfterRW termino");
        // The render and input paths now support the real GTA frontend. Keep
        // the world unloaded until the player chooses New Game/Load Game: this
        // is faster, avoids the original intro-on-boot path and leaves far more
        // memory available to the menu on real PSP hardware.
        gGameState = GS_INIT_FRONTEND;
        break;

    case GS_INIT_FRONTEND:
        gPspBootStage = 33;
        traceBoot("S33 GS_INIT_FRONTEND");
        LoadingScreen(nullptr, nullptr, "loadsc0");
        FrontEndMenuManager.m_bGameNotLoaded = true;
        FrontEndMenuManager.m_bWantToLoad = false;
        FrontEndMenuManager.m_bMenuActive = false;
        FrontEndMenuManager.m_nCurrScreen = MENUPAGE_NONE;
        CMenuManager::m_bStartUpFrontEndRequested = true;
        FrontEndMenuManager.m_nPrefsVideoMode = 0;
        FrontEndMenuManager.m_nDisplayVideoMode = 0;
        gGameState = GS_FRONTEND;
        break;

    case GS_FRONTEND:
        gPspBootStage = 34;
        {
            static bool tracedFrontend = false;
            if(!tracedFrontend) {
                traceBoot("S34 antes del primer FrontendIdle");
                tracedFrontend = true;
            }
        }
        RsEventHandler(rsFRONTENDIDLE, nullptr);
        if(FrontEndMenuManager.m_bWantToLoad) {
            InitialiseGame();
            FrontEndMenuManager.m_bGameNotLoaded = false;
            gGameState = GS_PLAYING_GAME;
        } else if(!FrontEndMenuManager.m_bMenuActive) {
            gGameState = GS_INIT_PLAYING_GAME;
        }
        break;

    case GS_INIT_PLAYING_GAME:
        gPspBootStage = 35;
        traceBoot("S35 antes de InitialiseGame");
        InitialiseGame();
        FrontEndMenuManager.m_bGameNotLoaded = false;
        gGameState = GS_PLAYING_GAME;
        break;

    case GS_PLAYING_GAME:
        gPspBootStage = 36;
        {
            static bool tracedPlaying = false;
            if(!tracedPlaying) {
                traceBoot("S36 primer frame de juego");
                tracedPlaying = true;
            }
        }
        RsEventHandler(rsIDLE, reinterpret_cast<void *>(TRUE));
        break;

    default:
        // Movie states are deliberately skipped on PSP.
        gGameState = GS_INIT_ONCE;
        break;
    }
}

int main(int argc, char **argv)
{
    gPspBootStage = 1;
    traceBoot("M1 main iniciado");
    installExitCallback();
    traceBoot("M2 callback instalado");
    if(argc > 0 && argv[0]) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s", argv[0]);
        char *slash = std::strrchr(path, '/');
        if(slash) { *slash = '\0'; chdir(path); }
    }
    traceBoot("M3 directorio de juego seleccionado");

    gPspBootStage = 2;
    if(RsEventHandler(rsINITIALIZE, nullptr) == rsEVENTERROR) {
        traceBoot("E1 rsINITIALIZE fallo");
        showBootFailure("Fallo la inicializacion base de re3.", 1);
        return 1;
    }
    traceBoot("M4 rsINITIALIZE correcto");
    ControlsManager.MakeControllerActionsBlank();
    ControlsManager.InitDefaultControlConfiguration();
    gPspBootStage = 3;
    if(RsEventHandler(rsRWINITIALIZE, nullptr) == rsEVENTERROR) {
        traceBoot("E2 rsRWINITIALIZE fallo");
        showBootFailure("Fallo RenderWare/PSP GU.", 2);
        RsEventHandler(rsTERMINATE, nullptr);
        return 2;
    }
    traceBoot("M5 rsRWINITIALIZE correcto");

    gPspBootStage = 30;
    psPostRWinit();
    RwRect rect{0, 0, 480, 272};
    RsEventHandler(rsCAMERASIZE, &rect);
    while(!RsGlobal.quit)
        runGameState();
    traceBoot("X2 bucle principal termino");
    if(!gPspSystemExitRequested)
        showBootFailure("El motor solicito terminar la ejecucion.", 3);
    RsEventHandler(rsRWTERMINATE, nullptr);
    traceBoot("X3 RenderWare terminado");
    RsEventHandler(rsTERMINATE, nullptr);
    traceBoot("X4 re3 terminado; llamando sceKernelExitGame");
    sceKernelExitGame();
    return 0;
}
