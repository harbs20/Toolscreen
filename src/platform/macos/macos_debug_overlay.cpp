#include "macos_debug_overlay.h"

#include "common/mode_dimension_state.h"
#include "macos_runtime.h"

#include <OpenGL/gl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace {

enum class OverlayMode {
    Marker,
    Thin,
    Wide,
    EyeZoom,
    Fullscreen,
};

struct OverlaySettings {
    bool enabled = false;
    OverlayMode mode = OverlayMode::Thin;
    std::string modeName = "THIN";
    float scale = 1.0f;
};

struct OverlayRegion {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Glyph {
    char value = ' ';
    std::array<unsigned char, 7> rows {};
};

std::atomic_bool g_loggedOverlayReady{ false };
std::mutex g_overlaySettingsOverrideMutex;
OverlaySettings g_overlaySettingsOverride;
std::chrono::steady_clock::time_point g_overlaySettingsOverrideTime;
std::string g_lastLoggedSettings;

constexpr float kOverlayScaleMin = 0.65f;
constexpr float kOverlayScaleMax = 1.60f;

constexpr std::array<Glyph, 43> kGlyphs = { {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E } },
    { '6', { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '%', { 0x19, 0x1A, 0x04, 0x08, 0x0B, 0x13, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } },
    { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
} };

bool IsEnvironmentTruthy(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return false;
    }

    const std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "YES" || text == "on" ||
        text == "ON";
}

