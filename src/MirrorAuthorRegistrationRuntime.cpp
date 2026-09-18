#include "PCH.h"

#define RR_MIRROR_AUTHOR_API_BUILD
#include "MirrorAuthorCatalogRuntimeLoader.h"
#include "MirrorAuthorRegistrationRuntime.h"
#include "MirrorAuthorResolutionTransactionPolicy.h"

#include <limits>
#include <new>

namespace MirrorAuthorRegistrationRuntime
{
	namespace
	{
		constexpr std::wstring_view kCABITransportDiagnosticMarker =
			L"RealisticReflections_MirrorAuthorRegistrationDiagnostic.enable";
		constexpr std::wstring_view kManifestTransportDiagnosticMarker =
			L"RealisticReflections_MirrorAuthorManifestDiagnostic.enable";
		constexpr std::wstring_view kResolutionDiagnosticMarker =
			L"RealisticReflections_MirrorAuthorResolutionDiagnostic.enable";

		struct MarkerObservation
		{
			fs::path path{};
			bool readSucceeded{ false };
			bool present{ false };
			std::error_code error{};
		};

		struct RuntimeCounters
		{
			std::atomic<std::uint64_t> postLoadCalls{ 0 };
			std::atomic<std::uint64_t> dispatchAttempts{ 0 };
			std::atomic<std::uint64_t> dispatchSuccesses{ 0 };
			std::atomic<std::uint64_t> dispatchFailures{ 0 };
			std::atomic<std::uint64_t> dispatchCxxFaults{ 0 };
			std::atomic<std::uint64_t> manifestLoadAttempts{ 0 };
			std::atomic<std::uint64_t> manifestLoadSuccesses{ 0 };
			std::atomic<std::uint64_t> manifestLoadFailures{ 0 };
			std::atomic<std::uint64_t> queryCalls{ 0 };
			std::atomic<std::uint64_t> querySuccesses{ 0 };
			std::atomic<std::uint64_t> registrationCalls{ 0 };
			std::atomic<std::uint64_t> dataLoadedCalls{ 0 };
			std::atomic<std::uint64_t> resolutionAttempts{ 0 };
			std::atomic<std::uint64_t> resolutionAccepted{ 0 };
			std::atomic<std::uint64_t> resolutionEmpty{ 0 };
			std::atomic<std::uint64_t> resolutionRejected{ 0 };
			std::atomic<std::uint64_t> resolutionCxxFaults{ 0 };
		};

		stl::no_destructor<Detail::CollectionWindow> g_window{};
		RuntimeCounters g_counters{};
		std::atomic_bool g_postLoadHandled{ false };
		std::atomic_bool g_dataLoadedHandled{ false };
		std::atomic_bool g_supportedRuntimeLatched{ false };
		std::atomic_bool g_collectionMarkerLatched{ false };
		std::atomic_bool g_cabiTransportMarkerLatched{ false };
		std::atomic_bool g_manifestTransportMarkerLatched{ false };
		std::atomic_bool g_resolutionMarkerLatched{ false };
		std::atomic<std::uint64_t> g_lastIssuedLifecycleToken{ 0 };

		[[nodiscard]] std::uint64_t CurrentThreadToken() noexcept
		{
			return static_cast<std::uint64_t>(GetCurrentThreadId());
		}

		[[nodiscard]] bool IsSupportedFlatRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			return (REL::Module::IsSE() &&
					version == REL::Version{ 1, 5, 97, 0 }) ||
			       (REL::Module::IsAE() &&
					SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			       SupportedRuntimePolicy::IsExactVRRuntime();
		}

		[[nodiscard]] bool TryExecutableDataDirectory(
			fs::path& output,
			std::error_code& error)
		{
			output.clear();
			error.clear();
			std::array<wchar_t, 32768> executablePath{};
			SetLastError(ERROR_SUCCESS);
			const DWORD length = GetModuleFileNameW(
				nullptr, executablePath.data(),
				static_cast<DWORD>(executablePath.size()));
			if (length == 0) {
				const auto nativeError = GetLastError();
				const auto errorValue = nativeError != ERROR_SUCCESS ?
					nativeError : static_cast<DWORD>(ERROR_GEN_FAILURE);
				error = std::error_code(
					static_cast<int>(errorValue),
					std::system_category());
				return false;
			}
			if (length >= executablePath.size()) {
				error = std::error_code(
					static_cast<int>(ERROR_INSUFFICIENT_BUFFER),
					std::system_category());
				return false;
			}
			output = fs::path{
				std::wstring_view{ executablePath.data(), length } }.parent_path() /
			       L"Data";
			return true;
		}

		[[nodiscard]] MarkerObservation ReadMarker(
			const fs::path& dataDirectory,
			const std::wstring_view markerName) noexcept
		{
			MarkerObservation observation{};
			try {
				observation.path = dataDirectory / markerName;
				const bool exists = fs::exists(observation.path, observation.error);
				if (observation.error)
					return observation;
				if (!exists) {
					observation.readSucceeded = true;
					return observation;
				}
				observation.present =
					fs::is_regular_file(observation.path, observation.error);
				observation.readSucceeded = !observation.error;
				return observation;
			} catch (...) {
				observation.readSucceeded = false;
				observation.present = false;
				observation.error = std::make_error_code(
					std::errc::not_enough_memory);
				return observation;
			}
		}

		[[nodiscard]] std::uint64_t IssueLifecycleToken() noexcept
		{
			auto current =
				g_lastIssuedLifecycleToken.load(std::memory_order_relaxed);
			for (;;) {
				if (current == (std::numeric_limits<std::uint64_t>::max)())
					return 0;
				const auto next = current + 1;
				if (g_lastIssuedLifecycleToken.compare_exchange_weak(
						current, next, std::memory_order_acq_rel,
						std::memory_order_relaxed)) {
					return next;
				}
			}
		}

		template <class T>
		[[nodiscard]] std::uint64_t IdentityToken(const T* value) noexcept
		{
			return static_cast<std::uint64_t>(
				reinterpret_cast<std::uintptr_t>(value));
		}

		[[nodiscard]] bool BasenameMatches(
			const RE::TESFile* file,
			const std::string_view expected) noexcept
		{
			return file &&
			       MirrorAuthorRegistrationCollector::FilenameEquals(
				       file->GetFilename(), expected);
		}

