#include "platform/macos/macos_runtime.h"

#include <dlfcn.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

void Require(bool condition, const char* message) {
    if (!condition) {
        Fail(message);
    }
}

template <typename T, typename U>
void RequireEqual(const T& actual, const U& expected, const char* label) {
    if (!(actual == expected)) {
        std::ostringstream stream;
        stream << label << " mismatch. Expected '" << expected << "' but got '" << actual << "'.";
        Fail(stream.str());
    }
}

template <typename Function>
Function LoadSymbol(void* library, const char* name) {
    dlerror();
    void* symbol = dlsym(library, name);
    const char* error = dlerror();
    if (error != nullptr || symbol == nullptr) {
        Fail("Failed to load symbol '" + std::string(name) + "': " + (error ? error : "not found"));
    }

    return reinterpret_cast<Function>(symbol);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            Fail("Expected path to the macOS runtime dylib.");
        }

        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            Fail("Failed to load macOS runtime dylib: " + std::string(dlerror()));
        }

        auto* const isInitialized =
            LoadSymbol<decltype(&ToolscreenMacRuntimeIsInitialized)>(library, "ToolscreenMacRuntimeIsInitialized");
        auto* const version =
            LoadSymbol<decltype(&ToolscreenMacRuntimeVersion)>(library, "ToolscreenMacRuntimeVersion");
        auto* const normalize =
            LoadSymbol<decltype(&ToolscreenMacRuntimeNormalizeNinjabrainApiBaseUrl)>(
                library,
                "ToolscreenMacRuntimeNormalizeNinjabrainApiBaseUrl");
        auto* const resolveHalfWidth =
            LoadSymbol<decltype(&ToolscreenMacRuntimeResolveHalfWidth)>(library, "ToolscreenMacRuntimeResolveHalfWidth");
        auto* const installOpenGLPresentHook =
            LoadSymbol<decltype(&ToolscreenMacRuntimeInstallOpenGLPresentHook)>(
                library,
                "ToolscreenMacRuntimeInstallOpenGLPresentHook");
        auto* const uninstallOpenGLPresentHook =
            LoadSymbol<decltype(&ToolscreenMacRuntimeUninstallOpenGLPresentHook)>(
                library,
                "ToolscreenMacRuntimeUninstallOpenGLPresentHook");
        auto* const isOpenGLPresentHookInstalled =
            LoadSymbol<decltype(&ToolscreenMacRuntimeIsOpenGLPresentHookInstalled)>(
                library,
                "ToolscreenMacRuntimeIsOpenGLPresentHookInstalled");
        auto* const startHotkeys =
            LoadSymbol<decltype(&ToolscreenMacRuntimeStartHotkeys)>(library, "ToolscreenMacRuntimeStartHotkeys");
        auto* const stopHotkeys =
            LoadSymbol<decltype(&ToolscreenMacRuntimeStopHotkeys)>(library, "ToolscreenMacRuntimeStopHotkeys");
        auto* const areHotkeysRunning =
            LoadSymbol<decltype(&ToolscreenMacRuntimeAreHotkeysRunning)>(library, "ToolscreenMacRuntimeAreHotkeysRunning");
        auto* const startNinjabrain =
            LoadSymbol<decltype(&ToolscreenMacRuntimeStartNinjabrainClient)>(
                library,
                "ToolscreenMacRuntimeStartNinjabrainClient");
        auto* const stopNinjabrain =
            LoadSymbol<decltype(&ToolscreenMacRuntimeStopNinjabrainClient)>(
                library,
                "ToolscreenMacRuntimeStopNinjabrainClient");
        auto* const getNinjabrainSnapshot =
            LoadSymbol<decltype(&ToolscreenMacRuntimeGetNinjabrainSnapshot)>(
                library,
                "ToolscreenMacRuntimeGetNinjabrainSnapshot");
        auto* const selfTest = LoadSymbol<decltype(&ToolscreenMacRuntimeSelfTest)>(library, "ToolscreenMacRuntimeSelfTest");

        Require(isInitialized() == 1, "Expected the macOS runtime constructor to mark the dylib initialized.");
        Require(version() != nullptr && std::string(version()).find('.') != std::string::npos, "Expected a version string.");
        RequireEqual(resolveHalfWidth(1920), 960, "half-width portable mode result");
        RequireEqual(
            std::string(normalize(" http://127.0.0.1:52533/// ")),
            std::string("http://127.0.0.1:52533"),
            "normalized Ninjabrain API URL");
        RequireEqual(isOpenGLPresentHookInstalled(), 0, "initial OpenGL present hook state");
        Require(installOpenGLPresentHook() == 1, "Expected OpenGL present hook installation to succeed.");
        RequireEqual(isOpenGLPresentHookInstalled(), 1, "installed OpenGL present hook state");
        Require(installOpenGLPresentHook() == 1, "Expected repeated OpenGL present hook installation to be idempotent.");
        Require(uninstallOpenGLPresentHook() == 1, "Expected OpenGL present hook removal to succeed.");
        RequireEqual(isOpenGLPresentHookInstalled(), 0, "removed OpenGL present hook state");
        RequireEqual(areHotkeysRunning(), 0, "initial hotkey thread state");
        Require(stopHotkeys() == 1, "Expected stopping inactive hotkeys to be idempotent.");
        (void)startHotkeys;
        ToolscreenMacRuntimeNinjabrainSnapshot ninjabrainSnapshot {};
        Require(getNinjabrainSnapshot(&ninjabrainSnapshot) == 1, "Expected Ninjabrain snapshot to be readable.");
        RequireEqual(ninjabrainSnapshot.running, 0, "initial Ninjabrain client state");
        Require(stopNinjabrain() == 1, "Expected stopping inactive Ninjabrain client to be idempotent.");
        (void)startNinjabrain;
        Require(selfTest() == 1, "Expected macOS runtime self-test to pass.");

        if (dlclose(library) != 0) {
            Fail("Failed to close macOS runtime dylib: " + std::string(dlerror()));
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
