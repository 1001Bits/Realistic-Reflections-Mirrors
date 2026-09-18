#pragma once

#include <cstdint>

namespace HandMirrorLastGoodPresentationPolicy
{
	enum class ReplayDecision : std::uint8_t
	{
		kReplay,
		kReplayToReplacement,
		kNoLastGood,
		kLifecycleTransition,
		kMirrorNotEquipped,
		kOwnerChanged,
		kPresentationModeChanged,
		kDeviceChanged,
		kTargetUnavailable,
		kLivePoseUnavailable
	};

	/**
	 * Raised (fixed portrait window) and lowered (physical reflected camera)
	 * presentations are different optical modes: a raised frame is a face crop
	 * glued to pane units, a lowered frame is a real reflection.  Replaying one
	 * mode's last-good frame while the live pane is in the other mode paints a
	 * stale portrait onto a lowered mirror (the V110 run replayed one raised
	 * frame for two minutes after the mirror was lowered, because every lowered
	 * fresh frame was reflected-uncovered).  Unknown means the live mode was not
	 * observed for this call and cannot prove a change.
	 */
	enum class PresentationMode : std::uint8_t
	{
		kUnknown,
		kLowered,
		kRaised
	};

	[[nodiscard]] constexpr PresentationMode ClassifyPresentationMode(
		const bool graphIndicatesRaisedPresentation) noexcept
	{
		return graphIndicatesRaisedPresentation ? PresentationMode::kRaised :
		                                          PresentationMode::kLowered;
	}

	/** True only when both modes are known and differ. */
	[[nodiscard]] constexpr bool PresentationModeChanged(
		const PresentationMode cached,
		const PresentationMode live) noexcept
	{
		return cached != PresentationMode::kUnknown &&
		       live != PresentationMode::kUnknown && cached != live;
	}

	enum class EquipmentEvidence : std::uint8_t
	{
		kExactEquipped,
		kPositivelyUnequipped,
		kIndeterminate
	};

	enum class ZeroReturnReplaySurface : std::uint8_t
	{
		kNone,
		kCurrentPresentationPose,
		kReturnSurface,
		kEntrySurface,
		kDetachedLastGoodSurface
	};

	[[nodiscard]] constexpr EquipmentEvidence ClassifyEquipment(
		const bool stableReadPair,
		const bool slotsConclusive,
		const bool approvedArmorInFirstSlot,
		const bool approvedArmorInThirdSlot,
		const bool exactIdentity) noexcept
	{
		if (!stableReadPair)
			return EquipmentEvidence::kIndeterminate;
		if (exactIdentity)
			return EquipmentEvidence::kExactEquipped;
		if (slotsConclusive && !approvedArmorInFirstSlot &&
			!approvedArmorInThirdSlot) {
			return EquipmentEvidence::kPositivelyUnequipped;
		}
		return EquipmentEvidence::kIndeterminate;
	}

	struct ReplayEvidence
	{
		bool lastGoodValid{ false };
		bool lifecycleStable{ false };
		EquipmentEvidence equipment{ EquipmentEvidence::kIndeterminate };
		bool exactOwnerUnchanged{ false };
		// Default false: an unobserved live mode cannot prove a change.  Callers
		// derive this from PresentationModeChanged(cached, live).
		bool presentationModeChanged{ false };
		bool deviceUnchanged{ false };
		bool liveTargetAvailable{ false };
		bool targetIdentityUnchanged{ false };
		bool targetReplacementProven{ false };
		bool requireCurrentPose{ false };
		bool currentPoseAvailable{ false };
	};

