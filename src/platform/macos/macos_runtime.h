#pragma once

#include <cstdint>

#if defined(_WIN32)
#define TOOLSCREEN_MACOS_RUNTIME_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define TOOLSCREEN_MACOS_RUNTIME_EXPORT __attribute__((visibility("default")))
#else
#define TOOLSCREEN_MACOS_RUNTIME_EXPORT
#endif

extern "C" {

struct ToolscreenMacRuntimeFrameSnapshot {
    std::uint64_t frameCount = 0;
    std::uintptr_t contextAddress = 0;
    int width = 0;
    int height = 0;
    int resolvedHalfWidth = 0;
    int initialized = 0;
};

struct ToolscreenMacRuntimeNinjabrainSnapshot {
    int running = 0;
    int connected = 0;
    int offline = 0;
    int validPrediction = 0;
    int eyeCount = 0;
    int strongholdX = 0;
    int strongholdZ = 0;
    double distance = 0.0;
    double lastAngle = 0.0;
    int hasBoatAngle = 0;
    double boatAngle = 0.0;
    int informationMessageCount = 0;
    int blindEnabled = 0;
    int blindHasResult = 0;
    double blindNetherX = 0.0;
    double blindNetherZ = 0.0;
    char resultType[32] = {};
    char boatState[32] = {};
};

TOOLSCREEN_MACOS_RUNTIME_EXPORT const char* ToolscreenMacRuntimeVersion();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeIsInitialized();
TOOLSCREEN_MACOS_RUNTIME_EXPORT const char* ToolscreenMacRuntimeLogPath();
TOOLSCREEN_MACOS_RUNTIME_EXPORT void ToolscreenMacRuntimeAppendLog(const char* message);
TOOLSCREEN_MACOS_RUNTIME_EXPORT const char* ToolscreenMacRuntimeNormalizeNinjabrainApiBaseUrl(const char* apiBaseUrl);
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeResolveHalfWidth(int screenWidth);
TOOLSCREEN_MACOS_RUNTIME_EXPORT void ToolscreenMacRuntimeResetFrameState();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeOnOpenGLPresent(void* context, int width, int height);
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeGetFrameSnapshot(ToolscreenMacRuntimeFrameSnapshot* outSnapshot);
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeInstallOpenGLPresentHook();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeUninstallOpenGLPresentHook();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeIsOpenGLPresentHookInstalled();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeStartHotkeys();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeStopHotkeys();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeAreHotkeysRunning();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeStartNinjabrainClient(const char* apiBaseUrl);
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeStopNinjabrainClient();
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeGetNinjabrainSnapshot(ToolscreenMacRuntimeNinjabrainSnapshot* outSnapshot);
TOOLSCREEN_MACOS_RUNTIME_EXPORT int ToolscreenMacRuntimeSelfTest();

}
