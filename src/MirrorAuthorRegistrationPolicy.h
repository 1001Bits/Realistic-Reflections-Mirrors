#pragma once

#include "MirrorAuthoringContract.h"
#include "MirrorSelectionPolicy.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace MirrorAuthorRegistrationPolicy
{
	// The marker-gated diagnostic C ABI and canonical manifest loader are real
	// diagnostic transports.  They collect values only; all TES authority and
	// product paths stay explicitly unsupported and unwired.
	inline constexpr bool kDiagnosticCABITransportWired = true;
	inline constexpr bool kPublicCABIShipped = true;
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kManifestLoaderWired = true;
	inline constexpr bool kOwnedKeywordRouteWired = false;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;

	inline constexpr std::size_t kMaximumPluginFilenameBytes = 255;
	inline constexpr std::size_t kMaximumRegisteredBases =
		MirrorAuthoringContract::kMaximumRegisteredBases;
	inline constexpr std::size_t kMaximumTrackedReferences =
		MirrorAuthoringContract::kMaximumTrackedReferences;
	static_assert(kMaximumTrackedReferences ==
		MirrorSelectionPolicy::kMaximumTrackedCandidates);
	inline constexpr std::uint32_t kMaximumFullPluginLocalFormID = 0x00FFFFFFU;
	inline constexpr std::uint32_t kMinimumLightPluginOwnedLocalFormID = 0x00000800U;
	inline constexpr std::uint32_t kMaximumLightPluginLocalFormID = 0x00000FFFU;
	inline constexpr std::uint32_t kRectangularSchema =
		static_cast<std::uint32_t>(
			MirrorAuthoringContract::SchemaVersion::kRectangularV2);

	// The C ABI and JSON manifest are explicit installed-user/compatibility-patch
	// opt-ins into one catalog. They do not prove legal authorship or security
	// ownership of the named ESP; runtime authority still requires exact loaded
	// target resolution. Neither transport outranks the other.
	enum class Transport : std::uint8_t
	{
		kNone,
		kPublicCABI,
		kManifest,
		kOwnedKeyword,
		kPaneMetadataOnly,
		kHeuristic
	};

	enum class CollectionPhase : std::uint8_t
	{
		kCollecting,
		kFrozen,
		kResolved
	};

	struct Declaration
	{
		Transport transport{ Transport::kNone };
		// Exact target TES source identity. This is not the API caller/manifest
		// packager's identity and conveys no legal-author ownership claim.
		std::string_view sourcePlugin{};
		std::uint32_t localFormID{ 0 };
		std::uint32_t mirrorSchema{ 0 };
		std::uint32_t flags{ 0 };
	};

	enum class DeclarationIssue : std::uint8_t
	{
		kNone,
		kCollectionWindowClosed,
		kTransportUnavailable,
		kPluginFilenameMissing,
		kPluginFilenameTooLong,
		kPluginFilenameInvalid,
		kPluginExtensionInvalid,
		kLocalFormIDInvalid,
		kSchemaUnsupported,
		kFlagsUnsupported,
		kFirstPartyNamespaceReserved
	};

	enum class ExistingRelation : std::uint8_t
	{
		kDifferentBase,
		kIdempotentDuplicate,
		kConflictingDescriptor
	};

	enum class Admission : std::uint8_t
	{
		kInsert,
		kIdempotentDuplicate,
		kRejectDeclaration,
		kRejectConflictingDescriptor,
		kRejectCapacity
	};

	struct AdmissionObservation
	{
		CollectionPhase phase{ CollectionPhase::kCollecting };
		Declaration candidate{};
		const Declaration* existingSameBase{ nullptr };
		std::size_t uniqueBaseCount{ 0 };
	};

	struct AdmissionResult
	{
		Admission admission{ Admission::kRejectDeclaration };
		DeclarationIssue issue{ DeclarationIssue::kNone };
	};

	[[nodiscard]] constexpr char LowerASCII(const char value) noexcept
	{
		return value >= 'A' && value <= 'Z' ?
			static_cast<char>(value + ('a' - 'A')) : value;
	}

	[[nodiscard]] constexpr bool FilenameEquals(
		const std::string_view left,
		const std::string_view right) noexcept
	{
		if (left.size() != right.size())
			return false;
		for (std::size_t index = 0; index < left.size(); ++index) {
			if (LowerASCII(left[index]) != LowerASCII(right[index]))
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool HasPluginExtension(
		const std::string_view value) noexcept
	{
		if (value.size() < 5)
			return false;
		const auto suffix = value.substr(value.size() - 4);
		return FilenameEquals(suffix, ".esp") ||
		       FilenameEquals(suffix, ".esm") ||
		       FilenameEquals(suffix, ".esl");
	}

	[[nodiscard]] constexpr bool HasInvalidFilenameByte(
		const std::string_view value) noexcept
	{
		for (const char rawByte : value) {
			const auto byte = static_cast<unsigned char>(rawByte);
			if (byte < 0x20U || byte == 0x7FU || byte == '<' || byte == '>' ||
				byte == ':' || byte == '"' || byte == '/' || byte == '\\' ||
				byte == '|' || byte == '?' || byte == '*') {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] constexpr bool IsDeclarationTransport(
		const Transport transport) noexcept
	{
		return transport == Transport::kPublicCABI ||
		       transport == Transport::kManifest;
	}

	[[nodiscard]] constexpr bool IsFirstPartyPluginNamespace(
		const std::string_view pluginName) noexcept
	{
		for (const auto& base : MirrorAuthoringContract::kFrozenV1Bases) {
			if (FilenameEquals(pluginName, base.pluginName))
				return true;
		}
		return false;
	}

	[[nodiscard]] constexpr bool SameBaseIdentity(
		const Declaration& left,
		const Declaration& right) noexcept
	{
		return left.localFormID == right.localFormID &&
		       FilenameEquals(left.sourcePlugin, right.sourcePlugin);
	}

	[[nodiscard]] constexpr ExistingRelation CompareExisting(
		const Declaration& candidate,
		const Declaration& existing) noexcept
	{
		if (!SameBaseIdentity(candidate, existing))
			return ExistingRelation::kDifferentBase;
		// Transport is deliberately absent: API + manifest for the same exact
		// descriptor is one idempotent declaration, not a conflict or precedence.
		return candidate.mirrorSchema == existing.mirrorSchema &&
		       candidate.flags == existing.flags ?
			ExistingRelation::kIdempotentDuplicate :
			ExistingRelation::kConflictingDescriptor;
	}

	[[nodiscard]] constexpr DeclarationIssue ValidateDeclaration(
		const CollectionPhase phase,
		const Declaration& declaration) noexcept
	{
		if (phase != CollectionPhase::kCollecting)
			return DeclarationIssue::kCollectionWindowClosed;
		if (!IsDeclarationTransport(declaration.transport))
			return DeclarationIssue::kTransportUnavailable;
		if (declaration.sourcePlugin.empty())
			return DeclarationIssue::kPluginFilenameMissing;
		if (declaration.sourcePlugin.size() > kMaximumPluginFilenameBytes)
			return DeclarationIssue::kPluginFilenameTooLong;
		if (HasInvalidFilenameByte(declaration.sourcePlugin))
			return DeclarationIssue::kPluginFilenameInvalid;
		if (!HasPluginExtension(declaration.sourcePlugin))
			return DeclarationIssue::kPluginExtensionInvalid;
		if (declaration.localFormID == 0 ||
			declaration.localFormID > kMaximumFullPluginLocalFormID) {
			return DeclarationIssue::kLocalFormIDInvalid;
		}
		if (declaration.mirrorSchema != kRectangularSchema)
			return DeclarationIssue::kSchemaUnsupported;
		if (declaration.flags != 0)
			return DeclarationIssue::kFlagsUnsupported;
		// The first-party namespaces are never an extension point.  Reserve the
		// whole files, including currently unused local IDs, so a third-party
		// declaration cannot pre-claim a later additive product record.
		if (IsFirstPartyPluginNamespace(declaration.sourcePlugin))
			return DeclarationIssue::kFirstPartyNamespaceReserved;
		return DeclarationIssue::kNone;
	}

	[[nodiscard]] constexpr AdmissionResult ClassifyAdmission(
		const AdmissionObservation& observation) noexcept
	{
		if (const auto issue = ValidateDeclaration(
				observation.phase, observation.candidate);
			issue != DeclarationIssue::kNone) {
			return { Admission::kRejectDeclaration, issue };
		}

		if (observation.existingSameBase) {
			switch (CompareExisting(
				observation.candidate, *observation.existingSameBase)) {
			case ExistingRelation::kIdempotentDuplicate:
				return { Admission::kIdempotentDuplicate, DeclarationIssue::kNone };
			case ExistingRelation::kConflictingDescriptor:
				return { Admission::kRejectConflictingDescriptor, DeclarationIssue::kNone };
			case ExistingRelation::kDifferentBase:
				// A caller claiming this pointer is the same-base slot is malformed.
				return { Admission::kRejectConflictingDescriptor, DeclarationIssue::kNone };
			}
		}
		if (observation.uniqueBaseCount >= kMaximumRegisteredBases)
			return { Admission::kRejectCapacity, DeclarationIssue::kNone };
		return { Admission::kInsert, DeclarationIssue::kNone };
	}

	enum class FreezeAction : std::uint8_t
	{
		kResolveNothing,
		kResolveCatalog,
		kKeepRuntimeDisabled,
		kRejectThirdPartyCatalog
	};

	struct FreezeObservation
	{
		CollectionPhase phase{ CollectionPhase::kCollecting };
		bool defaultOffMarkerPresent{ false };
		std::size_t uniqueBaseCount{ 0 };
		// Set when any unique declaration exceeded the bounded catalog.  The
		// entire third-party catalog then fails closed; first-party v1 is untouched.
		bool capacityOverflowObserved{ false };
	};

	[[nodiscard]] constexpr FreezeAction ClassifyFreeze(
		const FreezeObservation& observation) noexcept
	{
		if (observation.phase != CollectionPhase::kCollecting ||
			observation.capacityOverflowObserved ||
			observation.uniqueBaseCount > kMaximumRegisteredBases) {
			return FreezeAction::kRejectThirdPartyCatalog;
		}
		if (!observation.defaultOffMarkerPresent)
			return FreezeAction::kKeepRuntimeDisabled;
		return observation.uniqueBaseCount == 0 ?
			FreezeAction::kResolveNothing : FreezeAction::kResolveCatalog;
	}

	// The former loose BaseResolution/ClassifyResolvedCohort helpers were removed:
	// they could not prove that processed == eligible == the exact clean frozen
	// snapshot and therefore could accept a valid-looking subset.  Stage-3 form
	// evidence has one callable classifier only:
	// MirrorAuthorResolutionTransactionPolicy::ResolutionTransaction.  Keeping
	// that engine-independent transaction in its own header also prevents this
	// collection policy from silently acquiring TES/runtime authority.

	// Adapter inputs here are outcomes from stronger, separately reviewed
	// validators.  Authority is tested first so pane strings can never grant it.
	enum class ReferenceAdmission : std::uint8_t
	{
		kAccept,
		kRejectBaseAuthority,
		kRejectReferenceIdentity,
		kRejectLifecycle,
		kRejectLocation,
		kRejectPaneContract,
		kRejectInstanceContract,
		kRejectReferenceCapacityStartsCohortQuarantine,
		kRejectReferenceCohortQuarantined
	};

	struct ReferenceObservation
	{
		bool baseAuthorityAdmitted{ false };
		// Global mirror candidate count, including frozen first-party v1. The v2
		// route never receives extra capacity outside the existing runtime limit.
		// This classifier handles v2 admission only: an incoming frozen-v1
		// candidate has priority and the adapter must yield/invalidate v2 rather
		// than reject that first-party candidate.
		std::size_t trackedReferenceCount{ 0 };
		bool referenceAlreadyTracked{ false };
		// Sticky for one complete reference-enumeration epoch. The adapter must
		// invalidate every third-party candidate when capacity first overflows and
		// reset this only when rebuilding a new cohort from an empty snapshot.
		bool referenceCapacityOverflowObserved{ false };
		bool formHandleAndBaseIdentityCurrent{ false };
		bool loadedAttachedThreeD{ false };
		bool currentLocationAccepted{ false };
		bool exactPaneContractAccepted{ false };
		bool instanceContractAccepted{ false };
	};

	[[nodiscard]] constexpr ReferenceAdmission ClassifyReference(
		const ReferenceObservation& observation) noexcept
	{
		if (!observation.baseAuthorityAdmitted)
			return ReferenceAdmission::kRejectBaseAuthority;
		if (observation.referenceCapacityOverflowObserved ||
			observation.trackedReferenceCount > kMaximumTrackedReferences) {
			return ReferenceAdmission::kRejectReferenceCohortQuarantined;
		}
		if (!observation.formHandleAndBaseIdentityCurrent)
			return ReferenceAdmission::kRejectReferenceIdentity;
		if (!observation.loadedAttachedThreeD)
			return ReferenceAdmission::kRejectLifecycle;
		if (!observation.currentLocationAccepted)
			return ReferenceAdmission::kRejectLocation;
		if (!observation.exactPaneContractAccepted)
			return ReferenceAdmission::kRejectPaneContract;
		if (!observation.instanceContractAccepted)
			return ReferenceAdmission::kRejectInstanceContract;
		if (!observation.referenceAlreadyTracked &&
			observation.trackedReferenceCount >= kMaximumTrackedReferences) {
			return ReferenceAdmission::kRejectReferenceCapacityStartsCohortQuarantine;
		}
		return ReferenceAdmission::kAccept;
	}

	[[nodiscard]] constexpr bool RejectsEntireThirdPartyReferenceCohort(
		const ReferenceAdmission admission) noexcept
	{
		return admission ==
				ReferenceAdmission::kRejectReferenceCapacityStartsCohortQuarantine ||
		       admission == ReferenceAdmission::kRejectReferenceCohortQuarantined;
	}

	// V2 must advance the public (FormID,generation) identity for every mutation
	// which can invalidate a capture or pair it with different authored metadata.
	struct CandidateContinuityObservation
	{
		bool sameObjectRefHandleLifetime{ false };
		bool sameCatalogEntry{ false };
		bool sameResolvedBasePointer{ false };
		bool sameParentLocation{ false };
		bool sameReadiness{ false };
		bool sameWorldPlaneAndAperture{ false };
		bool samePaneObject{ false };
		bool samePaneMetadata{ false };
	};

	[[nodiscard]] constexpr bool RequiresCandidateGenerationAdvance(
		const CandidateContinuityObservation& observation) noexcept
	{
		return !observation.sameObjectRefHandleLifetime ||
		       !observation.sameCatalogEntry ||
		       !observation.sameResolvedBasePointer ||
		       !observation.sameParentLocation ||
		       !observation.sameReadiness ||
		       !observation.sameWorldPlaneAndAperture ||
		       !observation.samePaneObject ||
		       !observation.samePaneMetadata;
	}

	[[nodiscard]] constexpr bool TryAdvanceGeneration(
		const std::uint64_t current,
		std::uint64_t& output) noexcept
	{
		if (current == (std::numeric_limits<std::uint64_t>::max)()) {
			output = 0;
			return false;
		}
		output = current + 1;
		if (output == 0)
			return false;
		return true;
	}
}