		[[nodiscard]] bool DataHandlerIsBusy(
			const RE::TESDataHandler* handler) noexcept
		{
			if (!handler)
				return true;
			const auto& runtimeData = handler->GetGeometryRuntimeData();
			return runtimeData.loadingFiles || runtimeData.clearingData;
		}

		[[nodiscard]] MirrorAuthorResolutionTransactionPolicy::SourceFileKind
		SourceKind(const RE::TESFile* file) noexcept
		{
			using Kind =
				MirrorAuthorResolutionTransactionPolicy::SourceFileKind;
			if (!file)
				return Kind::kUnknown;
			return file->IsLight() ? Kind::kLight : Kind::kFull;
		}

		[[nodiscard]] MirrorAuthorResolutionTransactionPolicy::ResolvedRecordKind
		RecordKind(const RE::TESForm* form) noexcept
		{
			using Kind =
				MirrorAuthorResolutionTransactionPolicy::ResolvedRecordKind;
			if (!form)
				return Kind::kUnknown;
			switch (form->GetFormType()) {
			case RE::FormType::Static:
				return Kind::kStatic;
			case RE::FormType::MovableStatic:
				return Kind::kMovableStatic;
			default:
				return Kind::kOther;
			}
		}

		[[nodiscard]] MirrorAuthorResolutionTransactionPolicy::EntryPrelookupEvidence
		BuildEntryPrelookupEvidence(
			RE::TESDataHandler* handler,
			const MirrorAuthorRegistrationCollector::OwnedDeclaration& declaration,
			const std::size_t frozenIndex,
			const RE::TESFile*& discoveredSource)
		{
			using namespace MirrorAuthorResolutionTransactionPolicy;
			discoveredSource = nullptr;
			EntryPrelookupEvidence evidence{
				.frozenIndex = frozenIndex,
				.declarationCopySucceeded = true,
				.dataHandlerExists = handler != nullptr,
				.dataHandlerLoadingOrClearing = DataHandlerIsBusy(handler),
				.filenameHasESLExtension =
					MirrorAuthorRegistrationCollector::HasExplicitESLExtension(
						declaration.PluginName()),
				.declaredLocalFormID = declaration.localFormID
			};
			if (!handler || evidence.dataHandlerLoadingOrClearing)
				return evidence;

			discoveredSource =
				handler->LookupModByName(declaration.PluginName());
			evidence.sourceLookupResolved = discoveredSource != nullptr;
			if (!discoveredSource)
				return evidence;

			evidence.sourceBasenameMatches =
				BasenameMatches(discoveredSource, declaration.PluginName());
			evidence.sourceKind = SourceKind(discoveredSource);
			evidence.sourceFileToken = IdentityToken(discoveredSource);
			evidence.compileIndex = discoveredSource->GetCompileIndex();
			evidence.smallFileCompileIndex =
				discoveredSource->GetSmallFileCompileIndex();

			// The opposite index namespace is deliberately N/A zero.  A full
			// plugin's small-file index is not an audited light-list lookup key,
			// and the two index namespaces may legitimately contain the same number.
			if (evidence.sourceKind == SourceFileKind::kLight) {
				evidence.loadedFullByNameToken = IdentityToken(
					handler->LookupLoadedModByName(declaration.PluginName()));
				evidence.loadedLightByNameToken = IdentityToken(
					handler->LookupLoadedLightModByName(declaration.PluginName()));
				evidence.loadedLightByIndexToken = IdentityToken(
					handler->LookupLoadedLightModByIndex(
						evidence.smallFileCompileIndex));
				evidence.loadedFullByIndexToken = 0;
			} else if (evidence.sourceKind == SourceFileKind::kFull) {
				evidence.loadedFullByNameToken = IdentityToken(
					handler->LookupLoadedModByName(declaration.PluginName()));
				evidence.loadedLightByNameToken = IdentityToken(
					handler->LookupLoadedLightModByName(declaration.PluginName()));
				evidence.loadedFullByIndexToken = IdentityToken(
					handler->LookupLoadedModByIndex(evidence.compileIndex));
				evidence.loadedLightByIndexToken = 0;
			}
			return evidence;
		}

		[[nodiscard]] MirrorAuthorResolutionTransactionPolicy::EntryResolutionEvidence
		BuildEntryResolutionEvidence(
			RE::TESDataHandler* handler,
			const RE::TESFile* discoveredSource,
			const MirrorAuthorRegistrationCollector::OwnedDeclaration& declaration,
			const std::size_t frozenIndex)
		{
			using namespace MirrorAuthorResolutionTransactionPolicy;
			EntryResolutionEvidence evidence{ .frozenIndex = frozenIndex };
			if (!handler || !discoveredSource || DataHandlerIsBusy(handler))
				return evidence;

			const auto pluginName = declaration.PluginName();
			const auto runtimeFormID =
				handler->LookupFormID(declaration.localFormID, pluginName);
			auto* const untyped =
				handler->LookupForm(declaration.localFormID, pluginName);
			auto* const exactStatic = handler->LookupForm<RE::TESObjectSTAT>(
				declaration.localFormID, pluginName);

			evidence.lookupFormIDResolved = runtimeFormID != 0;
			evidence.lookupRuntimeFormID = runtimeFormID;
			evidence.untypedFormToken = IdentityToken(untyped);
			evidence.exactStaticFormToken = IdentityToken(exactStatic);
			evidence.resolvedRecordKind = RecordKind(untyped);
			if (!untyped)
				return evidence;

			evidence.resolvedRuntimeFormID = untyped->GetFormID();
			auto* const origin = untyped->GetFile(0);
			evidence.originSourceFileToken = IdentityToken(origin);
			evidence.originBasenameMatches =
				BasenameMatches(origin, pluginName);
			if (origin)
				evidence.resolvedLocalFormID = untyped->GetLocalFormID();
			evidence.sourceOwnsForm =
				discoveredSource->IsFormInMod(untyped->GetFormID());
			evidence.deleted = untyped->IsDeleted();
			return evidence;
		}

