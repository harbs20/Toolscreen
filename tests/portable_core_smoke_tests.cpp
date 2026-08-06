#include "common/expression_parser.h"
#include "common/mode_dimension_state.h"
#include "features/ninjabrain_data.h"
#include "features/ninjabrain_events.h"

#include <cmath>
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

void TestExpressionParser() {
    RequireEqual(EvaluateExpression("screenWidth / 2", 1920, 1080, -1), 960, "screenWidth expression");
    RequireEqual(EvaluateExpression("roundEven(screenHeight / 3)", 1920, 1080, -1), 360, "roundEven expression");
    RequireEqual(EvaluateExpression("min(screenWidth, screenHeight) - 80", 1280, 720, -1), 640, "min expression");
    RequireEqual(EvaluateExpression("unknown(", 1280, 720, 42), 42, "invalid expression fallback");

    std::string error;
    Require(ValidateExpression("max(screenWidth, screenHeight)", error), "Expected a valid expression.");
    Require(error.empty(), "Expected no validation error for a valid expression.");
    Require(!ValidateExpression("screenWidth / 0", error), "Expected division by zero to fail validation.");
    Require(!error.empty(), "Expected a validation error for division by zero.");

    Require(!IsExpression("1920"), "Plain positive integers should not be classified as expressions.");
    Require(!IsExpression("-1080"), "Plain negative integers should not be classified as expressions.");
    Require(IsExpression("screenWidth"), "Identifiers should be classified as expressions.");
}

void TestNinjabrainDataSnapshot() {
    NinjabrainData data;
    data.playerInNether = true;
    data.predictions[0].overworldDistance = 800.0;
    data.predictionCount = 1;

    PublishNinjabrainData(data);
    const auto snapshot = GetNinjabrainDataSnapshot();
    Require(snapshot != nullptr, "Expected a Ninjabrain data snapshot.");
    RequireEqual(snapshot->predictionCount, 1, "prediction count");
    RequireEqual(GetNinjabrainPredictionDisplayDistance(*snapshot, snapshot->predictions[0]), 100.0, "nether display distance");

    ModifyNinjabrainData([](NinjabrainData& next) {
        next.playerInNether = false;
        next.predictions[0].overworldDistance = 640.0;
    });

    const auto modified = GetNinjabrainDataSnapshot();
    Require(modified != nullptr, "Expected a modified Ninjabrain data snapshot.");
    RequireEqual(GetNinjabrainPredictionDisplayDistance(*modified, modified->predictions[0]), 640.0, "overworld display distance");
}

void TestModeDimensionState() {
    ModeDimensionState fullscreen;
    fullscreen.id = "Fullscreen";
    fullscreen.width = 1280;
    fullscreen.height = 720;
    fullscreen.stretch.x = 10;
    fullscreen.stretch.y = 20;
    fullscreen.stretch.width = 30;
    fullscreen.stretch.height = 40;

    RecalculateModeDimensionState(fullscreen, 1920, 1080);
    Require(fullscreen.stretch.enabled, "Fullscreen stretch should be enabled.");
    RequireEqual(fullscreen.stretch.x, 0, "fullscreen stretch x");
    RequireEqual(fullscreen.stretch.y, 0, "fullscreen stretch y");
    RequireEqual(fullscreen.stretch.width, 1920, "fullscreen stretch width");
    RequireEqual(fullscreen.stretch.height, 1080, "fullscreen stretch height");

    ModeDimensionState relative;
    relative.id = "Custom";
    relative.width = 100;
    relative.height = 100;
    relative.useRelativeSize = true;
    relative.relativeWidth = 0.5f;
    relative.relativeHeight = 0.25f;
    RecalculateModeDimensionState(relative, 1600, 900);
    RequireEqual(relative.width, 800, "relative width");
    RequireEqual(relative.height, 225, "relative height");
    RequireEqual(relative.manualWidth, 800, "relative manual width fallback");
    RequireEqual(relative.manualHeight, 225, "relative manual height fallback");

    ModeDimensionState expression;
    expression.id = "Expression";
    expression.width = 100;
    expression.height = 100;
    expression.widthExpr = "screenWidth / 3";
    expression.heightExpr = "max(120, screenHeight / 4)";
    RecalculateModeDimensionState(expression, 1920, 720);
    RequireEqual(expression.width, 640, "expression width");
    RequireEqual(expression.height, 180, "expression height");

    ModeDimensionState thin;
    thin.id = "Thin";
    thin.width = 120;
    thin.height = 200;
    RequireEqual(ResolveModeDimensionDisplayWidth(thin, 1, 1), 330, "thin minimum display width");

    ModeDimensionState eyezoom;
    eyezoom.id = "EyeZoom";
    eyezoom.width = 384;
    eyezoom.height = 16384;
    eyezoom.manualWidth = 400;
    eyezoom.manualHeight = 1200;

    ModeDimensionState preemptive;
    preemptive.id = "Preemptive";
    preemptive.width = 1;
    preemptive.height = 2;
    preemptive.manualWidth = 3;
    preemptive.manualHeight = 4;
    preemptive.useRelativeSize = true;
    preemptive.relativeWidth = 0.5f;
    preemptive.relativeHeight = 0.5f;
    preemptive.widthExpr = "screenWidth";
    preemptive.heightExpr = "screenHeight";

    Require(SyncPreemptiveModeDimensionState(eyezoom, preemptive), "Expected preemptive sync to report a change.");
    RequireEqual(preemptive.width, eyezoom.width, "preemptive synced width");
    RequireEqual(preemptive.height, eyezoom.height, "preemptive synced height");
    RequireEqual(preemptive.manualWidth, eyezoom.manualWidth, "preemptive synced manual width");
    RequireEqual(preemptive.manualHeight, eyezoom.manualHeight, "preemptive synced manual height");
    Require(!preemptive.useRelativeSize, "Preemptive should not keep relative sizing.");
    Require(preemptive.relativeWidth < 0.0f && preemptive.relativeHeight < 0.0f, "Preemptive relative values should be cleared.");
    Require(preemptive.widthExpr.empty() && preemptive.heightExpr.empty(), "Preemptive expressions should be cleared.");
}

