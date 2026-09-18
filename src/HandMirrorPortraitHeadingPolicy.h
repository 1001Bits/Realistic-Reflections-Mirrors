#pragma once

#include <cmath>
#include <cstdint>

namespace HandMirrorPortraitHeadingPolicy
{
	inline constexpr float kCosImmediateHeadingDegrees = 0.939692621F;  // cos(20 deg)
	inline constexpr float kCosConfirmedHeadingDegrees = 0.965925826F;  // cos(15 deg)

	enum class Action : std::uint8_t
	{
		kAcceptImmediate,
		kConfirmTransition,
		kHoldPendingAndRejectCapture
	};

	[[nodiscard]] inline Action SelectAction(
		const float acceptedHeadingDot,
		const bool pendingHeadingValid,
		const float pendingHeadingDot) noexcept
	{
		if (std::isfinite(acceptedHeadingDot) &&
			acceptedHeadingDot >= kCosImmediateHeadingDegrees) {
			return Action::kAcceptImmediate;
		}
		if (pendingHeadingValid && std::isfinite(pendingHeadingDot) &&
			pendingHeadingDot >= kCosConfirmedHeadingDegrees) {
			return Action::kConfirmTransition;
		}
		return Action::kHoldPendingAndRejectCapture;
	}
}