		[[nodiscard]] MirrorAuthorResolutionTransactionPolicy::FrozenV1IdentityEvidence
		ResolveFrozenV1Identity(
			RE::TESDataHandler* handler,
			const std::size_t frozenV1Index)
		{
			using namespace MirrorAuthorResolutionTransactionPolicy;
			const auto& identity =
				MirrorAuthoringContract::kFrozenV1Bases[frozenV1Index];
			FrozenV1IdentityEvidence evidence{
				.frozenV1Index = frozenV1Index
			};
			const bool handlerExists = handler != nullptr;
			const bool dataHandlerBusy = DataHandlerIsBusy(handler);
			if (!Detail::MayLookupFrozenV1Sources(
					handlerExists, dataHandlerBusy)) {
				if (Detail::FrozenV1LookupFailureIsActiveMalformed(
						handlerExists, dataHandlerBusy)) {
					// Busy is not an optional inactive first-party slot.  Mark the slot
					// active-but-unresolved so the alias transaction rejects without any
					// TESDataHandler lookup while loading/clearing is in progress.
					evidence.sourceActive = true;
				}
				return evidence;
			}

			const auto* const discovered =
				handler->LookupModByName(identity.pluginName);
			const auto* const loadedFull =
				handler->LookupLoadedModByName(identity.pluginName);
			const auto* const loadedLight =
				handler->LookupLoadedLightModByName(identity.pluginName);
			const bool compiled = discovered &&
				discovered->GetCompileIndex() != 0xFFU;
			evidence.sourceActive =
				compiled || loadedFull != nullptr || loadedLight != nullptr;
			if (!evidence.sourceActive)
				return evidence;
			if (!discovered ||
				!BasenameMatches(discovered, identity.pluginName)) {
				return evidence;
			}

			std::uint32_t expectedRuntimeFormID = 0;
			if (discovered->IsLight()) {
				const auto compileIndex = discovered->GetCompileIndex();
				const auto smallIndex = discovered->GetSmallFileCompileIndex();
				if (compileIndex != kLightCompileIndex || smallIndex > 0x0FFFU ||
					identity.localFormID <
						MirrorAuthorRegistrationPolicy::
							kMinimumLightPluginOwnedLocalFormID ||
					identity.localFormID >
						MirrorAuthorRegistrationPolicy::
							kMaximumLightPluginLocalFormID ||
					loadedFull != nullptr || loadedLight != discovered ||
					handler->LookupLoadedLightModByIndex(smallIndex) != discovered) {
					return evidence;
				}
				expectedRuntimeFormID =
					PackLightRuntimeFormID(smallIndex, identity.localFormID);
			} else {
				const auto compileIndex = discovered->GetCompileIndex();
				if (compileIndex >= kLightCompileIndex || identity.localFormID == 0 ||
					identity.localFormID >
						MirrorAuthorRegistrationPolicy::
							kMaximumFullPluginLocalFormID ||
					loadedLight != nullptr || loadedFull != discovered ||
					handler->LookupLoadedModByIndex(compileIndex) != discovered) {
					return evidence;
				}
				expectedRuntimeFormID =
					PackFullRuntimeFormID(compileIndex, identity.localFormID);
			}

			const auto lookupRuntimeFormID =
				handler->LookupFormID(identity.localFormID, identity.pluginName);
			auto* const untyped =
				handler->LookupForm(identity.localFormID, identity.pluginName);
			auto* const exactStatic = handler->LookupForm<RE::TESObjectSTAT>(
				identity.localFormID, identity.pluginName);
			if (lookupRuntimeFormID == 0 ||
				lookupRuntimeFormID != expectedRuntimeFormID || !untyped ||
				!exactStatic || untyped != exactStatic ||
				untyped->GetFormType() != RE::FormType::Static ||
				untyped->GetFormID() != expectedRuntimeFormID) {
				return evidence;
			}

			auto* const origin = untyped->GetFile(0);
			if (origin != discovered ||
				!BasenameMatches(origin, identity.pluginName) ||
				untyped->GetLocalFormID() != identity.localFormID ||
				!discovered->IsFormInMod(untyped->GetFormID()) ||
				untyped->IsDeleted()) {
				return evidence;
			}

			evidence.exactResolutionSucceeded = true;
			evidence.exactStaticFormToken = IdentityToken(exactStatic);
			evidence.runtimeFormID = exactStatic->GetFormID();
			return evidence;
		}

		extern "C" std::uint32_t RR_MIRROR_AUTHOR_CALL RegisterBaseThunk(
			const std::uint64_t lifecycleToken,
			const RR_MirrorAuthorBaseV1* declaration) noexcept
		{
			g_counters.registrationCalls.fetch_add(1, std::memory_order_relaxed);
			return Detail::RegisterFromCaller(
				g_window.get(), CurrentThreadToken(), lifecycleToken, declaration);
		}

		using ManifestLoadResult = MirrorAuthorCatalogRuntimeLoader::LoadResult;

		struct CollectionRunContext
		{
			const SKSE::MessagingInterface* messaging;
			ManifestLoadResult** manifestScratchSlot;
			std::uint64_t lifecycleToken;
			std::uint64_t ownerThreadToken;
			std::uint8_t requestedTransportMask;
			Detail::CollectionCompletionDisposition disposition;
			std::uint32_t finalizationResult;
			std::uint32_t manifestStageResult;
			std::uint32_t openCABIResult;
			bool manifestAttempted;
			bool manifestReturnedNormally;
			bool manifestResult;
			bool dispatchAttempted;
			bool dispatchReturnedNormally;
			bool dispatchResult;
		};
		static_assert(std::is_trivial_v<CollectionRunContext>);
		static_assert(std::is_standard_layout_v<CollectionRunContext>);

		void SecureDeleteManifestScratch(
			ManifestLoadResult*& scratch) noexcept
		{
			auto* const owned = scratch;
			scratch = nullptr;
			if (owned) {
				(void)SecureZeroMemory(owned, sizeof(*owned));
				delete owned;
			}
		}

		[[nodiscard]] std::array<char, 65> DigestHex(
			const MirrorAuthorCatalogDiscoveryPolicy::Digest256& digest) noexcept
		{
			constexpr char kDigits[] = "0123456789ABCDEF";
			std::array<char, 65> output{};
			for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
				const auto value = static_cast<unsigned char>(digest.bytes[index]);
				output[index * 2] = kDigits[value >> 4U];
				output[index * 2 + 1] = kDigits[value & 0x0FU];
			}
			return output;
		}

