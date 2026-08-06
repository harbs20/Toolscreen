#include "macos_runtime.h"

#include "macos_debug_overlay.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <OpenGL/gl.h>

#include <atomic>
#include <cmath>
#include <mutex>

namespace {

using FlushBufferFn = void (*)(id, SEL);

std::atomic<FlushBufferFn> g_originalFlushBuffer{ nullptr };
std::atomic_bool g_openGLPresentHookInstalled{ false };
std::mutex g_openGLPresentHookMutex;

int RoundDimension(CGFloat value) {
    if (!std::isfinite(static_cast<double>(value)) || value <= 0.0) {
        return 0;
    }

    return static_cast<int>(std::lround(static_cast<double>(value)));
}

void ReadContextDimensions(NSOpenGLContext* context, int& width, int& height) {
    width = 0;
    height = 0;

    NSView* view = [context view];
    if (view != nil) {
        NSRect bounds = [view bounds];
        NSSize backingSize = bounds.size;
        if ([view respondsToSelector:@selector(convertSizeToBacking:)]) {
            backingSize = [view convertSizeToBacking:bounds.size];
        } else if ([view window] != nil && [[view window] respondsToSelector:@selector(backingScaleFactor)]) {
            const CGFloat scale = [[view window] backingScaleFactor];
            backingSize.width *= scale;
            backingSize.height *= scale;
        }

        width = RoundDimension(backingSize.width);
        height = RoundDimension(backingSize.height);
    }

    if (width <= 0 || height <= 0) {
        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        width = viewport[2];
        height = viewport[3];
    }
}

void HookedFlushBuffer(id self, SEL selector) {
    if (self != nil && [self isKindOfClass:[NSOpenGLContext class]]) {
        int width = 0;
        int height = 0;
        ReadContextDimensions((NSOpenGLContext*)self, width, height);
        if (width > 0 && height > 0) {
            ToolscreenMacRuntimeOnOpenGLPresent((void*)self, width, height);
            ToolscreenMacRuntimeRenderDebugOverlay((void*)self, width, height);
        }
    }

    if (FlushBufferFn original = g_originalFlushBuffer.load(std::memory_order_acquire)) {
        original(self, selector);
    }
}

Method GetFlushBufferMethod() {
    Class openGLContextClass = NSClassFromString(@"NSOpenGLContext");
    if (openGLContextClass == Nil) {
        return nullptr;
    }

    return class_getInstanceMethod(openGLContextClass, @selector(flushBuffer));
}

} // namespace

extern "C" int ToolscreenMacRuntimeInstallOpenGLPresentHook() {
    std::lock_guard<std::mutex> lock(g_openGLPresentHookMutex);

    if (g_openGLPresentHookInstalled.load(std::memory_order_acquire)) {
        return 1;
    }

    Method method = GetFlushBufferMethod();
    if (method == nullptr) {
        ToolscreenMacRuntimeAppendLog("install-opengl-present-hook failed missing-NSOpenGLContext-flushBuffer");
        return 0;
    }

    IMP currentImplementation = method_getImplementation(method);
    if (currentImplementation == reinterpret_cast<IMP>(&HookedFlushBuffer)) {
        g_openGLPresentHookInstalled.store(true, std::memory_order_release);
        ToolscreenMacRuntimeAppendLog("install-opengl-present-hook already-installed");
        return 1;
    }

    g_originalFlushBuffer.store(reinterpret_cast<FlushBufferFn>(currentImplementation), std::memory_order_release);
    method_setImplementation(method, reinterpret_cast<IMP>(&HookedFlushBuffer));
    g_openGLPresentHookInstalled.store(true, std::memory_order_release);
    ToolscreenMacRuntimeAppendLog("install-opengl-present-hook ok selector=NSOpenGLContext.flushBuffer");
    return 1;
}

extern "C" int ToolscreenMacRuntimeUninstallOpenGLPresentHook() {
    std::lock_guard<std::mutex> lock(g_openGLPresentHookMutex);

    if (!g_openGLPresentHookInstalled.load(std::memory_order_acquire)) {
        return 1;
    }

    Method method = GetFlushBufferMethod();
    FlushBufferFn original = g_originalFlushBuffer.load(std::memory_order_acquire);
    if (method == nullptr || original == nullptr) {
        ToolscreenMacRuntimeAppendLog("uninstall-opengl-present-hook failed missing-original");
        return 0;
    }

    IMP currentImplementation = method_getImplementation(method);
    if (currentImplementation != reinterpret_cast<IMP>(&HookedFlushBuffer)) {
        ToolscreenMacRuntimeAppendLog("uninstall-opengl-present-hook failed replaced-by-other-implementation");
        return 0;
    }

    method_setImplementation(method, reinterpret_cast<IMP>(original));
    g_originalFlushBuffer.store(nullptr, std::memory_order_release);
    g_openGLPresentHookInstalled.store(false, std::memory_order_release);
    ToolscreenMacRuntimeAppendLog("uninstall-opengl-present-hook ok");
    return 1;
}

extern "C" int ToolscreenMacRuntimeIsOpenGLPresentHookInstalled() {
    return g_openGLPresentHookInstalled.load(std::memory_order_acquire) ? 1 : 0;
}
