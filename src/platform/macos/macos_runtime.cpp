#include "macos_runtime.h"

#include "common/mode_dimension_state.h"
#include "features/ninjabrain_api_constants.h"
#include "features/ninjabrain_data.h"
#include "features/ninjabrain_events.h"
#include "macos_hotkeys.h"
#include "macos_ninjabrain_client.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

#include <unistd.h>

#ifndef TOOLSCREEN_VERSION_LITERAL
#define TOOLSCREEN_VERSION_LITERAL "0.0.0-dev"
#endif

namespace {

std::atomic_bool g_runtimeInitialized{ false };
std::mutex g_frameStateMutex;
ToolscreenMacRuntimeFrameSnapshot g_frameState;
std::mutex g_logMutex;

bool IsEnvironmentTruthy(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return false;
    }

    const std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "YES" || text == "on" ||
        text == "ON";
}

std::string ResolveLogPath() {
    if (const char* overridePath = std::getenv("TOOLSCREEN_MACOS_LOG_PATH");
        overridePath != nullptr && overridePath[0] != '\0') {
        return overridePath;
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::string(home) + "/Library/Logs/Toolscreen/macos-runtime.log";
    }

    return "/tmp/toolscreen-macos-runtime.log";
}

std::string CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime {};
    localtime_r(&time, &localTime);

    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

void AppendLogLine(const std::string& message) {
    try {
        const std::filesystem::path path(ToolscreenMacRuntimeLogPath());
        if (path.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_logMutex);

        if (!path.parent_path().empty()) {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
        }

        std::ofstream stream(path, std::ios::app);
        if (!stream) {
            return;
        }

        stream << CurrentTimestamp() << " pid=" << getpid() << " " << message << '\n';
    } catch (const std::exception&) {
    }
}

ModeDimensionState MakeHalfWidthMode(int screenWidth) {
    ModeDimensionState mode;
    mode.id = "MacRuntimeSmoke";
    mode.width = 1;
    mode.height = 1;
    mode.useRelativeSize = true;
    mode.relativeWidth = 0.5f;
    mode.relativeHeight = 0.5f;
    RecalculateModeDimensionState(mode, screenWidth, 1080);
    return mode;
}

__attribute__((constructor)) void ToolscreenMacRuntimeOnLoad() {
    g_runtimeInitialized.store(true, std::memory_order_release);
    AppendLogLine(std::string("loaded version=") + TOOLSCREEN_VERSION_LITERAL);

    if (IsEnvironmentTruthy("TOOLSCREEN_MACOS_AUTO_INSTALL_OPENGL_HOOK")) {
        const int installed = ToolscreenMacRuntimeInstallOpenGLPresentHook();
        AppendLogLine(installed ? "auto-install-opengl-present-hook ok" : "auto-install-opengl-present-hook failed");
    }

    if (IsEnvironmentTruthy("TOOLSCREEN_MACOS_HOTKEYS")) {
        const int started = ToolscreenMacRuntimeStartHotkeys();
        AppendLogLine(started ? "auto-start-hotkeys ok" : "auto-start-hotkeys failed");
    }

    if (IsEnvironmentTruthy("TOOLSCREEN_MACOS_NINJABRAIN")) {
        const char* apiBaseUrl = std::getenv("TOOLSCREEN_MACOS_NINJABRAIN_API_BASE_URL");
        const int started = ToolscreenMacRuntimeStartNinjabrainClient(apiBaseUrl);
        AppendLogLine(started ? "auto-start-ninjabrain ok" : "auto-start-ninjabrain failed");
    }
}

__attribute__((destructor)) void ToolscreenMacRuntimeOnUnload() {
    ToolscreenMacRuntimeStopNinjabrainClient();

    if (ToolscreenMacRuntimeAreHotkeysRunning()) {
        ToolscreenMacRuntimeStopHotkeys();
    }

    if (ToolscreenMacRuntimeIsOpenGLPresentHookInstalled()) {
        ToolscreenMacRuntimeUninstallOpenGLPresentHook();
    }

    AppendLogLine("unloaded");
    g_runtimeInitialized.store(false, std::memory_order_release);
}

} // namespace

extern "C" const char* ToolscreenMacRuntimeVersion() {
    return TOOLSCREEN_VERSION_LITERAL;
}

