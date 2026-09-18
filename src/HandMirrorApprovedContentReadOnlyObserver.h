#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

#include <DirectXMath.h>

#include "HandMirrorApprovedContentReadOnlyGatePolicy.h"
#include "HandMirrorContentRuntimePolicy.h"
#include "HandMirrorFrameReceiptPolicy.h"
#include "HandMirrorRuntimeBridgePolicy.h"
#include "PlanarMirrorMath.h"

namespace RE
{
	class BSRenderPass;
	class NiAVObject;
	class TESForm;
	class TESObjectARMA;
}

namespace HandMirrorApprovedContentReadOnlyObserver
{
	inline constexpr std::wstring_view kEnableMarker =
		L"RealisticReflections_HandMirrorApprovedContentReadOnlyObserver.enable";
	inline constexpr std::wstring_view kLivePosePresentationMarker =
		L"RealisticReflections_HandMirrorLivePose.enable";
	inline constexpr std::wstring_view kVRRuntimeFixMarker =
		L"RealisticReflections_VRHandMirrorRuntimeFix.enable";
	inline constexpr std::string_view kFiligreeV1PluginBasename =
		"MirrorsOfSkyrim.esp";
	inline constexpr std::uint32_t kFiligreeV1ArmorLocalFormID = 0x900;
	inline constexpr std::uint32_t kFiligreeV1ArmorAddonLocalFormID = 0x901;
	// TESFile::GetRuntimeFormID expects a raw file-qualified form ID, unlike
	// TESDataHandler::LookupForm which consumes the local ID.  In a plugin's raw
	// records the high byte indexes the file's MAST list: values below the master
	// count select that master, and the master count itself is the file's own
	// slot (TESFile::GetRuntimeFormID: masterIndex < masterCount -> masterPtrs).
	// The add-on lists the five official masters and the fixed-mirror core,
	// so its own ARMO/ARMA are raw 06000900/06000901; the retired hand ESP
	// owned 01000900/01000901.  The 2026-09-09 AE run pinned 0x01 after the
	// consolidation and failed exact form validation with every hash passing
	// (rawSelf=01000800->runtime=01000800 lookupEqualsRaw=false), so the index
	// is now an input read from the validated source file, never a constant.
	inline constexpr std::uint32_t kFiligreeV1PluginMasterCount = 6;
	// Raw indexes 0xFF (light-plugin prefix) and 0xFE cannot be a file's own slot.
	inline constexpr std::uint32_t kTESFileMaximumMasterCount = 0xFD;
	inline constexpr std::uint32_t kFiligreeV1AllFrozenFilesVerifiedMask = 0x1F;

	[[nodiscard]] constexpr bool TESFileMasterCountUsable(
		const std::uint32_t masterCount) noexcept
	{
		return masterCount <= kTESFileMaximumMasterCount;
	}

	[[nodiscard]] constexpr std::uint32_t TESFileRawSelfFormID(
		const std::uint32_t masterCount,
		const std::uint32_t localFormID) noexcept
	{
		return ((masterCount & 0xFFU) << 24) | (localFormID & 0x00FFFFFFU);
	}

	[[nodiscard]] constexpr bool SharedRawHookRequested(
		const bool legacyRawMarker,
		const bool approvedContentMarker) noexcept
	{
		return legacyRawMarker || approvedContentMarker;
	}

	/**
	 * Request only the already-owned RenderWorld/fence fanout needed by the
	 * approved observer.  This is deliberately independent from ordinary M3
	 * capture authority; the approved marker may install dormant owners but may
	 * never make a wall/material channel eligible.
	 */
	[[nodiscard]] constexpr bool SharedRenderOwnersRequested(
		const bool legacySecondViewMarker,
		const bool productSecondViewRequest,
		const bool approvedContentMarker) noexcept
	{
		return legacySecondViewMarker || productSecondViewRequest ||
			approvedContentMarker;
	}

