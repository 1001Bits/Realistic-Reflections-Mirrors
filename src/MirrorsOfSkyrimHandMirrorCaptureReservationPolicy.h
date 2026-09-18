#pragma once

#include "HandMirrorInternalPublicationStore.h"
#include "HandMirrorFrameReceiptPolicy.h"
#include "MirrorsOfSkyrimCaptureScheduler.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace HandMirrorCaptureReservationPolicy
{
	using HandMirrorInternalPublicationStore::BeginResult;
	using HandMirrorInternalPublicationStore::PhaseSnapshotReservation;
	using HandMirrorInternalPublicationStore::PublicationCleanupProof;
	using HandMirrorInternalPublicationStore::PublicationSnapshot;
	using HandMirrorInternalPublicationStore::RetirementReceipt;
	using HandMirrorInternalPublicationStore::Store;
	using HandMirrorRuntimeBridgePolicy::BoundedCandidateCount;
	using HandMirrorRuntimeBridgePolicy::CaptureAttemptIdentity;
	using HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity;
	using HandMirrorRuntimeBridgePolicy::MovingSurfaceSample;
	using HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity;
	using HandMirrorRuntimeBridgePolicy::TokenIssuerState;
	using HandMirrorFrameReceiptPolicy::FrameReceipt;
	using MirrorsOfSkyrimCaptureScheduler::Channel;

	// The equipped-mirror adapter owns these four fail-closed internal seams.
	inline constexpr bool kCaptureReservationRuntimeWired = true;
	inline constexpr bool kNativeHandCaptureRuntimeWired = true;
	inline constexpr bool kInternalHandPublicationRuntimeWired = true;
	inline constexpr bool kFirstPersonHandDeliveryRuntimeWired = true;

	// The scheduler can reserve the mirror opportunity only once per source frame.
	// A larger queue would permit stale moving-surface work.
	inline constexpr std::size_t kPendingReservationCapacity = 1;
	inline constexpr std::size_t kActiveTicketCapacity = 1;
	static_assert(kPendingReservationCapacity == 1);
	static_assert(kActiveTicketCapacity == 1);
	static_assert(HandMirrorInternalPublicationStore::kPhysicalRoleCapacity == 2);

	enum class CoordinatorPhase : std::uint8_t
	{
		kUninitialized,
		kReady,
		kQuarantined
	};

	enum class FaultReason : std::uint8_t
	{
		kNone,
		kInvalidInitialization,
		kInvalidRequest,
		kSourceFrameReplay,
		kPendingReservationOverflow,
		kCandidateOverflow,
		kSourceFrameTokenExhaustion,
		kCallbackOrder,
		kCallbackTicketMismatch,
		kWrongCaptureTiming,
		kStoreProtocol,
		kOwnerLeaseReplay,
		kAttemptReplay,
		kTargetAlias,
		kCleanupFailure,
		kPublicationMismatch,
		kExternalNativeFault
	};

	enum class FrameDisposition : std::uint8_t
	{
		kNone,
		kReserved,
		kDarkSchedulerRejected,
		kDarkNoExactHand,
		kDarkStale,
		kDarkNoStagingTarget,
		kAttemptActive,
		kAborted,
		kPublished
	};

	enum class CaptureTiming : std::uint8_t
	{
		kUnknown,
		kFenceTail,
		kPostWorldBeforeFirstPerson,
		kFirstPersonDraw
	};

	struct SourceFrameLedgerEntry
	{
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t sourceFrameToken{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		FrameDisposition disposition{ FrameDisposition::kNone };

		constexpr bool operator==(const SourceFrameLedgerEntry&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsValidLedgerEntry(
		const SourceFrameLedgerEntry& entry) noexcept
	{
		return entry.sourceSequence != 0 && entry.sourceFrameToken != 0 &&
		       entry.mainViewFrame != 0 &&
		       entry.disposition != FrameDisposition::kNone;
	}

	struct FenceReservationRequest
	{
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		Channel schedulerChannel{ Channel::kNone };
		bool schedulerBudgetReserved{ false };
		BoundedCandidateCount exactHandCandidates{};
		MirrorOwnerIdentity owner{};
	};

	struct FenceReservation
	{
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t sourceFrameToken{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		Channel schedulerChannel{ Channel::kNone };
		MirrorOwnerIdentity owner{};
		bool valid{ false };

		constexpr bool operator==(const FenceReservation&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsExactHandOwner(
		const MirrorOwnerIdentity& owner) noexcept
	{
		using HandMirrorContentRuntimePolicy::Perspective;
		using HandMirrorRuntimeBridgePolicy::IsValidOwner;
		using HandMirrorRuntimeBridgePolicy::OwnerKind;
		return IsValidOwner(owner) && owner.kind == OwnerKind::kHand &&
		       (owner.hand.perspective == Perspective::kFirstPerson ||
		        owner.hand.perspective == Perspective::kVRFirstPerson);
	}

	[[nodiscard]] constexpr bool IsValidFenceReservation(
		const FenceReservation& reservation) noexcept
	{
		return reservation.valid && reservation.sourceSequence != 0 &&
		       reservation.sourceFrameToken != 0 &&
		       reservation.mainViewFrame != 0 &&
		       reservation.schedulerChannel == Channel::kMirror &&
		       IsExactHandOwner(reservation.owner);
	}

	[[nodiscard]] constexpr bool SameFenceReservation(
		const FenceReservation& left,
		const FenceReservation& right) noexcept
	{
		return IsValidFenceReservation(left) &&
		       IsValidFenceReservation(right) && left == right;
	}

	enum class ReserveStatus : std::uint8_t
	{
		kReserved,
		kDarkSchedulerRejected,
		kDarkNoExactHand,
		kUnavailable,
		kQuarantined,
		kInvalidRequest,
		kSourceFrameReplay,
		kPendingReservationOverflow,
		kCandidateOverflow,
		kTokenExhausted
	};

	struct PostWorldRequest
	{
		FenceReservation reservation{};
		FrameReceipt frameReceipt{};
		MovingSurfaceSample firstPersonSurface{};
		PrivateTargetIdentity mainViewTarget{};
		CaptureTiming timing{ CaptureTiming::kUnknown };
		bool nativeWorldReturnedNormally{ false };
		bool schedulerReservationStillExclusive{ false };
	};

	struct CaptureTicket
	{
		FenceReservation reservation{};
		FrameReceipt frameReceipt{};
		CaptureAttemptIdentity attempt{};
		PhaseSnapshotReservation phaseSnapshots{};
		PrivateTargetIdentity mainViewTarget{};
		RetirementReceipt retiredPriorPublication{};
		bool valid{ false };

		[[nodiscard]] constexpr bool operator==(
			const CaptureTicket& other) const noexcept
		{
			return reservation == other.reservation &&
			       frameReceipt == other.frameReceipt &&
			       HandMirrorInternalPublicationStore::SameCaptureAttemptValue(
				       attempt, other.attempt) &&
			       HandMirrorInternalPublicationStore::
				       SamePhaseSnapshotReservationValue(
					       phaseSnapshots, other.phaseSnapshots) &&
			       mainViewTarget == other.mainViewTarget &&
			       retiredPriorPublication == other.retiredPriorPublication &&
			       valid == other.valid;
		}
	};

	[[nodiscard]] constexpr bool IsValidCaptureTicket(
		const CaptureTicket& ticket) noexcept
	{
		using HandMirrorRuntimeBridgePolicy::ArePrivateTargetsPhysicallyDisjoint;
		using HandMirrorRuntimeBridgePolicy::IsValidHandCaptureAttempt;
		using HandMirrorRuntimeBridgePolicy::SameOwner;
		if (!ticket.valid || !IsValidFenceReservation(ticket.reservation) ||
			!HandMirrorFrameReceiptPolicy::IsValidFrameReceipt(
				ticket.frameReceipt) ||
			ticket.frameReceipt.sourceSequence !=
				ticket.reservation.sourceSequence ||
			ticket.frameReceipt.mainViewFrame !=
				ticket.reservation.mainViewFrame ||
			!IsValidHandCaptureAttempt(ticket.attempt) ||
			!HandMirrorInternalPublicationStore::
				PhaseSnapshotReservationMatchesSurface(
					ticket.phaseSnapshots, ticket.attempt.surface) ||
			!SameOwner(ticket.reservation.owner, ticket.attempt.owner) ||
			ticket.attempt.surface.sourceSequence !=
				ticket.reservation.sourceSequence ||
			ticket.attempt.surface.pose.mainViewFrame !=
				ticket.reservation.mainViewFrame ||
			!ArePrivateTargetsPhysicallyDisjoint(
				ticket.attempt.privateTarget, ticket.mainViewTarget)) {
			return false;
		}
		if (ticket.retiredPriorPublication == RetirementReceipt{})
			return true;
		return HandMirrorInternalPublicationStore::IsValidRetirementReceipt(
				ticket.retiredPriorPublication) &&
		       ticket.retiredPriorPublication.successorOwner ==
			       ticket.attempt.owner &&
		       ticket.retiredPriorPublication.successorOwnerLeaseToken ==
			       ticket.attempt.ownerLeaseSequence &&
		       ArePrivateTargetsPhysicallyDisjoint(
			       ticket.retiredPriorPublication.retired.publication.attempt
				       .privateTarget,
			       ticket.attempt.privateTarget) &&
		       ArePrivateTargetsPhysicallyDisjoint(
			       ticket.retiredPriorPublication.retired.publication.attempt
				       .privateTarget,
			       ticket.mainViewTarget);
	}

	[[nodiscard]] constexpr bool SameCaptureTicket(
		const CaptureTicket& left,
		const CaptureTicket& right) noexcept
	{
		return IsValidCaptureTicket(left) && IsValidCaptureTicket(right) &&
		       SameFenceReservation(left.reservation, right.reservation) &&
		       left.frameReceipt == right.frameReceipt &&
		       HandMirrorRuntimeBridgePolicy::SameCaptureAttempt(
			       left.attempt, right.attempt) &&
		       HandMirrorInternalPublicationStore::
			       SamePhaseSnapshotReservationValue(
				       left.phaseSnapshots, right.phaseSnapshots) &&
		       left.mainViewTarget == right.mainViewTarget &&
		       left.retiredPriorPublication == right.retiredPriorPublication;
	}

	enum class PostWorldStatus : std::uint8_t
	{
		kCaptureTicketReady,
		kDarkStale,
		kDarkSchedulerReservationLost,
		kDarkNoStagingTarget,
		kUnavailable,
		kQuarantined,
		kProtocolFault,
		kTokenReplay,
		kTargetAlias,
		kStoreFault
	};

	enum class AbortStatus : std::uint8_t
	{
		kAbortedDark,
		kUnavailable,
		kQuarantined,
		kProtocolFault,
		kStoreFault
	};

	enum class CancelReservationStatus : std::uint8_t
	{
		kCancelledDark,
		kUnavailable,
		kQuarantined,
		kProtocolFault
	};

	struct CompletionContext
	{
		FrameReceipt currentFrameReceipt{};
		PrivateTargetIdentity currentMainViewTarget{};
		CaptureTiming timing{ CaptureTiming::kUnknown };
		bool exactMirrorRenderAttemptConsumed{ false };
		bool finalFirstPersonDeliveryPending{ false };
	};

	enum class CompleteStatus : std::uint8_t
	{
		kPublished,
		kDarkStale,
		kUnavailable,
		kQuarantined,
		kProtocolFault,
		kTargetAlias,
		kCleanupRejected,
		kPublicationRejected,
		kStoreFault
	};

	[[nodiscard]] constexpr bool HasRequiredPrivateCleanup(
		const CaptureTicket& ticket,
		const PublicationCleanupProof& proof) noexcept
	{
		using HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample;
		using HandMirrorRuntimeBridgePolicy::SameCaptureAttempt;
		return IsValidCaptureTicket(ticket) &&
		       SameCaptureAttempt(ticket.attempt, proof.attempt) &&
		       IsValidMovingSurfaceSample(proof.postReturnSurface) &&
		       proof.postReturnSnapshot ==
			       ticket.phaseSnapshots.postCaptureReturn &&
		       proof.unboundTarget == ticket.attempt.privateTarget &&
		       proof.privateCaptureReturnedNormally &&
		       proof.exactItemSubtreeRestoredAndReadBack &&
		       proof.passLocalShadowStateClosed &&
		       proof.reflectedCameraOverrideClosed &&
		       proof.playerInclusionScopeClosed && proof.privatePassTLSClosed &&
		       proof.targetEndedAndUnbound &&
		       proof.activeTargetIdentityCleared &&
		       proof.mainCameraAccumulatorAndRendererRestored &&
		       proof.retainedSceneValuesReleased;
	}

	[[nodiscard]] constexpr bool PublicationMatchesTicket(
		const CaptureTicket& ticket,
		const PublicationSnapshot& publication) noexcept
	{
		using HandMirrorInternalPublicationStore::IsExactInternalHandPublication;
		using HandMirrorRuntimeBridgePolicy::SameCaptureAttempt;
		return IsValidCaptureTicket(ticket) &&
		       IsExactInternalHandPublication(publication) &&
		       SameCaptureAttempt(
			       ticket.attempt, publication.publication.attempt) &&
		       HandMirrorInternalPublicationStore::
			       SamePhaseSnapshotReservationValue(
				       ticket.phaseSnapshots, publication.phaseSnapshots) &&
		       publication.publication.attempt.surface.sourceSequence ==
			       ticket.reservation.sourceSequence;
	}

	struct AuditSnapshot
	{
		CoordinatorPhase phase{ CoordinatorPhase::kUninitialized };
		FaultReason fault{ FaultReason::kNone };
		std::size_t pendingReservationCount{ 0 };
		std::size_t activeTicketCount{ 0 };
		std::uint64_t lastSourceSequence{ 0 };
		std::uint64_t lastOwnerLeaseToken{ 0 };
		std::uint64_t lastAttemptToken{ 0 };
		SourceFrameLedgerEntry frameLedger{};
		bool identityStateScrubbed{ false };
	};

	/**
	 * Render-thread-owned, fixed-capacity hand reservation coordinator.  It
	 * spends only an already-selected kMirror scheduler opportunity.  The
	 * first-person moving surface is sampled after native RenderWorld returns;
	 * publication can be committed only through the frozen internal store after
	 * its complete cleanup proof succeeds in the identical source/main frame.
	 */
	class Coordinator
	{
	public:
		[[nodiscard]] bool Initialize(
			const TokenIssuerState sourceFrameTokens = {}) noexcept
		{
			if (phase_ != CoordinatorPhase::kUninitialized ||
				!HandMirrorRuntimeBridgePolicy::IsValidTokenIssuerState(
					sourceFrameTokens)) {
				return false;
			}
			sourceFrameTokens_ = sourceFrameTokens;
			phase_ = CoordinatorPhase::kReady;
			return true;
		}

		[[nodiscard]] ReserveStatus ReserveAtFence(
			Store& store,
			const FenceReservationRequest& request,
			FenceReservation& output) noexcept
		{
			output = {};
			if (phase_ == CoordinatorPhase::kQuarantined)
				return ReserveStatus::kQuarantined;
			if (phase_ != CoordinatorPhase::kReady)
				return ReserveStatus::kUnavailable;
			if (request.sourceSequence == 0 || request.mainViewFrame == 0 ||
				!IsKnownChannel(request.schedulerChannel)) {
				FailStop(store, FaultReason::kInvalidRequest);
				return ReserveStatus::kInvalidRequest;
			}
			if (request.sourceSequence <= lastSourceSequence_) {
				FailStop(store, FaultReason::kSourceFrameReplay);
				return ReserveStatus::kSourceFrameReplay;
			}
			if (PendingCount() != 0 || ActiveCount() != 0) {
				FailStop(store, FaultReason::kPendingReservationOverflow);
				return ReserveStatus::kPendingReservationOverflow;
			}

			const auto issue = HandMirrorRuntimeBridgePolicy::IssueNextToken(
				sourceFrameTokens_);
			if (issue.status !=
				HandMirrorRuntimeBridgePolicy::TokenIssueStatus::kIssued) {
				FailStop(store, FaultReason::kSourceFrameTokenExhaustion);
				return ReserveStatus::kTokenExhausted;
			}
			sourceFrameTokens_ = issue.nextState;
			lastSourceSequence_ = request.sourceSequence;
			frameLedger_ = {
				request.sourceSequence,
				issue.receipt.issuedToken,
				request.mainViewFrame,
				FrameDisposition::kDarkSchedulerRejected
			};

			if (request.schedulerChannel != Channel::kMirror ||
				!request.schedulerBudgetReserved) {
				return ReserveStatus::kDarkSchedulerRejected;
			}
			if (!HandMirrorRuntimeBridgePolicy::IsValidCandidateCount(
					request.exactHandCandidates) ||
				request.exactHandCandidates.overflowed) {
				FailStop(store, FaultReason::kCandidateOverflow);
				return ReserveStatus::kCandidateOverflow;
			}
			if (request.exactHandCandidates.exactCount != 1) {
				frameLedger_.disposition = FrameDisposition::kDarkNoExactHand;
				return ReserveStatus::kDarkNoExactHand;
			}
			if (!IsExactHandOwner(request.owner)) {
				FailStop(store, FaultReason::kInvalidRequest);
				return ReserveStatus::kInvalidRequest;
			}

			FenceReservation reservation{
				.sourceSequence = request.sourceSequence,
				.sourceFrameToken = issue.receipt.issuedToken,
				.mainViewFrame = request.mainViewFrame,
				.schedulerChannel = Channel::kMirror,
				.owner = request.owner,
				.valid = true
			};
			if (!IsValidFenceReservation(reservation)) {
				FailStop(store, FaultReason::kInvalidRequest);
				return ReserveStatus::kInvalidRequest;
			}
			pending_[0] = reservation;
			frameLedger_.disposition = FrameDisposition::kReserved;
			output = reservation;
			return ReserveStatus::kReserved;
		}

		[[nodiscard]] PostWorldStatus BeginPostWorldCapture(
			Store& store,
			const PostWorldRequest& request,
			CaptureTicket& output) noexcept
		{
			output = {};
			if (phase_ == CoordinatorPhase::kQuarantined)
				return PostWorldStatus::kQuarantined;
			if (phase_ != CoordinatorPhase::kReady)
				return PostWorldStatus::kUnavailable;
			if (ActiveCount() != 0 || PendingCount() != 1) {
				FailStop(store, FaultReason::kCallbackOrder);
				return PostWorldStatus::kProtocolFault;
			}
			if (!SameFenceReservation(request.reservation, pending_[0])) {
				FailStop(store, FaultReason::kCallbackTicketMismatch);
				return PostWorldStatus::kProtocolFault;
			}
			if (request.timing != CaptureTiming::kPostWorldBeforeFirstPerson ||
				!request.nativeWorldReturnedNormally) {
				FailStop(store, FaultReason::kWrongCaptureTiming);
				return PostWorldStatus::kProtocolFault;
			}

			// Resolution is one-shot.  Every ordinary rejection below burns this
			// source-frame reservation, so no abort/backpressure retry can reacquire
			// the mirror budget in the same frame.
			pending_[0] = {};
			if (!request.schedulerReservationStillExclusive) {
				frameLedger_.disposition =
					FrameDisposition::kDarkSchedulerRejected;
				return PostWorldStatus::kDarkSchedulerReservationLost;
			}
			if (!HandMirrorFrameReceiptPolicy::IsValidFrameReceipt(
					request.frameReceipt) ||
				request.frameReceipt.sourceSequence !=
					request.reservation.sourceSequence ||
				request.frameReceipt.mainViewFrame !=
					request.reservation.mainViewFrame) {
				frameLedger_.disposition = FrameDisposition::kDarkStale;
				return PostWorldStatus::kDarkStale;
			}
			if (!HandMirrorRuntimeBridgePolicy::IsValidPrivateTarget(
					request.mainViewTarget)) {
				FailStop(store, FaultReason::kTargetAlias);
				return PostWorldStatus::kTargetAlias;
			}
			if (!IsCurrentPostWorldSurface(request)) {
				frameLedger_.disposition = FrameDisposition::kDarkStale;
				return PostWorldStatus::kDarkStale;
			}
			const auto storeAudit = store.Inspect();
			if (storeAudit.phase !=
					HandMirrorInternalPublicationStore::StorePhase::kReady) {
				FailStop(store, FaultReason::kStoreProtocol);
				return PostWorldStatus::kStoreFault;
			}
			if (storeAudit.sourceSequence != request.reservation.sourceSequence) {
				frameLedger_.disposition = FrameDisposition::kDarkStale;
				return PostWorldStatus::kDarkStale;
			}

			BeginResult begin{};
			const auto beginStatus = store.Begin(request.firstPersonSurface, begin);
			if (beginStatus ==
				HandMirrorInternalPublicationStore::BeginStatus::kNoStagingRole) {
				frameLedger_.disposition =
					FrameDisposition::kDarkNoStagingTarget;
				return PostWorldStatus::kDarkNoStagingTarget;
			}
			if (beginStatus ==
				HandMirrorInternalPublicationStore::BeginStatus::kWrongSource) {
				frameLedger_.disposition = FrameDisposition::kDarkStale;
				return PostWorldStatus::kDarkStale;
			}
			if (beginStatus !=
				HandMirrorInternalPublicationStore::BeginStatus::kReady) {
				FailStop(store, FaultReason::kStoreProtocol);
				return PostWorldStatus::kStoreFault;
			}

			CaptureTicket ticket{
				.reservation = request.reservation,
				.frameReceipt = request.frameReceipt,
				.attempt = begin.attempt,
				.phaseSnapshots = begin.phaseSnapshots,
				.mainViewTarget = request.mainViewTarget,
				.retiredPriorPublication = begin.retiredPriorPublication,
				.valid = true
			};
			if (!IsValidCaptureTicket(ticket)) {
				AbortExactStoreAttempt(store, begin.attempt);
				FailStop(store, FaultReason::kTargetAlias);
				return PostWorldStatus::kTargetAlias;
			}
			if (begin.attempt.ownerLeaseSequence <= lastOwnerLeaseToken_) {
				AbortExactStoreAttempt(store, begin.attempt);
				FailStop(store, FaultReason::kOwnerLeaseReplay);
				return PostWorldStatus::kTokenReplay;
			}
			if (begin.attempt.attemptSequence <= lastAttemptToken_) {
				AbortExactStoreAttempt(store, begin.attempt);
				FailStop(store, FaultReason::kAttemptReplay);
				return PostWorldStatus::kTokenReplay;
			}

			lastOwnerLeaseToken_ = begin.attempt.ownerLeaseSequence;
			lastAttemptToken_ = begin.attempt.attemptSequence;
			active_[0] = ticket;
			frameLedger_.disposition = FrameDisposition::kAttemptActive;
			output = ticket;
			return PostWorldStatus::kCaptureTicketReady;
		}

		/**
		 * Consume an exact fence reservation when the chained main-world call or
		 * retained-list callback cannot reach BeginPostWorldCapture.  No Store
		 * attempt exists in this phase, so cancellation is a dark terminal
		 * disposition for this source rather than a Store abort.
		 */
		[[nodiscard]] CancelReservationStatus CancelFenceReservation(
			Store& store,
			const FenceReservation& reservation) noexcept
		{
			if (phase_ == CoordinatorPhase::kQuarantined)
				return CancelReservationStatus::kQuarantined;
			if (phase_ != CoordinatorPhase::kReady)
				return CancelReservationStatus::kUnavailable;
			if (ActiveCount() != 0 || PendingCount() != 1 ||
				!SameFenceReservation(reservation, pending_[0])) {
				FailStop(store, FaultReason::kCallbackTicketMismatch);
				return CancelReservationStatus::kProtocolFault;
			}
			pending_[0] = {};
			frameLedger_.disposition = FrameDisposition::kAborted;
			return CancelReservationStatus::kCancelledDark;
		}

		[[nodiscard]] AbortStatus AbortCapture(
			Store& store,
			const CaptureTicket& ticket) noexcept
		{
			if (phase_ == CoordinatorPhase::kQuarantined)
				return AbortStatus::kQuarantined;
			if (phase_ != CoordinatorPhase::kReady)
				return AbortStatus::kUnavailable;
			if (PendingCount() != 0 || ActiveCount() != 1 ||
				!SameCaptureTicket(ticket, active_[0])) {
				FailStop(store, FaultReason::kCallbackTicketMismatch);
				return AbortStatus::kProtocolFault;
			}
			const auto status = store.Abort(active_[0].attempt);
			if (status !=
				HandMirrorInternalPublicationStore::AbortStatus::kAborted) {
				FailStop(store, FaultReason::kStoreProtocol);
				return AbortStatus::kStoreFault;
			}
			active_[0] = {};
			frameLedger_.disposition = FrameDisposition::kAborted;
			return AbortStatus::kAbortedDark;
		}

		[[nodiscard]] CompleteStatus CompleteCapture(
			Store& store,
			const CaptureTicket& ticket,
			const CompletionContext& context,
			const PublicationCleanupProof& cleanup,
			PublicationSnapshot& output) noexcept
		{
			output = {};
			if (phase_ == CoordinatorPhase::kQuarantined)
				return CompleteStatus::kQuarantined;
			if (phase_ != CoordinatorPhase::kReady)
				return CompleteStatus::kUnavailable;
			if (PendingCount() != 0 || ActiveCount() != 1 ||
				!SameCaptureTicket(ticket, active_[0])) {
				FailStop(store, FaultReason::kCallbackTicketMismatch);
				return CompleteStatus::kProtocolFault;
			}
			if (context.timing != CaptureTiming::kPostWorldBeforeFirstPerson ||
				!context.exactMirrorRenderAttemptConsumed ||
				!context.finalFirstPersonDeliveryPending) {
				FailStop(store, FaultReason::kCallbackOrder);
				return CompleteStatus::kProtocolFault;
			}
			if (!HandMirrorRuntimeBridgePolicy::IsValidPrivateTarget(
					context.currentMainViewTarget) ||
				context.currentMainViewTarget != ticket.mainViewTarget) {
				FailStop(store, FaultReason::kTargetAlias);
				return CompleteStatus::kTargetAlias;
			}
			if (!HasRequiredPrivateCleanup(ticket, cleanup)) {
				PublicationSnapshot ignored{};
				(void)store.Complete(cleanup, ignored);
				FailStop(store, FaultReason::kCleanupFailure);
				return CompleteStatus::kCleanupRejected;
			}

			const bool stale =
				!HandMirrorFrameReceiptPolicy::IsValidFrameReceipt(
					context.currentFrameReceipt) ||
				context.currentFrameReceipt != ticket.frameReceipt ||
				!HandMirrorRuntimeBridgePolicy::SameMovingSurface(
					ticket.attempt.surface, cleanup.postReturnSurface);
			if (stale) {
				const auto abortStatus = store.Abort(ticket.attempt);
				if (abortStatus !=
					HandMirrorInternalPublicationStore::AbortStatus::kAborted) {
					FailStop(store, FaultReason::kStoreProtocol);
					return CompleteStatus::kStoreFault;
				}
				active_[0] = {};
				frameLedger_.disposition = FrameDisposition::kDarkStale;
				return CompleteStatus::kDarkStale;
			}
			if (!cleanup.mipFinalizedOnlyAfterCompleteCleanup) {
				PublicationSnapshot ignored{};
				(void)store.Complete(cleanup, ignored);
				FailStop(store, FaultReason::kCleanupFailure);
				return CompleteStatus::kCleanupRejected;
			}
			PublicationSnapshot publication{};
			const auto completeStatus = store.Complete(cleanup, publication);
			if (completeStatus !=
				HandMirrorInternalPublicationStore::CompleteStatus::kPublished) {
				FailStop(store, FaultReason::kStoreProtocol);
				return CompleteStatus::kStoreFault;
			}
			if (!PublicationMatchesTicket(ticket, publication)) {
				// Complete has already made the mismatched publication current.
				// FailStop must poison the Store before returning so that value can
				// never remain internally or publicly acquirable.
				FailStop(store, FaultReason::kPublicationMismatch);
				return CompleteStatus::kPublicationRejected;
			}

			active_[0] = {};
			frameLedger_.disposition = FrameDisposition::kPublished;
			output = publication;
			return CompleteStatus::kPublished;
		}

		[[nodiscard]] AuditSnapshot Inspect() const noexcept
		{
			return {
				.phase = phase_,
				.fault = fault_,
				.pendingReservationCount = PendingCount(),
				.activeTicketCount = ActiveCount(),
				.lastSourceSequence = lastSourceSequence_,
				.lastOwnerLeaseToken = lastOwnerLeaseToken_,
				.lastAttemptToken = lastAttemptToken_,
				.frameLedger = frameLedger_,
				.identityStateScrubbed = IdentityStateScrubbed()
			};
		}

		/** Terminal external fault entry; no fabricated Abort token is presented. */
		void QuarantineExternalNativeFault(Store& store) noexcept
		{
			FailStop(store, FaultReason::kExternalNativeFault);
		}

	private:
		[[nodiscard]] static constexpr bool IsKnownChannel(
			const Channel channel) noexcept
		{
			switch (channel) {
			case Channel::kNone:
			case Channel::kMirror:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] static constexpr bool IsCurrentPostWorldSurface(
			const PostWorldRequest& request) noexcept
		{
			using HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample;
			using HandMirrorRuntimeBridgePolicy::SameOwner;
			return IsValidMovingSurfaceSample(request.firstPersonSurface) &&
			       IsValidFenceReservation(request.reservation) &&
			       SameOwner(
				       request.reservation.owner,
				       HandMirrorRuntimeBridgePolicy::MakeHandOwner(
					       request.firstPersonSurface.owner)) &&
			       request.firstPersonSurface.sourceSequence ==
				       request.reservation.sourceSequence &&
			       request.firstPersonSurface.pose.mainViewFrame ==
				       request.reservation.mainViewFrame &&
			       HandMirrorRuntimeBridgePolicy::IsValidPrivateTarget(
				       request.mainViewTarget);
		}

		[[nodiscard]] std::size_t PendingCount() const noexcept
		{
			return IsValidFenceReservation(pending_[0]) ? 1U : 0U;
		}

		[[nodiscard]] std::size_t ActiveCount() const noexcept
		{
			return IsValidCaptureTicket(active_[0]) ? 1U : 0U;
		}

		static void AbortExactStoreAttempt(
			Store& store,
			const CaptureAttemptIdentity& attempt) noexcept
		{
			if (HandMirrorRuntimeBridgePolicy::IsValidHandCaptureAttempt(attempt) &&
				store.Inspect().phase ==
					HandMirrorInternalPublicationStore::StorePhase::kReady) {
				(void)store.Abort(attempt);
			}
		}

		static void PoisonStore(Store& store) noexcept
		{
			// Store policy may already have classified and scrubbed the exact
			// protocol/cleanup fault that caused this coordinator fail-stop. Preserve
			// that first terminal reason; use the external-native classification only
			// when the Store was still otherwise Ready.
			if (store.Inspect().phase !=
				HandMirrorInternalPublicationStore::StorePhase::kQuarantined) {
				store.QuarantineExternalNativeFault();
			}
		}

		void FailStop(Store& store, const FaultReason reason) noexcept
		{
			PoisonStore(store);
			pending_ = {};
			active_ = {};
			sourceFrameTokens_ = {};
			lastSourceSequence_ = 0;
			lastOwnerLeaseToken_ = 0;
			lastAttemptToken_ = 0;
			frameLedger_ = {};
			fault_ = reason == FaultReason::kNone ?
				FaultReason::kInvalidRequest : reason;
			phase_ = CoordinatorPhase::kQuarantined;
		}

		[[nodiscard]] bool IdentityStateScrubbed() const noexcept
		{
			return phase_ == CoordinatorPhase::kQuarantined &&
			       pending_ ==
				       std::array<FenceReservation,
					       kPendingReservationCapacity>{} &&
			       active_ ==
				       std::array<CaptureTicket, kActiveTicketCapacity>{} &&
			       sourceFrameTokens_ == TokenIssuerState{} &&
			       lastSourceSequence_ == 0 && lastOwnerLeaseToken_ == 0 &&
			       lastAttemptToken_ == 0 &&
			       frameLedger_ == SourceFrameLedgerEntry{};
		}

		CoordinatorPhase phase_{ CoordinatorPhase::kUninitialized };
		FaultReason fault_{ FaultReason::kNone };
		std::array<FenceReservation, kPendingReservationCapacity> pending_{};
		std::array<CaptureTicket, kActiveTicketCapacity> active_{};
		TokenIssuerState sourceFrameTokens_{};
		std::uint64_t lastSourceSequence_{ 0 };
		std::uint64_t lastOwnerLeaseToken_{ 0 };
		std::uint64_t lastAttemptToken_{ 0 };
		SourceFrameLedgerEntry frameLedger_{};
	};

	[[nodiscard]] constexpr bool CanEnableHandCaptureRuntime() noexcept
	{
		return kCaptureReservationRuntimeWired &&
		       kNativeHandCaptureRuntimeWired &&
		       kInternalHandPublicationRuntimeWired &&
		       kFirstPersonHandDeliveryRuntimeWired &&
		       HandMirrorRuntimeBridgePolicy::kSingletonOwnerGrantWired &&
		       HandMirrorRuntimeBridgePolicy::kCapturePublicationIdentityWired &&
		       HandMirrorRuntimeBridgePolicy::kSameFrameMovingPlaneWired &&
		       HandMirrorRuntimeBridgePolicy::kExactItemSubtreeSuppressionWired &&
		       HandMirrorRuntimeBridgePolicy::kFirstPersonDeliveryRouteAccepted &&
		       HandMirrorRuntimeBridgePolicy::kRuntimeBridgeWired;
	}
}
