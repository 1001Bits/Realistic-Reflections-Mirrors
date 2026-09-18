#pragma once

#include "PlanarMirrorMath.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace MirrorSceneCoveragePolicy
{
	[[nodiscard]] constexpr bool Finite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
	}

	/** Selection uses geometry bounds, never the placed reference's origin.
	 * The nearby field remains conservative; farther attached geometry is selected
	 * by its intersection with the reflected pane cone, with no distance cutoff.
	 */
	[[nodiscard]] inline bool Includes(const DirectX::XMFLOAT3& paneCenter,
		const PlanarMirrorMath::PaneVisibilityCone& cone,
		const DirectX::XMFLOAT3& boundCenter, float boundRadius,
		float nearbyRadius, float slack) noexcept
	{
		if (!Finite(paneCenter.x) || !Finite(paneCenter.y) || !Finite(paneCenter.z) ||
			!Finite(boundCenter.x) || !Finite(boundCenter.y) || !Finite(boundCenter.z) ||
			!Finite(boundRadius) || boundRadius < 0 || !Finite(nearbyRadius) ||
			nearbyRadius < 0 || !Finite(slack) || slack < 0 || cone.planeCount != 5)
			return false;
		const double dx = static_cast<double>(boundCenter.x) - paneCenter.x;
		const double dy = static_cast<double>(boundCenter.y) - paneCenter.y;
		const double dz = static_cast<double>(boundCenter.z) - paneCenter.z;
		const double reach = static_cast<double>(nearbyRadius) + boundRadius;
		return dx * dx + dy * dy + dz * dz <= reach * reach ||
			PlanarMirrorMath::SphereIntersectsPaneCone(cone, boundCenter, boundRadius, slack);
	}

	/** Placed captures (owner, 2026-09-17: "cut the capture's CPU work"). A
	 * nearby reference whose bound lies wholly outside the reflected pane cone
	 * cannot reach one pixel of the capture: a placed pane is static, so every
	 * pixel it shows is the pane itself projected through the captured camera.
	 * The caller builds the cone from the pane inflated past the projection's
	 * margin, the test carries slack, and unreadable input is never rejected.
	 * The pane plane is tested first so the two reasons count separately. */
	enum class ConeSide : std::uint8_t { kInside, kBehindPane, kOutsideCone };
	[[nodiscard]] inline ConeSide Classify(const PlanarMirrorMath::PaneVisibilityCone& cone,
		const DirectX::XMFLOAT3& boundCenter, float boundRadius, float slack) noexcept
	{
		if (cone.planeCount != 5 || !Finite(boundCenter.x) || !Finite(boundCenter.y) ||
			!Finite(boundCenter.z) || !Finite(boundRadius) || boundRadius < 0 ||
			!Finite(slack) || slack < 0)
			return ConeSide::kInside;
		const auto outside = [&](const DirectX::XMFLOAT4& plane) noexcept {
			const double side = static_cast<double>(plane.x) * boundCenter.x +
				static_cast<double>(plane.y) * boundCenter.y +
				static_cast<double>(plane.z) * boundCenter.z - plane.w;
			return std::isfinite(side) && side < -(static_cast<double>(boundRadius) + slack);
		};
		if (outside(cone.planes[4]))
			return ConeSide::kBehindPane;
		for (std::size_t index = 0; index < 4; ++index) {
			if (outside(cone.planes[index]))
				return ConeSide::kOutsideCone;
		}
		return ConeSide::kInside;
	}
}
