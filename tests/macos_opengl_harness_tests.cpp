#include "platform/macos/macos_runtime.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using ResetFrameStateFn = decltype(&ToolscreenMacRuntimeResetFrameState);
using OnOpenGLPresentFn = decltype(&ToolscreenMacRuntimeOnOpenGLPresent);
using GetFrameSnapshotFn = decltype(&ToolscreenMacRuntimeGetFrameSnapshot);

class CglUnavailable : public std::runtime_error {
  public:
    explicit CglUnavailable(const std::string& message)
        : std::runtime_error(message) {}
};

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

[[noreturn]] void SkipCgl(const std::string& message) {
    throw CglUnavailable(message);
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

std::string DescribeCglError(CGLError error) {
    const char* message = CGLErrorString(error);
    return message ? message : "unknown CGL error";
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

class ScopedCglContext {
  public:
    ScopedCglContext() {
        CGLPixelFormatAttribute acceleratedAttributes[] = {
            kCGLPFAOpenGLProfile,
            static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
            kCGLPFAAccelerated,
            static_cast<CGLPixelFormatAttribute>(0),
        };

        GLint displayCount = 0;
        CGLError error = CGLChoosePixelFormat(acceleratedAttributes, &pixelFormat_, &displayCount);
        if (error != kCGLNoError || pixelFormat_ == nullptr) {
            CGLPixelFormatAttribute fallbackAttributes[] = {
                static_cast<CGLPixelFormatAttribute>(0),
            };
            error = CGLChoosePixelFormat(fallbackAttributes, &pixelFormat_, &displayCount);
        }

        if (error != kCGLNoError || pixelFormat_ == nullptr) {
            SkipCgl("Failed to choose a CGL pixel format: " + DescribeCglError(error));
        }

        error = CGLCreateContext(pixelFormat_, nullptr, &context_);
        if (error != kCGLNoError || context_ == nullptr) {
            SkipCgl("Failed to create a CGL context: " + DescribeCglError(error));
        }

        error = CGLSetCurrentContext(context_);
        if (error != kCGLNoError) {
            SkipCgl("Failed to activate the CGL context: " + DescribeCglError(error));
        }
    }

    ~ScopedCglContext() {
        if (CGLGetCurrentContext() == context_) {
            CGLSetCurrentContext(nullptr);
        }
        if (context_ != nullptr) {
            CGLDestroyContext(context_);
        }
        if (pixelFormat_ != nullptr) {
            CGLReleasePixelFormat(pixelFormat_);
        }
    }

    ScopedCglContext(const ScopedCglContext&) = delete;
    ScopedCglContext& operator=(const ScopedCglContext&) = delete;

    CGLContextObj get() const {
        return context_;
    }

  private:
    CGLPixelFormatObj pixelFormat_ = nullptr;
    CGLContextObj context_ = nullptr;
};

void VerifySyntheticFramePath(
    ResetFrameStateFn resetFrameState,
    OnOpenGLPresentFn onOpenGLPresent,
    GetFrameSnapshotFn getFrameSnapshot) {
    resetFrameState();

    auto* context = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1234));
    Require(
        onOpenGLPresent(context, 1024, 768) == 1,
        "Expected the macOS runtime to accept the synthetic OpenGL present callback.");

    ToolscreenMacRuntimeFrameSnapshot snapshot;
    Require(getFrameSnapshot(&snapshot) == 1, "Expected to read a macOS runtime frame snapshot.");
    RequireEqual(snapshot.frameCount, static_cast<std::uint64_t>(1), "synthetic present callback count");
    RequireEqual(snapshot.contextAddress, reinterpret_cast<std::uintptr_t>(context), "synthetic present context address");
    RequireEqual(snapshot.width, 1024, "synthetic present width");
    RequireEqual(snapshot.height, 768, "synthetic present height");
    RequireEqual(snapshot.resolvedHalfWidth, 512, "synthetic present resolved half width");
    RequireEqual(snapshot.initialized, 1, "runtime initialized flag");
}

void VerifyLiveOpenGLFramePath(
    ResetFrameStateFn resetFrameState,
    OnOpenGLPresentFn onOpenGLPresent,
    GetFrameSnapshotFn getFrameSnapshot) {
    ScopedCglContext context;
    resetFrameState();

    const struct {
        int width;
        int height;
        float red;
        float green;
        float blue;
    } frames[] = {
        { 640, 360, 1.0f, 0.0f, 0.0f },
        { 800, 450, 0.0f, 1.0f, 0.0f },
        { 1280, 720, 0.0f, 0.0f, 1.0f },
    };

    for (const auto& frame : frames) {
        glViewport(0, 0, frame.width, frame.height);
        glClearColor(frame.red, frame.green, frame.blue, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();

        Require(
            onOpenGLPresent(context.get(), frame.width, frame.height) == 1,
            "Expected the macOS runtime to accept the OpenGL present callback.");
    }

    ToolscreenMacRuntimeFrameSnapshot snapshot;
    Require(getFrameSnapshot(&snapshot) == 1, "Expected to read a macOS runtime frame snapshot.");
    RequireEqual(snapshot.frameCount, static_cast<std::uint64_t>(3), "present callback count");
    RequireEqual(snapshot.contextAddress, reinterpret_cast<std::uintptr_t>(context.get()), "present context address");
    RequireEqual(snapshot.width, 1280, "last present width");
    RequireEqual(snapshot.height, 720, "last present height");
    RequireEqual(snapshot.resolvedHalfWidth, 640, "last present resolved half width");
    RequireEqual(snapshot.initialized, 1, "runtime initialized flag");
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

        auto* const resetFrameState =
            LoadSymbol<decltype(&ToolscreenMacRuntimeResetFrameState)>(library, "ToolscreenMacRuntimeResetFrameState");
        auto* const onOpenGLPresent =
            LoadSymbol<decltype(&ToolscreenMacRuntimeOnOpenGLPresent)>(library, "ToolscreenMacRuntimeOnOpenGLPresent");
        auto* const getFrameSnapshot =
            LoadSymbol<decltype(&ToolscreenMacRuntimeGetFrameSnapshot)>(library, "ToolscreenMacRuntimeGetFrameSnapshot");

        try {
            VerifyLiveOpenGLFramePath(resetFrameState, onOpenGLPresent, getFrameSnapshot);
        } catch (const CglUnavailable& exception) {
            std::cerr << "Skipping live CGL context check: " << exception.what() << '\n';
            VerifySyntheticFramePath(resetFrameState, onOpenGLPresent, getFrameSnapshot);
        }

        if (dlclose(library) != 0) {
            Fail("Failed to close macOS runtime dylib: " + std::string(dlerror()));
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
