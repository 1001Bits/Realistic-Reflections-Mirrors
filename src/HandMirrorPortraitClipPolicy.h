#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace HandMirrorPortraitClipPolicy
{
	// The portrait camera is deliberately normal-aligned with its virtual pane.
	// A conventional near plane can therefore coincide with that pane without an
	// oblique projection (whose column-2 rewrite breaks Skyrim's SoftEffect depth
	// reconstruction).
	inline constexpr float kMinimumPlaneDepth = 1.0e-4F;
	inline constexpr float kMinimumFarGap = 1.0e-3F;
	inline constexpr float kMinimumRetainedAnchorDepthMargin = 0.125F;
	// Leave a numeric gap in addition to the authored safety margin when a
	// physical pane has to be clamped in front of the retained portrait bound.
	// This keeps the strict comparison true after the projection constants are
	// rebuilt in single precision.
	inline constexpr float kRetainedBoundsClampSlack = 1.0e-3F;
	// The physical pane half-space is admitted only when the retained player's
	// world-space keep point lies unambiguously on the viewer side of the pane
	// volume.  This plane is for WorldRoot culling only; it must never replace the
	// stable portrait camera's conventional near plane for the retained player.
	inline constexpr float kMinimumPhysicalWorldKeepMargin = 0.125F;
	inline constexpr float kMinimumPlaneAlignment = 0.9999F;
	inline constexpr float kMaximumPlaneAlignment = 1.0001F;
	inline constexpr float kProjectionTolerance = 5.0e-4F;
	inline constexpr std::uint32_t kMaximumRetainedGeometryCount = 512;

	/**
	 * The hidden third-person graph can be stamped either by the current graphics
	 * frame or, on the verified SE 1.5.97 path, by its exact predecessor.  Bounds
	 * may tighten the portrait near plane only when every observed hidden tuple
	 * member belongs to the optical cohort selected for the capture.
	 */
	struct SelectedOpticsEpochInputs
	{
		std::uint32_t selectedGraphicsFrame{ 0 };
		std::uint32_t currentGraphicsFrame{ 0 };
		std::uint32_t hiddenRootFrame{ 0 };
		std::uint32_t hiddenCloneFrame{ 0 };
		std::uint32_t hiddenItemFrame{ 0 };
		std::uint32_t hiddenPaneFrame{ 0 };
		bool exactSE1597{ false };
		bool exactAE161170{ false };
	};

	struct SelectedOpticsEpoch
	{
		bool directCurrent{ false };
		bool exactPredecessor{ false };
		bool valid{ false };
	};

	[[nodiscard]] constexpr SelectedOpticsEpoch SelectOpticsEpoch(
		const SelectedOpticsEpochInputs& inputs) noexcept
	{
		if (inputs.selectedGraphicsFrame == 0 ||
			inputs.currentGraphicsFrame == 0 ||
			inputs.selectedGraphicsFrame ==
				(std::numeric_limits<std::uint32_t>::max)() ||
			inputs.currentGraphicsFrame ==
				(std::numeric_limits<std::uint32_t>::max)() ||
			inputs.hiddenRootFrame != inputs.selectedGraphicsFrame ||
			inputs.hiddenCloneFrame != inputs.selectedGraphicsFrame ||
			inputs.hiddenItemFrame != inputs.selectedGraphicsFrame ||
			inputs.hiddenPaneFrame != inputs.selectedGraphicsFrame) {
			return {};
		}

		if (inputs.selectedGraphicsFrame == inputs.currentGraphicsFrame) {
			return { .directCurrent = true, .valid = true };
		}
		const bool exactPredecessor =
			(inputs.exactSE1597 || inputs.exactAE161170) &&
			inputs.currentGraphicsFrame > 1 &&
			inputs.selectedGraphicsFrame == inputs.currentGraphicsFrame - 1;
		return exactPredecessor ?
			SelectedOpticsEpoch{ .exactPredecessor = true, .valid = true } :
			SelectedOpticsEpoch{};
	}

	/** Numeric BipedAnim object slots which can own visible head/face equipment. */
	[[nodiscard]] constexpr bool IsHeadgearBipedObjectIndex(
		const std::uint32_t index) noexcept
	{
		switch (index) {
		case 0:   // Head
		case 1:   // Hair
		case 11:  // LongHair
		case 12:  // Circlet
		case 13:  // Ears
		case 14:  // ModMouth
		case 15:  // ModNeck
		case 20:  // DecapitateHead
		case 25:  // ModFaceJewelry
			return true;
		default:
			return false;
		}
	}

	struct RetainedGeometryBounds
	{
		float minimumX{ 0.0F };
		float minimumY{ 0.0F };
		float minimumZ{ 0.0F };
		float maximumX{ 0.0F };
		float maximumY{ 0.0F };
		float maximumZ{ 0.0F };
		std::uint32_t geometryCount{ 0 };
		bool valid{ false };
	};

	[[nodiscard]] inline bool AccumulateRetainedGeometrySphere(
		RetainedGeometryBounds& bounds,
		const float centerX,
		const float centerY,
		const float centerZ,
		const float radius) noexcept
	{
		const float values[]{ centerX, centerY, centerZ, radius };
		for (const float value : values) {
			if (!std::isfinite(value))
				return false;
		}
		if (radius <= 0.0F ||
			bounds.geometryCount >= kMaximumRetainedGeometryCount) {
			return false;
		}

		const float minimumX = centerX - radius;
		const float minimumY = centerY - radius;
		const float minimumZ = centerZ - radius;
		const float maximumX = centerX + radius;
		const float maximumY = centerY + radius;
		const float maximumZ = centerZ + radius;
		const float expanded[]{ minimumX, minimumY, minimumZ,
			maximumX, maximumY, maximumZ };
		for (const float value : expanded) {
			if (!std::isfinite(value))
				return false;
		}

		if (!bounds.valid) {
			bounds = {
				.minimumX = minimumX,
				.minimumY = minimumY,
				.minimumZ = minimumZ,
				.maximumX = maximumX,
				.maximumY = maximumY,
				.maximumZ = maximumZ,
				.geometryCount = 1,
				.valid = true
			};
			return true;
		}

		bounds.minimumX = (std::min)(bounds.minimumX, minimumX);
		bounds.minimumY = (std::min)(bounds.minimumY, minimumY);
		bounds.minimumZ = (std::min)(bounds.minimumZ, minimumZ);
		bounds.maximumX = (std::max)(bounds.maximumX, maximumX);
		bounds.maximumY = (std::max)(bounds.maximumY, maximumY);
		bounds.maximumZ = (std::max)(bounds.maximumZ, maximumZ);
		++bounds.geometryCount;
		return true;
	}

	struct RetainedBoundsAxialDepth
	{
		float nearestDepth{ 0.0F };
		bool valid{ false };
	};

	[[nodiscard]] inline RetainedBoundsAxialDepth
		ComputeRetainedBoundsNearestAxialDepth(
			const RetainedGeometryBounds& bounds,
			const float originX,
			const float originY,
			const float originZ,
			const float forwardX,
			const float forwardY,
			const float forwardZ) noexcept
	{
		const float values[]{
			bounds.minimumX, bounds.minimumY, bounds.minimumZ,
			bounds.maximumX, bounds.maximumY, bounds.maximumZ,
			originX, originY, originZ, forwardX, forwardY, forwardZ
		};
		for (const float value : values) {
			if (!std::isfinite(value))
				return {};
		}
		if (!bounds.valid || bounds.geometryCount == 0 ||
			bounds.geometryCount > kMaximumRetainedGeometryCount ||
			bounds.minimumX > bounds.maximumX ||
			bounds.minimumY > bounds.maximumY ||
			bounds.minimumZ > bounds.maximumZ) {
			return {};
		}

		const float forwardLengthSquared = forwardX * forwardX +
			forwardY * forwardY + forwardZ * forwardZ;
		if (!std::isfinite(forwardLengthSquared) ||
			forwardLengthSquared < 0.999F || forwardLengthSquared > 1.001F) {
			return {};
		}

		const float centerX = (bounds.minimumX + bounds.maximumX) * 0.5F;
		const float centerY = (bounds.minimumY + bounds.maximumY) * 0.5F;
		const float centerZ = (bounds.minimumZ + bounds.maximumZ) * 0.5F;
		const float halfX = (bounds.maximumX - bounds.minimumX) * 0.5F;
		const float halfY = (bounds.maximumY - bounds.minimumY) * 0.5F;
		const float halfZ = (bounds.maximumZ - bounds.minimumZ) * 0.5F;
		const float centerDepth = (centerX - originX) * forwardX +
			(centerY - originY) * forwardY +
			(centerZ - originZ) * forwardZ;
		const float axialSupport = std::abs(forwardX) * halfX +
			std::abs(forwardY) * halfY + std::abs(forwardZ) * halfZ;
		const float nearestDepth = centerDepth - axialSupport;
		return std::isfinite(nearestDepth) ?
			RetainedBoundsAxialDepth{ nearestDepth, true } :
			RetainedBoundsAxialDepth{};
	}

	enum class RejectReason : std::uint8_t
	{
		kNone,
		kNonFiniteInput,
		kInvalidSourceFrustum,
		kInvalidSourceProjection,
		kCameraNotBehindPlane,
		kCameraNotPlaneAligned,
		kPlaneBeyondFar,
		kRetainedAnchorOutsideDepthInterval,
		kInvalidOutput
	};

	struct Inputs
	{
		float sourceLeft{ 0.0F };
		float sourceRight{ 0.0F };
		float sourceTop{ 0.0F };
		float sourceBottom{ 0.0F };
		float sourceNear{ 0.0F };
		float sourceFar{ 0.0F };
		float sourceProjection33{ 0.0F };
		float sourceProjection43{ 0.0F };
		float sourceProjection34{ 0.0F };
		float sourceProjection44{ 0.0F };
		// SignedDistance(virtualPlane, captureEye): strictly negative.
		float cameraPlaneSignedDistance{ 0.0F };
		// dot(captureForward, virtualPlane.normal): approximately +1.
		float cameraForwardPlaneDot{ 0.0F };
		// dot(retained portrait anchor - captureEye, captureForward). This is a
		// point invariant only; it makes no claim about complete skinned bounds.
		float retainedAnchorDepth{ 0.0F };
	};

	struct Result
	{
		RejectReason rejectReason{ RejectReason::kNonFiniteInput };
		float left{ 0.0F };
		float right{ 0.0F };
		float top{ 0.0F };
		float bottom{ 0.0F };
		float nearPlane{ 0.0F };
		float farPlane{ 0.0F };
		float frustumScale{ 0.0F };
		float projection33{ 0.0F };
		float projection43{ 0.0F };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return rejectReason == RejectReason::kNone;
		}
	};

	/** Read-only comparison between the physical hand pane volume and portrait near. */
	struct PhysicalPaneAxialInputs
	{
		float centerDepth{ 0.0F };
		float normalForwardDot{ 0.0F };
		float tangentForwardDot{ 0.0F };
		float bitangentForwardDot{ 0.0F };
		float halfWidth{ 0.0F };
		float halfHeight{ 0.0F };
		float frontClearance{ 0.0F };
		float backClearance{ 0.0F };
		float virtualNearDepth{ 0.0F };
		float retainedAnchorDepth{ 0.0F };
	};

	struct PhysicalPaneAxialObservation
	{
		float physicalDeepestDepth{ 0.0F };
		float physicalMinusVirtual{ 0.0F };
		float retainedAnchorClearance{ 0.0F };
		bool physicalWouldAdvanceNear{ false };
		bool physicalAdvanceWouldViolateAnchorMargin{ false };
		bool valid{ false };
	};

	/**
	 * Fail-closed selection for replacing the virtual portrait near depth with the
	 * deepest point of the real capture-space pane.  The retained bound must be in
	 * the same camera/world domain and must conservatively contain the complete
	 * head presentation (including equipped headgear).
	 */
	struct PhysicalPaneNearSelection
	{
		float selectedNearDepth{ 0.0F };
		bool physicalWouldAdvanceNear{ false };
		bool retainedBoundsUnsafe{ false };
		bool clampedToRetainedBounds{ false };
		bool applied{ false };
		bool valid{ false };
	};

	struct PhysicalWorldHalfSpaceInputs
	{
		float centerX{ 0.0F };
		float centerY{ 0.0F };
		float centerZ{ 0.0F };
		float normalX{ 0.0F };
		float normalY{ 0.0F };
		float normalZ{ 0.0F };
		float frontClearance{ 0.0F };
		float backClearance{ 0.0F };
		float keepPointX{ 0.0F };
		float keepPointY{ 0.0F };
		float keepPointZ{ 0.0F };
	};

	struct PhysicalWorldHalfSpace
	{
		float normalX{ 0.0F };
		float normalY{ 0.0F };
		float normalZ{ 0.0F };
		float distance{ 0.0F };
		float keepPointSignedDistance{ 0.0F };
		bool normalFlipped{ false };
		bool valid{ false };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return valid;
		}
	};

	/**
	 * Select the player-facing outer face of the real pane volume.  The returned
	 * plane keeps the half-space containing keepPoint.  It is intentionally
	 * independent of the synthetic portrait eye and retained portrait depth: the
	 * caller applies it only to the WorldRoot cull, then removes it before the
	 * separately retained player root is culled through the unchanged camera.
	 */
	[[nodiscard]] inline PhysicalWorldHalfSpace SelectPhysicalWorldHalfSpace(
		const PhysicalWorldHalfSpaceInputs& inputs) noexcept
	{
		const float values[]{
			inputs.centerX, inputs.centerY, inputs.centerZ,
			inputs.normalX, inputs.normalY, inputs.normalZ,
			inputs.frontClearance, inputs.backClearance,
			inputs.keepPointX, inputs.keepPointY, inputs.keepPointZ
		};
		for (const float value : values) {
			if (!std::isfinite(value))
				return {};
		}
		if (inputs.frontClearance < 0.0F || inputs.backClearance < 0.0F)
			return {};

		const float normalLengthSquared = inputs.normalX * inputs.normalX +
			inputs.normalY * inputs.normalY + inputs.normalZ * inputs.normalZ;
		if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-8F)
			return {};
		const float inverseNormalLength = 1.0F / std::sqrt(normalLengthSquared);
		float normalX = inputs.normalX * inverseNormalLength;
		float normalY = inputs.normalY * inverseNormalLength;
		float normalZ = inputs.normalZ * inverseNormalLength;
		const float rawKeepDistance =
			(inputs.keepPointX - inputs.centerX) * normalX +
			(inputs.keepPointY - inputs.centerY) * normalY +
			(inputs.keepPointZ - inputs.centerZ) * normalZ;
		if (!std::isfinite(rawKeepDistance) || std::abs(rawKeepDistance) <=
				kMinimumPhysicalWorldKeepMargin) {
			return {};
		}

		const bool normalFlipped = rawKeepDistance < 0.0F;
		const float keepSideClearance = normalFlipped ?
			inputs.backClearance : inputs.frontClearance;
		if (normalFlipped) {
			normalX = -normalX;
			normalY = -normalY;
			normalZ = -normalZ;
		}
		const float distance = normalX * inputs.centerX +
			normalY * inputs.centerY + normalZ * inputs.centerZ +
			keepSideClearance;
		const float keepPointSignedDistance =
			normalX * inputs.keepPointX + normalY * inputs.keepPointY +
			normalZ * inputs.keepPointZ - distance;
		if (!std::isfinite(distance) || !std::isfinite(keepPointSignedDistance) ||
			keepPointSignedDistance <= kMinimumPhysicalWorldKeepMargin) {
			return {};
		}
		return {
			.normalX = normalX,
			.normalY = normalY,
			.normalZ = normalZ,
			.distance = distance,
			.keepPointSignedDistance = keepPointSignedDistance,
			.normalFlipped = normalFlipped,
			.valid = true
		};
	}

	[[nodiscard]] inline PhysicalPaneNearSelection SelectPhysicalPaneNearDepth(
		const float virtualNearDepth,
		const float physicalDeepestDepth,
		const float retainedBoundsNearestDepth,
		const float farPlane) noexcept
	{
		const float values[]{ virtualNearDepth, physicalDeepestDepth,
			retainedBoundsNearestDepth, farPlane };
		for (const float value : values) {
			if (!std::isfinite(value))
				return {};
		}
		if (virtualNearDepth <= kMinimumPlaneDepth ||
			farPlane <= virtualNearDepth + kMinimumFarGap ||
			retainedBoundsNearestDepth <= virtualNearDepth) {
			return {};
		}

		PhysicalPaneNearSelection output{
			.selectedNearDepth = virtualNearDepth,
			.physicalWouldAdvanceNear =
				physicalDeepestDepth > virtualNearDepth,
			.valid = true
		};
		if (!output.physicalWouldAdvanceNear)
			return output;

		const float retainedSafeLimit = retainedBoundsNearestDepth -
			kMinimumRetainedAnchorDepthMargin - kRetainedBoundsClampSlack;
		const float farSafeLimit = farPlane - kMinimumFarGap -
			kRetainedBoundsClampSlack;
		const float safeLimit = (std::min)(retainedSafeLimit, farSafeLimit);
		output.retainedBoundsUnsafe =
			physicalDeepestDepth > safeLimit;

		// A tilted or displaced physical hand pane can sit deeper than the nearest
		// retained face/helmet bound in the fixed selfie-camera domain.  Falling
		// all the way back to the virtual pane then leaves the entire interval
		// between those planes available to close room geometry.  Advance as far as
		// the exact retained bounds safely permit instead.  This preserves the
		// conventional projection used by particles, never crosses the complete
		// retained portrait bound, and still removes every camera-side object in the
		// portion of the physical-pane interval that can be proven safe.
		output.selectedNearDepth = (std::min)(physicalDeepestDepth, safeLimit);
		if (output.selectedNearDepth <=
			virtualNearDepth + kMinimumPlaneDepth) {
			output.selectedNearDepth = virtualNearDepth;
			return output;
		}
		output.clampedToRetainedBounds =
			output.selectedNearDepth < physicalDeepestDepth;
		output.applied = true;
		return output;
	}

	[[nodiscard]] inline PhysicalPaneAxialObservation ObservePhysicalPaneAxialDepth(
		const PhysicalPaneAxialInputs& inputs) noexcept
	{
		const float values[]{
			inputs.centerDepth,
			inputs.normalForwardDot,
			inputs.tangentForwardDot,
			inputs.bitangentForwardDot,
			inputs.halfWidth,
			inputs.halfHeight,
			inputs.frontClearance,
			inputs.backClearance,
			inputs.virtualNearDepth,
			inputs.retainedAnchorDepth
		};
		for (const float value : values) {
			if (!std::isfinite(value))
				return {};
		}
		if (inputs.halfWidth < 0.0F || inputs.halfHeight < 0.0F ||
			inputs.frontClearance < 0.0F || inputs.backClearance < 0.0F ||
			inputs.virtualNearDepth <= kMinimumPlaneDepth ||
			inputs.retainedAnchorDepth <= inputs.virtualNearDepth) {
			return {};
		}

		const float normalSupport = (std::max)(
			inputs.frontClearance * inputs.normalForwardDot,
			-inputs.backClearance * inputs.normalForwardDot);
		const float physicalDeepestDepth = inputs.centerDepth +
			inputs.halfWidth * std::abs(inputs.tangentForwardDot) +
			inputs.halfHeight * std::abs(inputs.bitangentForwardDot) +
			normalSupport;
		const float physicalMinusVirtual =
			physicalDeepestDepth - inputs.virtualNearDepth;
		const float retainedAnchorClearance =
			inputs.retainedAnchorDepth - physicalDeepestDepth;
		if (!std::isfinite(physicalDeepestDepth) ||
			!std::isfinite(physicalMinusVirtual) ||
			!std::isfinite(retainedAnchorClearance)) {
			return {};
		}
		const bool wouldAdvance = physicalMinusVirtual > 0.0F;
		return {
			.physicalDeepestDepth = physicalDeepestDepth,
			.physicalMinusVirtual = physicalMinusVirtual,
			.retainedAnchorClearance = retainedAnchorClearance,
			.physicalWouldAdvanceNear = wouldAdvance,
			.physicalAdvanceWouldViolateAnchorMargin = wouldAdvance &&
				retainedAnchorClearance <= kMinimumRetainedAnchorDepthMargin,
			.valid = true
		};
	}

	[[nodiscard]] inline bool NearlyEqual(
		const float left,
		const float right,
		const float tolerance = kProjectionTolerance) noexcept
	{
		if (!std::isfinite(left) || !std::isfinite(right) ||
			!std::isfinite(tolerance) || tolerance < 0.0F) {
			return false;
		}
		const float scale = (std::max)({ 1.0F, std::abs(left), std::abs(right) });
		return std::abs(left - right) <= tolerance * scale;
	}

	[[nodiscard]] inline Result Build(const Inputs& inputs) noexcept
	{
		const float values[]{
			inputs.sourceLeft,
			inputs.sourceRight,
			inputs.sourceTop,
			inputs.sourceBottom,
			inputs.sourceNear,
			inputs.sourceFar,
			inputs.sourceProjection33,
			inputs.sourceProjection43,
			inputs.sourceProjection34,
			inputs.sourceProjection44,
			inputs.cameraPlaneSignedDistance,
			inputs.cameraForwardPlaneDot,
			inputs.retainedAnchorDepth
		};
		for (const float value : values) {
			if (!std::isfinite(value))
				return { .rejectReason = RejectReason::kNonFiniteInput };
		}

		if (inputs.sourceLeft >= inputs.sourceRight ||
			inputs.sourceBottom >= inputs.sourceTop ||
			inputs.sourceNear <= kMinimumPlaneDepth ||
			inputs.sourceFar <= inputs.sourceNear + kMinimumFarGap) {
			return { .rejectReason = RejectReason::kInvalidSourceFrustum };
		}

		const float expectedSource33 =
			inputs.sourceFar / (inputs.sourceFar - inputs.sourceNear);
		const float expectedSource43 =
			-inputs.sourceNear * inputs.sourceFar /
			(inputs.sourceFar - inputs.sourceNear);
		if (!NearlyEqual(inputs.sourceProjection33, expectedSource33) ||
			!NearlyEqual(inputs.sourceProjection43, expectedSource43) ||
			!NearlyEqual(inputs.sourceProjection34, 1.0F) ||
			!NearlyEqual(inputs.sourceProjection44, 0.0F)) {
			return { .rejectReason = RejectReason::kInvalidSourceProjection };
		}

		if (inputs.cameraPlaneSignedDistance >= -kMinimumPlaneDepth) {
			return { .rejectReason = RejectReason::kCameraNotBehindPlane };
		}
		if (inputs.cameraForwardPlaneDot < kMinimumPlaneAlignment ||
			inputs.cameraForwardPlaneDot > kMaximumPlaneAlignment) {
			return { .rejectReason = RejectReason::kCameraNotPlaneAligned };
		}

		// The normal-aligned conventional near plane coincides with the exact
		// virtual pane. There is deliberately no additive bias. Source FOV is
		// preserved below even when the pane is nearer than the source near plane.
		const float paneDepth = -inputs.cameraPlaneSignedDistance;
		const float nearPlane = paneDepth;
		if (inputs.sourceFar <= nearPlane + kMinimumFarGap) {
			return { .rejectReason = RejectReason::kPlaneBeyondFar };
		}
		if (inputs.retainedAnchorDepth <=
				nearPlane + kMinimumRetainedAnchorDepthMargin ||
			inputs.retainedAnchorDepth >= inputs.sourceFar - kMinimumFarGap) {
			return {
				.rejectReason =
					RejectReason::kRetainedAnchorOutsideDepthInterval
			};
		}

		const float frustumScale = nearPlane / inputs.sourceNear;
		const float denominator = inputs.sourceFar - nearPlane;
		Result output{
			.rejectReason = RejectReason::kNone,
			.left = inputs.sourceLeft * frustumScale,
			.right = inputs.sourceRight * frustumScale,
			.top = inputs.sourceTop * frustumScale,
			.bottom = inputs.sourceBottom * frustumScale,
			.nearPlane = nearPlane,
			.farPlane = inputs.sourceFar,
			.frustumScale = frustumScale,
			.projection33 = inputs.sourceFar / denominator,
			.projection43 = -nearPlane * inputs.sourceFar / denominator
		};
		const float outputValues[]{
			output.left, output.right, output.top, output.bottom,
			output.nearPlane, output.farPlane, output.frustumScale,
			output.projection33, output.projection43
		};
		for (const float value : outputValues) {
			if (!std::isfinite(value))
				return { .rejectReason = RejectReason::kInvalidOutput };
		}
		if (output.left >= output.right || output.bottom >= output.top ||
			output.frustumScale <= 0.0F) {
			return { .rejectReason = RejectReason::kInvalidOutput };
		}
		return output;
	}

	[[nodiscard]] constexpr std::uint32_t SelectRetainedPlayerCullMode(
		const bool exactHandCapture,
		const bool retainedPlayerRoot,
		const std::uint32_t ordinaryCullMode) noexcept
	{
		// All-pass is scoped to the exact retained hand player root. WorldRoot and
		// every non-hand channel retain their existing mode.
		return exactHandCapture && retainedPlayerRoot ? 1u : ordinaryCullMode;
	}
}
