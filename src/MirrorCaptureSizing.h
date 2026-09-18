#pragma once

#include <atomic>
#include <cstdint>

namespace MirrorCaptureSizing
{
	struct Extent
	{
		std::uint32_t width{}, height{};
		constexpr bool operator==(const Extent&) const noexcept = default;
		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return width > 0 && height > 0 && width <= 16384 && height <= 16384;
		}
		[[nodiscard]] constexpr std::uint32_t MipLevels() const noexcept
		{
			auto edge = width > height ? width : height;
			std::uint32_t levels = 0;
			while (edge) { ++levels; edge >>= 1; }
			return levels;
		}
	};

	// Snapshot taken from the public swapchain before entering a private pass.
	// ENB's own screen constants cannot be changed via the engine's size globals.
	inline std::atomic_bool framebufferRequired{ false };
	inline std::atomic<std::uint64_t> framebufferExtent{ 0 };
	inline void SetFramebuffer(bool required, Extent extent) noexcept
	{
		framebufferExtent.store((std::uint64_t(extent.width) << 32) | extent.height,
			std::memory_order_release);
		framebufferRequired.store(required, std::memory_order_release);
	}
	[[nodiscard]] inline bool UsesFramebuffer() noexcept
	{
		return framebufferRequired.load(std::memory_order_acquire);
	}
	[[nodiscard]] inline Extent ForSquare(std::uint32_t requested) noexcept
	{
		if (!UsesFramebuffer()) return { requested, requested };
		const auto packed = framebufferExtent.load(std::memory_order_acquire);
		return { static_cast<std::uint32_t>(packed >> 32), static_cast<std::uint32_t>(packed) };
	}
	// Published frames retain their own extent across a later resize. Their SRV
	// shape/device/identity is independently validated before any draw.
	[[nodiscard]] constexpr bool CompletedExtentAllowed(Extent extent, bool framebuffer) noexcept
	{
		return framebuffer && extent.Valid();
	}
}
