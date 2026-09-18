#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace HandMirrorReadOnlyProbePolicy
{
	// The runtime observer is a separately default-off diagnostic.  It joins the
	// already-owned RenderWorld and generic-draw seams and owns only the uniquely
	// validated RenderFirstPersonView caller patch.  It still has no strict hand
	// admission, scene mutation, capture, publication, or engine-pointer ownership;
	// an ordinary vanilla-shield observation can never authorize a hand mirror.
	inline constexpr bool kRuntimeObserverWired = true;
	inline constexpr bool kOptInMarkerImplemented = true;
	inline constexpr bool kExistingHookJoinWired = true;
	inline constexpr bool kFirstPersonNormalReturnJoinWired = true;
	// RenderWorld scope alone cannot distinguish the exact main-view body draw
	// from every shadow-family draw.  Keep the third-person join fail closed until
	// an exact non-shadow main-view discriminator is proven.
	inline constexpr bool kThirdPersonMainViewClassificationWired = false;
	inline constexpr bool kStrictHandAdmissionWired = false;
	inline constexpr bool kStrictPaneDrawJoinWired = false;
	inline constexpr bool kHandMirrorContentExists = false;
	inline constexpr bool kDefaultEnabled = false;
	inline constexpr bool kInstallsHooks = true;
	inline constexpr bool kOwnsOnlyValidatedFirstPersonCallPatch = true;
	inline constexpr bool kRetainsEnginePointers = false;
	inline constexpr bool kMutatesScene = false;
	inline constexpr bool kCapturesReflection = false;
	inline constexpr bool kPublishesReflection = false;
	inline constexpr bool kRawDiagnosticCanAuthorizeHandMirror = false;

	inline constexpr std::size_t kRawTransformWordCount = 13;
	inline constexpr std::size_t kPhaseReadCapacity = 12;
	inline constexpr std::size_t kDrawObservationCapacity = 64;
	inline constexpr std::size_t kDrawJoinCapacity = 12;

	enum class RenderThreadGateStatus : std::uint8_t
	{
		kAccept,
		kCaptureUnset,
		kUnavailableUnset,
		kMismatch
	};

	// A callback observed before the trusted RenderWorld scope establishes its
	// thread owns no evidence and is merely unavailable.  Only a disagreement
	// with an already-established nonzero identity is a safety violation.
	[[nodiscard]] constexpr RenderThreadGateStatus ClassifyRenderThreadGate(
		const std::uint32_t expected,
		const std::uint32_t current,
		const bool captureIfUnset) noexcept
	{
		if (expected == 0) {
			return captureIfUnset ? RenderThreadGateStatus::kCaptureUnset :
				RenderThreadGateStatus::kUnavailableUnset;
		}
		return expected == current ? RenderThreadGateStatus::kAccept :
			RenderThreadGateStatus::kMismatch;
	}

	enum class MainViewPhase : std::uint8_t
	{
		kPreWorld,
		kInWorld,
		kPostWorld,
		kPreFirstPerson,
		kInFirstPerson,
		kPostFirstPerson,
		kCount
	};

	[[nodiscard]] constexpr std::uint8_t PhaseOrdinal(
		const MainViewPhase phase) noexcept
	{
		return static_cast<std::uint8_t>(phase);
	}

	struct PhaseStamp
	{
		std::uint64_t mainViewEpoch{ 0 };
		MainViewPhase phase{ MainViewPhase::kCount };
		// One-based redundant ordinal catches malformed or partially overwritten
		// telemetry before a sample is compared.
		std::uint8_t phaseSequence{ 0 };

		constexpr bool operator==(const PhaseStamp&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidPhaseStamp(
		const PhaseStamp& stamp) noexcept
	{
		return stamp.mainViewEpoch != 0 &&
			PhaseOrdinal(stamp.phase) < PhaseOrdinal(MainViewPhase::kCount) &&
			stamp.phaseSequence == PhaseOrdinal(stamp.phase) + 1u;
	}

	enum class PhaseTransitionStatus : std::uint8_t
	{
		kValidSameMainView,
		kValidNextMainView,
		kInvalidStamp,
		kSkippedOrReordered,
		kMainViewEpochMismatch,
		kMainViewEpochExhausted
	};

	[[nodiscard]] constexpr PhaseTransitionStatus ClassifyPhaseTransition(
		const PhaseStamp& previous,
		const PhaseStamp& current) noexcept
	{
		if (!IsValidPhaseStamp(previous) || !IsValidPhaseStamp(current))
			return PhaseTransitionStatus::kInvalidStamp;

		if (previous.phase != MainViewPhase::kPostFirstPerson) {
			if (current.mainViewEpoch != previous.mainViewEpoch)
				return PhaseTransitionStatus::kMainViewEpochMismatch;
			return PhaseOrdinal(current.phase) == PhaseOrdinal(previous.phase) + 1u ?
				PhaseTransitionStatus::kValidSameMainView :
				PhaseTransitionStatus::kSkippedOrReordered;
		}

		if (previous.mainViewEpoch ==
			(std::numeric_limits<std::uint64_t>::max)()) {
			return PhaseTransitionStatus::kMainViewEpochExhausted;
		}
		if (current.phase != MainViewPhase::kPreWorld)
			return PhaseTransitionStatus::kSkippedOrReordered;
		return current.mainViewEpoch == previous.mainViewEpoch + 1u ?
			PhaseTransitionStatus::kValidNextMainView :
			PhaseTransitionStatus::kMainViewEpochMismatch;
	}

	struct ProbeEpochs
	{
		// stable advances for equip/root/clone lifecycle changes.  pose is a
		// separate high-frequency value epoch and never advances stable by itself.
		std::uint64_t stable{ 0 };
		std::uint64_t equip{ 0 };
		std::uint64_t root{ 0 };
		std::uint64_t clone{ 0 };
		std::uint64_t pose{ 0 };

		constexpr bool operator==(const ProbeEpochs&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidEpochs(
		const ProbeEpochs& epochs) noexcept
	{
		return epochs.stable != 0 && epochs.equip != 0 && epochs.root != 0 &&
			epochs.clone != 0 && epochs.pose != 0;
	}

	[[nodiscard]] constexpr ProbeEpochs InitialEpochs() noexcept
	{
		return { 1, 1, 1, 1, 1 };
	}

	enum class EpochEvent : std::uint8_t
	{
		kEquipLifecycleChanged,
		kPerspectiveOrBipedRootChanged,
		kPartCloneOrRaceModelChanged,
		kPoseObserved,
		kGameLoadInvalidation,
		kUnknown
	};

	enum class EpochAdvanceStatus : std::uint8_t
	{
		kAdvanced,
		kInvalidCurrent,
		kUnknownEvent,
		kExhausted
	};

	struct EpochAdvanceResult
	{
		ProbeEpochs next{};
		EpochAdvanceStatus status{ EpochAdvanceStatus::kInvalidCurrent };

		[[nodiscard]] constexpr bool Succeeded() const noexcept
		{
			return status == EpochAdvanceStatus::kAdvanced;
		}

		[[nodiscard]] constexpr bool RequiresFailStop() const noexcept
		{
			return !Succeeded();
		}
	};

	[[nodiscard]] constexpr EpochAdvanceResult AdvanceEpochs(
		const ProbeEpochs& current,
		const EpochEvent event) noexcept
	{
		EpochAdvanceResult result{ current, EpochAdvanceStatus::kInvalidCurrent };
		if (!IsValidEpochs(current))
			return result;
		if (event == EpochEvent::kUnknown) {
			result.status = EpochAdvanceStatus::kUnknownEvent;
			return result;
		}

		const bool advanceStable = event != EpochEvent::kPoseObserved;
		const bool advanceEquip =
			event == EpochEvent::kEquipLifecycleChanged ||
			event == EpochEvent::kGameLoadInvalidation;
		const bool advanceRoot =
			event == EpochEvent::kPerspectiveOrBipedRootChanged ||
			event == EpochEvent::kGameLoadInvalidation;
		const bool advanceClone =
			event == EpochEvent::kPartCloneOrRaceModelChanged ||
			event == EpochEvent::kGameLoadInvalidation;
		const bool advancePose =
			event == EpochEvent::kPoseObserved ||
			event == EpochEvent::kGameLoadInvalidation;

		const auto exhausted = [](const std::uint64_t value,
			const bool advance) constexpr noexcept {
			return advance && value ==
				(std::numeric_limits<std::uint64_t>::max)();
		};
		if (exhausted(current.stable, advanceStable) ||
			exhausted(current.equip, advanceEquip) ||
			exhausted(current.root, advanceRoot) ||
			exhausted(current.clone, advanceClone) ||
			exhausted(current.pose, advancePose)) {
			result.status = EpochAdvanceStatus::kExhausted;
			return result;
		}

		result.next.stable += advanceStable ? 1u : 0u;
		result.next.equip += advanceEquip ? 1u : 0u;
		result.next.root += advanceRoot ? 1u : 0u;
		result.next.clone += advanceClone ? 1u : 0u;
		result.next.pose += advancePose ? 1u : 0u;
		result.status = EpochAdvanceStatus::kAdvanced;
		return result;
	}

	// Raw identities intentionally contain no plugin/local-form allowlist and no
	// mirror item-subtree or pane.  They can describe an ordinary vanilla shield.
	// Address-shaped fields are copied integers only; no operation dereferences
	// them and admitted telemetry explicitly requires no engine pointer retained.
	struct RawBipedObjectValueIdentity
	{
		std::uintptr_t itemAddressValue{ 0 };
		std::uintptr_t armorAddonAddressValue{ 0 };
		std::uintptr_t partCloneAddressValue{ 0 };

		constexpr bool operator==(
			const RawBipedObjectValueIdentity&) const noexcept = default;
	};

	struct RawShieldValueIdentity
	{
		std::uint32_t playerFormID{ 0 };
		std::uint64_t playerHandleToken{ 0 };
		std::uintptr_t playerActorAddressValue{ 0 };
		std::uintptr_t firstPersonBipedAddressValue{ 0 };
		std::uintptr_t thirdPersonBipedAddressValue{ 0 };
		std::uintptr_t firstPersonRootAddressValue{ 0 };
		std::uintptr_t thirdPersonRootAddressValue{ 0 };
		RawBipedObjectValueIdentity firstPersonShield{};
		RawBipedObjectValueIdentity thirdPersonShield{};

		constexpr bool operator==(
			const RawShieldValueIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsPresentRawBipedObject(
		const RawBipedObjectValueIdentity& object) noexcept
	{
		return object.itemAddressValue != 0 &&
			object.armorAddonAddressValue != 0 &&
			object.partCloneAddressValue != 0;
	}

	[[nodiscard]] constexpr bool IsValidRawShieldValueIdentity(
		const RawShieldValueIdentity& identity) noexcept
	{
		return identity.playerFormID != 0 && identity.playerHandleToken != 0 &&
			identity.playerActorAddressValue != 0 &&
			identity.firstPersonBipedAddressValue != 0 &&
			identity.thirdPersonBipedAddressValue != 0 &&
			identity.firstPersonBipedAddressValue !=
				identity.thirdPersonBipedAddressValue &&
			identity.firstPersonRootAddressValue != 0 &&
			identity.thirdPersonRootAddressValue != 0 &&
			identity.firstPersonRootAddressValue !=
				identity.thirdPersonRootAddressValue &&
			IsPresentRawBipedObject(identity.firstPersonShield) &&
			IsPresentRawBipedObject(identity.thirdPersonShield) &&
			identity.firstPersonShield.partCloneAddressValue !=
				identity.thirdPersonShield.partCloneAddressValue;
	}

	using RawTransformWords =
		std::array<std::uint32_t, kRawTransformWordCount>;

	inline constexpr std::uint64_t kTransformHashOffset =
		UINT64_C(14695981039346656037);
	inline constexpr std::uint64_t kTransformHashPrime = UINT64_C(1099511628211);

	[[nodiscard]] constexpr std::uint64_t HashRawTransformWords(
		const RawTransformWords& words) noexcept
	{
		std::uint64_t hash = kTransformHashOffset;
		for (const auto word : words) {
			// Fixed little-endian byte order keeps persisted evidence comparable.
			for (std::uint32_t shift = 0; shift < 32; shift += 8) {
				hash ^= static_cast<std::uint8_t>(word >> shift);
				hash *= kTransformHashPrime;
			}
		}
		return hash;
	}

	struct RawTransformSnapshot
	{
		RawTransformWords words{};
		std::uint64_t hash{ 0 };

		constexpr bool operator==(
			const RawTransformSnapshot&) const noexcept = default;
	};

	[[nodiscard]] constexpr RawTransformSnapshot MakeRawTransformSnapshot(
		const RawTransformWords& words) noexcept
	{
		return { words, HashRawTransformWords(words) };
	}

	[[nodiscard]] constexpr RawTransformSnapshot MakeRawTransformSnapshot(
		const std::array<float, kRawTransformWordCount>& values) noexcept
	{
		RawTransformWords words{};
		for (std::size_t index = 0; index < values.size(); ++index)
			words[index] = std::bit_cast<std::uint32_t>(values[index]);
		return MakeRawTransformSnapshot(words);
	}

	[[nodiscard]] constexpr bool IsSelfConsistentRawTransform(
		const RawTransformSnapshot& snapshot) noexcept
	{
		return snapshot.hash == HashRawTransformWords(snapshot.words);
	}

	[[nodiscard]] constexpr bool SameRawTransform(
		const RawTransformSnapshot& left,
		const RawTransformSnapshot& right) noexcept
	{
		// Hashes may accelerate a lookup, but exact words remain authoritative.
		return IsSelfConsistentRawTransform(left) &&
			IsSelfConsistentRawTransform(right) && left.hash == right.hash &&
			left.words == right.words;
	}

	struct RawBipedTransformValues
	{
		RawTransformSnapshot rootWorld{};
		RawTransformSnapshot partCloneWorld{};

		constexpr bool operator==(
			const RawBipedTransformValues&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsSelfConsistentRawBipedTransforms(
		const RawBipedTransformValues& values) noexcept
	{
		return IsSelfConsistentRawTransform(values.rootWorld) &&
			IsSelfConsistentRawTransform(values.partCloneWorld);
	}

	[[nodiscard]] constexpr bool SameRawBipedTransforms(
		const RawBipedTransformValues& left,
		const RawBipedTransformValues& right) noexcept
	{
		return SameRawTransform(left.rootWorld, right.rootWorld) &&
			SameRawTransform(left.partCloneWorld, right.partCloneWorld);
	}

	struct RawPhaseReadObservation
	{
		std::uint64_t readSequence{ 0 };
		PhaseStamp stamp{};
		RawShieldValueIdentity identity{};
		ProbeEpochs epochs{};
		RawBipedTransformValues firstPersonTransforms{};
		RawBipedTransformValues thirdPersonTransforms{};
		bool actorIsExactPlayer{ false };
		bool bothBipedObjectsRead{ false };
		bool bothShieldTuplesRead{ false };
		bool guardedReadCompleted{ false };
		bool guardedReadFaulted{ false };
		bool noEnginePointerRetained{ false };

		constexpr bool operator==(
			const RawPhaseReadObservation&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsStructurallyValidRawPhaseRead(
		const RawPhaseReadObservation& read) noexcept
	{
		return read.readSequence != 0 && IsValidPhaseStamp(read.stamp) &&
			IsValidRawShieldValueIdentity(read.identity) &&
			IsValidEpochs(read.epochs) &&
			IsSelfConsistentRawBipedTransforms(read.firstPersonTransforms) &&
			IsSelfConsistentRawBipedTransforms(read.thirdPersonTransforms) &&
			read.actorIsExactPlayer && read.bothBipedObjectsRead &&
			read.bothShieldTuplesRead && read.guardedReadCompleted &&
			!read.guardedReadFaulted && read.noEnginePointerRetained;
	}

	enum class RawDoubleReadStatus : std::uint8_t
	{
		kAdmittedDiagnosticOnly,
		kFirstReadInvalid,
		kSecondReadInvalid,
		kReadSequenceNotAdjacent,
		kPhaseChanged,
		kRawIdentityChanged,
		kLifecycleOrPoseEpochChanged,
		kFirstPersonTransformChanged,
		kThirdPersonTransformChanged
	};

	struct CoherentRawPhaseSample
	{
		RawDoubleReadStatus status{ RawDoubleReadStatus::kFirstReadInvalid };
		std::uint64_t firstReadSequence{ 0 };
		std::uint64_t secondReadSequence{ 0 };
		PhaseStamp stamp{};
		RawShieldValueIdentity identity{};
		ProbeEpochs epochs{};
		RawBipedTransformValues firstPersonTransforms{};
		RawBipedTransformValues thirdPersonTransforms{};

		[[nodiscard]] constexpr bool AdmittedDiagnosticOnly() const noexcept
		{
			return status == RawDoubleReadStatus::kAdmittedDiagnosticOnly;
		}
	};

	[[nodiscard]] constexpr CoherentRawPhaseSample AdmitCoherentRawDoubleRead(
		const RawPhaseReadObservation& first,
		const RawPhaseReadObservation& second) noexcept
	{
		CoherentRawPhaseSample result{};
		if (!IsStructurallyValidRawPhaseRead(first)) {
			result.status = RawDoubleReadStatus::kFirstReadInvalid;
			return result;
		}
		if (!IsStructurallyValidRawPhaseRead(second)) {
			result.status = RawDoubleReadStatus::kSecondReadInvalid;
			return result;
		}
		if (first.readSequence ==
				(std::numeric_limits<std::uint64_t>::max)() ||
			second.readSequence != first.readSequence + 1u) {
			result.status = RawDoubleReadStatus::kReadSequenceNotAdjacent;
			return result;
		}
		if (first.stamp != second.stamp) {
			result.status = RawDoubleReadStatus::kPhaseChanged;
			return result;
		}
		if (first.identity != second.identity) {
			result.status = RawDoubleReadStatus::kRawIdentityChanged;
			return result;
		}
		if (first.epochs != second.epochs) {
			result.status = RawDoubleReadStatus::kLifecycleOrPoseEpochChanged;
			return result;
		}
		if (!SameRawBipedTransforms(
				first.firstPersonTransforms, second.firstPersonTransforms)) {
			result.status = RawDoubleReadStatus::kFirstPersonTransformChanged;
			return result;
		}
		if (!SameRawBipedTransforms(
				first.thirdPersonTransforms, second.thirdPersonTransforms)) {
			result.status = RawDoubleReadStatus::kThirdPersonTransformChanged;
			return result;
		}

		result.status = RawDoubleReadStatus::kAdmittedDiagnosticOnly;
		result.firstReadSequence = first.readSequence;
		result.secondReadSequence = second.readSequence;
		result.stamp = second.stamp;
		result.identity = second.identity;
		result.epochs = second.epochs;
		result.firstPersonTransforms = second.firstPersonTransforms;
		result.thirdPersonTransforms = second.thirdPersonTransforms;
		return result;
	}

	enum class BipedPath : std::uint8_t
	{
		kFirstPerson,
		kThirdPerson,
		kUnknown
	};

	struct RawNormalReturnDrawObservation
	{
		std::uint64_t drawSequence{ 0 };
		PhaseStamp stamp{};
		BipedPath path{ BipedPath::kUnknown };
		RawShieldValueIdentity identity{};
		ProbeEpochs epochs{};
		RawBipedTransformValues transforms{};
		std::uintptr_t observedBipedAddressValue{ 0 };
		std::uintptr_t observedRootAddressValue{ 0 };
		std::uintptr_t observedPartCloneAddressValue{ 0 };
		bool drawResolvedToExactPartClone{ false };
		bool mainPlayerViewSubmission{ false };
		bool privateShadowMenuAndInventoryPassExcluded{ false };
		bool nativeDrawEntered{ false };
		bool nativeDrawReturnedNormally{ false };
		bool transformCopyCompletedAfterNormalReturn{ false };
		bool callbackFaulted{ false };
		bool noEnginePointerRetained{ false };
	};

	[[nodiscard]] constexpr bool IsValidRawNormalReturnDrawObservation(
		const RawNormalReturnDrawObservation& draw) noexcept
	{
		if (draw.drawSequence == 0 || !IsValidPhaseStamp(draw.stamp) ||
			draw.path == BipedPath::kUnknown ||
			!IsValidRawShieldValueIdentity(draw.identity) ||
			!IsValidEpochs(draw.epochs) ||
			!IsSelfConsistentRawBipedTransforms(draw.transforms) ||
			!draw.drawResolvedToExactPartClone ||
			!draw.mainPlayerViewSubmission ||
			!draw.privateShadowMenuAndInventoryPassExcluded ||
			!draw.nativeDrawEntered || !draw.nativeDrawReturnedNormally ||
			!draw.transformCopyCompletedAfterNormalReturn ||
			draw.callbackFaulted || !draw.noEnginePointerRetained) {
			return false;
		}

		const bool firstPerson = draw.path == BipedPath::kFirstPerson;
		const auto expectedBiped = firstPerson ?
			draw.identity.firstPersonBipedAddressValue :
			draw.identity.thirdPersonBipedAddressValue;
		const auto expectedRoot = firstPerson ?
			draw.identity.firstPersonRootAddressValue :
			draw.identity.thirdPersonRootAddressValue;
		const auto& expectedObject = firstPerson ?
			draw.identity.firstPersonShield : draw.identity.thirdPersonShield;
		return draw.observedBipedAddressValue == expectedBiped &&
			draw.observedRootAddressValue == expectedRoot &&
			draw.observedPartCloneAddressValue ==
				expectedObject.partCloneAddressValue;
	}

	enum class RawDrawJoinStatus : std::uint8_t
	{
		kJoinedDiagnosticBitExact,
		kSampleNotAdmitted,
		kDrawInvalidOrDidNotReturnNormally,
		kUnsupportedPhasePath,
		kMainViewChanged,
		kRawIdentityChanged,
		kStableEquipRootOrCloneEpochChanged,
		kPoseEpochChanged,
		kTransformBitsChanged
	};

	struct RawDrawJoinResult
	{
		RawDrawJoinStatus status{ RawDrawJoinStatus::kSampleNotAdmitted };
		std::uint64_t mainViewEpoch{ 0 };
		std::uint64_t drawSequence{ 0 };
		BipedPath path{ BipedPath::kUnknown };
		RawShieldValueIdentity identity{};
		std::uint64_t partCloneTransformHash{ 0 };

		[[nodiscard]] constexpr bool JoinedDiagnosticOnly() const noexcept
		{
			return status == RawDrawJoinStatus::kJoinedDiagnosticBitExact;
		}
	};

	[[nodiscard]] constexpr bool IsSupportedRawDrawJoinPhasePath(
		const MainViewPhase samplePhase,
		const MainViewPhase drawPhase,
		const BipedPath path) noexcept
	{
		if (path == BipedPath::kThirdPerson) {
			return samplePhase == MainViewPhase::kPreWorld &&
				drawPhase == MainViewPhase::kInWorld;
		}
		if (path == BipedPath::kFirstPerson) {
			return (samplePhase == MainViewPhase::kPostWorld ||
				samplePhase == MainViewPhase::kPreFirstPerson) &&
				drawPhase == MainViewPhase::kInFirstPerson;
		}
		return false;
	}

	[[nodiscard]] constexpr RawDrawJoinResult JoinRawNormalReturnDraw(
		const CoherentRawPhaseSample& sample,
		const RawNormalReturnDrawObservation& draw) noexcept
	{
		RawDrawJoinResult result{};
		if (!sample.AdmittedDiagnosticOnly()) {
			result.status = RawDrawJoinStatus::kSampleNotAdmitted;
			return result;
		}
		if (!IsValidRawNormalReturnDrawObservation(draw)) {
			result.status =
				RawDrawJoinStatus::kDrawInvalidOrDidNotReturnNormally;
			return result;
		}
		if (!IsSupportedRawDrawJoinPhasePath(
				sample.stamp.phase, draw.stamp.phase, draw.path)) {
			result.status = RawDrawJoinStatus::kUnsupportedPhasePath;
			return result;
		}
		if (sample.stamp.mainViewEpoch != draw.stamp.mainViewEpoch) {
			result.status = RawDrawJoinStatus::kMainViewChanged;
			return result;
		}
		if (sample.identity != draw.identity) {
			result.status = RawDrawJoinStatus::kRawIdentityChanged;
			return result;
		}
		if (sample.epochs.stable != draw.epochs.stable ||
			sample.epochs.equip != draw.epochs.equip ||
			sample.epochs.root != draw.epochs.root ||
			sample.epochs.clone != draw.epochs.clone) {
			result.status =
				RawDrawJoinStatus::kStableEquipRootOrCloneEpochChanged;
			return result;
		}
		if (sample.epochs.pose != draw.epochs.pose) {
			result.status = RawDrawJoinStatus::kPoseEpochChanged;
			return result;
		}

		const auto& expectedTransforms =
			draw.path == BipedPath::kFirstPerson ?
				sample.firstPersonTransforms : sample.thirdPersonTransforms;
		if (!SameRawBipedTransforms(expectedTransforms, draw.transforms)) {
			result.status = RawDrawJoinStatus::kTransformBitsChanged;
			return result;
		}

		result.status = RawDrawJoinStatus::kJoinedDiagnosticBitExact;
		result.mainViewEpoch = draw.stamp.mainViewEpoch;
		result.drawSequence = draw.drawSequence;
		result.path = draw.path;
		result.identity = draw.identity;
		result.partCloneTransformHash = draw.transforms.partCloneWorld.hash;
		return result;
	}

	[[nodiscard]] constexpr bool CanRawDiagnosticAuthorizeHandMirror(
		const CoherentRawPhaseSample& sample,
		const RawDrawJoinResult& drawJoin) noexcept
	{
		return kRawDiagnosticCanAuthorizeHandMirror &&
			sample.AdmittedDiagnosticOnly() && drawJoin.JoinedDiagnosticOnly();
	}

	// This separate future layer is where first-party form identity, the authored
	// mirror-item subtree, and the pane would be proven.  A successful raw join is
	// necessary evidence, never sufficient authority.
	struct FutureStrictHandAdmissionEvidence
	{
		RawDrawJoinResult rawJoin{};
		std::uintptr_t armorPluginIdentityValue{ 0 };
		std::uint32_t armorLocalFormID{ 0 };
		std::uintptr_t armorBaseAddressValue{ 0 };
		std::uintptr_t armorAddonPluginIdentityValue{ 0 };
		std::uint32_t armorAddonLocalFormID{ 0 };
		std::uintptr_t armorAddonBaseAddressValue{ 0 };
		std::uintptr_t mirrorItemSubtreeAddressValue{ 0 };
		std::uintptr_t paneAddressValue{ 0 };
		bool firstPartyFormsResolvedExactly{ false };
		bool subtreeDescendsExactPartClone{ false };
		bool paneDescendsExactSubtree{ false };
		bool schemaAndApertureValidated{ false };
		bool strictPaneNormalReturnJoinCompleted{ false };
	};

	[[nodiscard]] constexpr bool PassesFutureStrictHandAdmissionGate(
		const FutureStrictHandAdmissionEvidence& evidence) noexcept
	{
		const auto& rawObject =
			evidence.rawJoin.path == BipedPath::kFirstPerson ?
				evidence.rawJoin.identity.firstPersonShield :
				evidence.rawJoin.identity.thirdPersonShield;
		return evidence.rawJoin.JoinedDiagnosticOnly() &&
			evidence.armorPluginIdentityValue != 0 &&
			evidence.armorLocalFormID != 0 &&
			evidence.armorBaseAddressValue != 0 &&
			evidence.armorAddonPluginIdentityValue != 0 &&
			evidence.armorAddonLocalFormID != 0 &&
			evidence.armorAddonBaseAddressValue != 0 &&
			evidence.mirrorItemSubtreeAddressValue != 0 &&
			evidence.paneAddressValue != 0 &&
			evidence.mirrorItemSubtreeAddressValue !=
				evidence.paneAddressValue &&
			evidence.firstPartyFormsResolvedExactly &&
			rawObject.itemAddressValue == evidence.armorBaseAddressValue &&
			rawObject.armorAddonAddressValue ==
				evidence.armorAddonBaseAddressValue &&
			evidence.subtreeDescendsExactPartClone &&
			evidence.paneDescendsExactSubtree &&
			evidence.schemaAndApertureValidated &&
			evidence.strictPaneNormalReturnJoinCompleted;
	}

	[[nodiscard]] constexpr bool CanAuthorizeStrictHandMirror(
		const FutureStrictHandAdmissionEvidence& evidence) noexcept
	{
		return kHandMirrorContentExists && kStrictHandAdmissionWired &&
			kStrictPaneDrawJoinWired &&
			PassesFutureStrictHandAdmissionGate(evidence);
	}

	enum class ProbeFault : std::uint8_t
	{
		kNone,
		kPhaseReadOverflow,
		kDrawObservationOverflow,
		kDrawJoinOverflow,
		kPendingDrawOverflow,
		kGuardedReadFault,
		kDrawCallbackFault,
		kPhaseOrderFault,
		kEpochExhausted,
		kMalformedEvidence
	};

	struct EvidenceLedgerState
	{
		std::size_t phaseReads{ 0 };
		std::size_t drawObservations{ 0 };
		std::size_t drawJoins{ 0 };
		ProbeFault firstFault{ ProbeFault::kNone };
		bool overflowed{ false };
		bool faultStopped{ false };

		constexpr bool operator==(
			const EvidenceLedgerState&) const noexcept = default;
	};

	enum class EvidenceRecordKind : std::uint8_t
	{
		kPhaseRead,
		kDrawObservation,
		kDrawJoin
	};

	enum class LedgerTransitionStatus : std::uint8_t
	{
		kRecorded,
		kFailStopped,
		kOverflowFaultLatched
	};

	struct LedgerTransitionResult
	{
		EvidenceLedgerState next{};
		LedgerTransitionStatus status{ LedgerTransitionStatus::kFailStopped };
	};

	[[nodiscard]] constexpr EvidenceLedgerState LatchProbeFault(
		EvidenceLedgerState state,
		const ProbeFault fault) noexcept
	{
		if (fault == ProbeFault::kNone)
			return state;
		if (state.firstFault == ProbeFault::kNone)
			state.firstFault = fault;
		state.faultStopped = true;
		return state;
	}

	[[nodiscard]] constexpr LedgerTransitionResult RecordEvidence(
		const EvidenceLedgerState& current,
		const EvidenceRecordKind kind) noexcept
	{
		if (current.faultStopped)
			return { current, LedgerTransitionStatus::kFailStopped };

		auto next = current;
		std::size_t* count = nullptr;
		std::size_t capacity = 0;
		ProbeFault overflowFault = ProbeFault::kMalformedEvidence;
		switch (kind) {
		case EvidenceRecordKind::kPhaseRead:
			count = &next.phaseReads;
			capacity = kPhaseReadCapacity;
			overflowFault = ProbeFault::kPhaseReadOverflow;
			break;
		case EvidenceRecordKind::kDrawObservation:
			count = &next.drawObservations;
			capacity = kDrawObservationCapacity;
			overflowFault = ProbeFault::kDrawObservationOverflow;
			break;
		case EvidenceRecordKind::kDrawJoin:
			count = &next.drawJoins;
			capacity = kDrawJoinCapacity;
			overflowFault = ProbeFault::kDrawJoinOverflow;
			break;
		}
		if (count == nullptr || *count >= capacity) {
			next = LatchProbeFault(next, overflowFault);
			next.overflowed = true;
			return { next, LedgerTransitionStatus::kOverflowFaultLatched };
		}
		++*count;
		return { next, LedgerTransitionStatus::kRecorded };
	}

	[[nodiscard]] constexpr bool EvidenceLedgerUsable(
		const EvidenceLedgerState& state) noexcept
	{
		return !state.faultStopped && !state.overflowed &&
			state.firstFault == ProbeFault::kNone &&
			state.phaseReads <= kPhaseReadCapacity &&
			state.drawObservations <= kDrawObservationCapacity &&
			state.drawJoins <= kDrawJoinCapacity;
	}

	struct FutureRuntimeActivationEvidence
	{
		bool explicitOptInMarkerPresent{ false };
		bool exactRuntimePinned{ false };
		bool signaturesVerified{ false };
		bool firstPersonCallbackABIClosed{ false };
		bool reusesExistingHookOwners{ false };
		bool ownsOnlyValidatedFirstPersonCallPatch{ false };
		bool readOnlyValueCopiesOnly{ false };
		bool noEnginePointerRetention{ false };
		bool faultAndOverflowFailStopInstalled{ false };
	};

	[[nodiscard]] constexpr bool PassesFutureDefaultOffGate(
		const FutureRuntimeActivationEvidence& evidence) noexcept
	{
		return evidence.explicitOptInMarkerPresent && evidence.exactRuntimePinned &&
			evidence.signaturesVerified &&
			evidence.firstPersonCallbackABIClosed &&
			evidence.reusesExistingHookOwners &&
			evidence.ownsOnlyValidatedFirstPersonCallPatch &&
			evidence.readOnlyValueCopiesOnly &&
			evidence.noEnginePointerRetention &&
			evidence.faultAndOverflowFailStopInstalled;
	}

	[[nodiscard]] constexpr bool CanActivateRuntimeObserver(
		const FutureRuntimeActivationEvidence& evidence) noexcept
	{
		return kRuntimeObserverWired && kOptInMarkerImplemented &&
			kExistingHookJoinWired && kFirstPersonNormalReturnJoinWired &&
			!kThirdPersonMainViewClassificationWired && !kDefaultEnabled &&
			kInstallsHooks && kOwnsOnlyValidatedFirstPersonCallPatch &&
			!kRetainsEnginePointers && !kMutatesScene &&
			!kCapturesReflection && !kPublishesReflection &&
			PassesFutureDefaultOffGate(evidence);
	}
}
