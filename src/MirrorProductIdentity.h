#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace MirrorProductIdentity
{
	enum class Kind : std::uint8_t
	{
		kNone,
		kLegacy,
		kMirrorsOfSkyrim
	};

	struct Identity
	{
		Kind kind{ Kind::kNone };
		std::string_view pluginName{};
		std::uint32_t localFormID{ 0 };
		std::string_view modelPath{};
	};

	inline constexpr Identity kLegacy{
		Kind::kLegacy,
		"RealisticReflections.esp",
		0x800,
		"realisticreflections\\mirror01.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrim{
		Kind::kMirrorsOfSkyrim,
		"RealisticReflectionsMirrors.esm",
		0x800,
		"mirrors_of_skyrim\\mirror01.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimWhiterunWood{
		Kind::kMirrorsOfSkyrim,
		"RealisticReflectionsMirrors.esm",
		0x801,
		"mirrors_of_skyrim\\mirror02.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimNordicDarkWood{
		Kind::kMirrorsOfSkyrim,
		"RealisticReflectionsMirrors.esm",
		0x802,
		"mirrors_of_skyrim\\mirror03.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimGildedNoble{
		Kind::kMirrorsOfSkyrim,
		"RealisticReflectionsMirrors.esm",
		0x803,
		"mirrors_of_skyrim\\mirror04.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimMarkarthBrass{
		Kind::kMirrorsOfSkyrim,
		"RealisticReflectionsMirrors.esm",
		0x804,
		"mirrors_of_skyrim\\mirror05.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimPlaced01{
		Kind::kMirrorsOfSkyrim,
		"MirrorsOfSkyrim.esp",
		0x80F,
		"mirrors_of_skyrim\\mirror01.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimPlaced02{
		Kind::kMirrorsOfSkyrim,
		"MirrorsOfSkyrim.esp",
		0x810,
		"mirrors_of_skyrim\\mirror02.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimPlaced03{
		Kind::kMirrorsOfSkyrim,
		"MirrorsOfSkyrim.esp",
		0x811,
		"mirrors_of_skyrim\\mirror03.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimPlaced04{
		Kind::kMirrorsOfSkyrim,
		"MirrorsOfSkyrim.esp",
		0x812,
		"mirrors_of_skyrim\\mirror04.nif"
	};
	inline constexpr Identity kMirrorsOfSkyrimPlaced05{
		Kind::kMirrorsOfSkyrim,
		"MirrorsOfSkyrim.esp",
		0x813,
		"mirrors_of_skyrim\\mirror05.nif"
	};
	inline constexpr std::array kAllowlist{
		kLegacy,
		kMirrorsOfSkyrim,
		kMirrorsOfSkyrimWhiterunWood,
		kMirrorsOfSkyrimNordicDarkWood,
		kMirrorsOfSkyrimGildedNoble,
		kMirrorsOfSkyrimMarkarthBrass,
		kMirrorsOfSkyrimPlaced01,
		kMirrorsOfSkyrimPlaced02,
		kMirrorsOfSkyrimPlaced03,
		kMirrorsOfSkyrimPlaced04,
		kMirrorsOfSkyrimPlaced05
	};
	inline constexpr std::string_view kPaneName = "TrueMirror:0";
	inline constexpr std::string_view kPaneIntegerTag = "TrueMirror";
	inline constexpr std::int32_t kPaneIntegerValue = 1;

	[[nodiscard]] constexpr char LowerASCII(char value) noexcept
	{
		return value >= 'A' && value <= 'Z' ?
		           static_cast<char>(value + ('a' - 'A')) :
		           value;
	}

	[[nodiscard]] constexpr bool FilenameEquals(
		std::string_view left,
		std::string_view right) noexcept
	{
		if (left.size() != right.size())
			return false;
		for (std::size_t index = 0; index < left.size(); ++index) {
			if (LowerASCII(left[index]) != LowerASCII(right[index]))
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr const Identity* Match(
		std::string_view pluginName,
		std::uint32_t localFormID) noexcept
	{
		for (const auto& identity : kAllowlist) {
			if (identity.localFormID == localFormID &&
				FilenameEquals(pluginName, identity.pluginName)) {
				return &identity;
			}
		}
		return nullptr;
	}

	[[nodiscard]] constexpr bool SameSource(
		const Identity& left,
		const Identity& right) noexcept
	{
		return left.localFormID == right.localFormID &&
		       FilenameEquals(left.pluginName, right.pluginName);
	}

	[[nodiscard]] constexpr const Identity* FindAnchor(Kind kind) noexcept
	{
		for (const auto& identity : kAllowlist) {
			if (identity.kind == kind && identity.localFormID == 0x800)
				return &identity;
		}
		return nullptr;
	}

	// Compatibility name for existing placement/control callers.  The lookup
	// is deliberately anchor-specific even though one product owns many styles.
	[[nodiscard]] constexpr const Identity* Find(Kind kind) noexcept
	{
		return FindAnchor(kind);
	}

	[[nodiscard]] constexpr Kind PreferredPlacement(
		bool mirrorsOfSkyrimAvailable,
		bool legacyAvailable) noexcept
	{
		if (mirrorsOfSkyrimAvailable)
			return Kind::kMirrorsOfSkyrim;
		return legacyAvailable ? Kind::kLegacy : Kind::kNone;
	}

	[[nodiscard]] constexpr bool RequiresGenerationAdvance(
		Kind previousKind,
		std::uintptr_t previousBaseToken,
		Kind currentKind,
		std::uintptr_t currentBaseToken) noexcept
	{
		return previousKind != currentKind || previousBaseToken != currentBaseToken;
	}
}
