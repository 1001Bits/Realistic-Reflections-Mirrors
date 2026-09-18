#pragma once

#include <string_view>

namespace MirrorPaneNames
{
	[[nodiscard]] bool ContainsNoCase(std::string_view text, std::string_view token) noexcept;
	[[nodiscard]] bool IsExcludedMirrorName(std::string_view name) noexcept;
	[[nodiscard]] bool IsMirrorName(std::string_view name) noexcept;
}