extern "C" int ToolscreenMacRuntimeIsInitialized() {
    return g_runtimeInitialized.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" const char* ToolscreenMacRuntimeLogPath() {
    try {
        static const std::string path = ResolveLogPath();
        return path.c_str();
    } catch (const std::exception&) {
        return "";
    }
}

extern "C" void ToolscreenMacRuntimeAppendLog(const char* message) {
    AppendLogLine(message != nullptr ? message : "");
}

extern "C" const char* ToolscreenMacRuntimeNormalizeNinjabrainApiBaseUrl(const char* apiBaseUrl) {
    try {
        static thread_local std::string normalized;
        normalized = NormalizeNinjabrainApiBaseUrl(apiBaseUrl ? apiBaseUrl : "", kDefaultNinjabrainApiBaseUrl);
        return normalized.c_str();
    } catch (const std::exception&) {
        return "";
    }
}

extern "C" int ToolscreenMacRuntimeResolveHalfWidth(int screenWidth) {
    try {
        return MakeHalfWidthMode(screenWidth).width;
    } catch (const std::exception&) {
        return 0;
    }
}

extern "C" void ToolscreenMacRuntimeResetFrameState() {
    std::lock_guard<std::mutex> lock(g_frameStateMutex);
    g_frameState = {};
    g_frameState.initialized = ToolscreenMacRuntimeIsInitialized();
}

extern "C" int ToolscreenMacRuntimeOnOpenGLPresent(void* context, int width, int height) {
    std::uint64_t frameCount = 0;
    int resolvedHalfWidth = 0;

    try {
        if (!ToolscreenMacRuntimeIsInitialized() || context == nullptr || width <= 0 || height <= 0) {
            return 0;
        }

        resolvedHalfWidth = MakeHalfWidthMode(width).width;

        {
            std::lock_guard<std::mutex> lock(g_frameStateMutex);
            ++g_frameState.frameCount;
            frameCount = g_frameState.frameCount;
            g_frameState.contextAddress = reinterpret_cast<std::uintptr_t>(context);
            g_frameState.width = width;
            g_frameState.height = height;
            g_frameState.resolvedHalfWidth = resolvedHalfWidth;
            g_frameState.initialized = 1;
        }
    } catch (const std::exception&) {
        return 0;
    }

    if (frameCount == 1 || frameCount % 600 == 0) {
        std::ostringstream stream;
        stream << "opengl-present frame=" << frameCount << " context=0x"
               << std::hex << reinterpret_cast<std::uintptr_t>(context) << std::dec << " size=" << width << "x"
               << height << " resolvedHalfWidth=" << resolvedHalfWidth;
        AppendLogLine(stream.str());
    }

    return 1;
}

extern "C" int ToolscreenMacRuntimeGetFrameSnapshot(ToolscreenMacRuntimeFrameSnapshot* outSnapshot) {
    if (outSnapshot == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_frameStateMutex);
    *outSnapshot = g_frameState;
    outSnapshot->initialized = ToolscreenMacRuntimeIsInitialized();
    return 1;
}

extern "C" int ToolscreenMacRuntimeSelfTest() {
    try {
        if (!ToolscreenMacRuntimeIsInitialized()) {
            return 0;
        }

        if (ToolscreenMacRuntimeResolveHalfWidth(1920) != 960) {
            return 0;
        }

        const std::string normalized =
            ToolscreenMacRuntimeNormalizeNinjabrainApiBaseUrl(" http://127.0.0.1:52533/// ");
        if (normalized != "http://127.0.0.1:52533") {
            return 0;
        }

        NinjabrainData data;
        ApplyNinjabrainBoatEvent(R"({"boatAngle":45.5,"boatState":"VALID"})", data);
        if (data.boatState != "VALID" || !data.hasBoatAngle || data.boatAngle != 45.5) {
            return 0;
        }

        ToolscreenMacRuntimeResetFrameState();
        if (!ToolscreenMacRuntimeOnOpenGLPresent(reinterpret_cast<void*>(0x1), 1920, 1080)) {
            return 0;
        }

        ToolscreenMacRuntimeFrameSnapshot snapshot;
        if (!ToolscreenMacRuntimeGetFrameSnapshot(&snapshot) || snapshot.frameCount != 1 ||
            snapshot.width != 1920 || snapshot.height != 1080 || snapshot.resolvedHalfWidth != 960 ||
            snapshot.contextAddress != 0x1) {
            return 0;
        }
    } catch (const std::exception&) {
        return 0;
    }

    return 1;
}