	[[nodiscard]] constexpr bool OrdinarySecondViewCaptureRequested(
		const bool legacySecondViewMarker,
		const bool productSecondViewRequest) noexcept
	{
		return legacySecondViewMarker || productSecondViewRequest;
	}

	[[nodiscard]] constexpr bool FrozenContentReady(
		const std::uint32_t verifiedFileMask,
		const bool exactFormsPinned) noexcept
	{
		return verifiedFileMask == kFiligreeV1AllFrozenFilesVerifiedMask &&
			exactFormsPinned;
	}

	/** Bounded normal-return cohort for one outer first-person call. */
	enum class ExactPaneReturnCohort : std::uint8_t
	{
		kZero,
		kOne,
		kTwoSameImmutableIdentity,
		kRejected
	};

	/**
	 * Observe one exact pane normal return. The first return stages its copied
	 * value. A second is accepted only under the same immutable receipt, owner,
	 * pane, and target identity; its pass-local pose/presentation then replaces
	 * the first. An immutable mismatch or any third/later return is sticky
	 * rejection.
	 */
	[[nodiscard]] constexpr ExactPaneReturnCohort ObserveExactPaneReturn(
		const ExactPaneReturnCohort current,
		const bool sameImmutableIdentity) noexcept
	{
		switch (current) {
		case ExactPaneReturnCohort::kZero:
			return ExactPaneReturnCohort::kOne;
		case ExactPaneReturnCohort::kOne:
			return sameImmutableIdentity ?
				ExactPaneReturnCohort::kTwoSameImmutableIdentity :
				ExactPaneReturnCohort::kRejected;
		case ExactPaneReturnCohort::kTwoSameImmutableIdentity:
		case ExactPaneReturnCohort::kRejected:
		default:
			return ExactPaneReturnCohort::kRejected;
		}
	}

	[[nodiscard]] constexpr bool AcceptsExactPaneReturnCohort(
		const ExactPaneReturnCohort cohort) noexcept
	{
		return cohort == ExactPaneReturnCohort::kOne ||
			cohort == ExactPaneReturnCohort::kTwoSameImmutableIdentity;
	}

	[[nodiscard]] constexpr bool SameMainTargetIdentityBits(
		const HandMirrorApprovedContentReadOnlyGatePolicy::MainTargetIdentity& left,
		const HandMirrorApprovedContentReadOnlyGatePolicy::MainTargetIdentity& right)
		noexcept
	{
		return left.deviceIdentity == right.deviceIdentity &&
			left.colorViewIdentity == right.colorViewIdentity &&
			left.colorResourceIdentity == right.colorResourceIdentity &&
			left.depthViewIdentity == right.depthViewIdentity &&
			left.depthResourceIdentity == right.depthResourceIdentity &&
			left.viewportIdentity == right.viewportIdentity &&
			left.viewportWidth == right.viewportWidth &&
			left.viewportHeight == right.viewportHeight &&
			std::bit_cast<std::uint32_t>(left.minimumDepth) ==
				std::bit_cast<std::uint32_t>(right.minimumDepth) &&
			std::bit_cast<std::uint32_t>(left.maximumDepth) ==
				std::bit_cast<std::uint32_t>(right.maximumDepth);
	}

