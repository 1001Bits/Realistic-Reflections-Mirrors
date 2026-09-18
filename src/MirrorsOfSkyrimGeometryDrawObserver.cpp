#include "PCH.h"

#include "MirrorsOfSkyrimGeometryDrawObserver.h"

#include "GeometryDrawObserverSignature.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorReadOnlyObserver.h"
#include "HandMirrorReflectionRuntime.h"
#include "PlayerDrawProbeLifetime.h"
#include "PlayerPassAncestry.h"

#include <array>
#include <cstring>
#include <type_traits>

#include <d3d11.h>

namespace MirrorsOfSkyrimGeometryDrawObserver
{
	namespace
	{
		constexpr REL::Version kSupportedSE{ 1, 5, 97, 0 };
		constexpr REL::Version kSupportedAE{ 1, 6, 1170, 0 };
		constexpr REL::RelocationID kGenericGeometryDraw{ 100847, 107637 };
		constexpr std::size_t kMaximumParentDepth = 256;
		constexpr std::uint8_t kFirstAcceptedGeometryType = 1;
		constexpr std::uint8_t kLastAcceptedGeometryType = 13;

		struct AggregateCounters
		{
			std::atomic<std::uint64_t> sessions{ 0 };
			std::atomic<std::uint64_t> sessionsWithTargetDraw{ 0 };
			std::atomic<std::uint64_t> sessionsWithPlayer{ 0 };
			std::atomic<std::uint64_t> sessionsWithSkinnedPlayer{ 0 };
			std::atomic<std::uint64_t> aliasedRootSessions{ 0 };
			std::atomic<std::uint64_t> lifetimeCleanupFaults{ 0 };
			std::atomic<std::uint64_t> drawCalls{ 0 };
			std::atomic<std::uint64_t> drawReturns{ 0 };
			std::atomic<std::uint64_t> eligibleGeometryCalls{ 0 };
			std::atomic<std::uint64_t> eligibleGeometryReturns{ 0 };
			std::atomic<std::uint64_t> invalidPassSnapshots{ 0 };
			std::atomic<std::uint64_t> missingGeometrySnapshots{ 0 };
			std::atomic<std::uint64_t> unsupportedGeometryTypes{ 0 };
			std::atomic<std::uint64_t> snapshotFaults{ 0 };
			std::atomic<std::uint64_t> targetMatchedReturns{ 0 };
			std::atomic<std::uint64_t> targetMismatches{ 0 };
			std::atomic<std::uint64_t> targetQueryFaults{ 0 };
			std::atomic<std::uint64_t> selectedRootReturns{ 0 };
			std::atomic<std::uint64_t> selectedRootSkinnedReturns{ 0 };
			std::atomic<std::uint64_t> bodyRootReturns{ 0 };
			std::atomic<std::uint64_t> bodyRootSkinnedReturns{ 0 };
			std::atomic<std::uint64_t> playerReturns{ 0 };
			std::atomic<std::uint64_t> skinnedPlayerReturns{ 0 };
			std::atomic<std::uint64_t> unrelatedReturns{ 0 };
			std::atomic<std::uint64_t> unclassifiedReturns{ 0 };
			std::atomic<std::uint64_t> truncatedParentWalks{ 0 };
			std::atomic<std::uint64_t> malformedParentWalks{ 0 };
			std::atomic<std::uint64_t> classificationFaults{ 0 };
		};

		struct ActiveObservation
		{
			Result result{};
			RE::NiAVObject* selectedRoot{ nullptr };
			RE::NiAVObject* bodyRoot{ nullptr };
			ID3D11RenderTargetView* expectedTargetRTV{ nullptr };
			std::uint64_t token{ 0 };
			bool active{ false };
		};

		struct DrawSnapshot
		{
			RE::BSGeometry* geometry{ nullptr };
			std::uint8_t geometryType{ 0 };
			bool eligible{ false };
		};

		enum class TargetStatus : std::uint8_t
		{
			kMatch,
			kMismatch,
			kFault
		};

