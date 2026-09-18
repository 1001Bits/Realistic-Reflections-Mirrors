#pragma once

#include "MirrorAuthorCatalogDiscoveryPolicy.h"
#include "MirrorAuthorRegistrationPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace MirrorAuthorResolutionTransactionPolicy
{
	// The default-off native adapter now feeds exact read-only TES observations
	// into this transaction.  These two diagnostic facts grant no TES authority,
	// recognition, delivery, or supported product feature.
	inline constexpr bool kDiagnosticResolutionRuntimeWired = true;
	inline constexpr bool kDiagnosticTESResolutionWired = true;
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kManifestLoaderWired = true;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;

	inline constexpr std::size_t kMaximumRegisteredBases =
		MirrorAuthorRegistrationPolicy::kMaximumRegisteredBases;
	inline constexpr std::size_t kFrozenV1BaseCount =
		MirrorAuthoringContract::kFrozenV1Bases.size();
	inline constexpr std::uint8_t kLightCompileIndex = 0xFEU;
	inline constexpr std::uint8_t kCABITransportMask = 0x01U;
	inline constexpr std::uint8_t kManifestTransportMask = 0x02U;
	inline constexpr std::uint8_t kKnownTransportMask =
		kCABITransportMask | kManifestTransportMask;
	static_assert(kMaximumRegisteredBases == 256);
	static_assert(kFrozenV1BaseCount == 6);

	// Engine identities cross this policy boundary only as copied integer
	// evidence.  The transaction contains no TESFile/TESForm pointer type and
	// exposes no getter for the stored identity values.
	using NumericIdentityToken = std::uint64_t;
	static_assert(!std::is_pointer_v<NumericIdentityToken>);

	enum class FrozenCatalogState : std::uint8_t
	{
		kDormant,
		kCollecting,
		kNormallyFrozen,
		kAborted,
		kQuarantined
	};

	enum class TransactionPhase : std::uint8_t
	{
		kDormant,
		kResolvingEntries,
		kAwaitingEntryResolution,
		kCheckingFrozenV1Aliases,
		kEmpty,
		kAccepted,
		kRejected
	};

	enum class SourceFileKind : std::uint8_t
	{
		kUnknown,
		kFull,
		kLight
	};

	enum class ResolvedRecordKind : std::uint8_t
	{
		kUnknown,
		kStatic,
		kMovableStatic,
		kOther
	};

	// This models the native wrapper's cleanup result.  The policy catches
	// nothing: the current default-off adapter translates normal/C++ completion
	// and uses a POD __finally for propagating SEH, while every abnormal
	// disposition erases the transaction's partial numeric identity evidence.
	enum class CompletionDisposition : std::uint8_t
	{
		kReturnedNormally,
		kStandardCppException,
		kNonStandardCppException,
		kPropagatingStructuredException
	};

	enum class Rejection : std::uint8_t
	{
		kNone,
		kAlreadyStarted,
		kCatalogDormant,
		kCatalogStillCollecting,
		kCatalogAborted,
		kCatalogQuarantined,
		kCatalogStateInvalid,
		kUnsupportedRuntime,
		kCollectionMarkerNotLatched,
		kResolutionMarkerNotLatched,
		kTransportMaskInvalid,
		kTransportMarkerMismatch,
		kTransportCompletionMismatch,
		kManifestReceiptInvalid,
		kFrozenTransportMaskProofMissing,
		kFrozenTransportMaskProofInvalid,
		kDispatchDidNotReturnNormally,
		kDispatchResultFalse,
		kFreezeResultFailed,
		kCollectionWindowStillActive,
		kCollectorPhaseNotFrozen,
		kCollectorQuarantined,
		kFrozenSnapshotCopyFailed,
		kFrozenSnapshotCountMismatch,
		kLifecycleTokenMissing,
		kLifecycleMismatch,
		kOriginThreadTokenMissing,
		kWrongThread,
		kCapacityExceeded,
		kWrongPhase,
		kEntryIndexOutOfOrder,
		kDeclarationCopyFailed,
		kDataHandlerMissing,
		kDataHandlerBusy,
		kSourceLookupMissing,
		kSourceBasenameMismatch,
		kSourceTokenMissing,
		kSourceKindUnknown,
		kLoadedSourceListMismatch,
		kLoadedSourceIndexMismatch,
		kFullCompileIndexInvalid,
		kFullLocalFormIDInvalid,
		kLightCompileIndexInvalid,
		kLightSmallIndexInvalid,
		kLightLocalFormIDInvalid,
		kLookupFormIDMissing,
		kRuntimePackingMismatch,
		kUntypedLookupMissing,
		kTypedStaticLookupMissing,
		kTypedUntypedIdentityMismatch,
		kResolvedTypeMismatch,
		kResolvedRuntimeFormIDMismatch,
		kOriginSourceMismatch,
		kOriginBasenameMismatch,
		kResolvedLocalFormIDMismatch,
		kSourceOwnershipMismatch,
		kDeletedForm,
		kThirdPartyIdentityAlias,
		kThirdPartyRuntimeFormIDAlias,
		kResolutionPassIncomplete,
		kStandardCppException,
		kNonStandardCppException,
		kPropagatingStructuredException,
		kFrozenV1IndexOutOfOrder,
		kFrozenV1ResolutionMissing,
		kFrozenV1IdentityAlias,
		kFrozenV1PassIncomplete
	};

	struct FrozenCatalogObservation
	{
		FrozenCatalogState state{ FrozenCatalogState::kDormant };
		bool supportedRuntime{ false };
		bool collectionMarkerLatched{ false };
		bool resolutionMarkerLatched{ false };
		// A clean-freeze receipt is deliberately more specific than a zero/nonzero
		// FrozenCount.  A normal empty freeze has all facts below and count
		// zero; dormant, collecting, aborted, and quarantined states cannot mimic it.
		bool dispatchReturnedNormally{ false };
		bool dispatchResult{ false };
		bool freezeResultSucceeded{ false };
		bool collectionWindowInactive{ false };
		bool collectorPhaseFrozen{ false };
		bool collectorQuarantined{ true };
		bool frozenSnapshotCopySucceeded{ false };
		std::uint64_t lifecycleToken{ 0 };
		// Captured once at the originating PostLoad collection window.  This is
		// deliberately distinct from a transient DataLoaded callback thread value.
		std::uint64_t postLoadOriginThreadToken{ 0 };
		std::size_t frozenCount{ 0 };
		std::size_t frozenSnapshotCount{ 0 };

		// The master marker is collectionMarkerLatched above.  These two latched
		// transport selections must exactly derive requestedTransportMask; neither
		// an unrequested transport nor an incomplete requested transport is accepted.
		bool cabiTransportMarkerLatched{ false };
		bool manifestTransportMarkerLatched{ false };
		std::uint8_t requestedTransportMask{ 0 };
		std::uint8_t completedTransportMask{ 0 };
		std::uint8_t frozenTransportMaskUnion{ 0 };
		bool manifestAttempted{ false };
		bool manifestReturnedNormally{ false };
		bool manifestResult{ false };
		bool dispatchAttempted{ false };
		bool manifestDirectoryMissing{ false };
		std::uint32_t manifestFileCount{ 0 };
		std::uint64_t manifestAggregateByteCount{ 0 };
		std::size_t manifestRawRegistrationCount{ 0 };
		std::size_t manifestUniqueRegistrationCount{ 0 };
		std::size_t cabiContributedFrozenCount{ 0 };
		std::size_t manifestContributedFrozenCount{ 0 };
		bool frozenTransportMasksValidated{ false };
	};

	// Values required before LookupFormID/LookupForm may be attempted.  The four
	// loaded-list tokens model by-name membership for both file kinds plus the
	// reverse-index membership appropriate to sourceKind.  Exactly the applicable
	// by-name/index pair must equal sourceFileToken.  The opposite by-name lookup
	// must be null; the opposite index field is explicit N/A zero because full and
	// light index namespaces may collide and no cross-namespace index lookup is an
	// audited invariant.  filenameHasESLExtension is evidence only:
	// TESFile::IsLight(), represented by sourceKind, is authoritative, so a
	// light-flagged .esp follows the compact path.
	struct EntryPrelookupEvidence
	{
		std::size_t frozenIndex{ 0 };
		bool declarationCopySucceeded{ false };
		bool dataHandlerExists{ false };
		bool dataHandlerLoadingOrClearing{ true };
		bool sourceLookupResolved{ false };
		bool sourceBasenameMatches{ false };
		bool filenameHasESLExtension{ false };
		SourceFileKind sourceKind{ SourceFileKind::kUnknown };
		NumericIdentityToken sourceFileToken{ 0 };
		NumericIdentityToken loadedFullByNameToken{ 0 };
		NumericIdentityToken loadedFullByIndexToken{ 0 };
		NumericIdentityToken loadedLightByNameToken{ 0 };
		NumericIdentityToken loadedLightByIndexToken{ 0 };
		std::uint8_t compileIndex{ 0xFFU };
		// Meaningful only when sourceKind is kLight.  No raw full-file sentinel
		// representation has been audited, so the full path deliberately ignores it.
		std::uint16_t smallFileCompileIndex{ 0 };
		std::uint32_t declaredLocalFormID{ 0 };
	};

	// All form/source identities below are copied integer observations.  The
	// default-off native adapter obtains exactStaticFormToken from
	// TESDataHandler::LookupForm<TESObjectSTAT>, not TESForm::As<T>, so MSTT cannot
	// satisfy the exact Static record-kind requirement.  This remains diagnostic
	// evidence only and grants no TES or recognition authority.
	struct EntryResolutionEvidence
	{
		std::size_t frozenIndex{ 0 };
		bool lookupFormIDResolved{ false };
		std::uint32_t lookupRuntimeFormID{ 0 };
		NumericIdentityToken untypedFormToken{ 0 };
		NumericIdentityToken exactStaticFormToken{ 0 };
		ResolvedRecordKind resolvedRecordKind{ ResolvedRecordKind::kUnknown };
		std::uint32_t resolvedRuntimeFormID{ 0 };
		NumericIdentityToken originSourceFileToken{ 0 };
		bool originBasenameMatches{ false };
		std::uint32_t resolvedLocalFormID{ 0 };
		bool sourceOwnsForm{ false };
		bool deleted{ true };
	};

	struct FrozenV1IdentityEvidence
	{
		std::size_t frozenV1Index{ 0 };
		// An inactive first-party source is an allowed absent slot, but the slot
		// must still be visited.  Active slots require exact form and runtime IDs.
		bool sourceActive{ false };
		bool exactResolutionSucceeded{ false };
		NumericIdentityToken exactStaticFormToken{ 0 };
		std::uint32_t runtimeFormID{ 0 };
	};

	struct TransactionSnapshot
	{
		TransactionPhase phase{ TransactionPhase::kDormant };
		Rejection rejection{ Rejection::kNone };
		std::uint64_t lifecycleToken{ 0 };
		std::uint64_t postLoadOriginThreadToken{ 0 };
		std::size_t frozenCount{ 0 };
		std::size_t processedCount{ 0 };
		std::size_t eligibleCount{ 0 };
		std::size_t distinctThirdPartyIdentityCount{ 0 };
		std::size_t distinctThirdPartyRuntimeFormIDCount{ 0 };
		std::size_t processedFrozenV1Count{ 0 };
		std::size_t resolvedFrozenV1Count{ 0 };
		// Count only; no source/form identity value is ever copied out.
		std::size_t retainedNumericIdentityCount{ 0 };
	};

	static_assert(std::is_trivially_copyable_v<FrozenCatalogObservation>);
	static_assert(std::is_trivially_copyable_v<EntryPrelookupEvidence>);
	static_assert(std::is_trivially_copyable_v<EntryResolutionEvidence>);
	static_assert(std::is_trivially_copyable_v<FrozenV1IdentityEvidence>);
	static_assert(std::is_trivially_copyable_v<TransactionSnapshot>);

	[[nodiscard]] constexpr std::uint32_t PackFullRuntimeFormID(
		const std::uint8_t compileIndex,
		const std::uint32_t localFormID) noexcept
	{
		return (static_cast<std::uint32_t>(compileIndex) << 24U) | localFormID;
	}

	[[nodiscard]] constexpr std::uint32_t PackLightRuntimeFormID(
		const std::uint16_t smallFileCompileIndex,
		const std::uint32_t localFormID) noexcept
	{
		return 0xFE000000U |
		       (static_cast<std::uint32_t>(smallFileCompileIndex) << 12U) |
		       localFormID;
	}

	[[nodiscard]] constexpr bool IsExactTransportMask(
		const std::uint8_t mask) noexcept
	{
		return mask == kCABITransportMask ||
		       mask == kManifestTransportMask ||
		       mask == kKnownTransportMask;
	}

	[[nodiscard]] constexpr bool HasTransport(
		const std::uint8_t mask,
		const std::uint8_t transport) noexcept
	{
		return (mask & transport) != 0;
	}

	[[nodiscard]] constexpr bool ManifestReceiptIsCoherent(
		const FrozenCatalogObservation& catalog) noexcept
	{
		using namespace MirrorAuthorCatalogDiscoveryPolicy;
		if (catalog.manifestFileCount > kMaximumCatalogFiles ||
			catalog.manifestAggregateByteCount > kMaximumAggregateBytes ||
			catalog.manifestRawRegistrationCount > kMaximumRawRegistrations ||
			catalog.manifestUniqueRegistrationCount > kMaximumUniqueBases ||
			catalog.manifestUniqueRegistrationCount >
				catalog.manifestRawRegistrationCount) {
			return false;
		}
		if (catalog.manifestDirectoryMissing &&
			(catalog.manifestFileCount != 0 ||
			 catalog.manifestAggregateByteCount != 0 ||
			 catalog.manifestRawRegistrationCount != 0 ||
			 catalog.manifestUniqueRegistrationCount != 0)) {
			return false;
		}
		if (catalog.manifestFileCount == 0) {
			return catalog.manifestAggregateByteCount == 0 &&
			       catalog.manifestRawRegistrationCount == 0 &&
			       catalog.manifestUniqueRegistrationCount == 0;
		}
		return catalog.manifestAggregateByteCount != 0;
	}

	[[nodiscard]] constexpr bool FrozenTransportProofIsExact(
		const FrozenCatalogObservation& catalog) noexcept
	{
		if (!catalog.frozenTransportMasksValidated ||
			catalog.cabiContributedFrozenCount > catalog.frozenCount ||
			catalog.manifestContributedFrozenCount > catalog.frozenCount ||
			(catalog.frozenTransportMaskUnion &
			 static_cast<std::uint8_t>(~catalog.requestedTransportMask)) != 0) {
			return false;
		}

		const bool hasCABI = HasTransport(
			catalog.requestedTransportMask, kCABITransportMask);
		const bool hasManifest = HasTransport(
			catalog.requestedTransportMask, kManifestTransportMask);
		if ((!hasCABI && catalog.cabiContributedFrozenCount != 0) ||
			(!hasManifest && catalog.manifestContributedFrozenCount != 0)) {
			return false;
		}
		if (catalog.manifestContributedFrozenCount !=
			catalog.manifestUniqueRegistrationCount) {
			return false;
		}

		const auto expectedUnion = static_cast<std::uint8_t>(
			(catalog.cabiContributedFrozenCount != 0 ? kCABITransportMask : 0U) |
			(catalog.manifestContributedFrozenCount != 0 ?
				kManifestTransportMask : 0U));
		if (catalog.frozenTransportMaskUnion != expectedUnion)
			return false;
		if (catalog.frozenCount == 0)
			return catalog.cabiContributedFrozenCount == 0 &&
			       catalog.manifestContributedFrozenCount == 0;
		if (catalog.manifestContributedFrozenCount <
			catalog.frozenCount - catalog.cabiContributedFrozenCount) {
			return false;
		}
		if (catalog.requestedTransportMask == kCABITransportMask)
			return catalog.cabiContributedFrozenCount == catalog.frozenCount;
		if (catalog.requestedTransportMask == kManifestTransportMask)
			return catalog.manifestContributedFrozenCount == catalog.frozenCount;
		return true;
	}

	class ResolutionTransaction final
	{
	public:
		constexpr ResolutionTransaction() noexcept = default;
		ResolutionTransaction(const ResolutionTransaction&) = delete;
		ResolutionTransaction& operator=(const ResolutionTransaction&) = delete;
		ResolutionTransaction(ResolutionTransaction&&) = delete;
		ResolutionTransaction& operator=(ResolutionTransaction&&) = delete;

		[[nodiscard]] constexpr Rejection Begin(
			const FrozenCatalogObservation& catalog,
			const std::uint64_t expectedLifecycleToken,
			const std::uint64_t currentThreadToken) noexcept
		{
			if (everStarted_)
				return FailProtocolUnlessTerminal(Rejection::kAlreadyStarted);
			everStarted_ = true;

			switch (catalog.state) {
			case FrozenCatalogState::kDormant:
				return Reject(Rejection::kCatalogDormant);
			case FrozenCatalogState::kCollecting:
				return Reject(Rejection::kCatalogStillCollecting);
			case FrozenCatalogState::kAborted:
				return Reject(Rejection::kCatalogAborted);
			case FrozenCatalogState::kQuarantined:
				return Reject(Rejection::kCatalogQuarantined);
			case FrozenCatalogState::kNormallyFrozen:
				break;
			default:
				return Reject(Rejection::kCatalogStateInvalid);
			}
			if (!catalog.supportedRuntime)
				return Reject(Rejection::kUnsupportedRuntime);
			if (!catalog.collectionMarkerLatched)
				return Reject(Rejection::kCollectionMarkerNotLatched);
			if (!catalog.resolutionMarkerLatched)
				return Reject(Rejection::kResolutionMarkerNotLatched);

			if (!IsExactTransportMask(catalog.requestedTransportMask))
				return Reject(Rejection::kTransportMaskInvalid);
			const auto markerMask = static_cast<std::uint8_t>(
				(catalog.cabiTransportMarkerLatched ? kCABITransportMask : 0U) |
				(catalog.manifestTransportMarkerLatched ?
					kManifestTransportMask : 0U));
			if (markerMask != catalog.requestedTransportMask)
				return Reject(Rejection::kTransportMarkerMismatch);
			if (catalog.completedTransportMask != catalog.requestedTransportMask)
				return Reject(Rejection::kTransportCompletionMismatch);

			const bool hasCABI = HasTransport(
				catalog.requestedTransportMask, kCABITransportMask);
			const bool hasManifest = HasTransport(
				catalog.requestedTransportMask, kManifestTransportMask);
			if (catalog.dispatchAttempted != hasCABI ||
				(!hasCABI &&
				 (catalog.dispatchReturnedNormally || catalog.dispatchResult))) {
				return Reject(Rejection::kTransportCompletionMismatch);
			}
			if (hasCABI && !catalog.dispatchReturnedNormally)
				return Reject(Rejection::kDispatchDidNotReturnNormally);
			if (hasCABI && !catalog.dispatchResult)
				return Reject(Rejection::kDispatchResultFalse);

			if (catalog.manifestAttempted != hasManifest ||
				catalog.manifestReturnedNormally != hasManifest ||
				catalog.manifestResult != hasManifest) {
				return Reject(Rejection::kTransportCompletionMismatch);
			}
			if (hasManifest) {
				if (!ManifestReceiptIsCoherent(catalog))
					return Reject(Rejection::kManifestReceiptInvalid);
			} else if (catalog.manifestDirectoryMissing ||
				catalog.manifestFileCount != 0 ||
				catalog.manifestAggregateByteCount != 0 ||
				catalog.manifestRawRegistrationCount != 0 ||
				catalog.manifestUniqueRegistrationCount != 0) {
				return Reject(Rejection::kManifestReceiptInvalid);
			}
			if (!catalog.frozenTransportMasksValidated)
				return Reject(Rejection::kFrozenTransportMaskProofMissing);
			if (!FrozenTransportProofIsExact(catalog))
				return Reject(Rejection::kFrozenTransportMaskProofInvalid);
			if (!catalog.freezeResultSucceeded)
				return Reject(Rejection::kFreezeResultFailed);
			if (!catalog.collectionWindowInactive)
				return Reject(Rejection::kCollectionWindowStillActive);
			if (!catalog.collectorPhaseFrozen)
				return Reject(Rejection::kCollectorPhaseNotFrozen);
			if (catalog.collectorQuarantined)
				return Reject(Rejection::kCollectorQuarantined);
			if (!catalog.frozenSnapshotCopySucceeded)
				return Reject(Rejection::kFrozenSnapshotCopyFailed);
			if (catalog.frozenSnapshotCount != catalog.frozenCount)
				return Reject(Rejection::kFrozenSnapshotCountMismatch);

			if (catalog.lifecycleToken == 0 || expectedLifecycleToken == 0)
				return Reject(Rejection::kLifecycleTokenMissing);
			if (catalog.lifecycleToken != expectedLifecycleToken)
				return Reject(Rejection::kLifecycleMismatch);
			if (catalog.postLoadOriginThreadToken == 0)
				return Reject(Rejection::kOriginThreadTokenMissing);
			if (currentThreadToken == 0 ||
				currentThreadToken != catalog.postLoadOriginThreadToken) {
				return Reject(Rejection::kWrongThread);
			}
			if (catalog.frozenCount > kMaximumRegisteredBases)
				return Reject(Rejection::kCapacityExceeded);

			// These values are written exactly once.  Terminal scrubbing deliberately
			// leaves them intact so the origin remains immutable audit identity.
			lifecycleToken_ = catalog.lifecycleToken;
			postLoadOriginThreadToken_ = catalog.postLoadOriginThreadToken;
			frozenCount_ = catalog.frozenCount;
			if (frozenCount_ == 0) {
				ScrubIdentityEvidence();
				phase_ = TransactionPhase::kEmpty;
				return Rejection::kNone;
			}
			phase_ = TransactionPhase::kResolvingEntries;
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr Rejection BeginEntry(
			const std::uint64_t lifecycleToken,
			const std::uint64_t currentThreadToken,
			const EntryPrelookupEvidence& evidence) noexcept
		{
			if (phase_ != TransactionPhase::kResolvingEntries)
				return FailProtocolUnlessTerminal(Rejection::kWrongPhase);
			if (const auto owner = ValidateOwner(lifecycleToken, currentThreadToken);
				owner != Rejection::kNone) {
				return Reject(owner);
			}
			if (evidence.frozenIndex != processedCount_ ||
				evidence.frozenIndex >= frozenCount_) {
				return Reject(Rejection::kEntryIndexOutOfOrder);
			}

			if (!evidence.declarationCopySucceeded)
				return RejectProcessed(Rejection::kDeclarationCopyFailed);
			if (!evidence.dataHandlerExists)
				return RejectProcessed(Rejection::kDataHandlerMissing);
			if (evidence.dataHandlerLoadingOrClearing)
				return RejectProcessed(Rejection::kDataHandlerBusy);
			if (!evidence.sourceLookupResolved)
				return RejectProcessed(Rejection::kSourceLookupMissing);
			if (!evidence.sourceBasenameMatches)
				return RejectProcessed(Rejection::kSourceBasenameMismatch);
			if (evidence.sourceFileToken == 0)
				return RejectProcessed(Rejection::kSourceTokenMissing);

			std::uint32_t expectedRuntimeFormID = 0;
			switch (evidence.sourceKind) {
			case SourceFileKind::kFull:
				if (evidence.loadedFullByNameToken != evidence.sourceFileToken ||
					evidence.loadedLightByNameToken != 0) {
					return RejectProcessed(Rejection::kLoadedSourceListMismatch);
				}
				if (evidence.loadedFullByIndexToken != evidence.sourceFileToken ||
					evidence.loadedLightByIndexToken != 0) {
					return RejectProcessed(Rejection::kLoadedSourceIndexMismatch);
				}
				if (evidence.compileIndex >= kLightCompileIndex) {
					return RejectProcessed(Rejection::kFullCompileIndexInvalid);
				}
				if (evidence.declaredLocalFormID == 0 ||
					evidence.declaredLocalFormID >
						MirrorAuthorRegistrationPolicy::kMaximumFullPluginLocalFormID) {
					return RejectProcessed(Rejection::kFullLocalFormIDInvalid);
				}
				expectedRuntimeFormID = PackFullRuntimeFormID(
					evidence.compileIndex, evidence.declaredLocalFormID);
				break;

			case SourceFileKind::kLight:
				if (evidence.loadedLightByNameToken != evidence.sourceFileToken ||
					evidence.loadedFullByNameToken != 0) {
					return RejectProcessed(Rejection::kLoadedSourceListMismatch);
				}
				if (evidence.loadedLightByIndexToken != evidence.sourceFileToken ||
					evidence.loadedFullByIndexToken != 0) {
					return RejectProcessed(Rejection::kLoadedSourceIndexMismatch);
				}
				if (evidence.compileIndex != kLightCompileIndex)
					return RejectProcessed(Rejection::kLightCompileIndexInvalid);
				if (evidence.smallFileCompileIndex > 0x0FFFU)
					return RejectProcessed(Rejection::kLightSmallIndexInvalid);
				// This check happens before the transaction accepts any lookup
				// evidence.  CommonLib's helpers do not mask an oversized local ID.
				if (evidence.declaredLocalFormID <
						MirrorAuthorRegistrationPolicy::
							kMinimumLightPluginOwnedLocalFormID ||
					evidence.declaredLocalFormID >
						MirrorAuthorRegistrationPolicy::kMaximumLightPluginLocalFormID) {
					return RejectProcessed(Rejection::kLightLocalFormIDInvalid);
				}
				expectedRuntimeFormID = PackLightRuntimeFormID(
					evidence.smallFileCompileIndex,
					evidence.declaredLocalFormID);
				break;

			case SourceFileKind::kUnknown:
				return RejectProcessed(Rejection::kSourceKindUnknown);
			default:
				return RejectProcessed(Rejection::kSourceKindUnknown);
			}

			pending_ = {
				true,
				evidence.frozenIndex,
				evidence.sourceFileToken,
				evidence.declaredLocalFormID,
				expectedRuntimeFormID
			};
			phase_ = TransactionPhase::kAwaitingEntryResolution;
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr Rejection CompleteEntry(
			const std::uint64_t lifecycleToken,
			const std::uint64_t currentThreadToken,
			const EntryResolutionEvidence& evidence) noexcept
		{
			if (phase_ != TransactionPhase::kAwaitingEntryResolution ||
				!pending_.active) {
				return FailProtocolUnlessTerminal(Rejection::kWrongPhase);
			}
			if (const auto owner = ValidateOwner(lifecycleToken, currentThreadToken);
				owner != Rejection::kNone) {
				return Reject(owner);
			}
			if (evidence.frozenIndex != pending_.frozenIndex)
				return Reject(Rejection::kEntryIndexOutOfOrder);

			const auto pending = pending_;
			pending_ = {};
			phase_ = TransactionPhase::kResolvingEntries;
			++processedCount_;

			if (!evidence.lookupFormIDResolved ||
				evidence.lookupRuntimeFormID == 0) {
				return Reject(Rejection::kLookupFormIDMissing);
			}
			if (evidence.lookupRuntimeFormID != pending.expectedRuntimeFormID)
				return Reject(Rejection::kRuntimePackingMismatch);
			if (evidence.untypedFormToken == 0)
				return Reject(Rejection::kUntypedLookupMissing);
			if (evidence.exactStaticFormToken == 0)
				return Reject(Rejection::kTypedStaticLookupMissing);
			if (evidence.untypedFormToken != evidence.exactStaticFormToken)
				return Reject(Rejection::kTypedUntypedIdentityMismatch);
			if (evidence.resolvedRecordKind != ResolvedRecordKind::kStatic)
				return Reject(Rejection::kResolvedTypeMismatch);
			if (evidence.resolvedRuntimeFormID != pending.expectedRuntimeFormID)
				return Reject(Rejection::kResolvedRuntimeFormIDMismatch);
			if (evidence.originSourceFileToken != pending.sourceFileToken)
				return Reject(Rejection::kOriginSourceMismatch);
			if (!evidence.originBasenameMatches)
				return Reject(Rejection::kOriginBasenameMismatch);
			if (evidence.resolvedLocalFormID != pending.declaredLocalFormID)
				return Reject(Rejection::kResolvedLocalFormIDMismatch);
			if (!evidence.sourceOwnsForm)
				return Reject(Rejection::kSourceOwnershipMismatch);
			if (evidence.deleted)
				return Reject(Rejection::kDeletedForm);

			for (std::size_t index = 0; index < eligibleCount_; ++index) {
				if (thirdPartyBaseTokens_[index] == evidence.exactStaticFormToken)
					return Reject(Rejection::kThirdPartyIdentityAlias);
				if (thirdPartyRuntimeFormIDs_[index] ==
					evidence.resolvedRuntimeFormID) {
					return Reject(Rejection::kThirdPartyRuntimeFormIDAlias);
				}
			}
			thirdPartyBaseTokens_[eligibleCount_] =
				evidence.exactStaticFormToken;
			thirdPartyRuntimeFormIDs_[eligibleCount_] =
				evidence.resolvedRuntimeFormID;
			++eligibleCount_;
			++distinctThirdPartyIdentityCount_;
			++distinctThirdPartyRuntimeFormIDCount_;
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr Rejection CloseResolutionPass(
			const std::uint64_t lifecycleToken,
			const std::uint64_t currentThreadToken,
			const CompletionDisposition completion) noexcept
		{
			if (phase_ != TransactionPhase::kResolvingEntries &&
				phase_ != TransactionPhase::kAwaitingEntryResolution) {
				return FailProtocolUnlessTerminal(Rejection::kWrongPhase);
			}
			if (const auto owner = ValidateOwner(lifecycleToken, currentThreadToken);
				owner != Rejection::kNone) {
				return Reject(owner);
			}
			if (completion != CompletionDisposition::kReturnedNormally)
				return Reject(RejectionForAbnormalCompletion(completion));
			if (phase_ != TransactionPhase::kResolvingEntries || pending_.active ||
				processedCount_ != frozenCount_ ||
				eligibleCount_ != frozenCount_ ||
				distinctThirdPartyIdentityCount_ != frozenCount_ ||
				distinctThirdPartyRuntimeFormIDCount_ != frozenCount_) {
				return Reject(Rejection::kResolutionPassIncomplete);
			}

			phase_ = TransactionPhase::kCheckingFrozenV1Aliases;
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr Rejection AddFrozenV1Identity(
			const std::uint64_t lifecycleToken,
			const std::uint64_t currentThreadToken,
			const FrozenV1IdentityEvidence& evidence) noexcept
		{
			if (phase_ != TransactionPhase::kCheckingFrozenV1Aliases)
				return FailProtocolUnlessTerminal(Rejection::kWrongPhase);
			if (const auto owner = ValidateOwner(lifecycleToken, currentThreadToken);
				owner != Rejection::kNone) {
				return Reject(owner);
			}
			if (evidence.frozenV1Index != processedFrozenV1Count_ ||
				evidence.frozenV1Index >= kFrozenV1BaseCount) {
				return Reject(Rejection::kFrozenV1IndexOutOfOrder);
			}
			if (!evidence.sourceActive) {
				if (evidence.exactResolutionSucceeded ||
					evidence.exactStaticFormToken != 0 ||
					evidence.runtimeFormID != 0) {
					return Reject(Rejection::kFrozenV1ResolutionMissing);
				}
				++processedFrozenV1Count_;
				return Rejection::kNone;
			}
			if (!evidence.exactResolutionSucceeded ||
				evidence.exactStaticFormToken == 0 ||
				evidence.runtimeFormID == 0) {
				return Reject(Rejection::kFrozenV1ResolutionMissing);
			}

			for (std::size_t index = 0; index < eligibleCount_; ++index) {
				if (thirdPartyBaseTokens_[index] ==
						evidence.exactStaticFormToken ||
					thirdPartyRuntimeFormIDs_[index] == evidence.runtimeFormID) {
					return Reject(Rejection::kFrozenV1IdentityAlias);
				}
			}
			for (std::size_t index = 0; index < resolvedFrozenV1Count_; ++index) {
				if (frozenV1BaseTokens_[index] == evidence.exactStaticFormToken ||
					frozenV1RuntimeFormIDs_[index] == evidence.runtimeFormID) {
					return Reject(Rejection::kFrozenV1IdentityAlias);
				}
			}

			frozenV1BaseTokens_[resolvedFrozenV1Count_] =
				evidence.exactStaticFormToken;
			frozenV1RuntimeFormIDs_[resolvedFrozenV1Count_] =
				evidence.runtimeFormID;
			++resolvedFrozenV1Count_;
			++processedFrozenV1Count_;
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr Rejection CloseFrozenV1AliasPass(
			const std::uint64_t lifecycleToken,
			const std::uint64_t currentThreadToken,
			const CompletionDisposition completion) noexcept
		{
			if (phase_ != TransactionPhase::kCheckingFrozenV1Aliases)
				return FailProtocolUnlessTerminal(Rejection::kWrongPhase);
			if (const auto owner = ValidateOwner(lifecycleToken, currentThreadToken);
				owner != Rejection::kNone) {
				return Reject(owner);
			}
			if (completion != CompletionDisposition::kReturnedNormally)
				return Reject(RejectionForAbnormalCompletion(completion));
			if (processedFrozenV1Count_ != kFrozenV1BaseCount)
				return Reject(Rejection::kFrozenV1PassIncomplete);

			ScrubIdentityEvidence();
			phase_ = TransactionPhase::kAccepted;
			rejection_ = Rejection::kNone;
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr TransactionSnapshot Snapshot() const noexcept
		{
			return {
				.phase = phase_,
				.rejection = rejection_,
				.lifecycleToken = lifecycleToken_,
				.postLoadOriginThreadToken = postLoadOriginThreadToken_,
				.frozenCount = frozenCount_,
				.processedCount = processedCount_,
				.eligibleCount = eligibleCount_,
				.distinctThirdPartyIdentityCount =
					distinctThirdPartyIdentityCount_,
				.distinctThirdPartyRuntimeFormIDCount =
					distinctThirdPartyRuntimeFormIDCount_,
				.processedFrozenV1Count = processedFrozenV1Count_,
				.resolvedFrozenV1Count = resolvedFrozenV1Count_,
				.retainedNumericIdentityCount =
					CountRetainedNumericIdentityEvidence()
			};
		}

	private:
		[[nodiscard]] constexpr bool IsTerminal() const noexcept
		{
			return phase_ == TransactionPhase::kEmpty ||
			       phase_ == TransactionPhase::kAccepted ||
			       phase_ == TransactionPhase::kRejected;
		}

		[[nodiscard]] constexpr Rejection FailProtocolUnlessTerminal(
			const Rejection rejection) noexcept
		{
			if (IsTerminal())
				return rejection;
			// A transition invoked before Begin is also a consumed one-shot.  It may
			// not be ignored and followed by a later successful Begin.
			everStarted_ = true;
			return Reject(rejection);
		}

		[[nodiscard]] static constexpr Rejection RejectionForAbnormalCompletion(
			const CompletionDisposition completion) noexcept
		{
			switch (completion) {
			case CompletionDisposition::kStandardCppException:
				return Rejection::kStandardCppException;
			case CompletionDisposition::kNonStandardCppException:
				return Rejection::kNonStandardCppException;
			case CompletionDisposition::kPropagatingStructuredException:
				return Rejection::kPropagatingStructuredException;
			case CompletionDisposition::kReturnedNormally:
				break;
			}
			return Rejection::kWrongPhase;
		}

		struct PendingEntry
		{
			bool active{ false };
			std::size_t frozenIndex{ 0 };
			NumericIdentityToken sourceFileToken{ 0 };
			std::uint32_t declaredLocalFormID{ 0 };
			std::uint32_t expectedRuntimeFormID{ 0 };
		};

		[[nodiscard]] constexpr Rejection ValidateOwner(
			const std::uint64_t lifecycleToken,
			const std::uint64_t currentThreadToken) const noexcept
		{
			if (lifecycleToken == 0 || lifecycleToken != lifecycleToken_)
				return Rejection::kLifecycleMismatch;
			if (currentThreadToken == 0 ||
				currentThreadToken != postLoadOriginThreadToken_) {
				return Rejection::kWrongThread;
			}
			return Rejection::kNone;
		}

		[[nodiscard]] constexpr Rejection RejectProcessed(
			const Rejection rejection) noexcept
		{
			++processedCount_;
			return Reject(rejection);
		}

		[[nodiscard]] constexpr Rejection Reject(
			const Rejection rejection) noexcept
		{
			ScrubIdentityEvidence();
			phase_ = TransactionPhase::kRejected;
			rejection_ = rejection;
			return rejection;
		}

		constexpr void ScrubIdentityEvidence() noexcept
		{
			pending_ = {};
			thirdPartyBaseTokens_ = {};
			thirdPartyRuntimeFormIDs_ = {};
			frozenV1BaseTokens_ = {};
			frozenV1RuntimeFormIDs_ = {};
		}

		[[nodiscard]] constexpr std::size_t
		CountRetainedNumericIdentityEvidence() const noexcept
		{
			std::size_t count = pending_.sourceFileToken != 0 ? 1U : 0U;
			if (pending_.declaredLocalFormID != 0)
				++count;
			if (pending_.expectedRuntimeFormID != 0)
				++count;
			for (const auto token : thirdPartyBaseTokens_) {
				if (token != 0)
					++count;
			}
			for (const auto runtimeFormID : thirdPartyRuntimeFormIDs_) {
				if (runtimeFormID != 0)
					++count;
			}
			for (const auto token : frozenV1BaseTokens_) {
				if (token != 0)
					++count;
			}
			for (const auto runtimeFormID : frozenV1RuntimeFormIDs_) {
				if (runtimeFormID != 0)
					++count;
			}
			return count;
		}

		std::array<NumericIdentityToken, kMaximumRegisteredBases>
			thirdPartyBaseTokens_{};
		std::array<std::uint32_t, kMaximumRegisteredBases>
			thirdPartyRuntimeFormIDs_{};
		std::array<NumericIdentityToken, kFrozenV1BaseCount>
			frozenV1BaseTokens_{};
		std::array<std::uint32_t, kFrozenV1BaseCount>
			frozenV1RuntimeFormIDs_{};
		PendingEntry pending_{};
		std::uint64_t lifecycleToken_{ 0 };
		std::uint64_t postLoadOriginThreadToken_{ 0 };
		std::size_t frozenCount_{ 0 };
		std::size_t processedCount_{ 0 };
		std::size_t eligibleCount_{ 0 };
		std::size_t distinctThirdPartyIdentityCount_{ 0 };
		std::size_t distinctThirdPartyRuntimeFormIDCount_{ 0 };
		std::size_t processedFrozenV1Count_{ 0 };
		std::size_t resolvedFrozenV1Count_{ 0 };
		TransactionPhase phase_{ TransactionPhase::kDormant };
		Rejection rejection_{ Rejection::kNone };
		bool everStarted_{ false };
	};
}
