#pragma once

#include <atomic>
#include <cstdint>

namespace MirrorCaptureWorkPolicy
{
	// Set once at InputLoaded from the exact-empty performance test marker.
	inline std::atomic_bool enabled{ false };
	inline constexpr std::int64_t kIdleRetentionMilliseconds = 5000;

	[[nodiscard]] constexpr bool CaptureDemand(bool visibilityObserved, bool recentlyVisible) noexcept
	{
		return !visibilityObserved || recentlyVisible;
	}

	[[nodiscard]] constexpr bool Retain(bool visibilityObserved, bool recentlyVisible,
		std::int64_t now, std::int64_t lastVisible) noexcept
	{
		return CaptureDemand(visibilityObserved, recentlyVisible) ||
			(lastVisible > 0 && now >= lastVisible &&
				now - lastVisible <= kIdleRetentionMilliseconds);
	}
}
