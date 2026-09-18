#pragma once

#include <cstdint>

namespace PlanarMipChainPolicy
{
	/** Number of subresources in a complete 2D minification chain. */
	[[nodiscard]] constexpr std::uint32_t FullMipCount(
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		if (a_width == 0 || a_height == 0)
			return 0;

		std::uint32_t levels = 1;
		std::uint32_t extent = a_width > a_height ? a_width : a_height;
		while (extent > 1) {
			extent >>= 1;
			++levels;
		}
		return levels;
	}

	/**
	 * Planar delivery requires at least one minified level and every level down
	 * to 1x1.  A partial chain is not a quality-preserving fallback.
	 */
	[[nodiscard]] constexpr bool IsCompleteMinificationChain(
		std::uint32_t a_width,
		std::uint32_t a_height,
		std::uint32_t a_mipLevels) noexcept
	{
		const auto expected = FullMipCount(a_width, a_height);
		return expected > 1 && a_mipLevels == expected;
	}
}