		void LogManifestLoad(const ManifestLoadResult& load) noexcept
		{
			try {
				if (!load.Succeeded()) {
					logger::error(
						"[RR][MirrorAuthorManifestDiagnostic] load rejected issue={} native={} discovery={} parser={} parserOffset={} builder={} merge={} sourceReads={} authority=false",
						std::to_underlying(load.issue), load.nativeError,
						std::to_underlying(load.discoveryIssue),
						std::to_underlying(load.parseIssue), load.parseErrorOffset,
						std::to_underlying(load.builderIssue),
						std::to_underlying(load.mergeIssue),
						load.sourceFileRequests);
					return;
				}

				const auto cohortDigest = DigestHex(load.inputCohortDigest);
				const auto semanticDigest = DigestHex(load.mergedSemanticDigest);
				logger::info(
					"[RR][MirrorAuthorManifestDiagnostic] accepted directoryMissing={} files={} aggregateBytes={} rawRegistrations={} uniqueBases={} inputCohortSHA256={} mergedSemanticSHA256={} hashAuthority=false tesAuthority=false recognition=false delivery=false supportedFeature=false",
					load.cohort.directoryMissing, load.cohort.fileCount,
					load.cohort.aggregateLength,
					load.cohort.rawRegistrationCount,
					load.cohort.catalog.entryCount,
					std::string_view{ cohortDigest.data(), cohortDigest.size() - 1 },
					std::string_view{ semanticDigest.data(), semanticDigest.size() - 1 });
				for (const auto& file : load.cohort.FileSummaries()) {
					const auto rawDigest = DigestHex(file.contentDigest);
					logger::info(
						"[RR][MirrorAuthorManifestDiagnostic] file={} bytes={} rawSHA256={} hashAuthority=false",
						file.leaf.View(), file.byteCount,
						std::string_view{ rawDigest.data(), rawDigest.size() - 1 });
				}
			} catch (...) {
				// Hashes and logging are diagnostic facts only and cannot authorize input.
			}
		}

		[[nodiscard]] Detail::ManifestCohortFacts ManifestFactsFromLoad(
			const ManifestLoadResult& load) noexcept
		{
			return {
				.directoryMissing = load.cohort.directoryMissing,
				.fileCount = load.cohort.fileCount,
				.aggregateByteCount = load.cohort.aggregateLength,
				.rawRegistrationCount = load.cohort.rawRegistrationCount,
				.uniqueRegistrationCount = load.cohort.catalog.entryCount
			};
		}

		void RunCollectionCore(CollectionRunContext* context)
		{
			try {
				if (Detail::HasTransport(
						context->requestedTransportMask,
						Detail::kManifestTransportMask)) {
					context->manifestAttempted = true;
					auto* const load = *context->manifestScratchSlot;
					if (!load || !load->Succeeded()) {
						context->manifestReturnedNormally = true;
						context->manifestResult = false;
						context->disposition = Detail::
							CollectionCompletionDisposition::kReturnedNormally;
						SecureDeleteManifestScratch(*context->manifestScratchSlot);
						return;
					}

					for (std::size_t index = 0;
						index < load->cohort.catalog.entryCount; ++index) {
						const auto& declaration =
							load->cohort.catalog.registrations[index];
						context->manifestStageResult = g_window.get().StageManifest(
							context->ownerThreadToken, context->lifecycleToken, {
								.sourcePlugin = declaration.PluginName(),
								.localFormID = declaration.localFormID,
								.mirrorSchema = declaration.mirrorSchema,
								.flags = declaration.flags
							});
						if (context->manifestStageResult !=
							RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
							context->manifestReturnedNormally = true;
							context->manifestResult = false;
							context->disposition = Detail::
								CollectionCompletionDisposition::kReturnedNormally;
							SecureDeleteManifestScratch(
								*context->manifestScratchSlot);
							return;
						}
					}

					context->manifestStageResult =
						g_window.get().CompleteManifestStaging(
							context->ownerThreadToken, context->lifecycleToken,
							ManifestFactsFromLoad(*load));
					context->manifestReturnedNormally = true;
					context->manifestResult = context->manifestStageResult ==
						RR_MIRROR_AUTHOR_RESULT_SUCCESS;
					SecureDeleteManifestScratch(*context->manifestScratchSlot);
					if (!context->manifestResult) {
						context->disposition = Detail::
							CollectionCompletionDisposition::kReturnedNormally;
						return;
					}
				} else {
					SecureDeleteManifestScratch(*context->manifestScratchSlot);
				}

				// Manifest scratch is gone before Query can become visible and before
				// any peer listener runs.
				if (Detail::HasTransport(
						context->requestedTransportMask,
						Detail::kCABITransportMask)) {
					if (!context->messaging) {
						context->disposition = Detail::
							CollectionCompletionDisposition::kReturnedNormally;
						return;
					}
					context->openCABIResult = g_window.get().OpenCABI(
						context->ownerThreadToken, context->lifecycleToken);
					if (context->openCABIResult !=
						RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
						context->disposition = Detail::
							CollectionCompletionDisposition::kReturnedNormally;
						return;
					}
					context->dispatchAttempted = true;
					g_counters.dispatchAttempts.fetch_add(
						1, std::memory_order_relaxed);
					context->dispatchResult = context->messaging->Dispatch(
						RR_MIRROR_AUTHOR_PROVISIONAL_MESSAGE_COLLECT_V1,
						nullptr, 0, nullptr);
					context->dispatchReturnedNormally = true;
				}
				context->disposition = Detail::
					CollectionCompletionDisposition::kReturnedNormally;
			} catch (const std::exception&) {
				context->disposition = Detail::
					CollectionCompletionDisposition::kStandardCppException;
				throw;
			} catch (...) {
				// Under /EHsc this is a non-standard C++ exception.  Arbitrary SEH
				// bypasses both catches and retains the propagating-SEH default.
				context->disposition = Detail::
					CollectionCompletionDisposition::kNonStandardCppException;
				throw;
			}
		}

		void FinalizeCollectionAndScrub(CollectionRunContext* context) noexcept
		{
			context->finalizationResult = Detail::FinalizeCollection(
				g_window.get(), context->ownerThreadToken, {
					.disposition = context->disposition,
					.manifestAttempted = context->manifestAttempted,
					.manifestReturnedNormally =
						context->manifestReturnedNormally,
					.manifestResult = context->manifestResult,
					.dispatchAttempted = context->dispatchAttempted,
					.dispatchReturnedNormally =
						context->dispatchReturnedNormally,
					.dispatchResult = context->dispatchResult
				});
			// Finalization deliberately precedes the idempotent scratch cleanup.
			SecureDeleteManifestScratch(*context->manifestScratchSlot);
		}

		// POD-only MSVC SEH leaf covering every action after BeginStaging.  There
		// is deliberately no __except: arbitrary SEH aborts/scrubs in __finally,
		// then continues propagating under /EHsc.
		__declspec(noinline) void RunCollectionAndFinalize(
			CollectionRunContext* context)
		{
			__try {
				RunCollectionCore(context);
			} __finally {
				FinalizeCollectionAndScrub(context);
			}
		}

