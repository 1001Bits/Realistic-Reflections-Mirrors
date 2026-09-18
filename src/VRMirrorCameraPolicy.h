#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace VRMirrorCameraPolicy
{
	// VR 1.4.15: native SetDirtyStates (0xDC2E70) reads the active BYTE
	// at shadow-state +0x958, caches it at +0x880, and uploads it as a float.
	// +0x881 is the upload-dirty BYTE, not padding in a drawStereo float.
	// Private mono reflection passes temporarily clear +0x958 and restore it.
	inline constexpr std::size_t kStereoCached = 0x880;
	inline constexpr std::size_t kStereoDirty = 0x881;
	inline constexpr std::size_t kStereoActive = 0x958;
	inline constexpr std::size_t kStereoStateExtent = kStereoActive + 1;
	inline constexpr std::uint32_t kInputLayoutDirty = 0x400;

	// The caller verifies the exact runtime and native instruction windows.
	// Arm before mutation so a guarded native fault still leaves a cleanup owner.
	// Never restore the cached byte: it describes what the GPU last received.
	class StereoLease
	{
	public:
		[[nodiscard]] bool Armed() const noexcept { return owner != nullptr; }
		[[nodiscard]] bool Begin(std::span<std::byte> state) noexcept
		{
			if (Armed() || state.size() < kStereoStateExtent ||
				std::to_integer<unsigned>(state[kStereoActive]) > 1)
				return false;
			saved = state[kStereoActive];
			owner = state.data();
			SetMode(state, std::byte{ 0 });
			return true;
		}
		[[nodiscard]] bool Prepare(std::span<std::byte> state) noexcept
		{
			if (!Matches(state))
				return false;
			if (state[kStereoActive] != std::byte{ 0 })
				SetMode(state, std::byte{ 0 });
			return true;
		}
		[[nodiscard]] bool Restore(std::span<std::byte> state) noexcept
		{
			if (!Armed())
				return true;
			if (!Matches(state))
				return false;
			SetMode(state, saved);
			if (state[kStereoActive] != saved)
				return false;
			owner = nullptr;
			return true;
		}
	private:
		[[nodiscard]] bool Matches(std::span<std::byte> state) const noexcept
		{
			return Armed() && state.data() == owner && state.size() >= kStereoStateExtent;
		}
		static void SetMode(std::span<std::byte> state, std::byte mode) noexcept
		{
			state[kStereoActive] = mode;
			state[kStereoDirty] = std::byte{ 1 };
			std::uint32_t dirty = 0;
			std::memcpy(&dirty, state.data(), sizeof(dirty));
			dirty |= kInputLayoutDirty;
			std::memcpy(state.data(), &dirty, sizeof(dirty));
		}
		std::byte* owner{};
		std::byte saved{};
	};

	// These are constructor-owned arrays, not viewport or 4x4 view matrices.
	// Validate every extent before writing any entry, including a one-view camera.
	template <class Frustum, class Position, class Rotation,
		std::size_t FrustumExtent, std::size_t PositionExtent, std::size_t RotationExtent>
	[[nodiscard]] bool Normalize(std::uint32_t viewCount,
		std::span<Frustum, FrustumExtent> frustums, std::span<Position, PositionExtent> positions,
		std::span<Rotation, RotationExtent> rotations, const Frustum& frustum,
		const Position& position, const Rotation& rotation, bool& changed) noexcept
	{
		changed = false;
		if (viewCount == 0 || viewCount > 2 || frustums.size() < viewCount ||
			positions.size() < viewCount || rotations.size() < viewCount)
			return false;
		const auto copy = [&changed](auto& destination, const auto& source) {
			if (std::memcmp(&destination, &source, sizeof(source)) != 0) {
				std::memcpy(&destination, &source, sizeof(source));
				changed = true;
			}
		};
		for (std::uint32_t eye = 0; eye < viewCount; ++eye) {
			copy(frustums[eye], frustum);
			copy(positions[eye], position);
			copy(rotations[eye], rotation);
		}
		return true;
	}
}