	[[nodiscard]] constexpr ReplayDecision EvaluateReplay(
		const ReplayEvidence& evidence) noexcept
	{
		if (!evidence.lastGoodValid)
			return ReplayDecision::kNoLastGood;
		if (!evidence.lifecycleStable)
			return ReplayDecision::kLifecycleTransition;
		if (evidence.equipment == EquipmentEvidence::kPositivelyUnequipped)
			return ReplayDecision::kMirrorNotEquipped;
		if (evidence.equipment == EquipmentEvidence::kExactEquipped &&
			!evidence.exactOwnerUnchanged)
			return ReplayDecision::kOwnerChanged;
		if (evidence.presentationModeChanged)
			return ReplayDecision::kPresentationModeChanged;
		if (evidence.requireCurrentPose && !evidence.currentPoseAvailable)
			return ReplayDecision::kLivePoseUnavailable;
		if (!evidence.deviceUnchanged)
			return ReplayDecision::kDeviceChanged;
		if (!evidence.liveTargetAvailable)
			return ReplayDecision::kTargetUnavailable;
		if (!evidence.targetIdentityUnchanged) {
			return evidence.targetReplacementProven ?
				ReplayDecision::kReplayToReplacement :
				ReplayDecision::kTargetUnavailable;
		}
		return ReplayDecision::kReplay;
	}

	/**
	 * A zero-pane-callback call may lose only its return-side surface observer.
	 * The surface frozen at entry belongs to this same native call and is usable
	 * only when that observer is explicitly absent (never invalid/conflicting) and
	 * the independently retained receipt and presentation color destination prove
	 * unchanged and the complete returned color/depth pair is retained. Skyrim may
	 * rotate that returned depth attachment without changing the presentation color
	 * destination. If the observer is absent at both seams, the detached last-good
	 * presentation may retain its own fully validated pane basis. Equipment, owner,
	 * device, and lifecycle evidence are still re-proved by EvaluateReplay before
	 * that image can draw.
	 */
	[[nodiscard]] constexpr ZeroReturnReplaySurface SelectZeroReturnReplaySurface(
		const bool entryToReturnReceiptAndTargetStable,
		const bool returnSurfaceValid,
		const bool returnSurfaceObserverExplicitlyAbsent,
		const bool entrySurfaceValid,
		const bool detachedLastGoodSurfaceValid,
		const bool currentPresentationPoseValid = false,
		const bool requireCurrentFrameRaster = false) noexcept
	{
		if (!entryToReturnReceiptAndTargetStable)
			return ZeroReturnReplaySurface::kNone;
		// Capture observers can be unavailable solely because hidden animation
		// is stale. Independently sampled current first-person pose wins; this
		// never allows a detached cached pane position to follow a moving frame.
		if (currentPresentationPoseValid)
			return ZeroReturnReplaySurface::kCurrentPresentationPose;
		// Live movement cannot use an entry, late-return or cached pose with a
		// different camera. If the native frame did not draw, neither does its pane.
		if (requireCurrentFrameRaster)
			return ZeroReturnReplaySurface::kNone;
		if (returnSurfaceValid)
			return ZeroReturnReplaySurface::kReturnSurface;
		if (!returnSurfaceObserverExplicitlyAbsent)
			return ZeroReturnReplaySurface::kNone;
		if (entrySurfaceValid)
			return ZeroReturnReplaySurface::kEntrySurface;
		return detachedLastGoodSurfaceValid ?
			ZeroReturnReplaySurface::kDetachedLastGoodSurface :
			ZeroReturnReplaySurface::kNone;
	}

	/**
	 * Only a complete final presentation whose entire authored reflected aperture
	 * is covered may replace last-good. Main-visible coverage alone is weaker: a
	 * later main view can reveal a portion which the cached projection never owned.
	 */
	[[nodiscard]] constexpr bool CanCommit(
		const bool outerFinal,
		const bool fullCoverageDraw,
		const bool fullReflectedApertureCoverage,
		const bool exactOwner,
		const bool colorResourceValid,
		const bool deviceValid) noexcept
	{
		return outerFinal && fullCoverageDraw &&
		       fullReflectedApertureCoverage && exactOwner &&
		       colorResourceValid && deviceValid;
	}

	/** Keep the retained publication generation coherent across a fresh pose. */
	template <class MovingSurface>
	constexpr void NormalizeReplaySurfaceGeneration(
		MovingSurface& current,
		const MovingSurface& cached) noexcept
	{
		current.owner.stableGeneration = cached.owner.stableGeneration;
		current.pose.stableGeneration = cached.pose.stableGeneration;
	}
}
