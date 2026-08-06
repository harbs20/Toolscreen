#include "macos_hotkeys.h"

#include "macos_debug_overlay.h"
#include "macos_runtime.h"

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

namespace {

enum class OverlayMode {
    Thin,
    Wide,
    EyeZoom,
    Fullscreen,
    Marker,
};

struct OverlaySettings {
    bool enabled = true;
    OverlayMode mode = OverlayMode::Thin;
    float scale = 1.0f;
};

std::atomic_bool g_hotkeysRunning{ false };
std::mutex g_hotkeyMutex;
std::thread g_hotkeyThread;

constexpr float kOverlayScaleMin = 0.65f;
constexpr float kOverlayScaleMax = 1.60f;
constexpr float kOverlayScaleStep = 0.05f;

std::string Lowercase(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

std::string Trim(std::string value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool ParseBool(const std::string& value, bool fallback) {
    const std::string lowered = Lowercase(Trim(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return fallback;
}

float ClampScale(float value) {
    return std::max(kOverlayScaleMin, std::min(value, kOverlayScaleMax));
}

float ParseFloat(const std::string& value, float fallback) {
    try {
        std::size_t consumed = 0;
        const float parsed = std::stof(Trim(value), &consumed);
        if (consumed == 0) {
            return fallback;
        }
        return parsed;
    } catch (const std::exception&) {
        return fallback;
    }
}

int ScalePercent(float scale) {
    return static_cast<int>(std::lround(scale * 100.0f));
}

const char* OverlayModeValue(OverlayMode mode) {
    switch (mode) {
    case OverlayMode::Thin:
        return "thin";
    case OverlayMode::Wide:
        return "wide";
    case OverlayMode::EyeZoom:
        return "eyezoom";
    case OverlayMode::Fullscreen:
        return "fullscreen";
    case OverlayMode::Marker:
        return "marker";
    }

    return "thin";
}

OverlayMode ParseMode(const std::string& value, OverlayMode fallback) {
    const std::string lowered = Lowercase(Trim(value));
    if (lowered == "thin") {
        return OverlayMode::Thin;
    }
    if (lowered == "wide") {
        return OverlayMode::Wide;
    }
    if (lowered == "eyezoom" || lowered == "eye_zoom" || lowered == "eye-zoom") {
        return OverlayMode::EyeZoom;
    }
    if (lowered == "fullscreen" || lowered == "full") {
        return OverlayMode::Fullscreen;
    }
    if (lowered == "marker") {
        return OverlayMode::Marker;
    }
    return fallback;
}

OverlayMode NextMode(OverlayMode mode) {
    switch (mode) {
    case OverlayMode::Thin:
        return OverlayMode::Wide;
    case OverlayMode::Wide:
        return OverlayMode::EyeZoom;
    case OverlayMode::EyeZoom:
        return OverlayMode::Fullscreen;
    case OverlayMode::Fullscreen:
        return OverlayMode::Marker;
    case OverlayMode::Marker:
        return OverlayMode::Thin;
    }

    return OverlayMode::Thin;
}

std::string ResolveSettingsPath() {
    if (const char* overridePath = std::getenv("TOOLSCREEN_MACOS_SETTINGS_PATH");
        overridePath != nullptr && overridePath[0] != '\0') {
        return overridePath;
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::string(home) + "/Library/Application Support/Toolscreen/macos-runtime.properties";
    }

    return "/tmp/toolscreen-macos-runtime.properties";
}

OverlaySettings ReadSettings() {
    OverlaySettings settings;

    std::ifstream stream(ResolveSettingsPath());
    if (!stream) {
        return settings;
    }

    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = Lowercase(Trim(line.substr(0, separator)));
        const std::string value = Trim(line.substr(separator + 1));

        if (key == "overlay.enabled") {
            settings.enabled = ParseBool(value, settings.enabled);
        } else if (key == "overlay.mode") {
            settings.mode = ParseMode(value, settings.mode);
        } else if (key == "overlay.scale") {
            settings.scale = ClampScale(ParseFloat(value, settings.scale));
        }
    }

    return settings;
}

bool WriteSettings(const OverlaySettings& settings) {
    const std::filesystem::path path(ResolveSettingsPath());
    if (!path.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    std::ofstream stream(path);
    if (!stream) {
        return false;
    }

    stream << "overlay.enabled=" << (settings.enabled ? "true" : "false") << '\n';
    stream << "overlay.mode=" << OverlayModeValue(settings.mode) << '\n';
    stream << "overlay.scale=" << static_cast<float>(ScalePercent(settings.scale)) / 100.0f << '\n';
    return true;
}

bool WriteAndApplySettings(const OverlaySettings& settings) {
    const bool wrote = WriteSettings(settings);
    if (wrote) {
        ToolscreenMacRuntimeSetDebugOverlaySettings(
            settings.enabled ? 1 : 0,
            OverlayModeValue(settings.mode),
            settings.scale);
    }
    return wrote;
}

void LogSettingsAction(const char* action, const OverlaySettings& settings, bool wrote) {
    std::ostringstream stream;
    stream << "macos-hotkey " << action << " enabled=" << (settings.enabled ? "true" : "false")
           << " mode=" << OverlayModeValue(settings.mode) << " scale=" << ScalePercent(settings.scale)
           << (wrote ? " ok" : " failed");
    ToolscreenMacRuntimeAppendLog(stream.str().c_str());
}

void CycleMode() {
    OverlaySettings settings = ReadSettings();
    settings.enabled = true;
    settings.mode = NextMode(settings.mode);
    LogSettingsAction("cycle-mode", settings, WriteAndApplySettings(settings));
}

void SetMode(OverlayMode mode, const char* action) {
    OverlaySettings settings = ReadSettings();
    settings.enabled = true;
    settings.mode = mode;
    LogSettingsAction(action, settings, WriteAndApplySettings(settings));
}

void ToggleOverlay() {
    OverlaySettings settings = ReadSettings();
    settings.enabled = !settings.enabled;
    LogSettingsAction("toggle-overlay", settings, WriteAndApplySettings(settings));
}

void AdjustScale(float delta) {
    OverlaySettings settings = ReadSettings();
    settings.enabled = true;
    settings.scale = ClampScale(settings.scale + delta);
    LogSettingsAction(delta > 0.0f ? "scale-up" : "scale-down", settings, WriteAndApplySettings(settings));
}

void ResetOverlay() {
    OverlaySettings settings = ReadSettings();
    settings.enabled = true;
    settings.mode = OverlayMode::Fullscreen;
    settings.scale = 1.0f;
    LogSettingsAction("reset-overlay", settings, WriteAndApplySettings(settings));
}

bool KeyDown(CGKeyCode keyCode) {
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, keyCode);
}

bool AnyCommandDown() {
    return KeyDown(kVK_Command) || KeyDown(kVK_RightCommand);
}

bool AnyControlDown() {
    return KeyDown(kVK_Control) || KeyDown(kVK_RightControl);
}

bool AnyOptionDown() {
    return KeyDown(kVK_Option) || KeyDown(kVK_RightOption);
}

bool AnyShiftDown() {
    return KeyDown(kVK_Shift) || KeyDown(kVK_RightShift);
}

bool ChordDown(CGKeyCode keyCode) {
    return AnyCommandDown() && AnyOptionDown() && AnyControlDown() && KeyDown(keyCode);
}

bool BareKeyDown(CGKeyCode keyCode) {
    return KeyDown(keyCode) && !AnyCommandDown() && !AnyControlDown() && !AnyOptionDown() && !AnyShiftDown();
}

bool OptionOnlyDown() {
    return AnyOptionDown() && !AnyCommandDown() && !AnyControlDown() && !AnyShiftDown();
}

void RunHotkeyThread() {
    ToolscreenMacRuntimeAppendLog(
        "macos-hotkey poller-ready bindings=Option/V/5/6/F6/F7/F8/F9/Ctrl+Opt+Cmd+-/=/0/8/9");

    bool previousTallDown = false;
    bool previousThinDown = false;
    bool previousWideDown = false;
    bool previousShrinkDown = false;
    bool previousGrowDown = false;
    bool previousResetDown = false;
    bool previousCycleDown = false;
    bool previousToggleDown = false;

    while (g_hotkeysRunning.load(std::memory_order_acquire)) {
        const bool tallDown = OptionOnlyDown();
        const bool thinDown = BareKeyDown(kVK_ANSI_V);
        const bool wideDown = BareKeyDown(kVK_ANSI_5);
        const bool shrinkDown = KeyDown(kVK_F6) || ChordDown(kVK_ANSI_Minus);
        const bool growDown = KeyDown(kVK_F7) || ChordDown(kVK_ANSI_Equal);
        const bool resetDown = BareKeyDown(kVK_ANSI_6) || ChordDown(kVK_ANSI_0);
        const bool cycleDown = KeyDown(kVK_F8) || ChordDown(kVK_ANSI_8);
        const bool toggleDown = KeyDown(kVK_F9) || ChordDown(kVK_ANSI_9);

        if (tallDown && !previousTallDown) {
            SetMode(OverlayMode::EyeZoom, "set-tall-mode");
        }
        if (thinDown && !previousThinDown) {
            SetMode(OverlayMode::Thin, "set-thin-mode");
        }
        if (wideDown && !previousWideDown) {
            SetMode(OverlayMode::Wide, "set-wide-mode");
        }
        if (shrinkDown && !previousShrinkDown) {
            AdjustScale(-kOverlayScaleStep);
        }
        if (growDown && !previousGrowDown) {
            AdjustScale(kOverlayScaleStep);
        }
        if (resetDown && !previousResetDown) {
            ResetOverlay();
        }
        if (cycleDown && !previousCycleDown) {
            CycleMode();
        }
        if (toggleDown && !previousToggleDown) {
            ToggleOverlay();
        }

        previousTallDown = tallDown;
        previousThinDown = thinDown;
        previousWideDown = wideDown;
        previousShrinkDown = shrinkDown;
        previousGrowDown = growDown;
        previousResetDown = resetDown;
        previousCycleDown = cycleDown;
        previousToggleDown = toggleDown;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    ToolscreenMacRuntimeAppendLog("macos-hotkey poller-stopped");
}

} // namespace

extern "C" int ToolscreenMacRuntimeStartHotkeys() {
    std::lock_guard<std::mutex> lock(g_hotkeyMutex);

    if (g_hotkeysRunning.load(std::memory_order_acquire)) {
        return 1;
    }

    g_hotkeysRunning.store(true, std::memory_order_release);
    try {
        g_hotkeyThread = std::thread(RunHotkeyThread);
    } catch (const std::exception&) {
        g_hotkeysRunning.store(false, std::memory_order_release);
        return 0;
    }

    return 1;
}

extern "C" int ToolscreenMacRuntimeStopHotkeys() {
    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock(g_hotkeyMutex);
        g_hotkeysRunning.store(false, std::memory_order_release);
        if (g_hotkeyThread.joinable()) {
            threadToJoin = std::move(g_hotkeyThread);
        }
    }

    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }

    return 1;
}

extern "C" int ToolscreenMacRuntimeAreHotkeysRunning() {
    return g_hotkeysRunning.load(std::memory_order_acquire) ? 1 : 0;
}
