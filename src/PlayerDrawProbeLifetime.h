#pragma once

#include <cstdint>

namespace PlayerDrawProbeLifetime
{
	enum class EndDisposition : std::uint8_t
	{
		kAlreadyEnded,
		kComplete,
		kQuarantine
	};

	/**
	 * Decide whether an End call owns the active thread-local observation.  A
	 * mismatch must never clear another thread's TLS slot: its independently
	 * retained engine/COM references instead become process-lifetime quarantine.
	 */
	[[nodiscard]] constexpr EndDisposition ClassifyEnd(
		bool sessionActive,
		std::uint32_t sessionThreadID,
		std::uint64_t sessionToken,
		std::uint32_t currentThreadID,
		bool observationActive,
		std::uint64_t observationToken) noexcept
	{
		if (!sessionActive)
			return EndDisposition::kAlreadyEnded;
		if (sessionToken != 0 && sessionThreadID == currentThreadID && observationActive &&
			observationToken == sessionToken) {
			return EndDisposition::kComplete;
		}
		return EndDisposition::kQuarantine;
	}
}
