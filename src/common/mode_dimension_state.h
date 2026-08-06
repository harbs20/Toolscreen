#pragma once

#include <string>

struct ModeStretchState {
    bool enabled = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ModeDimensionState {
    std::string id;
    int width = 0;
    int height = 0;
    int manualWidth = 0;
    int manualHeight = 0;
    bool useRelativeSize = false;
    float relativeWidth = 0.5f;
    float relativeHeight = 0.5f;
    std::string widthExpr;
    std::string heightExpr;
    ModeStretchState stretch;
};

bool SyncPreemptiveModeDimensionState(const ModeDimensionState& eyezoomMode, ModeDimensionState& preemptiveMode);
int ResolveModeDimensionDisplayWidth(const ModeDimensionState& mode, int screenW, int screenH);
int ResolveModeDimensionDisplayHeight(const ModeDimensionState& mode, int screenW, int screenH);
void RecalculateModeDimensionState(ModeDimensionState& mode, int screenW, int screenH);