bool IsEnvironmentSet(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

float Clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

std::string Trim(std::string value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (begin >= end) {
        return {};
    }

    return std::string(begin, end);
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

OverlayMode ParseOverlayMode(const std::string& value, OverlayMode fallback) {
    const std::string lowered = Lowercase(Trim(value));
    if (lowered == "marker") {
        return OverlayMode::Marker;
    }
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
    return fallback;
}

const char* OverlayModeName(OverlayMode mode) {
    switch (mode) {
    case OverlayMode::Marker:
        return "MARKER";
    case OverlayMode::Thin:
        return "THIN";
    case OverlayMode::Wide:
        return "WIDE";
    case OverlayMode::EyeZoom:
        return "EYEZOOM";
    case OverlayMode::Fullscreen:
        return "FULLSCREEN";
    }

    return "THIN";
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

std::string ResolveSettingsPath() {
    if (const char* overridePath = std::getenv("TOOLSCREEN_MACOS_SETTINGS_PATH");
        overridePath != nullptr && overridePath[0] != '\0') {
        return overridePath;
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::string(home) + "/Library/Application Support/Toolscreen/macos-runtime.properties";
    }

    return {};
}

OverlaySettings LoadOverlaySettingsFromDisk(OverlaySettings settings) {
    const std::string path = ResolveSettingsPath();
    if (path.empty()) {
        return settings;
    }

    std::ifstream stream(path);
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
            if (Lowercase(value) == "off") {
                settings.enabled = false;
            } else {
                settings.mode = ParseOverlayMode(value, settings.mode);
                settings.modeName = OverlayModeName(settings.mode);
            }
        } else if (key == "overlay.scale") {
            settings.scale = Clamp(ParseFloat(value, settings.scale), kOverlayScaleMin, kOverlayScaleMax);
        }
    }

    return settings;
}

OverlaySettings ResolveOverlaySettings() {
    OverlaySettings defaults;
    defaults.enabled = IsEnvironmentTruthy("TOOLSCREEN_MACOS_OVERLAY") ||
        IsEnvironmentTruthy("TOOLSCREEN_MACOS_DEBUG_OVERLAY");

    if (!IsEnvironmentSet("TOOLSCREEN_MACOS_OVERLAY") && !IsEnvironmentSet("TOOLSCREEN_MACOS_DEBUG_OVERLAY")) {
        defaults.enabled = false;
    }

    if (const char* mode = std::getenv("TOOLSCREEN_MACOS_OVERLAY_MODE"); mode != nullptr && mode[0] != '\0') {
        defaults.mode = ParseOverlayMode(mode, defaults.mode);
    }
    if (const char* scale = std::getenv("TOOLSCREEN_MACOS_OVERLAY_SCALE"); scale != nullptr && scale[0] != '\0') {
        defaults.scale = Clamp(ParseFloat(scale, defaults.scale), kOverlayScaleMin, kOverlayScaleMax);
    }
    defaults.modeName = OverlayModeName(defaults.mode);

    static OverlaySettings cached = defaults;
    static auto lastLoadTime = std::chrono::steady_clock::time_point {};
    static auto cachedUpdateTime = std::chrono::steady_clock::time_point {};

    const auto now = std::chrono::steady_clock::now();
    if (lastLoadTime.time_since_epoch().count() == 0 ||
        now - lastLoadTime > std::chrono::milliseconds(500)) {
        cached = LoadOverlaySettingsFromDisk(defaults);
        cached.modeName = OverlayModeName(cached.mode);
        lastLoadTime = now;
        cachedUpdateTime = now;
    }

    {
        std::lock_guard<std::mutex> lock(g_overlaySettingsOverrideMutex);
        if (g_overlaySettingsOverrideTime > cachedUpdateTime) {
            cached = g_overlaySettingsOverride;
            cached.modeName = OverlayModeName(cached.mode);
            cachedUpdateTime = g_overlaySettingsOverrideTime;
        }
    }

    std::ostringstream stream;
    stream << "macos-overlay settings enabled=" << (cached.enabled ? "true" : "false") << " mode="
           << cached.modeName << " scale=" << ScalePercent(cached.scale);
    const std::string message = stream.str();
    if (message != g_lastLoggedSettings) {
        g_lastLoggedSettings = message;
        ToolscreenMacRuntimeAppendLog(message.c_str());
    }

    return cached;
}

ModeDimensionState MakeModeState(OverlayMode mode, int screenWidth, int screenHeight, float scale) {
    ModeDimensionState state;
    state.id = OverlayModeName(mode);
    state.width = 1;
    state.height = 1;
    state.manualWidth = 1;
    state.manualHeight = 1;

    switch (mode) {
    case OverlayMode::Marker:
        state.width = static_cast<int>(std::lround(224.0f * scale));
        state.height = static_cast<int>(std::lround(58.0f * scale));
        break;
    case OverlayMode::Thin:
        state.width = static_cast<int>(std::lround(330.0f * scale));
        state.height = screenHeight;
        break;
    case OverlayMode::Wide:
        state.useRelativeSize = true;
        state.relativeWidth = 1.0f;
        state.width = screenWidth;
        state.height = static_cast<int>(std::lround(400.0f * scale));
        break;
    case OverlayMode::EyeZoom:
        state.width = static_cast<int>(std::lround(384.0f * scale));
        state.height = screenHeight;
        break;
    case OverlayMode::Fullscreen:
        state.useRelativeSize = true;
        state.relativeWidth = 1.0f;
        state.relativeHeight = 1.0f;
        break;
    }

    state.manualWidth = state.width;
    state.manualHeight = state.height;
    RecalculateModeDimensionState(state, screenWidth, screenHeight);
    return state;
}

OverlayRegion ResolveRegion(const OverlaySettings& settings, int screenWidth, int screenHeight) {
    const OverlayMode mode = settings.mode;
    const float scale = Clamp(static_cast<float>(screenWidth) / 1920.0f, 0.85f, 1.45f) *
        Clamp(settings.scale, kOverlayScaleMin, kOverlayScaleMax);
    const ModeDimensionState state = MakeModeState(mode, screenWidth, screenHeight, scale);

    OverlayRegion region;
    region.width = static_cast<float>(std::max(1, state.width));
    region.height = static_cast<float>(std::max(1, state.height));

    switch (mode) {
    case OverlayMode::Marker:
    case OverlayMode::Thin:
    case OverlayMode::Fullscreen:
        region.x = 0.0f;
        region.y = 0.0f;
        break;
    case OverlayMode::Wide:
        region.x = 0.0f;
        region.y = static_cast<float>(screenHeight) - region.height;
        break;
    case OverlayMode::EyeZoom:
        region.x = static_cast<float>(screenWidth) - region.width;
        region.y = 0.0f;
        break;
    }

    region.width = Clamp(region.width, 1.0f, static_cast<float>(screenWidth));
    region.height = Clamp(region.height, 1.0f, static_cast<float>(screenHeight));
    region.x = Clamp(region.x, 0.0f, static_cast<float>(screenWidth) - region.width);
    region.y = Clamp(region.y, 0.0f, static_cast<float>(screenHeight) - region.height);
    return region;
}

void LogOverlayReadyOnce() {
    bool expected = false;
    if (g_loggedOverlayReady.compare_exchange_strong(expected, true)) {
        ToolscreenMacRuntimeAppendLog("macos-overlay ready");
    }
}

void DrawRect(float x, float y, float width, float height, float red, float green, float blue, float alpha) {
    glColor4f(red, green, blue, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

void DrawBorder(float x, float y, float width, float height, float thickness, float red, float green, float blue, float alpha) {
    DrawRect(x, y, width, thickness, red, green, blue, alpha);
    DrawRect(x, y + height - thickness, width, thickness, red, green, blue, alpha);
    DrawRect(x, y, thickness, height, red, green, blue, alpha);
    DrawRect(x + width - thickness, y, thickness, height, red, green, blue, alpha);
}

const Glyph& FindGlyph(char value) {
    const char normalized = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.value == normalized) {
            return glyph;
        }
    }

    return kGlyphs.back();
}

void DrawText(float x, float y, const std::string& text, float pixel, float red, float green, float blue, float alpha) {
    float cursorX = x;
    const float gap = pixel;
    const float advance = 6.0f * pixel;

    for (char ch : text) {
        const Glyph& glyph = FindGlyph(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                const unsigned char mask = static_cast<unsigned char>(1U << (4 - col));
                if ((glyph.rows[static_cast<std::size_t>(row)] & mask) != 0) {
                    DrawRect(cursorX + static_cast<float>(col) * pixel, y + static_cast<float>(row) * pixel, pixel, pixel, red, green, blue, alpha);
                }
            }
        }

        cursorX += (ch == ' ') ? advance - gap : advance;
    }
}

