#pragma once
#include <array>
#include <cstdint>
namespace MirrorFurnishingCatalog
{
	struct Fixture { std::uint32_t controller, vanillaParent; bool home; };
	constexpr bool Enabled(bool setting, bool needsParent, bool parentPresent, bool parentDisabled) noexcept
	{
		return setting && (!needsParent || (parentPresent && !parentDisabled));
	}
	// Mirrors packaging/mirror-acquisition-placements.json; no vanilla records are mutated.
	inline constexpr std::array kFixtures{
		Fixture{ 0xB00, 0xC6E3A, true },
		Fixture{ 0xB01, 0xC7F18, true },
		Fixture{ 0xB02, 0xE2D52, true },
		Fixture{ 0xB03, 0xDF48F, true },
		Fixture{ 0xB04, 0xE25D5, true },
		Fixture{ 0xB05, 0x0, false },
		Fixture{ 0xB06, 0x0, false },
		Fixture{ 0xB07, 0x0, false },
		Fixture{ 0xB08, 0x0, false },
		Fixture{ 0xB09, 0x0, false },
		Fixture{ 0xB0A, 0x0, false },
		Fixture{ 0xB0B, 0xC6E3A, true },
		Fixture{ 0xB0C, 0xC7F18, true },
		Fixture{ 0xB0D, 0xE25D5, true },
		Fixture{ 0xB0E, 0x0, false },
		Fixture{ 0xB0F, 0x0, false }
	};
}
