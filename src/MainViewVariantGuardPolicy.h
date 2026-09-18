#pragma once

#include <cstdint>

namespace MainViewVariantGuardPolicy
{
	// Distance between the delivery-time shadow-state main-view origin and the
	// live WorldRoot NiCamera origin above which the shadow-state block is
	// treated as a different camera variant. The 2026-08-19 third-person
	// session measured the wrong-variant gap at ~284 units (frame-pose-map,
	// 01:52:36) while healthy same-frame skew stays in single digits even at
	// sprint speed; 64 keeps an order-of-magnitude margin on both sides.
	inline constexpr float kOriginToleranceUnits = 64.0f;

	// Skyrim VR keeps two shadow-state eye slots whose origins straddle the
	// WorldRoot NiCamera by half the interpupillary distance: ~64 mm at the
	// engine's 1 unit = 1.428 cm scale is ~4.5 units eye-to-eye, so either
	// eye's posAdjust sits ~2.3 units from the camera origin the guard reads.
	// The delivery always feeds the guard eye 0, but even a right-eye sample
	// would stay an order of magnitude inside the variant tolerance.
	inline constexpr float kVRHalfInterpupillaryUnits = 2.3f;
	static_assert(kVRHalfInterpupillaryUnits * 8.0f < kOriginToleranceUnits,
		"VR half-IPD eye offset must sit far inside the variant tolerance");

	enum class Action : std::uint8_t
	{
		kAcceptLive,       // reads agree — draw with the live main view
		kUseHeldView,      // wrong variant, consecutive held view available
		kSkipDelivery,     // wrong variant, no usable held view — fail closed
		kAcceptUnverified  // ground-truth camera unavailable — pass through
	};

	struct Observation
	{
		bool cameraAvailable{ false };
		// Distance between the shadow-state origin and the NiCamera origin;
		// non-finite values classify as mismatched.
		float originDelta{ 0.0f };
		bool heldViewValid{ false };
		bool heldViewConsecutive{ false };
	};

	[[nodiscard]] constexpr bool Mismatch(float delta, float tolerance) noexcept
	{
		// Negated comparison so NaN deltas classify as mismatched.
		return !(delta <= tolerance);
	}

	[[nodiscard]] constexpr Action Classify(
		const Observation& observation,
		float tolerance = kOriginToleranceUnits) noexcept
	{
		if (!observation.cameraAvailable)
			return Action::kAcceptUnverified;
		if (!Mismatch(observation.originDelta, tolerance))
			return Action::kAcceptLive;
		if (observation.heldViewValid && observation.heldViewConsecutive)
			return Action::kUseHeldView;
		return Action::kSkipDelivery;
	}

	// Exact-main capture is intentionally deferred until the chained main-world
	// render has returned. The retained source view identifies and validates the
	// reflected capture, but it must never place the pane on a later main target:
	// the pane vertex shader writes SV_Position/depth into the current target and
	// therefore requires that target's verified raster view. Reflected UVs remain
	// projected by the capture's own reflected view-projection.
	//
	// Five producer channels share one private render per frame. A successful
	// mirror turn can consequently be reused by delivery frames source+1 through
	// source+5. This explicit window covers one complete contested rotation while
	// still failing closed after a missed/failed next mirror turn.
	//
	// The pair test compares two of the plugin's own samples (the capture's
	// retained source-main origin against the publication's reflected origin).
	// One unit is below the VR eye separation. A mono VR capture uses the
	// headset-center source pose, so its publication rebases the retained
	// eye-0 matrix to that exact source origin without changing world-to-clip
	// coordinates. Current per-eye pane placement still uses each native eye.
	inline constexpr float kDeferredPairOriginToleranceUnits = 1.0f;
	inline constexpr std::uint64_t kMaximumDeferredPairAgeFrames = 5;

	enum class DeferredPairAction : std::uint8_t
	{
		kUseCurrentRasterView,
		kSkipMissingPair,
		kSkipCurrentRasterViewUnverified,
		kSkipAgeOutsideWindow,
		kSkipOriginMismatch
	};

	struct DeferredPairObservation
	{
		bool pairedMainViewValid{ false };
		bool currentRasterMainViewVerified{ false };
		std::uint64_t sourceMainWorldFrame{ 0 };
		std::uint64_t currentMainWorldFrame{ 0 };
		// Distance between the publication's reflected origin and the exact
		// reflection of its retained source-main origin.  Non-finite is invalid.
		float reflectedOriginDelta{ 0.0f };
	};

	[[nodiscard]] constexpr std::uint64_t NextNonZeroFrame(
		std::uint64_t frame) noexcept
	{
		++frame;
		if (frame == 0)
			++frame;
		return frame;
	}

	[[nodiscard]] constexpr std::uint64_t DeferredPairAgeFrames(
		std::uint64_t sourceMainWorldFrame,
		std::uint64_t currentMainWorldFrame,
		std::uint64_t maximumAgeFrames = kMaximumDeferredPairAgeFrames) noexcept
	{
		if (sourceMainWorldFrame == 0 || currentMainWorldFrame == 0 ||
			maximumAgeFrames == 0) {
			return 0;
		}
		std::uint64_t frame = sourceMainWorldFrame;
		for (std::uint64_t age = 1; age <= maximumAgeFrames; ++age) {
			frame = NextNonZeroFrame(frame);
			if (frame == currentMainWorldFrame)
				return age;
		}
		return 0;
	}

	[[nodiscard]] constexpr DeferredPairAction ClassifyDeferredPair(
		const DeferredPairObservation& observation,
		float tolerance = kDeferredPairOriginToleranceUnits,
		std::uint64_t maximumAgeFrames = kMaximumDeferredPairAgeFrames) noexcept
	{
		if (!observation.pairedMainViewValid ||
			observation.sourceMainWorldFrame == 0 ||
			observation.currentMainWorldFrame == 0) {
			return DeferredPairAction::kSkipMissingPair;
		}
		if (!observation.currentRasterMainViewVerified)
			return DeferredPairAction::kSkipCurrentRasterViewUnverified;
		if (DeferredPairAgeFrames(observation.sourceMainWorldFrame,
				observation.currentMainWorldFrame, maximumAgeFrames) == 0)
			return DeferredPairAction::kSkipAgeOutsideWindow;
		if (Mismatch(observation.reflectedOriginDelta, tolerance))
			return DeferredPairAction::kSkipOriginMismatch;
		return DeferredPairAction::kUseCurrentRasterView;
	}
}
