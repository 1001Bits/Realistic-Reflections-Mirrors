#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * BSGraphics::State frame counter, addressed per runtime.
 *
 * CommonLibSSE-NG declares `BSGraphics::State::frameCount` as a single direct
 * member at 0x4C for every runtime.  That is correct on Skyrim SE 1.5.97 and
 * on Skyrim VR 1.4.15.  Anniversary Edition (1.6.629 and later) inserts eight
 * bytes ahead of it -- the same insertion that moves the class's RUNTIME_DATA
 * block from 0x58 to 0x60, which CommonLib does model
 * (`RUNTIME_MEMBER_ACCESSOR_VERSIONED(RUNTIME_DATA, GetRuntimeData,
 * RUNTIME_SSE_1_6_629, 0x58, 0x60, 0x60)`).  VR's 0x60 block comes from a
 * different insertion: on VR (2026-09-05 04:00 session) 0x4C read a live
 * count of 189 while 0x54 read 0, which is `insideFrame` in the SE layout.
 *
 * Evidence: on Skyrim 1.7.104 the hand-mirror content gate read a "frame
 * counter" of 3,123,917,619 (0xBA333333) while the scene nodes reported ~12,600,
 * so every freshness comparison rejected and the hand mirror stayed black.
 * 0xBA333333 is a small negative float, i.e. `projectionPosScaleX` -- exactly
 * the field that lands on 0x4C once the eight bytes are inserted.  On SE the
 * same comparison matched every frame and rejected nothing.
 */
namespace GraphicsFrameCounterPolicy
{
	inline constexpr std::ptrdiff_t kSE1597Offset = 0x4C;
	inline constexpr std::ptrdiff_t kShiftedOffset = 0x54;

	/** `a_shifted` is true only for Anniversary Edition runtimes. */
	[[nodiscard]] constexpr std::ptrdiff_t Offset(const bool a_shifted) noexcept
	{
		return a_shifted ? kShiftedOffset : kSE1597Offset;
	}

	static_assert(Offset(false) == kSE1597Offset);
	static_assert(Offset(true) == kSE1597Offset + 8);

	/**
	 * A live frame counter advances by small steps and stays far below the
	 * float-bit-pattern magnitudes that a mis-addressed read produces.  Callers
	 * use this only to report a mis-addressed field, never to gate rendering.
	 */
	inline constexpr std::uint32_t kImplausibleFrameCounter = 0x4000'0000;

	[[nodiscard]] constexpr bool IsPlausibleFrameCounter(
		const std::uint32_t a_value) noexcept
	{
		return a_value < kImplausibleFrameCounter;
	}

	[[nodiscard]] inline std::uint32_t Read(
		const void* a_state,
		const bool a_shifted) noexcept
	{
		if (!a_state)
			return 0;
		std::uint32_t value = 0;
		std::memcpy(
			&value,
			static_cast<const std::byte*>(a_state) + Offset(a_shifted),
			sizeof(value));
		return value;
	}
}
