#pragma once

#include <cmath>

#include <DirectXMath.h>

namespace SecondViewMath
{
	/**
	 * Offset a camera along its world-space right axis.  Skyrim units are
	 * deliberately kept outside this helper so the convention is testable
	 * without loading any engine types.
	 */
	[[nodiscard]] inline bool OffsetSideways(
		const DirectX::XMFLOAT3& a_position,
		const DirectX::XMFLOAT3& a_right,
		float a_distance,
		DirectX::XMFLOAT3& a_output) noexcept
	{
		if (!std::isfinite(a_position.x) || !std::isfinite(a_position.y) ||
			!std::isfinite(a_position.z) || !std::isfinite(a_right.x) ||
			!std::isfinite(a_right.y) || !std::isfinite(a_right.z) ||
			!std::isfinite(a_distance))
			return false;

		const float lengthSquared =
			a_right.x * a_right.x + a_right.y * a_right.y + a_right.z * a_right.z;
		if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f)
			return false;

		const float scale = a_distance / std::sqrt(lengthSquared);
		a_output = {
			a_position.x + a_right.x * scale,
			a_position.y + a_right.y * scale,
			a_position.z + a_right.z * scale
		};
		return std::isfinite(a_output.x) && std::isfinite(a_output.y) &&
		       std::isfinite(a_output.z);
	}
}
