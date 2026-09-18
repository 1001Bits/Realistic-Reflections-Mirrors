#pragma once

#include <cmath>
#include <numbers>

namespace HandMirrorPortraitPitchPolicy
{
	struct Vector3
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	struct Result
	{
		Vector3 forward{};
		Vector3 up{};
		Vector3 right{};
		float cameraHeightOffset{ 0.0F };
		bool valid{ false };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return valid;
		}
	};

	// Deferred/offline-only pitch theorem.  The live BuildHandPortraitCapturePlane
	// path deliberately does not call Build: elevating a level camera without a
	// matching asymmetric raster frustum moved the resolved face outside the
	// complete raised crop in V108/V109.  Keep this math only for a future lens-
	// shift implementation with an explicit full-aperture coverage proof.
	inline constexpr bool kLiveRuntimeWired = false;
	inline constexpr float kRaisedDownwardPitchDegrees = 15.0F;
	// This distance scale remains runtime-wired independently of the deferred
	// elevation theorem: ConfigureHandCameraFromFrozenRaster moves the reflected
	// eye from one full virtual-pane distance behind the pane to this retained
	// distance, preserving the last accepted portrait scale.
	inline constexpr float kReflectedPaneDistanceScale = 0.5F;
	inline constexpr float kHeadingUnitTolerance = 1.0e-3F;
	static_assert(kReflectedPaneDistanceScale > 0.0F &&
		kReflectedPaneDistanceScale <= 1.0F);

	[[nodiscard]] inline Result Build(
		const float headingX,
		const float headingY,
		const float virtualPaneDistance) noexcept
	{
		const float values[]{ headingX, headingY, virtualPaneDistance };
		for (const float value : values) {
			if (!std::isfinite(value))
				return {};
		}
		const float headingLengthSquared =
			headingX * headingX + headingY * headingY;
		if (std::abs(headingLengthSquared - 1.0F) > kHeadingUnitTolerance ||
			virtualPaneDistance <= 0.0F) {
			return {};
		}

		constexpr float pitchRadians = kRaisedDownwardPitchDegrees *
			std::numbers::pi_v<float> / 180.0F;
		const float tangent = std::tan(pitchRadians);
		const float cameraHeightOffset =
			(1.0F + kReflectedPaneDistanceScale) *
			virtualPaneDistance * tangent;
		if (!std::isfinite(tangent) || !std::isfinite(cameraHeightOffset) ||
			cameraHeightOffset <= 0.0F) {
			return {};
		}

		return {
			.forward = { headingX, headingY, 0.0F },
			.up = { 0.0F, 0.0F, 1.0F },
			.right = { headingY, -headingX, 0.0F },
			.cameraHeightOffset = cameraHeightOffset,
			.valid = true
		};
	}
}
