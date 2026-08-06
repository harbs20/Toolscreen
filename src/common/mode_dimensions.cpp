#include "mode_dimensions.h"

#include "mode_dimension_state.h"

#include "gui/gui.h"
#include "runtime/logic_thread.h"

namespace {

ModeDimensionState ToModeDimensionState(const ModeConfig& mode) {
    ModeDimensionState state;
    state.id = mode.id;
    state.width = mode.width;
    state.height = mode.height;
    state.manualWidth = mode.manualWidth;
    state.manualHeight = mode.manualHeight;
    state.useRelativeSize = mode.useRelativeSize;
    state.relativeWidth = mode.relativeWidth;
    state.relativeHeight = mode.relativeHeight;
    state.widthExpr = mode.widthExpr;
    state.heightExpr = mode.heightExpr;
    state.stretch.enabled = mode.stretch.enabled;
    state.stretch.x = mode.stretch.x;
    state.stretch.y = mode.stretch.y;
    state.stretch.width = mode.stretch.width;
    state.stretch.height = mode.stretch.height;
    return state;
}

void ApplyModeDimensionState(const ModeDimensionState& state, ModeConfig& mode) {
    mode.width = state.width;
    mode.height = state.height;
    mode.manualWidth = state.manualWidth;
    mode.manualHeight = state.manualHeight;
    mode.useRelativeSize = state.useRelativeSize;
    mode.relativeWidth = state.relativeWidth;
    mode.relativeHeight = state.relativeHeight;
    mode.widthExpr = state.widthExpr;
    mode.heightExpr = state.heightExpr;
    mode.stretch.enabled = state.stretch.enabled;
    mode.stretch.x = state.stretch.x;
    mode.stretch.y = state.stretch.y;
    mode.stretch.width = state.stretch.width;
    mode.stretch.height = state.stretch.height;
}

} // namespace

bool SyncPreemptiveModeFromEyeZoom(Config& config) {
    ModeConfig* eyezoomMode = nullptr;
    ModeConfig* preemptiveMode = nullptr;
    for (auto& mode : config.modes) {
        if (!eyezoomMode && mode.id == "EyeZoom") { eyezoomMode = &mode; }
        if (!preemptiveMode && mode.id == "Preemptive") { preemptiveMode = &mode; }
    }

    if (!eyezoomMode || !preemptiveMode) { return false; }

    const ModeDimensionState eyezoomState = ToModeDimensionState(*eyezoomMode);
    ModeDimensionState preemptiveState = ToModeDimensionState(*preemptiveMode);
    const bool changed = SyncPreemptiveModeDimensionState(eyezoomState, preemptiveState);
    if (changed) {
        ApplyModeDimensionState(preemptiveState, *preemptiveMode);
    }

    return changed;
}

int ResolveModeDisplayWidth(const ModeConfig& mode, int screenW, int screenH) {
    return ResolveModeDimensionDisplayWidth(ToModeDimensionState(mode), screenW, screenH);
}

int ResolveModeDisplayHeight(const ModeConfig& mode, int screenW, int screenH) {
    return ResolveModeDimensionDisplayHeight(ToModeDimensionState(mode), screenW, screenH);
}

void RecalculateModeDimensions(Config& config, int screenW, int screenH) {
    for (auto& mode : config.modes) {
        ModeDimensionState state = ToModeDimensionState(mode);
        RecalculateModeDimensionState(state, screenW, screenH);
        ApplyModeDimensionState(state, mode);
    }

    SyncPreemptiveModeFromEyeZoom(config);
}

void RecalculateModeDimensions() {
    int screenW = GetCachedWindowWidth();
    int screenH = GetCachedWindowHeight();
    RecalculateModeDimensions(g_config, screenW, screenH);
}
