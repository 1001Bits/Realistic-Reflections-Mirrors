#pragma once

#include "MirrorAuthoringContract.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace HandMirrorContentRuntimePolicy
{
	// Product shipping, distribution, content emission, and acceptance remain
	// closed.  The separately marker-gated internal reflection runtime owns the
	// exact technical integration seams below without granting product authority.
	inline constexpr bool kOwnedFormIDsAssigned = false;
	inline constexpr bool kPlayerFacingNamesApproved = false;
	inline constexpr bool kEconomyValuesApproved = false;
	inline constexpr bool kWorldInventoryModelsAuthored = false;
	inline constexpr bool kFirstPersonModelsAuthored = false;
	inline constexpr bool kThirdPersonModelsAuthored = false;
	inline constexpr bool kContentBuilderIntegrationWired = false;
	inline constexpr bool kContentEmissionAuthorized = false;
	inline constexpr bool kContentPluginEmitted = false;

	inline constexpr bool kEquipEventIntegrationWired = true;
	inline constexpr bool kBipedPartCloneResolverWired = true;
	inline constexpr bool kMainViewSubmissionObserverWired = true;
	inline constexpr bool kHandMirrorPoseResolverWired = true;
	inline constexpr bool kSameFrameMovingSurfaceDeliveryWired = true;
	inline constexpr bool kPrivateMirrorItemSubtreeSuppressionWired = true;
	inline constexpr bool kPlayerSelfInclusionWired = true;
	inline constexpr bool kSingletonMirrorArbiterIntegrationWired = true;
	inline constexpr bool kSaveLoadEquipRescanIntegrationWired = true;
	inline constexpr bool kRuntimeIntegrationWired = true;
	inline constexpr bool kRuntimeAcceptanceGranted = false;
	inline constexpr bool kPlanarAPIV1HandMirrorPublicationAllowed = false;
	inline constexpr bool kHandMirrorIdentityABIVersionImplemented = false;
	inline constexpr bool kVRFirstReleaseSupported = false;
	inline constexpr bool kVRIKFirstReleaseSupported = false;

	inline constexpr bool kMerchantTargetsAudited = true;
	inline constexpr bool kMerchantServiceAndRestockSemanticsVerified = true;
	inline constexpr std::size_t kAuditedMerchantTargetCount = 8;
	inline constexpr bool kDistributionAdapterVersionPinned = false;
	inline constexpr bool kDistributionAdapterIntegrationWired = false;
	inline constexpr bool kDistributionEmitted = false;
	inline constexpr bool kAnyHandMirrorArtifactEmitted = false;

	// These are explicit product exclusions, not deferred implementation lanes.
	inline constexpr bool kPublicOrThirdPartyRuntimeRegistrationAllowed = false;
	inline constexpr bool kCraftingAllowed = false;
	inline constexpr bool kMiscInventoryProductAllowed = false;
	inline constexpr bool kConstructibleObjectAllowed = false;
	inline constexpr bool kWorldPlacementAllowed = false;
	inline constexpr bool kPreviewGhostAllowed = false;
	inline constexpr bool kPlacementJournalAllowed = false;
	inline constexpr bool kPlacementRegistryAllowed = false;
	inline constexpr bool kDirectMasterContainerOverrideAllowed = false;
	inline constexpr bool kRuntimeBaseContainerMutationAllowed = false;
	inline constexpr bool kActorDistributionAllowed = false;
	inline constexpr bool kLeveledListOverrideAllowed = false;

	// Verified engine semantics. These values describe the shield equipment
	// contract; they do not reserve or assign any form in this mod.
	inline constexpr std::uint32_t kArmorShieldRecordFlag = 1U << 6U;
	inline constexpr std::uint32_t kShieldBipedSlotMask = 1U << 9U;
	inline constexpr std::uint32_t kShieldBipedObjectIndex = 9U;
	inline constexpr std::uint32_t kDisplayedShieldSlotNumber = 39U;
	inline constexpr std::int32_t kShieldEquippedItemType = 25;
	inline constexpr std::size_t kMirrorPublicationChannelCapacity = 1;
	inline constexpr std::size_t kPlannedHandMirrorStyleCount = 5;
	inline constexpr std::uint32_t kHandAcquireHysteresisFrames = 1;
	inline constexpr std::uint32_t kHandReleaseHysteresisFrames = 2;

	enum class RecordKind : std::uint8_t
	{
		kUnknown,
		kArmor,
		kArmorAddon,
		kWeapon,
		kActivator,
		kStatic,
		kMisc,
		kConstructibleObject
	};

	enum class IdentityAuthority : std::uint8_t
	{
		kUnknown,
		kFirstPartyInternal,
		kPublicRegistration,
		kThirdPartyMarker
	};

	enum class Perspective : std::uint8_t
	{
		kUnknown,
		kFirstPerson,
		kThirdPerson,
		// The headset view displays and captures the same VRIK body. Only the
		// separately gated exact VR runtime constructs this identity.
		kVRFirstPerson
	};

	enum class DeliveryOpportunity : std::uint8_t
	{
		kUnknown,
		kPostWorldBeforeFirstPerson,
		kPreWorldBeforeThirdPerson,
		kPostCaptureThirdPersonOverlay
	};

	enum class StableGenerationEvent : std::uint8_t
	{
		kUnknown,
		kPoseOnly,
		kEquip,
		kUnequip,
		kPerspectiveRootChanged,
		kBipedRootRebuilt,
		kPartCloneReplaced,
		kRaceOrSexModelRebuilt,
		kAuthoredSurfaceIdentityChanged,
		kGameLoadInvalidation
	};

	enum class DistributionLane : std::uint8_t
	{
		kNone,
		kOptionalExactMerchantAdapter,
		kDirectMasterContainerOverride,
		kRuntimeBaseContainerMutation,
		kActorDistribution,
		kLeveledListOverride,
		kCraftingRecipe
	};

	enum class RejectionReason : std::uint8_t
	{
		kNone,
		kRecordShape,
		kShieldClassification,
		kFirstPartyIdentity,
		kModelSet,
		kPaneContract,
		kForbiddenArtifact,
		kStableGeneration,
		kPlayerOrEquipState,
		kBipedObjectIdentity,
		kPerspectiveRoot,
		kNonGameplayClone,
		kNotRaised,
		kMainViewSubmission,
		kPoseObservation,
		kDeliveryOpportunity,
		kSingletonOwnership,
		kSuppressionTarget,
		kSuppressionOrdering,
		kSuppressionRestore,
		kPlayerSelfInclusion,
		kReloadRescan,
		kDistributionLane,
		kDistributionEvidence
	};

	struct HandMirrorContentEvidence
	{
		RecordKind armorKind{ RecordKind::kUnknown };
		RecordKind armorAddonKind{ RecordKind::kUnknown };
		IdentityAuthority authority{ IdentityAuthority::kUnknown };
		std::uint32_t armorRecordFlags{ 0 };
		std::uint32_t armorBipedSlotMask{ 0 };
		std::uint32_t armorAddonBipedSlotMask{ 0 };
		std::int32_t equippedItemType{ 0 };
		std::size_t styleCount{ 0 };
		std::size_t forbiddenOwnedRecordCount{ 0 };
		bool everyStyleOwnsDistinctArmorAndAddon{ false };
		bool everyStyleProvidesCompleteModelSet{ false };
		bool exactArmorPluginAndLocalIdentityAssigned{ false };
		bool exactArmorAddonPluginAndLocalIdentityAssigned{ false };
		bool armorReferencesExactArmorAddon{ false };
		bool armorAddonSupportsPlayerRaceAndSex{ false };
		bool maleWorldInventoryModelAssigned{ false };
		bool femaleWorldInventoryModelAssigned{ false };
		bool maleFirstPersonModelAssigned{ false };
		bool femaleFirstPersonModelAssigned{ false };
		bool maleThirdPersonModelAssigned{ false };
		bool femaleThirdPersonModelAssigned{ false };
		bool firstPersonPathsDistinctFromThirdPersonPaths{ false };
		bool worldInventoryModelsContainNoLivePane{ false };
		bool everyEquippedModelHasExactSchemaV2Pane{ false };
		bool everyPaneHasExactLocalAxesAndApertureMetadata{ false };
		bool everyEquippedPaneHasAuthoredDarkFallback{ false };
		bool frozenContentApprovalIdentityAssigned{ false };
		bool everyEquippedModelAssetIdentityAssigned{ false };
		bool everyStyleUsesOneSharedAuthoredSurfaceIdentity{ false };
		bool publicRegistrationOrUnownedKeywordUnused{ false };
	};

	[[nodiscard]] constexpr bool IsForbiddenProductRecordKind(
		const RecordKind kind) noexcept
	{
		return kind == RecordKind::kMisc ||
		       kind == RecordKind::kConstructibleObject ||
		       kind == RecordKind::kActivator || kind == RecordKind::kStatic ||
		       kind == RecordKind::kWeapon;
	}

	[[nodiscard]] constexpr RejectionReason ValidateContentShape(
		const HandMirrorContentEvidence& evidence) noexcept
	{
		if (evidence.armorKind != RecordKind::kArmor ||
			evidence.armorAddonKind != RecordKind::kArmorAddon)
			return RejectionReason::kRecordShape;
		if ((evidence.armorRecordFlags & kArmorShieldRecordFlag) == 0 ||
			evidence.armorBipedSlotMask != kShieldBipedSlotMask ||
			evidence.armorAddonBipedSlotMask != kShieldBipedSlotMask ||
			evidence.equippedItemType != kShieldEquippedItemType) {
			return RejectionReason::kShieldClassification;
		}
		if (evidence.authority != IdentityAuthority::kFirstPartyInternal ||
			!evidence.exactArmorPluginAndLocalIdentityAssigned ||
			!evidence.exactArmorAddonPluginAndLocalIdentityAssigned ||
			!evidence.armorReferencesExactArmorAddon ||
			!evidence.armorAddonSupportsPlayerRaceAndSex ||
			!evidence.publicRegistrationOrUnownedKeywordUnused) {
			return RejectionReason::kFirstPartyIdentity;
		}
		if (evidence.styleCount != kPlannedHandMirrorStyleCount ||
			!evidence.everyStyleOwnsDistinctArmorAndAddon ||
			!evidence.everyStyleProvidesCompleteModelSet) {
			return RejectionReason::kModelSet;
		}
		if (!evidence.maleWorldInventoryModelAssigned ||
			!evidence.femaleWorldInventoryModelAssigned ||
			!evidence.maleFirstPersonModelAssigned ||
			!evidence.femaleFirstPersonModelAssigned ||
			!evidence.maleThirdPersonModelAssigned ||
			!evidence.femaleThirdPersonModelAssigned ||
			!evidence.firstPersonPathsDistinctFromThirdPersonPaths ||
			!evidence.worldInventoryModelsContainNoLivePane) {
			return RejectionReason::kModelSet;
		}
		if (!evidence.everyEquippedModelHasExactSchemaV2Pane ||
			!evidence.everyPaneHasExactLocalAxesAndApertureMetadata ||
			!evidence.everyEquippedPaneHasAuthoredDarkFallback ||
			!evidence.frozenContentApprovalIdentityAssigned ||
			!evidence.everyEquippedModelAssetIdentityAssigned ||
			!evidence.everyStyleUsesOneSharedAuthoredSurfaceIdentity) {
			return RejectionReason::kPaneContract;
		}
		if (evidence.forbiddenOwnedRecordCount != 0)
			return RejectionReason::kForbiddenArtifact;
		return RejectionReason::kNone;
	}

	struct StableCandidateGeneration
	{
		std::uint64_t equip{ 0 };
		std::uint64_t root{ 0 };
		std::uint64_t clone{ 0 };

		constexpr bool operator==(
			const StableCandidateGeneration&) const noexcept = default;
	};

	/**
	 * Immutable receipt for one approved hand-mirror style's authored surface.
	 *
	 * These are value identities, not engine pointers and not public registration
	 * handles. A future deterministic content/NIF validator must assign them from
	 * one frozen content lock. The same receipt must be recovered independently
	 * from the visible model, the third-person capture model, the sampled pane,
	 * and the publication attempt. Model bytes remain separately identified so
	 * distinct first- and third-person assets cannot masquerade as one another.
	 */
	struct AuthoredSurfaceIdentity
	{
		std::uint64_t contentApprovalIdentity{ 0 };
		std::uint64_t styleIdentity{ 0 };
		std::uint64_t mirrorItemSubtreeContractIdentity{ 0 };
		std::uint64_t paneContractIdentity{ 0 };
		std::uint64_t apertureContractIdentity{ 0 };
		MirrorAuthoringContract::SchemaVersion paneSchema{
			MirrorAuthoringContract::SchemaVersion::kUnknown };

		constexpr bool operator==(
			const AuthoredSurfaceIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidAuthoredSurfaceIdentity(
		const AuthoredSurfaceIdentity& identity) noexcept
	{
		return identity.contentApprovalIdentity != 0 &&
		       identity.styleIdentity != 0 &&
		       identity.mirrorItemSubtreeContractIdentity != 0 &&
		       identity.paneContractIdentity != 0 &&
		       identity.apertureContractIdentity != 0 &&
		       identity.paneSchema ==
			       MirrorAuthoringContract::SchemaVersion::kRectangularV2;
	}

	[[nodiscard]] constexpr bool AreIdentityTriplesDisjoint(
		const std::uintptr_t left0,
		const std::uintptr_t left1,
		const std::uintptr_t left2,
		const std::uintptr_t right0,
		const std::uintptr_t right1,
		const std::uintptr_t right2) noexcept
	{
		const std::uintptr_t left[]{ left0, left1, left2 };
		const std::uintptr_t right[]{ right0, right1, right2 };
		for (const auto leftValue : left) {
			for (const auto rightValue : right) {
				if (leftValue == 0 || rightValue == 0 ||
					leftValue == rightValue) {
					return false;
				}
			}
		}
		return true;
	}

	[[nodiscard]] constexpr bool IsDistinctIdentityTriple(
		const std::uintptr_t first,
		const std::uintptr_t second,
		const std::uintptr_t third) noexcept
	{
		return first != 0 && second != 0 && third != 0 && first != second &&
		       first != third && second != third;
	}

	struct HandMirrorOwnerIdentity
	{
		std::uint32_t playerFormID{ 0 };
		std::uint64_t playerHandleToken{ 0 };
		std::uintptr_t armorPluginIdentity{ 0 };
		std::uint32_t armorLocalFormID{ 0 };
		std::uintptr_t armorBase{ 0 };
		std::uintptr_t armorAddonPluginIdentity{ 0 };
		std::uint32_t armorAddonLocalFormID{ 0 };
		std::uintptr_t armorAddonBase{ 0 };
		std::uint32_t shieldSlotNumber{ 0 };
		StableCandidateGeneration stableGeneration{};
		AuthoredSurfaceIdentity authoredSurface{};
		std::uint64_t visibleModelAssetIdentity{ 0 };
		std::uint64_t captureModelAssetIdentity{ 0 };
		Perspective perspective{ Perspective::kUnknown };
		std::uintptr_t visiblePartClone{ 0 };
		std::uintptr_t capturePartClone{ 0 };
		std::uintptr_t visibleMirrorItemSubtree{ 0 };
		std::uintptr_t captureMirrorItemSubtree{ 0 };
		std::uintptr_t visiblePaneSubtree{ 0 };
		std::uintptr_t capturePaneSubtree{ 0 };

		constexpr bool operator==(
			const HandMirrorOwnerIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidHandMirrorOwnerIdentity(
		const HandMirrorOwnerIdentity& owner) noexcept
	{
		const bool commonIdentityValid =
		       owner.playerFormID != 0 && owner.playerHandleToken != 0 &&
		       owner.armorPluginIdentity != 0 && owner.armorLocalFormID != 0 &&
		       owner.armorBase != 0 && owner.armorAddonPluginIdentity != 0 &&
		       owner.armorAddonLocalFormID != 0 && owner.armorAddonBase != 0 &&
		       owner.shieldSlotNumber == kDisplayedShieldSlotNumber &&
		       owner.stableGeneration.equip != 0 &&
		       owner.stableGeneration.root != 0 &&
		       owner.stableGeneration.clone != 0 &&
		       IsValidAuthoredSurfaceIdentity(owner.authoredSurface) &&
		       owner.visibleModelAssetIdentity != 0 &&
		       owner.captureModelAssetIdentity != 0 &&
		       owner.perspective != Perspective::kUnknown &&
		       owner.visiblePartClone != 0 && owner.capturePartClone != 0 &&
		       owner.visibleMirrorItemSubtree != 0 &&
		       owner.captureMirrorItemSubtree != 0 &&
		       owner.visiblePaneSubtree != 0 && owner.capturePaneSubtree != 0 &&
		       IsDistinctIdentityTriple(
			       owner.visiblePartClone,
			       owner.visibleMirrorItemSubtree,
			       owner.visiblePaneSubtree) &&
		       IsDistinctIdentityTriple(
			       owner.capturePartClone,
			       owner.captureMirrorItemSubtree,
			       owner.capturePaneSubtree);
		if (!commonIdentityValid)
			return false;
		if (owner.perspective == Perspective::kFirstPerson) {
			return owner.visibleModelAssetIdentity !=
				       owner.captureModelAssetIdentity &&
			       AreIdentityTriplesDisjoint(
				       owner.visiblePartClone,
				       owner.visibleMirrorItemSubtree,
				       owner.visiblePaneSubtree,
				       owner.capturePartClone,
				       owner.captureMirrorItemSubtree,
				       owner.capturePaneSubtree);
		}
		return (owner.perspective == Perspective::kThirdPerson ||
		        owner.perspective == Perspective::kVRFirstPerson) &&
		       owner.visibleModelAssetIdentity ==
			       owner.captureModelAssetIdentity &&
		       owner.visiblePartClone == owner.capturePartClone &&
		       owner.visibleMirrorItemSubtree ==
			       owner.captureMirrorItemSubtree &&
		       owner.visiblePaneSubtree == owner.capturePaneSubtree;
	}

	[[nodiscard]] constexpr bool IsValidStableGeneration(
		const StableCandidateGeneration generation) noexcept
	{
		return generation.equip != 0 && generation.root != 0 &&
		       generation.clone != 0;
	}

	[[nodiscard]] constexpr bool RequiresStableGenerationAdvance(
		const StableGenerationEvent event) noexcept
	{
		switch (event) {
		case StableGenerationEvent::kEquip:
		case StableGenerationEvent::kUnequip:
		case StableGenerationEvent::kPerspectiveRootChanged:
		case StableGenerationEvent::kBipedRootRebuilt:
		case StableGenerationEvent::kPartCloneReplaced:
		case StableGenerationEvent::kRaceOrSexModelRebuilt:
		case StableGenerationEvent::kAuthoredSurfaceIdentityChanged:
		case StableGenerationEvent::kGameLoadInvalidation: return true;
		case StableGenerationEvent::kUnknown: return true;
		case StableGenerationEvent::kPoseOnly: return false;
		}
		return false;
	}

	struct Float3
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };

		constexpr bool operator==(const Float3&) const noexcept = default;
	};

	struct PanePoseGeometry
	{
		Float3 center{};
		Float3 normal{};
		Float3 tangent{};
		Float3 bitangent{};
		float halfWidth{ 0.0F };
		float halfHeight{ 0.0F };
		float frontClearance{ 0.0F };
		float backClearance{ 0.0F };

		constexpr bool operator==(const PanePoseGeometry&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsFinite(const float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000U) !=
		       0x7F800000U;
	}

	[[nodiscard]] constexpr bool IsSanePoseGeometry(
		const PanePoseGeometry& geometry) noexcept
	{
		const auto finite3 = [](const Float3 value) constexpr noexcept {
			return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
		};
		if (!finite3(geometry.center) || !finite3(geometry.normal) ||
			!finite3(geometry.tangent) || !finite3(geometry.bitangent) ||
			!IsFinite(geometry.halfWidth) || !IsFinite(geometry.halfHeight) ||
			!IsFinite(geometry.frontClearance) ||
			!IsFinite(geometry.backClearance)) {
			return false;
		}
		if (geometry.halfWidth <
				MirrorAuthoringContract::kMinimumApertureHalfExtent ||
			geometry.halfWidth >
				MirrorAuthoringContract::kMaximumApertureHalfExtent ||
			geometry.halfHeight <
				MirrorAuthoringContract::kMinimumApertureHalfExtent ||
			geometry.halfHeight >
				MirrorAuthoringContract::kMaximumApertureHalfExtent ||
			geometry.halfWidth > geometry.halfHeight *
				MirrorAuthoringContract::kMaximumApertureAspectRatio ||
			geometry.halfHeight > geometry.halfWidth *
				MirrorAuthoringContract::kMaximumApertureAspectRatio ||
			geometry.frontClearance <
				MirrorAuthoringContract::kMinimumClearance ||
			geometry.frontClearance >
				MirrorAuthoringContract::kMaximumClearance ||
			geometry.backClearance <
				MirrorAuthoringContract::kMinimumClearance ||
			geometry.backClearance >
				MirrorAuthoringContract::kMaximumClearance) {
			return false;
		}
		const MirrorAuthoringContract::InstanceObservation observation{
			.worldAxes = {
				.normal = { geometry.normal.x, geometry.normal.y, geometry.normal.z },
				.tangent = {
					geometry.tangent.x, geometry.tangent.y, geometry.tangent.z },
				.bitangent = { geometry.bitangent.x, geometry.bitangent.y,
					geometry.bitangent.z } },
			.uniformScale = 1.0F
		};
		const MirrorAuthoringContract::Aperture aperture{
			geometry.halfWidth, geometry.halfHeight, geometry.frontClearance,
			geometry.backClearance
		};
		MirrorAuthoringContract::ValidatedInstance validated{};
		return MirrorAuthoringContract::ValidateV2Instance(
			observation, aperture, validated) ==
			MirrorAuthoringContract::RejectionReason::kNone;
	}

	struct PoseObservation
	{
		StableCandidateGeneration stableGeneration{};
		std::uint64_t poseSequence{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		std::uintptr_t observedPaneSubtree{ 0 };
		AuthoredSurfaceIdentity authoredSurface{};
		PanePoseGeometry geometry{};

		constexpr bool operator==(const PoseObservation&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidPoseObservation(
		const PoseObservation observation) noexcept
	{
		return IsValidStableGeneration(observation.stableGeneration) &&
		       observation.poseSequence != 0 && observation.mainViewFrame != 0 &&
		       observation.observedPaneSubtree != 0 &&
		       IsValidAuthoredSurfaceIdentity(observation.authoredSurface) &&
		       IsSanePoseGeometry(observation.geometry);
	}

	[[nodiscard]] constexpr bool IsNewerPoseForSameCandidate(
		const PoseObservation previous,
		const PoseObservation current) noexcept
	{
		return IsValidPoseObservation(previous) && IsValidPoseObservation(current) &&
		       previous.stableGeneration == current.stableGeneration &&
		       previous.authoredSurface == current.authoredSurface &&
		       current.poseSequence > previous.poseSequence &&
		       current.mainViewFrame >= previous.mainViewFrame;
	}

	struct BipedPartCloneIdentity
	{
		std::uintptr_t item{ 0 };
		std::uintptr_t armorAddon{ 0 };
		std::uintptr_t partClone{ 0 };
		std::uintptr_t mirrorItemSubtree{ 0 };
		std::uintptr_t paneSubtree{ 0 };
		std::uint64_t modelAssetIdentity{ 0 };
		AuthoredSurfaceIdentity authoredSurface{};

		constexpr bool operator==(
			const BipedPartCloneIdentity&) const noexcept = default;
	};

	struct EquippedCandidateEvidence
	{
		HandMirrorOwnerIdentity ownerIdentity{};
		IdentityAuthority authority{ IdentityAuthority::kUnknown };
		Perspective perspective{ Perspective::kUnknown };
		StableCandidateGeneration stableGeneration{};
		std::uint32_t playerFormID{ 0 };
		std::uint64_t playerHandleToken{ 0 };
		std::uintptr_t exactOwnedArmorPluginIdentity{ 0 };
		std::uint32_t exactOwnedArmorLocalFormID{ 0 };
		std::uintptr_t exactOwnedArmorAddonPluginIdentity{ 0 };
		std::uint32_t exactOwnedArmorAddonLocalFormID{ 0 };
		std::uint32_t equippedBipedSlotNumber{ 0 };
		std::uintptr_t playerActor{ 0 };
		std::uintptr_t exactOwnedArmorBase{ 0 };
		std::uintptr_t exactOwnedArmorAddon{ 0 };
		std::uintptr_t visiblePerspectiveRoot{ 0 };
		std::uintptr_t thirdPersonPlayerRoot{ 0 };
		BipedPartCloneIdentity visibleBipedObject{};
		BipedPartCloneIdentity privateCaptureBipedObject{};
		std::size_t activeShieldSlotCandidateCount{ 0 };
		bool actorIsExactPlayer{ false };
		bool exactOwnedArmorResolvedByPluginAndLocalIdentity{ false };
		bool exactOwnedArmorAddonBelongsToArmorAndCurrentRace{ false };
		bool wornArmorAtShieldSlotMatchesExactArmor{ false };
		bool equippedObjectAtShieldSlotMatchesExactArmor{ false };
		bool nativeEquippedStateRevalidatedAfterWakeup{ false };
		bool visibleBipedObjectIsCorrectForPerspective{ false };
		bool privateCaptureBipedObjectIsThirdPerson{ false };
		bool visiblePartCloneDescendsExactPerspectiveRoot{ false };
		bool capturePartCloneDescendsThirdPersonPlayerRoot{ false };
		bool visibleMirrorItemDescendsExactVisiblePartClone{ false };
		bool captureMirrorItemDescendsExactCapturePartClone{ false };
		bool visiblePaneDescendsExactMirrorItemSubtree{ false };
		bool capturePaneDescendsExactMirrorItemSubtree{ false };
		bool visibleAndCaptureClonesMatchCurrentNativeEquipEpoch{ false };
		bool authoredSurfaceIdentityResolvedFromFrozenContentLock{ false };
		bool visibleModelAssetMatchesApprovedPerspectiveAndSex{ false };
		bool captureModelAssetMatchesApprovedThirdPersonAndSex{ false };
		bool thirdPersonCaptureClonePoseCurrentForMainFrame{ false };
		bool inventoryPreviewClone{ false };
		bool droppedWorldClone{ false };
		bool menuOrDisplayClone{ false };
		bool publicRegistrationOrUnownedMarkerUsed{ false };
	};

	[[nodiscard]] constexpr bool IsExactBipedObject(
		const BipedPartCloneIdentity& object,
		const std::uintptr_t armor,
		const std::uintptr_t armorAddon) noexcept
	{
		return armor != 0 && armorAddon != 0 && object.item == armor &&
		       object.armorAddon == armorAddon && object.partClone != 0 &&
		       object.mirrorItemSubtree != 0 && object.paneSubtree != 0 &&
		       object.partClone != object.mirrorItemSubtree &&
		       object.partClone != object.paneSubtree &&
		       object.mirrorItemSubtree != object.paneSubtree &&
		       object.modelAssetIdentity != 0 &&
		       IsValidAuthoredSurfaceIdentity(object.authoredSurface);
	}

	[[nodiscard]] constexpr bool AreCloneSubtreesDisjoint(
		const BipedPartCloneIdentity& left,
		const BipedPartCloneIdentity& right) noexcept
	{
		return AreIdentityTriplesDisjoint(
			left.partClone, left.mirrorItemSubtree, left.paneSubtree,
			right.partClone, right.mirrorItemSubtree, right.paneSubtree);
	}

	[[nodiscard]] constexpr RejectionReason ValidateEquippedCandidate(
		const EquippedCandidateEvidence& evidence) noexcept
	{
		if (evidence.authority != IdentityAuthority::kFirstPartyInternal ||
			evidence.publicRegistrationOrUnownedMarkerUsed ||
			!evidence.exactOwnedArmorResolvedByPluginAndLocalIdentity ||
			!evidence.exactOwnedArmorAddonBelongsToArmorAndCurrentRace) {
			return RejectionReason::kFirstPartyIdentity;
		}
		if (!IsValidStableGeneration(evidence.stableGeneration))
			return RejectionReason::kStableGeneration;
		if (!IsValidAuthoredSurfaceIdentity(
				evidence.visibleBipedObject.authoredSurface) ||
			!IsValidAuthoredSurfaceIdentity(
				evidence.privateCaptureBipedObject.authoredSurface) ||
			evidence.visibleBipedObject.authoredSurface !=
				evidence.privateCaptureBipedObject.authoredSurface ||
			!evidence.authoredSurfaceIdentityResolvedFromFrozenContentLock ||
			!evidence.visibleModelAssetMatchesApprovedPerspectiveAndSex ||
			!evidence.captureModelAssetMatchesApprovedThirdPersonAndSex) {
			return RejectionReason::kPaneContract;
		}
		const HandMirrorOwnerIdentity expectedOwner{
			evidence.playerFormID,
			evidence.playerHandleToken,
			evidence.exactOwnedArmorPluginIdentity,
			evidence.exactOwnedArmorLocalFormID,
			evidence.exactOwnedArmorBase,
			evidence.exactOwnedArmorAddonPluginIdentity,
			evidence.exactOwnedArmorAddonLocalFormID,
			evidence.exactOwnedArmorAddon,
			evidence.equippedBipedSlotNumber,
			evidence.stableGeneration,
			evidence.visibleBipedObject.authoredSurface,
			evidence.visibleBipedObject.modelAssetIdentity,
			evidence.privateCaptureBipedObject.modelAssetIdentity,
			evidence.perspective,
			evidence.visibleBipedObject.partClone,
			evidence.privateCaptureBipedObject.partClone,
			evidence.visibleBipedObject.mirrorItemSubtree,
			evidence.privateCaptureBipedObject.mirrorItemSubtree,
			evidence.visibleBipedObject.paneSubtree,
			evidence.privateCaptureBipedObject.paneSubtree
		};
		if (evidence.ownerIdentity != expectedOwner)
			return RejectionReason::kBipedObjectIdentity;
		if (evidence.playerFormID == 0 || evidence.playerHandleToken == 0 ||
			evidence.exactOwnedArmorPluginIdentity == 0 ||
			evidence.exactOwnedArmorLocalFormID == 0 ||
			evidence.exactOwnedArmorAddonPluginIdentity == 0 ||
			evidence.exactOwnedArmorAddonLocalFormID == 0 ||
			evidence.equippedBipedSlotNumber != kDisplayedShieldSlotNumber ||
			evidence.playerActor == 0 || !evidence.actorIsExactPlayer ||
			evidence.activeShieldSlotCandidateCount != 1 ||
			!evidence.wornArmorAtShieldSlotMatchesExactArmor ||
			!evidence.equippedObjectAtShieldSlotMatchesExactArmor ||
			!evidence.nativeEquippedStateRevalidatedAfterWakeup) {
			return RejectionReason::kPlayerOrEquipState;
		}
		if (!IsExactBipedObject(
				evidence.visibleBipedObject, evidence.exactOwnedArmorBase,
				evidence.exactOwnedArmorAddon) ||
			!IsExactBipedObject(
				evidence.privateCaptureBipedObject,
				evidence.exactOwnedArmorBase,
				evidence.exactOwnedArmorAddon) ||
			!evidence.visibleAndCaptureClonesMatchCurrentNativeEquipEpoch) {
			return RejectionReason::kBipedObjectIdentity;
		}
		if (evidence.perspective == Perspective::kUnknown ||
			evidence.visiblePerspectiveRoot == 0 ||
			evidence.thirdPersonPlayerRoot == 0 ||
			!evidence.visibleBipedObjectIsCorrectForPerspective ||
			!evidence.privateCaptureBipedObjectIsThirdPerson ||
			!evidence.visiblePartCloneDescendsExactPerspectiveRoot ||
			!evidence.capturePartCloneDescendsThirdPersonPlayerRoot ||
			!evidence.visibleMirrorItemDescendsExactVisiblePartClone ||
			!evidence.captureMirrorItemDescendsExactCapturePartClone ||
			!evidence.visiblePaneDescendsExactMirrorItemSubtree ||
			!evidence.capturePaneDescendsExactMirrorItemSubtree ||
			!evidence.thirdPersonCaptureClonePoseCurrentForMainFrame) {
			return RejectionReason::kPerspectiveRoot;
		}
		if (evidence.perspective == Perspective::kFirstPerson &&
			(evidence.visiblePerspectiveRoot == evidence.thirdPersonPlayerRoot ||
			 !AreCloneSubtreesDisjoint(
				 evidence.visibleBipedObject,
				 evidence.privateCaptureBipedObject) ||
			 evidence.visibleBipedObject.modelAssetIdentity ==
				 evidence.privateCaptureBipedObject.modelAssetIdentity)) {
			return RejectionReason::kPerspectiveRoot;
		}
		if ((evidence.perspective == Perspective::kThirdPerson ||
			evidence.perspective == Perspective::kVRFirstPerson) &&
			(evidence.visiblePerspectiveRoot != evidence.thirdPersonPlayerRoot ||
			 evidence.visibleBipedObject !=
				 evidence.privateCaptureBipedObject)) {
			return RejectionReason::kPerspectiveRoot;
		}
		if (evidence.inventoryPreviewClone || evidence.droppedWorldClone ||
			evidence.menuOrDisplayClone) {
			return RejectionReason::kNonGameplayClone;
		}
		return RejectionReason::kNone;
	}

	struct RaisedPoseEvidence
	{
		PoseObservation pose{};
		std::uintptr_t exactExpectedVisiblePaneSubtree{ 0 };
		bool actorIsBlocking{ false };
		bool shieldIsRaisedInCurrentPose{ false };
		bool exactPerspectiveBipedResolvedAfterAnimationUpdate{ false };
		bool paneBelongsToCurrentPerspectiveRoot{ false };
		bool poseSampledForCurrentDeliveryOpportunity{ false };
		bool paneWorldTransformFinite{ false };
		bool paneSchemaAndApertureRevalidatedAtPoseSample{ false };
		bool authoredSurfaceIdentityRevalidatedAtPoseSample{ false };
		bool renderCameraOnReflectiveFrontSide{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidateRaisedPose(
		const EquippedCandidateEvidence& candidate,
		const RaisedPoseEvidence& evidence) noexcept
	{
		const auto candidateReason = ValidateEquippedCandidate(candidate);
		if (candidateReason != RejectionReason::kNone)
			return candidateReason;
		if (!evidence.actorIsBlocking || !evidence.shieldIsRaisedInCurrentPose)
			return RejectionReason::kNotRaised;
		if (!IsValidPoseObservation(evidence.pose) ||
			evidence.pose.stableGeneration != candidate.stableGeneration ||
			evidence.pose.observedPaneSubtree !=
				candidate.visibleBipedObject.paneSubtree ||
			evidence.pose.authoredSurface !=
				candidate.ownerIdentity.authoredSurface ||
			evidence.exactExpectedVisiblePaneSubtree !=
				candidate.visibleBipedObject.paneSubtree ||
			!evidence.paneWorldTransformFinite ||
			!evidence.paneSchemaAndApertureRevalidatedAtPoseSample ||
			!evidence.authoredSurfaceIdentityRevalidatedAtPoseSample ||
			!evidence.renderCameraOnReflectiveFrontSide) {
			return RejectionReason::kPoseObservation;
		}
		if (!evidence.exactPerspectiveBipedResolvedAfterAnimationUpdate ||
			!evidence.paneBelongsToCurrentPerspectiveRoot ||
			!evidence.poseSampledForCurrentDeliveryOpportunity) {
			return RejectionReason::kPoseObservation;
		}
		return RejectionReason::kNone;
	}

	struct PostPublicationPaneDrawEvidence
	{
		PoseObservation pose{};
		std::uintptr_t exactExpectedVisiblePaneSubtree{ 0 };
		bool publicationReadyBeforeDraw{ false };
		bool exactPaneSubmittedByCurrentMainPlayerView{ false };
		bool submissionBelongsToCurrentPerspectiveRoot{ false };
		bool submissionWasVisibleAndNotAppCulled{ false };
		bool submissionWasNotInventoryMenuShadowOrPrivatePass{ false };
		bool rendererConsumedExactGenerationAndPose{ false };
		bool drawCompletedInSameMainViewFrame{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidatePostPublicationPaneDraw(
		const EquippedCandidateEvidence& candidate,
		const RaisedPoseEvidence& raised,
		const PostPublicationPaneDrawEvidence& draw) noexcept
	{
		if (ValidateEquippedCandidate(candidate) != RejectionReason::kNone ||
			draw.pose != raised.pose ||
			draw.exactExpectedVisiblePaneSubtree !=
				candidate.visibleBipedObject.paneSubtree ||
			!draw.publicationReadyBeforeDraw ||
			!draw.exactPaneSubmittedByCurrentMainPlayerView ||
			!draw.submissionBelongsToCurrentPerspectiveRoot ||
			!draw.submissionWasVisibleAndNotAppCulled ||
			!draw.submissionWasNotInventoryMenuShadowOrPrivatePass ||
			!draw.rendererConsumedExactGenerationAndPose ||
			!draw.drawCompletedInSameMainViewFrame) {
			return RejectionReason::kMainViewSubmission;
		}
		return RejectionReason::kNone;
	}

	enum class MirrorOwnerSelection : std::uint8_t
	{
		kDark,
		kWall,
		kHand
	};

	struct HandWallArbitrationInput
	{
		MirrorOwnerSelection currentOwner{ MirrorOwnerSelection::kDark };
		HandMirrorOwnerIdentity candidateHandOwner{};
		HandMirrorOwnerIdentity accumulatedHandOwner{};
		HandMirrorOwnerIdentity currentHandOwner{};
		std::uint32_t consecutiveRaisedVisibleHandFrames{ 0 };
		std::uint32_t consecutiveHandIneligibleFrames{ 0 };
		bool exactHandEligible{ false };
		bool exactWallEligible{ false };
		bool ownerIdentityAmbiguous{ false };
	};

	[[nodiscard]] constexpr MirrorOwnerSelection SelectMirrorOwner(
		const HandWallArbitrationInput input) noexcept
	{
		if (input.ownerIdentityAmbiguous)
			return MirrorOwnerSelection::kDark;
		if (input.exactHandEligible) {
			if (!IsValidHandMirrorOwnerIdentity(input.candidateHandOwner))
				return MirrorOwnerSelection::kDark;
			if ((input.currentOwner == MirrorOwnerSelection::kHand &&
				 input.currentHandOwner == input.candidateHandOwner) ||
				(input.accumulatedHandOwner == input.candidateHandOwner &&
				 input.consecutiveRaisedVisibleHandFrames >=
					kHandAcquireHysteresisFrames)) {
				return MirrorOwnerSelection::kHand;
			}
			return input.exactWallEligible ? MirrorOwnerSelection::kWall :
				MirrorOwnerSelection::kDark;
		}
		if (input.currentOwner == MirrorOwnerSelection::kHand &&
			input.consecutiveHandIneligibleFrames <
				kHandReleaseHysteresisFrames) {
			return MirrorOwnerSelection::kDark;
		}
		return input.exactWallEligible ? MirrorOwnerSelection::kWall :
			MirrorOwnerSelection::kDark;
	}

	struct SingletonChannelEvidence
	{
		std::size_t publicationChannelCapacity{ 0 };
		std::size_t activePublicationOwnerCount{ 0 };
		std::size_t competingPublicationOwnerCount{ 0 };
		HandWallArbitrationInput arbitrationInput{};
		MirrorOwnerSelection arbitrationResult{ MirrorOwnerSelection::kDark };
		HandMirrorOwnerIdentity selectedOwner{};
		HandMirrorOwnerIdentity captureOwner{};
		HandMirrorOwnerIdentity publicationOwner{};
		HandMirrorOwnerIdentity drawOwner{};
		bool publicAPIV1PublicationExcluded{ false };
		bool noWallOrOtherHandMirrorOwnsTheChannel{ false };
		bool darkFallbackSelectedOnTieOrUncertainty{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidateSingletonChannel(
		const HandMirrorOwnerIdentity& expectedOwner,
		const SingletonChannelEvidence& evidence) noexcept
	{
		if (evidence.publicationChannelCapacity !=
				kMirrorPublicationChannelCapacity ||
			evidence.activePublicationOwnerCount != 1 ||
			evidence.competingPublicationOwnerCount != 0 ||
			evidence.arbitrationResult !=
				SelectMirrorOwner(evidence.arbitrationInput) ||
			evidence.arbitrationResult != MirrorOwnerSelection::kHand ||
			evidence.arbitrationInput.candidateHandOwner != expectedOwner ||
			evidence.selectedOwner != expectedOwner ||
			evidence.captureOwner != expectedOwner ||
			evidence.publicationOwner != expectedOwner ||
			evidence.drawOwner != expectedOwner ||
			!evidence.publicAPIV1PublicationExcluded ||
			!evidence.noWallOrOtherHandMirrorOwnsTheChannel ||
			!evidence.darkFallbackSelectedOnTieOrUncertainty) {
			return RejectionReason::kSingletonOwnership;
		}
		return RejectionReason::kNone;
	}

	struct PrivateMirrorItemSuppressionEvidence
	{
		StableCandidateGeneration stableGeneration{};
		std::uintptr_t exactExpectedCaptureMirrorItemSubtree{ 0 };
		std::uintptr_t requestedSuppressionSubtree{ 0 };
		std::uintptr_t exactCapturePaneSubtree{ 0 };
		std::uintptr_t exactCapturePartClone{ 0 };
		std::uintptr_t playerRoot{ 0 };
		std::uintptr_t firstPersonRoot{ 0 };
		std::uintptr_t thirdPersonRoot{ 0 };
		bool targetIsExactSmallestAuthoredMirrorItemSubtree{ false };
		bool targetDescendsExactCapturePartClone{ false };
		bool targetContainsExactAuthoredFrameAndPane{ false };
		bool targetContainsPlayerRoot{ false };
		bool targetContainsFirstPersonRoot{ false };
		bool targetContainsThirdPersonRoot{ false };
		bool currentPoseSampleCompletedBeforeSuppression{ false };
		bool priorAppCullStateRead{ false };
		bool priorAppCulled{ false };
		std::uint64_t mutationLeaseToken{ 0 };
		std::uint64_t restoreLeaseToken{ 0 };
		bool exclusiveMutationOwnershipProven{ false };
		bool stateChangedByThisAttempt{ false };
		bool privateAppCullAppliedAndObserved{ false };
		bool ambiguousConcurrentWriteObserved{ false };
		bool cleanupAttemptedOnEveryExitPath{ false };
		bool restorePerformedByThisAttempt{ false };
		bool restoredAppCulled{ false };
		bool postCleanupReadbackMatchesPriorState{ false };
		bool suppressionFaultLatched{ false };
		bool noOtherSubtreeMutated{ false };
		bool cleanupCompletedBeforePublication{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidatePrivateMirrorItemSuppression(
		const EquippedCandidateEvidence& candidate,
		const PrivateMirrorItemSuppressionEvidence& evidence) noexcept
	{
		if (ValidateEquippedCandidate(candidate) != RejectionReason::kNone ||
			evidence.stableGeneration != candidate.stableGeneration ||
			evidence.exactExpectedCaptureMirrorItemSubtree !=
				candidate.privateCaptureBipedObject.mirrorItemSubtree ||
			evidence.requestedSuppressionSubtree !=
				candidate.privateCaptureBipedObject.mirrorItemSubtree ||
			evidence.exactCapturePaneSubtree !=
				candidate.privateCaptureBipedObject.paneSubtree ||
			evidence.exactCapturePartClone !=
				candidate.privateCaptureBipedObject.partClone ||
			evidence.requestedSuppressionSubtree == 0 ||
			evidence.playerRoot == 0 || evidence.thirdPersonRoot == 0 ||
			evidence.requestedSuppressionSubtree == evidence.playerRoot ||
			evidence.requestedSuppressionSubtree == evidence.firstPersonRoot ||
			evidence.requestedSuppressionSubtree == evidence.thirdPersonRoot ||
			!evidence.targetIsExactSmallestAuthoredMirrorItemSubtree ||
			!evidence.targetDescendsExactCapturePartClone ||
			!evidence.targetContainsExactAuthoredFrameAndPane ||
			evidence.targetContainsPlayerRoot ||
			evidence.targetContainsFirstPersonRoot ||
			evidence.targetContainsThirdPersonRoot) {
			return RejectionReason::kSuppressionTarget;
		}
		if (!evidence.currentPoseSampleCompletedBeforeSuppression ||
			!evidence.priorAppCullStateRead ||
			evidence.mutationLeaseToken == 0 ||
			evidence.restoreLeaseToken != evidence.mutationLeaseToken ||
			!evidence.exclusiveMutationOwnershipProven ||
			!evidence.privateAppCullAppliedAndObserved ||
			evidence.ambiguousConcurrentWriteObserved ||
			evidence.suppressionFaultLatched ||
			!evidence.noOtherSubtreeMutated) {
			return RejectionReason::kSuppressionOrdering;
		}
		const bool cleanMutationHistory = evidence.priorAppCulled ?
			(!evidence.stateChangedByThisAttempt &&
			 !evidence.restorePerformedByThisAttempt) :
			(evidence.stateChangedByThisAttempt &&
			 evidence.restorePerformedByThisAttempt);
		if (!cleanMutationHistory ||
			!evidence.cleanupAttemptedOnEveryExitPath ||
			evidence.restoredAppCulled != evidence.priorAppCulled ||
			!evidence.postCleanupReadbackMatchesPriorState ||
			!evidence.cleanupCompletedBeforePublication) {
			return RejectionReason::kSuppressionRestore;
		}
		return RejectionReason::kNone;
	}

	struct PlayerSelfInclusionEvidence
	{
		StableCandidateGeneration stableGeneration{};
		std::uintptr_t exactThirdPersonPlayerRoot{ 0 };
		std::uintptr_t submittedPrivatePlayerRoot{ 0 };
		bool privateCaptureUsesThirdPersonPlayerBody{ false };
		bool thirdPersonPlayerRootSubmittedToPrivateView{ false };
		bool firstPersonRootExcludedFromPrivateView{ false };
		bool exactCaptureMirrorItemSubtreeSuppressed{ false };
		bool playerRootNeverAppCulledForMirrorSuppression{ false };
		bool otherPlayerBodySubtreesRemainSubmitted{ false };
		bool playerDrawObservedOnExactPrivateTarget{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidatePlayerSelfInclusion(
		const EquippedCandidateEvidence& candidate,
		const PlayerSelfInclusionEvidence& evidence) noexcept
	{
		if (ValidateEquippedCandidate(candidate) != RejectionReason::kNone ||
			evidence.stableGeneration != candidate.stableGeneration ||
			evidence.exactThirdPersonPlayerRoot !=
				candidate.thirdPersonPlayerRoot ||
			evidence.submittedPrivatePlayerRoot !=
				candidate.thirdPersonPlayerRoot ||
			!evidence.privateCaptureUsesThirdPersonPlayerBody ||
			!evidence.thirdPersonPlayerRootSubmittedToPrivateView ||
			!evidence.firstPersonRootExcludedFromPrivateView ||
			!evidence.exactCaptureMirrorItemSubtreeSuppressed ||
			!evidence.playerRootNeverAppCulledForMirrorSuppression ||
			!evidence.otherPlayerBodySubtreesRemainSubmitted ||
			!evidence.playerDrawObservedOnExactPrivateTarget) {
			return RejectionReason::kPlayerSelfInclusion;
		}
		return RejectionReason::kNone;
	}

	struct FrameDeliveryEvidence
	{
		EquippedCandidateEvidence candidate{};
		RaisedPoseEvidence raisedPose{};
		PostPublicationPaneDrawEvidence paneDraw{};
		SingletonChannelEvidence singleton{};
		PrivateMirrorItemSuppressionEvidence suppression{};
		PlayerSelfInclusionEvidence playerInclusion{};
		StableCandidateGeneration generationAtPublication{};
		PoseObservation poseAtPublication{};
		DeliveryOpportunity deliveryOpportunity{
			DeliveryOpportunity::kUnknown };
		bool captureCompletedBeforeVisiblePaneDraw{ false };
		bool visiblePaneDrawCompletedAfterPublication{ false };
		bool oneFrameOldPosePublicationRejected{ false };
		bool candidateRevalidatedAfterPrivateRender{ false };
		bool poseStillMatchesExactSubmittedMainViewObservation{ false };
		bool allPrivateStateRestoredBeforePublication{ false };
		bool darkFallbackOnAnyMismatch{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidateFrameDelivery(
		const FrameDeliveryEvidence& evidence) noexcept
	{
		auto reason = ValidateRaisedPose(
			evidence.candidate, evidence.raisedPose);
		if (reason != RejectionReason::kNone)
			return reason;
		reason = ValidateSingletonChannel(
			evidence.candidate.ownerIdentity, evidence.singleton);
		if (reason != RejectionReason::kNone)
			return reason;
		reason = ValidatePrivateMirrorItemSuppression(
			evidence.candidate, evidence.suppression);
		if (reason != RejectionReason::kNone)
			return reason;
		reason = ValidatePlayerSelfInclusion(
			evidence.candidate, evidence.playerInclusion);
		if (reason != RejectionReason::kNone)
			return reason;
		const bool firstPersonOpportunity =
			evidence.candidate.perspective == Perspective::kFirstPerson &&
			evidence.deliveryOpportunity ==
				DeliveryOpportunity::kPostWorldBeforeFirstPerson;
		const bool thirdPersonOpportunity =
			evidence.candidate.perspective == Perspective::kThirdPerson &&
			(evidence.deliveryOpportunity ==
					DeliveryOpportunity::kPreWorldBeforeThirdPerson ||
			 evidence.deliveryOpportunity ==
					DeliveryOpportunity::kPostCaptureThirdPersonOverlay);
		if ((!firstPersonOpportunity && !thirdPersonOpportunity) ||
			!evidence.captureCompletedBeforeVisiblePaneDraw ||
			!evidence.visiblePaneDrawCompletedAfterPublication ||
			!evidence.oneFrameOldPosePublicationRejected) {
			return RejectionReason::kDeliveryOpportunity;
		}
		if (evidence.generationAtPublication !=
				evidence.candidate.stableGeneration ||
			evidence.poseAtPublication != evidence.raisedPose.pose ||
			!evidence.candidateRevalidatedAfterPrivateRender ||
			!evidence.poseStillMatchesExactSubmittedMainViewObservation ||
			!evidence.allPrivateStateRestoredBeforePublication ||
			!evidence.darkFallbackOnAnyMismatch) {
			return RejectionReason::kPoseObservation;
		}
		reason = ValidatePostPublicationPaneDraw(
			evidence.candidate, evidence.raisedPose, evidence.paneDraw);
		if (reason != RejectionReason::kNone)
			return reason;
		return RejectionReason::kNone;
	}

	struct ReloadRescanEvidence
	{
		StableCandidateGeneration invalidatedGeneration{};
		StableCandidateGeneration reacquiredGeneration{};
		std::size_t serializedTransientPointerCount{ 0 };
		std::size_t serializedHandMirrorRegistryRowCount{ 0 };
		bool nativeInventoryAndEquipStateLoadedFirst{ false };
		bool oldCloneAndPanePointersInvalidated{ false };
		bool oldPublicationLeaseRetired{ false };
		bool equipAndBothPerspectiveBipedsRescanned{ false };
		bool exactOwnedArmorAndAddonIdentityReacquired{ false };
		bool exactPartCloneIdentityReacquired{ false };
		bool raisedPoseAndPostPublicationDrawMustBeObservedAgain{ false };
		bool mirrorRemainsDarkUntilFreshAdmission{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidateReloadRescan(
		const ReloadRescanEvidence& evidence) noexcept
	{
		if (!IsValidStableGeneration(evidence.invalidatedGeneration) ||
			!IsValidStableGeneration(evidence.reacquiredGeneration) ||
			evidence.invalidatedGeneration == evidence.reacquiredGeneration ||
			evidence.serializedTransientPointerCount != 0 ||
			evidence.serializedHandMirrorRegistryRowCount != 0 ||
			!evidence.nativeInventoryAndEquipStateLoadedFirst ||
			!evidence.oldCloneAndPanePointersInvalidated ||
			!evidence.oldPublicationLeaseRetired ||
			!evidence.equipAndBothPerspectiveBipedsRescanned ||
			!evidence.exactOwnedArmorAndAddonIdentityReacquired ||
			!evidence.exactPartCloneIdentityReacquired ||
			!evidence.raisedPoseAndPostPublicationDrawMustBeObservedAgain ||
			!evidence.mirrorRemainsDarkUntilFreshAdmission) {
			return RejectionReason::kReloadRescan;
		}
		return RejectionReason::kNone;
	}

	struct MerchantDistributionEvidence
	{
		DistributionLane lane{ DistributionLane::kNone };
		IdentityAuthority itemAuthority{ IdentityAuthority::kUnknown };
		std::size_t exactTargetReferenceCount{ 0 };
		bool exactOwnedArmorIdentityAssigned{ false };
		bool targetReferencesAndContainerBasesAudited{ false };
		bool merchantServiceLinkVerified{ false };
		bool resetAndRestockBehaviorVerified{ false };
		bool stockCountChanceAndGoldValueApproved{ false };
		bool adapterTransportAndRuntimeVersionPinned{ false };
		bool dependencyDeclaredIfRequired{ false };
		bool directMasterContainerOverrideAbsent{ false };
		bool runtimeBaseContainerMutationAbsent{ false };
		bool actorAndLeveledListInjectionAbsent{ false };
		bool craftingRecipeAbsent{ false };
	};

	[[nodiscard]] constexpr RejectionReason ValidateMerchantDistributionPlan(
		const MerchantDistributionEvidence& evidence) noexcept
	{
		if (evidence.lane !=
			DistributionLane::kOptionalExactMerchantAdapter) {
			return RejectionReason::kDistributionLane;
		}
		if (evidence.itemAuthority != IdentityAuthority::kFirstPartyInternal ||
			evidence.exactTargetReferenceCount != kAuditedMerchantTargetCount ||
			!evidence.exactOwnedArmorIdentityAssigned ||
			!evidence.targetReferencesAndContainerBasesAudited ||
			!evidence.merchantServiceLinkVerified ||
			!evidence.resetAndRestockBehaviorVerified ||
			!evidence.stockCountChanceAndGoldValueApproved ||
			!evidence.adapterTransportAndRuntimeVersionPinned ||
			!evidence.dependencyDeclaredIfRequired ||
			!evidence.directMasterContainerOverrideAbsent ||
			!evidence.runtimeBaseContainerMutationAbsent ||
			!evidence.actorAndLeveledListInjectionAbsent ||
			!evidence.craftingRecipeAbsent) {
			return RejectionReason::kDistributionEvidence;
		}
		return RejectionReason::kNone;
	}

	[[nodiscard]] constexpr bool CanAuthorizeHandMirrorContentEmission(
		const HandMirrorContentEvidence& evidence) noexcept
	{
		return ValidateContentShape(evidence) == RejectionReason::kNone &&
		       kOwnedFormIDsAssigned && kPlayerFacingNamesApproved &&
		       kEconomyValuesApproved && kWorldInventoryModelsAuthored &&
		       kFirstPersonModelsAuthored && kThirdPersonModelsAuthored &&
		       kContentBuilderIntegrationWired && kContentEmissionAuthorized;
	}

	[[nodiscard]] constexpr bool CanEnableHandMirrorRuntime(
		const HandMirrorContentEvidence& content,
		const FrameDeliveryEvidence& frame) noexcept
	{
		return CanAuthorizeHandMirrorContentEmission(content) &&
		       kContentPluginEmitted &&
		       ValidateFrameDelivery(frame) == RejectionReason::kNone &&
		       kEquipEventIntegrationWired && kBipedPartCloneResolverWired &&
		       kMainViewSubmissionObserverWired && kHandMirrorPoseResolverWired &&
		       kSameFrameMovingSurfaceDeliveryWired &&
		       kPrivateMirrorItemSubtreeSuppressionWired &&
		       kPlayerSelfInclusionWired &&
		       kSingletonMirrorArbiterIntegrationWired &&
		       kSaveLoadEquipRescanIntegrationWired && kRuntimeIntegrationWired &&
		       kRuntimeAcceptanceGranted;
	}

	[[nodiscard]] constexpr bool CanEmitMerchantDistribution(
		const MerchantDistributionEvidence& evidence) noexcept
	{
		return ValidateMerchantDistributionPlan(evidence) ==
				   RejectionReason::kNone &&
		       kContentPluginEmitted && kMerchantTargetsAudited &&
		       kMerchantServiceAndRestockSemanticsVerified &&
		       kDistributionAdapterVersionPinned &&
		       kDistributionAdapterIntegrationWired && kDistributionEmitted;
	}
}
