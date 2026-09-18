#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace MirrorContentProfile
{
	inline constexpr std::string_view kCorePlugin = "RealisticReflectionsMirrors.esm";
	inline constexpr std::string_view kAddonPlugin = "MirrorsOfSkyrim.esp";
	[[nodiscard]] constexpr std::string_view StandingPlugin(std::uint32_t local) noexcept
	{
		return local >= 0x80F && local <= 0x813 ? kAddonPlugin : kCorePlugin;
	}
	inline std::atomic_bool addonLoaded{ false };

	// InputLoaded precedes form resolution. This only requests preparation;
	// loaded forms and the exact hand-content hash gates authorize runtime use.
	[[nodiscard]] inline bool AddonCandidatePresent() noexcept
	{
		std::error_code error;
		return std::filesystem::is_regular_file(
			std::filesystem::path{ "Data" } / kAddonPlugin, error) && !error;
	}
	[[nodiscard]] inline bool AddonLoaded() noexcept
	{
		return addonLoaded.load(std::memory_order_acquire);
	}
}