std::string IntText(std::int64_t value) {
    return std::to_string(value);
}

std::string RoundedText(double value) {
    return IntText(static_cast<std::int64_t>(std::lround(value)));
}

int EstimateFps(std::uint64_t frameCount) {
    static auto firstFrameTime = std::chrono::steady_clock::now();
    static std::uint64_t firstFrameCount = frameCount;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - firstFrameTime).count();
    if (elapsed <= 0.25 || frameCount <= firstFrameCount) {
        return 0;
    }

    return static_cast<int>(std::lround(static_cast<double>(frameCount - firstFrameCount) / elapsed));
}

std::string NinjabrainStatusText(const ToolscreenMacRuntimeNinjabrainSnapshot& snapshot) {
    if (snapshot.running == 0) {
        return "NB OFF";
    }
    if (snapshot.connected != 0) {
        return "NB CONNECTED";
    }
    if (snapshot.offline != 0) {
        return "NB OFFLINE";
    }
    return "NB CONNECTING";
}

void DrawMarker(const OverlayRegion& region) {
    const float scale = Clamp(region.width / 224.0f, 0.85f, 1.45f);
    const float stripe = 7.0f * scale;
    const float pad = 14.0f * scale;
    const float lineX = region.x + pad + 42.0f * scale;

    DrawRect(region.x, region.y, region.width, region.height, 0.02f, 0.025f, 0.03f, 0.56f);
    DrawRect(region.x, region.y, stripe, region.height, 0.0f, 0.86f, 0.78f, 0.96f);
    DrawRect(region.x + pad, region.y + pad, 28.0f * scale, 28.0f * scale, 1.0f, 0.72f, 0.2f, 0.96f);
    DrawRect(lineX, region.y + pad, 126.0f * scale, 8.0f * scale, 0.92f, 0.96f, 1.0f, 0.92f);
    DrawRect(lineX, region.y + pad + 17.0f * scale, 92.0f * scale, 8.0f * scale, 0.38f, 0.72f, 1.0f, 0.9f);
    DrawRect(region.x + region.width - 22.0f * scale, region.y + 10.0f * scale, 10.0f * scale, 10.0f * scale, 1.0f, 0.25f, 0.44f, 0.95f);
}

