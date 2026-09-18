#pragma once

#include "HandMirrorContentRuntimePolicy.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace HandMirrorRuntimeBridgePolicy
{
	// These technical bridge seams are wired by the separate marker-gated
	// internal runtime.  Product acceptance, raw-observer authority, third-person
	// delivery, and public API-v1 publication remain independently denied.
	inline constexpr bool kSingletonOwnerGrantWired = true;
	inline constexpr bool kCapturePublicationIdentityWired = true;
	inline constexpr bool kSameFrameMovingPlaneWired = true;
	inline constexpr bool kExactItemSubtreeSuppressionWired = true;
	inline constexpr bool kFirstPersonDeliveryRouteAccepted = true;
	inline constexpr bool kThirdPersonDeliveryRouteAccepted = false;
	inline constexpr bool kRawObserverEvidenceAuthorizesBridge = false;
	inline constexpr bool kRuntimeBridgeWired = true;
	inline constexpr bool kRuntimeAcceptanceGranted = false;
	inline constexpr bool kPlanarAPIV1HandPublicationAllowed = false;

	enum class TokenIssueStatus : std::uint8_t
	{
		kIssued,
		kExhausted,
		kInvalidState
	};

	struct TokenIssuerState
	{
		std::uint64_t lastIssuedToken{ 0 };
		bool exhausted{ false };

		constexpr bool operator==(const TokenIssuerState&) const noexcept = default;
	};

	struct TokenIssueReceipt
	{
		std::uint64_t predecessorToken{ 0 };
		std::uint64_t issuedToken{ 0 };
		bool exhaustedAfterIssue{ false };

		constexpr bool operator==(const TokenIssueReceipt&) const noexcept = default;
	};

	struct TokenIssueResult
	{
		TokenIssueStatus status{ TokenIssueStatus::kInvalidState };
		TokenIssueReceipt receipt{};
		TokenIssuerState nextState{};

		constexpr bool operator==(const TokenIssueResult&) const noexcept = default;
	};

	inline constexpr std::uint64_t kMaximumIssuedToken =
		std::numeric_limits<std::uint64_t>::max();

	[[nodiscard]] constexpr bool IsValidTokenIssuerState(
		const TokenIssuerState state) noexcept
	{
		return state.exhausted ? state.lastIssuedToken == kMaximumIssuedToken :
			state.lastIssuedToken != kMaximumIssuedToken;
	}

	[[nodiscard]] constexpr bool IsValidTokenIssueReceipt(
		const TokenIssueReceipt receipt) noexcept
	{
		return receipt.predecessorToken != kMaximumIssuedToken &&
		       receipt.issuedToken == receipt.predecessorToken + 1 &&
		       receipt.exhaustedAfterIssue ==
			       (receipt.issuedToken == kMaximumIssuedToken);
	}

	/**
	 * Generic nonwrapping token source.  Issuing UINT64_MAX succeeds once and
	 * returns an exhausted state.  Every later call returns the identical
	 * exhausted state and no receipt; exhaustion can never be cleared by this
	 * policy or represented as a wrapped zero token.
	 */
	[[nodiscard]] constexpr TokenIssueResult IssueNextToken(
		const TokenIssuerState state) noexcept
	{
		TokenIssueResult result{};
		result.nextState = state;
		if (!IsValidTokenIssuerState(state)) {
			result.status = TokenIssueStatus::kInvalidState;
			return result;
		}
		if (state.exhausted) {
			result.status = TokenIssueStatus::kExhausted;
			return result;
		}

		const auto token = state.lastIssuedToken + 1;
		result.status = TokenIssueStatus::kIssued;
		result.receipt = {
			state.lastIssuedToken, token, token == kMaximumIssuedToken };
		result.nextState = { token, token == kMaximumIssuedToken };
		return result;
	}

	inline constexpr std::size_t kMaximumTrackedHandCandidates = 8;

	struct BoundedCandidateCount
	{
		std::size_t exactCount{ 0 };
		bool overflowed{ false };

		constexpr bool operator==(const BoundedCandidateCount&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsValidCandidateCount(
		const BoundedCandidateCount count) noexcept
	{
		return count.exactCount <= kMaximumTrackedHandCandidates;
	}

	/**
	 * Counts only exact eligible hand identities.  The ninth candidate sets a
	 * sticky overflow without incrementing or wrapping the bounded count.
	 */
	[[nodiscard]] constexpr BoundedCandidateCount CountExactHandCandidate(
		const BoundedCandidateCount count) noexcept
	{
		if (count.overflowed)
			return count;
		if (!IsValidCandidateCount(count) ||
			count.exactCount == kMaximumTrackedHandCandidates) {
			return { kMaximumTrackedHandCandidates, true };
		}
		return { count.exactCount + 1, false };
	}

	enum class OwnerKind : std::uint8_t
	{
		kDark,
		kWall,
		kHand
	};

	struct WallOwnerIdentity
	{
		std::uint32_t formID{ 0 };
		std::uint64_t candidateGeneration{ 0 };

		constexpr bool operator==(const WallOwnerIdentity&) const noexcept = default;
	};

	struct MirrorOwnerIdentity
	{
		OwnerKind kind{ OwnerKind::kDark };
		WallOwnerIdentity wall{};
		HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity hand{};

		constexpr bool operator==(const MirrorOwnerIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidWallOwner(
		const WallOwnerIdentity owner) noexcept
	{
		return owner.formID != 0 && owner.candidateGeneration != 0;
	}

	[[nodiscard]] constexpr MirrorOwnerIdentity MakeWallOwner(
		const WallOwnerIdentity owner) noexcept
	{
		return { OwnerKind::kWall, owner, {} };
	}

	[[nodiscard]] constexpr MirrorOwnerIdentity MakeHandOwner(
		const HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity& owner) noexcept
	{
		return { OwnerKind::kHand, {}, owner };
	}

	[[nodiscard]] constexpr bool IsDarkOwner(
		const MirrorOwnerIdentity& owner) noexcept
	{
		return owner == MirrorOwnerIdentity{};
	}

	[[nodiscard]] constexpr bool IsValidOwner(
		const MirrorOwnerIdentity& owner) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		if (owner.kind == OwnerKind::kWall) {
			return IsValidWallOwner(owner.wall) &&
			       owner.hand == HandMirrorOwnerIdentity{};
		}
		if (owner.kind == OwnerKind::kHand) {
			return owner.wall == WallOwnerIdentity{} &&
			       IsValidHandMirrorOwnerIdentity(owner.hand);
		}
		return false;
	}

	[[nodiscard]] constexpr bool SameOwner(
		const MirrorOwnerIdentity& left,
		const MirrorOwnerIdentity& right) noexcept
	{
		return IsValidOwner(left) && IsValidOwner(right) && left == right;
	}

	struct PrivateTargetIdentity
	{
		std::uint64_t allocationGeneration{ 0 };
		std::uint64_t colorResourceToken{ 0 };
		std::uint64_t depthResourceToken{ 0 };

		constexpr bool operator==(const PrivateTargetIdentity&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsValidPrivateTarget(
		const PrivateTargetIdentity target) noexcept
	{
		return target.allocationGeneration != 0 &&
		       target.colorResourceToken != 0 &&
		       target.depthResourceToken != 0 &&
		       target.colorResourceToken != target.depthResourceToken;
	}

	[[nodiscard]] constexpr bool ArePrivateTargetsPhysicallyDisjoint(
		const PrivateTargetIdentity& left,
		const PrivateTargetIdentity& right) noexcept
	{
		return IsValidPrivateTarget(left) && IsValidPrivateTarget(right) &&
		       left.allocationGeneration != right.allocationGeneration &&
		       left.colorResourceToken != right.colorResourceToken &&
		       left.colorResourceToken != right.depthResourceToken &&
		       left.depthResourceToken != right.colorResourceToken &&
		       left.depthResourceToken != right.depthResourceToken;
	}

	struct PriorPublicationIdentity
	{
		MirrorOwnerIdentity owner{};
		std::uint64_t ownerLeaseSequence{ 0 };
		std::uint64_t attemptSequence{ 0 };
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		PrivateTargetIdentity privateTarget{};
		std::uint64_t captureSequence{ 0 };
		std::uint64_t nativePublicationToken{ 0 };

		constexpr bool operator==(const PriorPublicationIdentity&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsValidPriorPublication(
		const PriorPublicationIdentity& publication) noexcept
	{
		return IsValidOwner(publication.owner) &&
		       publication.ownerLeaseSequence != 0 &&
		       publication.attemptSequence != 0 &&
		       publication.sourceSequence != 0 &&
		       publication.mainViewFrame != 0 &&
		       IsValidPrivateTarget(publication.privateTarget) &&
		       publication.captureSequence != 0 &&
		       publication.nativePublicationToken != 0;
	}

	struct PublicationRetirementReceipt
	{
		PriorPublicationIdentity retiredPublication{};
		MirrorOwnerIdentity successorOwner{};
		std::uint64_t successorOwnerLeaseSequence{ 0 };
		std::uint64_t retirementToken{ 0 };
		bool invalidationCommitted{ false };
		bool retiredPublicationNoLongerAcquirable{ false };
		bool retiredResourcesUnavailableToSuccessor{ false };

		constexpr bool operator==(
			const PublicationRetirementReceipt&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsEmptyRetirementReceipt(
		const PublicationRetirementReceipt& receipt) noexcept
	{
		return receipt == PublicationRetirementReceipt{};
	}

	[[nodiscard]] constexpr bool MatchesExactRetirementReceipt(
		const PriorPublicationIdentity& prior,
		const MirrorOwnerIdentity& successor,
		const std::uint64_t successorOwnerLeaseSequence,
		const PublicationRetirementReceipt& receipt) noexcept
	{
		if (!IsValidPriorPublication(prior) ||
			receipt.retiredPublication != prior ||
			receipt.successorOwner != successor || receipt.retirementToken == 0 ||
			!receipt.invalidationCommitted ||
			!receipt.retiredPublicationNoLongerAcquirable ||
			!receipt.retiredResourcesUnavailableToSuccessor) {
			return false;
		}

		if (IsValidOwner(successor)) {
			return receipt.successorOwnerLeaseSequence ==
				       successorOwnerLeaseSequence &&
			       successorOwnerLeaseSequence != 0;
		}
		return IsDarkOwner(successor) && successorOwnerLeaseSequence == 0 &&
		       receipt.successorOwnerLeaseSequence == 0;
	}

	enum class OwnerGrantStatus : std::uint8_t
	{
		kGrantedWall,
		kGrantedHand,
		kDarkNoEligibleOwner,
		kDarkAmbiguous,
		kCandidateCountOverflow,
		kInvalidEvidence,
		kBlockedUntilPreviousPublicationInvalidated
	};

	struct OwnerGrantInput
	{
		HandMirrorContentRuntimePolicy::HandWallArbitrationInput arbitration{};
		WallOwnerIdentity exactWallCandidate{};
		BoundedCandidateCount eligibleHandCandidates{};
		PriorPublicationIdentity previousPublication{};
		PublicationRetirementReceipt retirementReceipt{};
		std::uint64_t nextOwnerLeaseSequence{ 0 };
		bool wallCandidateCurrent{ false };
	};

	struct OwnerGrant
	{
		OwnerGrantStatus status{ OwnerGrantStatus::kInvalidEvidence };
		MirrorOwnerIdentity owner{};
		std::uint64_t ownerLeaseSequence{ 0 };
		bool previousPublicationInvalidationRequired{ false };
		bool mayBeginCapture{ false };

		constexpr bool operator==(const OwnerGrant&) const noexcept = default;
	};

	/**
	 * Extends the existing hand/wall hysteresis decision with the complete value
	 * identity of the publication already carried by the singleton channel.  A
	 * same-identity wall may keep its established continuity frame.  Every hand
	 * attempt must instead present an exact, successor-bound retirement receipt;
	 * a boolean "invalidated" claim cannot retire or replay a publication.
	 */
	[[nodiscard]] constexpr OwnerGrant DecideOwnerGrant(
		const OwnerGrantInput& input) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		OwnerGrant result{};
		const bool exactWallEligible = IsValidWallOwner(input.exactWallCandidate) &&
			input.wallCandidateCurrent;
		if (input.arbitration.exactWallEligible != exactWallEligible) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			return result;
		}
		if (!IsValidCandidateCount(input.eligibleHandCandidates)) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			return result;
		}
		if (input.eligibleHandCandidates.overflowed) {
			result.status = OwnerGrantStatus::kCandidateCountOverflow;
			return result;
		}
		const bool hasPreviousPublication =
			input.previousPublication != PriorPublicationIdentity{};
		if (hasPreviousPublication &&
			!IsValidPriorPublication(input.previousPublication)) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			return result;
		}
		if (!hasPreviousPublication &&
			!IsEmptyRetirementReceipt(input.retirementReceipt)) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			return result;
		}

		const bool ambiguous = input.arbitration.ownerIdentityAmbiguous ||
			input.eligibleHandCandidates.exactCount > 1;
		if (ambiguous) {
			result.status = OwnerGrantStatus::kDarkAmbiguous;
		} else if (input.arbitration.exactHandEligible) {
			if (input.eligibleHandCandidates.exactCount != 1 ||
				!IsValidHandMirrorOwnerIdentity(
					input.arbitration.candidateHandOwner)) {
				result.status = OwnerGrantStatus::kInvalidEvidence;
				return result;
			}
		} else if (input.eligibleHandCandidates.exactCount != 0) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			return result;
		}

		if (!ambiguous) {
			switch (SelectMirrorOwner(input.arbitration)) {
			case MirrorOwnerSelection::kWall:
				result.owner = MakeWallOwner(input.exactWallCandidate);
				result.status = OwnerGrantStatus::kGrantedWall;
				break;
			case MirrorOwnerSelection::kHand:
				result.owner = MakeHandOwner(
					input.arbitration.candidateHandOwner);
				result.status = OwnerGrantStatus::kGrantedHand;
				break;
			case MirrorOwnerSelection::kDark:
			default:
				result.status = OwnerGrantStatus::kDarkNoEligibleOwner;
				break;
			}
		}

		const bool selectingDark = !IsValidOwner(result.owner);
		if (!selectingDark && input.nextOwnerLeaseSequence == 0) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			result.owner = {};
			return result;
		}

		if (hasPreviousPublication) {
			result.previousPublicationInvalidationRequired = selectingDark ||
				result.owner.kind == OwnerKind::kHand ||
				!SameOwner(input.previousPublication.owner, result.owner);
		}
		if (result.previousPublicationInvalidationRequired) {
			if (IsEmptyRetirementReceipt(input.retirementReceipt)) {
				result.status =
					OwnerGrantStatus::kBlockedUntilPreviousPublicationInvalidated;
				return result;
			}
			if (!MatchesExactRetirementReceipt(
					input.previousPublication, result.owner,
					selectingDark ? 0 : input.nextOwnerLeaseSequence,
					input.retirementReceipt)) {
				result.status = OwnerGrantStatus::kInvalidEvidence;
				return result;
			}
		} else if (!IsEmptyRetirementReceipt(input.retirementReceipt)) {
			result.status = OwnerGrantStatus::kInvalidEvidence;
			return result;
		}

		if (!selectingDark) {
			result.ownerLeaseSequence = input.nextOwnerLeaseSequence;
			result.mayBeginCapture = true;
		}
		return result;
	}

	struct MovingSurfaceSample
	{
		HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity owner{};
		HandMirrorContentRuntimePolicy::PoseObservation pose{};
		std::uint64_t sourceSequence{ 0 };
	};

	[[nodiscard]] constexpr bool SameFloatBits(
		const float left,
		const float right) noexcept
	{
		return std::bit_cast<std::uint32_t>(left) ==
		       std::bit_cast<std::uint32_t>(right);
	}

	[[nodiscard]] constexpr bool SameFloat3Bits(
		const HandMirrorContentRuntimePolicy::Float3 left,
		const HandMirrorContentRuntimePolicy::Float3 right) noexcept
	{
		return SameFloatBits(left.x, right.x) &&
		       SameFloatBits(left.y, right.y) &&
		       SameFloatBits(left.z, right.z);
	}

	[[nodiscard]] constexpr bool SameGeometryBits(
		const HandMirrorContentRuntimePolicy::PanePoseGeometry& left,
		const HandMirrorContentRuntimePolicy::PanePoseGeometry& right) noexcept
	{
		return SameFloat3Bits(left.center, right.center) &&
		       SameFloat3Bits(left.normal, right.normal) &&
		       SameFloat3Bits(left.tangent, right.tangent) &&
		       SameFloat3Bits(left.bitangent, right.bitangent) &&
		       SameFloatBits(left.halfWidth, right.halfWidth) &&
		       SameFloatBits(left.halfHeight, right.halfHeight) &&
		       SameFloatBits(left.frontClearance, right.frontClearance) &&
		       SameFloatBits(left.backClearance, right.backClearance);
	}

	[[nodiscard]] constexpr bool IsValidMovingSurfaceSample(
		const MovingSurfaceSample& sample) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		return sample.sourceSequence != 0 &&
		       IsValidHandMirrorOwnerIdentity(sample.owner) &&
		       IsValidPoseObservation(sample.pose) &&
		       sample.pose.stableGeneration == sample.owner.stableGeneration &&
		       sample.pose.observedPaneSubtree == sample.owner.visiblePaneSubtree &&
		       sample.pose.authoredSurface == sample.owner.authoredSurface;
	}

	[[nodiscard]] constexpr bool SameMovingSurface(
		const MovingSurfaceSample& left,
		const MovingSurfaceSample& right) noexcept
	{
		return IsValidMovingSurfaceSample(left) &&
		       IsValidMovingSurfaceSample(right) &&
		       left.owner == right.owner &&
		       left.sourceSequence == right.sourceSequence &&
		       left.pose.stableGeneration == right.pose.stableGeneration &&
		       left.pose.poseSequence == right.pose.poseSequence &&
		       left.pose.mainViewFrame == right.pose.mainViewFrame &&
		       left.pose.observedPaneSubtree == right.pose.observedPaneSubtree &&
		       left.pose.authoredSurface == right.pose.authoredSurface &&
		       SameGeometryBits(left.pose.geometry, right.pose.geometry);
	}

	struct CaptureAttemptIdentity
	{
		MirrorOwnerIdentity owner{};
		std::uint64_t ownerLeaseSequence{ 0 };
		std::uint64_t attemptSequence{ 0 };
		MovingSurfaceSample surface{};
		PrivateTargetIdentity privateTarget{};
	};

	[[nodiscard]] constexpr bool IsValidHandCaptureAttempt(
		const CaptureAttemptIdentity& attempt) noexcept
	{
		return attempt.owner.kind == OwnerKind::kHand &&
		       IsValidOwner(attempt.owner) && attempt.ownerLeaseSequence != 0 &&
		       attempt.attemptSequence != 0 &&
		       IsValidMovingSurfaceSample(attempt.surface) &&
		       attempt.owner.hand == attempt.surface.owner &&
		       IsValidPrivateTarget(attempt.privateTarget);
	}

	[[nodiscard]] constexpr bool SameCaptureAttempt(
		const CaptureAttemptIdentity& left,
		const CaptureAttemptIdentity& right) noexcept
	{
		return IsValidHandCaptureAttempt(left) &&
		       IsValidHandCaptureAttempt(right) &&
		       SameOwner(left.owner, right.owner) &&
		       left.ownerLeaseSequence == right.ownerLeaseSequence &&
		       left.attemptSequence == right.attemptSequence &&
		       left.privateTarget == right.privateTarget &&
		       SameMovingSurface(left.surface, right.surface);
	}

	[[nodiscard]] constexpr bool SameCaptureEnvelope(
		const CaptureAttemptIdentity& left,
		const CaptureAttemptIdentity& right) noexcept
	{
		return IsValidHandCaptureAttempt(left) &&
		       IsValidHandCaptureAttempt(right) &&
		       SameOwner(left.owner, right.owner) &&
		       left.ownerLeaseSequence == right.ownerLeaseSequence &&
		       left.attemptSequence == right.attemptSequence &&
		       left.privateTarget == right.privateTarget;
	}

	enum class PublicationAudience : std::uint8_t
	{
		kUnknown,
		kInternalHandOnly,
		kPlanarAPIV1,
		// Mirror-only modules use this equivalent internal name without acquiring
		// or advertising any external delivery channel.
		kWallPane = kPlanarAPIV1,
		// Alias the existing wall/API-v1 value with the complete internal
		// audience name used by the shared pane renderer.  Keeping the numeric
		// value preserves every existing offline receipt while making it explicit
		// that only a tagged wall owner may use this audience for pane delivery.
		kWallPaneAndPlanarAPIV1 = kPlanarAPIV1
	};

	enum class PublicationConsumer : std::uint8_t
	{
		kWallPane,
		kInternalHandPane,
		kPlanarAPIV1
	};

	/**
	 * Closed owner/audience filter shared by future internal publication adapters
	 * and the renderer-neutral pane payload.  The public C ABI can consume only a
	 * canonical wall owner; an internal hand publication can reach only the hand
	 * pane consumer.  No runtime gate is consulted or changed here.
	 */
	[[nodiscard]] constexpr bool AllowsPublicationConsumer(
		const MirrorOwnerIdentity& owner,
		const PublicationAudience audience,
		const PublicationConsumer consumer) noexcept
	{
		if (!IsValidOwner(owner))
			return false;
		if (owner.kind == OwnerKind::kWall) {
			if (audience != PublicationAudience::kWallPaneAndPlanarAPIV1)
				return false;
			return consumer == PublicationConsumer::kWallPane ||
			       consumer == PublicationConsumer::kPlanarAPIV1;
		}
		return owner.kind == OwnerKind::kHand &&
		       audience == PublicationAudience::kInternalHandOnly &&
		       consumer == PublicationConsumer::kInternalHandPane;
	}

	struct PublicationIdentity
	{
		CaptureAttemptIdentity attempt{};
		std::uint64_t captureSequence{ 0 };
		std::uint64_t nativePublicationToken{ 0 };
		PublicationAudience audience{ PublicationAudience::kUnknown };
	};

	[[nodiscard]] constexpr bool IsValidPublication(
		const PublicationIdentity& publication) noexcept
	{
		return publication.captureSequence != 0 &&
		       publication.nativePublicationToken != 0 &&
		       publication.audience != PublicationAudience::kUnknown &&
		       IsValidHandCaptureAttempt(publication.attempt);
	}

	[[nodiscard]] constexpr bool SamePublication(
		const PublicationIdentity& left,
		const PublicationIdentity& right) noexcept
	{
		return IsValidPublication(left) && IsValidPublication(right) &&
		       left.captureSequence == right.captureSequence &&
		       left.nativePublicationToken == right.nativePublicationToken &&
		       left.audience == right.audience &&
		       SameCaptureAttempt(left.attempt, right.attempt);
	}

	enum class SnapshotPhase : std::uint8_t
	{
		kUnknown,
		kRaised,
		kPostCaptureReturn,
		kVisibleDraw
	};

	struct PhaseSnapshotIdentity
	{
		std::uint64_t snapshotToken{ 0 };
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t poseSequence{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		SnapshotPhase phase{ SnapshotPhase::kUnknown };

		constexpr bool operator==(const PhaseSnapshotIdentity&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool SnapshotMatchesSurface(
		const PhaseSnapshotIdentity& snapshot,
		const SnapshotPhase expectedPhase,
		const MovingSurfaceSample& surface) noexcept
	{
		return snapshot.snapshotToken != 0 &&
		       snapshot.sourceSequence == surface.sourceSequence &&
		       snapshot.poseSequence == surface.pose.poseSequence &&
		       snapshot.mainViewFrame == surface.pose.mainViewFrame &&
		       snapshot.phase == expectedPhase;
	}

	struct SuppressionLeaseIdentity
	{
		MirrorOwnerIdentity owner{};
		std::uint64_t ownerLeaseSequence{ 0 };
		std::uint64_t attemptSequence{ 0 };
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t nativeMutationToken{ 0 };

		constexpr bool operator==(const SuppressionLeaseIdentity&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsValidSuppressionLease(
		const SuppressionLeaseIdentity& lease) noexcept
	{
		return IsValidOwner(lease.owner) && lease.ownerLeaseSequence != 0 &&
		       lease.attemptSequence != 0 && lease.sourceSequence != 0 &&
		       lease.nativeMutationToken != 0;
	}

	struct SuppressionAttemptBinding
	{
		SuppressionLeaseIdentity acquiredLease{};
		SuppressionLeaseIdentity cleanupLease{};
		HandMirrorContentRuntimePolicy::StableCandidateGeneration stableGeneration{};
		std::uintptr_t exactItemSubtree{ 0 };
		std::uintptr_t exactCapturePartClone{ 0 };
		std::uintptr_t exactCapturePaneSubtree{ 0 };
		bool attemptReservedBeforeSuppression{ false };
		bool suppressionObservedBeforePrivateDraw{ false };
		bool suppressionRemainedObservedThroughPrivateReturn{ false };
		bool exactAttemptReturnedNormally{ false };
		bool mutationOwnershipClaimed{ false };
		bool cleanupAndReadbackCompletedBeforePublication{ false };
		bool publicationSkippedOnAnyCaptureOrCleanupFault{ false };
	};

	[[nodiscard]] constexpr bool ValidateSuppressionBinding(
		const CaptureAttemptIdentity& attempt,
		const HandMirrorContentRuntimePolicy::EquippedCandidateEvidence& candidate,
		const HandMirrorContentRuntimePolicy::PrivateMirrorItemSuppressionEvidence&
			suppression,
		const SuppressionAttemptBinding& binding) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		return IsValidHandCaptureAttempt(attempt) &&
		       ValidateEquippedCandidate(candidate) == RejectionReason::kNone &&
		       ValidatePrivateMirrorItemSuppression(candidate, suppression) ==
			       RejectionReason::kNone &&
		       candidate.ownerIdentity == attempt.owner.hand &&
		       IsValidSuppressionLease(binding.acquiredLease) &&
		       binding.cleanupLease == binding.acquiredLease &&
		       SameOwner(binding.acquiredLease.owner, attempt.owner) &&
		       binding.acquiredLease.ownerLeaseSequence ==
			       attempt.ownerLeaseSequence &&
		       binding.acquiredLease.attemptSequence ==
			       attempt.attemptSequence &&
		       binding.acquiredLease.sourceSequence ==
			       attempt.surface.sourceSequence &&
		       binding.acquiredLease.nativeMutationToken ==
			       suppression.mutationLeaseToken &&
		       binding.cleanupLease.nativeMutationToken ==
			       suppression.restoreLeaseToken &&
		       binding.stableGeneration ==
			       attempt.surface.pose.stableGeneration &&
		       binding.exactItemSubtree ==
			       candidate.privateCaptureBipedObject.mirrorItemSubtree &&
		       binding.exactItemSubtree ==
			       suppression.requestedSuppressionSubtree &&
		       binding.exactCapturePartClone ==
			       candidate.privateCaptureBipedObject.partClone &&
		       binding.exactCapturePaneSubtree ==
			       candidate.privateCaptureBipedObject.paneSubtree &&
		       binding.attemptReservedBeforeSuppression &&
		       binding.suppressionObservedBeforePrivateDraw &&
		       binding.suppressionRemainedObservedThroughPrivateReturn &&
		       binding.exactAttemptReturnedNormally &&
		       binding.mutationOwnershipClaimed == !suppression.priorAppCulled &&
		       binding.cleanupAndReadbackCompletedBeforePublication &&
		       binding.publicationSkippedOnAnyCaptureOrCleanupFault;
	}

	struct PlayerSelfInclusionAttemptBinding
	{
		MirrorOwnerIdentity owner{};
		std::uint64_t ownerLeaseSequence{ 0 };
		std::uint64_t attemptSequence{ 0 };
		std::uint64_t sourceSequence{ 0 };
		HandMirrorContentRuntimePolicy::StableCandidateGeneration stableGeneration{};
		PrivateTargetIdentity privateTarget{};
		std::uintptr_t exactThirdPersonPlayerRoot{ 0 };
		std::uintptr_t exactCapturePartClone{ 0 };
		std::uint64_t playerDrawEvidenceToken{ 0 };
		bool exactPrivateTargetBoundBeforePlayerDraw{ false };
		bool playerDrawReturnedNormallyBeforeCaptureReturn{ false };
		bool targetIdentityRevalidatedAfterPlayerDraw{ false };

		constexpr bool operator==(
			const PlayerSelfInclusionAttemptBinding&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool ValidatePlayerSelfInclusionBinding(
		const CaptureAttemptIdentity& attempt,
		const HandMirrorContentRuntimePolicy::EquippedCandidateEvidence& candidate,
		const HandMirrorContentRuntimePolicy::PlayerSelfInclusionEvidence& evidence,
		const PlayerSelfInclusionAttemptBinding& binding) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		return IsValidHandCaptureAttempt(attempt) &&
		       ValidatePlayerSelfInclusion(candidate, evidence) ==
			       RejectionReason::kNone &&
		       candidate.ownerIdentity == attempt.owner.hand &&
		       SameOwner(binding.owner, attempt.owner) &&
		       binding.ownerLeaseSequence == attempt.ownerLeaseSequence &&
		       binding.attemptSequence == attempt.attemptSequence &&
		       binding.sourceSequence == attempt.surface.sourceSequence &&
		       binding.stableGeneration ==
			       attempt.surface.pose.stableGeneration &&
		       binding.privateTarget == attempt.privateTarget &&
		       binding.exactThirdPersonPlayerRoot ==
			       candidate.thirdPersonPlayerRoot &&
		       binding.exactThirdPersonPlayerRoot ==
			       evidence.submittedPrivatePlayerRoot &&
		       binding.exactCapturePartClone ==
			       candidate.privateCaptureBipedObject.partClone &&
		       binding.playerDrawEvidenceToken != 0 &&
		       binding.exactPrivateTargetBoundBeforePlayerDraw &&
		       binding.playerDrawReturnedNormallyBeforeCaptureReturn &&
		       binding.targetIdentityRevalidatedAfterPlayerDraw;
	}

	struct BridgeTokenIssuanceChain
	{
		TokenIssuerState initialState{};
		TokenIssueReceipt retirementIssue{};
		TokenIssueReceipt ownerLeaseIssue{};
		TokenIssueReceipt attemptIssue{};
		TokenIssueReceipt raisedSnapshotIssue{};
		TokenIssueReceipt suppressionLeaseIssue{};
		TokenIssueReceipt playerDrawIssue{};
		TokenIssueReceipt postReturnSnapshotIssue{};
		TokenIssueReceipt publicationIssue{};
		TokenIssueReceipt visibleDrawSnapshotIssue{};
		TokenIssuerState finalState{};
	};

	[[nodiscard]] constexpr bool ConsumeChainedIssue(
		std::uint64_t& cursor,
		bool& exhausted,
		const TokenIssueReceipt receipt) noexcept
	{
		if (exhausted || !IsValidTokenIssueReceipt(receipt) ||
			receipt.predecessorToken != cursor) {
			return false;
		}
		cursor = receipt.issuedToken;
		exhausted = receipt.exhaustedAfterIssue;
		return true;
	}

	[[nodiscard]] constexpr bool ValidateBridgeTokenChain(
		const BridgeTokenIssuanceChain& chain,
		const bool retirementRequired) noexcept
	{
		if (!IsValidTokenIssuerState(chain.initialState) ||
			chain.initialState.exhausted) {
			return false;
		}
		auto cursor = chain.initialState.lastIssuedToken;
		auto exhausted = chain.initialState.exhausted;
		if (retirementRequired) {
			if (!ConsumeChainedIssue(
					cursor, exhausted, chain.retirementIssue)) {
				return false;
			}
		} else if (chain.retirementIssue != TokenIssueReceipt{}) {
			return false;
		}

		return ConsumeChainedIssue(cursor, exhausted, chain.ownerLeaseIssue) &&
		       ConsumeChainedIssue(cursor, exhausted, chain.attemptIssue) &&
		       ConsumeChainedIssue(
			       cursor, exhausted, chain.raisedSnapshotIssue) &&
		       ConsumeChainedIssue(
			       cursor, exhausted, chain.suppressionLeaseIssue) &&
		       ConsumeChainedIssue(cursor, exhausted, chain.playerDrawIssue) &&
		       ConsumeChainedIssue(
			       cursor, exhausted, chain.postReturnSnapshotIssue) &&
		       ConsumeChainedIssue(cursor, exhausted, chain.publicationIssue) &&
		       ConsumeChainedIssue(
			       cursor, exhausted, chain.visibleDrawSnapshotIssue) &&
		       chain.finalState == TokenIssuerState{ cursor, exhausted };
	}

	enum class BridgeRejection : std::uint8_t
	{
		kNone,
		kOwnerGrant,
		kTokenIssuance,
		kCaptureIdentity,
		kRaisedPose,
		kSnapshotIdentity,
		kMovingSurfaceChanged,
		kSuppressionBinding,
		kPlayerSelfInclusion,
		kPrivateCaptureOrdering,
		kPublicationIdentity,
		kVisibleDelivery,
		kPublicAPIExposure
	};

	struct HandBridgeEvidence
	{
		OwnerGrantInput ownerGrantInput{};
		OwnerGrant ownerGrant{};
		BridgeTokenIssuanceChain tokenChain{};
		CaptureAttemptIdentity reservedAttempt{};
		CaptureAttemptIdentity preCaptureAttempt{};
		CaptureAttemptIdentity postCaptureAttempt{};
		HandMirrorContentRuntimePolicy::EquippedCandidateEvidence candidate{};
		HandMirrorContentRuntimePolicy::RaisedPoseEvidence raisedPose{};
		PhaseSnapshotIdentity raisedSnapshot{};
		PhaseSnapshotIdentity postCaptureReturnSnapshot{};
		HandMirrorContentRuntimePolicy::PrivateMirrorItemSuppressionEvidence
			suppression{};
		SuppressionAttemptBinding suppressionBinding{};
		HandMirrorContentRuntimePolicy::PlayerSelfInclusionEvidence
			playerSelfInclusion{};
		PlayerSelfInclusionAttemptBinding playerSelfInclusionBinding{};
		PublicationIdentity publication{};
		PublicationIdentity visibleDelivery{};
		HandMirrorContentRuntimePolicy::PostPublicationPaneDrawEvidence
			visiblePaneDraw{};
		PhaseSnapshotIdentity visibleDrawSnapshot{};
		HandMirrorContentRuntimePolicy::DeliveryOpportunity deliveryOpportunity{
			HandMirrorContentRuntimePolicy::DeliveryOpportunity::kUnknown };
		bool exactTimingOpportunityObserved{ false };
		bool onePrivateRenderBudgetReservedAndConsumed{ false };
		bool privateCaptureReturnedNormally{ false };
		bool candidateAndPoseReResolvedAfterPrivateReturn{ false };
		bool allPrivateStateRestoredBeforePublication{ false };
		bool privateTargetUnboundBeforePublication{ false };
		bool publicationRetainedExactAttemptResources{ false };
		bool publicationSnapshotCarriesExactBridgeIdentity{ false };
		bool previousPublicationResourceNotSampledByHand{ false };
		bool publicationReadyBeforeVisiblePaneDraw{ false };
		bool exactPaneSubmittedByCurrentMainPlayerView{ false };
		bool exactMainViewFrameCurrentAtDraw{ false };
		bool noWallOrOtherHandAttemptActive{ false };
		bool darkFallbackOnAnyMismatch{ false };
		bool publicAPIV1PublicationExcluded{ false };
	};

	[[nodiscard]] constexpr bool IsConsistentDeliveryOpportunity(
		const HandMirrorContentRuntimePolicy::Perspective perspective,
		const HandMirrorContentRuntimePolicy::DeliveryOpportunity opportunity) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		if (perspective == Perspective::kFirstPerson) {
			return opportunity == DeliveryOpportunity::kPostWorldBeforeFirstPerson;
		}
		if (perspective == Perspective::kThirdPerson) {
			return opportunity == DeliveryOpportunity::kPreWorldBeforeThirdPerson ||
			       opportunity == DeliveryOpportunity::kPostCaptureThirdPersonOverlay;
		}
		return false;
	}

	/**
	 * A successful result is a structural contract, not runtime authorization.
	 * Every phase must carry one exact owner lease, attempt, pose, source frame,
	 * and private-target role.  Any bit-level pose drift makes the hand surface
	 * dark; unlike a wall mirror, it can never fall back to a continuity frame.
	 */
	[[nodiscard]] constexpr BridgeRejection ValidateHandBridge(
		const HandBridgeEvidence& evidence) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		if (evidence.ownerGrant != DecideOwnerGrant(evidence.ownerGrantInput) ||
			evidence.ownerGrant.status != OwnerGrantStatus::kGrantedHand ||
			!evidence.ownerGrant.mayBeginCapture ||
			evidence.ownerGrant.owner.kind != OwnerKind::kHand ||
			evidence.ownerGrant.ownerLeaseSequence == 0) {
			return BridgeRejection::kOwnerGrant;
		}
		const bool retirementRequired =
			evidence.ownerGrant.previousPublicationInvalidationRequired;
		if (!ValidateBridgeTokenChain(
				evidence.tokenChain, retirementRequired) ||
			(retirementRequired &&
			 evidence.tokenChain.retirementIssue.issuedToken !=
				 evidence.ownerGrantInput.retirementReceipt.retirementToken) ||
			evidence.tokenChain.ownerLeaseIssue.issuedToken !=
				evidence.ownerGrant.ownerLeaseSequence ||
			evidence.tokenChain.attemptIssue.issuedToken !=
				evidence.reservedAttempt.attemptSequence ||
			evidence.tokenChain.raisedSnapshotIssue.issuedToken !=
				evidence.raisedSnapshot.snapshotToken ||
			evidence.tokenChain.suppressionLeaseIssue.issuedToken !=
				evidence.suppressionBinding.acquiredLease.nativeMutationToken ||
			evidence.tokenChain.playerDrawIssue.issuedToken !=
				evidence.playerSelfInclusionBinding.playerDrawEvidenceToken ||
			evidence.tokenChain.postReturnSnapshotIssue.issuedToken !=
				evidence.postCaptureReturnSnapshot.snapshotToken ||
			evidence.tokenChain.publicationIssue.issuedToken !=
				evidence.publication.nativePublicationToken ||
			evidence.tokenChain.visibleDrawSnapshotIssue.issuedToken !=
				evidence.visibleDrawSnapshot.snapshotToken) {
			return BridgeRejection::kTokenIssuance;
		}
		if (!IsValidHandCaptureAttempt(evidence.reservedAttempt) ||
			!SameOwner(
				evidence.ownerGrant.owner, evidence.reservedAttempt.owner) ||
			evidence.ownerGrant.ownerLeaseSequence !=
				evidence.reservedAttempt.ownerLeaseSequence ||
			ValidateEquippedCandidate(evidence.candidate) !=
				RejectionReason::kNone ||
			evidence.candidate.ownerIdentity !=
				evidence.reservedAttempt.owner.hand) {
			return BridgeRejection::kCaptureIdentity;
		}
		if (retirementRequired &&
			!ArePrivateTargetsPhysicallyDisjoint(
				evidence.ownerGrantInput.previousPublication.privateTarget,
				evidence.reservedAttempt.privateTarget)) {
			return BridgeRejection::kCaptureIdentity;
		}
		const MovingSurfaceSample raisedSurface{
			evidence.reservedAttempt.owner.hand,
			evidence.raisedPose.pose,
			evidence.reservedAttempt.surface.sourceSequence
		};
		if (ValidateRaisedPose(evidence.candidate, evidence.raisedPose) !=
				RejectionReason::kNone ||
			!SameMovingSurface(
				evidence.reservedAttempt.surface, raisedSurface)) {
			return BridgeRejection::kRaisedPose;
		}
		if (!SameCaptureEnvelope(
				evidence.reservedAttempt, evidence.preCaptureAttempt) ||
			!SameCaptureEnvelope(
				evidence.reservedAttempt, evidence.postCaptureAttempt) ||
			!SameCaptureEnvelope(
				evidence.reservedAttempt, evidence.publication.attempt) ||
			!SameCaptureEnvelope(
				evidence.reservedAttempt, evidence.visibleDelivery.attempt)) {
			return BridgeRejection::kCaptureIdentity;
		}
		const MovingSurfaceSample visibleDrawSurface{
			evidence.visibleDelivery.attempt.owner.hand,
			evidence.visiblePaneDraw.pose,
			evidence.visibleDelivery.attempt.surface.sourceSequence
		};
		if (!SnapshotMatchesSurface(
				evidence.raisedSnapshot, SnapshotPhase::kRaised,
				raisedSurface) ||
			!SnapshotMatchesSurface(
				evidence.postCaptureReturnSnapshot,
				SnapshotPhase::kPostCaptureReturn,
				evidence.postCaptureAttempt.surface) ||
			!SnapshotMatchesSurface(
				evidence.visibleDrawSnapshot, SnapshotPhase::kVisibleDraw,
				visibleDrawSurface) ||
			evidence.raisedSnapshot.snapshotToken ==
				evidence.postCaptureReturnSnapshot.snapshotToken ||
			evidence.raisedSnapshot.snapshotToken ==
				evidence.visibleDrawSnapshot.snapshotToken ||
			evidence.postCaptureReturnSnapshot.snapshotToken ==
				evidence.visibleDrawSnapshot.snapshotToken) {
			return BridgeRejection::kSnapshotIdentity;
		}
		if (!SameMovingSurface(
				evidence.reservedAttempt.surface,
				evidence.preCaptureAttempt.surface) ||
			!SameMovingSurface(
				evidence.reservedAttempt.surface,
				evidence.postCaptureAttempt.surface) ||
			!SameMovingSurface(
				evidence.reservedAttempt.surface,
				evidence.publication.attempt.surface) ||
			!SameMovingSurface(
				evidence.reservedAttempt.surface,
				evidence.visibleDelivery.attempt.surface)) {
			return BridgeRejection::kMovingSurfaceChanged;
		}
		if (!ValidateSuppressionBinding(
				evidence.reservedAttempt, evidence.candidate,
				evidence.suppression, evidence.suppressionBinding)) {
			return BridgeRejection::kSuppressionBinding;
		}
		if (!ValidatePlayerSelfInclusionBinding(
				evidence.reservedAttempt, evidence.candidate,
				evidence.playerSelfInclusion,
				evidence.playerSelfInclusionBinding)) {
			return BridgeRejection::kPlayerSelfInclusion;
		}
		if (!evidence.exactTimingOpportunityObserved ||
			!evidence.onePrivateRenderBudgetReservedAndConsumed ||
			!evidence.privateCaptureReturnedNormally ||
			!evidence.candidateAndPoseReResolvedAfterPrivateReturn ||
			!evidence.allPrivateStateRestoredBeforePublication) {
			return BridgeRejection::kPrivateCaptureOrdering;
		}
		if (!IsValidPublication(evidence.publication) ||
			!SamePublication(evidence.publication, evidence.visibleDelivery) ||
			!evidence.privateTargetUnboundBeforePublication ||
			!evidence.publicationRetainedExactAttemptResources ||
			!evidence.publicationSnapshotCarriesExactBridgeIdentity ||
			!evidence.previousPublicationResourceNotSampledByHand) {
			return BridgeRejection::kPublicationIdentity;
		}
		if (!IsConsistentDeliveryOpportunity(
				evidence.candidate.perspective,
				evidence.deliveryOpportunity) ||
			ValidatePostPublicationPaneDraw(
				evidence.candidate, evidence.raisedPose,
				evidence.visiblePaneDraw) != RejectionReason::kNone ||
			!SameMovingSurface(
				evidence.visibleDelivery.attempt.surface,
				visibleDrawSurface) ||
			!evidence.publicationReadyBeforeVisiblePaneDraw ||
			!evidence.exactPaneSubmittedByCurrentMainPlayerView ||
			!evidence.exactMainViewFrameCurrentAtDraw ||
			!evidence.noWallOrOtherHandAttemptActive ||
			!evidence.darkFallbackOnAnyMismatch) {
			return BridgeRejection::kVisibleDelivery;
		}
		if (evidence.publication.audience !=
				PublicationAudience::kInternalHandOnly ||
			evidence.visibleDelivery.audience !=
				PublicationAudience::kInternalHandOnly ||
			!evidence.publicAPIV1PublicationExcluded ||
			kPlanarAPIV1HandPublicationAllowed) {
			return BridgeRejection::kPublicAPIExposure;
		}
		return BridgeRejection::kNone;
	}

	[[nodiscard]] constexpr bool CanEnableRuntimeBridge(
		const HandBridgeEvidence& evidence) noexcept
	{
		using namespace HandMirrorContentRuntimePolicy;
		const bool perspectiveRouteAccepted =
			evidence.candidate.perspective == Perspective::kFirstPerson ?
				kFirstPersonDeliveryRouteAccepted :
				(evidence.candidate.perspective == Perspective::kThirdPerson &&
				 kThirdPersonDeliveryRouteAccepted);
		return ValidateHandBridge(evidence) == BridgeRejection::kNone &&
		       perspectiveRouteAccepted && kSingletonOwnerGrantWired &&
		       kCapturePublicationIdentityWired &&
		       kSameFrameMovingPlaneWired &&
		       kExactItemSubtreeSuppressionWired && kRuntimeBridgeWired &&
		       kRuntimeAcceptanceGranted &&
		       !kRawObserverEvidenceAuthorizesBridge &&
		       !kPlanarAPIV1HandPublicationAllowed &&
		       kContentPluginEmitted &&
		       HandMirrorContentRuntimePolicy::kRuntimeIntegrationWired &&
		       HandMirrorContentRuntimePolicy::kRuntimeAcceptanceGranted;
	}
}
