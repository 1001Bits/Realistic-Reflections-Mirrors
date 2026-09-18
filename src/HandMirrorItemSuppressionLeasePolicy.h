#pragma once

#include "HandMirrorRuntimeBridgePolicy.h"

#include <cstdint>
#include <type_traits>

namespace HandMirrorItemSuppressionLeasePolicy
{
	namespace Bridge = HandMirrorRuntimeBridgePolicy;

	/**
	 * Offline, value-only contract for a future native/SEH adapter.  This policy
	 * retains opaque integer receipts only.  It performs no engine read or write,
	 * retains no scene pointer, and does not authorize any runtime bridge.
	 */
	static_assert(Bridge::kExactItemSubtreeSuppressionWired);

	enum class Phase : std::uint8_t
	{
		kIdle,
		kAwaitingSuppressionReadback,
		kSuppressedWithoutWrite,
		kSuppressedWithOwnedWrite,
		kAwaitingRestoreReadback,
		kFailStopRestorePending,
		kClosed,
		kFaulted
	};

	/** Status reported by a future guarded native adapter. */
	enum class AdapterCallStatus : std::uint8_t
	{
		kNotAttempted,
		kReturnedNormallyWithReadback,
		kReturnedFailure,
		kExceptionCaught,
		kReadbackUnavailable
	};

	enum class PrivateCallOutcome : std::uint8_t
	{
		kNotObserved,
		kReturnedNormally,
		kReturnedAbnormally,
		kExceptionCaught
	};

	enum class AdapterActionKind : std::uint8_t
	{
		kNone,
		kSetAppCulled,
		kRestorePriorAppCull
	};

	enum class LeaseClaim : std::uint8_t
	{
		kNone,
		kObservedAlreadyCulledNoWrite,
		kMutationReadbackPending,
		kExclusiveMutationOwned
	};

	enum class Failure : std::uint8_t
	{
		kNone,
		kRepeatedBegin,
		kRepeatedClose,
		kCloseWithoutBegin,
		kUnexpectedOrdering,
		kInvalidAttemptIdentity,
		kInvalidSubtreeIdentity,
		kInvalidCurrentAncestry,
		kAttemptNotReserved,
		kPoseSampleMissing,
		kPreexistingConflict,
		kInvalidTokenIssuerState,
		kMutationTokenExhausted,
		kAdapterNotInvoked,
		kAbnormalNativeReturn,
		kNativeException,
		kReadbackMissing,
		kStaleOrABALease,
		kIdentityDrift,
		kWriteNotAttempted,
		kUnexpectedAdapterRequest,
		kConcurrentOrUnexpectedState,
		kSuppressionReadbackMismatch,
		kPrivateReturnMissing,
		kRestoreReadbackMismatch
	};

	enum class TransitionStatus : std::uint8_t
	{
		kRejected,
		kSuppressionWriteRequested,
		kLeaseActiveWithoutWrite,
		kLeaseActiveWithOwnedWrite,
		kRestoreWriteRequested,
		kFailStopRestoreRequested,
		kFailStopAwaitingRestoreReadback,
		kClosedClean,
		kFailStopped
	};

	/**
	 * Scalar receipts copied from one already-reserved bridge attempt.  The
	 * native adapter is responsible for deriving ownerIdentityToken from the
	 * exact tagged hand owner.  The policy never stores that owner's pointers.
	 */
	struct AttemptIdentity
	{
		std::uint64_t ownerIdentityToken{ 0 };
		std::uint64_t ownerLeaseSequence{ 0 };
		std::uint64_t attemptSequence{ 0 };
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t equipGenerationToken{ 0 };
		std::uint64_t rootGenerationToken{ 0 };
		std::uint64_t partCloneGenerationToken{ 0 };

		constexpr bool operator==(const AttemptIdentity&) const noexcept = default;
	};

	struct ExactSubtreeIdentity
	{
		std::uint64_t capturePartCloneToken{ 0 };
		std::uint64_t mirrorItemSubtreeToken{ 0 };
		std::uint64_t paneSubtreeToken{ 0 };

