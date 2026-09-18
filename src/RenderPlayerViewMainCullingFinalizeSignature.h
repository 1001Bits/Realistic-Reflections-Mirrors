#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace RenderPlayerViewMainCullingFinalizeSignature
{
	inline constexpr std::ptrdiff_t kFinalizeCallOffset = 0x2EC;
	// Skyrim VR 1.4.15: Main::RenderPlayerView (0x5B9330) calls
	// DrawWorld::MainAccum (0x1322130) at +0x248.  The masked pattern below
	// matches the VR bytes unchanged outside the displacement ranges.
	inline constexpr std::ptrdiff_t kVRFinalizeCallOffset = 0x248;
	// Skyrim 1.7.104: Main::RenderPlayerView (0x656E60) calls DrawWorld::MainAccum
	// (0x1538430) at +0x30A.  The masked pattern below is unchanged there; only
	// the call offset moves, and REL::Relocate cannot separate the two AE
	// runtimes, so callers must pass the exact runtime in.
	inline constexpr std::ptrdiff_t kAE17104FinalizeCallOffset = 0x30A;
	inline constexpr std::size_t kWindowSize = 31;

	[[nodiscard]] constexpr std::ptrdiff_t FinalizeCallOffset(
		const bool a_vr) noexcept
	{
		return a_vr ? kVRFinalizeCallOffset : kFinalizeCallOffset;
	}

	/**
	 * Exact-runtime selector.  `a_vr` wins over `a_ae17104`; with both false the
	 * SE 1.5.97 / AE 1.6.1170 offset is returned, which keeps the two-argument
	 * `FinalizeCallOffset` overload above behaving exactly as before.
	 */
	[[nodiscard]] constexpr std::ptrdiff_t FinalizeCallOffsetFor(
		const bool a_vr,
		const bool a_ae17104) noexcept
	{
		if (a_vr)
			return kVRFinalizeCallOffset;
		return a_ae17104 ? kAE17104FinalizeCallOffset : kFinalizeCallOffset;
	}

	// SE 1.5.97 at 0x5B1B4C and AE 1.6.1170 at 0x64479C. The two
	// instruction streams differ only in their three rel32/RIP displacements.
	inline constexpr std::array<std::uint8_t, kWindowSize> kPattern{
		0xE8, 0x0F, 0x0F, 0xD3, 0x00,
		0x80, 0x3D, 0x98, 0x13, 0xC8, 0x02, 0x00,
		0x74, 0x40, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00,
		0x48, 0x8B, 0xD6, 0x48, 0x8D, 0x0D,
		0x26, 0xAD, 0xA7, 0x02, 0xE8
	};

	[[nodiscard]] constexpr bool IsDisplacementByte(std::size_t a_index) noexcept
	{
		return (a_index >= 1 && a_index <= 4) ||
			(a_index >= 7 && a_index <= 10) ||
			(a_index >= 26 && a_index <= 29);
	}

	[[nodiscard]] constexpr bool Matches(
		std::span<const std::uint8_t> a_window) noexcept
	{
		if (a_window.size() != kWindowSize)
			return false;
		for (std::size_t i = 0; i < kWindowSize; ++i) {
			if (!IsDisplacementByte(i) && a_window[i] != kPattern[i])
				return false;
		}
		return true;
	}
}