	/**
	 * Compare immutable identity for two individually observed normal returns.
	 * Snapshot tokens are per-callback nonces and must be distinct and correctly
	 * paired. Pane transform and viewport presentation are deliberately omitted:
	 * the latest accepted return owns those values until exact outer close.
	 */
	[[nodiscard]] constexpr bool SameExactPaneNormalReturnImmutableIdentity(
		const HandMirrorApprovedContentReadOnlyGatePolicy::
			ExactFirstPersonPaneDrawEvidence& first,
		const HandMirrorApprovedContentReadOnlyGatePolicy::
			ExactFirstPersonPaneDrawEvidence& later) noexcept
	{
		if (first.pose.snapshotToken == 0 || later.pose.snapshotToken == 0 ||
			first.pose.snapshotToken == later.pose.snapshotToken ||
			first.target.paneDrawSnapshotToken != first.pose.snapshotToken ||
			later.target.paneDrawSnapshotToken != later.pose.snapshotToken) {
			return false;
		}

		return first.pose.sourceFrame == later.pose.sourceFrame &&
			first.pose.phase == later.pose.phase &&
			first.pose.binding == later.pose.binding &&
			first.target.sourceFrame == later.target.sourceFrame &&
			HandMirrorApprovedContentReadOnlyGatePolicy::
				SameMainTargetResourceIdentity(
				first.target.expectedMainTarget,
				later.target.expectedMainTarget) &&
			HandMirrorApprovedContentReadOnlyGatePolicy::
				SameMainTargetResourceIdentity(
				first.target.observedAtPaneDrawReturn,
				later.target.observedAtPaneDrawReturn) &&
			first.target.prospectivePrivateColorResourceIdentity ==
				later.target.prospectivePrivateColorResourceIdentity &&
			first.target.prospectivePrivateDepthResourceIdentity ==
				later.target.prospectivePrivateDepthResourceIdentity;
	}

	enum class RuntimePreFreshnessReject : std::uint16_t
	{
		kNone = 0,
		kExactTuple = 1u << 0,
		kCurrentRaceAndSex = 1u << 1,
		kFirstPersonPerspective = 1u << 2,
		// Bit 3 was the falsified gameplay-blocking authority.  Keep the wire/log
		// position reserved so archived masks cannot be silently reinterpreted.
		kReservedFormerBlockingIntent = 1u << 3,
		kFirstPersonSchema = 1u << 4,
		kThirdPersonSchema = 1u << 5,
		kVisibleFirstPersonPane = 1u << 6,
		kExactFirstPersonPanePass = 1u << 7,
		kReceiptInvalid = 1u << 8,
		kGraphicsFrameMismatch = 1u << 9,
		kOwnerIdentity = 1u << 10,
		kCandidateRead = 1u << 11,
		kCandidatePairMismatch = 1u << 12,
		kLifecycleRefresh = 1u << 13
	};

	using RuntimePreFreshnessRejectMask = std::uint16_t;

	[[nodiscard]] constexpr RuntimePreFreshnessRejectMask Mask(
		const RuntimePreFreshnessReject value) noexcept
	{
		return static_cast<RuntimePreFreshnessRejectMask>(value);
	}

	struct RuntimePreFreshnessEvidence
	{
		bool exactTupleEquipped{ false };
		bool currentRaceAndSexExact{ false };
		bool firstPersonPerspective{ false };
		bool firstPersonSchemaExact{ false };
		bool thirdPersonSchemaExact{ false };
		bool firstPersonPaneVisibleAndNotAppCulled{ false };
		bool exactPassRequired{ false };
		bool exactPassIsFirstPersonPane{ false };
	};

	/**
	 * The native exact-pane observer always requires a visible, non-app-culled
	 * pane.  The enclosing first-person zero-callback seam has no native pane
	 * draw by definition, so its separate observer may waive that one signal
	 * while retaining every identity, schema, freshness, and receipt gate.
	 */
	enum class RuntimePaneVisibilityRequirement : std::uint8_t
	{
		kRequireVisibleAndNotAppCulled,
		kAllowAppCulledOnlyForOuterZeroCallback,
		kAllowAppCulledOnlyForPostCaptureRevalidation
	};

	[[nodiscard]] constexpr bool AcceptRuntimePaneVisibility(
		const bool visibleAndNotAppCulled,
		const RuntimePaneVisibilityRequirement requirement) noexcept
	{
		return visibleAndNotAppCulled ||
			requirement == RuntimePaneVisibilityRequirement::
				kAllowAppCulledOnlyForOuterZeroCallback ||
			requirement == RuntimePaneVisibilityRequirement::
				kAllowAppCulledOnlyForPostCaptureRevalidation;
	}