void TestNinjabrainEvents() {
    RequireEqual(
        NormalizeNinjabrainApiBaseUrl(" http://127.0.0.1:52533/// "),
        std::string("http://127.0.0.1:52533"),
        "normalized API URL");
    RequireEqual(
        NormalizeNinjabrainApiBaseUrl("   "),
        std::string(kDefaultNinjabrainApiBaseUrl),
        "default API URL");

    NinjabrainData data;
    ApplyNinjabrainBoatEvent(R"({"boatAngle":45.5,"boatState":"VALID"})", data);
    RequireEqual(data.boatState, std::string("VALID"), "boat state");
    Require(data.hasBoatAngle, "Expected boat angle to be present.");
    RequireEqual(data.boatAngle, 45.5, "boat angle");

    ApplyNinjabrainInformationMessagesEvent(
        R"({"informationMessages":[{"severity":"WARNING","type":"MISMEASURE","message":"Check eye"}]})",
        data);
    RequireEqual(data.informationMessageCount, 1, "information message count");
    RequireEqual(data.informationMessages[0].severity, std::string("WARNING"), "information severity");
    RequireEqual(data.informationMessages[0].type, std::string("MISMEASURE"), "information type");

    ApplyNinjabrainBlindEvent(
        R"({"isBlindModeEnabled":true,"hasDivine":true,"blindResult":{"evaluation":"GOOD","xInNether":10,"zInNether":-20,"averageDistance":42}})",
        data);
    Require(data.blind.enabled, "Expected blind mode to be enabled.");
    Require(data.blind.hasDivine, "Expected divine state.");
    Require(data.blind.hasResult, "Expected blind result.");
    RequireEqual(data.blind.evaluation, std::string("GOOD"), "blind evaluation");
    RequireEqual(data.blind.zInNether, -20.0, "blind nether z");

    ApplyNinjabrainStrongholdEvent(
        R"({"resultType":"TRIANGULATED","playerPosition":{"xInOverworld":0,"zInOverworld":0,"horizontalAngle":0,"isInNether":false},"eyeThrows":[{"angle":10,"angleWithoutCorrection":9,"correction":1,"error":0.5,"type":"NORMAL"}],"predictions":[{"chunkX":1,"chunkZ":2,"certainty":0.75,"overworldDistance":256}]})",
        data);
    Require(data.validPrediction, "Expected stronghold prediction to be valid.");
    RequireEqual(data.resultType, std::string("TRIANGULATED"), "stronghold result type");
    RequireEqual(data.eyeCount, 1, "eye count");
    RequireEqual(data.predictionCount, 1, "prediction count from event");
    RequireEqual(data.strongholdX, 20, "stronghold x");
    RequireEqual(data.strongholdZ, 36, "stronghold z");
    RequireEqual(data.certainty, 0.75, "stronghold certainty");
    RequireEqual(data.informationMessageCount, 1, "information message preservation");
    RequireEqual(data.boatState, std::string("VALID"), "boat state preservation");
    Require(data.blind.enabled, "blind data preservation");

    bool loggedError = false;
    ApplyNinjabrainBoatEvent("{", data, [&](const std::string&) {
        loggedError = true;
    });
    Require(loggedError, "Expected invalid JSON to report through the log callback.");
}

} // namespace

int main() {
    try {
        TestExpressionParser();
        TestNinjabrainDataSnapshot();
        TestModeDimensionState();
        TestNinjabrainEvents();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
