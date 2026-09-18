#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace RenderPlayerViewSceneListFenceSignature
{
	inline constexpr std::size_t kWindowSize = 0x61;
	inline constexpr std::size_t kRenderWaterEffectsCallOffset = 0x03;
	inline constexpr std::size_t kJobListFinishCallOffset = 0x0F;
	inline constexpr std::size_t kProcessAllQueuedLightsCallOffset = 0x25;
	inline constexpr std::size_t kCalculateShadowCasterLightsCallOffset = 0x55;

	// SE RenderPlayerView +[0x16F,0x1D0). Every non-displacement byte is
	// identical in AE 1.6.1170, and in Skyrim 1.7.104 the window sits at the
	// same +0x16F with the same four call offsets (0x03 / 0x0F / 0x25 / 0x55),
	// so 1.7.104 deliberately shares the flat layout and pattern below. The wide
	// window proves that the hook is the scene-list fence between
	// RenderWaterEffects and ProcessAllQueuedLights, rather than another call to
	// the same JobList::Finish function.
	inline constexpr auto kSEPattern = std::to_array<std::uint8_t>({
		0x49, 0x8B, 0xCC, 0xE8, 0x29, 0x09, 0x00, 0x00,
		0x48, 0x8B, 0x0D, 0x22, 0x18, 0xC8, 0x02, 0xE8,
		0xBD, 0x04, 0x68, 0x00, 0x83, 0xA7, 0xF4, 0x00,
		0x00, 0x00, 0xFE, 0x48, 0x8B, 0x0D, 0x7F, 0x1A,
		0xC8, 0x02, 0x48, 0x8B, 0x09, 0xE8, 0xA7, 0x8D,
		0xD0, 0x00, 0x83, 0x8F, 0xF4, 0x00, 0x00, 0x00,
		0x01, 0x32, 0xDB, 0x45, 0x84, 0xF6, 0x74, 0x1A,
		0x4D, 0x85, 0xFF, 0x74, 0x15, 0x49, 0x8B, 0x47,
		0x18, 0x0F, 0xB6, 0x98, 0xF4, 0x00, 0x00, 0x00,
		0x80, 0xE3, 0x01, 0x83, 0x88, 0xF4, 0x00, 0x00,
		0x00, 0x01, 0x48, 0x8B, 0xCE, 0xE8, 0x37, 0x0C,
		0xD3, 0x00, 0x45, 0x84, 0xF6, 0x74, 0x1D, 0x4D,
		0x85
	});
	static_assert(kSEPattern.size() == kWindowSize);

	[[nodiscard]] constexpr bool IsDisplacementByte(std::size_t a_index) noexcept
	{
		return (a_index >= 0x04 && a_index < 0x08) ||
			(a_index >= 0x0B && a_index < 0x0F) ||
			(a_index >= 0x10 && a_index < 0x14) ||
			(a_index >= 0x1E && a_index < 0x22) ||
			(a_index >= 0x26 && a_index < 0x2A) ||
			(a_index >= 0x56 && a_index < 0x5A);
	}

	[[nodiscard]] constexpr bool Matches(
		std::span<const std::uint8_t> a_window) noexcept
	{
		if (a_window.size() != kWindowSize)
			return false;
		for (std::size_t index = 0; index < kWindowSize; ++index) {
			if (!IsDisplacementByte(index) && a_window[index] != kSEPattern[index])
				return false;
		}
		return true;
	}

	// Skyrim VR 1.4.15 (RenderPlayerView 0x5B9330): the same fence sits at
	// +[0x197,0x1F8).  VR drops the two NiAVObject flag read-modify-writes that
	// flat keeps between the calls, and the flag byte moved to +0x10C, so the
	// window carries its own pattern and call offsets:
	//   +0x03 RenderWaterEffects (0x5B9C00), +0x0F JobList::Finish (0xC76D80),
	//   +0x1E ProcessAllQueuedLights (0x12F81D0),
	//   +0x47 CalculateShadowCasterLights (0x1321D80).
	inline constexpr std::size_t kFlatWindowOffsetFromRenderPlayerView = 0x16F;
	inline constexpr std::size_t kVRWindowOffsetFromRenderPlayerView = 0x197;
	inline constexpr std::size_t kVRRenderWaterEffectsCallOffset = 0x03;
	inline constexpr std::size_t kVRJobListFinishCallOffset = 0x0F;
	inline constexpr std::size_t kVRProcessAllQueuedLightsCallOffset = 0x1E;
	inline constexpr std::size_t kVRCalculateShadowCasterLightsCallOffset = 0x47;

	inline constexpr auto kVRPattern = std::to_array<std::uint8_t>({
		0x49, 0x8B, 0xCD, 0xE8, 0x31, 0x07, 0x00, 0x00,
		0x48, 0x8B, 0x0D, 0xCA, 0xC2, 0xEC, 0x02, 0xE8,
		0xA5, 0xD8, 0x6B, 0x00, 0x48, 0x8B, 0x0D, 0x0E,
		0xC6, 0xEC, 0x02, 0x48, 0x8B, 0x09, 0xE8, 0xE6,
		0xEC, 0xD3, 0x00, 0x32, 0xDB, 0x45, 0x84, 0xFF,
		0x74, 0x1A, 0x48, 0x85, 0xFF, 0x74, 0x15, 0x48,
		0x8B, 0x47, 0x18, 0x0F, 0xB6, 0x98, 0x0C, 0x01,
		0x00, 0x00, 0x80, 0xE3, 0x01, 0x83, 0x88, 0x0C,
		0x01, 0x00, 0x00, 0x01, 0x48, 0x8B, 0xCE, 0xE8,
		0x6D, 0x88, 0xD6, 0x00, 0x45, 0x84, 0xFF, 0x74,
		0x1D, 0x48, 0x85, 0xFF, 0x74, 0x18, 0x48, 0x8B,
		0x47, 0x18, 0x84, 0xDB, 0x74, 0x09, 0x83, 0x88,
		0x0C
	});
	static_assert(kVRPattern.size() == kWindowSize);

	[[nodiscard]] constexpr bool IsVRDisplacementByte(std::size_t a_index) noexcept
	{
		return (a_index >= 0x04 && a_index < 0x08) ||
			(a_index >= 0x0B && a_index < 0x0F) ||
			(a_index >= 0x10 && a_index < 0x14) ||
			(a_index >= 0x17 && a_index < 0x1B) ||
			(a_index >= 0x1F && a_index < 0x23) ||
			(a_index >= 0x48 && a_index < 0x4C);
	}

	[[nodiscard]] constexpr bool MatchesVR(
		std::span<const std::uint8_t> a_window) noexcept
	{
		if (a_window.size() != kWindowSize)
			return false;
		for (std::size_t index = 0; index < kWindowSize; ++index) {
			if (!IsVRDisplacementByte(index) && a_window[index] != kVRPattern[index])
				return false;
		}
		return true;
	}

	struct Layout
	{
		std::size_t windowOffset{ 0 };
		std::size_t water{ 0 };
		std::size_t finish{ 0 };
		std::size_t lights{ 0 };
		std::size_t shadowCaster{ 0 };
	};

	[[nodiscard]] constexpr Layout SelectLayout(const bool a_vr) noexcept
	{
		if (a_vr) {
			return Layout{
				.windowOffset = kVRWindowOffsetFromRenderPlayerView,
				.water = kVRRenderWaterEffectsCallOffset,
				.finish = kVRJobListFinishCallOffset,
				.lights = kVRProcessAllQueuedLightsCallOffset,
				.shadowCaster = kVRCalculateShadowCasterLightsCallOffset
			};
		}
		return Layout{
			.windowOffset = kFlatWindowOffsetFromRenderPlayerView,
			.water = kRenderWaterEffectsCallOffset,
			.finish = kJobListFinishCallOffset,
			.lights = kProcessAllQueuedLightsCallOffset,
			.shadowCaster = kCalculateShadowCasterLightsCallOffset
		};
	}

	[[nodiscard]] constexpr bool MatchesFor(
		const bool a_vr,
		std::span<const std::uint8_t> a_window) noexcept
	{
		return a_vr ? MatchesVR(a_window) : Matches(a_window);
	}
}
