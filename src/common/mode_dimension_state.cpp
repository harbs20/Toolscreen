#include "mode_dimension_state.h"

#include "expression_parser.h"

#include <cmath>

namespace {

void NormalizeScreenSize(int& screenW, int& screenH) {
    if (screenW < 1) { screenW = 1; }
    if (screenH < 1) { screenH = 1; }
}

bool IsRelativeWidthEnabled(const ModeDimensionState& mode) {
    return mode.id != "Preemptive" && mode.useRelativeSize && mode.relativeWidth >= 0.0f && mode.relativeWidth <= 1.0f;
}

bool IsRelativeHeightEnabled(const ModeDimensionState& mode) {
    return mode.id != "Preemptive" && mode.useRelativeSize && mode.relativeHeight >= 0.0f && mode.relativeHeight <= 1.0f;
}

int ResolveRelativeDimension(float relativeValue, int referenceSize) {
    int result = static_cast<int>(std::lround(relativeValue * static_cast<float>(referenceSize)));
    if (result < 1) { result = 1; }
    return result;
}

} // namespace

bool SyncPreemptiveModeDimensionState(const ModeDimensionState& eyezoomMode, ModeDimensionState& preemptiveMode) {
    bool changed = false;

    if (preemptiveMode.width != eyezoomMode.width) {
        preemptiveMode.width = eyezoomMode.width;
        changed = true;
    }
    if (preemptiveMode.height != eyezoomMode.height) {
        preemptiveMode.height = eyezoomMode.height;
        changed = true;
    }

    const int syncedManualWidth = (eyezoomMode.manualWidth > 0) ? eyezoomMode.manualWidth : eyezoomMode.width;
    const int syncedManualHeight = (eyezoomMode.manualHeight > 0) ? eyezoomMode.manualHeight : eyezoomMode.height;
    if (preemptiveMode.manualWidth != syncedManualWidth) {
        preemptiveMode.manualWidth = syncedManualWidth;
        changed = true;
    }
    if (preemptiveMode.manualHeight != syncedManualHeight) {
        preemptiveMode.manualHeight = syncedManualHeight;
        changed = true;
    }
    if (preemptiveMode.useRelativeSize) {
        preemptiveMode.useRelativeSize = false;
        changed = true;
    }
    if (preemptiveMode.relativeWidth != -1.0f) {
        preemptiveMode.relativeWidth = -1.0f;
        changed = true;
    }
    if (preemptiveMode.relativeHeight != -1.0f) {
        preemptiveMode.relativeHeight = -1.0f;
        changed = true;
    }
    if (!preemptiveMode.widthExpr.empty()) {
        preemptiveMode.widthExpr.clear();
        changed = true;
    }
    if (!preemptiveMode.heightExpr.empty()) {
        preemptiveMode.heightExpr.clear();
        changed = true;
    }

    return changed;
}

int ResolveModeDimensionDisplayWidth(const ModeDimensionState& mode, int screenW, int screenH) {
    NormalizeScreenSize(screenW, screenH);

    int width = mode.width;
    if (IsRelativeWidthEnabled(mode)) {
        width = ResolveRelativeDimension(mode.relativeWidth, screenW);
    }

    if (mode.id == "Thin" && width < 330) {
        width = 330;
    }

    return width;
}

int ResolveModeDimensionDisplayHeight(const ModeDimensionState& mode, int screenW, int screenH) {
    NormalizeScreenSize(screenW, screenH);

    int height = mode.height;
    if (IsRelativeHeightEnabled(mode)) {
        height = ResolveRelativeDimension(mode.relativeHeight, screenH);
    }

    return height;
}

void RecalculateModeDimensionState(ModeDimensionState& mode, int screenW, int screenH) {
    NormalizeScreenSize(screenW, screenH);

    if (mode.id == "Fullscreen") {
        mode.stretch.enabled = true;
        mode.stretch.x = 0;
        mode.stretch.y = 0;
        mode.stretch.width = screenW;
        mode.stretch.height = screenH;
    }

    if (mode.id == "Preemptive") {
        mode.useRelativeSize = false;
        mode.relativeWidth = -1.0f;
        mode.relativeHeight = -1.0f;
        mode.widthExpr.clear();
        mode.heightExpr.clear();
    }

    const bool widthIsRelative = IsRelativeWidthEnabled(mode);
    const bool heightIsRelative = IsRelativeHeightEnabled(mode);
    const bool expressionAllowed = mode.id != "Fullscreen" && mode.id != "Preemptive";
    const bool widthUsesExpression = expressionAllowed && !widthIsRelative && !mode.widthExpr.empty();
    const bool heightUsesExpression = expressionAllowed && !heightIsRelative && !mode.heightExpr.empty();

    if (widthIsRelative) {
        const int newWidth = ResolveRelativeDimension(mode.relativeWidth, screenW);
        mode.width = newWidth;
        if (mode.manualWidth < 1) {
            mode.manualWidth = newWidth;
        }
    }
    if (heightIsRelative) {
        const int newHeight = ResolveRelativeDimension(mode.relativeHeight, screenH);
        mode.height = newHeight;
        if (mode.manualHeight < 1) {
            mode.manualHeight = newHeight;
        }
    }

    if (widthUsesExpression) {
        const int newWidth = EvaluateExpression(mode.widthExpr, screenW, screenH, mode.width);
        if (newWidth > 0) {
            mode.width = newWidth;
        }
    }
    if (heightUsesExpression) {
        const int newHeight = EvaluateExpression(mode.heightExpr, screenW, screenH, mode.height);
        if (newHeight > 0) {
            mode.height = newHeight;
        }
    }

    if (mode.id == "Thin" && mode.width < 330) { mode.width = 330; }
}
