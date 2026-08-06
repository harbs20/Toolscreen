#pragma once

#include "macos_runtime.h"

extern "C" int ToolscreenMacRuntimeStartNinjabrainClient(const char* apiBaseUrl);
extern "C" int ToolscreenMacRuntimeStopNinjabrainClient();
extern "C" int ToolscreenMacRuntimeGetNinjabrainSnapshot(ToolscreenMacRuntimeNinjabrainSnapshot* outSnapshot);