	[[nodiscard]] constexpr RuntimePreFreshnessRejectMask
		ClassifyRuntimePreFreshness(
			const RuntimePreFreshnessEvidence& evidence) noexcept
	{
		RuntimePreFreshnessRejectMask rejects = 0;
		if (!evidence.exactTupleEquipped)
			rejects |= Mask(RuntimePreFreshnessReject::kExactTuple);
		if (!evidence.currentRaceAndSexExact)
			rejects |= Mask(RuntimePreFreshnessReject::kCurrentRaceAndSex);
		if (!evidence.firstPersonPerspective)
			rejects |= Mask(RuntimePreFreshnessReject::kFirstPersonPerspective);
		if (!evidence.firstPersonSchemaExact)
			rejects |= Mask(RuntimePreFreshnessReject::kFirstPersonSchema);
		if (!evidence.thirdPersonSchemaExact)
			rejects |= Mask(RuntimePreFreshnessReject::kThirdPersonSchema);
		if (!evidence.firstPersonPaneVisibleAndNotAppCulled) {
			rejects |= Mask(RuntimePreFreshnessReject::kVisibleFirstPersonPane);
		}
		if (evidence.exactPassRequired &&
			!evidence.exactPassIsFirstPersonPane) {
			rejects |= Mask(
				RuntimePreFreshnessReject::kExactFirstPersonPanePass);
		}
		return rejects;
	}

	[[nodiscard]] constexpr RuntimePreFreshnessRejectMask
		ClassifyRuntimePreFreshnessForVisibility(
			RuntimePreFreshnessEvidence evidence,
			const RuntimePaneVisibilityRequirement requirement) noexcept
	{
		evidence.firstPersonPaneVisibleAndNotAppCulled =
			AcceptRuntimePaneVisibility(
				evidence.firstPersonPaneVisibleAndNotAppCulled, requirement);
		return ClassifyRuntimePreFreshness(evidence);
	}

	[[nodiscard]] constexpr bool HasRuntimePreFreshnessReject(
		const RuntimePreFreshnessRejectMask rejects,
		const RuntimePreFreshnessReject value) noexcept
	{
		return (rejects & Mask(value)) != 0;
	}

	struct FrozenAssetContract
	{
		std::string_view role{};
		std::string_view modelPath{};
		std::uint64_t byteLength{ 0 };
		std::array<std::uint8_t, 32> sha256{};
	};

	inline constexpr std::uint64_t kFiligreeV1PluginByteLength = 9052;
	inline constexpr std::array<std::uint8_t, 32>
		kFiligreeV1PluginSHA256{
			0x40, 0x1C, 0x2E, 0xC1, 0x9D, 0xE1, 0xE2, 0xEE,
			0x5C, 0x59, 0xED, 0x7D, 0x93, 0x10, 0x39, 0x71,
			0xA3, 0xEA, 0x70, 0x83, 0x6A, 0x8C, 0x4A, 0x14,
			0xA7, 0x8E, 0xE9, 0xD5, 0x2E, 0x68, 0x51, 0xBB
		};

	// One full add-on plugin is byte-identical on SE, AE and VR.
	inline constexpr auto kFiligreeV1PluginSHA256VR = kFiligreeV1PluginSHA256;