		static_assert(std::is_trivially_copyable_v<Result> &&
			std::is_standard_layout_v<Result>);
		static_assert(std::is_trivially_copyable_v<Session> &&
			std::is_standard_layout_v<Session>);
		static_assert(std::is_trivially_copyable_v<ActiveObservation> &&
			std::is_standard_layout_v<ActiveObservation>);

		AggregateCounters g_counters{};
		std::atomic<std::uint64_t> g_nextToken{ 1 };
		std::atomic<std::uint64_t> g_beginRejects{ 0 };
		std::atomic<std::uint64_t> g_endMismatches{ 0 };
		std::atomic<std::uint64_t> g_quarantinedSessions{ 0 };
		std::atomic<std::uint64_t> g_rootRetainFailures{ 0 };
		std::atomic<std::uint64_t> g_rootReleaseFailures{ 0 };
		std::atomic<std::uint64_t> g_targetRetainFailures{ 0 };
		std::atomic<std::uint64_t> g_targetReleaseFailures{ 0 };
		std::atomic<std::uint64_t> g_installRequests{ 0 };
		std::atomic<std::uint64_t> g_unsupportedRuntimeRequests{ 0 };
		std::atomic<std::uint64_t> g_signatureFailures{ 0 };
		std::atomic<std::uint64_t> g_detourFailures{ 0 };
		std::atomic_bool g_installAttempted{ false };
		std::atomic_bool g_installed{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic<std::uintptr_t> g_targetAddress{ 0 };
		SRWLOCK g_installLock = SRWLOCK_INIT;
		thread_local ActiveObservation g_active{};

		[[nodiscard]] bool RetainRoot(RE::NiAVObject* root) noexcept
		{
			if (!root)
				return true;
			__try {
				root->IncRefCount();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_rootRetainFailures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		[[nodiscard]] bool ReleaseRoot(RE::NiAVObject* root) noexcept
		{
			if (!root)
				return true;
			__try {
				root->DecRefCount();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_rootReleaseFailures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		[[nodiscard]] bool RetainTarget(ID3D11RenderTargetView* target) noexcept
		{
			if (!target)
				return false;
			__try {
				target->AddRef();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_targetRetainFailures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		[[nodiscard]] bool ReleaseTarget(ID3D11RenderTargetView* target) noexcept
		{
			if (!target)
				return true;
			__try {
				target->Release();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_targetReleaseFailures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		[[nodiscard]] OwnedReferenceReleaseOutcome ReleaseOwnedReferences(
			ActiveObservation& observation) noexcept
		{
			auto* const selectedRoot = observation.selectedRoot;
			auto* const bodyRoot = observation.bodyRoot;
			auto* const target = observation.expectedTargetRTV;
			observation.selectedRoot = nullptr;
			observation.bodyRoot = nullptr;
			observation.expectedTargetRTV = nullptr;

			const OwnedReferenceReleaseOutcome outcome{
				.targetReleased = ReleaseTarget(target),
				.bodyRootReleased = bodyRoot && bodyRoot != selectedRoot ?
					ReleaseRoot(bodyRoot) : true,
				.selectedRootReleased = ReleaseRoot(selectedRoot)
			};
			if (!outcome.targetReleased || !outcome.bodyRootReleased ||
				!outcome.selectedRootReleased) {
				g_faulted.store(true, std::memory_order_release);
			}
			return outcome;
		}

		[[nodiscard]] bool GetRuntimeSignature(
			GeometryDrawObserverSignature::Runtime& output) noexcept
		{
			const auto version = REL::Module::get().version();
			if (REL::Module::IsSE() && version == kSupportedSE) {
				output = GeometryDrawObserverSignature::Runtime::kSE1597;
				return true;
			}
			// 1.7.104 resolves ID 107637 to 0x155F050 and its entry window equals
			// the AE one byte for byte; it is named explicitly so the AE case never
			// silently absorbs a runtime whose bytes were not verified.
			if (SupportedRuntimePolicy::IsExactAE17104Runtime()) {
				output = GeometryDrawObserverSignature::Runtime::kAE17104;
				return true;
			}
			if (SupportedRuntimePolicy::IsExactAE161179Runtime()) {
				output = GeometryDrawObserverSignature::Runtime::kAE161179;
				return true;
			}
			if (SupportedRuntimePolicy::IsExactAE161170Runtime()) {
				output = GeometryDrawObserverSignature::Runtime::kAE161170;
				return true;
			}
			if (SupportedRuntimePolicy::IsExactVRRuntime()) {
				// ID 100847 resolves through the VR address library to 0x1348390;
				// the VR entry prologue is verified by the kVR1415 signature.
				output = GeometryDrawObserverSignature::Runtime::kVR1415;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool IsExecutableRange(
			const void* address,
			const std::size_t length) noexcept
		{
			if (!address || length == 0)
				return false;
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT ||
				(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}
			const DWORD protection = info.Protect & 0xFF;
			const bool executable = protection == PAGE_EXECUTE ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
			if (!executable)
				return false;

			const auto begin = reinterpret_cast<std::uintptr_t>(address);
			const auto regionBegin =
				reinterpret_cast<std::uintptr_t>(info.BaseAddress);
			const auto regionEnd = regionBegin + info.RegionSize;
			return begin <= regionEnd && length <= regionEnd - begin;
		}

		[[nodiscard]] bool ReadEntryWindow(
			const void* address,
			std::array<std::uint8_t,
				GeometryDrawObserverSignature::kWindowSize>& output) noexcept
		{
			if (!IsExecutableRange(address, output.size()))
				return false;
			__try {
				std::memcpy(output.data(), address, output.size());
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void SnapshotPass(
			RE::BSRenderPass* pass,
			DrawSnapshot& snapshot,
			Result& result) noexcept
		{
			if (!pass) {
				++result.invalidPassSnapshots;
				return;
			}
			__try {
				snapshot.geometry = pass->geometry;
				if (!snapshot.geometry) {
					++result.missingGeometrySnapshots;
					return;
				}
				snapshot.geometryType =
					snapshot.geometry->GetType().underlying();
				snapshot.eligible =
					snapshot.geometryType >= kFirstAcceptedGeometryType &&
					snapshot.geometryType <= kLastAcceptedGeometryType;
				if (snapshot.eligible)
					++result.eligibleGeometryCalls;
				else
					++result.unsupportedGeometryTypes;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				snapshot = {};
				++result.snapshotFaults;
			}
		}

		[[nodiscard]] TargetStatus InspectTarget(
			ID3D11RenderTargetView* expected) noexcept
		{
			if (!expected)
				return TargetStatus::kFault;

			ID3D11RenderTargetView* current = nullptr;
			TargetStatus status = TargetStatus::kFault;
			__try {
				__try {
					auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
					if (renderer) {
						auto* context =
							reinterpret_cast<ID3D11DeviceContext*>(
								renderer->GetRuntimeData().context);
						if (context) {
							context->OMGetRenderTargets(1, &current, nullptr);
							status = current == expected ?
								TargetStatus::kMatch : TargetStatus::kMismatch;
						}
					}
				} __finally {
					if (current)
						current->Release();
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				status = TargetStatus::kFault;
			}
			return status;
		}

		[[nodiscard]] PlayerPassAncestry::Result ClassifyAgainstRoot(
			RE::BSGeometry* geometry,
			RE::NiAVObject* root,
			bool& faulted) noexcept
		{
			faulted = false;
			if (!geometry || !root)
				return PlayerPassAncestry::Result::kNoMatch;
			__try {
				return PlayerPassAncestry::Classify(
					static_cast<const RE::NiAVObject*>(geometry),
					static_cast<const RE::NiAVObject*>(root),
					kMaximumParentDepth,
					[](const RE::NiAVObject* object) noexcept {
						return object->parent;
					});
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				faulted = true;
				return PlayerPassAncestry::Result::kNoMatch;
			}
		}

		void RecordAncestryOutcome(
			const PlayerPassAncestry::Result ancestry,
			const bool faulted,
			Result& result) noexcept
		{
			if (faulted) {
				++result.classificationFaults;
				return;
			}
			switch (ancestry) {
			case PlayerPassAncestry::Result::kTruncated:
				++result.truncatedParentWalks;
				break;
			case PlayerPassAncestry::Result::kMalformed:
				++result.malformedParentWalks;
				break;
			case PlayerPassAncestry::Result::kMatch:
			case PlayerPassAncestry::Result::kNoMatch:
			default:
				break;
			}
		}

		[[nodiscard]] bool ReadSkinInstance(
			RE::BSGeometry* geometry,
			bool& faulted) noexcept
		{
			faulted = false;
			__try {
				return geometry &&
				       geometry->GetGeometryRuntimeData().skinInstance.get() != nullptr;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				faulted = true;
				return false;
			}
		}

		void ClassifyCompletedDraw(RE::BSGeometry* geometry, Result& result) noexcept
		{
			bool selectedFault = false;
			const auto selected = ClassifyAgainstRoot(
				geometry, g_active.selectedRoot, selectedFault);
			RecordAncestryOutcome(selected, selectedFault, result);

			PlayerPassAncestry::Result body =
				PlayerPassAncestry::Result::kNoMatch;
			bool bodyFault = false;
			if (g_active.bodyRoot &&
				g_active.bodyRoot == g_active.selectedRoot) {
				body = selected;
				bodyFault = selectedFault;
			} else if (g_active.bodyRoot) {
				body = ClassifyAgainstRoot(
					geometry, g_active.bodyRoot, bodyFault);
				RecordAncestryOutcome(body, bodyFault, result);
			}

			const bool selectedMatch = !selectedFault &&
				selected == PlayerPassAncestry::Result::kMatch;
			const bool bodyMatch = !bodyFault &&
				body == PlayerPassAncestry::Result::kMatch;
			if (!selectedMatch && !bodyMatch) {
				const bool selectedDefinitive = !g_active.selectedRoot ||
					(!selectedFault &&
						selected == PlayerPassAncestry::Result::kNoMatch);
				const bool bodyDefinitive = !g_active.bodyRoot ||
					(!bodyFault && body == PlayerPassAncestry::Result::kNoMatch);
				if (selectedDefinitive && bodyDefinitive)
					++result.unrelatedReturns;
				else
					++result.unclassifiedReturns;
				return;
			}

			bool skinFault = false;
			const bool skinned = ReadSkinInstance(geometry, skinFault);
			if (skinFault)
				++result.classificationFaults;
			if (selectedMatch) {
				++result.selectedRootReturns;
				if (skinned && !skinFault)
					++result.selectedRootSkinnedReturns;
			}
			if (bodyMatch) {
				++result.bodyRootReturns;
				if (skinned && !skinFault)
					++result.bodyRootSkinnedReturns;
			}
			++result.playerReturns;
			if (skinned && !skinFault)
				++result.skinnedPlayerReturns;
		}

		struct GenericGeometryDrawHook
		{
			using Function = void(RE::BSRenderPass*);

			__declspec(noinline) static
				HandMirrorReadOnlyObserver::GenericDrawToken
			ObserveReadOnlyHandEntryGuarded(RE::BSRenderPass* pass) noexcept
			{
				HandMirrorReadOnlyObserver::GenericDrawToken token{};
				__try {
					token = HandMirrorReadOnlyObserver::OnGenericDrawEntry(pass);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					token = {};
					HandMirrorReadOnlyObserver::FailStopCallbackFault();
				}
				return token;
			}

			__declspec(noinline) static void ObserveReadOnlyHandReturnGuarded(
				const HandMirrorReadOnlyObserver::GenericDrawToken& token) noexcept
			{
				__try {
					HandMirrorReadOnlyObserver::OnGenericDrawReturnedNormally(token);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorReadOnlyObserver::FailStopCallbackFault();
				}
			}

			__declspec(noinline) static
				HandMirrorApprovedContentReadOnlyObserver::GenericDrawToken
			ObserveApprovedHandEntryGuarded(RE::BSRenderPass* pass) noexcept
			{
				HandMirrorApprovedContentReadOnlyObserver::GenericDrawToken token{};
				__try {
					token = HandMirrorApprovedContentReadOnlyObserver::
						OnGenericDrawEntry(pass);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					token = {};
					HandMirrorApprovedContentReadOnlyObserver::
						FailStopCallbackFault();
				}
				return token;
			}

			__declspec(noinline) static void ObserveApprovedHandReturnGuarded(
				const HandMirrorApprovedContentReadOnlyObserver::GenericDrawToken&
					token) noexcept
			{
				__try {
					HandMirrorApprovedContentReadOnlyObserver::
						OnGenericDrawReturnedNormally(token);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorApprovedContentReadOnlyObserver::
						FailStopCallbackFault();
				}
			}

			__declspec(noinline) static
				HandMirrorReflectionRuntime::GenericDrawToken
			ObserveReflectiveHandEntryGuarded(RE::BSRenderPass* pass) noexcept
			{
				HandMirrorReflectionRuntime::GenericDrawToken token{};
				__try {
					token = HandMirrorReflectionRuntime::OnGenericDrawEntry(pass);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					token = {};
					HandMirrorReflectionRuntime::FailStopCallbackFault();
				}
				return token;
			}

			__declspec(noinline) static void ObserveReflectiveHandReturnGuarded(
				HandMirrorReflectionRuntime::GenericDrawToken& token,
				RE::BSRenderPass* pass) noexcept
			{
				__try {
					HandMirrorReflectionRuntime::OnGenericDrawReturnedNormally(
						token, pass);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorReflectionRuntime::FailStopCallbackFault();
				}
			}

			__declspec(noinline) static void ObserveReflectiveHandFinallyGuarded(
				HandMirrorReflectionRuntime::GenericDrawToken& token) noexcept
			{
				__try {
					HandMirrorReflectionRuntime::OnGenericDrawFinally(token);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorReflectionRuntime::FailStopCallbackFault();
				}
			}

			__declspec(noinline) static void InvokeNativeWithReflectiveFinally(
				RE::BSRenderPass* pass,
				HandMirrorReflectionRuntime::GenericDrawToken& reflectiveToken)
			{
				bool returnedNormally = false;
				__try {
					func(pass);
					returnedNormally = true;
				} __finally {
					if (!returnedNormally)
						ObserveReflectiveHandFinallyGuarded(reflectiveToken);
				}
			}

			static void thunk(RE::BSRenderPass* pass)
			{
				const auto readOnlyToken = ObserveReadOnlyHandEntryGuarded(pass);
				const auto approvedToken = ObserveApprovedHandEntryGuarded(pass);
				auto reflectiveToken = ObserveReflectiveHandEntryGuarded(pass);

				const bool observing =
					!g_faulted.load(std::memory_order_acquire) && g_active.active;
				const std::uint64_t token = observing ? g_active.token : 0;
				DrawSnapshot snapshot{};
				if (observing) {
					++g_active.result.drawCalls;
					SnapshotPass(pass, snapshot, g_active.result);
				}

				InvokeNativeWithReflectiveFinally(pass, reflectiveToken);

				ObserveReadOnlyHandReturnGuarded(readOnlyToken);
				ObserveApprovedHandReturnGuarded(approvedToken);
				ObserveReflectiveHandReturnGuarded(reflectiveToken, pass);
				ObserveReflectiveHandFinallyGuarded(reflectiveToken);

				if (g_faulted.load(std::memory_order_acquire) || !observing ||
					!g_active.active || g_active.token != token) {
					return;
				}
				++g_active.result.drawReturns;
				if (!snapshot.eligible)
					return;
				++g_active.result.eligibleGeometryReturns;

				switch (InspectTarget(g_active.expectedTargetRTV)) {
				case TargetStatus::kMatch:
					++g_active.result.targetMatchedReturns;
					ClassifyCompletedDraw(snapshot.geometry, g_active.result);
					break;
				case TargetStatus::kMismatch:
					++g_active.result.targetMismatches;
					break;
				case TargetStatus::kFault:
				default:
					++g_active.result.targetQueryFaults;
					break;
				}
			}

			static inline REL::Relocation<Function> func;
		};

		void Add(
			std::atomic<std::uint64_t>& counter,
			const std::uint64_t value) noexcept
		{
			counter.fetch_add(value, std::memory_order_relaxed);
		}

		void Aggregate(const Result& result) noexcept
		{
			Add(g_counters.sessions, 1);
			if (result.targetMatchedReturns != 0)
				Add(g_counters.sessionsWithTargetDraw, 1);
			if (result.playerReturns != 0)
				Add(g_counters.sessionsWithPlayer, 1);
			if (result.skinnedPlayerReturns != 0)
				Add(g_counters.sessionsWithSkinnedPlayer, 1);
			if (result.rootsAliased)
				Add(g_counters.aliasedRootSessions, 1);
			if (result.lifetimeCleanupFault)
				Add(g_counters.lifetimeCleanupFaults, 1);
			Add(g_counters.drawCalls, result.drawCalls);
			Add(g_counters.drawReturns, result.drawReturns);
			Add(g_counters.eligibleGeometryCalls, result.eligibleGeometryCalls);
			Add(g_counters.eligibleGeometryReturns, result.eligibleGeometryReturns);
			Add(g_counters.invalidPassSnapshots, result.invalidPassSnapshots);
			Add(g_counters.missingGeometrySnapshots, result.missingGeometrySnapshots);
			Add(g_counters.unsupportedGeometryTypes, result.unsupportedGeometryTypes);
			Add(g_counters.snapshotFaults, result.snapshotFaults);
			Add(g_counters.targetMatchedReturns, result.targetMatchedReturns);
			Add(g_counters.targetMismatches, result.targetMismatches);
			Add(g_counters.targetQueryFaults, result.targetQueryFaults);
			Add(g_counters.selectedRootReturns, result.selectedRootReturns);
			Add(
				g_counters.selectedRootSkinnedReturns,
				result.selectedRootSkinnedReturns);
			Add(g_counters.bodyRootReturns, result.bodyRootReturns);
			Add(
				g_counters.bodyRootSkinnedReturns,
				result.bodyRootSkinnedReturns);
			Add(g_counters.playerReturns, result.playerReturns);
			Add(g_counters.skinnedPlayerReturns, result.skinnedPlayerReturns);
			Add(g_counters.unrelatedReturns, result.unrelatedReturns);
			Add(g_counters.unclassifiedReturns, result.unclassifiedReturns);
			Add(g_counters.truncatedParentWalks, result.truncatedParentWalks);
			Add(g_counters.malformedParentWalks, result.malformedParentWalks);
			Add(g_counters.classificationFaults, result.classificationFaults);
		}
	}

	bool EnsureInstalled() noexcept
	{
		g_installRequests.fetch_add(1, std::memory_order_relaxed);
		if (g_installed.load(std::memory_order_acquire))
			return true;

		GeometryDrawObserverSignature::Runtime signatureRuntime{};
		if (!GetRuntimeSignature(signatureRuntime)) {
			g_unsupportedRuntimeRequests.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		AcquireSRWLockExclusive(&g_installLock);
		if (g_installed.load(std::memory_order_acquire)) {
			ReleaseSRWLockExclusive(&g_installLock);
			return true;
		}
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel)) {
			ReleaseSRWLockExclusive(&g_installLock);
			return false;
		}

		const auto target = kGenericGeometryDraw.address();
		g_targetAddress.store(target, std::memory_order_release);
		std::array<std::uint8_t,
			GeometryDrawObserverSignature::kWindowSize> window{};
		if (!ReadEntryWindow(reinterpret_cast<const void*>(target), window) ||
			!GeometryDrawObserverSignature::Matches(window, signatureRuntime)) {
			g_signatureFailures.fetch_add(1, std::memory_order_relaxed);
			ReleaseSRWLockExclusive(&g_installLock);
			try {
				logger::critical(
					"[MirrorsOfSkyrim][GeometryDrawObserver] exact entry signature mismatch for runtime {}",
					REL::Module::get().version().string());
			} catch (...) {
			}
			return false;
		}

		const bool installed =
			stl::detour_thunk<GenericGeometryDrawHook>(kGenericGeometryDraw);
		if (!installed)
			g_detourFailures.fetch_add(1, std::memory_order_relaxed);
		g_installed.store(installed, std::memory_order_release);
		ReleaseSRWLockExclusive(&g_installLock);

		try {
			if (installed) {
				logger::info(
					"[MirrorsOfSkyrim][GeometryDrawObserver] dormant generic draw owner installed at RelocationID (100847,107637)");
			} else {
				logger::critical(
					"[MirrorsOfSkyrim][GeometryDrawObserver] detour installation failed");
			}
		} catch (...) {
		}
		return installed;
	}

	bool IsInstalled() noexcept
	{
		return g_installed.load(std::memory_order_acquire);
	}

	bool Begin(
		Session& session,
		RE::NiAVObject* selectedRoot,
		RE::NiAVObject* bodyRoot,
		ID3D11RenderTargetView* expectedTargetRTV) noexcept
	{
		if (session.active || (!selectedRoot && !bodyRoot) ||
			!expectedTargetRTV || !IsInstalled() || g_active.active ||
			g_faulted.load(std::memory_order_acquire)) {
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		ActiveObservation pending{};
		pending.result.rootsAliased =
			selectedRoot && selectedRoot == bodyRoot;
		pending.selectedRoot = selectedRoot;
		pending.bodyRoot = bodyRoot;
		pending.expectedTargetRTV = expectedTargetRTV;
		const auto ownership = ClassifyRootOwnership(
			selectedRoot != nullptr,
			bodyRoot != nullptr,
			pending.result.rootsAliased);

		bool retainedSelected = false;
		bool retainedBody = false;
		bool retainedTarget = false;
		bool ownershipReady = true;
		if (ownership.retainSelectedRoot) {
			retainedSelected = RetainRoot(selectedRoot);
			ownershipReady = retainedSelected;
		}
		if (ownershipReady && ownership.retainBodyRoot) {
			retainedBody = RetainRoot(bodyRoot);
			ownershipReady = retainedBody;
		}
		if (ownershipReady) {
			retainedTarget = RetainTarget(expectedTargetRTV);
			ownershipReady = retainedTarget;
		}
		if (g_faulted.load(std::memory_order_acquire))
			ownershipReady = false;
		if (!ownershipReady) {
			g_faulted.store(true, std::memory_order_release);
			pending.expectedTargetRTV =
				retainedTarget ? expectedTargetRTV : nullptr;
			pending.bodyRoot = retainedBody ? bodyRoot : nullptr;
			pending.selectedRoot = retainedSelected ? selectedRoot : nullptr;
			(void)ReleaseOwnedReferences(pending);
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		std::uint64_t token =
			g_nextToken.fetch_add(1, std::memory_order_relaxed);
		if (token == 0)
			token = g_nextToken.fetch_add(1, std::memory_order_relaxed);
		session = {};
		session.token = token;
		session.threadID = GetCurrentThreadId();
		session.active = true;

		pending.token = token;
		pending.active = true;
		g_active = pending;
		return true;
	}

	Result Snapshot(const Session& session) noexcept
	{
		if (!g_faulted.load(std::memory_order_acquire) && session.active &&
			session.threadID == GetCurrentThreadId() && g_active.active &&
			g_active.token == session.token) {
			auto result = g_active.result;
			result.valid = true;
			return result;
		}
		return session.result;
	}

	Result End(Session& session) noexcept
	{
		const auto disposition = PlayerDrawProbeLifetime::ClassifyEnd(
			session.active,
			session.threadID,
			session.token,
			GetCurrentThreadId(),
			g_active.active,
			g_active.token);
		if (disposition ==
			PlayerDrawProbeLifetime::EndDisposition::kAlreadyEnded) {
			return session.result;
		}
		if (disposition ==
			PlayerDrawProbeLifetime::EndDisposition::kQuarantine) {
			g_endMismatches.fetch_add(1, std::memory_order_relaxed);
			g_quarantinedSessions.fetch_add(1, std::memory_order_relaxed);
			g_faulted.store(true, std::memory_order_release);
			session.active = false;
			session.result = {};
			return session.result;
		}

		auto completed = g_active;
		g_active = {};
		session.result = completed.result;
		session.result.valid = true;
		session.active = false;
		const auto releaseOutcome = ReleaseOwnedReferences(completed);
		session.result = ApplyOwnedReferenceReleaseOutcome(
			session.result, releaseOutcome);
		Aggregate(session.result);
		return session.result;
	}

	void LogDiagnostics(const char* reason)
	{
		logger::info(
			"[MirrorsOfSkyrim][GeometryDrawObserver][{}] installed={} target=0x{:X} "
			"install(requests/unsupported/signature/detour)={}/{}/{}/{} "
			"scope(faulted/rejects/endMismatch/quarantined/sessions/target/player/skinned/aliases/cleanupFaults)={}/{}/{}/{}/{}/{}/{}/{}/{}/{} "
			"ownership(rootRetain/rootRelease/targetRetain/targetRelease)={}/{}/{}/{}",
			reason ? reason : "status",
			IsInstalled(),
			g_targetAddress.load(std::memory_order_relaxed),
			g_installRequests.load(std::memory_order_relaxed),
			g_unsupportedRuntimeRequests.load(std::memory_order_relaxed),
			g_signatureFailures.load(std::memory_order_relaxed),
			g_detourFailures.load(std::memory_order_relaxed),
			g_faulted.load(std::memory_order_relaxed),
			g_beginRejects.load(std::memory_order_relaxed),
			g_endMismatches.load(std::memory_order_relaxed),
			g_quarantinedSessions.load(std::memory_order_relaxed),
			g_counters.sessions.load(std::memory_order_relaxed),
			g_counters.sessionsWithTargetDraw.load(std::memory_order_relaxed),
			g_counters.sessionsWithPlayer.load(std::memory_order_relaxed),
			g_counters.sessionsWithSkinnedPlayer.load(std::memory_order_relaxed),
			g_counters.aliasedRootSessions.load(std::memory_order_relaxed),
			g_counters.lifetimeCleanupFaults.load(std::memory_order_relaxed),
			g_rootRetainFailures.load(std::memory_order_relaxed),
			g_rootReleaseFailures.load(std::memory_order_relaxed),
			g_targetRetainFailures.load(std::memory_order_relaxed),
			g_targetReleaseFailures.load(std::memory_order_relaxed));
		logger::info(
			"[MirrorsOfSkyrim][GeometryDrawObserver][{}] draws(calls/returns/eligibleCalls/eligibleReturns)={}/{}/{}/{} "
			"snapshot(invalidPass/missingGeometry/unsupportedType/fault)={}/{}/{}/{} "
			"target(match/mismatch/fault)={}/{}/{}",
			reason ? reason : "status",
			g_counters.drawCalls.load(std::memory_order_relaxed),
			g_counters.drawReturns.load(std::memory_order_relaxed),
			g_counters.eligibleGeometryCalls.load(std::memory_order_relaxed),
			g_counters.eligibleGeometryReturns.load(std::memory_order_relaxed),
			g_counters.invalidPassSnapshots.load(std::memory_order_relaxed),
			g_counters.missingGeometrySnapshots.load(std::memory_order_relaxed),
			g_counters.unsupportedGeometryTypes.load(std::memory_order_relaxed),
			g_counters.snapshotFaults.load(std::memory_order_relaxed),
			g_counters.targetMatchedReturns.load(std::memory_order_relaxed),
			g_counters.targetMismatches.load(std::memory_order_relaxed),
			g_counters.targetQueryFaults.load(std::memory_order_relaxed));
		logger::info(
			"[MirrorsOfSkyrim][GeometryDrawObserver][{}] roots(selected/skinned/body/skinned/player/skinned/unrelated/unclassified)={}/{}/{}/{}/{}/{}/{}/{} "
			"parents(truncated/malformed/classifyFault)={}/{}/{}",
			reason ? reason : "status",
			g_counters.selectedRootReturns.load(std::memory_order_relaxed),
			g_counters.selectedRootSkinnedReturns.load(std::memory_order_relaxed),
			g_counters.bodyRootReturns.load(std::memory_order_relaxed),
			g_counters.bodyRootSkinnedReturns.load(std::memory_order_relaxed),
			g_counters.playerReturns.load(std::memory_order_relaxed),
			g_counters.skinnedPlayerReturns.load(std::memory_order_relaxed),
			g_counters.unrelatedReturns.load(std::memory_order_relaxed),
			g_counters.unclassifiedReturns.load(std::memory_order_relaxed),
			g_counters.truncatedParentWalks.load(std::memory_order_relaxed),
			g_counters.malformedParentWalks.load(std::memory_order_relaxed),
			g_counters.classificationFaults.load(std::memory_order_relaxed));
	}
}