		constexpr bool operator==(
			const ExactSubtreeIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidAttemptIdentity(
		const AttemptIdentity& identity) noexcept
	{
		return identity.ownerIdentityToken != 0 &&
		       identity.ownerLeaseSequence != 0 &&
		       identity.attemptSequence != 0 && identity.sourceSequence != 0 &&
		       identity.equipGenerationToken != 0 &&
		       identity.rootGenerationToken != 0 &&
		       identity.partCloneGenerationToken != 0;
	}

	[[nodiscard]] constexpr bool IsValidSubtreeIdentity(
		const ExactSubtreeIdentity& identity) noexcept
	{
		return identity.capturePartCloneToken != 0 &&
		       identity.mirrorItemSubtreeToken != 0 &&
		       identity.paneSubtreeToken != 0 &&
		       identity.capturePartCloneToken !=
			       identity.mirrorItemSubtreeToken &&
		       identity.capturePartCloneToken != identity.paneSubtreeToken &&
		       identity.mirrorItemSubtreeToken != identity.paneSubtreeToken;
	}

	/**
	 * A fresh guarded observation, never a retained native object.  The exact
	 * RRMirrorItem contract is deliberately explicit so a caller cannot replace
	 * it with the player, either root, the biped root, or the whole partClone.
	 */
	struct CurrentSubtreeObservation
	{
		AttemptIdentity attempt{};
		ExactSubtreeIdentity subtree{};
		AdapterCallStatus adapterStatus{ AdapterCallStatus::kNotAttempted };
		bool appCulled{ false };
		bool attemptOwnerSourceAndGenerationsAreCurrent{ false };
		bool capturePartCloneIsCurrentThirdPerson{ false };
		bool mirrorItemIsExactApprovedRRMirrorItem{ false };
		bool mirrorItemDirectlyDescendsCapturePartClone{ false };
		bool paneDirectlyDescendsMirrorItem{ false };
		bool mirrorItemContainsExactApprovedFrameAndPane{ false };
		bool targetContainsPlayer{ false };
		bool targetContainsFirstPersonRoot{ false };
		bool targetContainsThirdPersonRoot{ false };
		bool targetContainsBipedRoot{ false };
		bool targetIsWholePartClone{ false };
		bool foreignMutationOrLeaseObserved{ false };
	};

	[[nodiscard]] constexpr bool HasExactCurrentAncestry(
		const CurrentSubtreeObservation& observation) noexcept
	{
		return IsValidAttemptIdentity(observation.attempt) &&
		       IsValidSubtreeIdentity(observation.subtree) &&
		       observation.attemptOwnerSourceAndGenerationsAreCurrent &&
		       observation.capturePartCloneIsCurrentThirdPerson &&
		       observation.mirrorItemIsExactApprovedRRMirrorItem &&
		       observation.mirrorItemDirectlyDescendsCapturePartClone &&
		       observation.paneDirectlyDescendsMirrorItem &&
		       observation.mirrorItemContainsExactApprovedFrameAndPane &&
		       !observation.targetContainsPlayer &&
		       !observation.targetContainsFirstPersonRoot &&
		       !observation.targetContainsThirdPersonRoot &&
		       !observation.targetContainsBipedRoot &&
		       !observation.targetIsWholePartClone;
	}

	struct LeaseIdentity
	{
		AttemptIdentity attempt{};
		ExactSubtreeIdentity subtree{};
		std::uint64_t nativeMutationToken{ 0 };

		constexpr bool operator==(const LeaseIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidMutationLease(
		const LeaseIdentity& lease) noexcept
	{
		return IsValidAttemptIdentity(lease.attempt) &&
		       IsValidSubtreeIdentity(lease.subtree) &&
		       lease.nativeMutationToken != 0;
	}

	[[nodiscard]] constexpr bool IsValidObservedNoWriteLease(
		const LeaseIdentity& lease) noexcept
	{
		return IsValidAttemptIdentity(lease.attempt) &&
		       IsValidSubtreeIdentity(lease.subtree) &&
		       lease.nativeMutationToken == 0;
	}

	struct BeginInput
	{
		CurrentSubtreeObservation preflight{};
		Bridge::TokenIssuerState mutationTokenState{};
		bool attemptReservedBeforeSuppression{ false };
		bool currentPoseSampleCompletedBeforeSuppression{ false };
	};

	struct AdapterEvidence
	{
		LeaseIdentity presentedLease{};
		CurrentSubtreeObservation current{};
		bool writeAttempted{ false };
		bool requestedAppCulled{ false };
	};

	struct CloseInput
	{
		LeaseIdentity presentedLease{};
		CurrentSubtreeObservation current{};
		PrivateCallOutcome privateCallOutcome{ PrivateCallOutcome::kNotObserved };
	};

	struct AdapterAction
	{
		AdapterActionKind kind{ AdapterActionKind::kNone };
		LeaseIdentity lease{};
		bool requestedAppCulled{ false };

		constexpr bool operator==(const AdapterAction&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidAdapterAction(
		const AdapterAction& action) noexcept
	{
		switch (action.kind) {
		case AdapterActionKind::kNone:
			return action.lease == LeaseIdentity{} &&
			       !action.requestedAppCulled;
		case AdapterActionKind::kSetAppCulled:
			return IsValidMutationLease(action.lease) &&
			       action.requestedAppCulled;
		case AdapterActionKind::kRestorePriorAppCull:
			return IsValidMutationLease(action.lease) &&
			       !action.requestedAppCulled;
		}
		return false;
	}

	struct TransitionResult
	{
		TransitionStatus status{ TransitionStatus::kRejected };
		Failure eventFailure{ Failure::kNone };
		Failure firstFailure{ Failure::kNone };
		AdapterAction action{};
		Bridge::TokenIssuerState nextMutationTokenState{};
		LeaseClaim activeClaim{ LeaseClaim::kNone };
		bool cleanupProven{ false };
		bool authorizationBindingScrubbed{ true };
		bool fullyScrubbed{ true };
	};

	/**
	 * Inventory/equipment UI can invalidate the live clone receipt after this
	 * scope has already retained and app-culled the exact RRMirrorItem.  That is
	 * not permission to publish the capture, but it also is not evidence that the
	 * retained cleanup target changed.  This value-only decision keeps that
	 * expected lifecycle abort separate from ordinary live revalidation faults.
	 */
	enum class LifecycleCleanupOnlyAction : std::uint8_t
	{
		kNotApplicable,
		kReleaseNoWriteLease,
		kRestorePriorAppCull,
		kFailStop
	};

	struct LifecycleCleanupOnlyInput
	{
		bool lifecycleTransitionSuspended{ false };
		bool adapterScopeActive{ false };
		bool exactAdapterLeaseOwner{ false };
		bool priorStateRecorded{ false };
		bool observedNoWriteHistory{ false };
		bool mutationOwnedHistory{ false };
		bool mutationMayHaveOccurred{ false };
		bool retainedExactItem{ false };
		bool retainedItemMatchesMutationTarget{ false };
		bool priorAppCulled{ false };
	};

	struct LifecycleCleanupOnlyDecision
	{
		LifecycleCleanupOnlyAction action{
			LifecycleCleanupOnlyAction::kNotApplicable };
		bool requestedAppCulled{ false };
	};

	/**
	 * The inventory transition can replace the equipped clone one render callback
	 * before either MenuOpenCloseEvent or UI::numItemMenus becomes observable.
	 * This is the only signal that may bridge that gap: a completed native pass,
	 * a successful guarded reread proving that the live exact ancestry changed,
	 * and exclusive ownership of the retained object that this scope itself
	 * app-culled.  The caller still has to restore/read back the recorded prior
	 * value before releasing the object and must discard the capture.
	 */
	struct SynchronousSubtreeReplacementInput
	{
		bool privatePassReturnedNormally{ false };
		bool observerHealthy{ false };
		bool liveRevalidationCompleted{ false };
		bool liveExactAncestryCurrent{ false };
		bool currentCullReadSucceeded{ false };
		bool currentCullIsSuppressed{ false };
		bool adapterScopeActive{ false };
		bool exactAdapterLeaseOwner{ false };
		bool priorStateRecorded{ false };
		bool mutationOwnedHistory{ false };
		bool mutationMayHaveOccurred{ false };
		bool retainedExactItem{ false };
		bool retainedItemMatchesMutationTarget{ false };
		bool foreignMutationOrLeaseObserved{ false };
		Failure firstFailure{ Failure::kNone };
	};

	[[nodiscard]] constexpr bool
	CanCleanupSynchronousSubtreeReplacement(
		const SynchronousSubtreeReplacementInput& input) noexcept
	{
		return input.privatePassReturnedNormally && input.observerHealthy &&
		       input.liveRevalidationCompleted &&
		       !input.liveExactAncestryCurrent &&
		       input.currentCullReadSucceeded && input.currentCullIsSuppressed &&
		       input.adapterScopeActive && input.exactAdapterLeaseOwner &&
		       input.priorStateRecorded && input.mutationOwnedHistory &&
		       input.mutationMayHaveOccurred && input.retainedExactItem &&
		       input.retainedItemMatchesMutationTarget &&
		       !input.foreignMutationOrLeaseObserved &&
		       input.firstFailure == Failure::kNone;
	}

	enum class LifecycleFailureContext : std::uint8_t
	{
		kOrdinary,
		kPrivatePassFailedWithoutTrackedNativeFault
	};

	[[nodiscard]] constexpr LifecycleCleanupOnlyDecision
	EvaluateLifecycleCleanupOnly(
		const LifecycleCleanupOnlyInput& input) noexcept
	{
		if (!input.lifecycleTransitionSuspended)
			return {};
		if (!input.adapterScopeActive || !input.exactAdapterLeaseOwner) {
			return { LifecycleCleanupOnlyAction::kFailStop, false };
		}

		if (!input.mutationMayHaveOccurred) {
			if (input.mutationOwnedHistory || input.retainedExactItem ||
				input.retainedItemMatchesMutationTarget) {
				return { LifecycleCleanupOnlyAction::kFailStop, false };
			}
			return {
				LifecycleCleanupOnlyAction::kReleaseNoWriteLease,
				input.priorAppCulled
			};
		}

		if (!input.priorStateRecorded || input.observedNoWriteHistory ||
			!input.retainedExactItem ||
			!input.retainedItemMatchesMutationTarget) {
			return { LifecycleCleanupOnlyAction::kFailStop, false };
		}
		return {
			LifecycleCleanupOnlyAction::kRestorePriorAppCull,
			input.priorAppCulled
		};
	}

	/**
	 * Only failures produced by live subtree freshness disappearing during the
	 * InventoryMenu/equipment transition may be downgraded after exact retained-
	 * item cleanup.  Every adapter, ordering, write, readback-value, lease, or
	 * tracked native failure remains terminal even when the transition overlaps
	 * it.  The one explicit clean-private-pass context admits only the normal
	 * engine false-return that can race an InventoryMenu transition; the caller
	 * must have independently ruled out every tracked native fault.
	 */
	[[nodiscard]] constexpr bool IsLifecycleAttributableFailure(
		const Failure failure,
		const LifecycleFailureContext context =
			LifecycleFailureContext::kOrdinary) noexcept
	{
		return failure == Failure::kNone ||
		       failure == Failure::kReadbackMissing ||
		       failure == Failure::kIdentityDrift ||
		       (context == LifecycleFailureContext::
					kPrivatePassFailedWithoutTrackedNativeFault &&
			failure == Failure::kAbnormalNativeReturn);
	}

	static_assert(std::is_trivially_copyable_v<AttemptIdentity>);
	static_assert(std::is_standard_layout_v<AttemptIdentity>);
	static_assert(std::is_trivially_copyable_v<ExactSubtreeIdentity>);
	static_assert(std::is_standard_layout_v<ExactSubtreeIdentity>);
	static_assert(std::is_trivially_copyable_v<CurrentSubtreeObservation>);
	static_assert(std::is_standard_layout_v<CurrentSubtreeObservation>);
	static_assert(std::is_trivially_copyable_v<LeaseIdentity>);
	static_assert(std::is_standard_layout_v<LeaseIdentity>);
	static_assert(std::is_trivially_copyable_v<AdapterAction>);
	static_assert(std::is_standard_layout_v<AdapterAction>);
	static_assert(std::is_trivially_copyable_v<LifecycleCleanupOnlyInput>);
	static_assert(std::is_standard_layout_v<LifecycleCleanupOnlyInput>);
	static_assert(std::is_trivially_copyable_v<LifecycleCleanupOnlyDecision>);
	static_assert(std::is_standard_layout_v<LifecycleCleanupOnlyDecision>);
	static_assert(std::is_trivially_copyable_v<SynchronousSubtreeReplacementInput>);
	static_assert(std::is_standard_layout_v<SynchronousSubtreeReplacementInput>);
	static_assert(std::is_trivially_copyable_v<LifecycleFailureContext>);

	/**
	 * Single-use exact suppression scope.  A fault permanently closes admission.
	 * If a write may have happened, the live authorization binding is moved into
	 * a cleanup-only ticket.  That ticket accepts only one exact restore/readback
	 * completion and can never authorize another operation.
	 */
	class LeaseMachine
	{
	public:
		LeaseMachine() = default;
		LeaseMachine(const LeaseMachine&) = delete;
		LeaseMachine& operator=(const LeaseMachine&) = delete;
		LeaseMachine(LeaseMachine&&) = delete;
		LeaseMachine& operator=(LeaseMachine&&) = delete;

		[[nodiscard]] constexpr TransitionResult Begin(
			const BeginInput& input) noexcept
		{
			if (begun_ || phase_ != Phase::kIdle) {
				return RejectRepeatedBegin();
			}
			begun_ = true;
			mutationTokenState_ = input.mutationTokenState;

			if (!Bridge::IsValidTokenIssuerState(mutationTokenState_)) {
				return FailWithoutCleanup(Failure::kInvalidTokenIssuerState);
			}
			if (const auto failure = AdapterFailure(
					input.preflight.adapterStatus);
				failure != Failure::kNone) {
				return FailWithoutCleanup(failure);
			}
			if (!IsValidAttemptIdentity(input.preflight.attempt)) {
				return FailWithoutCleanup(Failure::kInvalidAttemptIdentity);
			}
			if (!IsValidSubtreeIdentity(input.preflight.subtree)) {
				return FailWithoutCleanup(Failure::kInvalidSubtreeIdentity);
			}
			if (!HasExactCurrentAncestry(input.preflight)) {
				return FailWithoutCleanup(Failure::kInvalidCurrentAncestry);
			}
			if (!input.attemptReservedBeforeSuppression) {
				return FailWithoutCleanup(Failure::kAttemptNotReserved);
			}
			if (!input.currentPoseSampleCompletedBeforeSuppression) {
				return FailWithoutCleanup(Failure::kPoseSampleMissing);
			}
			if (input.preflight.foreignMutationOrLeaseObserved) {
				return FailWithoutCleanup(Failure::kPreexistingConflict);
			}

			priorStateRecorded_ = true;
			priorAppCulled_ = input.preflight.appCulled;
			activeLease_ = {
				input.preflight.attempt, input.preflight.subtree, 0 };

			if (priorAppCulled_) {
				observedNoWriteHistory_ = true;
				activeClaim_ = LeaseClaim::kObservedAlreadyCulledNoWrite;
				phase_ = Phase::kSuppressedWithoutWrite;
				return MakeResult(TransitionStatus::kLeaseActiveWithoutWrite);
			}

			const auto issue = Bridge::IssueNextToken(mutationTokenState_);
			mutationTokenState_ = issue.nextState;
			if (issue.status == Bridge::TokenIssueStatus::kExhausted) {
				return FailWithoutCleanup(Failure::kMutationTokenExhausted);
			}
			if (issue.status != Bridge::TokenIssueStatus::kIssued ||
				!Bridge::IsValidTokenIssueReceipt(issue.receipt)) {
				return FailWithoutCleanup(Failure::kInvalidTokenIssuerState);
			}

			activeLease_.nativeMutationToken = issue.receipt.issuedToken;
			activeClaim_ = LeaseClaim::kMutationReadbackPending;
			phase_ = Phase::kAwaitingSuppressionReadback;
			return MakeResult(
				TransitionStatus::kSuppressionWriteRequested,
				Failure::kNone, MakeSetAction(activeLease_));
		}

		[[nodiscard]] constexpr TransitionResult CompleteSuppressionReadback(
			const AdapterEvidence& evidence) noexcept
		{
			if (phase_ != Phase::kAwaitingSuppressionReadback) {
				return RejectUnexpectedOrdering();
			}
			if (evidence.presentedLease != activeLease_) {
				return QuarantineForRestore(Failure::kStaleOrABALease);
			}
			if (const auto failure = AdapterFailure(
					evidence.current.adapterStatus);
				failure != Failure::kNone) {
				return QuarantineForRestore(failure);
			}
			if (!MatchesActiveIdentity(evidence.current)) {
				return QuarantineForRestore(Failure::kIdentityDrift);
			}
			if (!evidence.writeAttempted) {
				return QuarantineForRestore(Failure::kWriteNotAttempted);
			}
			if (!evidence.requestedAppCulled) {
				return QuarantineForRestore(
					Failure::kUnexpectedAdapterRequest);
			}
			if (evidence.current.foreignMutationOrLeaseObserved) {
				return QuarantineForRestore(
					Failure::kConcurrentOrUnexpectedState);
			}
			if (!evidence.current.appCulled) {
				return QuarantineForRestore(
					Failure::kSuppressionReadbackMismatch);
			}

			mutationOwnedHistory_ = true;
			activeClaim_ = LeaseClaim::kExclusiveMutationOwned;
			phase_ = Phase::kSuppressedWithOwnedWrite;
			return MakeResult(TransitionStatus::kLeaseActiveWithOwnedWrite);
		}

		[[nodiscard]] constexpr TransitionResult Close(
			const CloseInput& input) noexcept
		{
			if (phase_ == Phase::kAwaitingRestoreReadback ||
				phase_ == Phase::kFailStopRestorePending) {
				LatchFailure(Failure::kRepeatedClose);
				phase_ = Phase::kFailStopRestorePending;
				return MakeResult(
					TransitionStatus::kFailStopAwaitingRestoreReadback,
					Failure::kRepeatedClose);
			}
			if (phase_ == Phase::kClosed || phase_ == Phase::kFaulted) {
				LatchFailure(Failure::kRepeatedClose);
				phase_ = Phase::kFaulted;
				ScrubAllBindings();
				return MakeResult(
					TransitionStatus::kFailStopped, Failure::kRepeatedClose);
			}
			if (phase_ == Phase::kIdle) {
				return FailWithoutCleanup(Failure::kCloseWithoutBegin);
			}
			if (phase_ == Phase::kAwaitingSuppressionReadback) {
				return QuarantineForRestore(Failure::kReadbackMissing);
			}
			if (phase_ != Phase::kSuppressedWithoutWrite &&
				phase_ != Phase::kSuppressedWithOwnedWrite) {
				return RejectUnexpectedOrdering();
			}

			Failure eventFailure = Failure::kNone;
			auto record = [&](const Failure failure) constexpr {
				if (failure == Failure::kNone)
					return;
				if (eventFailure == Failure::kNone)
					eventFailure = failure;
				LatchFailure(failure);
			};

			if (input.presentedLease != activeLease_)
				record(Failure::kStaleOrABALease);
			record(PrivateReturnFailure(input.privateCallOutcome));
			record(AdapterFailure(input.current.adapterStatus));
			if (input.current.adapterStatus ==
					AdapterCallStatus::kReturnedNormallyWithReadback) {
				if (!MatchesActiveIdentity(input.current))
					record(Failure::kIdentityDrift);
				if (input.current.foreignMutationOrLeaseObserved)
					record(Failure::kConcurrentOrUnexpectedState);
				if (!input.current.appCulled)
					record(Failure::kConcurrentOrUnexpectedState);
			}

			if (phase_ == Phase::kSuppressedWithOwnedWrite) {
				MoveActiveLeaseToCleanupTicket();
				restoreCommandIssued_ = true;
				phase_ = firstFailure_ == Failure::kNone ?
					Phase::kAwaitingRestoreReadback :
					Phase::kFailStopRestorePending;
				return MakeResult(
					firstFailure_ == Failure::kNone ?
						TransitionStatus::kRestoreWriteRequested :
						TransitionStatus::kFailStopRestoreRequested,
					eventFailure, MakeRestoreAction(cleanupTicket_));
			}

			// An already-culled subtree was observed, never owned or rewritten.
			ScrubAllBindings();
			if (firstFailure_ != Failure::kNone) {
				phase_ = Phase::kFaulted;
				return MakeResult(
					TransitionStatus::kFailStopped, eventFailure);
			}
			cleanupProven_ = true;
			phase_ = Phase::kClosed;
			return MakeResult(TransitionStatus::kClosedClean);
		}

		[[nodiscard]] constexpr TransitionResult CompleteRestoreReadback(
			const AdapterEvidence& evidence) noexcept
		{
			if (phase_ != Phase::kAwaitingRestoreReadback &&
				phase_ != Phase::kFailStopRestorePending) {
				return RejectUnexpectedOrdering();
			}
			if (evidence.presentedLease != cleanupTicket_) {
				LatchFailure(Failure::kStaleOrABALease);
				phase_ = Phase::kFailStopRestorePending;
				return MakeResult(
					TransitionStatus::kFailStopRestoreRequested,
					Failure::kStaleOrABALease,
					MakeRestoreAction(cleanupTicket_));
			}

			Failure eventFailure = AdapterFailure(evidence.current.adapterStatus);
			if (eventFailure == Failure::kNone &&
				!MatchesCleanupIdentity(evidence.current)) {
				eventFailure = Failure::kIdentityDrift;
			}
			if (eventFailure == Failure::kNone && !evidence.writeAttempted)
				eventFailure = Failure::kWriteNotAttempted;
			if (eventFailure == Failure::kNone &&
				evidence.requestedAppCulled != priorAppCulled_) {
				eventFailure = Failure::kUnexpectedAdapterRequest;
			}
			if (eventFailure == Failure::kNone &&
				evidence.current.foreignMutationOrLeaseObserved) {
				eventFailure = Failure::kConcurrentOrUnexpectedState;
			}
			if (eventFailure == Failure::kNone &&
				evidence.current.appCulled != priorAppCulled_) {
				eventFailure = Failure::kRestoreReadbackMismatch;
			}

			if (eventFailure != Failure::kNone) {
				LatchFailure(eventFailure);
				cleanupProven_ = false;
				phase_ = Phase::kFailStopRestorePending;
				// A failed native restore or an unavailable/mismatching readback
				// cannot prove that the owned write was undone.  Keep only the
				// exact cleanup ticket and reissue the idempotent prior-value write;
				// the sticky fault permanently prevents this scope from succeeding.
				return MakeResult(
					TransitionStatus::kFailStopRestoreRequested, eventFailure,
					MakeRestoreAction(cleanupTicket_));
			}

			cleanupProven_ = true;
			ScrubAllBindings();
			if (firstFailure_ == Failure::kNone) {
				phase_ = Phase::kClosed;
				return MakeResult(TransitionStatus::kClosedClean);
			}
			phase_ = Phase::kFaulted;
			return MakeResult(TransitionStatus::kFailStopped);
		}

		[[nodiscard]] constexpr Phase CurrentPhase() const noexcept
		{
			return phase_;
		}
		[[nodiscard]] constexpr Failure FirstFailure() const noexcept
		{
			return firstFailure_;
		}
		[[nodiscard]] constexpr LeaseClaim ActiveClaim() const noexcept
		{
			return activeClaim_;
		}
		[[nodiscard]] constexpr Bridge::TokenIssuerState MutationTokenState()
			const noexcept
		{
			return mutationTokenState_;
		}
		[[nodiscard]] constexpr bool Faulted() const noexcept
		{
			return firstFailure_ != Failure::kNone;
		}
		[[nodiscard]] constexpr bool CleanupProven() const noexcept
		{
			return cleanupProven_;
		}
		[[nodiscard]] constexpr bool ClosedCleanly() const noexcept
		{
			return phase_ == Phase::kClosed && firstFailure_ == Failure::kNone &&
			       cleanupProven_ && FullyScrubbed();
		}
		[[nodiscard]] constexpr bool PriorStateRecorded() const noexcept
		{
			return priorStateRecorded_;
		}
		[[nodiscard]] constexpr bool PriorAppCulled() const noexcept
		{
			return priorAppCulled_;
		}
		[[nodiscard]] constexpr bool ObservedNoWriteHistory() const noexcept
		{
			return observedNoWriteHistory_;
		}
		[[nodiscard]] constexpr bool MutationOwnedHistory() const noexcept
		{
			return mutationOwnedHistory_;
		}
		[[nodiscard]] constexpr bool AuthorizationBindingScrubbed() const noexcept
		{
			return activeLease_ == LeaseIdentity{};
		}
		[[nodiscard]] constexpr bool CleanupOnlyTicketPending() const noexcept
		{
			return cleanupTicket_ != LeaseIdentity{};
		}
		[[nodiscard]] constexpr bool FullyScrubbed() const noexcept
		{
			return AuthorizationBindingScrubbed() &&
			       !CleanupOnlyTicketPending();
		}

	private:
		[[nodiscard]] static constexpr Failure AdapterFailure(
			const AdapterCallStatus status) noexcept
		{
			switch (status) {
			case AdapterCallStatus::kReturnedNormallyWithReadback:
				return Failure::kNone;
			case AdapterCallStatus::kNotAttempted:
				return Failure::kAdapterNotInvoked;
			case AdapterCallStatus::kReturnedFailure:
				return Failure::kAbnormalNativeReturn;
			case AdapterCallStatus::kExceptionCaught:
				return Failure::kNativeException;
			case AdapterCallStatus::kReadbackUnavailable:
				return Failure::kReadbackMissing;
			}
			return Failure::kAbnormalNativeReturn;
		}

		[[nodiscard]] static constexpr Failure PrivateReturnFailure(
			const PrivateCallOutcome outcome) noexcept
		{
			switch (outcome) {
			case PrivateCallOutcome::kReturnedNormally:
				return Failure::kNone;
			case PrivateCallOutcome::kNotObserved:
				return Failure::kPrivateReturnMissing;
			case PrivateCallOutcome::kReturnedAbnormally:
				return Failure::kAbnormalNativeReturn;
			case PrivateCallOutcome::kExceptionCaught:
				return Failure::kNativeException;
			}
			return Failure::kAbnormalNativeReturn;
		}

		[[nodiscard]] constexpr bool MatchesActiveIdentity(
			const CurrentSubtreeObservation& observation) const noexcept
		{
			return HasExactCurrentAncestry(observation) &&
			       observation.attempt == activeLease_.attempt &&
			       observation.subtree == activeLease_.subtree;
		}

		[[nodiscard]] constexpr bool MatchesCleanupIdentity(
			const CurrentSubtreeObservation& observation) const noexcept
		{
			return HasExactCurrentAncestry(observation) &&
			       observation.attempt == cleanupTicket_.attempt &&
			       observation.subtree == cleanupTicket_.subtree;
		}

		[[nodiscard]] static constexpr AdapterAction MakeSetAction(
			const LeaseIdentity& lease) noexcept
		{
			return { AdapterActionKind::kSetAppCulled, lease, true };
		}

		[[nodiscard]] static constexpr AdapterAction MakeRestoreAction(
			const LeaseIdentity& lease) noexcept
		{
			return { AdapterActionKind::kRestorePriorAppCull, lease, false };
		}

		constexpr void LatchFailure(const Failure failure) noexcept
		{
			if (failure != Failure::kNone && firstFailure_ == Failure::kNone)
				firstFailure_ = failure;
		}

		constexpr void MoveActiveLeaseToCleanupTicket() noexcept
		{
			cleanupTicket_ = activeLease_;
			activeLease_ = {};
			activeClaim_ = LeaseClaim::kNone;
		}

		constexpr void ScrubAllBindings() noexcept
		{
			activeLease_ = {};
			cleanupTicket_ = {};
			activeClaim_ = LeaseClaim::kNone;
			restoreCommandIssued_ = false;
		}

		[[nodiscard]] constexpr TransitionResult MakeResult(
			const TransitionStatus status,
			const Failure eventFailure = Failure::kNone,
			const AdapterAction& action = {}) const noexcept
		{
			return {
				status,
				eventFailure,
				firstFailure_,
				action,
				mutationTokenState_,
				activeClaim_,
				cleanupProven_,
				AuthorizationBindingScrubbed(),
				FullyScrubbed()
			};
		}

		[[nodiscard]] constexpr TransitionResult FailWithoutCleanup(
			const Failure failure) noexcept
		{
			LatchFailure(failure);
			phase_ = Phase::kFaulted;
			ScrubAllBindings();
			return MakeResult(TransitionStatus::kFailStopped, failure);
		}

		[[nodiscard]] constexpr TransitionResult QuarantineForRestore(
			const Failure failure) noexcept
		{
			LatchFailure(failure);
			if (activeLease_ != LeaseIdentity{})
				MoveActiveLeaseToCleanupTicket();
			phase_ = Phase::kFailStopRestorePending;
			if (!restoreCommandIssued_) {
				restoreCommandIssued_ = true;
				return MakeResult(
					TransitionStatus::kFailStopRestoreRequested, failure,
					MakeRestoreAction(cleanupTicket_));
			}
			return MakeResult(
				TransitionStatus::kFailStopAwaitingRestoreReadback, failure);
		}

		[[nodiscard]] constexpr TransitionResult RejectRepeatedBegin() noexcept
		{
			LatchFailure(Failure::kRepeatedBegin);
			if (phase_ == Phase::kAwaitingSuppressionReadback ||
				phase_ == Phase::kSuppressedWithOwnedWrite) {
				return QuarantineForRestore(Failure::kRepeatedBegin);
			}
			if (phase_ == Phase::kAwaitingRestoreReadback ||
				phase_ == Phase::kFailStopRestorePending) {
				phase_ = Phase::kFailStopRestorePending;
				return MakeResult(
					TransitionStatus::kFailStopAwaitingRestoreReadback,
					Failure::kRepeatedBegin);
			}
			phase_ = Phase::kFaulted;
			ScrubAllBindings();
			return MakeResult(
				TransitionStatus::kFailStopped, Failure::kRepeatedBegin);
		}

		[[nodiscard]] constexpr TransitionResult RejectUnexpectedOrdering()
			noexcept
		{
			LatchFailure(Failure::kUnexpectedOrdering);
			if (phase_ == Phase::kAwaitingSuppressionReadback ||
				phase_ == Phase::kSuppressedWithOwnedWrite) {
				return QuarantineForRestore(Failure::kUnexpectedOrdering);
			}
			if (phase_ == Phase::kAwaitingRestoreReadback ||
				phase_ == Phase::kFailStopRestorePending) {
				phase_ = Phase::kFailStopRestorePending;
				return MakeResult(
					TransitionStatus::kFailStopAwaitingRestoreReadback,
					Failure::kUnexpectedOrdering);
			}
			phase_ = Phase::kFaulted;
			ScrubAllBindings();
			return MakeResult(
				TransitionStatus::kFailStopped, Failure::kUnexpectedOrdering);
		}

		Phase phase_{ Phase::kIdle };
		Failure firstFailure_{ Failure::kNone };
		Bridge::TokenIssuerState mutationTokenState_{};
		LeaseIdentity activeLease_{};
		LeaseIdentity cleanupTicket_{};
		LeaseClaim activeClaim_{ LeaseClaim::kNone };
		bool begun_{ false };
		bool priorStateRecorded_{ false };
		bool priorAppCulled_{ false };
		bool observedNoWriteHistory_{ false };
		bool mutationOwnedHistory_{ false };
		bool cleanupProven_{ false };
		bool restoreCommandIssued_{ false };
	};
}