	// ARMA MOD4/MOD5 are first person; MOD2/MOD3 are third person.
	// V179 (2026-09-10): the first-person roll is removed so the drawn mirror
	// stands fully upright in the grip; the 4.5-unit right shift is kept and
	// third-person, geometry and materials are unchanged from V178.
	// Reproduce with tools/orient_hand_mirror_upright_nifs.py and the frozen
	// prototypes/hand-mirror/gilded-noble-round-filigree-upright-v3/source inputs,
	// passing --first-person-right-shift 4.5 --first-person-roll-degrees 0, then
	// tools/repair_hand_mirror_pane.py on the output directory.
	// The stable order maps the bounded sex/perspective selector directly to the
	// four shipping equipped roles. The pane repair keeps those poses and adds
	// an opaque native material plus a 0.03-unit overlap under the frame.
	// The third-person roles additionally contain an untagged black backing;
	// reproduce with tools/add_hand_mirror_backing.py from opaque-backing-v1/source.
	inline constexpr std::array<FrozenAssetContract, 4>
		kFiligreeV1EquippedAssets{
			FrozenAssetContract{
				"male-first-person",
				"mirrors_of_skyrim\\hand_mirror\\gilded_noble_round_filigree_v1\\male-first-person.nif",
				163734,
				{ 0x5A, 0xFE, 0x7E, 0x2A, 0x45, 0xF5, 0x48, 0x49,
					0x39, 0xB4, 0xF2, 0x61, 0x31, 0xEC, 0x64, 0x3F,
					0xC5, 0xB3, 0x45, 0xFA, 0x6D, 0x08, 0x75, 0xB5,
					0xB3, 0x82, 0x3C, 0xCB, 0x97, 0xEC, 0xC0, 0x9B } },
			FrozenAssetContract{
				"female-first-person",
				"mirrors_of_skyrim\\hand_mirror\\gilded_noble_round_filigree_v1\\female-first-person.nif",
				163736,
				{ 0x9D, 0x9D, 0xD3, 0x48, 0x89, 0x77, 0x1B, 0x96,
					0x0B, 0xF4, 0xC7, 0xA5, 0x83, 0xDF, 0xE8, 0x1A,
					0x83, 0xA5, 0xF2, 0x3F, 0xA8, 0x80, 0xEF, 0x0D,
					0x0F, 0x08, 0xA0, 0xFC, 0xE9, 0x0F, 0x33, 0xE8 } },
			FrozenAssetContract{
				"male-third-person",
				"mirrors_of_skyrim\\hand_mirror\\gilded_noble_round_filigree_v1\\male-third-person.nif",
				164959,
				{ 0xEB, 0x4D, 0xB0, 0x64, 0xDC, 0xE7, 0x91, 0xBE,
					0x95, 0xF9, 0x21, 0x62, 0x05, 0x35, 0xEA, 0x65,
					0x57, 0x2E, 0xAE, 0x8A, 0x5C, 0x52, 0x6A, 0x36,
					0x2F, 0xE5, 0xAC, 0x19, 0x00, 0x42, 0x1A, 0x01 } },
			FrozenAssetContract{
				"female-third-person",
				"mirrors_of_skyrim\\hand_mirror\\gilded_noble_round_filigree_v1\\female-third-person.nif",
				164961,
				{ 0x45, 0x46, 0x31, 0x0B, 0xE5, 0xE7, 0x29, 0x9E,
					0x5B, 0x4B, 0xE8, 0xBF, 0xBF, 0x65, 0x74, 0xB3,
					0xE4, 0x54, 0x5B, 0x1E, 0xA3, 0x28, 0x3E, 0xF1,
					0x14, 0xCE, 0x4C, 0x9C, 0xAC, 0x0E, 0xB5, 0x0D } }
		};

