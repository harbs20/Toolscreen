#include "ninjabrain_api.h"

#include <httplib.h>

#ifndef TOOLSCREEN_PORTABLE_NINJABRAIN_API
#include "common/utils.h"
#endif

#include <chrono>
#include <functional>
#include <utility>

namespace {

using namespace std::chrono_literals;
using SteadyClock = std::chrono::steady_clock;

constexpr auto kNinjabrainReconnectIntervalMs = 200;
constexpr auto kNinjabrainConnectionTimeout = 200ms;
constexpr auto kNinjabrainReadTimeout = 2s;
constexpr auto kNinjabrainWriteTimeout = 1s;
constexpr char kStrongholdEventsPath[] = "/api/v1/stronghold/events";
constexpr char kInformationMessagesEventsPath[] = "/api/v1/information-messages/events";
constexpr char kBoatEventsPath[] = "/api/v1/boat/events";
constexpr char kBlindEventsPath[] = "/api/v1/blind/events";

void LogIfPresent(const NinjabrainLogCallback& callback, const std::string& message) {
    if (callback) { callback(message); }
}

void LogNinjabrainApiMessage(const std::string& message) {
#ifdef TOOLSCREEN_PORTABLE_NINJABRAIN_API
    (void)message;
#else
    LogCategory("ninjabrain", message);
#endif
}

long long DurationToMilliseconds(SteadyClock::duration duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

std::pair<time_t, time_t> SplitDurationForHttplibTimeout(std::chrono::milliseconds duration) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    const auto remainder = std::chrono::duration_cast<std::chrono::microseconds>(duration - seconds);
    return { static_cast<time_t>(seconds.count()), static_cast<time_t>(remainder.count()) };
}

template <typename Callback, typename... Args>
void InvokeIfPresent(const Callback& callback, Args&&... args) {
    if (callback) { callback(std::forward<Args>(args)...); }
}

bool IsQuietStreamTimeout(httplib::Error error) {
    return error == httplib::Error::Read || error == httplib::Error::Timeout;
}

} // namespace

void NinjabrainApiConnectionTracker::Start(std::string apiBaseUrl) {
    sessionRunning_ = true;
    apiBaseUrl_ = NormalizeNinjabrainApiBaseUrl(std::move(apiBaseUrl));
    lastError_.clear();
    strongholdState_ = StreamState::Connecting;
    informationMessagesState_ = StreamState::Connecting;
    boatState_ = StreamState::Connecting;
    blindState_ = StreamState::Connecting;
}

void NinjabrainApiConnectionTracker::Stop() {
    sessionRunning_ = false;
    lastError_.clear();
    strongholdState_ = StreamState::Disconnected;
    informationMessagesState_ = StreamState::Disconnected;
    boatState_ = StreamState::Disconnected;
    blindState_ = StreamState::Disconnected;
}

void NinjabrainApiConnectionTracker::MarkStrongholdConnected() {
    MarkStreamConnected(strongholdState_);
}

void NinjabrainApiConnectionTracker::MarkStrongholdDisconnected(std::string error) {
    MarkStreamDisconnected(strongholdState_, std::move(error));
}

void NinjabrainApiConnectionTracker::MarkInformationMessagesConnected() {
    MarkStreamConnected(informationMessagesState_);
}

void NinjabrainApiConnectionTracker::MarkInformationMessagesDisconnected(std::string error) {
    MarkStreamDisconnected(informationMessagesState_, std::move(error));
}

void NinjabrainApiConnectionTracker::MarkBoatConnected() {
    MarkStreamConnected(boatState_);
}

void NinjabrainApiConnectionTracker::MarkBoatDisconnected(std::string error) {
    MarkStreamDisconnected(boatState_, std::move(error));
}

void NinjabrainApiConnectionTracker::MarkBlindConnected() {
    MarkStreamConnected(blindState_);
}

void NinjabrainApiConnectionTracker::MarkBlindDisconnected(std::string error) {
    MarkStreamDisconnected(blindState_, std::move(error));
}