		using FrozenV1Snapshot = std::array<
			MirrorAuthorResolutionTransactionPolicy::FrozenV1IdentityEvidence,
			MirrorAuthorResolutionTransactionPolicy::kFrozenV1BaseCount>;

		struct ResolutionRunContext
		{
			MirrorAuthorResolutionTransactionPolicy::ResolutionTransaction* transaction;
			Detail::FrozenSnapshot* frozenSnapshot;
			FrozenV1Snapshot* frozenV1Snapshot;
			std::uint64_t lifecycleToken;
			std::uint64_t currentThreadToken;
			MirrorAuthorResolutionTransactionPolicy::CompletionDisposition completion;
			MirrorAuthorResolutionTransactionPolicy::Rejection cleanupResult;
			bool returnedNormally;
		};
		static_assert(std::is_trivial_v<ResolutionRunContext>);
		static_assert(std::is_standard_layout_v<ResolutionRunContext>);

		void RunResolutionCore(ResolutionRunContext* context)
		{
			using namespace MirrorAuthorResolutionTransactionPolicy;
			try {
				auto* const handler = RE::TESDataHandler::GetSingleton();
				for (std::size_t index = 0;
					index < context->frozenSnapshot->count; ++index) {
					const auto& declaration =
						context->frozenSnapshot->declarations[index];
					const RE::TESFile* discoveredSource = nullptr;
					const auto prelookup = BuildEntryPrelookupEvidence(
						handler, declaration, index, discoveredSource);
					if (context->transaction->BeginEntry(
							context->lifecycleToken,
							context->currentThreadToken,
							prelookup) != Rejection::kNone) {
						context->returnedNormally = true;
						context->completion =
							CompletionDisposition::kReturnedNormally;
						return;
					}

					const auto resolved = BuildEntryResolutionEvidence(
						handler, discoveredSource, declaration, index);
					if (context->transaction->CompleteEntry(
							context->lifecycleToken,
							context->currentThreadToken,
							resolved) != Rejection::kNone) {
						context->returnedNormally = true;
						context->completion =
							CompletionDisposition::kReturnedNormally;
						return;
					}
				}

				if (context->transaction->CloseResolutionPass(
						context->lifecycleToken,
						context->currentThreadToken,
						CompletionDisposition::kReturnedNormally) !=
					Rejection::kNone) {
					context->returnedNormally = true;
					context->completion =
						CompletionDisposition::kReturnedNormally;
					return;
				}

				for (std::size_t index = 0;
					index < context->frozenV1Snapshot->size(); ++index) {
					(*context->frozenV1Snapshot)[index] =
						ResolveFrozenV1Identity(handler, index);
				}
				for (const auto& identity : *context->frozenV1Snapshot) {
					if (context->transaction->AddFrozenV1Identity(
							context->lifecycleToken,
							context->currentThreadToken,
							identity) != Rejection::kNone) {
						context->returnedNormally = true;
						context->completion =
							CompletionDisposition::kReturnedNormally;
						return;
					}
				}

				(void) context->transaction->CloseFrozenV1AliasPass(
					context->lifecycleToken,
					context->currentThreadToken,
					CompletionDisposition::kReturnedNormally);
				context->returnedNormally = true;
				context->completion = CompletionDisposition::kReturnedNormally;
			} catch (const std::exception&) {
				context->completion =
					CompletionDisposition::kStandardCppException;
				throw;
			} catch (...) {
				// With /EHsc this classifies only a non-standard C++ exception.
				// Arbitrary SEH bypasses both catches and retains the propagating-SEH
				// default for the POD __finally leaf below.
				context->completion =
					CompletionDisposition::kNonStandardCppException;
				throw;
			}
		}

		void CleanupResolutionTransaction(ResolutionRunContext* context) noexcept
		{
			using namespace MirrorAuthorResolutionTransactionPolicy;
			if (!context->returnedNormally) {
				const auto phase = context->transaction->Snapshot().phase;
				if (phase == TransactionPhase::kResolvingEntries ||
					phase == TransactionPhase::kAwaitingEntryResolution) {
					context->cleanupResult =
						context->transaction->CloseResolutionPass(
							context->lifecycleToken,
							context->currentThreadToken,
							context->completion);
				} else if (phase == TransactionPhase::kCheckingFrozenV1Aliases) {
					context->cleanupResult =
						context->transaction->CloseFrozenV1AliasPass(
							context->lifecycleToken,
							context->currentThreadToken,
							context->completion);
				}
			}
			SecureZeroMemory(
				context->frozenSnapshot, sizeof(*context->frozenSnapshot));
			SecureZeroMemory(
				context->frozenV1Snapshot, sizeof(*context->frozenV1Snapshot));
		}

		// This leaf owns no C++ object.  It deliberately has no __except: native TES
		// calls execute beneath it, arbitrary SEH triggers value-only cleanup in the
		// __finally, then continues propagating.  C++ failures have already written
		// their exact disposition before normal C++ unwind reaches this boundary.
		__declspec(noinline) void RunResolutionAndCleanup(
			ResolutionRunContext* context)
		{
			__try {
				RunResolutionCore(context);
			} __finally {
				CleanupResolutionTransaction(context);
			}
		}

		void LogResolutionTerminal(
			const std::string_view outcome,
			const MirrorAuthorResolutionTransactionPolicy::TransactionSnapshot&
				snapshot) noexcept
		{
			try {
				logger::info(
					"[RR][MirrorAuthorResolutionDiagnostic] outcome={} phase={} rejection={} frozen={} processed={} eligible={} distinctPointers={} distinctRuntimeIDs={} frozenV1Visited={} frozenV1Resolved={} retainedIdentityValues={} tesAuthority=false recognition=false delivery=false supportedFeature=false",
					outcome, std::to_underlying(snapshot.phase),
					std::to_underlying(snapshot.rejection), snapshot.frozenCount,
					snapshot.processedCount, snapshot.eligibleCount,
					snapshot.distinctThirdPartyIdentityCount,
					snapshot.distinctThirdPartyRuntimeFormIDCount,
					snapshot.processedFrozenV1Count,
					snapshot.resolvedFrozenV1Count,
					snapshot.retainedNumericIdentityCount);
			} catch (...) {
				// Aggregate logging is outside the transaction and cannot authorize it.
			}
		}

