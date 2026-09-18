#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MirrorPaneDeliverySignature
{
	inline constexpr std::size_t kCallOffset = 16;
	inline constexpr std::size_t kWindowSize = 25;
	inline constexpr std::size_t kFiveByteBranchStubSize = 14;

	inline constexpr std::array<std::uint8_t, kCallOffset> kPrefix{
		0x48, 0x8B, 0x8B, 0x30, 0x01, 0x00, 0x00, 0xBA,
		0x6D, 0x00, 0x00, 0x5C, 0x44, 0x8D, 0x42, 0x03
	};
	inline constexpr std::array<std::uint8_t, 4> kSuffix{
		0x84, 0xC0, 0x0F, 0x84
	};

	// Call-site offsets of the query call inside BSShaderAccumulator::
	// FinishAccumulating: SE 1.5.97 +0x3C5, AE 1.6.1170 +0x3B4, VR 1.4.15
	// +0x3D2 (0x13093A0).  The VR prefix differs from flat in exactly one
	// field: the accumulator keeps its BSBatchRenderer at +0x158 (flat +0x130).
	// Skyrim 1.7.104 keeps the AE call offset +0x3B4 and the AE prefix/suffix
	// bytes exactly (verified 2026-09-05), so it needs no entry of its own and
	// deliberately reuses kAECallOffsetFromFinishAccumulating.
	inline constexpr std::ptrdiff_t kSECallOffsetFromFinishAccumulating = 0x3C5;
	inline constexpr std::ptrdiff_t kAECallOffsetFromFinishAccumulating = 0x3B4;
	inline constexpr std::ptrdiff_t kVRCallOffsetFromFinishAccumulating = 0x3D2;
	inline constexpr std::array<std::uint8_t, kCallOffset> kVRPrefix{
		0x48, 0x8B, 0x8B, 0x58, 0x01, 0x00, 0x00, 0xBA,
		0x6D, 0x00, 0x00, 0x5C, 0x44, 0x8D, 0x42, 0x03
	};

	/**
	 * Validate the instruction bytes around the SE/AE post-decals, pre-water query call. The rel32 call target
	 * and following conditional-branch displacement deliberately remain variable so an existing call-chain peer
	 * can still be preserved.
	 */
	[[nodiscard]] constexpr bool Matches(std::span<const std::uint8_t> window) noexcept
	{
		return window.size() == kWindowSize &&
			std::equal(kPrefix.begin(), kPrefix.end(), window.begin()) &&
			window[kCallOffset] == 0xE8 &&
			std::equal(
				kSuffix.begin(), kSuffix.end(), window.begin() + kCallOffset + 5);
	}

	[[nodiscard]] constexpr bool MatchesVR(std::span<const std::uint8_t> window) noexcept
	{
		return window.size() == kWindowSize &&
			std::equal(kVRPrefix.begin(), kVRPrefix.end(), window.begin()) &&
			window[kCallOffset] == 0xE8 &&
			std::equal(
				kSuffix.begin(), kSuffix.end(), window.begin() + kCallOffset + 5);
	}

	[[nodiscard]] constexpr bool MatchesFor(
		const bool a_vr,
		std::span<const std::uint8_t> window) noexcept
	{
		return a_vr ? MatchesVR(window) : Matches(window);
	}

	/** Validate the exact 14-byte target emitted by the pinned CommonLibSSE-NG write_call<5>. */
	[[nodiscard]] constexpr bool MatchesFiveByteBranchStub(
		std::span<const std::uint8_t> stub,
		std::uint64_t expectedDestination) noexcept
	{
		if (stub.size() != kFiveByteBranchStubSize || stub[0] != 0xFF || stub[1] != 0x25 ||
			stub[2] != 0 || stub[3] != 0 || stub[4] != 0 || stub[5] != 0) {
			return false;
		}

		std::uint64_t destination = 0;
		for (std::size_t i = 0; i < sizeof(destination); ++i)
			destination |= static_cast<std::uint64_t>(stub[6 + i]) << (i * 8);
		return destination == expectedDestination;
	}
}