NinjabrainApiStatus NinjabrainApiConnectionTracker::Snapshot() const {
    NinjabrainApiStatus status;
    status.apiBaseUrl = apiBaseUrl_;

    if (!sessionRunning_) {
        status.connectionState = NinjabrainApiConnectionState::Stopped;
        return status;
    }

    if (strongholdState_ == StreamState::Connected ||
        informationMessagesState_ == StreamState::Connected ||
        boatState_ == StreamState::Connected ||
        blindState_ == StreamState::Connected) {
        status.connectionState = NinjabrainApiConnectionState::Connected;
        return status;
    }

    if (!lastError_.empty()) {
        status.connectionState = NinjabrainApiConnectionState::Offline;
        status.error = lastError_;
        return status;
    }

    status.connectionState = NinjabrainApiConnectionState::Connecting;
    return status;
}

void NinjabrainApiConnectionTracker::MarkStreamConnected(StreamState& streamState) {
    streamState = StreamState::Connected;
    if (strongholdState_ == StreamState::Connected &&
        informationMessagesState_ == StreamState::Connected &&
        boatState_ == StreamState::Connected &&
        blindState_ == StreamState::Connected) {
        lastError_.clear();
    }
}

void NinjabrainApiConnectionTracker::MarkStreamDisconnected(StreamState& streamState, std::string error) {
    streamState = StreamState::Disconnected;
    lastError_ = std::move(error);
}

NinjabrainApiSession::NinjabrainApiSession(std::string apiBaseUrl, NinjabrainApiSessionCallbacks callbacks)
    : apiBaseUrl_(NormalizeNinjabrainApiBaseUrl(std::move(apiBaseUrl))),
      callbacks_(std::move(callbacks)) {
    strongholdThread_ = std::jthread([this](std::stop_token stopToken) {
        RunStream(
            stopToken,
            "stronghold",
            kStrongholdEventsPath,
            callbacks_.onStrongholdMessage,
            callbacks_.onStrongholdConnect,
            callbacks_.onStrongholdDisconnect);
    });
    informationMessagesThread_ = std::jthread([this](std::stop_token stopToken) {
        RunStream(
            stopToken,
            "information-messages",
            kInformationMessagesEventsPath,
            callbacks_.onInformationMessagesMessage,
            callbacks_.onInformationMessagesConnect,
            callbacks_.onInformationMessagesDisconnect);
    });
    boatThread_ = std::jthread([this](std::stop_token stopToken) {
        RunStream(
            stopToken,
            "boat",
            kBoatEventsPath,
            callbacks_.onBoatMessage,
            callbacks_.onBoatConnect,
            callbacks_.onBoatDisconnect);
    });
    blindThread_ = std::jthread([this](std::stop_token stopToken) {
        RunStream(
            stopToken,
            "blind",
            kBlindEventsPath,
            callbacks_.onBlindMessage,
            callbacks_.onBlindConnect,
            callbacks_.onBlindDisconnect);
    });
}

NinjabrainApiSession::~NinjabrainApiSession() {
    Stop();
}

void NinjabrainApiSession::Stop() {
    strongholdThread_.request_stop();
    informationMessagesThread_.request_stop();
    boatThread_.request_stop();
    blindThread_.request_stop();
}