void DrawHud(const OverlaySettings& settings, const OverlayRegion& region, int screenWidth, int screenHeight) {
    ToolscreenMacRuntimeFrameSnapshot snapshot;
    ToolscreenMacRuntimeGetFrameSnapshot(&snapshot);

    ToolscreenMacRuntimeNinjabrainSnapshot ninjabrainSnapshot {};
    const bool hasNinjabrainSnapshot = ToolscreenMacRuntimeGetNinjabrainSnapshot(&ninjabrainSnapshot) != 0;

    const float scale = Clamp(static_cast<float>(screenWidth) / 1920.0f, 0.85f, 1.45f) *
        Clamp(settings.scale, kOverlayScaleMin, kOverlayScaleMax);
    const float border = std::max(2.0f, 2.0f * scale);
    const float pad = 15.0f * scale;
    const float textPixel = std::max(2.0f, 3.0f * scale);
    const float lineHeight = 8.2f * textPixel;

    int lineCount = 7;
    if (hasNinjabrainSnapshot) {
        ++lineCount;
        if (ninjabrainSnapshot.validPrediction != 0) {
            ++lineCount;
        }
        if (ninjabrainSnapshot.eyeCount > 0) {
            ++lineCount;
        }
        if (ninjabrainSnapshot.hasBoatAngle != 0) {
            ++lineCount;
        }
        if (ninjabrainSnapshot.blindHasResult != 0) {
            ++lineCount;
        }
    }

    const bool fullscreen = settings.mode == OverlayMode::Fullscreen;
    const float backgroundAlpha = fullscreen ? 0.045f : 0.18f;
    const float headerWidth = std::min(region.width - pad * 2.0f, 430.0f * scale);
    const float desiredHeaderHeight = 28.0f * scale + static_cast<float>(lineCount) * lineHeight;
    const float headerHeight = std::max(1.0f, std::min(region.height - pad * 2.0f, desiredHeaderHeight));
    const float headerX = region.x + pad;
    const float headerY = region.y + pad;

    DrawRect(region.x, region.y, region.width, region.height, 0.02f, 0.025f, 0.03f, backgroundAlpha);
    DrawBorder(region.x, region.y, region.width, region.height, border, 0.0f, 0.86f, 0.78f, 0.7f);
    DrawRect(headerX, headerY, headerWidth, headerHeight, 0.02f, 0.025f, 0.03f, 0.62f);
    DrawRect(headerX, headerY, 7.0f * scale, headerHeight, 0.0f, 0.86f, 0.78f, 0.96f);

    const float textX = headerX + 18.0f * scale;
    float textY = headerY + 14.0f * scale;
    const float textBottom = headerY + headerHeight - 6.0f * scale;
    auto drawLine = [&](const std::string& text, float red, float green, float blue, float alpha) {
        if (textY + 7.0f * textPixel <= textBottom) {
            DrawText(textX, textY, text, textPixel, red, green, blue, alpha);
        }
        textY += lineHeight;
    };

    drawLine("TOOLSCREEN MAC", 0.92f, 0.96f, 1.0f, 0.95f);
    drawLine(std::string("MODE ") + settings.modeName, 1.0f, 0.72f, 0.2f, 0.96f);
    drawLine(std::string("SCALE ") + IntText(ScalePercent(settings.scale)) + "%", 0.86f, 0.86f, 0.92f, 0.9f);
    drawLine(std::string("FRAMES ") + IntText(static_cast<std::int64_t>(snapshot.frameCount)), 0.7f, 0.86f, 1.0f, 0.92f);
    drawLine(std::string("FPS ") + IntText(EstimateFps(snapshot.frameCount)), 0.7f, 1.0f, 0.82f, 0.92f);
    drawLine(std::string("FB ") + IntText(screenWidth) + "X" + IntText(screenHeight), 0.86f, 0.86f, 0.92f, 0.9f);
    drawLine(
        std::string("OV ") + IntText(static_cast<std::int64_t>(std::lround(region.width))) + "X" +
            IntText(static_cast<std::int64_t>(std::lround(region.height))),
        0.86f,
        0.86f,
        0.92f,
        0.9f);

    if (hasNinjabrainSnapshot) {
        drawLine(NinjabrainStatusText(ninjabrainSnapshot), 0.0f, 0.86f, 0.78f, 0.96f);
        if (ninjabrainSnapshot.validPrediction != 0) {
            drawLine(
                std::string("SH ") + IntText(ninjabrainSnapshot.strongholdX) + " " +
                    IntText(ninjabrainSnapshot.strongholdZ),
                1.0f,
                0.72f,
                0.2f,
                0.96f);
        }
        if (ninjabrainSnapshot.eyeCount > 0) {
            drawLine(
                std::string("EYES ") + IntText(ninjabrainSnapshot.eyeCount) + " ANG " +
                    RoundedText(ninjabrainSnapshot.lastAngle),
                0.7f,
                0.86f,
                1.0f,
                0.92f);
        }
        if (ninjabrainSnapshot.hasBoatAngle != 0) {
            drawLine(std::string("BOAT ") + RoundedText(ninjabrainSnapshot.boatAngle), 0.7f, 1.0f, 0.82f, 0.92f);
        }
        if (ninjabrainSnapshot.blindHasResult != 0) {
            drawLine(
                std::string("BLIND ") + RoundedText(ninjabrainSnapshot.blindNetherX) + " " +
                    RoundedText(ninjabrainSnapshot.blindNetherZ),
                0.86f,
                0.86f,
                0.92f,
                0.9f);
        }
    }
}

} // namespace

