#pragma once

#include <bit>
#include <cstdint>

/**
 * Raised portrait zoom: minimum zoom fits the body, maximum zoom crops the
 * face. Below default zoom the opt-in centred policy moves both the capture
 * eye and sampling anchor down, and widens the frustum to fit boots and head.
 * At/above default it leaves the accepted face framing unchanged. The legacy
 * crop-only policy remains available for comparison while the fix is untested.
 */
namespace HandMirrorPortraitZoomPolicy
{
	/** Crown/helmet clearance above the eye anchor, world units. */
	inline constexpr float kHeadAboveEyesUnits = 16.0F;
	/** Fallback eye height above the feet when the body root is unusable. */
	inline constexpr float kFallbackEyeAboveFeetUnits = 120.0F;
	/** Full-body framing margin (8 %). */
	inline constexpr float kBodyMargin = 1.08F;
	/** Legacy widening cap; the corrected fit does not silently crop to this limit. */
	inline constexpr float kMaximumFrustumWiden = 4.0F;
	/** Plausible eye-above-feet range for a scaled player. */
	inline constexpr float kMinimumEyeAboveFeetUnits = 40.0F;
	inline constexpr float kMaximumEyeAboveFeetUnits = 220.0F;

	[[nodiscard]] constexpr bool Finite(const float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
	}

	/**
	 * 0 at or above the default zoom (accepted composition unchanged), 1 at or
	 * below the minimum zoom (full body), linear between.
	 */
	[[nodiscard]] constexpr float BodyBlend(
		const float zoom,
		const float minimumZoom,
		const float defaultZoom) noexcept
	{
		if (!Finite(zoom) || !Finite(minimumZoom) || !Finite(defaultZoom) ||
			defaultZoom <= minimumZoom) {
			return 0.0F;
		}
		if (zoom >= defaultZoom)
			return 0.0F;
		if (zoom <= minimumZoom)
			return 1.0F;
		return (defaultZoom - zoom) / (defaultZoom - minimumZoom);
	}

	struct BodyFramingInput
	{
		float faceAnchorZ{ 0.0F };
		float bodyBottomZ{ 0.0F };
		/** Depth of the face anchor along the portrait camera forward axis. */
		float anchorDepth{ 0.0F };
		/** Source frustum top edge divided by its near plane (vertical half slope). */
		float sourceTopSlope{ 0.0F };
		float blend{ 0.0F };
	};

	struct BodyFraming
	{
		float frustumWiden{ 1.0F };
		/** World units the sampling anchor moves down from the eyes. */
		float anchorDropUnits{ 0.0F };
		bool valid{ false };
		/** Distance to translate the capture eye down its own up axis. */
		float captureDropUnits{ 0.0F };
	};

	[[nodiscard]] constexpr BodyFraming ComputeBodyFraming(
		const BodyFramingInput& input) noexcept
	{
		if (!Finite(input.faceAnchorZ) || !Finite(input.bodyBottomZ) ||
			!Finite(input.anchorDepth) || !Finite(input.sourceTopSlope) ||
			!Finite(input.blend)) {
			return {};
		}
		const float blend = input.blend <= 0.0F ? 0.0F :
			(input.blend >= 1.0F ? 1.0F : input.blend);
		if (blend == 0.0F)
			return { 1.0F, 0.0F, true };
		if (input.anchorDepth <= 1.0e-3F || input.sourceTopSlope <= 1.0e-4F)
			return {};
		const float top = input.faceAnchorZ + kHeadAboveEyesUnits;
		const float eyeAboveFeet = input.faceAnchorZ - input.bodyBottomZ;
		const float bottom =
			eyeAboveFeet >= kMinimumEyeAboveFeetUnits &&
					eyeAboveFeet <= kMaximumEyeAboveFeetUnits ?
				input.bodyBottomZ :
				input.faceAnchorZ - kFallbackEyeAboveFeetUnits;
		const float halfBody = 0.5F * (top - bottom) * kBodyMargin;
		const float visibleHalf = input.anchorDepth * input.sourceTopSlope;
		float needed = halfBody / visibleHalf;
		if (needed < 1.0F)
			needed = 1.0F;
		if (needed > kMaximumFrustumWiden)
			needed = kMaximumFrustumWiden;
		const float centre = 0.5F * (top + bottom);
		return {
			1.0F + blend * (needed - 1.0F),
			blend * (input.faceAnchorZ - centre),
			true
		};
	}