	// VR retains its existing VRIK grip transforms. Only MirrorFrame cap topology
	// and 0.02-unit internal face recesses differ; the four exact hashes remain pinned.
	inline constexpr auto kFiligreeV1EquippedAssetsVR = [] {
		auto assets = kFiligreeV1EquippedAssets;
		// VR keeps its separately pinned existing assets until its own test.
		assets[0].byteLength = assets[2].byteLength = 163791;
		assets[1].byteLength = assets[3].byteLength = 163793;
		assets[0].sha256 = {
			0x90, 0x18, 0x18, 0xDF, 0x59, 0xB0, 0xD0, 0x61,
			0x90, 0x1A, 0xB4, 0x46, 0x5F, 0xC0, 0xC7, 0x1D,
			0x21, 0xDA, 0xCC, 0xFB, 0xC2, 0x85, 0x4B, 0x6F,
			0x75, 0x9C, 0x55, 0xC1, 0x45, 0xEE, 0x39, 0xBF };
		assets[1].sha256 = {
			0x58, 0xB2, 0x41, 0x54, 0x17, 0x1C, 0x51, 0x51,
			0x76, 0x22, 0x74, 0x24, 0x41, 0x5C, 0x59, 0x42,
			0x7E, 0xBD, 0x24, 0xE7, 0xA4, 0x8E, 0x3C, 0xD4,
			0x0F, 0x49, 0xB7, 0x9C, 0xB9, 0xD2, 0x6C, 0x9F };
		assets[2].sha256 = {
			0xFD, 0xF9, 0xB9, 0x71, 0x10, 0xB5, 0x97, 0x1C,
			0x24, 0x04, 0x8A, 0x91, 0x35, 0x26, 0xF2, 0x39,
			0x51, 0xE2, 0xFA, 0x2D, 0xAE, 0x5C, 0x23, 0xCA,
			0x52, 0xAD, 0x70, 0xFD, 0x4D, 0x83, 0xBD, 0x8A };
		assets[3].sha256 = {
			0x2F, 0x99, 0x17, 0x15, 0xB3, 0x03, 0xF0, 0x6C,
			0x08, 0x79, 0xFA, 0xBC, 0xD2, 0x34, 0xD9, 0x04,
			0x84, 0xC0, 0x97, 0xDE, 0x2C, 0xAB, 0x2C, 0x4B,
			0x00, 0x11, 0x5C, 0x8B, 0x6E, 0x29, 0x36, 0xE0 };
		return assets;
	}();

	/** Value-only token spanning one call through the existing generic-draw owner. */
	struct GenericDrawToken
	{
		std::uint64_t sourceFrame{ 0 };
		std::uint64_t entrySequence{ 0 };
		std::uint64_t paneIdentity{ 0 };
		bool active{ false };
	};

	/**
	 * Complete value identity sampled twice from the exact equipped filigree clone.
	 * Engine pointers never enter this payload and may not outlive the callback
	 * supplied to RunWithExactRuntimeCandidate.
	 */
	struct RuntimeCandidateSnapshot
	{
		HandMirrorContentRuntimePolicy::EquippedCandidateEvidence candidate{};
		HandMirrorRuntimeBridgePolicy::MovingSurfaceSample surface{};
		PlanarMirrorMath::Plane reflectionPlane{};
		PlanarMirrorMath::PaneFit paneFit{};
		HandMirrorFrameReceiptPolicy::FrameReceipt receipt{};
		HandMirrorFrameReceiptPolicy::FreshnessEvidence hiddenFreshness{};
		bool exactPassIsFirstPersonPane{ false };
		bool hiddenThirdPersonFreshForGraphicsFrame{ false };
		// Retained as diagnostics for the two narrowly authorized non-draw
		// visibility waivers. It is never an identity or freshness substitute.
		bool paneVisibleAndNotAppCulled{ false };
		// Presentation hint only. A false or changing gameplay graph value may
		// select the physical lowered view, but can never reject an otherwise exact
		// equipped mirror or turn its pane dark.
		bool graphIndicatesRaisedPresentation{ false };
	};

	/**
	 * Value-only equipped identity for presentation continuity.  Unlike a capture
	 * candidate, this deliberately does not require a current hidden-third-person
	 * animation sample, a visible pane callback, or PlayerCamera's transient
	 * perspective bit.  It still proves the exact frozen forms/assets, both direct
	 * clone schemas, current race/sex, lifecycle generation, and equipped pane.
	 */
	struct RuntimeEquippedPresentationIdentity
	{
		HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity owner{};
		bool valid{ false };
	};