extern "C" void ToolscreenMacRuntimeSetDebugOverlaySettings(int enabled, const char* mode, float scale) {
    OverlaySettings settings;
    settings.enabled = enabled != 0;
    settings.mode = ParseOverlayMode(mode != nullptr ? mode : "", OverlayMode::Thin);
    settings.modeName = OverlayModeName(settings.mode);
    settings.scale = Clamp(scale, kOverlayScaleMin, kOverlayScaleMax);

    std::lock_guard<std::mutex> lock(g_overlaySettingsOverrideMutex);
    g_overlaySettingsOverride = settings;
    g_overlaySettingsOverrideTime = std::chrono::steady_clock::now();
}

extern "C" void ToolscreenMacRuntimeRenderDebugOverlay(void* context, int width, int height) {
    const OverlaySettings settings = ResolveOverlaySettings();
    if (!settings.enabled || context == nullptr || width <= 0 || height <= 0) {
        return;
    }

    LogOverlayReadyOnce();

    GLint previousMatrixMode = GL_MODELVIEW;
    GLint previousProgram = 0;
    glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_VIEWPORT_BIT);
    glUseProgram(0);
    glViewport(0, 0, width, height);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<GLdouble>(width), static_cast<GLdouble>(height), 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const OverlayRegion region = ResolveRegion(settings, width, height);
    if (settings.mode == OverlayMode::Marker) {
        DrawMarker(region);
    } else {
        DrawHud(settings, region, width, height);
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(previousMatrixMode);
    glUseProgram(static_cast<GLuint>(previousProgram));
    glPopAttrib();
}