		void LogFrozenCatalog(const std::uint32_t finalizationResult) noexcept
		{
			try {
				const auto& window = g_window.get();
				const auto frozenCount = window.FrozenCount();
				logger::info(
					"[RR][MirrorAuthorDiagnostic] finalization result={} token={} entries={} quarantined={} cause={} runtimeAuthority=false",
					finalizationResult, window.LifecycleToken(), frozenCount,
					window.Quarantined(), window.QuarantineCause());
				for (std::size_t index = 0; index < frozenCount; ++index) {
					MirrorAuthorRegistrationCollector::OwnedDeclaration declaration{};
					if (!window.TryCopyFrozen(index, declaration)) {
						logger::error(
							"[RR][MirrorAuthorDiagnostic] token-bound frozen copy failed at index={}",
							index);
						break;
					}
					logger::info(
						"[RR][MirrorAuthorDiagnostic] frozen[{}] plugin={} local={:06X} schema={} flags={} transports={:02X} authority=false",
						index, declaration.PluginName(), declaration.localFormID,
						declaration.mirrorSchema, declaration.flags,
						declaration.transportMask);
				}
			} catch (...) {
				// Logging is deliberately outside the callback/window.  A formatting or
				// sink failure cannot reopen collection or grant runtime authority.
			}
		}
	}

	void OnPostLoad() noexcept
	{
		ManifestLoadResult* manifestScratch = nullptr;
		CollectionRunContext runContext{};
		try {
			g_counters.postLoadCalls.fetch_add(1, std::memory_order_relaxed);
			if (g_postLoadHandled.exchange(true, std::memory_order_acq_rel))
				return;

			const bool supportedRuntime = IsSupportedFlatRuntime();
			g_supportedRuntimeLatched.store(
				supportedRuntime, std::memory_order_release);

			fs::path dataDirectory{};
			std::error_code dataDirectoryError{};
			const bool dataDirectoryDerived = TryExecutableDataDirectory(
				dataDirectory, dataDirectoryError);
			MarkerObservation cabiMarker{};
			MarkerObservation manifestMarker{};
			MarkerObservation resolutionMarker{};
			if (dataDirectoryDerived) {
				cabiMarker = ReadMarker(
					dataDirectory, kCABITransportDiagnosticMarker);
				manifestMarker = ReadMarker(
					dataDirectory, kManifestTransportDiagnosticMarker);
				resolutionMarker = ReadMarker(
					dataDirectory, kResolutionDiagnosticMarker);
			} else {
				cabiMarker.error = dataDirectoryError;
				manifestMarker.error = dataDirectoryError;
				resolutionMarker.error = dataDirectoryError;
			}

			Detail::ActivationObservation activation{
				.supportedRuntime = supportedRuntime,
				.cabiMarkerReadSucceeded = cabiMarker.readSucceeded,
				.cabiMarkerPresent = cabiMarker.present,
				.manifestMarkerReadSucceeded = manifestMarker.readSucceeded,
				.manifestMarkerPresent = manifestMarker.present,
				.resolutionMarkerReadSucceeded =
					resolutionMarker.readSucceeded,
				.resolutionMarkerPresent = resolutionMarker.present,
				.messagingAvailable = false
			};
			auto selection = Detail::SelectDiagnosticTransports(activation);
			const bool wantsCABI = Detail::HasTransport(
				selection.requestedTransportMask,
				Detail::kCABITransportMask);
			auto* const messaging = supportedRuntime && wantsCABI ?
				SKSE::GetMessagingInterface() : nullptr;
			activation.messagingAvailable = messaging != nullptr;
			selection = Detail::SelectDiagnosticTransports(activation);

			const bool exactMarkerReads = selection.markerReadsExact;
			const bool cabiMarkerLatched = exactMarkerReads && cabiMarker.present;
			const bool manifestMarkerLatched =
				exactMarkerReads && manifestMarker.present;
			const bool anyTransportMarkerLatched =
				cabiMarkerLatched || manifestMarkerLatched;
			g_collectionMarkerLatched.store(
				anyTransportMarkerLatched,
				std::memory_order_release);
			g_cabiTransportMarkerLatched.store(
				cabiMarkerLatched,
				std::memory_order_release);
			g_manifestTransportMarkerLatched.store(
				manifestMarkerLatched,
				std::memory_order_release);
			g_resolutionMarkerLatched.store(
				resolutionMarker.readSucceeded && resolutionMarker.present,
				std::memory_order_release);
			if (!supportedRuntime) {
				logger::info(
					"[RR][MirrorAuthorDiagnostic] idle: runtime {} is outside exact SE 1.5.97 / AE 1.6.1170 gate",
					REL::Module::get().version().string());
				return;
			}
			if (!selection.markerReadsExact) {
				logger::error(
					"[RR][MirrorAuthorDiagnostic] inert: exact transport-marker observation failed dataPath={} dataError={} cabiRead={} cabiError={} manifestRead={} manifestError={} tesAccess=false authority=false",
					dataDirectory.string(), dataDirectoryError.value(),
					cabiMarker.readSucceeded, cabiMarker.error.value(),
					manifestMarker.readSucceeded, manifestMarker.error.value());
				return;
			}
			if (!resolutionMarker.readSucceeded) {
				logger::error(
					"[RR][MirrorAuthorResolutionDiagnostic] independent resolution-marker read failed path={} error={} ({}); collection may continue but DataLoaded TES access remains inert",
					resolutionMarker.path.string(),
					resolutionMarker.error.value(),
					resolutionMarker.error.message());
			}
			if (selection.requestedTransportMask == 0) {
				logger::info(
					"[RR][MirrorAuthorDiagnostic] default-off; no exact CABI or manifest transport marker is present cabiPath={} manifestPath={} resolutionMarkerLatched={} tesAccess=false authority=false",
					cabiMarker.path.string(), manifestMarker.path.string(),
					resolutionMarker.present);
				return;
			}
			if (!selection.shouldBegin) {
				logger::error(
					"[RR][MirrorAuthorDiagnostic] inert: requestedTransports={:02X} messagingAvailable={} (messaging is required iff CABI is selected) authority=false",
					selection.requestedTransportMask, messaging != nullptr);
				return;
			}

			if (Detail::HasTransport(
					selection.requestedTransportMask,
					Detail::kManifestTransportMask)) {
				g_counters.manifestLoadAttempts.fetch_add(
					1, std::memory_order_relaxed);
				manifestScratch = new (std::nothrow) ManifestLoadResult{};
				if (!manifestScratch) {
					g_counters.manifestLoadFailures.fetch_add(
						1, std::memory_order_relaxed);
					logger::error(
						"[RR][MirrorAuthorManifestDiagnostic] heap allocation failed before collection; dispatchSkipped=true authority=false");
					return;
				}
				MirrorAuthorCatalogRuntimeLoader::LoadFromDataDirectoryInto(
					dataDirectory.native(), *manifestScratch);
				LogManifestLoad(*manifestScratch);
				if (!manifestScratch->Succeeded()) {
					g_counters.manifestLoadFailures.fetch_add(
						1, std::memory_order_relaxed);
					SecureDeleteManifestScratch(manifestScratch);
					return;
				}
				g_counters.manifestLoadSuccesses.fetch_add(
					1, std::memory_order_relaxed);
			}

			const auto lifecycleToken = IssueLifecycleToken();
			const auto ownerThreadToken = CurrentThreadToken();
			if (lifecycleToken == 0 || ownerThreadToken == 0) {
				SecureDeleteManifestScratch(manifestScratch);
				logger::critical(
					"[RR][MirrorAuthorDiagnostic] lifecycle token/thread issuance failed; collection remains closed");
				return;
			}

			runContext = {
				.messaging = messaging,
				.manifestScratchSlot = &manifestScratch,
				.lifecycleToken = lifecycleToken,
				.ownerThreadToken = ownerThreadToken,
				.requestedTransportMask = selection.requestedTransportMask,
				.disposition = Detail::CollectionCompletionDisposition::
					kPropagatingStructuredException,
				.finalizationResult = RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED,
				.manifestStageResult = RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED,
				.openCABIResult = RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED,
				.manifestAttempted = false,
				.manifestReturnedNormally = false,
				.manifestResult = false,
				.dispatchAttempted = false,
				.dispatchReturnedNormally = false,
				.dispatchResult = false
			};
			const auto beginResult = g_window.get().BeginStaging(
				lifecycleToken, ownerThreadToken,
				selection.requestedTransportMask,
				wantsCABI ? &RegisterBaseThunk : nullptr);
			if (beginResult != RR_MIRROR_AUTHOR_RESULT_SUCCESS) {
				SecureDeleteManifestScratch(manifestScratch);
				logger::critical(
					"[RR][MirrorAuthorDiagnostic] staging begin failed result={} requestedTransports={:02X}; query remains null",
					beginResult, selection.requestedTransportMask);
				return;
			}
			try {
				RunCollectionAndFinalize(&runContext);
			} catch (const std::exception& exception) {
				if (runContext.dispatchAttempted) {
					g_counters.dispatchFailures.fetch_add(
						1, std::memory_order_relaxed);
					g_counters.dispatchCxxFaults.fetch_add(
						1, std::memory_order_relaxed);
				}
				logger::error(
					"[RR][MirrorAuthorDiagnostic] collection threw standard C++ after unified abort result={} manifestAttempted={} dispatchAttempted={} what={} authority=false",
					runContext.finalizationResult,
					runContext.manifestAttempted, runContext.dispatchAttempted,
					exception.what());
				LogFrozenCatalog(runContext.finalizationResult);
				return;
			} catch (...) {
				if (runContext.dispatchAttempted) {
					g_counters.dispatchFailures.fetch_add(
						1, std::memory_order_relaxed);
					g_counters.dispatchCxxFaults.fetch_add(
						1, std::memory_order_relaxed);
				}
				logger::error(
					"[RR][MirrorAuthorDiagnostic] collection threw non-standard C++ after unified abort result={} manifestAttempted={} dispatchAttempted={} authority=false",
					runContext.finalizationResult,
					runContext.manifestAttempted, runContext.dispatchAttempted);
				LogFrozenCatalog(runContext.finalizationResult);
				return;
			}
			if (runContext.dispatchAttempted) {
				if (runContext.dispatchReturnedNormally && runContext.dispatchResult) {
					g_counters.dispatchSuccesses.fetch_add(
						1, std::memory_order_relaxed);
				} else {
					g_counters.dispatchFailures.fetch_add(
						1, std::memory_order_relaxed);
				}
			}
			logger::info(
				"[RR][MirrorAuthorDiagnostic] PostLoad collection requestedTransports={:02X} manifestAttempted={} manifestReturned={} manifestResult={} dispatchAttempted={} dispatchReturned={} dispatchResult={} finalization={} activeAfter={} cabiMarkerLatched={} manifestMarkerLatched={} resolutionMarkerLatched={} recognitionAuthority=false delivery=false supportedFeature=false",
				selection.requestedTransportMask,
				runContext.manifestAttempted,
				runContext.manifestReturnedNormally,
				runContext.manifestResult,
				runContext.dispatchAttempted,
				runContext.dispatchReturnedNormally,
				runContext.dispatchResult,
				runContext.finalizationResult,
				g_window.get().Active(),
				g_cabiTransportMarkerLatched.load(std::memory_order_acquire),
				g_manifestTransportMarkerLatched.load(std::memory_order_acquire),
				g_resolutionMarkerLatched.load(std::memory_order_acquire));
			LogFrozenCatalog(runContext.finalizationResult);
		} catch (const std::exception& exception) {
			SecureDeleteManifestScratch(manifestScratch);
			try {
				logger::error(
					"[RR][MirrorAuthorDiagnostic] pre-begin/terminal logging standard C++ failure contained what={} authority=false",
					exception.what());
			} catch (...) {
				// The default-off diagnostic cannot affect the product lifecycle.
			}
		} catch (...) {
			// /EHsc does not translate arbitrary SEH into this C++ catch.  Every
			// post-Begin action is already under RunCollectionAndFinalize.
			SecureDeleteManifestScratch(manifestScratch);
			try {
				logger::error(
					"[RR][MirrorAuthorDiagnostic] pre-begin/terminal logging non-standard C++ failure contained authority=false");
			} catch (...) {
				// The default-off diagnostic cannot affect the product lifecycle.
			}
		}
	}

	void OnDataLoaded() noexcept
	{
		using namespace MirrorAuthorResolutionTransactionPolicy;
		g_counters.dataLoadedCalls.fetch_add(1, std::memory_order_relaxed);
		// Consume the lifecycle before any TESDataHandler/TESFile/TESForm access.
		if (g_dataLoadedHandled.exchange(true, std::memory_order_acq_rel))
			return;

		const bool supportedRuntime =
			g_supportedRuntimeLatched.load(std::memory_order_acquire) &&
			IsSupportedFlatRuntime();
		const bool collectionMarkerLatched =
			g_collectionMarkerLatched.load(std::memory_order_acquire);
		const bool cabiTransportMarkerLatched =
			g_cabiTransportMarkerLatched.load(std::memory_order_acquire);
		const bool manifestTransportMarkerLatched =
			g_manifestTransportMarkerLatched.load(std::memory_order_acquire);
		const bool resolutionMarkerLatched =
			g_resolutionMarkerLatched.load(std::memory_order_acquire);
		if (!supportedRuntime || !collectionMarkerLatched ||
			!resolutionMarkerLatched) {
			try {
				logger::info(
					"[RR][MirrorAuthorResolutionDiagnostic] inert at DataLoaded: supportedRuntime={} collectionMarkerLatched={} resolutionMarkerLatched={} tesAccess=false authority=false",
					supportedRuntime, collectionMarkerLatched,
					resolutionMarkerLatched);
			} catch (...) {
				// Marker-off diagnostics cannot affect the product lifecycle.
			}
			return;
		}

		g_counters.resolutionAttempts.fetch_add(1, std::memory_order_relaxed);
		auto& window = g_window.get();
		Detail::CleanFrozenReceipt receipt{};
		const bool hasCleanFrozenReceipt =
			window.TryGetCleanFrozenReceipt(receipt);
		Detail::FrozenSnapshot frozenSnapshot{};
		const bool frozenSnapshotCopySucceeded =
			hasCleanFrozenReceipt &&
			Detail::TryCopyExactFrozenSnapshot(window, frozenSnapshot);

		ResolutionTransaction transaction{};
		const auto currentThreadToken = CurrentThreadToken();
		const auto expectedLifecycleToken = window.LifecycleToken();
		const auto catalog = Detail::BuildFrozenCatalogObservation({
			.supportedRuntime = supportedRuntime,
			.cabiTransportMarkerLatched = cabiTransportMarkerLatched,
			.manifestTransportMarkerLatched = manifestTransportMarkerLatched,
			.resolutionMarkerLatched = resolutionMarkerLatched,
			.hasCleanFrozenReceipt = hasCleanFrozenReceipt,
			.frozenSnapshotCopySucceeded = frozenSnapshotCopySucceeded,
			.frozenSnapshotCount = frozenSnapshot.count
		}, receipt);
		const auto beginResult = transaction.Begin(
			catalog, expectedLifecycleToken, currentThreadToken);
		auto terminal = transaction.Snapshot();
		if (beginResult != Rejection::kNone) {
			SecureZeroMemory(&frozenSnapshot, sizeof(frozenSnapshot));
			g_counters.resolutionRejected.fetch_add(1, std::memory_order_relaxed);
			LogResolutionTerminal("rejected-before-tes", terminal);
			return;
		}
		if (terminal.phase == TransactionPhase::kEmpty) {
			SecureZeroMemory(&frozenSnapshot, sizeof(frozenSnapshot));
			g_counters.resolutionEmpty.fetch_add(1, std::memory_order_relaxed);
			LogResolutionTerminal("clean-empty-no-tes", terminal);
			return;
		}

		FrozenV1Snapshot frozenV1Snapshot{};
		ResolutionRunContext context{
			.transaction = &transaction,
			.frozenSnapshot = &frozenSnapshot,
			.frozenV1Snapshot = &frozenV1Snapshot,
			.lifecycleToken = expectedLifecycleToken,
			.currentThreadToken = currentThreadToken,
			.completion = CompletionDisposition::kPropagatingStructuredException,
			.cleanupResult = Rejection::kNone,
			.returnedNormally = false
		};
		try {
			RunResolutionAndCleanup(&context);
		} catch (const std::exception& exception) {
			g_counters.resolutionCxxFaults.fetch_add(1, std::memory_order_relaxed);
			g_counters.resolutionRejected.fetch_add(1, std::memory_order_relaxed);
			terminal = transaction.Snapshot();
			try {
				logger::error(
					"[RR][MirrorAuthorResolutionDiagnostic] standard C++ failure contained after value cleanup disposition={} rejection={} what={} authority=false",
					std::to_underlying(context.completion),
					std::to_underlying(terminal.rejection), exception.what());
			} catch (...) {
				// The transaction is already terminal and scrubbed.
			}
			LogResolutionTerminal("standard-cxx-rejected", terminal);
			return;
		} catch (...) {
			// With /EHsc this catches only a non-standard C++ exception.  Arbitrary
			// SEH has already run __finally cleanup and continues propagating.
			g_counters.resolutionCxxFaults.fetch_add(1, std::memory_order_relaxed);
			g_counters.resolutionRejected.fetch_add(1, std::memory_order_relaxed);
			terminal = transaction.Snapshot();
			try {
				logger::error(
					"[RR][MirrorAuthorResolutionDiagnostic] non-standard C++ failure contained after value cleanup disposition={} rejection={} authority=false",
					std::to_underlying(context.completion),
					std::to_underlying(terminal.rejection));
			} catch (...) {
				// The transaction is already terminal and scrubbed.
			}
			LogResolutionTerminal("nonstandard-cxx-rejected", terminal);
			return;
		}

		terminal = transaction.Snapshot();
		if (terminal.phase == TransactionPhase::kAccepted) {
			g_counters.resolutionAccepted.fetch_add(1, std::memory_order_relaxed);
			LogResolutionTerminal("diagnostic-accepted", terminal);
		} else {
			g_counters.resolutionRejected.fetch_add(1, std::memory_order_relaxed);
			LogResolutionTerminal("diagnostic-rejected", terminal);
		}
	}

	[[nodiscard]] const RR_MirrorAuthoringAPI* QueryExportInternal(
		const std::uint32_t requestedVersion) noexcept
	{
		g_counters.queryCalls.fetch_add(1, std::memory_order_relaxed);
		const auto* result = g_window.get().Query(
			requestedVersion, CurrentThreadToken());
		if (result)
			g_counters.querySuccesses.fetch_add(1, std::memory_order_relaxed);
		return result;
	}
}

extern "C" RR_MIRROR_AUTHOR_PUBLIC const RR_MirrorAuthoringAPI*
	RR_MIRROR_AUTHOR_CALL RR_GetMirrorAuthoringAPI(
		const std::uint32_t requestedAPIVersion)
{
	return MirrorAuthorRegistrationRuntime::QueryExportInternal(
		requestedAPIVersion);
}
