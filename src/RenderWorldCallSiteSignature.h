#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace RenderWorldCallSiteSignature
{
	inline constexpr std::size_t kCallOffset = 25;
	inline constexpr std::size_t kWindowSize = 38;

	/**
	 * Validate the SE/AE Main::RenderPlayerView call to Main::RenderWorld.
	 * RIP-relative globals and rel32 call targets deliberately stay variable so
	 * the same matcher accepts both runtimes and an already-chained peer such as
	 * Community Shaders.
	 */
	[[nodiscard]] constexpr bool Matches(
		std::span<const std::uint8_t> a_window) noexcept
	{
		return a_window.size() == kWindowSize &&
			a_window[0] == 0xF3 && a_window[1] == 0x0F &&
			a_window[2] == 0x11 && a_window[3] == 0x05 &&
			a_window[8] == 0x48 && a_window[9] == 0x8B &&
			a_window[10] == 0x05 &&
			a_window[15] == 0x83 && a_window[16] == 0xB8 &&
			a_window[17] == 0x60 && a_window[18] == 0x01 &&
			a_window[19] == 0x00 && a_window[20] == 0x00 &&
			a_window[21] == 0x00 &&
			a_window[22] == 0x0F && a_window[23] == 0x97 &&
			a_window[24] == 0xC1 &&
			a_window[kCallOffset] == 0xE8 &&
			a_window[30] == 0x48 && a_window[31] == 0x8B &&
			a_window[32] == 0x0D && a_window[37] == 0xE8;
	}
}
