#pragma once

#include "HandMirrorSettings.h"

#include <cstdint>

namespace HandMirrorWheelZoomPolicy
{
	inline constexpr std::uint32_t kWheelUp = 8;
	inline constexpr std::uint32_t kWheelDown = 9;
	inline constexpr float kZoomStep = 0.05F;

	struct Context
	{
		bool enabled{ false };
		bool gameplay{ false };
		bool firstPerson{ false };
		bool mirrorEquipped{ false };
		bool raised{ false };
	};

	struct Result
	{
		bool consume{ false };
		float zoom{ HandMirrorSettings::kDefaultPortraitZoom };
	};

	[[nodiscard]] constexpr bool IsWheel(const bool mouse,
		const std::uint32_t key) noexcept
	{
		return mouse && (key == kWheelUp || key == kWheelDown);
	}

	[[nodiscard]] inline Result Evaluate(const Context& context, const bool mouse,
		const std::uint32_t key, const bool down, const float zoom) noexcept
	{
		if (!context.enabled || !context.gameplay || !context.firstPerson ||
			!context.mirrorEquipped || !context.raised || !IsWheel(mouse, key))
			return { false, zoom };
		// Swallow releases and limit hits too: they must never reach POV zoom.
		return { true, HandMirrorSettings::ClampPortraitZoom(zoom +
			(down ? (key == kWheelUp ? kZoomStep : -kZoomStep) : 0.0F)) };
	}
}