	/**
	 * Fit the body in the actual capture, before the pane's sampling crop.
	 * Moving only the crop cannot recover legs outside an eye-centred capture:
	 * at minimum zoom the crop fills the texture and its centre is clamped.
	 * Translate parallel to the capture plane, preserving its axial clip depths,
	 * direction and camera distance. Account for perspective at both endpoints
	 * when looking up/down, and for the small border excluded by the crop.
	 */
	[[nodiscard]] constexpr BodyFraming ComputeCenteredBodyFraming(
		const BodyFramingInput& input, const float forwardZ, const float upZ,
		const float minimumZoom = 1.0F) noexcept
	{
		if (!Finite(input.faceAnchorZ) || !Finite(input.bodyBottomZ) ||
			!Finite(input.anchorDepth) || !Finite(input.sourceTopSlope) ||
			!Finite(input.blend) || !Finite(forwardZ) || !Finite(upZ) ||
			!Finite(minimumZoom) || minimumZoom <= 0.0F || upZ <= 0.0F ||
			forwardZ < -1.0F || forwardZ > 1.0F || upZ > 1.0F)
			return {};
		const float blend = input.blend <= 0.0F ? 0.0F :
			(input.blend >= 1.0F ? 1.0F : input.blend);
		if (blend == 0.0F)
			return { 1.0F, 0.0F, true, 0.0F };
		if (input.anchorDepth <= 1.0e-3F || input.sourceTopSlope <= 1.0e-4F)
			return {};
		const float measuredHeight = input.faceAnchorZ - input.bodyBottomZ;
		const float height = measuredHeight >= kMinimumEyeAboveFeetUnits &&
			measuredHeight <= kMaximumEyeAboveFeetUnits ? measuredHeight : kFallbackEyeAboveFeetUnits;
		constexpr float bootClearance = 6.0F;
		const float bottomOffset = -height - bootClearance;
		const float topOffset = kHeadAboveEyesUnits;
		const float topDepth = input.anchorDepth + topOffset * forwardZ;
		const float bottomDepth = input.anchorDepth + bottomOffset * forwardZ;
		if (topDepth <= 1.0e-3F || bottomDepth <= 1.0e-3F ||
			!Finite(topDepth) || !Finite(bottomDepth))
			return {};
		// Centre the projected endpoints, not just their world-space midpoint:
		// at a pitched camera the boots and crown have different depths.
		const float fullCaptureDrop = -upZ *
			(topOffset * bottomDepth + bottomOffset * topDepth) / (topDepth + bottomDepth);
		const float drop = fullCaptureDrop / upZ;
		if (!Finite(fullCaptureDrop) || !Finite(drop))
			return {};
		const float topSlope = (topOffset * upZ + fullCaptureDrop) / topDepth;
		const float bottomSlope = -(bottomOffset * upZ + fullCaptureDrop) / bottomDepth;
		const float neededSlope = topSlope > bottomSlope ? topSlope : bottomSlope;
		const float cropHalfNdc = 0.998F / minimumZoom;
		float needed = neededSlope * kBodyMargin / (input.sourceTopSlope * cropHalfNdc);
		if (!Finite(needed) || needed <= 0.0F)
			return {};
		if (needed < 1.0F)
			needed = 1.0F;
		// Do not cap to the legacy 4x factor: that silently crops tall/close bodies.
		return { 1.0F + blend * (needed - 1.0F), blend * drop, true,
			blend * fullCaptureDrop };
	}

	static_assert(BodyBlend(1.794F, 1.0F, 1.794F) == 0.0F);
	static_assert(BodyBlend(3.0F, 1.0F, 1.794F) == 0.0F);
	static_assert(BodyBlend(1.0F, 1.0F, 1.794F) == 1.0F);
	static_assert(BodyBlend(0.5F, 1.0F, 1.794F) == 1.0F);
	static_assert(BodyBlend(1.397F, 1.0F, 1.794F) > 0.49F &&
		BodyBlend(1.397F, 1.0F, 1.794F) < 0.51F);
	static_assert(BodyBlend(1.0F, 2.0F, 1.0F) == 0.0F);

	// Default zoom: nothing changes.
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 0.0F }).valid);
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 0.0F }).frustumWiden == 1.0F);
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 0.0F }).anchorDropUnits == 0.0F);
	// Minimum zoom: feet (0) to crown (136) fit with margin; anchor at the body centre.
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 1.0F }).valid);
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 1.0F }).frustumWiden > 3.3F &&
		ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 1.0F }).frustumWiden < 3.5F);
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 1.0F }).anchorDropUnits == 52.0F);
	// Implausible body root falls back to the standard eye height.
	static_assert(ComputeBodyFraming({ 120.0F, 119.0F, 60.0F, 0.36F, 1.0F }).anchorDropUnits == 52.0F);
	static_assert(ComputeBodyFraming({ 120.0F, -500.0F, 60.0F, 0.36F, 1.0F }).anchorDropUnits == 52.0F);
	// Widening is capped.
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 10.0F, 0.36F, 1.0F }).frustumWiden == kMaximumFrustumWiden);
	// Invalid depth is rejected rather than guessed.
	static_assert(!ComputeBodyFraming({ 120.0F, 0.0F, 0.0F, 0.36F, 1.0F }).valid);
	// Half blend halves both effects.
	static_assert(ComputeBodyFraming({ 120.0F, 0.0F, 60.0F, 0.36F, 0.5F }).anchorDropUnits == 26.0F);
}