	/** Current first-person pane and animation mode only; never capture authority. */
	struct RuntimePresentationPose
	{
		HandMirrorRuntimeBridgePolicy::MovingSurfaceSample surface{};
		HandMirrorFrameReceiptPolicy::FrameReceipt receipt{};
		bool graphIndicatesRaisedPresentation{ false };
	};

	/**
	 * Same-call native frame-draw replay only. Exact equipment, transforms and
	 * frame receipt remain required; hidden third-person animation freshness is
	 * irrelevant when mapping an already completed image onto the current pane.
	 * No hidden-freshness evidence is supplied or changed. The pass must draw the
	 * exact equipped MirrorFrame; a late camera query alone cannot prove its raster.
	 */
	[[nodiscard]] bool TryReadFramePresentationPose(
		RE::BSRenderPass* framePass,
		const HandMirrorFrameReceiptPolicy::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		RuntimePresentationPose& output) noexcept;

	/**
	 * Equipment-only continuity result.  A transient clone/schema/read miss is
	 * deliberately distinct from a stable observation that neither shield slot
	 * contains any approved hand-mirror armor.
	 */
	enum class RuntimeEquippedPresentationStatus : std::uint8_t
	{
		kExactEquipped,
		kPositivelyUnequipped,
		kIndeterminate,
		kFaulted
	};

	/** Raw values are borrowed only during one guarded synchronous callback. */
	struct RuntimeBorrowedScene
	{
		RE::NiAVObject* firstPersonRoot{ nullptr };
		RE::NiAVObject* firstPersonPartClone{ nullptr };
		RE::NiAVObject* firstPersonMirrorItem{ nullptr };
		RE::NiAVObject* firstPersonPane{ nullptr };
		RE::NiAVObject* thirdPersonRoot{ nullptr };
		RE::NiAVObject* thirdPersonPartClone{ nullptr };
		RE::NiAVObject* thirdPersonMirrorItem{ nullptr };
		RE::NiAVObject* thirdPersonPane{ nullptr };
		// PlayerCharacter::Get3D1(false), distinct from thirdPersonRoot (the
		// BipedAnim root).  Retained for the complete synchronous callback so a
		// bounds transaction and MirrorPlayerInclusion can prove one render root.
		RE::NiAVObject* thirdPersonRenderRoot{ nullptr };
		// Exact loaded records re-resolved at the same guarded borrow boundary.
		// Candidate payload identities are deliberately opaque Mix64 tokens and
		// must never be reinterpreted as engine pointers by callback consumers.
		RE::TESForm* exactArmorItem{ nullptr };
		RE::TESObjectARMA* exactArmorAddon{ nullptr };
	};

	using RuntimeCandidateOperation = bool (*)(
		const RuntimeCandidateSnapshot& snapshot,
		const RuntimeBorrowedScene& borrowed,
		void* context) noexcept;

	using RuntimeEquippedPresentationOperation = bool (*)(
		const RuntimeEquippedPresentationIdentity& identity,
		void* context) noexcept;

	struct SuppressionSubtreeRevalidation
	{
		bool receiptAndHiddenFresh{ false };
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
	};

	/** Fresh guarded reread while the callback still retains every exact scene value. */
	[[nodiscard]] bool RevalidateBorrowedSuppressionSubtree(
		const RuntimeCandidateSnapshot& snapshot,
		const RuntimeBorrowedScene& borrowed,
		RE::NiAVObject* exactMutationTarget,
		SuppressionSubtreeRevalidation& output) noexcept;

	/**
	 * Reacquire, double-sample, strongly retain, invoke, and release the exact
	 * gameplay clone synchronously. A null pass selects the post-world capture
	 * phase; a non-null pass must be the exact first-person pane draw.
	 */
	[[nodiscard]] bool RunWithExactRuntimeCandidate(
		const HandMirrorFrameReceiptPolicy::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		RE::BSRenderPass* pass,
		RuntimeCandidateOperation operation,
		void* context) noexcept;

