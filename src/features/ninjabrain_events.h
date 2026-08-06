#pragma once

#include "features/ninjabrain_api_constants.h"
#include "features/ninjabrain_data.h"

#include <functional>
#include <string>
#include <string_view>

using NinjabrainLogCallback = std::function<void(const std::string&)>;

void ClearNinjabrainStrongholdData(NinjabrainData& data);
void ClearNinjabrainInformationMessagesData(NinjabrainData& data);
void ClearNinjabrainBoatData(NinjabrainData& data);
void ClearNinjabrainBlindData(NinjabrainData& data);

void ApplyNinjabrainBoatEvent(
    const std::string& payload,
    NinjabrainData& data,
    const NinjabrainLogCallback& logError = {});

void ApplyNinjabrainStrongholdEvent(
    const std::string& payload,
    NinjabrainData& data,
    const NinjabrainLogCallback& logError = {});

void ApplyNinjabrainInformationMessagesEvent(
    const std::string& payload,
    NinjabrainData& data,
    const NinjabrainLogCallback& logError = {});

void ApplyNinjabrainBlindEvent(
    const std::string& payload,
    NinjabrainData& data,
    const NinjabrainLogCallback& logError = {});

std::string NormalizeNinjabrainApiBaseUrl(
    std::string apiBaseUrl,
    std::string_view defaultBaseUrl = kDefaultNinjabrainApiBaseUrl);
