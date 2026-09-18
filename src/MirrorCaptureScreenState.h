#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace MirrorCaptureScreenState
{
	// Borrow only the engine's public State fields. Peer shaders read these
	// through native per-frame uploads; no Community Shaders layout is used.
	class Lease
	{
	public:
		bool Begin(std::uint32_t& width, std::uint32_t& height,
			std::uint32_t captureWidth, std::uint32_t captureHeight,
			std::array<float*, 4> ratios = {}) noexcept
		{
			if (active_ || !Extent(width) || !Extent(height) ||
				!Extent(captureWidth) || !Extent(captureHeight)) return false;
			for (std::size_t i=0; i<ratios.size(); ++i) {
				if (!ratios[i]) continue;
				const auto bits = std::bit_cast<std::uint32_t>(*ratios[i]);
				if ((bits & 0x7F800000U) == 0x7F800000U || *ratios[i] <= 0 || *ratios[i] > 1) return false;
				savedRatios_[i] = *ratios[i];
			}
			width_=&width; height_=&height; ratios_=ratios;
			savedWidth_=width; savedHeight_=height; active_=true;
			width=captureWidth; height=captureHeight;
			for (auto* ratio : ratios_) if (ratio) *ratio=1.0F;
			return true;
		}
		bool Matches(std::uint32_t captureWidth, std::uint32_t captureHeight) const noexcept
		{
			if (!active_ || *width_!=captureWidth || *height_!=captureHeight) return false;
			for (const auto* ratio:ratios_) if (ratio && *ratio!=1.0F) return false;
			return true;
		}
		void Restore() noexcept
		{
			if (!active_) return;
			*width_=savedWidth_; *height_=savedHeight_;
			for (std::size_t i=0; i<ratios_.size(); ++i) if (ratios_[i]) *ratios_[i]=savedRatios_[i];
			active_=false;
		}
	private:
		static bool Extent(std::uint32_t n) noexcept { return n>0 && n<=16384; }
		std::uint32_t *width_{}, *height_{};
		std::uint32_t savedWidth_{}, savedHeight_{};
		std::array<float*, 4> ratios_{};
		std::array<float, 4> savedRatios_{};
		bool active_{};
	};
}