void NinjabrainApiSession::RunStream(
    std::stop_token stopToken,
    const char* streamName,
    const char* path,
    const std::function<void(const std::string&)>& onMessage,
    const std::function<void()>& onConnect,
    const std::function<void(const std::string&)>& onDisconnect) const {
    httplib::Client client(apiBaseUrl_);
    client.set_keep_alive(true);
    const auto [connectionTimeoutSeconds, connectionTimeoutMicros] =
        SplitDurationForHttplibTimeout(std::chrono::duration_cast<std::chrono::milliseconds>(kNinjabrainConnectionTimeout));
    const auto [readTimeoutSeconds, readTimeoutMicros] =
        SplitDurationForHttplibTimeout(std::chrono::duration_cast<std::chrono::milliseconds>(kNinjabrainReadTimeout));
    const auto [writeTimeoutSeconds, writeTimeoutMicros] =
        SplitDurationForHttplibTimeout(std::chrono::duration_cast<std::chrono::milliseconds>(kNinjabrainWriteTimeout));
    client.set_connection_timeout(connectionTimeoutSeconds, connectionTimeoutMicros);
    client.set_read_timeout(readTimeoutSeconds, readTimeoutMicros);
    client.set_write_timeout(writeTimeoutSeconds, writeTimeoutMicros);

    const httplib::Headers headers = {
        { "Accept", "text/event-stream" },
        { "Cache-Control", "no-cache" },
    };

    httplib::sse::SSEClient sse(client, path, headers);
    sse.set_reconnect_interval(kNinjabrainReconnectIntervalMs);

    const std::string streamNameString = streamName;
    const long long connectionTimeoutMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(kNinjabrainConnectionTimeout).count();
    auto currentAttemptStartedAt = SteadyClock::now();
    auto outageStartedAt = currentAttemptStartedAt;
    auto connectedAt = SteadyClock::time_point{};
    bool outageInProgress = true;
    bool connected = false;
    bool disconnected = false;
    bool hasConnectedOnce = false;

    LogNinjabrainApiMessage(
        "NinjabrainBot API connecting " + streamNameString + " stream to " + apiBaseUrl_ + path +
        " (connection timeout " + std::to_string(connectionTimeoutMs) + " ms, reconnect delay " +
        std::to_string(kNinjabrainReconnectIntervalMs) + " ms).");

    sse.on_open([&, streamName = streamNameString]() {
        const auto now = SteadyClock::now();
        const long long outageDurationMs = DurationToMilliseconds(now - outageStartedAt);

        InvokeIfPresent(onConnect);
        connectedAt = now;
        connected = true;
        currentAttemptStartedAt = now;

        if (!hasConnectedOnce) {
            LogNinjabrainApiMessage(
                "NinjabrainBot API connected " + streamName + " stream after " +
                std::to_string(outageDurationMs) + " ms.");
            hasConnectedOnce = true;
        } else if (disconnected) {
            LogNinjabrainApiMessage(
                "NinjabrainBot API reconnected " + streamName + " stream after " +
                std::to_string(outageDurationMs) + " ms of downtime.");
            LogIfPresent(callbacks_.onLog, "Reconnected " + streamName + " stream.");
        }

        disconnected = false;
        outageInProgress = false;
    });
    sse.on_message([&](const httplib::sse::SSEMessage& message) {
        if (onMessage) { onMessage(message.data); }
    });
    sse.on_error([&, streamName = streamNameString](httplib::Error error) {
        if (stopToken.stop_requested() || error == httplib::Error::Canceled) { return; }

        const auto now = SteadyClock::now();
        if (IsQuietStreamTimeout(error)) {
            currentAttemptStartedAt = now;
            return;
        }

        const std::string errorString = httplib::to_string(error);
        const long long attemptDurationMs = DurationToMilliseconds(now - currentAttemptStartedAt);

        if (!outageInProgress) {
            outageStartedAt = now;
            outageInProgress = true;
        }

        const long long outageDurationMs = DurationToMilliseconds(now - outageStartedAt);

        InvokeIfPresent(onDisconnect, errorString);

        if (connected) {
            const long long connectedDurationMs = DurationToMilliseconds(now - connectedAt);
            LogNinjabrainApiMessage(
                "NinjabrainBot API lost " + streamName + " stream after " +
                std::to_string(connectedDurationMs) + " ms connected: " + errorString +
                ". Retrying in " + std::to_string(kNinjabrainReconnectIntervalMs) + " ms.");
            connected = false;
        } else if (error == httplib::Error::ConnectionTimeout) {
            LogNinjabrainApiMessage(
                "NinjabrainBot API " + streamName + " stream connection attempt timed out after " +
                std::to_string(attemptDurationMs) + " ms (total delay " +
                std::to_string(outageDurationMs) + " ms). Retrying in " +
                std::to_string(kNinjabrainReconnectIntervalMs) + " ms.");
        } else {
            const std::string attemptKind = hasConnectedOnce ? "reconnect" : "initial connection";
            LogNinjabrainApiMessage(
                "NinjabrainBot API " + streamName + " stream " + attemptKind + " failed after " +
                std::to_string(attemptDurationMs) + " ms (total delay " +
                std::to_string(outageDurationMs) + " ms): " + errorString + ". Retrying in " +
                std::to_string(kNinjabrainReconnectIntervalMs) + " ms.");
        }

        if (!disconnected) {
            LogIfPresent(
                callbacks_.onLog,
                "Lost " + streamName + " stream: " + errorString + ". Waiting for reconnect.");
            disconnected = true;
        }

        currentAttemptStartedAt = now;
    });

    std::stop_callback stopCallback(stopToken, [&]() {
        sse.stop();
        client.stop();
    });

    sse.start();
}
