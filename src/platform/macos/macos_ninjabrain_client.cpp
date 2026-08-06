#include "macos_ninjabrain_client.h"

#include "features/ninjabrain_api.h"
#include "features/ninjabrain_api_constants.h"
#include "features/ninjabrain_data.h"
#include "macos_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace {

std::mutex g_ninjabrainMutex;
std::unique_ptr<NinjabrainApiSession> g_ninjabrainSession;
NinjabrainApiConnectionTracker g_ninjabrainStatus;
NinjabrainData g_ninjabrainData;
std::uint64_t g_ninjabrainGeneration = 0;
std::uint64_t g_ninjabrainActiveGeneration = 0;

void CopyText(char* destination, std::size_t destinationSize, const std::string& value) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }

    std::snprintf(destination, destinationSize, "%s", value.c_str());
}

bool IsActiveGeneration(std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
    return g_ninjabrainActiveGeneration == generation;
}

void ModifyData(const std::function<void(NinjabrainData&)>& callback) {
    std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
    callback(g_ninjabrainData);
}

void LogNinjabrain(const std::string& message) {
    ToolscreenMacRuntimeAppendLog(("macos-ninjabrain " + message).c_str());
}

} // namespace

extern "C" int ToolscreenMacRuntimeStartNinjabrainClient(const char* apiBaseUrl) {
    try {
        std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
        if (g_ninjabrainSession) {
            return 1;
        }

        const std::string normalized =
            NormalizeNinjabrainApiBaseUrl(apiBaseUrl ? apiBaseUrl : "", kDefaultNinjabrainApiBaseUrl);
        const std::uint64_t generation = ++g_ninjabrainGeneration;
        g_ninjabrainActiveGeneration = generation;
        g_ninjabrainData = {};
        g_ninjabrainStatus.Start(normalized);

        NinjabrainApiSessionCallbacks callbacks;
        callbacks.onStrongholdMessage = [generation](const std::string& payload) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            ModifyData([&](NinjabrainData& data) {
                ApplyNinjabrainStrongholdEvent(payload, data, LogNinjabrain);
            });
        };
        callbacks.onStrongholdConnect = [generation]() {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
            g_ninjabrainStatus.MarkStrongholdConnected();
        };
        callbacks.onStrongholdDisconnect = [generation](const std::string& error) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
                g_ninjabrainStatus.MarkStrongholdDisconnected(error);
            }
            ModifyData([](NinjabrainData& data) {
                ClearNinjabrainStrongholdData(data);
            });
        };

        callbacks.onInformationMessagesMessage = [generation](const std::string& payload) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            ModifyData([&](NinjabrainData& data) {
                ApplyNinjabrainInformationMessagesEvent(payload, data, LogNinjabrain);
            });
        };
        callbacks.onInformationMessagesConnect = [generation]() {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
            g_ninjabrainStatus.MarkInformationMessagesConnected();
        };
        callbacks.onInformationMessagesDisconnect = [generation](const std::string& error) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
                g_ninjabrainStatus.MarkInformationMessagesDisconnected(error);
            }
            ModifyData([](NinjabrainData& data) {
                ClearNinjabrainInformationMessagesData(data);
            });
        };

        callbacks.onBoatMessage = [generation](const std::string& payload) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            ModifyData([&](NinjabrainData& data) {
                ApplyNinjabrainBoatEvent(payload, data, LogNinjabrain);
            });
        };
        callbacks.onBoatConnect = [generation]() {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
            g_ninjabrainStatus.MarkBoatConnected();
        };
        callbacks.onBoatDisconnect = [generation](const std::string& error) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
                g_ninjabrainStatus.MarkBoatDisconnected(error);
            }
            ModifyData([](NinjabrainData& data) {
                ClearNinjabrainBoatData(data);
            });
        };

        callbacks.onBlindMessage = [generation](const std::string& payload) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            ModifyData([&](NinjabrainData& data) {
                ApplyNinjabrainBlindEvent(payload, data, LogNinjabrain);
            });
        };
        callbacks.onBlindConnect = [generation]() {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
            g_ninjabrainStatus.MarkBlindConnected();
        };
        callbacks.onBlindDisconnect = [generation](const std::string& error) {
            if (!IsActiveGeneration(generation)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
                g_ninjabrainStatus.MarkBlindDisconnected(error);
            }
            ModifyData([](NinjabrainData& data) {
                ClearNinjabrainBlindData(data);
            });
        };
        callbacks.onLog = LogNinjabrain;

        g_ninjabrainSession = std::make_unique<NinjabrainApiSession>(normalized, std::move(callbacks));
        LogNinjabrain("started baseUrl=" + normalized);
    } catch (const std::exception& exception) {
        LogNinjabrain("start-failed " + std::string(exception.what()));
        return 0;
    }

    return 1;
}

extern "C" int ToolscreenMacRuntimeStopNinjabrainClient() {
    std::unique_ptr<NinjabrainApiSession> session;
    {
        std::lock_guard<std::mutex> lock(g_ninjabrainMutex);
        g_ninjabrainActiveGeneration = 0;
        g_ninjabrainStatus.Stop();
        g_ninjabrainData = {};
        session = std::move(g_ninjabrainSession);
    }

    if (session) {
        session->Stop();
        LogNinjabrain("stopped");
    }

    return 1;
}

extern "C" int ToolscreenMacRuntimeGetNinjabrainSnapshot(ToolscreenMacRuntimeNinjabrainSnapshot* outSnapshot) {
    if (outSnapshot == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_ninjabrainMutex);

    const NinjabrainApiStatus status = g_ninjabrainStatus.Snapshot();
    const bool running = status.connectionState != NinjabrainApiConnectionState::Stopped;
    const bool connected = status.connectionState == NinjabrainApiConnectionState::Connected;
    const bool offline = status.connectionState == NinjabrainApiConnectionState::Offline;

    ToolscreenMacRuntimeNinjabrainSnapshot snapshot;
    snapshot.running = running ? 1 : 0;
    snapshot.connected = connected ? 1 : 0;
    snapshot.offline = offline ? 1 : 0;
    snapshot.validPrediction = g_ninjabrainData.validPrediction ? 1 : 0;
    snapshot.eyeCount = g_ninjabrainData.eyeCount;
    snapshot.strongholdX = g_ninjabrainData.strongholdX;
    snapshot.strongholdZ = g_ninjabrainData.strongholdZ;
    snapshot.distance = g_ninjabrainData.distance;
    snapshot.lastAngle = g_ninjabrainData.lastAngle;
    snapshot.hasBoatAngle = g_ninjabrainData.hasBoatAngle ? 1 : 0;
    snapshot.boatAngle = g_ninjabrainData.boatAngle;
    snapshot.informationMessageCount = g_ninjabrainData.informationMessageCount;
    snapshot.blindEnabled = g_ninjabrainData.blind.enabled ? 1 : 0;
    snapshot.blindHasResult = g_ninjabrainData.blind.hasResult ? 1 : 0;
    snapshot.blindNetherX = g_ninjabrainData.blind.xInNether;
    snapshot.blindNetherZ = g_ninjabrainData.blind.zInNether;
    CopyText(snapshot.resultType, sizeof(snapshot.resultType), g_ninjabrainData.resultType);
    CopyText(snapshot.boatState, sizeof(snapshot.boatState), g_ninjabrainData.boatState);

    *outSnapshot = snapshot;
    return 1;
}
