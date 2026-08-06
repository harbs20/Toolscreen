#include "ninjabrain_data.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace {

std::shared_ptr<const NinjabrainData> g_ninjabrainDataSnapshot = std::make_shared<const NinjabrainData>();
std::mutex g_ninjabrainDataWriteMutex;

} // namespace

std::shared_ptr<const NinjabrainData> GetNinjabrainDataSnapshot() {
	return std::atomic_load_explicit(&g_ninjabrainDataSnapshot, std::memory_order_acquire);
}

void PublishNinjabrainData(NinjabrainData data) {
	std::lock_guard<std::mutex> lock(g_ninjabrainDataWriteMutex);
	if (data.lastUpdateTime == std::chrono::steady_clock::time_point{}) {
		data.lastUpdateTime = std::chrono::steady_clock::now();
	}
	std::atomic_store_explicit(
		&g_ninjabrainDataSnapshot,
		std::make_shared<const NinjabrainData>(std::move(data)),
		std::memory_order_release);
}

void ModifyNinjabrainData(const std::function<void(NinjabrainData&)>& modifier) {
	std::lock_guard<std::mutex> lock(g_ninjabrainDataWriteMutex);

	const auto current = std::atomic_load_explicit(&g_ninjabrainDataSnapshot, std::memory_order_acquire);
	auto next = std::make_shared<NinjabrainData>(current ? *current : NinjabrainData{});
	modifier(*next);
	next->lastUpdateTime = std::chrono::steady_clock::now();

	std::atomic_store_explicit(
		&g_ninjabrainDataSnapshot,
		std::shared_ptr<const NinjabrainData>(std::move(next)),
		std::memory_order_release);
}
