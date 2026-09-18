#include "MirrorPaneNames.h"

namespace
{
	char LowerASCII(char value) noexcept
	{
		return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
	}

	bool StartsWithNoCase(std::string_view text, std::string_view prefix) noexcept
	{
		if (prefix.size() > text.size())
			return false;
		for (std::size_t index = 0; index < prefix.size(); ++index) {
			if (LowerASCII(text[index]) != LowerASCII(prefix[index]))
				return false;
		}
		return true;
	}
}

namespace MirrorPaneNames
{
	bool ContainsNoCase(std::string_view text, std::string_view token) noexcept
	{
		if (token.empty() || token.size() > text.size())
			return false;
		for (std::size_t start = 0; start + token.size() <= text.size(); ++start) {
			bool matches = true;
			for (std::size_t index = 0; index < token.size(); ++index) {
				if (LowerASCII(text[start + index]) != LowerASCII(token[index])) {
					matches = false;
					break;
				}
			}
			if (matches)
				return true;
		}
		return false;
	}

	bool IsExcludedMirrorName(std::string_view name) noexcept
	{
		return ContainsNoCase(name, "mirrorball") || ContainsNoCase(name, "mirrored") ||
		       ContainsNoCase(name, "marker") || ContainsNoCase(name, "cameraattach");
	}

	bool IsMirrorName(std::string_view name) noexcept
	{
		if (name.empty() || IsExcludedMirrorName(name))
			return false;

		// Keep the fallback pane-specific. Generic ancestor/object names such as
		// RealisticReflectionsFrame01 must never make the frame eligible for suppression.
		return StartsWithNoCase(name, "truemirror:") ||
		       (ContainsNoCase(name, "mirror") && ContainsNoCase(name, "pane"));
	}
}
