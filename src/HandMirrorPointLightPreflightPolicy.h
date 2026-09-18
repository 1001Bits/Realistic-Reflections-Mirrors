#pragma once

#include <cstdint>
#include <span>

namespace HandMirrorPointLightPreflightPolicy
{
	// CalculateActiveLightsForSurface has a largest native capacity of 16.
	inline constexpr std::uint32_t kMaximumLights = 16;

	[[nodiscard]] constexpr bool ShouldInspect(
		bool privateMarker, bool exactPrivateMirror,
		bool mainMarker, bool insideMainWorld, bool insidePrivateScope) noexcept
	{
		return (privateMarker && exactPrivateMirror) ||
			(mainMarker && insideMainWorld && !insidePrivateScope);
	}

	enum class Result : std::uint8_t
	{
		kReady,
		kCountInvalid,
		kMissingArray,
		kMissingWrapper,
		kRetiredLight,
		kUnreadable,
		kInvalidWrapper,
		kForeignWrapper,
		kInvalidLight,
		kForeignLight,
		kTechniqueCountInvalid
	};

	// AE 1.7.104 BSLight::light is at +0x48. The native point-light packer
	// reads NiLight fields through +0x148; validate the address range before
	// following either pointer. This is a preflight, never a lifetime lease.
	inline constexpr std::uintptr_t kLightPointerOffset = 0x48;
	[[nodiscard]] constexpr bool PlausiblePointer(
		std::uintptr_t pointer, std::uintptr_t bytes) noexcept
	{
		constexpr std::uintptr_t userEnd = UINT64_C(0x0000800000000000);
		return pointer >= 0x10000 && (pointer & 7) == 0 &&
			bytes <= userEnd && pointer <= userEnd - bytes;
	}

	[[nodiscard]] constexpr bool KnownType(
		std::uintptr_t vtable, std::span<const std::uintptr_t> types) noexcept
	{
		if (!vtable) return false;
		for (const auto type : types) if (type == vtable) return true;
		return false;
	}

	/** Reject recycled wrappers before reading their purported NiLight. */
	template <class Reader>
	[[nodiscard]] Result InspectIdentity(
		std::uintptr_t wrapper,
		std::span<const std::uintptr_t> wrapperTypes,
		std::span<const std::uintptr_t> lightTypes,
		Reader&& read) noexcept
	{
		if (!wrapper) return Result::kMissingWrapper;
		if (!PlausiblePointer(wrapper, 0x140)) return Result::kInvalidWrapper;
		std::uintptr_t vtable = 0;
		if (!read(wrapper, vtable)) return Result::kUnreadable;
		if (!KnownType(vtable, wrapperTypes)) return Result::kForeignWrapper;
		std::uintptr_t light = 0;
		if (!read(wrapper + kLightPointerOffset, light)) return Result::kUnreadable;
		if (!light) return Result::kRetiredLight;
		if (!PlausiblePointer(light, 0x150)) return Result::kInvalidLight;
		if (!read(light, vtable)) return Result::kUnreadable;
		return KnownType(vtable, lightTypes) ? Result::kReady : Result::kForeignLight;
	}

	// Relative BSLightingShader descriptor: the native point count is bits
	// 3..5, not the low bits of passEnum (which includes base 0x4800002D).
	[[nodiscard]] constexpr bool LightingTechniqueFits(
		std::uint32_t technique, std::uint32_t count) noexcept
	{
		constexpr std::uint32_t base = 0x4800002D;
		if (technique < base || technique - base >= 0x14000000u) return false;
		const auto points = ((technique - base) >> 3) & 7;
		return count <= kMaximumLights && (points == 0 || points < count);
	}

	/**
	 * Validate at consumption, after selection/cache reuse. Slot zero is the
	 * directional light; GeometrySetupConstantPointLights starts at slot one.
	 * The reader is synchronous, read-only, and never retains engine objects.
	 */
	template <class Reader>
	[[nodiscard]] Result Inspect(
		const std::uint32_t count,
		const bool arrayPresent,
		Reader&& read,
		std::uint32_t& rejectedSlot) noexcept
	{
		rejectedSlot = 0;
		if (count > kMaximumLights)
			return Result::kCountInvalid;
		if (count <= 1)
			return Result::kReady;
		if (!arrayPresent)
			return Result::kMissingArray;
		for (std::uint32_t slot = 1; slot < count; ++slot) {
			rejectedSlot = slot;
			const auto result = read(slot);
			if (result != Result::kReady)
				return result;
		}
		rejectedSlot = 0;
		return Result::kReady;
	}
}
