#pragma once

extern "C" void ToolscreenMacRuntimeSetDebugOverlaySettings(int enabled, const char* mode, float scale);
extern "C" void ToolscreenMacRuntimeRenderDebugOverlay(void* context, int width, int height);
