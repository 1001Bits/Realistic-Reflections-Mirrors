#pragma once

namespace MirrorsOfSkyrimGeometryDrawObserver
{
	/** Install the single generic-geometry-draw hook owned by this DLL. */
	[[nodiscard]] bool EnsureInstalled() noexcept;
	/** True only while that exact hook remains installed and healthy. */
	[[nodiscard]] bool IsInstalled() noexcept;
}