	/**
	 * Observe the exact equipped candidate at the enclosing first-person seam
	 * when Skyrim supplies no native pane callback.  This admits an app-culled
	 * pane only; it does not relax exact equip, schema/aperture, owner,
	 * generation, finite/front-side pose, hidden freshness, or frame receipt.
	 */
	[[nodiscard]] bool RunWithOuterFirstPersonRuntimeCandidate(
		const HandMirrorFrameReceiptPolicy::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		RuntimeCandidateOperation operation,
		void* context) noexcept;

	/**
	 * Reacquire the just-captured candidate before publication. The initial
	 * capture admission already proved a visible first-person pane for this
	 * receipt, so this non-draw seam may waive only a transient app-cull bit.
	 * Every equip, schema, owner, generation, hidden-freshness, and receipt gate
	 * remains identical to RunWithExactRuntimeCandidate.
	 */
	[[nodiscard]] bool RunWithPostCaptureRuntimeCandidate(
		const HandMirrorFrameReceiptPolicy::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		RuntimeCandidateOperation operation,
		void* context) noexcept;

	/**
	 * Double-sample and retain the exact equipped hand-mirror graph for an
	 * equipment-scoped last-good replay and established-owner continuity. It
	 * supplies no live pose or hidden-animation evidence and cannot by itself
	 * authorize capture execution, publication, or API exposure.
	 */
	[[nodiscard]] RuntimeEquippedPresentationStatus
	RunWithEquippedPresentationIdentity(
		RuntimeEquippedPresentationOperation operation,
		void* context) noexcept;

	void PrepareAtInputLoaded(bool reflectiveRuntimeDependency = false) noexcept;
	[[nodiscard]] bool Requested() noexcept;
	/** Startup-latched, default-off flat-runtime capture/presentation correction. */
	[[nodiscard]] bool LivePosePresentationEnabled() noexcept;
	[[nodiscard]] bool VRRuntimeFixEnabled() noexcept;
	[[nodiscard]] bool VRPresentationFixEnabled() noexcept;
	[[nodiscard]] bool VRViewCohortFixEnabled() noexcept;
	[[nodiscard]] bool VRSceneFixEnabled() noexcept;
	/** Exact current VRIK pane only; supplies no capture or publication authority. */
	[[nodiscard]] bool IsExactCurrentVRPanePass(RE::BSRenderPass* pass) noexcept;
	/** Caller owns the live node and its guarded read scope. */
	[[nodiscard]] std::uint32_t ReadNodeUpdateStamp(
		const RE::NiAVObject* object) noexcept;
	void OnDataLoaded() noexcept;
	void OnGameLoaded() noexcept;
	void OnPlayerEquipWake() noexcept;

	// Fanout only: no hook is installed by this module.
	// True while the bound colour/depth targets and viewport are the main
	// player view (exact device, main colour target writable).  Skyrim VR uses
	// it to tell the body's main-pass pane draw from shadow/private passes.
	[[nodiscard]] bool CurrentDrawTargetsMainView() noexcept;

	void OnPreWorld() noexcept;
	void OnWorldReturnedNormally() noexcept;
	void OnWorldFinally() noexcept;
	void OnFirstPersonEnter() noexcept;
	void OnFirstPersonReturnedNormally() noexcept;
	void OnFirstPersonFinally() noexcept;
	[[nodiscard]] GenericDrawToken OnGenericDrawEntry(
		RE::BSRenderPass* pass) noexcept;
	void OnGenericDrawReturnedNormally(const GenericDrawToken& token) noexcept;
	void FailStopCallbackFault() noexcept;

	[[nodiscard]] bool IsEnabled() noexcept;
	[[nodiscard]] bool IsFaultStopped() noexcept;
	void MaybeLogPeriodic() noexcept;
	void LogDiagnostics(const char* reason);
}
