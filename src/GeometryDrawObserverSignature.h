#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace GeometryDrawObserverSignature
{
	enum class Runtime : std::uint8_t
	{
		kSE1597,
		kAE161170,
		// Skyrim 1.7.104 needs no new bytes: the generic geometry draw entry and
		// its switch-bound displacement (40 07 00 00) are byte-identical to AE
		// 1.6.1170, and ID 107637 resolves through the shared AE library.
		kAE17104,
		// GOG 1.6.1179: ID 107637 -> 0x14F3B40; Ghidra Combined confirmed the AE
		// prefix and (40 07 00 00) displacement.
		kAE161179,
		kVR1415
	};

	// 36 bytes cover the longest (VR) prefix plus its displacement; the flat
	// matchers only inspect their first 32 bytes.
	inline constexpr std::size_t kWindowSize = 36;

	// Skyrim VR 1.4.15 generic geometry draw (0x1348390, VR Address Library
	// ID 100847).  VR spills rdi (`push rdi`), keeps a 0x68 frame and reads the
	// pass type byte at +0x190 (flat +0x150), so the prefix is runtime-specific.
	inline constexpr std::array<std::uint8_t, 31> kVRPrefix{
		0x4C, 0x8B, 0xDC, 0x57, 0x41, 0x56, 0x48, 0x83,
		0xEC, 0x68, 0x4C, 0x8B, 0x71, 0x10, 0x48, 0x8B,
		0xF9, 0x41, 0x0F, 0xB6, 0x86, 0x90, 0x01, 0x00,
		0x00, 0xFF, 0xC8, 0x83, 0xF8, 0x0C, 0x0F
	};
	inline constexpr std::array<std::uint8_t, 5> kVRBranchDisplacement{
		0x87, 0x6C, 0x07, 0x00, 0x00
	};

	inline constexpr std::array<std::uint8_t, 28> kCommonPrefix{
		0x4C, 0x8B, 0xDC, 0x41, 0x56, 0x48, 0x83, 0xEC,
		0x70, 0x4C, 0x8B, 0x71, 0x10, 0x41, 0x0F, 0xB6,
		0x86, 0x50, 0x01, 0x00, 0x00, 0xFF, 0xC8, 0x83,
		0xF8, 0x0C, 0x0F, 0x87
	};

	inline constexpr std::array<std::uint8_t, 4> kSEBranchDisplacement{
		0x5F, 0x07, 0x00, 0x00
	};

	inline constexpr std::array<std::uint8_t, 4> kAEBranchDisplacement{
		0x40, 0x07, 0x00, 0x00
	};

	/**
	 * Match the complete verified entry window of the generic geometry draw
	 * dispatcher (RelocationID 100847/107637).  The final rel32 differs because
	 * the SE and AE switch bodies have different extents; it is intentionally
	 * runtime-specific rather than masked.
	 */
	[[nodiscard]] constexpr bool Matches(
		std::span<const std::uint8_t> window,
		Runtime runtime) noexcept
	{
		if (window.size() != kWindowSize)
			return false;
		if (runtime == Runtime::kVR1415) {
			for (std::size_t index = 0; index < kVRPrefix.size(); ++index) {
				if (window[index] != kVRPrefix[index])
					return false;
			}
			for (std::size_t index = 0; index < kVRBranchDisplacement.size(); ++index) {
				if (window[kVRPrefix.size() + index] != kVRBranchDisplacement[index])
					return false;
			}
			return true;
		}
		for (std::size_t index = 0; index < kCommonPrefix.size(); ++index) {
			if (window[index] != kCommonPrefix[index])
				return false;
		}

		// AE 1.6.1170, 1.6.1179, and 1.7.104 take kAEBranchDisplacement; each is
		// verified to carry the same (40 07 00 00) bound, so they share the array
		// deliberately rather than by falling through a default.
		const auto& displacement = runtime == Runtime::kSE1597 ?
			kSEBranchDisplacement : kAEBranchDisplacement;
		for (std::size_t index = 0; index < displacement.size(); ++index) {
			if (window[kCommonPrefix.size() + index] != displacement[index])
				return false;
		}
		return true;
	}
}
