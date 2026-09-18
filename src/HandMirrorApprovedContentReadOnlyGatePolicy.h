#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace HandMirrorApprovedContentReadOnlyGatePolicy
{
	// The separately default-off native observer now feeds this policy through
	// existing shared hook owners.  This header still owns no native adapter,
	// hook, scene pointer, render target, capture, publication, or delivery path;
	// runtime wiring grants no render or product authority.
	inline constexpr bool kDefaultEnabled = false;
	inline constexpr bool kApprovedContentObserverRuntimeWired = true;
	inline constexpr bool kApprovedContentAuthorityGranted = false;
	inline constexpr bool kRenderAuthorityGranted = false;
	inline constexpr bool kCaptureAuthorized = false;
	inline constexpr bool kPublicationAuthorized = false;
	inline constexpr bool kDeliveryAuthorized = false;
	inline constexpr bool kPlanarAPIV1ExposureAuthorized = false;
	inline constexpr bool kRuntimeAcceptanceGranted = false;
	inline constexpr bool kPolicyRetainsEnginePointers = false;

	inline constexpr std::uint32_t kShieldSlotNumber = 39;
	inline constexpr std::size_t kPoseWordCount = 13;
	inline constexpr std::size_t kMaximumExactCandidates = 8;
	inline constexpr std::size_t kTelemetryCapacity = 64;

	// All identity-shaped integers below are adapter-minted value tokens.  The
	// policy never treats them as addresses and supplies no dereference operation.
	using IdentityToken = std::uint64_t;

	[[nodiscard]] constexpr bool IsFiniteFloatBits(
		const std::uint32_t bits) noexcept
	{
		return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
	}

	[[nodiscard]] constexpr bool IsFinite(const float value) noexcept
	{
		return IsFiniteFloatBits(std::bit_cast<std::uint32_t>(value));
	}

	struct ExactFormIdentity
	{
		IdentityToken pluginIdentity{ 0 };
		std::uint32_t localFormID{ 0 };
		std::uint32_t runtimeFormID{ 0 };
		IdentityToken loadedRecordIdentity{ 0 };

		constexpr bool operator==(const ExactFormIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidExactFormIdentity(
		const ExactFormIdentity& identity) noexcept
	{
		return identity.pluginIdentity != 0 && identity.localFormID != 0 &&
			identity.runtimeFormID != 0 && identity.loadedRecordIdentity != 0;
	}

	struct AssetIdentity
	{
		IdentityToken approvalIdentity{ 0 };
		std::uint64_t byteLength{ 0 };
		std::array<std::uint64_t, 4> sha256Words{};

		constexpr bool operator==(const AssetIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidAssetIdentity(
		const AssetIdentity& identity) noexcept
	{
		if (identity.approvalIdentity == 0 || identity.byteLength == 0)
			return false;
		for (const auto word : identity.sha256Words) {
			if (word != 0)
				return true;
		}
		return false;
	}

	struct ApertureIdentity
	{
		IdentityToken contractIdentity{ 0 };
		std::uint32_t halfWidthBits{ 0 };
		std::uint32_t halfHeightBits{ 0 };
		std::uint32_t frontClearanceBits{ 0 };
		std::uint32_t backClearanceBits{ 0 };

		constexpr bool operator==(const ApertureIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidApertureIdentity(
		const ApertureIdentity& identity) noexcept
	{
		if (identity.contractIdentity == 0 ||
			!IsFiniteFloatBits(identity.halfWidthBits) ||
			!IsFiniteFloatBits(identity.halfHeightBits) ||
			!IsFiniteFloatBits(identity.frontClearanceBits) ||
			!IsFiniteFloatBits(identity.backClearanceBits)) {
			return false;
		}
		const auto halfWidth = std::bit_cast<float>(identity.halfWidthBits);
		const auto halfHeight = std::bit_cast<float>(identity.halfHeightBits);
		const auto front = std::bit_cast<float>(identity.frontClearanceBits);
		const auto back = std::bit_cast<float>(identity.backClearanceBits);
		return halfWidth > 0.0F && halfHeight > 0.0F && front >= 0.0F &&
			back >= 0.0F;
	}

	struct ApprovedContentIdentity
	{
		IdentityToken frozenContentLockIdentity{ 0 };
		IdentityToken styleIdentity{ 0 };
		ExactFormIdentity armor{};
		ExactFormIdentity armorAddon{};
		AssetIdentity firstPersonModel{};
		AssetIdentity thirdPersonModel{};
		IdentityToken mirrorItemSubtreeContractIdentity{ 0 };
		IdentityToken paneContractIdentity{ 0 };
		ApertureIdentity aperture{};

		constexpr bool operator==(
			const ApprovedContentIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidApprovedContentIdentity(
		const ApprovedContentIdentity& identity) noexcept
	{
		return identity.frozenContentLockIdentity != 0 &&
			identity.styleIdentity != 0 &&
			IsValidExactFormIdentity(identity.armor) &&
			IsValidExactFormIdentity(identity.armorAddon) &&
			identity.armor.pluginIdentity == identity.armorAddon.pluginIdentity &&
			identity.armor.localFormID != identity.armorAddon.localFormID &&
			identity.armor.runtimeFormID != identity.armorAddon.runtimeFormID &&
			identity.armor.loadedRecordIdentity !=
				identity.armorAddon.loadedRecordIdentity &&
			IsValidAssetIdentity(identity.firstPersonModel) &&
			IsValidAssetIdentity(identity.thirdPersonModel) &&
			identity.firstPersonModel != identity.thirdPersonModel &&
			identity.mirrorItemSubtreeContractIdentity != 0 &&
			identity.paneContractIdentity != 0 &&
			identity.mirrorItemSubtreeContractIdentity !=
				identity.paneContractIdentity &&
			IsValidApertureIdentity(identity.aperture);
	}

	enum class Perspective : std::uint8_t
	{
		kUnknown,
		kFirstPerson,
		kThirdPerson
	};

	struct LifecycleGenerations
	{
		std::uint64_t lifecycle{ 0 };
		std::uint64_t equip{ 0 };
		std::uint64_t pointOfView{ 0 };
		std::uint64_t firstPersonRoot{ 0 };
		std::uint64_t firstPersonClone{ 0 };
		std::uint64_t thirdPersonRoot{ 0 };
		std::uint64_t thirdPersonClone{ 0 };

		constexpr bool operator==(
			const LifecycleGenerations&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidLifecycleGenerations(
		const LifecycleGenerations& generations) noexcept
	{
		return generations.lifecycle != 0 && generations.equip != 0 &&
			generations.pointOfView != 0 &&
			generations.firstPersonRoot != 0 &&
			generations.firstPersonClone != 0 &&
			generations.thirdPersonRoot != 0 &&
			generations.thirdPersonClone != 0;
	}

	enum class ObserverFault : std::uint8_t
	{
		kNone,
		kMalformedState,
		kUnknownLifecycleEvent,
		kTokenExhausted,
		kCandidateOverflow,
		kTelemetryOverflow,
		kGuardedReadFault,
		kCallbackFault,
		kAbnormalNativeReturn,
		kEnginePointerRetention,
		kTargetAlias,
		kMalformedEvidence
	};

	struct LifecycleState
	{
		LifecycleGenerations generations{ 1, 1, 1, 1, 1, 1, 1 };
		Perspective perspective{ Perspective::kUnknown };
		bool exactApprovedArmorEquipped{ false };
		ObserverFault firstFault{ ObserverFault::kNone };
		bool terminalFailStop{ false };

		constexpr bool operator==(const LifecycleState&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidLifecycleState(
		const LifecycleState& state) noexcept
	{
		return IsValidLifecycleGenerations(state.generations) &&
			(state.terminalFailStop ==
				(state.firstFault != ObserverFault::kNone));
	}

	[[nodiscard]] constexpr LifecycleState LatchLifecycleFault(
		LifecycleState state,
		const ObserverFault fault) noexcept
	{
		if (fault == ObserverFault::kNone)
			return state;
		if (state.firstFault == ObserverFault::kNone)
			state.firstFault = fault;
		state.terminalFailStop = true;
		return state;
	}

	enum class LifecycleEvent : std::uint8_t
	{
		kEquipExactApprovedArmor,
		kUnequip,
		kPointOfViewFirstPerson,
		kPointOfViewThirdPerson,
		kFirstPersonRootRebuilt,
		kFirstPersonCloneReplaced,
		kThirdPersonRootRebuilt,
		kThirdPersonCloneReplaced,
		kGameLoadInvalidation,
		kUnknown
	};

	enum class LifecycleTransitionStatus : std::uint8_t
	{
		kAdvanced,
		kAlreadyFailStopped,
		kInvalidState,
		kUnknownEvent,
		kExhausted
	};

	struct LifecycleTransitionResult
	{
		LifecycleState next{};
		LifecycleTransitionStatus status{
			LifecycleTransitionStatus::kInvalidState };
	};

	[[nodiscard]] constexpr LifecycleTransitionResult ApplyLifecycleEvent(
		const LifecycleState& current,
		const LifecycleEvent event) noexcept
	{
		if (!IsValidLifecycleState(current)) {
			return { LatchLifecycleFault(
				current, ObserverFault::kMalformedState),
				LifecycleTransitionStatus::kInvalidState };
		}
		if (current.terminalFailStop)
			return { current, LifecycleTransitionStatus::kAlreadyFailStopped };
		if (event == LifecycleEvent::kUnknown) {
			return { LatchLifecycleFault(
				current, ObserverFault::kUnknownLifecycleEvent),
				LifecycleTransitionStatus::kUnknownEvent };
		}

		bool equip = false;
		bool pov = false;
		bool firstRoot = false;
		bool firstClone = false;
		bool thirdRoot = false;
		bool thirdClone = false;
		switch (event) {
		case LifecycleEvent::kEquipExactApprovedArmor:
		case LifecycleEvent::kUnequip: equip = true; break;
		case LifecycleEvent::kPointOfViewFirstPerson:
		case LifecycleEvent::kPointOfViewThirdPerson: pov = true; break;
		case LifecycleEvent::kFirstPersonRootRebuilt: firstRoot = true; break;
		case LifecycleEvent::kFirstPersonCloneReplaced: firstClone = true; break;
		case LifecycleEvent::kThirdPersonRootRebuilt: thirdRoot = true; break;
		case LifecycleEvent::kThirdPersonCloneReplaced: thirdClone = true; break;
		case LifecycleEvent::kGameLoadInvalidation:
			equip = pov = firstRoot = firstClone = thirdRoot = thirdClone = true;
			break;
		case LifecycleEvent::kUnknown: break;
		}

		const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
		const auto& generations = current.generations;
		if (generations.lifecycle == maximum ||
			(equip && generations.equip == maximum) ||
			(pov && generations.pointOfView == maximum) ||
			(firstRoot && generations.firstPersonRoot == maximum) ||
			(firstClone && generations.firstPersonClone == maximum) ||
			(thirdRoot && generations.thirdPersonRoot == maximum) ||
			(thirdClone && generations.thirdPersonClone == maximum)) {
			return { LatchLifecycleFault(
				current, ObserverFault::kTokenExhausted),
				LifecycleTransitionStatus::kExhausted };
		}

		auto next = current;
		++next.generations.lifecycle;
		next.generations.equip += equip ? 1u : 0u;
		next.generations.pointOfView += pov ? 1u : 0u;
		next.generations.firstPersonRoot += firstRoot ? 1u : 0u;
		next.generations.firstPersonClone += firstClone ? 1u : 0u;
		next.generations.thirdPersonRoot += thirdRoot ? 1u : 0u;
		next.generations.thirdPersonClone += thirdClone ? 1u : 0u;
		switch (event) {
		case LifecycleEvent::kEquipExactApprovedArmor:
			next.exactApprovedArmorEquipped = true;
			break;
		case LifecycleEvent::kUnequip:
			next.exactApprovedArmorEquipped = false;
			break;
		case LifecycleEvent::kPointOfViewFirstPerson:
			next.perspective = Perspective::kFirstPerson;
			break;
		case LifecycleEvent::kPointOfViewThirdPerson:
			next.perspective = Perspective::kThirdPerson;
			break;
		case LifecycleEvent::kGameLoadInvalidation:
			next.exactApprovedArmorEquipped = false;
			next.perspective = Perspective::kUnknown;
			break;
		default: break;
		}
		return { next, LifecycleTransitionStatus::kAdvanced };
	}

	struct TokenIssuerState
	{
		std::uint64_t lastIssued{ 0 };
		bool exhausted{ false };

		constexpr bool operator==(const TokenIssuerState&) const noexcept = default;
	};

	enum class TokenIssueStatus : std::uint8_t
	{
		kIssued,
		kExhausted,
		kInvalidState
	};

	struct TokenIssueResult
	{
		TokenIssueStatus status{ TokenIssueStatus::kInvalidState };
		std::uint64_t token{ 0 };
		TokenIssuerState next{};
	};

	[[nodiscard]] constexpr bool IsValidTokenIssuerState(
		const TokenIssuerState& state) noexcept
	{
		const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
		return state.exhausted ? state.lastIssued == maximum :
			state.lastIssued != maximum;
	}

	[[nodiscard]] constexpr TokenIssueResult IssueToken(
		const TokenIssuerState& current) noexcept
	{
		if (!IsValidTokenIssuerState(current))
			return {};
		if (current.exhausted)
			return { TokenIssueStatus::kExhausted, 0, current };
		const auto nextToken = current.lastIssued + 1u;
		const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
		return { TokenIssueStatus::kIssued, nextToken,
			{ nextToken, nextToken == maximum } };
	}

	struct CandidateCounter
	{
		std::size_t exactCount{ 0 };
		bool overflowed{ false };

		constexpr bool operator==(const CandidateCounter&) const noexcept = default;
	};

	[[nodiscard]] constexpr CandidateCounter ObserveExactCandidate(
		const CandidateCounter& current) noexcept
	{
		if (current.overflowed)
			return current;
		if (current.exactCount >= kMaximumExactCandidates)
			return { kMaximumExactCandidates, true };
		return { current.exactCount + 1u, false };
	}

	enum class CandidateCardinality : std::uint8_t
	{
		kZero,
		kExactlyOne,
		kAmbiguousMany,
		kOverflowFault
	};

	[[nodiscard]] constexpr CandidateCardinality ClassifyCandidates(
		const CandidateCounter& candidates) noexcept
	{
		if (candidates.overflowed ||
			candidates.exactCount > kMaximumExactCandidates) {
			return CandidateCardinality::kOverflowFault;
		}
		if (candidates.exactCount == 0)
			return CandidateCardinality::kZero;
		if (candidates.exactCount == 1)
			return CandidateCardinality::kExactlyOne;
		return CandidateCardinality::kAmbiguousMany;
	}

	struct CloneObservation
	{
		Perspective perspective{ Perspective::kUnknown };
		LifecycleGenerations generations{};
		ExactFormIdentity observedArmor{};
		ExactFormIdentity observedArmorAddon{};
		AssetIdentity observedModel{};
		IdentityToken perspectiveRootIdentity{ 0 };
		IdentityToken bipedIdentity{ 0 };
		IdentityToken partCloneIdentity{ 0 };
		IdentityToken mirrorItemSubtreeIdentity{ 0 };
		IdentityToken paneIdentity{ 0 };
		IdentityToken mirrorItemSubtreeContractIdentity{ 0 };
		IdentityToken paneContractIdentity{ 0 };
		ApertureIdentity aperture{};
		bool exactGameplayClone{ false };
		bool partCloneDescendsPerspectiveRoot{ false };
		bool mirrorItemDescendsPartClone{ false };
		bool paneDescendsMirrorItem{ false };
		bool noInventoryMenuShadowOrDisplayClone{ false };
		bool noEnginePointerRetained{ false };

		constexpr bool operator==(const CloneObservation&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidCloneObservation(
		const CloneObservation& clone,
		const ApprovedContentIdentity& approved,
		const Perspective expectedPerspective) noexcept
	{
		const auto& expectedModel = expectedPerspective == Perspective::kFirstPerson ?
			approved.firstPersonModel : approved.thirdPersonModel;
		return IsValidApprovedContentIdentity(approved) &&
			expectedPerspective != Perspective::kUnknown &&
			clone.perspective == expectedPerspective &&
			IsValidLifecycleGenerations(clone.generations) &&
			clone.observedArmor == approved.armor &&
			clone.observedArmorAddon == approved.armorAddon &&
			clone.observedModel == expectedModel &&
			clone.perspectiveRootIdentity != 0 && clone.bipedIdentity != 0 &&
			clone.partCloneIdentity != 0 &&
			clone.mirrorItemSubtreeIdentity != 0 && clone.paneIdentity != 0 &&
			clone.perspectiveRootIdentity != clone.bipedIdentity &&
			clone.partCloneIdentity != clone.mirrorItemSubtreeIdentity &&
			clone.partCloneIdentity != clone.paneIdentity &&
			clone.mirrorItemSubtreeIdentity != clone.paneIdentity &&
			clone.mirrorItemSubtreeContractIdentity ==
				approved.mirrorItemSubtreeContractIdentity &&
			clone.paneContractIdentity == approved.paneContractIdentity &&
			clone.aperture == approved.aperture &&
			clone.exactGameplayClone && clone.partCloneDescendsPerspectiveRoot &&
			clone.mirrorItemDescendsPartClone && clone.paneDescendsMirrorItem &&
			clone.noInventoryMenuShadowOrDisplayClone &&
			clone.noEnginePointerRetained;
	}

	[[nodiscard]] constexpr bool AreCloneDynamicIdentitiesDisjoint(
		const CloneObservation& first,
		const CloneObservation& third) noexcept
	{
		const IdentityToken firstValues[]{ first.perspectiveRootIdentity,
			first.bipedIdentity, first.partCloneIdentity,
			first.mirrorItemSubtreeIdentity, first.paneIdentity };
		const IdentityToken thirdValues[]{ third.perspectiveRootIdentity,
			third.bipedIdentity, third.partCloneIdentity,
			third.mirrorItemSubtreeIdentity, third.paneIdentity };
		for (const auto left : firstValues) {
			for (const auto right : thirdValues) {
				if (left == 0 || right == 0 || left == right)
					return false;
			}
		}
		return true;
	}

	struct ApprovedCandidateEvidence
	{
		ApprovedContentIdentity approvedContent{};
		LifecycleGenerations generations{};
		std::uint32_t shieldSlotNumber{ 0 };
		CloneObservation firstPersonClone{};
		CloneObservation thirdPersonClone{};
		bool resolvedFromFrozenFirstPartyContentLock{ false };
		bool exactArmorWornAndEquippedAtShieldSlot{ false };
		bool armorAddonMatchesArmorCurrentRaceAndSex{ false };
		bool equipStateReacquiredAfterWakeup{ false };
		bool publicOrThirdPartyRegistrationUsed{ false };
		bool noEnginePointerRetained{ false };
	};

	[[nodiscard]] constexpr bool IsValidApprovedCandidate(
		const ApprovedCandidateEvidence& candidate) noexcept
	{
		return IsValidApprovedContentIdentity(candidate.approvedContent) &&
			IsValidLifecycleGenerations(candidate.generations) &&
			candidate.shieldSlotNumber == kShieldSlotNumber &&
			IsValidCloneObservation(candidate.firstPersonClone,
				candidate.approvedContent, Perspective::kFirstPerson) &&
			IsValidCloneObservation(candidate.thirdPersonClone,
				candidate.approvedContent, Perspective::kThirdPerson) &&
			candidate.firstPersonClone.generations == candidate.generations &&
			candidate.thirdPersonClone.generations == candidate.generations &&
			AreCloneDynamicIdentitiesDisjoint(
				candidate.firstPersonClone, candidate.thirdPersonClone) &&
			candidate.resolvedFromFrozenFirstPartyContentLock &&
			candidate.exactArmorWornAndEquippedAtShieldSlot &&
			candidate.armorAddonMatchesArmorCurrentRaceAndSex &&
			candidate.equipStateReacquiredAfterWakeup &&
			!candidate.publicOrThirdPartyRegistrationUsed &&
			candidate.noEnginePointerRetained;
	}

	struct CandidateBinding
	{
		ApprovedContentIdentity approvedContent{};
		LifecycleGenerations generations{};
		IdentityToken firstPersonPartCloneIdentity{ 0 };
		IdentityToken firstPersonPaneIdentity{ 0 };
		IdentityToken thirdPersonPartCloneIdentity{ 0 };
		IdentityToken thirdPersonPaneIdentity{ 0 };

		constexpr bool operator==(const CandidateBinding&) const noexcept = default;
	};

	[[nodiscard]] constexpr CandidateBinding MakeCandidateBinding(
		const ApprovedCandidateEvidence& candidate) noexcept
	{
		if (!IsValidApprovedCandidate(candidate))
			return {};
		return { candidate.approvedContent, candidate.generations,
			candidate.firstPersonClone.partCloneIdentity,
			candidate.firstPersonClone.paneIdentity,
			candidate.thirdPersonClone.partCloneIdentity,
			candidate.thirdPersonClone.paneIdentity };
	}

	using PoseWords = std::array<std::uint32_t, kPoseWordCount>;

	[[nodiscard]] constexpr bool IsValidPoseWords(
		const PoseWords& words) noexcept
	{
		bool anyNonzero = false;
		for (const auto word : words) {
			if (!IsFiniteFloatBits(word))
				return false;
			anyNonzero = anyNonzero || word != 0;
		}
		return anyNonzero;
	}

	enum class PosePhase : std::uint8_t
	{
		kUnknown,
		kPostWorld,
		kExactFirstPersonPaneDraw
	};

	struct PoseSnapshot
	{
		std::uint64_t snapshotToken{ 0 };
		std::uint64_t sourceFrame{ 0 };
		PosePhase phase{ PosePhase::kUnknown };
		CandidateBinding binding{};
		PoseWords paneWorldTransformBits{};
		bool copiedCoherently{ false };
		bool noEnginePointerRetained{ false };

		constexpr bool operator==(const PoseSnapshot&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidPoseSnapshot(
		const PoseSnapshot& snapshot,
		const PosePhase expectedPhase) noexcept
	{
		return snapshot.snapshotToken != 0 && snapshot.sourceFrame != 0 &&
			snapshot.phase == expectedPhase &&
			IsValidApprovedContentIdentity(snapshot.binding.approvedContent) &&
			IsValidLifecycleGenerations(snapshot.binding.generations) &&
			snapshot.binding.firstPersonPartCloneIdentity != 0 &&
			snapshot.binding.firstPersonPaneIdentity != 0 &&
			snapshot.binding.thirdPersonPartCloneIdentity != 0 &&
			snapshot.binding.thirdPersonPaneIdentity != 0 &&
			IsValidPoseWords(snapshot.paneWorldTransformBits) &&
			snapshot.copiedCoherently && snapshot.noEnginePointerRetained;
	}

	enum class PoseComparisonStatus : std::uint8_t
	{
		kBitExact,
		kPostWorldInvalid,
		kPaneDrawInvalid,
		kReusedSnapshotToken,
		kSourceFrameChanged,
		kCandidateOrGenerationChanged,
		kPoseBitsChanged
	};

	[[nodiscard]] constexpr PoseComparisonStatus ComparePostWorldToPaneDrawPose(
		const PoseSnapshot& postWorld,
		const PoseSnapshot& paneDraw) noexcept
	{
		if (!IsValidPoseSnapshot(postWorld, PosePhase::kPostWorld))
			return PoseComparisonStatus::kPostWorldInvalid;
		if (!IsValidPoseSnapshot(
				paneDraw, PosePhase::kExactFirstPersonPaneDraw)) {
			return PoseComparisonStatus::kPaneDrawInvalid;
		}
		if (postWorld.snapshotToken == paneDraw.snapshotToken)
			return PoseComparisonStatus::kReusedSnapshotToken;
		if (postWorld.sourceFrame != paneDraw.sourceFrame)
			return PoseComparisonStatus::kSourceFrameChanged;
		if (postWorld.binding != paneDraw.binding)
			return PoseComparisonStatus::kCandidateOrGenerationChanged;
		if (postWorld.paneWorldTransformBits !=
			paneDraw.paneWorldTransformBits) {
			return PoseComparisonStatus::kPoseBitsChanged;
		}
		return PoseComparisonStatus::kBitExact;
	}

	struct PostWorldPoseEvidence
	{
		PoseSnapshot pose{};
		bool renderWorldReturnedNormally{ false };
		bool sampledAfterWorldAndBeforeFirstPerson{ false };
		bool exactCurrentFirstPersonCloneReacquired{ false };
		bool guardedReadCompleted{ false };
		bool guardedReadFaulted{ false };
		bool noEnginePointerRetained{ false };
	};

	[[nodiscard]] constexpr bool IsValidPostWorldPoseEvidence(
		const PostWorldPoseEvidence& evidence) noexcept
	{
		return IsValidPoseSnapshot(evidence.pose, PosePhase::kPostWorld) &&
			evidence.renderWorldReturnedNormally &&
			evidence.sampledAfterWorldAndBeforeFirstPerson &&
			evidence.exactCurrentFirstPersonCloneReacquired &&
			evidence.guardedReadCompleted && !evidence.guardedReadFaulted &&
			evidence.noEnginePointerRetained;
	}

	struct VisiblePaneClearanceEvidence
	{
		std::uint64_t sourceFrame{ 0 };
		std::uint64_t postWorldSnapshotToken{ 0 };
		float observedSourceEyeClearance{ 0.0F };
		float requiredSourceEyeClearance{ 0.0F };
		bool reflectiveFrontFacesCurrentCamera{ false };
		bool paneIntersectsCurrentMainView{ false };
		bool paneVisibleAndNotAppCulled{ false };
		bool sourceCameraAgreesWithCurrentMainView{ false };
		bool schemaAndApertureRevalidated{ false };
		bool noEnginePointerRetained{ false };
	};

	[[nodiscard]] constexpr bool PassesVisiblePaneClearance(
		const VisiblePaneClearanceEvidence& evidence,
		const PoseSnapshot& postWorld) noexcept
	{
		return evidence.sourceFrame == postWorld.sourceFrame &&
			evidence.postWorldSnapshotToken == postWorld.snapshotToken &&
			IsFinite(evidence.observedSourceEyeClearance) &&
			IsFinite(evidence.requiredSourceEyeClearance) &&
			evidence.requiredSourceEyeClearance >= 0.0F &&
			evidence.observedSourceEyeClearance >
				evidence.requiredSourceEyeClearance &&
			evidence.reflectiveFrontFacesCurrentCamera &&
			evidence.paneIntersectsCurrentMainView &&
			evidence.paneVisibleAndNotAppCulled &&
			evidence.sourceCameraAgreesWithCurrentMainView &&
			evidence.schemaAndApertureRevalidated &&
			evidence.noEnginePointerRetained;
	}

	struct HiddenThirdPersonPoseFreshnessEvidence
	{
		std::uint64_t observationToken{ 0 };
		std::uint64_t sourceFrame{ 0 };
		std::uint64_t animationUpdateFrame{ 0 };
		CandidateBinding binding{};
		PoseWords thirdPersonPartCloneWorldTransformBits{};
		bool exactHiddenThirdPersonRootReacquired{ false };
		bool exactThirdPersonPartCloneReacquired{ false };
		bool hiddenBodyPoseCurrentForSourceFrame{ false };
		bool boneTransformsCurrentForSourceFrame{ false };
		bool firstPersonRootNotUsedAsCapturePose{ false };
		bool copiedAfterAnimationUpdate{ false };
		bool guardedReadCompleted{ false };
		bool guardedReadFaulted{ false };
		bool noEnginePointerRetained{ false };
	};

	[[nodiscard]] constexpr bool IsFreshHiddenThirdPersonPose(
		const HiddenThirdPersonPoseFreshnessEvidence& evidence,
		const PoseSnapshot& postWorld) noexcept
	{
		return evidence.observationToken != 0 &&
			evidence.sourceFrame == postWorld.sourceFrame &&
			evidence.animationUpdateFrame == postWorld.sourceFrame &&
			evidence.binding == postWorld.binding &&
			IsValidPoseWords(
				evidence.thirdPersonPartCloneWorldTransformBits) &&
			evidence.exactHiddenThirdPersonRootReacquired &&
			evidence.exactThirdPersonPartCloneReacquired &&
			evidence.hiddenBodyPoseCurrentForSourceFrame &&
			evidence.boneTransformsCurrentForSourceFrame &&
			evidence.firstPersonRootNotUsedAsCapturePose &&
			evidence.copiedAfterAnimationUpdate &&
			evidence.guardedReadCompleted && !evidence.guardedReadFaulted &&
			evidence.noEnginePointerRetained;
	}

	struct MainTargetIdentity
	{
		IdentityToken deviceIdentity{ 0 };
		IdentityToken colorViewIdentity{ 0 };
		IdentityToken colorResourceIdentity{ 0 };
		IdentityToken depthViewIdentity{ 0 };
		IdentityToken depthResourceIdentity{ 0 };
		IdentityToken viewportIdentity{ 0 };
		std::uint32_t viewportWidth{ 0 };
		std::uint32_t viewportHeight{ 0 };
		float minimumDepth{ 0.0F };
		float maximumDepth{ 1.0F };

		constexpr bool operator==(const MainTargetIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidMainTargetIdentity(
		const MainTargetIdentity& identity) noexcept
	{
		return identity.deviceIdentity != 0 && identity.colorViewIdentity != 0 &&
			identity.colorResourceIdentity != 0 &&
			identity.depthViewIdentity != 0 &&
			identity.depthResourceIdentity != 0 &&
			identity.viewportIdentity != 0 && identity.viewportWidth != 0 &&
			identity.viewportHeight != 0 && IsFinite(identity.minimumDepth) &&
			IsFinite(identity.maximumDepth) && identity.minimumDepth >= 0.0F &&
			identity.maximumDepth <= 1.0F &&
			identity.minimumDepth < identity.maximumDepth &&
			identity.colorViewIdentity != identity.depthViewIdentity &&
			identity.colorResourceIdentity != identity.depthResourceIdentity;
	}

	[[nodiscard]] constexpr bool SameMainTargetResourceIdentity(
		const MainTargetIdentity& left,
		const MainTargetIdentity& right) noexcept
	{
		return left.deviceIdentity == right.deviceIdentity &&
			left.colorViewIdentity == right.colorViewIdentity &&
			left.colorResourceIdentity == right.colorResourceIdentity &&
			left.depthViewIdentity == right.depthViewIdentity &&
			left.depthResourceIdentity == right.depthResourceIdentity;
	}

	struct MainTargetEvidence
	{
		std::uint64_t sourceFrame{ 0 };
		std::uint64_t paneDrawSnapshotToken{ 0 };
		MainTargetIdentity expectedMainTarget{};
		MainTargetIdentity observedAtPaneDrawReturn{};
		IdentityToken prospectivePrivateColorResourceIdentity{ 0 };
		IdentityToken prospectivePrivateDepthResourceIdentity{ 0 };
		bool currentMainColorWritable{ false };
		bool currentMainDepthWritable{ false };
		bool currentViewportMatchesMainView{ false };
		bool contextAndTargetsBelongToExactDevice{ false };
		bool targetStableAcrossNativePaneDraw{ false };
		bool privateTargetUnbound{ false };
		bool noOutputUAVPredicationOrStreamOutputConflict{ false };
		bool noResourceAliasObserved{ false };
		bool guardedReadCompleted{ false };
		bool guardedReadFaulted{ false };
		bool noEnginePointerRetained{ false };
	};

	enum class MainTargetStatus : std::uint8_t
	{
		kValid,
		kMalformed,
		kNotCurrentWritableMainTarget,
		kResourceAlias
	};

	[[nodiscard]] constexpr MainTargetStatus ValidateMainTargetEvidence(
		const MainTargetEvidence& evidence,
		const PoseSnapshot& paneDraw) noexcept
	{
		if (evidence.sourceFrame == 0 ||
			evidence.paneDrawSnapshotToken == 0 ||
			!IsValidMainTargetIdentity(evidence.expectedMainTarget) ||
			!IsValidMainTargetIdentity(evidence.observedAtPaneDrawReturn) ||
			evidence.prospectivePrivateColorResourceIdentity == 0 ||
			evidence.prospectivePrivateDepthResourceIdentity == 0) {
			return MainTargetStatus::kMalformed;
		}
		const auto mainColor =
			evidence.observedAtPaneDrawReturn.colorResourceIdentity;
		const auto mainDepth =
			evidence.observedAtPaneDrawReturn.depthResourceIdentity;
		const auto privateColor =
			evidence.prospectivePrivateColorResourceIdentity;
		const auto privateDepth =
			evidence.prospectivePrivateDepthResourceIdentity;
		if (mainColor == mainDepth || mainColor == privateColor ||
			mainColor == privateDepth || mainDepth == privateColor ||
			mainDepth == privateDepth || privateColor == privateDepth ||
			!evidence.noResourceAliasObserved) {
			return MainTargetStatus::kResourceAlias;
		}
		if (evidence.sourceFrame != paneDraw.sourceFrame ||
			evidence.paneDrawSnapshotToken != paneDraw.snapshotToken ||
			!SameMainTargetResourceIdentity(
				evidence.expectedMainTarget,
				evidence.observedAtPaneDrawReturn) ||
			!evidence.currentMainColorWritable ||
			!evidence.currentMainDepthWritable ||
			!evidence.currentViewportMatchesMainView ||
			!evidence.contextAndTargetsBelongToExactDevice ||
			!evidence.targetStableAcrossNativePaneDraw ||
			!evidence.privateTargetUnbound ||
			!evidence.noOutputUAVPredicationOrStreamOutputConflict ||
			!evidence.guardedReadCompleted || evidence.guardedReadFaulted ||
			!evidence.noEnginePointerRetained) {
			return MainTargetStatus::kNotCurrentWritableMainTarget;
		}
		return MainTargetStatus::kValid;
	}

	struct ExactFirstPersonPaneDrawEvidence
	{
		PoseSnapshot pose{};
		MainTargetEvidence target{};
		bool insideExactFirstPersonNativeScope{ false };
		bool exactCurrentFirstPersonPaneSubmitted{ false };
		bool paneDescendsExactCurrentMirrorItemAndPartClone{ false };
		bool currentMainPlayerViewSubmission{ false };
		bool visibleAndNotAppCulled{ false };
		bool menuInventoryShadowAndPrivatePassExcluded{ false };
		bool nativeDrawEntered{ false };
		bool nativeDrawReturnedNormally{ false };
		bool poseCopiedAfterNormalReturn{ false };
		bool callbackFaulted{ false };
		bool noEnginePointerRetained{ false };
	};

	[[nodiscard]] constexpr bool IsValidExactFirstPersonPaneDraw(
		const ExactFirstPersonPaneDrawEvidence& evidence) noexcept
	{
		return IsValidPoseSnapshot(
				evidence.pose, PosePhase::kExactFirstPersonPaneDraw) &&
			evidence.insideExactFirstPersonNativeScope &&
			evidence.exactCurrentFirstPersonPaneSubmitted &&
			evidence.paneDescendsExactCurrentMirrorItemAndPartClone &&
			evidence.currentMainPlayerViewSubmission &&
			evidence.visibleAndNotAppCulled &&
			evidence.menuInventoryShadowAndPrivatePassExcluded &&
			evidence.nativeDrawEntered && evidence.nativeDrawReturnedNormally &&
			evidence.poseCopiedAfterNormalReturn && !evidence.callbackFaulted &&
			evidence.noEnginePointerRetained;
	}

	enum class ReadOnlyGateStatus : std::uint8_t
	{
		kAdmittedReadOnlyEvidenceOnly,
		kAlreadyFailStopped,
		kCandidateOverflowFault,
		kNoCandidateDark,
		kAmbiguousCandidatesDark,
		kApprovedCandidateInvalid,
		kLifecycleOrPointOfViewInvalid,
		kPostWorldPoseInvalid,
		kFirstPersonPaneDrawInvalid,
		kPoseNotBitExact,
		kVisiblePaneClearanceRejected,
		kHiddenThirdPersonPoseStale,
		kMainTargetInvalid,
		kTargetAliasFault,
		kObserverFaultEvidence,
		kObservationTokenInvalid
	};

	struct ObserverSafetyEvidence
	{
		bool guardedReadsCompleted{ false };
		bool guardedReadFaulted{ false };
		bool callbacksCompleted{ false };
		bool callbackFaulted{ false };
		bool nativeCallsReturnedNormally{ false };
		bool noEnginePointerRetainedAnywhere{ false };
	};

	struct ApprovedContentReadOnlyGateEvidence
	{
		std::uint64_t observationToken{ 0 };
		CandidateCounter candidates{};
		LifecycleState lifecycle{};
		ApprovedCandidateEvidence candidate{};
		PostWorldPoseEvidence postWorld{};
		VisiblePaneClearanceEvidence visiblePaneClearance{};
		HiddenThirdPersonPoseFreshnessEvidence hiddenThirdPerson{};
		ExactFirstPersonPaneDrawEvidence paneDraw{};
		ObserverSafetyEvidence safety{};
	};

	struct ReadOnlyGateResult
	{
		ReadOnlyGateStatus status{
			ReadOnlyGateStatus::kApprovedCandidateInvalid };
		std::uint64_t observationToken{ 0 };
		std::uint64_t sourceFrame{ 0 };
		CandidateBinding binding{};

		[[nodiscard]] constexpr bool ReadOnlyEvidenceComplete() const noexcept
		{
			return status ==
				ReadOnlyGateStatus::kAdmittedReadOnlyEvidenceOnly;
		}
	};

	[[nodiscard]] constexpr ReadOnlyGateResult EvaluateReadOnlyGate(
		const ApprovedContentReadOnlyGateEvidence& evidence) noexcept
	{
		ReadOnlyGateResult result{};
		if (evidence.lifecycle.terminalFailStop) {
			result.status = ReadOnlyGateStatus::kAlreadyFailStopped;
			return result;
		}
		if (!IsValidLifecycleState(evidence.lifecycle)) {
			result.status = ReadOnlyGateStatus::kObserverFaultEvidence;
			return result;
		}
		const auto cardinality = ClassifyCandidates(evidence.candidates);
		if (cardinality == CandidateCardinality::kOverflowFault) {
			result.status = ReadOnlyGateStatus::kCandidateOverflowFault;
			return result;
		}
		if (cardinality == CandidateCardinality::kZero) {
			result.status = ReadOnlyGateStatus::kNoCandidateDark;
			return result;
		}
		if (cardinality == CandidateCardinality::kAmbiguousMany) {
			result.status = ReadOnlyGateStatus::kAmbiguousCandidatesDark;
			return result;
		}
		if (evidence.observationToken == 0) {
			result.status = ReadOnlyGateStatus::kObservationTokenInvalid;
			return result;
		}
		if (evidence.safety.guardedReadFaulted ||
			evidence.safety.callbackFaulted ||
			!evidence.safety.guardedReadsCompleted ||
			!evidence.safety.callbacksCompleted ||
			!evidence.safety.nativeCallsReturnedNormally ||
			!evidence.safety.noEnginePointerRetainedAnywhere ||
			!evidence.candidate.noEnginePointerRetained ||
			!evidence.candidate.firstPersonClone.noEnginePointerRetained ||
			!evidence.candidate.thirdPersonClone.noEnginePointerRetained ||
			evidence.postWorld.guardedReadFaulted ||
			!evidence.postWorld.renderWorldReturnedNormally ||
			!evidence.postWorld.pose.noEnginePointerRetained ||
			evidence.hiddenThirdPerson.guardedReadFaulted ||
			!evidence.hiddenThirdPerson.noEnginePointerRetained ||
			evidence.paneDraw.callbackFaulted ||
			!evidence.paneDraw.nativeDrawReturnedNormally ||
			!evidence.paneDraw.pose.noEnginePointerRetained ||
			evidence.paneDraw.target.guardedReadFaulted ||
			!evidence.paneDraw.target.noEnginePointerRetained ||
			!evidence.postWorld.noEnginePointerRetained ||
			!evidence.paneDraw.noEnginePointerRetained) {
			result.status = ReadOnlyGateStatus::kObserverFaultEvidence;
			return result;
		}
		if (!IsValidApprovedCandidate(evidence.candidate)) {
			result.status = ReadOnlyGateStatus::kApprovedCandidateInvalid;
			return result;
		}
		if (!evidence.lifecycle.exactApprovedArmorEquipped ||
			evidence.lifecycle.perspective != Perspective::kFirstPerson ||
			evidence.lifecycle.generations != evidence.candidate.generations) {
			result.status =
				ReadOnlyGateStatus::kLifecycleOrPointOfViewInvalid;
			return result;
		}
		const auto binding = MakeCandidateBinding(evidence.candidate);
		if (!IsValidPostWorldPoseEvidence(evidence.postWorld) ||
			evidence.postWorld.pose.binding != binding) {
			result.status = ReadOnlyGateStatus::kPostWorldPoseInvalid;
			return result;
		}
		if (!IsValidExactFirstPersonPaneDraw(evidence.paneDraw) ||
			evidence.paneDraw.pose.binding != binding) {
			result.status = ReadOnlyGateStatus::kFirstPersonPaneDrawInvalid;
			return result;
		}
		// This observer only supplies read-only identity and pose evidence.  The
		// reflective runtime owns presentation, freshness, retained-target,
		// alias, and cleanup validation against the live render resources.

		result.status = ReadOnlyGateStatus::kAdmittedReadOnlyEvidenceOnly;
		result.observationToken = evidence.observationToken;
		result.sourceFrame = evidence.postWorld.pose.sourceFrame;
		result.binding = binding;
		return result;
	}

	[[nodiscard]] constexpr bool CanAuthorizeCapture(
		const ReadOnlyGateResult& result) noexcept
	{
		return kApprovedContentAuthorityGranted && kRenderAuthorityGranted &&
			kCaptureAuthorized && result.ReadOnlyEvidenceComplete();
	}

	[[nodiscard]] constexpr bool CanAuthorizePublication(
		const ReadOnlyGateResult& result) noexcept
	{
		return kApprovedContentAuthorityGranted && kRenderAuthorityGranted &&
			kPublicationAuthorized && result.ReadOnlyEvidenceComplete();
	}

	[[nodiscard]] constexpr bool CanAuthorizeDelivery(
		const ReadOnlyGateResult& result) noexcept
	{
		return kApprovedContentAuthorityGranted && kRenderAuthorityGranted &&
			kDeliveryAuthorized && result.ReadOnlyEvidenceComplete();
	}

	enum class TelemetryKind : std::uint8_t
	{
		kCandidateZero,
		kCandidateUnique,
		kCandidateMany,
		kAdmission,
		kOrdinaryReject,
		kFault,
		kCount
	};

	struct TelemetryLedger
	{
		std::size_t totalRecords{ 0 };
		std::array<std::size_t,
			static_cast<std::size_t>(TelemetryKind::kCount)> counts{};
		ObserverFault firstFault{ ObserverFault::kNone };
		bool overflowed{ false };
		bool terminalFailStop{ false };

		constexpr bool operator==(const TelemetryLedger&) const noexcept = default;
	};

	[[nodiscard]] constexpr TelemetryLedger LatchTerminalFault(
		TelemetryLedger ledger,
		const ObserverFault fault) noexcept
	{
		if (fault == ObserverFault::kNone)
			return ledger;
		if (ledger.firstFault == ObserverFault::kNone)
			ledger.firstFault = fault;
		ledger.terminalFailStop = true;
		return ledger;
	}

	enum class TelemetryRecordStatus : std::uint8_t
	{
		kRecorded,
		kAlreadyFailStopped,
		kOverflowFailStopped,
		kMalformedFailStopped
	};

	struct TelemetryRecordResult
	{
		TelemetryLedger next{};
		TelemetryRecordStatus status{
			TelemetryRecordStatus::kMalformedFailStopped };
	};

	[[nodiscard]] constexpr bool IsUsableTelemetryLedger(
		const TelemetryLedger& ledger) noexcept
	{
		std::size_t sum = 0;
		for (const auto count : ledger.counts) {
			if (count > kTelemetryCapacity)
				return false;
			sum += count;
		}
		return sum == ledger.totalRecords &&
			ledger.totalRecords <= kTelemetryCapacity &&
			(ledger.terminalFailStop ==
				(ledger.firstFault != ObserverFault::kNone));
	}

	[[nodiscard]] constexpr TelemetryRecordResult RecordTelemetry(
		const TelemetryLedger& current,
		const TelemetryKind kind) noexcept
	{
		if (!IsUsableTelemetryLedger(current) ||
			kind == TelemetryKind::kCount) {
			return { LatchTerminalFault(
				current, ObserverFault::kMalformedState),
				TelemetryRecordStatus::kMalformedFailStopped };
		}
		if (current.terminalFailStop) {
			return { current,
				TelemetryRecordStatus::kAlreadyFailStopped };
		}
		if (current.totalRecords >= kTelemetryCapacity) {
			auto next = LatchTerminalFault(
				current, ObserverFault::kTelemetryOverflow);
			next.overflowed = true;
			return { next,
				TelemetryRecordStatus::kOverflowFailStopped };
		}
		auto next = current;
		++next.totalRecords;
		++next.counts[static_cast<std::size_t>(kind)];
		return { next, TelemetryRecordStatus::kRecorded };
	}

	[[nodiscard]] constexpr bool IsTerminalGateStatus(
		const ReadOnlyGateStatus status) noexcept
	{
		return status == ReadOnlyGateStatus::kAlreadyFailStopped ||
			status == ReadOnlyGateStatus::kCandidateOverflowFault ||
			status == ReadOnlyGateStatus::kTargetAliasFault ||
			status == ReadOnlyGateStatus::kObserverFaultEvidence ||
			status == ReadOnlyGateStatus::kObservationTokenInvalid;
	}

	[[nodiscard]] constexpr ObserverFault FaultForTerminalGateStatus(
		const ReadOnlyGateStatus status) noexcept
	{
		switch (status) {
		case ReadOnlyGateStatus::kCandidateOverflowFault:
			return ObserverFault::kCandidateOverflow;
		case ReadOnlyGateStatus::kTargetAliasFault:
			return ObserverFault::kTargetAlias;
		case ReadOnlyGateStatus::kObservationTokenInvalid:
			return ObserverFault::kTokenExhausted;
		case ReadOnlyGateStatus::kObserverFaultEvidence:
			return ObserverFault::kMalformedEvidence;
		case ReadOnlyGateStatus::kAlreadyFailStopped:
			return ObserverFault::kMalformedState;
		default: return ObserverFault::kNone;
		}
	}

	[[nodiscard]] constexpr TelemetryLedger ApplyGateResult(
		const TelemetryLedger& current,
		const ReadOnlyGateResult& result) noexcept
	{
		if (current.terminalFailStop)
			return current;
		if (IsTerminalGateStatus(result.status)) {
			return LatchTerminalFault(
				current, FaultForTerminalGateStatus(result.status));
		}
		TelemetryKind kind = TelemetryKind::kOrdinaryReject;
		switch (result.status) {
		case ReadOnlyGateStatus::kAdmittedReadOnlyEvidenceOnly:
			kind = TelemetryKind::kAdmission;
			break;
		case ReadOnlyGateStatus::kNoCandidateDark:
			kind = TelemetryKind::kCandidateZero;
			break;
		case ReadOnlyGateStatus::kAmbiguousCandidatesDark:
			kind = TelemetryKind::kCandidateMany;
			break;
		default: break;
		}
		return RecordTelemetry(current, kind).next;
	}
}
