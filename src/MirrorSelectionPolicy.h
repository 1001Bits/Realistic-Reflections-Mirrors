#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace MirrorSelectionPolicy
{
	// Mirror references are authored product surfaces. Keep their registry bounded
	// independently of loaded-world enumeration so a pathological save cannot grow
	// render-thread selection work without limit.
	inline constexpr std::size_t kMaximumTrackedCandidates = 32;
	inline constexpr std::int64_t kMainWorldVisibilityLeaseMilliseconds = 500;
	inline constexpr std::int64_t kDeliveryOwnershipLeaseMilliseconds = 1000;
	inline constexpr std::int64_t
		kValidatedVisibleContinuityLeaseMilliseconds = 1000;
	inline constexpr float kStickyDistanceRatioSquared = 2.25F;
	inline constexpr std::size_t kNoCandidate = (std::numeric_limits<std::size_t>::max)();

	enum class ReferenceStatus : std::uint8_t
	{
		kEligible,
		kMissing,
		kBaseMismatch,
		kDeleted,
		kDisabled,
		kUnloaded,
		kRootMissing,
		kParentCellMissing,
		kParentCellDetached
	};

	[[nodiscard]] constexpr bool IsTransientLifecycleStatus(
		const ReferenceStatus status) noexcept
	{
		switch (status) {
		case ReferenceStatus::kDisabled:
		case ReferenceStatus::kUnloaded:
		case ReferenceStatus::kRootMissing:
		case ReferenceStatus::kParentCellMissing:
		case ReferenceStatus::kParentCellDetached:
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr bool RequiresPermanentRemoval(
		const ReferenceStatus status) noexcept
	{
		return status == ReferenceStatus::kMissing ||
		       status == ReferenceStatus::kBaseMismatch ||
		       status == ReferenceStatus::kDeleted;
	}

	struct ReferenceObservation
	{
		bool referencePresent{ false };
		bool baseMatches{ false };
		bool deleted{ false };
		bool disabled{ false };
		bool threeDLoaded{ false };
		bool rootPresent{ false };
		bool parentCellPresent{ false };
		bool parentCellAttached{ false };
	};

	[[nodiscard]] constexpr ReferenceStatus ClassifyReference(
		const ReferenceObservation& observation) noexcept
	{
		if (!observation.referencePresent)
			return ReferenceStatus::kMissing;
		if (!observation.baseMatches)
			return ReferenceStatus::kBaseMismatch;
		if (observation.deleted)
			return ReferenceStatus::kDeleted;
		if (observation.disabled)
			return ReferenceStatus::kDisabled;
		if (!observation.threeDLoaded)
			return ReferenceStatus::kUnloaded;
		if (!observation.rootPresent)
			return ReferenceStatus::kRootMissing;
		if (!observation.parentCellPresent)
			return ReferenceStatus::kParentCellMissing;
		if (!observation.parentCellAttached)
			return ReferenceStatus::kParentCellDetached;
		return ReferenceStatus::kEligible;
	}

	[[nodiscard]] constexpr bool IsEligible(
		const ReferenceObservation& observation) noexcept
	{
		return ClassifyReference(observation) == ReferenceStatus::kEligible;
	}

	enum class RegistryAdmission : std::uint8_t
	{
		kRejectInvalid,
		kUpdateExisting,
		kInsert,
		kRejectCapacity
	};

	[[nodiscard]] constexpr RegistryAdmission ClassifyRegistryAdmission(
		const std::size_t currentSize,
		const bool alreadyTracked,
		const std::uint32_t formID,
		const std::size_t maximumSize = kMaximumTrackedCandidates) noexcept
	{
		if (formID == 0)
			return RegistryAdmission::kRejectInvalid;
		if (alreadyTracked)
			return RegistryAdmission::kUpdateExisting;
		return currentSize < maximumSize ?
			RegistryAdmission::kInsert : RegistryAdmission::kRejectCapacity;
	}

	struct Candidate
	{
		std::uint32_t formID{ 0 };
		std::uint64_t generation{ 0 };
		float distanceSquared{ 0.0F };
		bool selectable{ false };
		// Evidence that this exact owned pane entered Skyrim's main-world lighting
		// setup during the bounded recent lease. This is deliberately stronger than
		// distance: a loaded mirror behind a wall or in an unseen part of the cell
		// must not displace the pane Skyrim is actually submitting.
		bool recentlyVisible{ false };
	};

	struct Request
	{
		std::uint32_t ownerFormID{ 0 };
		std::uint64_t ownerGeneration{ 0 };
		bool ownerLeaseActive{ false };
		bool visibilityEvidenceAvailable{ false };
		float stickyDistanceRatioSquared{ kStickyDistanceRatioSquared };
	};

	/** Which exact-generation observation currently protects the sticky owner. */
	enum class OwnershipLeaseSource : std::uint8_t
	{
		kNone,
		kSuccessfulDelivery,
		kValidatedVisibleContinuity
	};

	struct OwnershipLeaseObservation
	{
		std::uint32_t ownerFormID{ 0 };
		std::uint64_t ownerGeneration{ 0 };
		bool ownerIdentityTracked{ false };
		std::uint32_t successfulDeliveryFormID{ 0 };
		std::uint64_t successfulDeliveryGeneration{ 0 };
		bool successfulDeliveryRecent{ false };
		std::uint32_t validatedVisibleContinuityFormID{ 0 };
		std::uint64_t validatedVisibleContinuityGeneration{ 0 };
		bool validatedVisibleContinuityRecent{ false };
	};

	[[nodiscard]] constexpr OwnershipLeaseSource ClassifyOwnershipLease(
		const OwnershipLeaseObservation& observation) noexcept
	{
		if (!observation.ownerIdentityTracked || observation.ownerFormID == 0 ||
			observation.ownerGeneration == 0) {
			return OwnershipLeaseSource::kNone;
		}
		if (observation.successfulDeliveryRecent &&
			observation.successfulDeliveryFormID == observation.ownerFormID &&
			observation.successfulDeliveryGeneration ==
				observation.ownerGeneration) {
			return OwnershipLeaseSource::kSuccessfulDelivery;
		}
		if (observation.validatedVisibleContinuityRecent &&
			observation.validatedVisibleContinuityFormID ==
				observation.ownerFormID &&
			observation.validatedVisibleContinuityGeneration ==
				observation.ownerGeneration) {
			return OwnershipLeaseSource::kValidatedVisibleContinuity;
		}
		return OwnershipLeaseSource::kNone;
	}

	enum class ValidatedVisibleContinuityAction : std::uint8_t
	{
		kDisabled,
		kIdentityOrLocationRejected,
		kProjectionInvalid,
		kMainViewNotVisible,
		kAttemptNativeVisibilityProof
	};

	struct ValidatedVisibleContinuityObservation
	{
		bool featureEnabled{ false };
		bool exactIdentityCurrent{ false };
		bool locationCurrent{ false };
		bool projectionValid{ false };
		bool mainViewVisible{ false };
	};

	/**
	 * Admit only the read-only native-visibility proof.  This does not itself
	 * renew ownership; MirrorRecognition must still require a recent exact-pane
	 * engine submission for the same candidate generation.
	 */
	[[nodiscard]] constexpr ValidatedVisibleContinuityAction
		ClassifyValidatedVisibleContinuity(
			const ValidatedVisibleContinuityObservation& observation) noexcept
	{
		if (!observation.featureEnabled)
			return ValidatedVisibleContinuityAction::kDisabled;
		if (!observation.exactIdentityCurrent || !observation.locationCurrent)
			return ValidatedVisibleContinuityAction::kIdentityOrLocationRejected;
		if (!observation.projectionValid)
			return ValidatedVisibleContinuityAction::kProjectionInvalid;
		if (!observation.mainViewVisible)
			return ValidatedVisibleContinuityAction::kMainViewNotVisible;
		return ValidatedVisibleContinuityAction::kAttemptNativeVisibilityProof;
	}

	enum class DecisionAction : std::uint8_t
	{
		kNone,
		kAcquireNearest,
		kAcquireVisible,
		kKeepOwner,
		kSwitchNearest,
		kSwitchVisible,
		kHoldOwnerLease,
		kRejectUnseen
	};

	struct Decision
	{
		std::size_t index{ kNoCandidate };
		DecisionAction action{ DecisionAction::kNone };
	};

	[[nodiscard]] constexpr bool DefersCaptureWithoutInvalidation(
		const DecisionAction action) noexcept
	{
		return action == DecisionAction::kHoldOwnerLease;
	}

	[[nodiscard]] constexpr bool ReplacesExistingOwner(
		const DecisionAction action) noexcept
	{
		return action == DecisionAction::kSwitchNearest ||
		       action == DecisionAction::kSwitchVisible;
	}

	struct PublicPromotionObservation
	{
		bool mirrorProductCommitted{ false };
		bool cleanBaseReady{ false };
		bool lightingObserverReady{ false };
	};

	[[nodiscard]] constexpr bool PublicSelectionReady(
		const PublicPromotionObservation& observation) noexcept
	{
		return observation.mirrorProductCommitted && observation.cleanBaseReady &&
		       observation.lightingObserverReady;
	}

	[[nodiscard]] constexpr bool ValidCandidate(const Candidate& candidate) noexcept
	{
		return candidate.selectable && candidate.formID != 0 &&
			candidate.generation != 0 && candidate.distanceSquared >= 0.0F &&
			candidate.distanceSquared <= (std::numeric_limits<float>::max)();
	}

	[[nodiscard]] constexpr bool Less(
		const Candidate& left,
		const Candidate& right) noexcept
	{
		if (left.distanceSquared != right.distanceSquared)
			return left.distanceSquared < right.distanceSquared;
		if (left.formID != right.formID)
			return left.formID < right.formID;
		return left.generation < right.generation;
	}

	/**
	 * Select one owner for the singleton mirror target/publication.
	 *
	 * This is intentionally stable ownership, not round-robin fairness: the target
	 * is reused on every capture, so rotating among panes would overwrite the sole
	 * published texture and make every noncurrent pane sample the wrong surface.
	 * Once main-world visibility evidence exists, only recently submitted panes may
	 * acquire ownership. A recently delivered owner also receives a bounded grace
	 * interval in which a transient transform/load observation yields no selection
	 * instead of handing the target to an unseen challenger.
	 */
	[[nodiscard]] constexpr Decision Select(
		const std::span<const Candidate> candidates,
		const Request request) noexcept
	{
		std::size_t nearest = kNoCandidate;
		std::size_t nearestVisible = kNoCandidate;
		std::size_t owner = kNoCandidate;
		for (std::size_t index = 0; index < candidates.size(); ++index) {
			const auto& candidate = candidates[index];
			if (!ValidCandidate(candidate))
				continue;
			if (nearest == kNoCandidate || Less(candidate, candidates[nearest]))
				nearest = index;
			if (candidate.recentlyVisible &&
				(nearestVisible == kNoCandidate ||
					Less(candidate, candidates[nearestVisible]))) {
				nearestVisible = index;
			}
			if (candidate.formID == request.ownerFormID &&
				candidate.generation == request.ownerGeneration) {
				owner = index;
			}
		}

		if (request.ownerFormID != 0 && request.ownerLeaseActive) {
			if (owner != kNoCandidate)
				return { owner, DecisionAction::kKeepOwner };
			return { kNoCandidate, DecisionAction::kHoldOwnerLease };
		}

		if (owner != kNoCandidate && candidates[owner].recentlyVisible)
			return { owner, DecisionAction::kKeepOwner };

		if (request.visibilityEvidenceAvailable) {
			if (nearestVisible == kNoCandidate)
				return { kNoCandidate, DecisionAction::kRejectUnseen };
			if (nearestVisible == owner)
				return { owner, DecisionAction::kKeepOwner };
			return { nearestVisible, request.ownerFormID == 0 ?
				DecisionAction::kAcquireVisible : DecisionAction::kSwitchVisible };
		}

		if (nearest == kNoCandidate)
			return {};
		if (owner == kNoCandidate) {
			return { nearest, request.ownerFormID == 0 ?
				DecisionAction::kAcquireNearest : DecisionAction::kSwitchNearest };
		}

		const float threshold = candidates[nearest].distanceSquared *
			request.stickyDistanceRatioSquared;
		if (threshold >= 0.0F &&
			candidates[owner].distanceSquared <= threshold) {
			return { owner, DecisionAction::kKeepOwner };
		}
		return { nearest, DecisionAction::kSwitchNearest };
	}
}
