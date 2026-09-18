#pragma once

#include "MirrorAuthorCatalogDiscoveryPolicy.h"
#include "MirrorAuthorRegistrationCollector.h"
#include "MirrorAuthorResolutionTransactionPolicy.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace MirrorAuthorRegistrationRuntime
{
	// These are diagnostic wiring facts, not product-authority facts.  The
	// separately marker-gated DataLoaded adapter resolves exact TESObjectSTAT
	// identities only into a value-only audit transaction.  It never admits a
	// schema-v2 pane, mutates MirrorRecognition, or grants render authority.
	inline constexpr bool kDiagnosticCollectionTransportWired = true;
	inline constexpr bool kDiagnosticManifestTransportWired = true;
	inline constexpr bool kDiagnosticReadOnlyTESResolutionWired = true;
	inline constexpr bool kRuntimeTESAuthorityWired = false;
	inline constexpr bool kRecognitionWired = false;
	inline constexpr bool kDeliveryWired = false;
	inline constexpr bool kSupportedRuntimeFeature = false;

	void OnPostLoad() noexcept;
	void OnDataLoaded() noexcept;

	namespace Detail
	{
		static_assert(std::is_trivially_copyable_v<RR_MirrorAuthorBaseV1>);
		// Windows SEH filter result EXCEPTION_EXECUTE_HANDLER.  Keeping the pinned
		// value here avoids making this engine-independent header include Windows.h.
		inline constexpr int kSEHExecuteHandler = 1;

		inline constexpr std::uint8_t kCABITransportMask =
			static_cast<std::uint8_t>(
				MirrorAuthorRegistrationCollector::Transport::kPublicCABI);
		inline constexpr std::uint8_t kManifestTransportMask =
			static_cast<std::uint8_t>(
				MirrorAuthorRegistrationCollector::Transport::kManifest);
		inline constexpr std::uint8_t kKnownTransportMask =
			kCABITransportMask | kManifestTransportMask;

		enum class CollectionWindowPhase : std::uint8_t
		{
			kDormant,
			kStaging,
			kCABIActive,
			kFrozen,
			kAborted
		};

		enum class CollectionCompletionDisposition : std::uint8_t
		{
			kReturnedNormally,
			kStandardCppException,
			kNonStandardCppException,
			kPropagatingStructuredException
		};

		struct ActivationObservation
		{
			bool supportedRuntime{ false };
			bool cabiMarkerReadSucceeded{ false };
			bool cabiMarkerPresent{ false };
			bool manifestMarkerReadSucceeded{ false };
			bool manifestMarkerPresent{ false };
			bool resolutionMarkerReadSucceeded{ false };
			bool resolutionMarkerPresent{ false };
			bool messagingAvailable{ false };
		};

		struct DiagnosticTransportSelection
		{
			std::uint8_t requestedTransportMask{ 0 };
			bool markerReadsExact{ false };
			bool shouldBegin{ false };
		};
		static_assert(std::is_trivially_copyable_v<DiagnosticTransportSelection>);

		// Compatibility input for the existing CABI-only native adapter.  New
		// manifest/merged adapters use CollectionCompletion below.
		struct DispatchCompletion
		{
			bool returnedNormally;
			bool dispatchResult;
		};
		static_assert(std::is_trivial_v<DispatchCompletion>);
		static_assert(std::is_standard_layout_v<DispatchCompletion>);

		struct ManifestCohortFacts
		{
			bool directoryMissing{ false };
			std::uint32_t fileCount{ 0 };
			std::uint64_t aggregateByteCount{ 0 };
			std::size_t rawRegistrationCount{ 0 };
			std::size_t uniqueRegistrationCount{ 0 };
		};
		static_assert(std::is_trivially_copyable_v<ManifestCohortFacts>);

		struct CollectionCompletion
		{
			CollectionCompletionDisposition disposition{
				CollectionCompletionDisposition::kReturnedNormally };
			bool manifestAttempted{ false };
			bool manifestReturnedNormally{ false };
			bool manifestResult{ false };
			bool dispatchAttempted{ false };
			bool dispatchReturnedNormally{ false };
			bool dispatchResult{ false };
		};
		static_assert(std::is_trivially_copyable_v<CollectionCompletion>);

		struct NormallyFrozenReceipt
		{
			// Retained field names keep the existing CABI-only adapter source-compatible.
			bool dispatchReturnedNormally{ false };
			bool dispatchResult{ false };
			bool freezeResultSucceeded{ false };
			bool collectionWindowInactive{ false };
			bool collectorPhaseFrozen{ false };
			bool collectorQuarantined{ true };
			std::uint64_t lifecycleToken{ 0 };
			std::uint64_t postLoadOriginThreadToken{ 0 };
			std::size_t frozenCount{ 0 };

			// Exact clean-transaction provenance.  completedTransportMask must equal
			// requestedTransportMask; a transport can complete cleanly with zero values.
			std::uint8_t requestedTransportMask{ 0 };
			std::uint8_t completedTransportMask{ 0 };
			std::uint8_t frozenTransportMaskUnion{ 0 };
			bool manifestAttempted{ false };
			bool manifestReturnedNormally{ false };
			bool manifestResult{ false };
			bool dispatchAttempted{ false };
			ManifestCohortFacts manifest{};
			std::size_t cabiContributedFrozenCount{ 0 };
			std::size_t manifestContributedFrozenCount{ 0 };
		};
		using CleanFrozenReceipt = NormallyFrozenReceipt;
		static_assert(std::is_trivially_copyable_v<NormallyFrozenReceipt>);

		struct FrozenCatalogBridgeInput
		{
			bool supportedRuntime{ false };
			bool cabiTransportMarkerLatched{ false };
			bool manifestTransportMarkerLatched{ false };
			bool resolutionMarkerLatched{ false };
			bool hasCleanFrozenReceipt{ false };
			bool frozenSnapshotCopySucceeded{ false };
			std::size_t frozenSnapshotCount{ 0 };
		};
		static_assert(std::is_trivially_copyable_v<FrozenCatalogBridgeInput>);

		struct FrozenSnapshot
		{
			std::array<MirrorAuthorRegistrationCollector::OwnedDeclaration,
				RR_MIRROR_AUTHOR_MAX_REGISTERED_BASES>
				declarations{};
			std::size_t count{ 0 };
			CleanFrozenReceipt receipt{};
		};
		static_assert(std::is_trivially_copyable_v<FrozenSnapshot>);

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

		[[nodiscard]] constexpr bool ManifestFactsAreBoundedAndCoherent(
			const ManifestCohortFacts& facts) noexcept
		{
			using namespace MirrorAuthorCatalogDiscoveryPolicy;
			if (facts.fileCount > kMaximumCatalogFiles ||
				facts.aggregateByteCount > kMaximumAggregateBytes ||
				facts.rawRegistrationCount > kMaximumRawRegistrations ||
				facts.uniqueRegistrationCount > kMaximumUniqueBases ||
				facts.uniqueRegistrationCount > facts.rawRegistrationCount) {
				return false;
			}
			if (facts.directoryMissing &&
				(facts.fileCount != 0 || facts.aggregateByteCount != 0 ||
				 facts.rawRegistrationCount != 0 ||
				 facts.uniqueRegistrationCount != 0)) {
				return false;
			}
			if (facts.fileCount == 0) {
				return facts.aggregateByteCount == 0 &&
				       facts.rawRegistrationCount == 0 &&
				       facts.uniqueRegistrationCount == 0;
			}
			return facts.aggregateByteCount != 0;
		}

		[[nodiscard]] constexpr DiagnosticTransportSelection
		SelectDiagnosticTransports(
			const ActivationObservation& observation) noexcept
		{
			DiagnosticTransportSelection selection{};
			selection.markerReadsExact =
				observation.cabiMarkerReadSucceeded &&
				observation.manifestMarkerReadSucceeded;
			if (!selection.markerReadsExact)
				return selection;
			selection.requestedTransportMask = static_cast<std::uint8_t>(
				(observation.cabiMarkerPresent ? kCABITransportMask : 0U) |
				(observation.manifestMarkerPresent ? kManifestTransportMask : 0U));
			selection.shouldBegin = observation.supportedRuntime &&
				IsExactTransportMask(selection.requestedTransportMask) &&
				(!HasTransport(
					selection.requestedTransportMask, kCABITransportMask) ||
				 observation.messagingAvailable);
			return selection;
		}

		[[nodiscard]] constexpr bool ShouldOpenDiagnosticWindow(
			const ActivationObservation& observation) noexcept
		{
			return SelectDiagnosticTransports(observation).shouldBegin;
		}

		[[nodiscard]] constexpr bool MayLookupFrozenV1Sources(
			const bool dataHandlerExists,
			const bool dataHandlerLoadingOrClearing) noexcept
		{
			return dataHandlerExists && !dataHandlerLoadingOrClearing;
		}

		[[nodiscard]] constexpr bool FrozenV1LookupFailureIsActiveMalformed(
			const bool dataHandlerExists,
			const bool dataHandlerLoadingOrClearing) noexcept
		{
			return dataHandlerExists && dataHandlerLoadingOrClearing;
		}

		[[nodiscard]] constexpr bool CleanReceiptHasExactFacts(
			const CleanFrozenReceipt& receipt) noexcept;

		class CollectionWindow final
		{
		public:
			[[nodiscard]] std::uint32_t BeginStaging(
				const std::uint64_t lifecycleToken,
				const std::uint64_t ownerThreadToken,
				const std::uint8_t requestedTransportMask,
				const RR_MirrorAuthorRegisterBaseFn registerBase) noexcept
			{
				if (everBegun_ || lifecycleToken == 0 || ownerThreadToken == 0)
					return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
				if (!IsExactTransportMask(requestedTransportMask) ||
					(HasTransport(requestedTransportMask, kCABITransportMask) !=
					 (registerBase != nullptr))) {
					return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
				}
				const auto result = collector_.BeginLifecycle(lifecycleToken);
				if (result != RR_MIRROR_AUTHOR_RESULT_SUCCESS)
					return result;

				everBegun_ = true;
				requestedTransportMask_ = requestedTransportMask;
				table_ = {
					sizeof(RR_MirrorAuthoringAPI),
					RR_MIRROR_AUTHOR_API_VERSION_1,
					RR_MIRROR_AUTHOR_BASE_VERSION_1,
					RR_MIRROR_AUTHOR_MAX_REGISTERED_BASES,
					lifecycleToken,
					registerBase,
					{}
				};
				lifecycleToken_.store(lifecycleToken, std::memory_order_relaxed);
				ownerThreadToken_.store(ownerThreadToken, std::memory_order_relaxed);
				phase_.store(CollectionWindowPhase::kStaging,
					std::memory_order_release);
				return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
			}

			// Existing runtime compatibility: CABI-only collection has no manifest
			// work, so it may perform the explicit staging->active transition here.
			[[nodiscard]] std::uint32_t Begin(
				const std::uint64_t lifecycleToken,
				const std::uint64_t ownerThreadToken,
				const RR_MirrorAuthorRegisterBaseFn registerBase) noexcept
			{
				const auto begin = BeginStaging(
					lifecycleToken, ownerThreadToken, kCABITransportMask,
					registerBase);
				if (begin != RR_MIRROR_AUTHOR_RESULT_SUCCESS)
					return begin;
				return OpenCABI(ownerThreadToken, lifecycleToken);
			}

			[[nodiscard]] std::uint32_t StageManifest(
				const std::uint64_t currentThreadToken,
				const std::uint64_t lifecycleToken,
				const MirrorAuthorRegistrationCollector::ManifestDeclaration&
					declaration) noexcept
			{
				const auto phase = phase_.load(std::memory_order_acquire);
				if (phase != CollectionWindowPhase::kStaging &&
					phase != CollectionWindowPhase::kCABIActive) {
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!OwnerMatches(currentThreadToken, lifecycleToken))
					return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
				if (phase != CollectionWindowPhase::kStaging ||
					manifestStagingComplete_) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!HasTransport(requestedTransportMask_, kManifestTransportMask)) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
				}

				const auto result = collector_.RegisterManifest(
					lifecycleToken, declaration);
				if (result == RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
					++stagedManifestUniqueCount_;
					return result;
				}

				// The discovery policy emits one canonical unique catalog.  Therefore an
				// idempotent manifest duplicate is an adapter protocol fault, while any
				// other rejected staged value is likewise fatal to the whole cohort.
				(void)CloseAndAbort(currentThreadToken);
				return result == RR_MIRROR_AUTHOR_RESULT_IDEMPOTENT_DUPLICATE ?
					RR_MIRROR_AUTHOR_RESULT_CATALOG_QUARANTINED : result;
			}

			[[nodiscard]] std::uint32_t CompleteManifestStaging(
				const std::uint64_t currentThreadToken,
				const std::uint64_t lifecycleToken,
				const ManifestCohortFacts& facts) noexcept
			{
				const auto phase = phase_.load(std::memory_order_acquire);
				if (phase != CollectionWindowPhase::kStaging &&
					phase != CollectionWindowPhase::kCABIActive) {
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!OwnerMatches(currentThreadToken, lifecycleToken))
					return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
				if (phase != CollectionWindowPhase::kStaging ||
					manifestStagingComplete_) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!HasTransport(requestedTransportMask_, kManifestTransportMask) ||
					!ManifestFactsAreBoundedAndCoherent(facts) ||
					facts.uniqueRegistrationCount != stagedManifestUniqueCount_) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
				}
				manifestFacts_ = facts;
				manifestStagingComplete_ = true;
				return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
			}

			[[nodiscard]] std::uint32_t OpenCABI(
				const std::uint64_t currentThreadToken,
				const std::uint64_t lifecycleToken) noexcept
			{
				const auto phase = phase_.load(std::memory_order_acquire);
				if (phase != CollectionWindowPhase::kStaging &&
					phase != CollectionWindowPhase::kCABIActive) {
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!OwnerMatches(currentThreadToken, lifecycleToken))
					return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
				if (phase != CollectionWindowPhase::kStaging) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!HasTransport(requestedTransportMask_, kCABITransportMask) ||
					(HasTransport(requestedTransportMask_, kManifestTransportMask) &&
					 !manifestStagingComplete_)) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
				}

				queryActive_.store(true, std::memory_order_relaxed);
				phase_.store(CollectionWindowPhase::kCABIActive,
					std::memory_order_release);
				return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
			}

			[[nodiscard]] const RR_MirrorAuthoringAPI* Query(
				const std::uint32_t requestedVersion,
				const std::uint64_t currentThreadToken) const noexcept
			{
				if (requestedVersion != RR_MIRROR_AUTHOR_API_VERSION_1 ||
					currentThreadToken == 0 ||
					phase_.load(std::memory_order_acquire) !=
						CollectionWindowPhase::kCABIActive ||
					!queryActive_.load(std::memory_order_acquire) ||
					ownerThreadToken_.load(std::memory_order_relaxed) !=
						currentThreadToken) {
					return nullptr;
				}
				return &table_;
			}

			[[nodiscard]] std::uint32_t PreflightRegistration(
				const std::uint64_t currentThreadToken,
				const std::uint64_t lifecycleToken) const noexcept
			{
				if (phase_.load(std::memory_order_acquire) !=
						CollectionWindowPhase::kCABIActive ||
					!queryActive_.load(std::memory_order_acquire)) {
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				return OwnerMatches(currentThreadToken, lifecycleToken) ?
					RR_MIRROR_AUTHOR_RESULT_SUCCESS :
					RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
			}

			[[nodiscard]] std::uint32_t RegisterCopied(
				const std::uint64_t currentThreadToken,
				const std::uint64_t lifecycleToken,
				const RR_MirrorAuthorBaseV1& declaration) noexcept
			{
				if (const auto preflight =
						PreflightRegistration(currentThreadToken, lifecycleToken);
					preflight != RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
					return preflight;
				}
				return collector_.RegisterCABI(lifecycleToken, &declaration);
			}

			[[nodiscard]] std::uint32_t CloseAndAbort(
				const std::uint64_t currentThreadToken) noexcept
			{
				const auto phase = phase_.load(std::memory_order_acquire);
				if (phase != CollectionWindowPhase::kStaging &&
					phase != CollectionWindowPhase::kCABIActive) {
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (currentThreadToken == 0 ||
					ownerThreadToken_.load(std::memory_order_relaxed) !=
						currentThreadToken) {
					return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
				}

				queryActive_.store(false, std::memory_order_release);
				ownerThreadToken_.store(0, std::memory_order_relaxed);
				const auto result = collector_.Abort(LifecycleToken());
				phase_.store(CollectionWindowPhase::kAborted,
					std::memory_order_release);
				return result;
			}

			[[nodiscard]] bool Active() const noexcept
			{
				return queryActive_.load(std::memory_order_acquire) &&
				       Phase() == CollectionWindowPhase::kCABIActive;
			}

			[[nodiscard]] CollectionWindowPhase Phase() const noexcept
			{
				return phase_.load(std::memory_order_acquire);
			}

			[[nodiscard]] std::uint8_t RequestedTransportMask() const noexcept
			{
				return requestedTransportMask_;
			}

			[[nodiscard]] std::uint64_t LifecycleToken() const noexcept
			{
				return lifecycleToken_.load(std::memory_order_relaxed);
			}

			[[nodiscard]] std::size_t FrozenCount() const noexcept
			{
				return normallyFrozen_.load(std::memory_order_acquire) ?
					collector_.FrozenCount(LifecycleToken()) : 0;
			}

			[[nodiscard]] bool TryCopyFrozen(
				const std::size_t index,
				MirrorAuthorRegistrationCollector::OwnedDeclaration& output) const noexcept
			{
				output = {};
				if (!normallyFrozen_.load(std::memory_order_acquire))
					return false;
				return collector_.TryCopyFrozen(LifecycleToken(), index, output);
			}

			[[nodiscard]] bool Quarantined() const noexcept
			{
				return collector_.IsQuarantined();
			}

			[[nodiscard]] std::uint32_t QuarantineCause() const noexcept
			{
				return collector_.QuarantineCause();
			}

			[[nodiscard]] bool TryGetNormallyFrozenReceipt(
				NormallyFrozenReceipt& output) const noexcept
			{
				output = {};
				if (!normallyFrozen_.load(std::memory_order_acquire))
					return false;
				output = normallyFrozenReceipt_;
				return true;
			}

			[[nodiscard]] bool TryGetCleanFrozenReceipt(
				CleanFrozenReceipt& output) const noexcept
			{
				return TryGetNormallyFrozenReceipt(output);
			}

		private:
			[[nodiscard]] bool OwnerMatches(
				const std::uint64_t currentThreadToken,
				const std::uint64_t lifecycleToken) const noexcept
			{
				return currentThreadToken != 0 && lifecycleToken != 0 &&
				       ownerThreadToken_.load(std::memory_order_relaxed) ==
					       currentThreadToken &&
				       lifecycleToken_.load(std::memory_order_relaxed) ==
					       lifecycleToken;
			}

			[[nodiscard]] std::uint32_t CloseFreezeAndMintReceipt(
				const std::uint64_t currentThreadToken,
				const CollectionCompletion& completion) noexcept
			{
				const auto phase = phase_.load(std::memory_order_acquire);
				if (phase != CollectionWindowPhase::kStaging &&
					phase != CollectionWindowPhase::kCABIActive) {
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}
				if (!OwnerMatches(currentThreadToken, LifecycleToken()))
					return RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH;
				const auto expectedPhase =
					HasTransport(requestedTransportMask_, kCABITransportMask) ?
						CollectionWindowPhase::kCABIActive :
						CollectionWindowPhase::kStaging;
				if (phase != expectedPhase) {
					(void)CloseAndAbort(currentThreadToken);
					return RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED;
				}

				queryActive_.store(false, std::memory_order_release);
				MirrorAuthorRegistrationCollector::TransportProof proof{};
				if (!collector_.TryBuildTransportProof(
						LifecycleToken(), requestedTransportMask_, proof)) {
					const auto abortResult = collector_.Abort(LifecycleToken());
					ownerThreadToken_.store(0, std::memory_order_relaxed);
					phase_.store(CollectionWindowPhase::kAborted,
						std::memory_order_release);
					return abortResult;
				}

				CleanFrozenReceipt candidate{
					.dispatchReturnedNormally = completion.dispatchReturnedNormally,
					.dispatchResult = completion.dispatchResult,
					.freezeResultSucceeded = true,
					.collectionWindowInactive = true,
					.collectorPhaseFrozen = true,
					.collectorQuarantined = false,
					.lifecycleToken = LifecycleToken(),
					.postLoadOriginThreadToken = currentThreadToken,
					.frozenCount = proof.declarationCount,
					.requestedTransportMask = requestedTransportMask_,
					.completedTransportMask = requestedTransportMask_,
					.frozenTransportMaskUnion = proof.maskUnion,
					.manifestAttempted = completion.manifestAttempted,
					.manifestReturnedNormally = completion.manifestReturnedNormally,
					.manifestResult = completion.manifestResult,
					.dispatchAttempted = completion.dispatchAttempted,
					.manifest = manifestFacts_,
					.cabiContributedFrozenCount = proof.cabiContributionCount,
					.manifestContributedFrozenCount =
						proof.manifestContributionCount
				};
				if (!CleanReceiptHasExactFacts(candidate)) {
					const auto abortResult = collector_.Abort(LifecycleToken());
					ownerThreadToken_.store(0, std::memory_order_relaxed);
					phase_.store(CollectionWindowPhase::kAborted,
						std::memory_order_release);
					return abortResult;
				}

				ownerThreadToken_.store(0, std::memory_order_relaxed);
				const auto freezeResult = collector_.Freeze(LifecycleToken());
				if (freezeResult != RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
					if (collector_.CurrentPhase() ==
						MirrorAuthorRegistrationCollector::Phase::kCollecting) {
						(void)collector_.Abort(LifecycleToken());
					}
					phase_.store(CollectionWindowPhase::kAborted,
						std::memory_order_release);
					return freezeResult;
				}

				// Candidate construction and every fallible proof check occurred while
				// the collector was still abortable.  These final operations are no-throw
				// value publication, so a successful Freeze cannot strand an unreceipted
				// accessible cohort.
				static_assert(std::is_nothrow_copy_assignable_v<CleanFrozenReceipt>);
				normallyFrozenReceipt_ = candidate;
				phase_.store(CollectionWindowPhase::kFrozen,
					std::memory_order_release);
				normallyFrozen_.store(true, std::memory_order_release);
				return RR_MIRROR_AUTHOR_RESULT_SUCCESS;
			}

			friend std::uint32_t FinalizeCollection(
				CollectionWindow& window,
				std::uint64_t ownerThreadToken,
				CollectionCompletion completion) noexcept;
			friend std::uint32_t FinalizeAfterDispatch(
				CollectionWindow& window,
				std::uint64_t ownerThreadToken,
				DispatchCompletion completion) noexcept;

			MirrorAuthorRegistrationCollector::Collector collector_{};
			RR_MirrorAuthoringAPI table_{};
			NormallyFrozenReceipt normallyFrozenReceipt_{};
			ManifestCohortFacts manifestFacts_{};
			std::atomic<std::uint64_t> lifecycleToken_{ 0 };
			std::atomic<std::uint64_t> ownerThreadToken_{ 0 };
			std::atomic<CollectionWindowPhase> phase_{
				CollectionWindowPhase::kDormant };
			std::atomic_bool queryActive_{ false };
			std::atomic_bool normallyFrozen_{ false };
			std::size_t stagedManifestUniqueCount_{ 0 };
			std::uint8_t requestedTransportMask_{ 0 };
			bool manifestStagingComplete_{ false };
			bool everBegun_{ false };
		};

		[[nodiscard]] constexpr bool CompletionMatchesRequestedTransports(
			const std::uint8_t requestedTransportMask,
			const bool manifestStagingComplete,
			const CollectionCompletion& completion) noexcept
		{
			if (completion.disposition !=
				CollectionCompletionDisposition::kReturnedNormally) {
				return false;
			}
			const bool wantsManifest =
				HasTransport(requestedTransportMask, kManifestTransportMask);
			const bool wantsCABI =
				HasTransport(requestedTransportMask, kCABITransportMask);
			if (wantsManifest != manifestStagingComplete ||
				completion.manifestAttempted != wantsManifest ||
				completion.manifestReturnedNormally != wantsManifest ||
				completion.manifestResult != wantsManifest ||
				completion.dispatchAttempted != wantsCABI ||
				completion.dispatchReturnedNormally != wantsCABI ||
				completion.dispatchResult != wantsCABI) {
				return false;
			}
			return true;
		}

		[[nodiscard]] inline std::uint32_t FinalizeCollection(
			CollectionWindow& window,
			const std::uint64_t ownerThreadToken,
			const CollectionCompletion completion) noexcept
		{
			if (!CompletionMatchesRequestedTransports(
					window.requestedTransportMask_,
					window.manifestStagingComplete_, completion)) {
				return window.CloseAndAbort(ownerThreadToken);
			}
			return window.CloseFreezeAndMintReceipt(
				ownerThreadToken, completion);
		}

		[[nodiscard]] inline std::uint32_t FinalizeAfterDispatch(
			CollectionWindow& window,
			const std::uint64_t ownerThreadToken,
			const DispatchCompletion completion) noexcept
		{
			return FinalizeCollection(window, ownerThreadToken, {
				.disposition = completion.returnedNormally ?
					CollectionCompletionDisposition::kReturnedNormally :
					CollectionCompletionDisposition::kNonStandardCppException,
				.dispatchAttempted = true,
				.dispatchReturnedNormally = completion.returnedNormally,
				.dispatchResult = completion.dispatchResult
			});
		}

		[[nodiscard]] constexpr bool CleanReceiptHasExactFacts(
			const CleanFrozenReceipt& receipt) noexcept
		{
			if (!receipt.freezeResultSucceeded ||
				!receipt.collectionWindowInactive ||
				!receipt.collectorPhaseFrozen || receipt.collectorQuarantined ||
				receipt.lifecycleToken == 0 ||
				receipt.postLoadOriginThreadToken == 0 ||
				!IsExactTransportMask(receipt.requestedTransportMask) ||
				receipt.completedTransportMask !=
					receipt.requestedTransportMask ||
				(receipt.frozenTransportMaskUnion &
				 static_cast<std::uint8_t>(~receipt.requestedTransportMask)) != 0 ||
				receipt.frozenCount > RR_MIRROR_AUTHOR_MAX_REGISTERED_BASES ||
				receipt.cabiContributedFrozenCount > receipt.frozenCount ||
				receipt.manifestContributedFrozenCount > receipt.frozenCount) {
				return false;
			}

			const bool hasManifest = HasTransport(
				receipt.requestedTransportMask, kManifestTransportMask);
			const bool hasCABI = HasTransport(
				receipt.requestedTransportMask, kCABITransportMask);
			if (receipt.manifestAttempted != hasManifest ||
				receipt.manifestReturnedNormally != hasManifest ||
				receipt.manifestResult != hasManifest ||
				receipt.dispatchAttempted != hasCABI ||
				receipt.dispatchReturnedNormally != hasCABI ||
				receipt.dispatchResult != hasCABI) {
				return false;
			}
			if (hasManifest) {
				if (!ManifestFactsAreBoundedAndCoherent(receipt.manifest) ||
					receipt.manifest.uniqueRegistrationCount !=
						receipt.manifestContributedFrozenCount) {
					return false;
				}
			} else if (receipt.manifest.directoryMissing ||
				receipt.manifest.fileCount != 0 ||
				receipt.manifest.aggregateByteCount != 0 ||
				receipt.manifest.rawRegistrationCount != 0 ||
				receipt.manifest.uniqueRegistrationCount != 0 ||
				receipt.manifestContributedFrozenCount != 0) {
				return false;
			}
			if (!hasCABI && receipt.cabiContributedFrozenCount != 0)
				return false;

			const auto expectedUnion = static_cast<std::uint8_t>(
				(receipt.cabiContributedFrozenCount != 0 ?
					kCABITransportMask : 0U) |
				(receipt.manifestContributedFrozenCount != 0 ?
					kManifestTransportMask : 0U));
			if (receipt.frozenTransportMaskUnion != expectedUnion)
				return false;
			if (receipt.frozenCount == 0) {
				return receipt.cabiContributedFrozenCount == 0 &&
				       receipt.manifestContributedFrozenCount == 0;
			}
			if (receipt.manifestContributedFrozenCount <
				receipt.frozenCount - receipt.cabiContributedFrozenCount) {
				return false;
			}
			if (receipt.requestedTransportMask == kCABITransportMask)
				return receipt.cabiContributedFrozenCount == receipt.frozenCount;
			if (receipt.requestedTransportMask == kManifestTransportMask) {
				return receipt.manifestContributedFrozenCount ==
				       receipt.frozenCount;
			}
			return true;
		}

		/**
		 * Pure, value-only bridge into the TES-resolution transaction.  Receipt
		 * fields are copied only from the finalizer-minted exact receipt, and the
		 * transport-mask proof bit is derived only from
		 * TryCopyExactFrozenSnapshot succeeding.
		 */
		[[nodiscard]] constexpr
		MirrorAuthorResolutionTransactionPolicy::FrozenCatalogObservation
		BuildFrozenCatalogObservation(
			const FrozenCatalogBridgeInput& input,
			const CleanFrozenReceipt& receipt) noexcept
		{
			using namespace MirrorAuthorResolutionTransactionPolicy;
			FrozenCatalogObservation observation{
				.state = static_cast<FrozenCatalogState>(0xFFU),
				.supportedRuntime = input.supportedRuntime,
				.collectionMarkerLatched =
					input.cabiTransportMarkerLatched ||
					input.manifestTransportMarkerLatched,
				.resolutionMarkerLatched = input.resolutionMarkerLatched,
				.cabiTransportMarkerLatched =
					input.cabiTransportMarkerLatched,
				.manifestTransportMarkerLatched =
					input.manifestTransportMarkerLatched
			};
			if (!input.hasCleanFrozenReceipt ||
				!CleanReceiptHasExactFacts(receipt)) {
				return observation;
			}

			observation.state = FrozenCatalogState::kNormallyFrozen;
			observation.dispatchReturnedNormally =
				receipt.dispatchReturnedNormally;
			observation.dispatchResult = receipt.dispatchResult;
			observation.freezeResultSucceeded = receipt.freezeResultSucceeded;
			observation.collectionWindowInactive =
				receipt.collectionWindowInactive;
			observation.collectorPhaseFrozen = receipt.collectorPhaseFrozen;
			observation.collectorQuarantined = receipt.collectorQuarantined;
			observation.frozenSnapshotCopySucceeded =
				input.frozenSnapshotCopySucceeded;
			observation.lifecycleToken = receipt.lifecycleToken;
			observation.postLoadOriginThreadToken =
				receipt.postLoadOriginThreadToken;
			observation.frozenCount = receipt.frozenCount;
			observation.frozenSnapshotCount = input.frozenSnapshotCount;
			observation.requestedTransportMask =
				receipt.requestedTransportMask;
			observation.completedTransportMask =
				receipt.completedTransportMask;
			observation.frozenTransportMaskUnion =
				receipt.frozenTransportMaskUnion;
			observation.manifestAttempted = receipt.manifestAttempted;
			observation.manifestReturnedNormally =
				receipt.manifestReturnedNormally;
			observation.manifestResult = receipt.manifestResult;
			observation.dispatchAttempted = receipt.dispatchAttempted;
			observation.manifestDirectoryMissing =
				receipt.manifest.directoryMissing;
			observation.manifestFileCount = receipt.manifest.fileCount;
			observation.manifestAggregateByteCount =
				receipt.manifest.aggregateByteCount;
			observation.manifestRawRegistrationCount =
				receipt.manifest.rawRegistrationCount;
			observation.manifestUniqueRegistrationCount =
				receipt.manifest.uniqueRegistrationCount;
			observation.cabiContributedFrozenCount =
				receipt.cabiContributedFrozenCount;
			observation.manifestContributedFrozenCount =
				receipt.manifestContributedFrozenCount;
			observation.frozenTransportMasksValidated =
				input.frozenSnapshotCopySucceeded;
			return observation;
		}

		[[nodiscard]] inline bool TryCopyExactFrozenSnapshot(
			const CollectionWindow& window,
			FrozenSnapshot& output) noexcept
		{
			output = {};
			CleanFrozenReceipt receipt{};
			if (!window.TryGetCleanFrozenReceipt(receipt) ||
				!CleanReceiptHasExactFacts(receipt) ||
				receipt.frozenCount > output.declarations.size()) {
				return false;
			}

			std::uint8_t maskUnion = 0;
			std::size_t cabiCount = 0;
			std::size_t manifestCount = 0;
			for (std::size_t index = 0; index < receipt.frozenCount; ++index) {
				auto& declaration = output.declarations[index];
				if (!window.TryCopyFrozen(index, declaration) ||
					declaration.transportMask == 0 ||
					(declaration.transportMask &
					 static_cast<std::uint8_t>(~receipt.requestedTransportMask)) != 0) {
					output = {};
					return false;
				}
				maskUnion |= declaration.transportMask;
				if (HasTransport(declaration.transportMask, kCABITransportMask))
					++cabiCount;
				if (HasTransport(declaration.transportMask, kManifestTransportMask))
					++manifestCount;
			}
			if (window.FrozenCount() != receipt.frozenCount ||
				maskUnion != receipt.frozenTransportMaskUnion ||
				cabiCount != receipt.cabiContributedFrozenCount ||
				manifestCount != receipt.manifestContributedFrozenCount) {
				output = {};
				return false;
			}
			output.count = receipt.frozenCount;
			output.receipt = receipt;
			return true;
		}

#if defined(_MSC_VER)
		[[nodiscard]] __declspec(noinline) inline bool CopyDeclarationFromCaller(
#else
		[[nodiscard]] inline bool CopyDeclarationFromCaller(
#endif
			const RR_MirrorAuthorBaseV1* source,
			RR_MirrorAuthorBaseV1& output) noexcept
		{
			output = {};
			if (!source)
				return false;
#if defined(_MSC_VER)
			__try {
				output = *source;
				return true;
			} __except (kSEHExecuteHandler) {
				output = {};
				return false;
			}
#else
			output = *source;
			return true;
#endif
		}

		[[nodiscard]] inline std::uint32_t RegisterFromCaller(
			CollectionWindow& window,
			const std::uint64_t currentThreadToken,
			const std::uint64_t lifecycleToken,
			const RR_MirrorAuthorBaseV1* declaration) noexcept
		{
			if (const auto preflight =
					window.PreflightRegistration(currentThreadToken, lifecycleToken);
				preflight != RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
				return preflight;
			}
			RR_MirrorAuthorBaseV1 copied{};
			if (!CopyDeclarationFromCaller(declaration, copied))
				return RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT;
			return window.RegisterCopied(
				currentThreadToken, lifecycleToken, copied);
		}
	}
}
