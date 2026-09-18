#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace FirstPersonViewCallSiteSignature
{
	enum class Runtime : std::uint8_t
	{
		kSE1597,
		kAE161170,
		// Skyrim 1.7.104 shares the AE Address Library ID space but moves the
		// call: RenderPlayerView+0x971 instead of +0x954.
		kAE17104,
		// GOG 1.6.1179 keeps the call at RenderPlayerView+0x954, but the other
		// RIP-relative displacements in the 49-byte window differ from Steam
		// 1.6.1170, so MatchesMasked needs its own bytes.
		kAE161179,
		kVR1415
	};

	// Main::RenderPlayerView is RelocationID (35560,36559).  The native
	// RenderFirstPersonView target is RelocationID (100411,107129).
	// Skyrim VR 1.4.15: RenderPlayerView (0x5B9330) calls the live VR
	// first-person renderer 0x13244E0 at +0x7FA.  The VR Address Library maps
	// ID 100411 to 0x13218C0, a never-called flat-shaped copy, so the VR target
	// must be pinned as REL::VariantID(100411, 107129, 0x13244E0).
	inline constexpr std::size_t kWindowSize = 49;
	inline constexpr std::size_t kCallOffsetInWindow = 21;
	inline constexpr std::size_t kRel32OffsetInWindow = 22;
	inline constexpr std::size_t kRel32Size = 4;
	inline constexpr std::size_t kSECallOffsetFromCaller = 0x944;
	inline constexpr std::size_t kAECallOffsetFromCaller = 0x954;
	inline constexpr std::size_t kAE17104CallOffsetFromCaller = 0x971;
	inline constexpr std::size_t kVRCallOffsetFromCaller = 0x7FA;
	inline constexpr std::uintptr_t kVRRenderFirstPersonViewRVA = 0x13244E0;
	inline constexpr std::size_t kSEWindowOffsetFromCaller =
		kSECallOffsetFromCaller - kCallOffsetInWindow;
	inline constexpr std::size_t kAEWindowOffsetFromCaller =
		kAECallOffsetFromCaller - kCallOffsetInWindow;
	inline constexpr std::size_t kAE17104WindowOffsetFromCaller =
		kAE17104CallOffsetFromCaller - kCallOffsetInWindow;
	inline constexpr std::size_t kVRWindowOffsetFromCaller =
		kVRCallOffsetFromCaller - kCallOffsetInWindow;

	inline constexpr std::array<std::uint8_t, kWindowSize> kSEWindow{
		0x48, 0x8B, 0x05, 0x8A, 0xC9, 0x90, 0x01, 0x83,
		0xB8, 0x60, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x97,
		0xC1, 0x40, 0x0F, 0xB6, 0xD7, 0xE8, 0xF7, 0xFF,
		0xD2, 0x00, 0x83, 0x3D, 0x98, 0x5D, 0xA7, 0x02,
		0x00, 0x74, 0x0E, 0x44, 0x89, 0x3D, 0x8F, 0x5D,
		0xA7, 0x02, 0x83, 0x0D, 0xF0, 0x5C, 0xA7, 0x02,
		0x10
	};

	inline constexpr std::array<std::uint8_t, kWindowSize> kAEWindow{
		0x48, 0x8B, 0x05, 0x0A, 0x1C, 0xAB, 0x01, 0x83,
		0xB8, 0x60, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x97,
		0xC1, 0x40, 0x0F, 0xB6, 0xD7, 0xE8, 0xC7, 0x68,
		0xE8, 0x00, 0x83, 0x3D, 0xF8, 0x5D, 0x9E, 0x01,
		0x00, 0x74, 0x0E, 0x44, 0x89, 0x3D, 0xEF, 0x5D,
		0x9E, 0x01, 0x83, 0x0D, 0x50, 0x5D, 0x9E, 0x01,
		0x10
	};

	// Skyrim 1.7.104 RenderPlayerView (0x656E60) +0x95C.  The instruction stream
	// is identical to AE 1.6.1170; only the four RIP-relative displacements move.
	// The masked rel32 at 22..25 resolves to RenderFirstPersonView 0x1537B10.
	inline constexpr std::array<std::uint8_t, kWindowSize> kAE17104Window{
		0x48, 0x8B, 0x05, 0xFD, 0x6D, 0xB4, 0x01, 0x83,
		0xB8, 0x60, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x97,
		0xC1, 0x40, 0x0F, 0xB6, 0xD7, 0xE8, 0x3A, 0x03,
		0xEE, 0x00, 0x83, 0x3D, 0xBB, 0x84, 0xA7, 0x01,
		0x00, 0x74, 0x0E, 0x44, 0x89, 0x3D, 0xB2, 0x84,
		0xA7, 0x01, 0x83, 0x0D, 0x13, 0x84, 0xA7, 0x01,
		0x10
	};

	// GOG 1.6.1179 RenderPlayerView (0x646710) +0x93F.  Call remains at +0x954
	// targeting RenderFirstPersonView 0x14CC740; only the RIP-relatives move.
	inline constexpr std::array<std::uint8_t, kWindowSize> kAE161179Window{
		0x48, 0x8B, 0x05, 0xAA, 0x0D, 0xAB, 0x01, 0x83,
		0xB8, 0x60, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x97,
		0xC1, 0x40, 0x0F, 0xB6, 0xD7, 0xE8, 0xD7, 0x56,
		0xE8, 0x00, 0x83, 0x3D, 0x98, 0x4B, 0x9E, 0x01,
		0x00, 0x74, 0x0E, 0x44, 0x89, 0x3D, 0x8F, 0x4B,
		0x9E, 0x01, 0x83, 0x0D, 0xF0, 0x4A, 0x9E, 0x01,
		0x10
	};

	// VR RenderPlayerView +0x7E5..+0x816: `... call SetJobTaskEvent(5);
	// call 0x53BEA0; call RenderFirstPersonView; mov ecx,6; call ...`.
	inline constexpr std::array<std::uint8_t, kWindowSize> kVRWindow{
		0x05, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0D, 0xC0,
		0xBE, 0xEC, 0x02, 0xE8, 0x2B, 0x1B, 0xD5, 0x00,
		0xE8, 0x76, 0x23, 0xF8, 0xFF, 0xE8, 0xB1, 0xA9,
		0xD6, 0x00, 0xB9, 0x06, 0x00, 0x00, 0x00, 0xE8,
		0x27, 0x37, 0x6C, 0x00, 0x33, 0xC9, 0xE8, 0x20,
		0xEE, 0x08, 0x00, 0xB9, 0x07, 0x00, 0x00, 0x00,
		0xE8
	};

	[[nodiscard]] constexpr const std::array<std::uint8_t, kWindowSize>&
	ExpectedWindow(const Runtime runtime) noexcept
	{
		switch (runtime) {
		case Runtime::kSE1597:
			return kSEWindow;
		case Runtime::kAE17104:
			return kAE17104Window;
		case Runtime::kAE161179:
			return kAE161179Window;
		case Runtime::kVR1415:
			return kVRWindow;
		case Runtime::kAE161170:
		default:
			return kAEWindow;
		}
	}

	[[nodiscard]] constexpr std::size_t CallOffsetFromCaller(
		const Runtime runtime) noexcept
	{
		switch (runtime) {
		case Runtime::kSE1597:
			return kSECallOffsetFromCaller;
		case Runtime::kAE17104:
			return kAE17104CallOffsetFromCaller;
		case Runtime::kAE161179:
		case Runtime::kAE161170:
			return kAECallOffsetFromCaller;
		case Runtime::kVR1415:
			return kVRCallOffsetFromCaller;
		default:
			return kAECallOffsetFromCaller;
		}
	}

	[[nodiscard]] constexpr std::size_t WindowOffsetFromCaller(
		const Runtime runtime) noexcept
	{
		return CallOffsetFromCaller(runtime) - kCallOffsetInWindow;
	}

	/**
	 * Match every byte of the frozen runtime window except the call's rel32.
	 * The rel32 is decoded independently.  The installing owner then admits only
	 * the exact native target or an explicitly verified peer chain.
	 */
	[[nodiscard]] constexpr bool MatchesMasked(
		const std::span<const std::uint8_t> window,
		const Runtime runtime) noexcept
	{
		if (window.size() != kWindowSize)
			return false;
		const auto& expected = ExpectedWindow(runtime);
		for (std::size_t index = 0; index < window.size(); ++index) {
			if (index >= kRel32OffsetInWindow &&
				index < kRel32OffsetInWindow + kRel32Size) {
				continue;
			}
			if (window[index] != expected[index])
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool DecodeRel32Target(
		const std::uintptr_t windowAddress,
		const std::span<const std::uint8_t> window,
		std::uintptr_t& target) noexcept
	{
		target = 0;
		if (window.size() != kWindowSize ||
			window[kCallOffsetInWindow] != 0xE8)
			return false;

		const std::uint32_t raw =
			static_cast<std::uint32_t>(window[kRel32OffsetInWindow]) |
			(static_cast<std::uint32_t>(window[kRel32OffsetInWindow + 1]) << 8) |
			(static_cast<std::uint32_t>(window[kRel32OffsetInWindow + 2]) << 16) |
			(static_cast<std::uint32_t>(window[kRel32OffsetInWindow + 3]) << 24);
		const auto displacement = std::bit_cast<std::int32_t>(raw);
		constexpr std::size_t kInstructionSize = 5;
		if (windowAddress >
				(std::numeric_limits<std::uintptr_t>::max)() -
				kCallOffsetInWindow - kInstructionSize) {
			return false;
		}
		const auto nextInstruction =
			windowAddress + kCallOffsetInWindow + kInstructionSize;
		if (displacement < 0) {
			const auto magnitude = static_cast<std::uint64_t>(
				-static_cast<std::int64_t>(displacement));
			if (magnitude > nextInstruction)
				return false;
			target = nextInstruction - static_cast<std::uintptr_t>(magnitude);
		} else {
			const auto positive = static_cast<std::uintptr_t>(displacement);
			if (nextInstruction >
					(std::numeric_limits<std::uintptr_t>::max)() - positive) {
				return false;
			}
			target = nextInstruction + positive;
		}
		return target != 0;
	}

	[[nodiscard]] constexpr bool MatchesExactNativeCall(
		const std::uintptr_t windowAddress,
		const std::span<const std::uint8_t> window,
		const Runtime runtime,
		const std::uintptr_t nativeTarget) noexcept
	{
		std::uintptr_t decodedTarget = 0;
		return nativeTarget != 0 && MatchesMasked(window, runtime) &&
			DecodeRel32Target(windowAddress, window, decodedTarget) &&
			decodedTarget == nativeTarget;
	}
}
