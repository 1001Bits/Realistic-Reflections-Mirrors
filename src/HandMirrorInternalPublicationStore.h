#pragma once

#include "HandMirrorRuntimeBridgePolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace HandMirrorInternalPublicationStore
{
	using HandMirrorRuntimeBridgePolicy::CaptureAttemptIdentity;
	using HandMirrorRuntimeBridgePolicy::IsValidHandCaptureAttempt;
	using HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity;
	using HandMirrorRuntimeBridgePolicy::MovingSurfaceSample;
	using HandMirrorRuntimeBridgePolicy::PhaseSnapshotIdentity;
	using HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity;
	using HandMirrorRuntimeBridgePolicy::PublicationAudience;
	using HandMirrorRuntimeBridgePolicy::PublicationConsumer;
	using HandMirrorRuntimeBridgePolicy::PublicationIdentity;
	using HandMirrorRuntimeBridgePolicy::SameCaptureAttempt;
	using HandMirrorRuntimeBridgePolicy::SameMovingSurface;
	using HandMirrorRuntimeBridgePolicy::SnapshotMatchesSurface;
	using HandMirrorRuntimeBridgePolicy::SnapshotPhase;
	using HandMirrorRuntimeBridgePolicy::TokenIssueReceipt;
	using HandMirrorRuntimeBridgePolicy::TokenIssuerState;

	// Engine-independent first slice only.  The store owns value identities and
	// state transitions; a future runtime adapter must own serialization, D3D
	// objects, callback drain, and every native cleanup operation.
	inline constexpr std::size_t kPhysicalRoleCapacity = 2;
	inline constexpr std::uint8_t kMaximumBoundedPaneNormalReturnCount = 3;
	inline constexpr std::uint32_t kInvalidPhysicalRole =
		(std::numeric_limits<std::uint32_t>::max)();
	inline constexpr bool kPlanarAPIV1AcquireAllowed = false;
	static_assert(kPhysicalRoleCapacity == 2);

	enum class StorePhase : std::uint8_t
	{
		kUninitialized,
		kReady,
		kQuarantined
	};

	enum class FaultReason : std::uint8_t
	{
		kNone,
		kProtocolReplay,
		kInternalInvariant,
		kTokenExhaustion,
		kCleanupProof,
		kRetirementCapacity,
		kRetirementRelease,
		kInvalidationOrder,
		kAbnormalNativeReturn,
		kExternalNativeFault
	};

	enum class RetirementCause : std::uint8_t
	{
		kUnknown,
		kSuccessorAttempt,
		kFirstPersonConsumed,
		kOuterFirstPersonDark,
		kSourceAdvanced,
		kEquipInvalidated,
		kLoadInvalidated,
		kTargetReallocated
	};

	struct TokenSeeds
	{
		TokenIssuerState ownerLease{};
		TokenIssuerState attempt{};
		TokenIssuerState phaseSnapshot{};
		TokenIssuerState capture{};
		TokenIssuerState publication{};
		TokenIssuerState retirement{};

		constexpr bool operator==(const TokenSeeds&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool SameMovingSurfaceValue(
		const MovingSurfaceSample& left,
		const MovingSurfaceSample& right) noexcept
	{
		return left.owner == right.owner && left.pose == right.pose &&
		       left.sourceSequence == right.sourceSequence;
	}

	[[nodiscard]] constexpr bool SameCaptureAttemptValue(
		const CaptureAttemptIdentity& left,
		const CaptureAttemptIdentity& right) noexcept
	{
		return left.owner == right.owner &&
		       left.ownerLeaseSequence == right.ownerLeaseSequence &&
		       left.attemptSequence == right.attemptSequence &&
		       SameMovingSurfaceValue(left.surface, right.surface) &&
		       left.privateTarget == right.privateTarget;
	}

	[[nodiscard]] constexpr bool SamePublicationValue(
		const PublicationIdentity& left,
		const PublicationIdentity& right) noexcept
	{
		return SameCaptureAttemptValue(left.attempt, right.attempt) &&
		       left.captureSequence == right.captureSequence &&
		       left.nativePublicationToken == right.nativePublicationToken &&
		       left.audience == right.audience;
	}

	struct PhaseSnapshotReservation
	{
		PhaseSnapshotIdentity raised{};
		PhaseSnapshotIdentity postCaptureReturn{};
		PhaseSnapshotIdentity visibleDraw{};

		constexpr bool operator==(
			const PhaseSnapshotReservation&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool SamePhaseSnapshotReservationValue(
		const PhaseSnapshotReservation& left,
		const PhaseSnapshotReservation& right) noexcept
	{
		return left == right;
	}

	[[nodiscard]] constexpr bool IsValidPhaseSnapshotReservation(
		const PhaseSnapshotReservation& reservation) noexcept
	{
		const auto maximum =
			HandMirrorRuntimeBridgePolicy::kMaximumIssuedToken;
		return reservation.raised.snapshotToken != 0 &&
		       reservation.raised.snapshotToken != maximum &&
		       reservation.postCaptureReturn.snapshotToken ==
			       reservation.raised.snapshotToken + 1 &&
		       reservation.postCaptureReturn.snapshotToken != maximum &&
		       reservation.visibleDraw.snapshotToken ==
			       reservation.postCaptureReturn.snapshotToken + 1 &&
		       reservation.raised.sourceSequence != 0 &&
		       reservation.raised.poseSequence != 0 &&
		       reservation.raised.mainViewFrame != 0 &&
		       reservation.raised.sourceSequence ==
			       reservation.postCaptureReturn.sourceSequence &&
		       reservation.raised.sourceSequence ==
			       reservation.visibleDraw.sourceSequence &&
		       reservation.raised.poseSequence ==
			       reservation.postCaptureReturn.poseSequence &&
		       reservation.raised.poseSequence ==
			       reservation.visibleDraw.poseSequence &&
		       reservation.raised.mainViewFrame ==
			       reservation.postCaptureReturn.mainViewFrame &&
		       reservation.raised.mainViewFrame ==
			       reservation.visibleDraw.mainViewFrame &&
		       reservation.raised.phase == SnapshotPhase::kRaised &&
		       reservation.postCaptureReturn.phase ==
			       SnapshotPhase::kPostCaptureReturn &&
		       reservation.visibleDraw.phase == SnapshotPhase::kVisibleDraw;
	}

	[[nodiscard]] constexpr bool PhaseSnapshotReservationMatchesSurface(
		const PhaseSnapshotReservation& reservation,
		const MovingSurfaceSample& surface) noexcept
	{
		return IsValidPhaseSnapshotReservation(reservation) &&
		       HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample(surface) &&
		       SnapshotMatchesSurface(
			       reservation.raised, SnapshotPhase::kRaised, surface) &&
		       SnapshotMatchesSurface(reservation.postCaptureReturn,
			       SnapshotPhase::kPostCaptureReturn, surface) &&
		       SnapshotMatchesSurface(
			       reservation.visibleDraw, SnapshotPhase::kVisibleDraw, surface);
	}

	struct PublicationSnapshot
	{
		PublicationIdentity publication{};
		PhaseSnapshotReservation phaseSnapshots{};
		std::uint32_t physicalRole{ kInvalidPhysicalRole };
		bool valid{ false };

		[[nodiscard]] constexpr bool operator==(
			const PublicationSnapshot& other) const noexcept
		{
			return SamePublicationValue(publication, other.publication) &&
			       SamePhaseSnapshotReservationValue(
				       phaseSnapshots, other.phaseSnapshots) &&
			       physicalRole == other.physicalRole && valid == other.valid;
		}
	};

	[[nodiscard]] constexpr bool IsExactInternalHandPublication(
		const PublicationSnapshot& snapshot) noexcept
	{
		using namespace HandMirrorRuntimeBridgePolicy;
		return snapshot.valid &&
		       snapshot.physicalRole < kPhysicalRoleCapacity &&
		       IsValidPublication(snapshot.publication) &&
		       snapshot.publication.captureSequence !=
			       snapshot.publication.nativePublicationToken &&
		       snapshot.publication.captureSequence !=
			       HandMirrorRuntimeBridgePolicy::kMaximumIssuedToken &&
		       snapshot.publication.nativePublicationToken ==
			       snapshot.publication.captureSequence + 1 &&
		       PhaseSnapshotReservationMatchesSurface(
			       snapshot.phaseSnapshots,
			       snapshot.publication.attempt.surface) &&
		       snapshot.publication.attempt.owner.kind == OwnerKind::kHand &&
		       snapshot.publication.audience ==
			       PublicationAudience::kInternalHandOnly &&
		       AllowsPublicationConsumer(
			       snapshot.publication.attempt.owner,
			       snapshot.publication.audience,
			       PublicationConsumer::kInternalHandPane);
	}

	[[nodiscard]] constexpr bool SamePublicationSnapshot(
		const PublicationSnapshot& left,
		const PublicationSnapshot& right) noexcept
	{
		using namespace HandMirrorRuntimeBridgePolicy;
		return IsExactInternalHandPublication(left) &&
		       IsExactInternalHandPublication(right) &&
		       left.physicalRole == right.physicalRole &&
		       SamePhaseSnapshotReservationValue(
			       left.phaseSnapshots, right.phaseSnapshots) &&
		       SamePublication(left.publication, right.publication);
	}

	struct RetirementReceipt
	{
		PublicationSnapshot retired{};
		MirrorOwnerIdentity successorOwner{};
		std::uint64_t successorOwnerLeaseToken{ 0 };
		std::uint64_t retirementToken{ 0 };
		RetirementCause cause{ RetirementCause::kUnknown };
		bool publicationNoLongerAcquirable{ false };
		bool physicalRoleHeldUntilReaderDrain{ false };

		[[nodiscard]] constexpr bool operator==(
			const RetirementReceipt& other) const noexcept
		{
			return retired == other.retired &&
			       successorOwner == other.successorOwner &&
			       successorOwnerLeaseToken ==
				       other.successorOwnerLeaseToken &&
			       retirementToken == other.retirementToken &&
			       cause == other.cause &&
			       publicationNoLongerAcquirable ==
				       other.publicationNoLongerAcquirable &&
			       physicalRoleHeldUntilReaderDrain ==
				       other.physicalRoleHeldUntilReaderDrain;
		}
	};

	[[nodiscard]] constexpr bool IsValidRetirementReceipt(
		const RetirementReceipt& receipt) noexcept
	{
		using namespace HandMirrorRuntimeBridgePolicy;
		if (!IsExactInternalHandPublication(receipt.retired) ||
			receipt.retirementToken == 0 ||
			receipt.cause == RetirementCause::kUnknown ||
			!receipt.publicationNoLongerAcquirable ||
			!receipt.physicalRoleHeldUntilReaderDrain) {
			return false;
		}
		if (receipt.cause == RetirementCause::kSuccessorAttempt) {
			return IsValidOwner(receipt.successorOwner) &&
			       receipt.successorOwner.kind == OwnerKind::kHand &&
			       receipt.successorOwnerLeaseToken != 0;
		}
		return IsDarkOwner(receipt.successorOwner) &&
		       receipt.successorOwnerLeaseToken == 0;
	}

	struct RetirementReleaseProof
	{
		RetirementReceipt receipt{};
		bool everyInternalReaderDrained{ false };
		bool targetUnboundFromEveryContext{ false };

		constexpr bool operator==(const RetirementReleaseProof&) const noexcept =
			default;
	};

	struct BeginResult
	{
		CaptureAttemptIdentity attempt{};
		PhaseSnapshotReservation phaseSnapshots{};
		RetirementReceipt retiredPriorPublication{};
	};

	enum class BeginStatus : std::uint8_t
	{
		kReady,
		kUnavailable,
		kQuarantined,
		kInvalidMovingSurface,
		kWrongSource,
		kNotFirstPersonHand,
		kAttemptAlreadyActive,
		kNoStagingRole,
		kTokenExhausted
	};

	struct PublicationCleanupProof
	{
		CaptureAttemptIdentity attempt{};
		MovingSurfaceSample postReturnSurface{};
		PhaseSnapshotIdentity postReturnSnapshot{};
		PrivateTargetIdentity unboundTarget{};
		bool privateCaptureReturnedNormally{ false };
		bool exactItemSubtreeRestoredAndReadBack{ false };
		bool passLocalShadowStateClosed{ false };
		bool reflectedCameraOverrideClosed{ false };
		bool playerInclusionScopeClosed{ false };
		bool privatePassTLSClosed{ false };
		bool targetEndedAndUnbound{ false };
		bool activeTargetIdentityCleared{ false };
		bool mainCameraAccumulatorAndRendererRestored{ false };
		bool retainedSceneValuesReleased{ false };
		bool mipFinalizedOnlyAfterCompleteCleanup{ false };
	};

	enum class CompleteStatus : std::uint8_t
	{
		kPublished,
		kUnavailable,
		kQuarantined,
		kNoActiveAttempt,
		kStaleAttempt,
		kCleanupRejected,
		kTokenExhausted
	};

	enum class AbortStatus : std::uint8_t
	{
		kAborted,
		kUnavailable,
		kQuarantined,
		kNoActiveAttempt,
		kStaleAttempt
	};

	struct InternalAcquireRequest
	{
		MirrorOwnerIdentity owner{};
		MovingSurfaceSample surface{};
		std::uint64_t sourceSequence{ 0 };
		PublicationConsumer consumer{ PublicationConsumer::kInternalHandPane };
	};

	enum class AcquireStatus : std::uint8_t
	{
		kAcquired,
		kUnavailable,
		kQuarantined,
		kNoPublication,
		kAlreadyConsumed,
		kWrongSource,
		kIdentityMismatch,
		kAudienceRejected
	};

	struct FirstPersonConsumptionProof
	{
		PublicationSnapshot acquired{};
		MovingSurfaceSample visibleSurface{};
		PhaseSnapshotIdentity visibleDrawSnapshot{};
		std::uint64_t sourceSequence{ 0 };
		PublicationConsumer consumer{ PublicationConsumer::kInternalHandPane };
		bool exactFirstPersonOpportunity{ false };
		bool firstPersonTLSActive{ false };
		bool exactPaneSubmittedByCurrentMainView{ false };
		bool nativePaneDrawReturnedNormally{ false };
		bool writableMainColorDepthAndViewProven{ false };
		bool customRendererSucceeded{ false };
	};

	struct ConsumptionResult
	{
		PublicationSnapshot consumed{};
		RetirementReceipt retirement{};
	};

	/**
	 * A publication may be retired dark after the outer first-person native call
	 * returns normally even when no exact pane delivery was admissible.  This is a
	 * distinct protocol from visible consumption: the pane-delivery facts must
	 * truthfully agree with the bounded observed normal-return count, and this
	 * proof can never produce a visible result.
	 */
	struct OuterFirstPersonDarkRetirementProof
	{
		PublicationSnapshot currentPublication{};
		std::uint64_t sourceSequence{ 0 };
		PublicationConsumer consumer{ PublicationConsumer::kInternalHandPane };
		std::uint8_t observedPaneNormalReturnCount{ 0 };
		bool outerFirstPersonTLSActive{ false };
		bool outerFirstPersonReturnedNormally{ false };
		bool exactPaneSubmittedByCurrentMainView{ false };
		bool nativePaneDrawReturnedNormally{ false };
	};

	struct DarkRetirementResult
	{
		PublicationSnapshot retired{};
		RetirementReceipt retirement{};
	};

	enum class DarkRetirementStatus : std::uint8_t
	{
		kRetiredDark,
		kUnavailable,
		kQuarantined,
		kNoPublication,
		kAlreadyRetired,
		kInvalidProof,
		kAbnormalOuterReturn,
		kTokenExhausted
	};

	enum class ConsumeStatus : std::uint8_t
	{
		kConsumedVisible,
		kConsumedDarkMismatch,
		kConsumedDarkDrawFailure,
		kUnavailable,
		kQuarantined,
		kNoPublication,
		kAlreadyConsumed,
		kNotFirstPersonOpportunity,
		kAbnormalNativeReturn,
		kTokenExhausted
	};

	enum class ReleaseStatus : std::uint8_t
	{
		kReleased,
		kUnavailable,
		kQuarantined,
		kNoRetirement,
		kInvalidProof
	};

	struct InvalidationResult
	{
		RetirementReceipt retirement{};
		bool activeAttemptAborted{ false };
	};

	enum class InvalidationStatus : std::uint8_t
	{
		kNoState,
		kAttemptAborted,
		kPublicationRetired,
		kPublicationRetiredAndAttemptAborted,
		kUnavailable,
		kQuarantined,
		kInvalidEvidence,
		kTokenExhausted
	};

	enum class OwnerLeaseBurnPolicy : std::uint8_t
	{
		kRequireNoPendingRetirement,
		kAllowSourceAdvancedPendingRetirement
	};

	struct AuditSnapshot
	{
		StorePhase phase{ StorePhase::kUninitialized };
		FaultReason fault{ FaultReason::kNone };
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t loadGeneration{ 0 };
		std::size_t currentPublicationCount{ 0 };
		std::size_t activeAttemptCount{ 0 };
		std::size_t pendingRetirementCount{ 0 };
		std::uint64_t lastConsumedPublicationToken{ 0 };
		bool identityStateScrubbed{ false };
	};

	class Store
	{
	public:
		using PhysicalTargets =
			std::array<PrivateTargetIdentity, kPhysicalRoleCapacity>;

		[[nodiscard]] bool Initialize(
			const PhysicalTargets& targets,
			const std::uint64_t sourceSequence,
			const std::uint64_t loadGeneration,
			const TokenSeeds seeds = {}) noexcept
		{
			if (phase_ != StorePhase::kUninitialized)
				return false;
			if (sourceSequence == 0 || loadGeneration == 0 ||
				!ValidTokenSeeds(seeds) ||
				!HandMirrorRuntimeBridgePolicy::
					ArePrivateTargetsPhysicallyDisjoint(targets[0], targets[1])) {
				return false;
			}
			physicalTargets_ = targets;
			sourceSequence_ = sourceSequence;
			loadGeneration_ = loadGeneration;
			tokens_ = seeds;
			phase_ = StorePhase::kReady;
			return true;
		}

		[[nodiscard]] BeginStatus Begin(
			const MovingSurfaceSample& surface,
			BeginResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return BeginStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return BeginStatus::kUnavailable;
			if (!ValidateInternalState())
				return QuarantineBegin(FaultReason::kInternalInvariant);
			if (!HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample(surface))
				return BeginStatus::kInvalidMovingSurface;
			if (surface.sourceSequence != sourceSequence_)
				return BeginStatus::kWrongSource;
			if (surface.owner.perspective !=
				HandMirrorContentRuntimePolicy::Perspective::kFirstPerson &&
				surface.owner.perspective !=
				HandMirrorContentRuntimePolicy::Perspective::kVRFirstPerson) {
				return BeginStatus::kNotFirstPersonHand;
			}
			if (active_.valid)
				return QuarantineBegin(
					FaultReason::kProtocolReplay,
					BeginStatus::kAttemptAlreadyActive);

			const auto stagingRole = FindFreeRole();
			if (stagingRole == kInvalidPhysicalRole)
				return BeginStatus::kNoStagingRole;

			const auto owner =
				HandMirrorRuntimeBridgePolicy::MakeHandOwner(surface.owner);
			TokenSeeds nextTokens = tokens_;
			TokenIssueReceipt ownerLeaseIssue{};
			TokenIssueReceipt attemptIssue{};
			TokenIssueReceipt retirementIssue{};
			PhaseSnapshotReservation phaseSnapshots{};
			if (!Issue(nextTokens.ownerLease, ownerLeaseIssue) ||
				!Issue(nextTokens.attempt, attemptIssue) ||
				!IssuePhaseSnapshotReservation(
					nextTokens.phaseSnapshot, surface, phaseSnapshots) ||
				(current_.valid &&
				 !Issue(nextTokens.retirement, retirementIssue))) {
				return QuarantineBegin(FaultReason::kTokenExhaustion);
			}

			RetirementReceipt priorRetirement{};
			if (current_.valid) {
				if (retired_.valid)
					return QuarantineBegin(FaultReason::kRetirementCapacity);
				priorRetirement = MakeRetirement(
					current_.snapshot, owner, ownerLeaseIssue.issuedToken,
					retirementIssue.issuedToken,
					RetirementCause::kSuccessorAttempt);
				if (!IsValidRetirementReceipt(priorRetirement))
					return QuarantineBegin(FaultReason::kInternalInvariant);
			}

			CaptureAttemptIdentity attempt{
				.owner = owner,
				.ownerLeaseSequence = ownerLeaseIssue.issuedToken,
				.attemptSequence = attemptIssue.issuedToken,
				.surface = surface,
				.privateTarget = physicalTargets_[stagingRole]
			};
			if (!IsValidHandCaptureAttempt(attempt))
				return QuarantineBegin(FaultReason::kInternalInvariant);

			// Commit only after every token, target, receipt, and attempt has been
			// proven.  No failed Begin can partially retire or reserve a role.
			tokens_ = nextTokens;
			if (current_.valid) {
				retired_ = { priorRetirement, true };
				current_ = {};
			}
			active_ = { attempt, phaseSnapshots, stagingRole, true };
			output = { attempt, phaseSnapshots, priorRetirement };
			return BeginStatus::kReady;
		}

		/**
		 * Burn one owner lease which a singleton grant preissued but whose exact
		 * fence reservation can no longer reach Begin.  The default policy retains
		 * the strict no-pending-retirement contract.  The explicit relaxed policy
		 * admits only the dark source-advance retirement state left after the
		 * publication and active attempt have both disappeared.
		 */
		[[nodiscard]] bool BurnUnusedOwnerLease(
			const std::uint64_t expectedLease,
			const OwnerLeaseBurnPolicy policy =
				OwnerLeaseBurnPolicy::kRequireNoPendingRetirement) noexcept
		{
			if (phase_ != StorePhase::kReady || expectedLease == 0 ||
				active_.valid || !ValidateInternalState()) {
				return false;
			}
			if (policy != OwnerLeaseBurnPolicy::kRequireNoPendingRetirement &&
				policy !=
					OwnerLeaseBurnPolicy::kAllowSourceAdvancedPendingRetirement) {
				return false;
			}
			if (retired_.valid &&
				(policy != OwnerLeaseBurnPolicy::
						kAllowSourceAdvancedPendingRetirement ||
				 current_.valid ||
				 !IsValidRetirementReceipt(retired_.receipt) ||
				 retired_.receipt.cause != RetirementCause::kSourceAdvanced ||
				 !HandMirrorRuntimeBridgePolicy::IsDarkOwner(
					 retired_.receipt.successorOwner) ||
				 retired_.receipt.successorOwnerLeaseToken != 0)) {
				return false;
			}
			TokenIssuerState next = tokens_.ownerLease;
			TokenIssueReceipt issue{};
			if (!Issue(next, issue) || issue.issuedToken != expectedLease)
				return false;
			tokens_.ownerLease = next;
			return true;
		}

		[[nodiscard]] AbortStatus Abort(
			const CaptureAttemptIdentity& attempt) noexcept
		{
			if (phase_ == StorePhase::kQuarantined)
				return AbortStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return AbortStatus::kUnavailable;
			if (!ValidateInternalState()) {
				Quarantine(FaultReason::kInternalInvariant);
				return AbortStatus::kQuarantined;
			}
			if (!active_.valid) {
				Quarantine(FaultReason::kProtocolReplay);
				return AbortStatus::kNoActiveAttempt;
			}
			if (!SameCaptureAttempt(active_.attempt, attempt)) {
				Quarantine(FaultReason::kProtocolReplay);
				return AbortStatus::kStaleAttempt;
			}
			active_ = {};
			return AbortStatus::kAborted;
		}

		[[nodiscard]] CompleteStatus Complete(
			const PublicationCleanupProof& proof,
			PublicationSnapshot& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return CompleteStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return CompleteStatus::kUnavailable;
			if (!ValidateInternalState())
				return QuarantineComplete(FaultReason::kInternalInvariant);
			if (!active_.valid)
				return QuarantineComplete(
					FaultReason::kProtocolReplay,
					CompleteStatus::kNoActiveAttempt);
			if (!SameCaptureAttempt(active_.attempt, proof.attempt))
				return QuarantineComplete(FaultReason::kProtocolReplay,
					CompleteStatus::kStaleAttempt);
			if (proof.postReturnSnapshot !=
				active_.phaseSnapshots.postCaptureReturn) {
				return QuarantineComplete(FaultReason::kProtocolReplay);
			}
			if (!ValidateCleanupProof(proof))
				return QuarantineComplete(FaultReason::kCleanupProof,
					CompleteStatus::kCleanupRejected);

			TokenSeeds nextTokens = tokens_;
			TokenIssueReceipt captureIssue{};
			TokenIssueReceipt publicationIssue{};
			if (!IssueCapturePublicationPair(
					nextTokens, captureIssue, publicationIssue)) {
				return QuarantineComplete(FaultReason::kTokenExhaustion,
					CompleteStatus::kTokenExhausted);
			}

			PublicationSnapshot next{
				.publication = {
					.attempt = active_.attempt,
					.captureSequence = captureIssue.issuedToken,
					.nativePublicationToken = publicationIssue.issuedToken,
					.audience = PublicationAudience::kInternalHandOnly },
				.phaseSnapshots = active_.phaseSnapshots,
				.physicalRole = active_.physicalRole,
				.valid = true
			};
			if (!IsExactInternalHandPublication(next))
				return QuarantineComplete(FaultReason::kInternalInvariant);

			// Atomic role swap: only a complete cleanup proof can turn staging into
			// the sole current publication.  The former current role was already
			// retired at Begin and cannot be restored as continuity.
			tokens_ = nextTokens;
			current_ = { next, true };
			active_ = {};
			output = next;
			return CompleteStatus::kPublished;
		}

		[[nodiscard]] AcquireStatus TryAcquireInternalHand(
			const InternalAcquireRequest& request,
			PublicationSnapshot& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return AcquireStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return AcquireStatus::kUnavailable;
			if (!ValidateInternalState()) {
				Quarantine(FaultReason::kInternalInvariant);
				return AcquireStatus::kQuarantined;
			}
			if (!current_.valid) {
				return AcquireStatus::kNoPublication;
			}
			if (request.sourceSequence == 0 ||
				request.sourceSequence != sourceSequence_ ||
				request.sourceSequence !=
					current_.snapshot.publication.attempt.surface.sourceSequence) {
				return AcquireStatus::kWrongSource;
			}
			if (!HandMirrorRuntimeBridgePolicy::SameOwner(
					request.owner,
					current_.snapshot.publication.attempt.owner) ||
				!SameMovingSurface(
					request.surface,
					current_.snapshot.publication.attempt.surface)) {
				return AcquireStatus::kIdentityMismatch;
			}
			if (!HandMirrorRuntimeBridgePolicy::AllowsPublicationConsumer(
					current_.snapshot.publication.attempt.owner,
					current_.snapshot.publication.audience,
					request.consumer)) {
				return AcquireStatus::kAudienceRejected;
			}
			output = current_.snapshot;
			return AcquireStatus::kAcquired;
		}

		[[nodiscard]] bool TryAcquireForPlanarAPIV1(
			PublicationSnapshot& output) const noexcept
		{
			output = {};
			return kPlanarAPIV1AcquireAllowed;
		}

		[[nodiscard]] ConsumeStatus ConsumeAtFirstPersonOpportunity(
			const FirstPersonConsumptionProof& proof,
			ConsumptionResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return ConsumeStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return ConsumeStatus::kUnavailable;
			if (!ValidateInternalState())
				return QuarantineConsume(FaultReason::kInternalInvariant);
			if (!proof.exactFirstPersonOpportunity)
				return ConsumeStatus::kNotFirstPersonOpportunity;
			if (!current_.valid) {
				if (proof.acquired.valid &&
					proof.acquired.publication.nativePublicationToken ==
						lastConsumedPublicationToken_) {
					return ConsumeStatus::kAlreadyConsumed;
				}
				return ConsumeStatus::kNoPublication;
			}
			if (proof.visibleDrawSnapshot !=
				current_.snapshot.phaseSnapshots.visibleDraw) {
				return QuarantineConsume(FaultReason::kProtocolReplay);
			}
			if (!proof.nativePaneDrawReturnedNormally)
				return QuarantineConsume(
					FaultReason::kAbnormalNativeReturn,
					ConsumeStatus::kAbnormalNativeReturn);

			TokenSeeds nextTokens = tokens_;
			TokenIssueReceipt retirementIssue{};
			if (!Issue(nextTokens.retirement, retirementIssue))
				return QuarantineConsume(FaultReason::kTokenExhaustion,
					ConsumeStatus::kTokenExhausted);
			if (retired_.valid)
				return QuarantineConsume(FaultReason::kRetirementCapacity);

			const bool exactIdentity =
				SamePublicationSnapshot(proof.acquired, current_.snapshot) &&
				proof.sourceSequence == sourceSequence_ &&
				proof.sourceSequence == current_.snapshot.publication.attempt
					.surface.sourceSequence &&
				SameMovingSurface(
					proof.visibleSurface,
					current_.snapshot.publication.attempt.surface) &&
				SnapshotMatchesSurface(
					proof.visibleDrawSnapshot,
					SnapshotPhase::kVisibleDraw,
					proof.visibleSurface) &&
				proof.consumer == PublicationConsumer::kInternalHandPane &&
				proof.firstPersonTLSActive &&
				proof.exactPaneSubmittedByCurrentMainView;

			const auto consumed = current_.snapshot;
			const auto receipt = MakeRetirement(
				consumed, {}, 0, retirementIssue.issuedToken,
				RetirementCause::kFirstPersonConsumed);
			if (!IsValidRetirementReceipt(receipt))
				return QuarantineConsume(FaultReason::kInternalInvariant);

			// The first exact opportunity is one-shot even if matching, target
			// validation, or the custom draw fails.  Dark is the only fallback.
			tokens_ = nextTokens;
			lastConsumedPublicationToken_ =
				consumed.publication.nativePublicationToken;
			retired_ = { receipt, true };
			current_ = {};
			output = { consumed, receipt };
			if (!exactIdentity)
				return ConsumeStatus::kConsumedDarkMismatch;
			if (!proof.writableMainColorDepthAndViewProven ||
				!proof.customRendererSucceeded) {
				return ConsumeStatus::kConsumedDarkDrawFailure;
			}
			return ConsumeStatus::kConsumedVisible;
		}

		[[nodiscard]] DarkRetirementStatus RetireDarkAfterOuterFirstPersonReturn(
			const OuterFirstPersonDarkRetirementProof& proof,
			DarkRetirementResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return DarkRetirementStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return DarkRetirementStatus::kUnavailable;
			if (!ValidateInternalState()) {
				Quarantine(FaultReason::kInternalInvariant);
				return DarkRetirementStatus::kQuarantined;
			}
			if (!proof.outerFirstPersonReturnedNormally) {
				Quarantine(FaultReason::kAbnormalNativeReturn);
				return DarkRetirementStatus::kAbnormalOuterReturn;
			}
			const bool paneEnteredAndReturned =
				proof.observedPaneNormalReturnCount != 0;
			if (!proof.outerFirstPersonTLSActive ||
				proof.observedPaneNormalReturnCount >
					kMaximumBoundedPaneNormalReturnCount ||
				proof.exactPaneSubmittedByCurrentMainView !=
					paneEnteredAndReturned ||
				proof.nativePaneDrawReturnedNormally !=
					paneEnteredAndReturned) {
				Quarantine(FaultReason::kProtocolReplay);
				return DarkRetirementStatus::kInvalidProof;
			}
			if (!current_.valid) {
				if (proof.currentPublication.valid &&
					proof.currentPublication.publication.nativePublicationToken ==
						lastConsumedPublicationToken_) {
					return DarkRetirementStatus::kAlreadyRetired;
				}
				return DarkRetirementStatus::kNoPublication;
			}
			if (!SamePublicationSnapshot(
					proof.currentPublication, current_.snapshot) ||
				proof.sourceSequence == 0 ||
				proof.sourceSequence != sourceSequence_ ||
				proof.sourceSequence != current_.snapshot.publication.attempt.surface
					.sourceSequence ||
				proof.consumer != PublicationConsumer::kInternalHandPane ||
				current_.snapshot.publication.audience !=
					PublicationAudience::kInternalHandOnly ||
				!HandMirrorRuntimeBridgePolicy::AllowsPublicationConsumer(
					current_.snapshot.publication.attempt.owner,
					current_.snapshot.publication.audience, proof.consumer)) {
				Quarantine(FaultReason::kProtocolReplay);
				return DarkRetirementStatus::kInvalidProof;
			}
			if (retired_.valid) {
				Quarantine(FaultReason::kRetirementCapacity);
				return DarkRetirementStatus::kQuarantined;
			}

			TokenSeeds nextTokens = tokens_;
			TokenIssueReceipt retirementIssue{};
			if (!Issue(nextTokens.retirement, retirementIssue)) {
				Quarantine(FaultReason::kTokenExhaustion);
				return DarkRetirementStatus::kTokenExhausted;
			}
			const auto retired = current_.snapshot;
			const auto receipt = MakeRetirement(
				retired, {}, 0, retirementIssue.issuedToken,
				RetirementCause::kOuterFirstPersonDark);
			if (!IsValidRetirementReceipt(receipt)) {
				Quarantine(FaultReason::kInternalInvariant);
				return DarkRetirementStatus::kQuarantined;
			}

			tokens_ = nextTokens;
			lastConsumedPublicationToken_ =
				retired.publication.nativePublicationToken;
			retired_ = { receipt, true };
			current_ = {};
			output = { retired, receipt };
			return DarkRetirementStatus::kRetiredDark;
		}

		[[nodiscard]] ReleaseStatus ReleaseRetired(
			const RetirementReleaseProof& proof) noexcept
		{
			if (phase_ == StorePhase::kQuarantined)
				return ReleaseStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return ReleaseStatus::kUnavailable;
			if (!ValidateInternalState()) {
				Quarantine(FaultReason::kInternalInvariant);
				return ReleaseStatus::kQuarantined;
			}
			if (!retired_.valid)
				return ReleaseStatus::kNoRetirement;
			if (!IsValidRetirementReceipt(proof.receipt) ||
				proof.receipt != retired_.receipt ||
				!proof.everyInternalReaderDrained ||
				!proof.targetUnboundFromEveryContext) {
				Quarantine(FaultReason::kRetirementRelease);
				return ReleaseStatus::kInvalidProof;
			}
			retired_ = {};
			return ReleaseStatus::kReleased;
		}

		[[nodiscard]] InvalidationStatus OnSourceAdvanced(
			const std::uint64_t nextSourceSequence,
			InvalidationResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return InvalidationStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return InvalidationStatus::kUnavailable;
			if (nextSourceSequence == 0 ||
				nextSourceSequence <= sourceSequence_) {
				return QuarantineInvalidation(FaultReason::kInvalidationOrder);
			}
			const auto status = ForceInvalidate(
				RetirementCause::kSourceAdvanced, output);
			if (phase_ == StorePhase::kReady)
				sourceSequence_ = nextSourceSequence;
			return status;
		}

		[[nodiscard]] InvalidationStatus OnEquipInvalidated(
			const HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity& owner,
			InvalidationResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return InvalidationStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return InvalidationStatus::kUnavailable;
			if (!HandMirrorContentRuntimePolicy::
					IsValidHandMirrorOwnerIdentity(owner) ||
				!AllLiveOwnersMatch(owner)) {
				return QuarantineInvalidation(FaultReason::kInvalidationOrder);
			}
			return ForceInvalidate(RetirementCause::kEquipInvalidated, output);
		}

		[[nodiscard]] InvalidationStatus OnLoadInvalidated(
			const std::uint64_t nextLoadGeneration,
			InvalidationResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return InvalidationStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return InvalidationStatus::kUnavailable;
			if (nextLoadGeneration == 0 ||
				nextLoadGeneration <= loadGeneration_) {
				return QuarantineInvalidation(FaultReason::kInvalidationOrder);
			}
			const auto status = ForceInvalidate(
				RetirementCause::kLoadInvalidated, output);
			if (phase_ == StorePhase::kReady)
				loadGeneration_ = nextLoadGeneration;
			return status;
		}

		/**
		 * Re-identify against a freshly allocated pair of private targets.
		 *
		 * The hand allocation stopped being the fixed 2048 on 2026-09-14, when both
		 * hand resolutions became 512-4096 settings, and the quality levels moved
		 * them again on 2026-09-15.  SecondView therefore re-creates both private
		 * targets mid-session whenever the player changes a level or a slider, and
		 * `allocationGeneration` changes with every re-creation, so the identities
		 * this store was initialised with become legitimately stale.  That is a
		 * reallocation, not a protocol violation.
		 *
		 * Every publication built against the retired allocation is invalidated
		 * first, so nothing that survives this call can name a texture the renderer
		 * has released.  The load generation advances for the same reason it does
		 * across a save load: an outstanding receipt from the old allocation must
		 * not be replayable against the new one.
		 */
		[[nodiscard]] InvalidationStatus Retarget(
			const PhysicalTargets& targets,
			const std::uint64_t nextLoadGeneration,
			InvalidationResult& output) noexcept
		{
			output = {};
			if (phase_ == StorePhase::kQuarantined)
				return InvalidationStatus::kQuarantined;
			if (phase_ != StorePhase::kReady)
				return InvalidationStatus::kUnavailable;
			if (nextLoadGeneration == 0 ||
				nextLoadGeneration <= loadGeneration_ ||
				!HandMirrorRuntimeBridgePolicy::ArePrivateTargetsPhysicallyDisjoint(
					targets[0], targets[1])) {
				return QuarantineInvalidation(FaultReason::kInvalidationOrder);
			}
			const auto status = ForceInvalidate(
				RetirementCause::kTargetReallocated, output);
			if (phase_ == StorePhase::kReady) {
				loadGeneration_ = nextLoadGeneration;
				physicalTargets_ = targets;
			}
			return status;
		}

		[[nodiscard]] AuditSnapshot Inspect() const noexcept
		{
			return {
				.phase = phase_,
				.fault = fault_,
				.sourceSequence = sourceSequence_,
				.loadGeneration = loadGeneration_,
				.currentPublicationCount = current_.valid ? 1U : 0U,
				.activeAttemptCount = active_.valid ? 1U : 0U,
				.pendingRetirementCount = retired_.valid ? 1U : 0U,
				.lastConsumedPublicationToken =
					lastConsumedPublicationToken_,
				.identityStateScrubbed = IdentityStateScrubbed()
			};
		}

		/** Terminally scrub every value authorization after an adapter/native fault. */
		void QuarantineExternalNativeFault() noexcept
		{
			Quarantine(FaultReason::kExternalNativeFault);
		}

	private:
		struct ActiveAttempt
		{
			CaptureAttemptIdentity attempt{};
			PhaseSnapshotReservation phaseSnapshots{};
			std::uint32_t physicalRole{ kInvalidPhysicalRole };
			bool valid{ false };
		};

		struct CurrentPublication
		{
			PublicationSnapshot snapshot{};
			bool valid{ false };
		};

		struct PendingRetirement
		{
			RetirementReceipt receipt{};
			bool valid{ false };
		};

		[[nodiscard]] static constexpr bool ValidTokenSeeds(
			const TokenSeeds& seeds) noexcept
		{
			using HandMirrorRuntimeBridgePolicy::IsValidTokenIssuerState;
			return IsValidTokenIssuerState(seeds.ownerLease) &&
			       IsValidTokenIssuerState(seeds.attempt) &&
			       IsValidTokenIssuerState(seeds.phaseSnapshot) &&
			       IsValidTokenIssuerState(seeds.capture) &&
			       IsValidTokenIssuerState(seeds.publication) &&
			       IsValidTokenIssuerState(seeds.retirement) &&
			       seeds.capture == seeds.publication;
		}

		[[nodiscard]] static constexpr bool Issue(
			TokenIssuerState& state,
			TokenIssueReceipt& receipt) noexcept
		{
			const auto result =
				HandMirrorRuntimeBridgePolicy::IssueNextToken(state);
			if (result.status !=
				HandMirrorRuntimeBridgePolicy::TokenIssueStatus::kIssued) {
				receipt = {};
				return false;
			}
			state = result.nextState;
			receipt = result.receipt;
			return true;
		}

		[[nodiscard]] static constexpr PhaseSnapshotIdentity MakePhaseSnapshot(
			const std::uint64_t token,
			const SnapshotPhase phase,
			const MovingSurfaceSample& surface) noexcept
		{
			return {
				.snapshotToken = token,
				.sourceSequence = surface.sourceSequence,
				.poseSequence = surface.pose.poseSequence,
				.mainViewFrame = surface.pose.mainViewFrame,
				.phase = phase
			};
		}

		/**
		 * Begin owns all three phase-snapshot identities.  Reserving the complete
		 * raised/post-return/visible chain up front makes an aborted attempt burn the
		 * chain and prevents later native evidence from selecting its own token.
		 */
		[[nodiscard]] static constexpr bool IssuePhaseSnapshotReservation(
			TokenIssuerState& state,
			const MovingSurfaceSample& surface,
			PhaseSnapshotReservation& reservation) noexcept
		{
			reservation = {};
			TokenIssueReceipt raisedIssue{};
			TokenIssueReceipt postReturnIssue{};
			TokenIssueReceipt visibleDrawIssue{};
			if (!Issue(state, raisedIssue) ||
				!Issue(state, postReturnIssue) ||
				!Issue(state, visibleDrawIssue)) {
				return false;
			}

			reservation = {
				.raised = MakePhaseSnapshot(
					raisedIssue.issuedToken, SnapshotPhase::kRaised, surface),
				.postCaptureReturn = MakePhaseSnapshot(
					postReturnIssue.issuedToken,
					SnapshotPhase::kPostCaptureReturn, surface),
				.visibleDraw = MakePhaseSnapshot(
					visibleDrawIssue.issuedToken,
					SnapshotPhase::kVisibleDraw, surface)
			};
			return postReturnIssue.predecessorToken ==
				       raisedIssue.issuedToken &&
			       visibleDrawIssue.predecessorToken ==
				       postReturnIssue.issuedToken &&
			       PhaseSnapshotReservationMatchesSurface(reservation, surface);
		}

		/**
		 * Capture and publication are distinct consecutive receipts on one
		 * globally nonreplaying completion chain.  The two persisted issuer states
		 * must agree at transaction boundaries; they are advanced on local copies
		 * and committed together only after both issues succeed.
		 */
		[[nodiscard]] static constexpr bool IssueCapturePublicationPair(
			TokenSeeds& seeds,
			TokenIssueReceipt& captureReceipt,
			TokenIssueReceipt& publicationReceipt) noexcept
		{
			captureReceipt = {};
			publicationReceipt = {};
			if (seeds.capture != seeds.publication ||
				!Issue(seeds.capture, captureReceipt)) {
				return false;
			}
			seeds.publication = seeds.capture;
			if (!Issue(seeds.publication, publicationReceipt))
				return false;
			seeds.capture = seeds.publication;
			return captureReceipt.issuedToken !=
				       publicationReceipt.issuedToken &&
			       captureReceipt.issuedToken !=
				       HandMirrorRuntimeBridgePolicy::kMaximumIssuedToken &&
			       publicationReceipt.predecessorToken ==
				       captureReceipt.issuedToken &&
			       publicationReceipt.issuedToken ==
				       captureReceipt.issuedToken + 1;
		}

		[[nodiscard]] static constexpr RetirementReceipt MakeRetirement(
			const PublicationSnapshot& publication,
			const MirrorOwnerIdentity& successor,
			const std::uint64_t successorLease,
			const std::uint64_t retirementToken,
			const RetirementCause cause) noexcept
		{
			return {
				.retired = publication,
				.successorOwner = successor,
				.successorOwnerLeaseToken = successorLease,
				.retirementToken = retirementToken,
				.cause = cause,
				.publicationNoLongerAcquirable = true,
				.physicalRoleHeldUntilReaderDrain = true
			};
		}

		[[nodiscard]] bool ValidateInternalState() const noexcept
		{
			if (phase_ != StorePhase::kReady || sourceSequence_ == 0 ||
				loadGeneration_ == 0 || !ValidTokenSeeds(tokens_) ||
				!HandMirrorRuntimeBridgePolicy::
					ArePrivateTargetsPhysicallyDisjoint(
						physicalTargets_[0], physicalTargets_[1])) {
				return false;
			}
			if (current_.valid) {
				if (!IsExactInternalHandPublication(current_.snapshot) ||
					current_.snapshot.physicalRole >= kPhysicalRoleCapacity ||
					current_.snapshot.publication.attempt.privateTarget !=
						physicalTargets_[current_.snapshot.physicalRole] ||
					current_.snapshot.publication.attempt.surface.sourceSequence !=
						sourceSequence_) {
					return false;
				}
			} else if (current_.snapshot != PublicationSnapshot{}) {
				return false;
			}
			if (active_.valid) {
				if (!IsValidHandCaptureAttempt(active_.attempt) ||
					!PhaseSnapshotReservationMatchesSurface(
						active_.phaseSnapshots, active_.attempt.surface) ||
					active_.physicalRole >= kPhysicalRoleCapacity ||
					active_.attempt.privateTarget !=
						physicalTargets_[active_.physicalRole] ||
					active_.attempt.surface.sourceSequence != sourceSequence_) {
					return false;
				}
			} else if (!SameCaptureAttemptValue(
					active_.attempt, CaptureAttemptIdentity{}) ||
				!SamePhaseSnapshotReservationValue(
					active_.phaseSnapshots, PhaseSnapshotReservation{}) ||
				active_.physicalRole != kInvalidPhysicalRole) {
				return false;
			}
			if (retired_.valid) {
				if (!IsValidRetirementReceipt(retired_.receipt))
					return false;
			} else if (retired_.receipt != RetirementReceipt{}) {
				return false;
			}
			const auto currentRole = current_.valid ?
				current_.snapshot.physicalRole : kInvalidPhysicalRole;
			const auto activeRole =
				active_.valid ? active_.physicalRole : kInvalidPhysicalRole;
			const auto retiredRole = retired_.valid ?
				retired_.receipt.retired.physicalRole : kInvalidPhysicalRole;
			return (currentRole == kInvalidPhysicalRole ||
					activeRole == kInvalidPhysicalRole ||
					currentRole != activeRole) &&
			       (currentRole == kInvalidPhysicalRole ||
					retiredRole == kInvalidPhysicalRole ||
					currentRole != retiredRole) &&
			       (activeRole == kInvalidPhysicalRole ||
					retiredRole == kInvalidPhysicalRole ||
					activeRole != retiredRole);
		}

		[[nodiscard]] std::uint32_t FindFreeRole() const noexcept
		{
			for (std::uint32_t role = 0;
				role < static_cast<std::uint32_t>(kPhysicalRoleCapacity);
				++role) {
				if (current_.valid && current_.snapshot.physicalRole == role)
					continue;
				if (active_.valid && active_.physicalRole == role)
					continue;
				if (retired_.valid &&
					retired_.receipt.retired.physicalRole == role) {
					continue;
				}
				return role;
			}
			return kInvalidPhysicalRole;
		}

		[[nodiscard]] bool ValidateCleanupProof(
			const PublicationCleanupProof& proof) const noexcept
		{
			return active_.valid &&
			       SameCaptureAttempt(active_.attempt, proof.attempt) &&
			       SameMovingSurface(
				       active_.attempt.surface, proof.postReturnSurface) &&
			       SnapshotMatchesSurface(
				       proof.postReturnSnapshot,
				       SnapshotPhase::kPostCaptureReturn,
				       proof.postReturnSurface) &&
			       proof.unboundTarget == active_.attempt.privateTarget &&
			       proof.privateCaptureReturnedNormally &&
			       proof.exactItemSubtreeRestoredAndReadBack &&
			       proof.passLocalShadowStateClosed &&
			       proof.reflectedCameraOverrideClosed &&
			       proof.playerInclusionScopeClosed &&
			       proof.privatePassTLSClosed &&
			       proof.targetEndedAndUnbound &&
			       proof.activeTargetIdentityCleared &&
			       proof.mainCameraAccumulatorAndRendererRestored &&
			       proof.retainedSceneValuesReleased &&
			       proof.mipFinalizedOnlyAfterCompleteCleanup;
		}

		[[nodiscard]] bool AllLiveOwnersMatch(
			const HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity& owner)
			const noexcept
		{
			if (current_.valid &&
				current_.snapshot.publication.attempt.owner.hand != owner) {
				return false;
			}
			if (active_.valid && active_.attempt.owner.hand != owner)
				return false;
			return true;
		}

		[[nodiscard]] InvalidationStatus ForceInvalidate(
			const RetirementCause cause,
			InvalidationResult& output) noexcept
		{
			if (!ValidateInternalState())
				return QuarantineInvalidation(FaultReason::kInternalInvariant);
			const bool aborted = active_.valid;
			const bool publishing = current_.valid;
			if (!aborted && !publishing)
				return InvalidationStatus::kNoState;
			if (publishing && retired_.valid)
				return QuarantineInvalidation(FaultReason::kRetirementCapacity);

			TokenSeeds nextTokens = tokens_;
			TokenIssueReceipt retirementIssue{};
			if (publishing &&
				!Issue(nextTokens.retirement, retirementIssue)) {
				return QuarantineInvalidation(FaultReason::kTokenExhaustion,
					InvalidationStatus::kTokenExhausted);
			}

			RetirementReceipt retirement{};
			if (publishing) {
				retirement = MakeRetirement(
					current_.snapshot, {}, 0, retirementIssue.issuedToken, cause);
				if (!IsValidRetirementReceipt(retirement))
					return QuarantineInvalidation(FaultReason::kInternalInvariant);
			}

			tokens_ = nextTokens;
			if (publishing) {
				retired_ = { retirement, true };
				current_ = {};
			}
			if (aborted)
				active_ = {};
			output = { retirement, aborted };
			if (publishing && aborted)
				return InvalidationStatus::kPublicationRetiredAndAttemptAborted;
			if (publishing)
				return InvalidationStatus::kPublicationRetired;
			return InvalidationStatus::kAttemptAborted;
		}

		void Quarantine(const FaultReason reason) noexcept
		{
			physicalTargets_ = {};
			current_ = {};
			active_ = {};
			retired_ = {};
			tokens_ = {};
			sourceSequence_ = 0;
			loadGeneration_ = 0;
			lastConsumedPublicationToken_ = 0;
			fault_ = reason == FaultReason::kNone ?
				FaultReason::kInternalInvariant : reason;
			phase_ = StorePhase::kQuarantined;
		}

		[[nodiscard]] BeginStatus QuarantineBegin(
			const FaultReason reason,
			const BeginStatus status = BeginStatus::kQuarantined) noexcept
		{
			Quarantine(reason);
			if (reason == FaultReason::kTokenExhaustion)
				return BeginStatus::kTokenExhausted;
			return status;
		}

		[[nodiscard]] CompleteStatus QuarantineComplete(
			const FaultReason reason,
			const CompleteStatus status = CompleteStatus::kQuarantined) noexcept
		{
			Quarantine(reason);
			return status;
		}

		[[nodiscard]] ConsumeStatus QuarantineConsume(
			const FaultReason reason,
			const ConsumeStatus status = ConsumeStatus::kQuarantined) noexcept
		{
			Quarantine(reason);
			return status;
		}

		[[nodiscard]] InvalidationStatus QuarantineInvalidation(
			const FaultReason reason,
			const InvalidationStatus status =
				InvalidationStatus::kQuarantined) noexcept
		{
			Quarantine(reason);
			return status;
		}

		[[nodiscard]] bool IdentityStateScrubbed() const noexcept
		{
			if (phase_ != StorePhase::kQuarantined)
				return false;
			return physicalTargets_ == PhysicalTargets{} &&
			       current_.snapshot == PublicationSnapshot{} &&
			       !current_.valid &&
			       SameCaptureAttemptValue(
				       active_.attempt, CaptureAttemptIdentity{}) &&
			       SamePhaseSnapshotReservationValue(
				       active_.phaseSnapshots, PhaseSnapshotReservation{}) &&
			       active_.physicalRole == kInvalidPhysicalRole &&
			       !active_.valid &&
			       retired_.receipt == RetirementReceipt{} && !retired_.valid &&
			       tokens_ == TokenSeeds{} && sourceSequence_ == 0 &&
			       loadGeneration_ == 0 &&
			       lastConsumedPublicationToken_ == 0;
		}

		StorePhase phase_{ StorePhase::kUninitialized };
		FaultReason fault_{ FaultReason::kNone };
		PhysicalTargets physicalTargets_{};
		TokenSeeds tokens_{};
		std::uint64_t sourceSequence_{ 0 };
		std::uint64_t loadGeneration_{ 0 };
		CurrentPublication current_{};
		ActiveAttempt active_{};
		PendingRetirement retired_{};
		std::uint64_t lastConsumedPublicationToken_{ 0 };
	};
}
