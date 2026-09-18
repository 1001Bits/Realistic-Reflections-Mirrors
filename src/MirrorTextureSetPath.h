#pragma once

#include <cstddef>
#include <string_view>

namespace MirrorTextureSetPath
{
	inline constexpr std::string_view kMarkerPath="mirrors_of_skyrim\\authoring\\mirror_surface.dds";

	[[nodiscard]] constexpr char Fold(char value) noexcept
	{
		if (value=='/') return '\\';
		return value>='A' && value<='Z' ? static_cast<char>(value-'A'+'a') : value;
	}
	[[nodiscard]] constexpr bool HasPrefix(const char* value,std::string_view prefix) noexcept
	{
		for (std::size_t i=0;i<prefix.size();++i)
			if (Fold(value[i])!=prefix[i]) return false;
		return true;
	}
	// Engine string reads must remain inside the caller's existing SEH boundary.
	[[nodiscard]] constexpr bool IsMarkerPath(const char* value) noexcept
	{
		if (!value) return false;
		// CK-swapped live materials retain Data\Textures\; authored NIFs can
		// use Textures\ or a texture-relative path. All name the same asset.
		constexpr std::string_view dataTextureRoot="data\\textures\\";
		constexpr std::string_view textureRoot="textures\\";
		if (HasPrefix(value,dataTextureRoot)) value+=dataTextureRoot.size();
		else if (HasPrefix(value,textureRoot)) value+=textureRoot.size();
		for (std::size_t i=0;i<kMarkerPath.size();++i)
			if (Fold(value[i])!=kMarkerPath[i]) return false;
		return value[kMarkerPath.size()]=='\0';
	}
}
