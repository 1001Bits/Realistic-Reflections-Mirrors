#include "PCH.h"

#include "MirrorMainSceneListBorrow.h"
#include "CommunityShadersMainCullingFinalizeChainPolicy.h"
#include "RenderPlayerViewMainCullingFinalizeSignature.h"

namespace MirrorMainSceneListBorrow
{
	namespace Policy = MirrorMainSceneListBorrowPolicy;
	namespace LODPolicy = MirrorPrivateLODStabilizationPolicy;

	namespace
	{
		struct NativeArrayHeader
		{
			void* data;
			std::uint32_t capacity;
			std::uint32_t pad0C;
			std::uint32_t size;
			std::uint32_t pad14;
		};
		static_assert(sizeof(NativeArrayHeader) == 0x18);

		struct HeaderRecord
		{
			NativeArrayHeader* address{ nullptr };
			NativeArrayHeader value{};
		};

		static_assert(sizeof(RE::NiPointer<RE::NiAVObject>) == sizeof(RE::NiAVObject*));
		static_assert(alignof(RE::NiPointer<RE::NiAVObject>) == alignof(RE::NiAVObject*));

		struct GuardedRetainedEntries
		{
			// Only this pointer array is exposed through the native 0x18 header. The
			// parallel scalar slots own references and are never visible to the engine.
			std::array<RE::NiAVObject*, Policy::kMaximumTotalEntries> values{};
			std::array<Policy::RetainedReferenceSlot,
				Policy::kMaximumTotalEntries> ownership{};
			std::size_t committedCount{ 0 };
		};
		static_assert(std::is_trivially_destructible_v<GuardedRetainedEntries>);

		struct BorrowState
		{
			Policy::BorrowToken token{};
			std::array<ListView, Policy::kMaximumLists> lists{};
			std::array<NativeArrayHeader, Policy::kMaximumLists> retainedHeaders{};
			GuardedRetainedEntries retainedEntries{};
			std::array<HeaderRecord, Policy::kMaximumLists + 2> headers{};
			std::size_t listCount{ 0 };
			std::size_t headerCount{ 0 };
			std::size_t nonEmptyListCount{ 0 };
			std::uint32_t totalEntries{ 0 };
			bool publicationReady{ false };
			bool replayCleanupComplete{ false };
			std::size_t replayAcquisitions{ 0 };
			bool portalPresent{ false };
			std::byte* portalEntry{ nullptr };
			void* portalOwner{ nullptr };
		};

		struct DerivedReplayState
		{
			std::array<ListView, Policy::kMaximumLists> lists{};
			std::array<NativeArrayHeader, Policy::kMaximumLists> headers{};
			GuardedRetainedEntries entries{};
			std::array<LODPolicy::StaticRootIdentity,
				Policy::kMaximumTotalEntries> coverageIdentities{};
			Policy::DerivedReplayCounts counts{};
			std::size_t ownedEntryCount{ 0 };
			std::size_t coverageIdentityCount{ 0 };
			std::uint32_t threadID{ 0 };
			std::uint64_t generation{ 0 };
			bool active{ false };
		};
		static_assert(std::is_trivially_destructible_v<BorrowState>);
		static_assert(std::is_trivially_destructible_v<DerivedReplayState>);

		struct DiagnosticsStorage
		{
			std::atomic<std::uint64_t> hookCalls{ 0 };
			std::atomic<std::uint64_t> begins{ 0 };
			std::atomic<std::uint64_t> acquires{ 0 };
			std::atomic<std::uint64_t> unused{ 0 };
			std::atomic<std::uint64_t> nested{ 0 };
			std::atomic<std::uint64_t> wrongThread{ 0 };
			std::atomic<std::uint64_t> generationRejects{ 0 };
			std::atomic<std::uint64_t> headerRejects{ 0 };
			std::atomic<std::uint64_t> sehFaults{ 0 };
			std::atomic<std::uint64_t> faults{ 0 };
			std::atomic<std::uint64_t> regularLists{ 0 };
			std::atomic<std::uint64_t> portalLists{ 0 };
			std::atomic<std::uint64_t> inlineLists{ 0 };
			std::atomic<std::uint64_t> entries{ 0 };
			std::atomic<std::uint64_t> culls{ 0 };
			std::atomic<std::uint64_t> returns{ 0 };
			std::atomic<std::uint64_t> fallbackSkips{ 0 };
			std::atomic<std::uint64_t> accumulator178Applies{ 0 };
			std::atomic<std::uint64_t> accumulator178Restores{ 0 };
			std::atomic<std::uint64_t> derivedBuilds{ 0 };
			std::atomic<std::uint64_t> derivedCoverageRejects{ 0 };
			std::atomic<std::uint64_t> derivedBorrowRejects{ 0 };
			std::atomic<std::uint64_t> derivedNonEmptyLists{ 0 };
			std::atomic<std::uint64_t> derivedEntries{ 0 };
			std::atomic<std::uint64_t> derivedFilteredEntries{ 0 };
			std::atomic<std::uint64_t> derivedCompletions{ 0 };
			std::atomic<std::uint64_t> derivedDiscards{ 0 };
		};

		std::atomic_bool g_requested{ false };
		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_hookInstalled{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic<std::uint32_t> g_renderThreadID{ 0 };
		std::atomic<std::uint64_t> g_nextGeneration{ 0 };
		std::atomic<std::uintptr_t> g_originalFinalize{ 0 };
		std::atomic<CaptureCallback> g_captureCallback{ nullptr };
		thread_local BorrowState g_borrow{};
		thread_local DerivedReplayState g_derived{};
		DiagnosticsStorage g_diagnostics{};

		[[nodiscard]] Policy::HeaderShape Shape(const NativeArrayHeader& a_header) noexcept
		{
			return { reinterpret_cast<std::uintptr_t>(a_header.data),
				a_header.capacity, a_header.size };
		}

		void LatchFault(bool a_seh) noexcept;
		void EndBorrow() noexcept;

		[[nodiscard]] bool RetainEntrySEH(
			GuardedRetainedEntries& a_entries,
			std::size_t a_index,
			RE::NiAVObject* a_value) noexcept
		{
			if (!a_value || a_index != a_entries.committedCount ||
				a_index >= a_entries.values.size() ||
				a_entries.values[a_index] != nullptr ||
				a_entries.ownership[a_index].owned ||
				a_entries.ownership[a_index].identity != 0) {
				return false;
			}
			bool retained = false;
			__try {
				a_value->IncRefCount();
				retained = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				retained = false;
			}
			const auto status = Policy::CommitRetainedReference(
				a_entries.ownership[a_index],
				reinterpret_cast<std::uintptr_t>(a_value), retained);
			if (status != Policy::RetainReferenceStatus::kCommitted) {
				// A failed IncRef may have changed the native count before faulting. The
				// slot remains tombstoned, so cleanup never guesses or retries it.
				return false;
			}
			a_entries.values[a_index] = a_value;
			++a_entries.committedCount;
			return true;
		}

		[[nodiscard]] bool ReleaseEntrySEH(RE::NiAVObject* a_value) noexcept
		{
			if (!a_value)
				return false;
			bool released = false;
			__try {
				a_value->DecRefCount();
				released = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				released = false;
			}
			return released;
		}

		[[nodiscard]] bool ReleaseAllEntries(
			GuardedRetainedEntries& a_entries) noexcept
		{
			const auto count = std::min(a_entries.committedCount,
				a_entries.values.size());
			bool clean = a_entries.committedCount <= a_entries.values.size();
			// Exhaust every normally committed slot. Each ownership record and native
			// array value is cleared before its independent guarded DecRef.
			for (std::size_t i = 0; i < count; ++i) {
				const auto rawValue = a_entries.values[i];
				const auto identity =
					Policy::TombstoneForRelease(a_entries.ownership[i]);
				a_entries.values[i] = nullptr;
				if (identity == 0 || rawValue !=
						reinterpret_cast<RE::NiAVObject*>(identity)) {
					clean = false;
				}
				auto* const owned = reinterpret_cast<RE::NiAVObject*>(identity);
				if (owned && !ReleaseEntrySEH(owned))
					clean = false;
			}
			a_entries.committedCount = 0;
			return clean;
		}

		[[nodiscard]] bool RetainedEntriesCoherent(
			const GuardedRetainedEntries& a_entries,
			std::size_t a_expectedCount) noexcept
		{
			if (a_expectedCount != a_entries.committedCount ||
				a_expectedCount > a_entries.values.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_expectedCount; ++i) {
				const auto* const value = a_entries.values[i];
				const auto& owner = a_entries.ownership[i];
				if (!value || !owner.owned || owner.identity !=
						reinterpret_cast<std::uintptr_t>(value)) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ClearDerivedReplayState() noexcept
		{
			const bool referencesReleased = ReleaseAllEntries(g_derived.entries);
			for (auto& list : g_derived.lists)
				list = {};
			for (auto& header : g_derived.headers)
				header = {};
			g_derived.counts = {};
			g_derived.ownedEntryCount = 0;
			const auto coverageCount = std::min<std::size_t>(
				g_derived.coverageIdentityCount,
				g_derived.coverageIdentities.size());
			for (std::size_t i = 0; i < coverageCount; ++i)
				g_derived.coverageIdentities[i] = {};
			g_derived.coverageIdentityCount = 0;
			g_derived.threadID = 0;
			g_derived.generation = 0;
			g_derived.active = false;
			if (!referencesReleased)
				LatchFault(false);
			return referencesReleased;
		}

		int CoverageExceptionFilter(unsigned int) noexcept
		{
			// Supplemental evidence belongs to a default-off caller.  An unreadable
			// span is malformed evidence, not corruption of the retained borrow.
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[nodiscard]] LODPolicy::CoverageStatus SnapshotCoverage(
			const LODPolicy::SupplementalCoverageEvidence& a_coverage) noexcept
		{
			if (!a_coverage.authoritative)
				return LODPolicy::CoverageStatus::kNotAuthoritative;
			if (a_coverage.overflowObserved ||
				a_coverage.expectedStaticCount > a_coverage.staticCapacity ||
				a_coverage.identities.size() > a_coverage.staticCapacity ||
				a_coverage.expectedStaticCount > g_derived.coverageIdentities.size() ||
				a_coverage.identities.size() > g_derived.coverageIdentities.size()) {
				return LODPolicy::CoverageStatus::kOverflow;
			}
			if (!a_coverage.collectionComplete)
				return LODPolicy::CoverageStatus::kCollectionIncomplete;
			if (a_coverage.expectedStaticCount != a_coverage.identities.size())
				return LODPolicy::CoverageStatus::kCountMismatch;

			auto result = LODPolicy::CoverageStatus::kMalformedIdentity;
			__try {
				g_derived.coverageIdentityCount = 0;
				for (const auto& identity : a_coverage.identities) {
					if (!LODPolicy::Valid(identity))
						return LODPolicy::CoverageStatus::kMalformedIdentity;
					g_derived.coverageIdentities[g_derived.coverageIdentityCount++] =
						identity;
				}
				auto first = g_derived.coverageIdentities.begin();
				auto last = first + g_derived.coverageIdentityCount;
				std::sort(first, last,
					[](const auto& a_left, const auto& a_right) noexcept {
						return a_left.root < a_right.root;
					});
				for (auto it = first; it != last; ++it) {
					if (it != first && (it - 1)->root == it->root)
						return LODPolicy::CoverageStatus::kMalformedIdentity;
				}
				std::sort(first, last,
					[](const auto& a_left, const auto& a_right) noexcept {
						return a_left.formID < a_right.formID;
					});
				for (auto it = first; it != last; ++it) {
					if (it != first && (it - 1)->formID == it->formID)
						return LODPolicy::CoverageStatus::kMalformedIdentity;
				}
				std::sort(first, last,
					[](const auto& a_left, const auto& a_right) noexcept {
						return a_left.root < a_right.root;
					});
				result = LODPolicy::CoverageStatus::kComplete;
			} __except (CoverageExceptionFilter(GetExceptionCode())) {
				result = LODPolicy::CoverageStatus::kMalformedIdentity;
			}
			return result;
		}

		[[nodiscard]] bool CoveredBySupplemental(std::uintptr_t a_root) noexcept
		{
			std::size_t first = 0;
			std::size_t last = g_derived.coverageIdentityCount;
			while (first < last) {
				const auto middle = first + (last - first) / 2;
				const auto candidate = g_derived.coverageIdentities[middle].root;
				if (candidate < a_root)
					first = middle + 1;
				else
					last = middle;
			}
			return first < g_derived.coverageIdentityCount &&
				g_derived.coverageIdentities[first].root == a_root;
		}

		[[nodiscard]] std::uintptr_t WorkerCountAddress()
		{
			static REL::Relocation<std::uintptr_t> value{ REL::VariantID(528071, 415016, 0x348580C) };
			return value.address();
		}

		[[nodiscard]] std::uintptr_t OuterListsAddress()
		{
			static REL::Relocation<std::uintptr_t> value{ REL::VariantID(528072, 415017, 0x3485AC0) };
			return value.address();
		}

		[[nodiscard]] std::uintptr_t InlineListAddress()
		{
			static REL::Relocation<std::uintptr_t> value{ REL::VariantID(528075, 415020, 0x3485AD8) };
			return value.address();
		}

		[[nodiscard]] std::uintptr_t ProcessArrayAddress()
		{
			static REL::Relocation<std::uintptr_t> value{ RELOCATION_ID(528077, 415022) };
			return value.address();
		}

		void LatchFault(bool a_seh) noexcept
		{
			if (a_seh)
				g_diagnostics.sehFaults.fetch_add(1, std::memory_order_relaxed);
			g_diagnostics.faults.fetch_add(1, std::memory_order_relaxed);
			g_faulted.store(true, std::memory_order_release);
			g_enabled.store(false, std::memory_order_release);
			g_borrow.token.faulted = true;
			g_borrow.publicationReady = false;
		}

		int BorrowExceptionFilter(unsigned int) noexcept
		{
			LatchFault(true);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[nodiscard]] bool AddHeader(NativeArrayHeader* a_header) noexcept
		{
			if (!a_header || g_borrow.headerCount >= g_borrow.headers.size())
				return false;
		const NativeArrayHeader snapshot = *a_header;
			if (!Policy::HeaderIsValid(Shape(snapshot)))
				return false;
			g_borrow.headers[g_borrow.headerCount++] = { a_header, snapshot };
			return true;
		}

		[[nodiscard]] bool AddList(
			NativeArrayHeader* a_header, Policy::ListKind a_kind, bool a_unk61) noexcept
		{
			if (g_borrow.listCount >= g_borrow.lists.size() || !AddHeader(a_header))
				return false;
		const NativeArrayHeader snapshot = *a_header;
			if (snapshot.size > Policy::kMaximumTotalEntries - g_borrow.totalEntries)
				return false;
		g_borrow.lists[g_borrow.listCount++] = {
				a_header,
				{ snapshot.data, snapshot.capacity, snapshot.size },
				a_kind,
				a_unk61
		};
		g_borrow.totalEntries += snapshot.size;
		if (snapshot.size != 0)
			++g_borrow.nonEmptyListCount;
		switch (a_kind) {
		case Policy::ListKind::kRegular:
			g_diagnostics.regularLists.fetch_add(1, std::memory_order_relaxed);
			break;
		case Policy::ListKind::kPortal:
			g_diagnostics.portalLists.fetch_add(1, std::memory_order_relaxed);
			break;
		case Policy::ListKind::kInline:
			g_diagnostics.inlineLists.fetch_add(1, std::memory_order_relaxed);
			break;
		}
		g_diagnostics.entries.fetch_add(snapshot.size, std::memory_order_relaxed);
		return true;
		}

		[[nodiscard]] bool BeginBorrowRaw() noexcept
		{
			const auto threadID = GetCurrentThreadId();
			std::uint32_t expectedThread = 0;
			if (!g_renderThreadID.compare_exchange_strong(
					expectedThread, threadID, std::memory_order_acq_rel,
					std::memory_order_acquire) && expectedThread != threadID) {
				g_diagnostics.wrongThread.fetch_add(1, std::memory_order_relaxed);
				LatchFault(false);
				return false;
			}
			if (g_borrow.token.active) {
				g_diagnostics.nested.fetch_add(1, std::memory_order_relaxed);
				LatchFault(false);
				return false;
			}

			if (g_borrow.retainedEntries.committedCount != 0) {
				(void)ReleaseAllEntries(g_borrow.retainedEntries);
				LatchFault(false);
				return false;
			}
			g_borrow = {};
			const auto generation = g_nextGeneration.fetch_add(
				1, std::memory_order_acq_rel) + 1;
			if (Policy::Begin(g_borrow.token, threadID, generation) !=
				Policy::Transition::kAccepted) {
				g_diagnostics.generationRejects.fetch_add(1, std::memory_order_relaxed);
				LatchFault(false);
				return false;
			}

			const auto workerCount =
				*reinterpret_cast<const std::uint32_t*>(WorkerCountAddress());
			auto* outer = reinterpret_cast<NativeArrayHeader*>(OuterListsAddress());
			auto* process = reinterpret_cast<NativeArrayHeader*>(ProcessArrayAddress());
			auto* inlineList = reinterpret_cast<NativeArrayHeader*>(InlineListAddress());
			const NativeArrayHeader outerSnapshot = *outer;
			const NativeArrayHeader processSnapshot = *process;
			if (!Policy::WorkerSetIsValid(
					workerCount, Shape(outerSnapshot), Shape(processSnapshot)) ||
				!Policy::HeaderIsValid(Shape(*inlineList)) ||
				!AddHeader(outer) || !AddHeader(process)) {
				g_diagnostics.headerRejects.fetch_add(1, std::memory_order_relaxed);
				LatchFault(false);
				return false;
			}

			auto* regular = static_cast<NativeArrayHeader*>(outerSnapshot.data);
			auto** processes = static_cast<void**>(processSnapshot.data);
			bool hasPortal = false;
			NativeArrayHeader* portalList = nullptr;
			if (processes[0]) {
				constexpr std::ptrdiff_t kPortalEntryOffset = 0x30190;
				constexpr std::ptrdiff_t kPortalEnabledOffset = 0x131;
				constexpr std::ptrdiff_t kPortalOwnerOffset = 0x10;
				constexpr std::ptrdiff_t kPortalListOffset = 0x58;
				g_borrow.portalEntry = *reinterpret_cast<std::byte**>(
					static_cast<std::byte*>(processes[0]) + kPortalEntryOffset);
				if (!g_borrow.portalEntry) {
					g_diagnostics.headerRejects.fetch_add(1, std::memory_order_relaxed);
					LatchFault(false);
					return false;
				}
				hasPortal = *reinterpret_cast<const std::uint8_t*>(
					g_borrow.portalEntry + kPortalEnabledOffset) != 0;
				if (hasPortal) {
					g_borrow.portalOwner = *reinterpret_cast<void**>(
						g_borrow.portalEntry + kPortalOwnerOffset);
					if (!g_borrow.portalOwner) {
						g_diagnostics.headerRejects.fetch_add(1, std::memory_order_relaxed);
						LatchFault(false);
						return false;
					}
					portalList = reinterpret_cast<NativeArrayHeader*>(
						static_cast<std::byte*>(g_borrow.portalOwner) + kPortalListOffset);
				}
			}
			g_borrow.portalPresent = hasPortal;

			const auto sequence = Policy::BuildReplaySequence(workerCount, hasPortal);
			for (std::size_t i = 0; i < sequence.count; ++i) {
				const auto step = sequence.steps[i];
				NativeArrayHeader* header = nullptr;
				bool unk61 = false;
				switch (step.kind) {
				case Policy::ListKind::kRegular:
					header = regular + step.workerIndex;
					break;
				case Policy::ListKind::kPortal:
					header = portalList;
					unk61 = true;
					break;
				case Policy::ListKind::kInline:
					header = inlineList;
					break;
				}
				if (!AddList(header, step.kind, unk61)) {
					g_diagnostics.headerRejects.fetch_add(1, std::memory_order_relaxed);
					LatchFault(false);
					return false;
				}
			}

			// The engine clears and releases these arrays before the established
			// post-main-world capture seam. Retain an immutable copy now, while the
			// finalizer guarantees stable headers, and never replay the live arrays.
			RE::NiAVObject* firstPersonRoot = nullptr;
			RE::NiAVObject* thirdPersonRoot = nullptr;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				firstPersonRoot = player->Get3D1(true);
				thirdPersonRoot = player->Get3D1(false);
			}
			if (firstPersonRoot == thirdPersonRoot)
				firstPersonRoot = nullptr;

			std::size_t retainedOffset = 0;
			std::size_t retainedNonEmptyLists = 0;
			for (std::size_t i = 0; i < g_borrow.listCount; ++i) {
				auto& list = g_borrow.lists[i];
				const auto count = static_cast<std::size_t>(list.header.size);
				if (retainedOffset + count > g_borrow.retainedEntries.values.size()) {
					LatchFault(false);
					return false;
				}
				auto* source = static_cast<RE::NiPointer<RE::NiAVObject>*>(list.header.data);
				std::size_t retainedCount = 0;
				for (std::size_t entry = 0; entry < count; ++entry) {
					if (!source[entry]) {
						LatchFault(false);
						return false;
					}
					bool firstPersonDescendant = false;
					if (firstPersonRoot) {
						auto* current = source[entry].get();
						std::size_t depth = 0;
						for (; current && depth < 256; ++depth) {
							if (current == firstPersonRoot) {
								firstPersonDescendant = true;
								break;
							}
							current = current->parent;
						}
						if (current && !firstPersonDescendant && depth == 256) {
							LatchFault(false);
							return false;
						}
					}
					if (firstPersonDescendant)
						continue;
					if (!RetainEntrySEH(
							g_borrow.retainedEntries,
							retainedOffset + retainedCount,
							source[entry].get())) {
						LatchFault(true);
						return false;
					}
					++retainedCount;
				}
				auto& retained = g_borrow.retainedHeaders[i];
				retained.data = retainedCount != 0 ?
					static_cast<void*>(
						g_borrow.retainedEntries.values.data() + retainedOffset) : nullptr;
				retained.capacity = static_cast<std::uint32_t>(retainedCount);
				retained.size = static_cast<std::uint32_t>(retainedCount);
				list.headerAddress = &retained;
				list.header = { retained.data, retained.capacity, retained.size };
				retainedOffset += retainedCount;
				if (retainedCount != 0)
					++retainedNonEmptyLists;
			}
			g_borrow.totalEntries = static_cast<std::uint32_t>(retainedOffset);
			g_borrow.nonEmptyListCount = retainedNonEmptyLists;
			// From this point the snapshot owns every entry. Engine header/portal
			// changes are expected during the chained main draw and are irrelevant.
			g_borrow.headerCount = 0;
			g_borrow.portalEntry = nullptr;
			g_borrow.portalOwner = nullptr;
			g_diagnostics.begins.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] bool BeginBorrow() noexcept
		{
			const bool activeBefore = g_borrow.token.active;
			bool result = false;
			__try {
				result = BeginBorrowRaw();
			} __except (BorrowExceptionFilter(GetExceptionCode())) {
				result = false;
			}
			// A newly opened partial borrow owns every normally committed entry even
			// when a later snapshot read/IncRef faults. Close it here because the
			// module's fail-stop disables RunDeferredCapture for this source.
			if (!result && !activeBefore && g_borrow.token.active)
				EndBorrow();
			return result;
		}

		[[nodiscard]] bool RevalidateRaw(const BorrowView& a_view) noexcept
		{
			if (!g_borrow.token.active || g_borrow.token.threadID != GetCurrentThreadId() ||
				a_view.lists != g_borrow.lists.data() ||
				a_view.threadID != g_borrow.token.threadID ||
				g_borrow.token.generation != a_view.generation ||
				g_borrow.listCount != a_view.listCount ||
				g_borrow.nonEmptyListCount != a_view.nonEmptyListCount ||
				g_borrow.totalEntries != a_view.totalEntries ||
				!RetainedEntriesCoherent(
					g_borrow.retainedEntries, g_borrow.totalEntries))
				return false;
			for (std::size_t i = 0; i < g_borrow.listCount; ++i) {
				const auto& list = g_borrow.lists[i];
				const auto& retained = g_borrow.retainedHeaders[i];
				if (list.headerAddress != &retained || list.header.data != retained.data ||
					list.header.capacity != retained.capacity ||
					list.header.size != retained.size ||
					!Policy::HeaderIsValid(Shape(retained)))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool Revalidate(const BorrowView& a_view) noexcept
		{
			bool result = false;
			__try {
				result = RevalidateRaw(a_view);
			} __except (BorrowExceptionFilter(GetExceptionCode())) {
				result = false;
			}
			if (!result && !g_faulted.load(std::memory_order_acquire)) {
				g_diagnostics.headerRejects.fetch_add(1, std::memory_order_relaxed);
				LatchFault(false);
			}
			return result;
		}

		[[nodiscard]] bool RevalidateDerivedRaw(
			const BorrowView& a_original,
			const DerivedReplayView& a_derived) noexcept
		{
			if (!RevalidateRaw(a_original) || !g_derived.active ||
				g_derived.threadID != GetCurrentThreadId() ||
				g_derived.threadID != a_derived.threadID ||
				g_derived.generation != a_derived.generation ||
				g_derived.generation != a_original.generation ||
				a_derived.lists != g_derived.lists.data() ||
				a_derived.listCount != g_derived.counts.listCount ||
				a_derived.nonEmptyListCount !=
					g_derived.counts.nonEmptyListCount ||
				a_derived.totalEntries != g_derived.counts.totalEntries ||
				a_derived.filteredEntries != g_derived.counts.filteredEntries ||
				a_derived.listCount != a_original.listCount ||
				static_cast<std::uint64_t>(a_derived.totalEntries) +
					a_derived.filteredEntries != a_original.totalEntries ||
				!Policy::DerivedReplayCountsAreValid(g_derived.counts) ||
				g_derived.ownedEntryCount != g_derived.counts.totalEntries ||
				!RetainedEntriesCoherent(
					g_derived.entries, g_derived.ownedEntryCount)) {
				return false;
			}

			std::uint32_t totalEntries = 0;
			std::size_t nonEmptyLists = 0;
			for (std::size_t i = 0; i < a_derived.listCount; ++i) {
				const auto& source = a_original.lists[i];
				const auto& list = g_derived.lists[i];
				const auto& header = g_derived.headers[i];
				if (list.headerAddress != &header ||
					list.header.data != header.data ||
					list.header.capacity != header.capacity ||
					list.header.size != header.size ||
					list.kind != source.kind || list.unk61 != source.unk61 ||
					header.capacity != header.size ||
					!Policy::HeaderIsValid(Shape(header))) {
					return false;
				}
				auto** sourceEntries =
					static_cast<RE::NiAVObject**>(source.header.data);
				auto** derivedEntries =
					static_cast<RE::NiAVObject**>(header.data);
				std::uint32_t derivedIndex = 0;
				for (std::uint32_t sourceIndex = 0;
					 sourceIndex < source.header.size; ++sourceIndex) {
					if (!sourceEntries || !sourceEntries[sourceIndex])
						return false;
					const auto root = reinterpret_cast<std::uintptr_t>(
						sourceEntries[sourceIndex]);
					if (CoveredBySupplemental(root))
						continue;
					if (!derivedEntries || derivedIndex >= header.size ||
						derivedEntries[derivedIndex] != sourceEntries[sourceIndex]) {
						return false;
					}
					++derivedIndex;
				}
				if (derivedIndex != header.size)
					return false;
				totalEntries += header.size;
				if (header.size != 0)
					++nonEmptyLists;
			}
			return totalEntries == a_derived.totalEntries &&
				nonEmptyLists == a_derived.nonEmptyListCount;
		}

		[[nodiscard]] bool RevalidateDerived(
			const BorrowView& a_original,
			const DerivedReplayView& a_derived) noexcept
		{
			bool result = false;
			__try {
				result = RevalidateDerivedRaw(a_original, a_derived);
			} __except (BorrowExceptionFilter(GetExceptionCode())) {
				result = false;
			}
			if (!result && !g_faulted.load(std::memory_order_acquire)) {
				g_diagnostics.derivedBorrowRejects.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(false);
			}
			return result;
		}

		void EndBorrow() noexcept
		{
			const bool active = g_borrow.token.active;
			if (g_derived.active) {
				g_diagnostics.derivedBorrowRejects.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(false);
			}
			(void)ClearDerivedReplayState();
			if (active) {
				if (g_borrow.replayAcquisitions == 0)
					g_diagnostics.unused.fetch_add(1, std::memory_order_relaxed);
				const auto transition = Policy::End(
					g_borrow.token, GetCurrentThreadId(), g_borrow.token.generation);
				if (transition == Policy::Transition::kWrongThread) {
					g_diagnostics.wrongThread.fetch_add(1, std::memory_order_relaxed);
					LatchFault(false);
				} else if (transition != Policy::Transition::kAccepted) {
					g_diagnostics.generationRejects.fetch_add(1, std::memory_order_relaxed);
					LatchFault(false);
				}
			} else if (g_borrow.retainedEntries.committedCount != 0) {
				// Inactive-with-owned-values is an impossible state, but the entries still
				// have exact scalar ownership and can be exhausted safely.
				LatchFault(false);
			}
			g_borrow.publicationReady = false;
			if (!ReleaseAllEntries(g_borrow.retainedEntries))
				LatchFault(false);
			for (auto& header : g_borrow.retainedHeaders)
				header = {};
			for (auto& list : g_borrow.lists)
				list = {};
			g_borrow.listCount = 0;
			g_borrow.headerCount = 0;
			g_borrow.nonEmptyListCount = 0;
			g_borrow.totalEntries = 0;
			g_borrow.token = {};
		}

		__declspec(noinline) bool RunBorrowedCaptureWithFinally() noexcept
		{
			const bool began = g_borrow.token.active;
			bool callbackReturned = false;
			__try {
				if (began) {
					const auto callback = g_captureCallback.load(std::memory_order_acquire);
					if (callback) {
						callback();
						callbackReturned = true;
					}
				}
			} __finally {
				if (began)
					EndBorrow();
			}
			return began && callbackReturned &&
				!g_faulted.load(std::memory_order_acquire) &&
				!g_borrow.token.active && !g_borrow.publicationReady;
		}

		__declspec(noinline) bool RunBorrowedCaptureWithSEH() noexcept
		{
			bool completed = false;
			__try {
				completed = RunBorrowedCaptureWithFinally();
			} __except (BorrowExceptionFilter(GetExceptionCode())) {
				completed = false;
			}
			return completed;
		}

		__declspec(noinline) bool ReadCallSiteWindowSEH(
			std::uintptr_t a_callSite, std::uint8_t* a_window,
			std::size_t a_windowSize, std::int32_t* a_displacement) noexcept
		{
			if (!a_callSite || !a_window || a_windowSize < 5 || !a_displacement)
				return false;
			__try {
				std::memcpy(a_window, reinterpret_cast<const void*>(a_callSite), a_windowSize);
				std::memcpy(a_displacement, a_window + 1, sizeof(*a_displacement));
				return true;
			} __except (BorrowExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool IsExecutableAddress(const std::uintptr_t a_address) noexcept
		{
			if (!a_address)
				return false;
			MEMORY_BASIC_INFORMATION information{};
			if (VirtualQuery(
					reinterpret_cast<const void*>(a_address), &information,
					sizeof(information)) != sizeof(information) ||
				information.State != MEM_COMMIT ||
				(information.Protect & PAGE_GUARD) != 0 ||
				(information.Protect & PAGE_NOACCESS) != 0) {
				return false;
			}
			const DWORD protection = information.Protect & 0xFFu;
			return protection == PAGE_EXECUTE ||
			       protection == PAGE_EXECUTE_READ ||
			       protection == PAGE_EXECUTE_READWRITE ||
			       protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool ReadCommunityShaders184ChainEvidenceSEH(
			const std::uintptr_t a_inspectedTarget,
			const std::uintptr_t a_expectedNativeTarget,
			CommunityShadersMainCullingFinalizeChainPolicy::Evidence& a_evidence,
			std::array<std::uint8_t,
				CommunityShadersMainCullingFinalizeChainPolicy::
					kFiveByteCallBranchStubSize>& a_branchStub,
			std::array<std::uint8_t,
				CommunityShadersMainCullingFinalizeChainPolicy::kThunkPrefix.size()>&
				a_thunkPrefix) noexcept
		{
			namespace Chain = CommunityShadersMainCullingFinalizeChainPolicy;
			const auto module = GetModuleHandleW(L"CommunityShaders.dll");
			if (!module)
				return false;
			const auto moduleBase = reinterpret_cast<std::uintptr_t>(module);
			bool read = false;
			__try {
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
				if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
					dos->e_lfanew > 0x1000) {
					__leave;
				}
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
					moduleBase + static_cast<std::uintptr_t>(dos->e_lfanew));
				if (nt->Signature != IMAGE_NT_SIGNATURE ||
					nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
					__leave;
				}
				const Chain::ModuleIdentity identity{
					.machine = nt->FileHeader.Machine,
					.sectionCount = nt->FileHeader.NumberOfSections,
					.optionalMagic = nt->OptionalHeader.Magic,
					.timeDateStamp = nt->FileHeader.TimeDateStamp,
					.sizeOfCode = nt->OptionalHeader.SizeOfCode,
					.sizeOfImage = nt->OptionalHeader.SizeOfImage
				};
				if (!Chain::MatchesModuleIdentity(identity) ||
					moduleBase > (std::numeric_limits<std::uintptr_t>::max)() -
						Chain::kRenderShadowMapsThunkRVA ||
					moduleBase > (std::numeric_limits<std::uintptr_t>::max)() -
						Chain::kOriginalFinalizeSlotRVA) {
					__leave;
				}
				const auto thunk = moduleBase + Chain::kRenderShadowMapsThunkRVA;
				const auto originalSlot = moduleBase + Chain::kOriginalFinalizeSlotRVA;
				if (!IsExecutableAddress(thunk))
					__leave;
				std::memcpy(a_thunkPrefix.data(),
					reinterpret_cast<const void*>(thunk), a_thunkPrefix.size());
				if (a_inspectedTarget != thunk) {
					if (!IsExecutableAddress(a_inspectedTarget))
						__leave;
					std::memcpy(a_branchStub.data(),
						reinterpret_cast<const void*>(a_inspectedTarget),
						a_branchStub.size());
				}
				const auto wrapperOriginal =
					*reinterpret_cast<const std::uintptr_t*>(originalSlot);
				a_evidence = {
					.expectedNativeTarget = a_expectedNativeTarget,
					.inspectedCallTarget = a_inspectedTarget,
					.communityShadersBase = moduleBase,
					.module = identity,
					.inspectedBranchStub = a_inspectedTarget == thunk ?
						std::span<const std::uint8_t>{} :
						std::span<const std::uint8_t>{ a_branchStub },
					.thunkPrefix = std::span<const std::uint8_t>{ a_thunkPrefix },
					.wrapperOriginalTarget = wrapperOriginal
				};
				read = true;
			} __except (BorrowExceptionFilter(GetExceptionCode())) {
				read = false;
			}
			return read;
		}

		[[nodiscard]] CommunityShadersMainCullingFinalizeChainPolicy::ChainKind
			ClassifyFinalizeChainTarget(
				const std::uintptr_t a_inspectedTarget,
				const std::uintptr_t a_expectedNativeTarget) noexcept
		{
			namespace Chain = CommunityShadersMainCullingFinalizeChainPolicy;
			if (a_inspectedTarget == a_expectedNativeTarget)
				return Chain::ChainKind::kNative;

			std::array<std::uint8_t, Chain::kFiveByteCallBranchStubSize> branchStub{};
			std::array<std::uint8_t, Chain::kThunkPrefix.size()> thunkPrefix{};
			Chain::Evidence evidence{};
			if (!ReadCommunityShaders184ChainEvidenceSEH(
					a_inspectedTarget, a_expectedNativeTarget, evidence,
					branchStub, thunkPrefix)) {
				return Chain::ChainKind::kRejected;
			}
			return Chain::Classify(evidence);
		}

		void FinalizeMainCullingHook() noexcept
		{
			g_diagnostics.hookCalls.fetch_add(1, std::memory_order_relaxed);
			const auto original = reinterpret_cast<void (*)()>(
				g_originalFinalize.load(std::memory_order_acquire));
			if (original)
				original();
			if (!g_enabled.load(std::memory_order_acquire))
				return;
			(void) BeginBorrow();
		}
	}

	void SetRequested(bool a_requested) noexcept
	{
		g_requested.store(a_requested, std::memory_order_release);
	}

	bool Requested() noexcept
	{
		return g_requested.load(std::memory_order_acquire);
	}

	bool InstallHook(CaptureCallback a_callback) noexcept
	{
		if (!a_callback || g_hookInstalled.load(std::memory_order_acquire))
			return false;
		const auto version = REL::Module::get().version();
		const bool isVR = SupportedRuntimePolicy::IsExactVRRuntime();
		if (!((REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
				(REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
				isVR))
			return false;

		// VR 1.4.15: RenderPlayerView (35560 -> 0x5B9330) calls MainAccum
		// (100415 -> 0x1322130) at +0x248; the 31-byte masked pattern is shared.
		// Skyrim 1.7.104: RenderPlayerView (0x656E60) calls MainAccum (0x1538430)
		// at +0x30A instead of the AE 1.6.1170 +0x2EC, and REL::Relocate cannot
		// separate the two AE runtimes, so the exact runtime is passed in.
		const bool isAE17104 = SupportedRuntimePolicy::IsExactAE17104Runtime();
		const auto renderPlayerView =
			REL::Relocation<std::uintptr_t>{ RELOCATION_ID(35560, 36559) }.address();
		const auto callSite = renderPlayerView +
			RenderPlayerViewMainCullingFinalizeSignature::FinalizeCallOffsetFor(
				isVR, isAE17104);
		std::array<std::uint8_t,
			RenderPlayerViewMainCullingFinalizeSignature::kWindowSize> window{};
		std::int32_t displacement = 0;
		const bool readable = ReadCallSiteWindowSEH(
			callSite, window.data(), window.size(), &displacement);
		const auto expectedFinalize =
			REL::Relocation<std::uintptr_t>{ RELOCATION_ID(100415, 107133) }.address();
		const auto decodedTarget = callSite + 5 + displacement;
		if (!readable ||
			!RenderPlayerViewMainCullingFinalizeSignature::Matches(window)) {
			logger::critical(
				"[RR][M3][main-scene-lists] refused hook: post-finalizer surrounding signature mismatch at 0x{:X}",
				callSite);
			return false;
		}
		const auto chainKind = ClassifyFinalizeChainTarget(
			decodedTarget, expectedFinalize);
		if (chainKind == CommunityShadersMainCullingFinalizeChainPolicy::
				ChainKind::kRejected) {
			logger::critical(
				"[RR][M3][main-scene-lists] refused hook: post-finalizer target is not native or the exact verified Community Shaders v1.8.4 chain at 0x{:X} target=0x{:X} native=0x{:X}",
				callSite, decodedTarget, expectedFinalize);
			return false;
		}

		g_captureCallback.store(a_callback, std::memory_order_release);
		// The inspected target is either native or the exact v1.8.4 CS wrapper
		// whose own preserved-function slot was independently proven to be native.
		// Keep that complete chain as our inner call; BeginBorrow runs only after it
		// returns, preserving both Skyrim and CS behavior.
		g_originalFinalize.store(decodedTarget, std::memory_order_release);
		std::uintptr_t chainedTarget = 0;
		try {
			chainedTarget = SKSE::GetTrampoline().write_call<5>(
				callSite, FinalizeMainCullingHook);
		} catch (...) {
			return false;
		}
		g_hookInstalled.store(true, std::memory_order_release);
		if (chainedTarget != decodedTarget) {
			if (chainedTarget)
				g_originalFinalize.store(chainedTarget, std::memory_order_release);
			LatchFault(false);
			return false;
		}
		logger::info(
			"[RR][M3][main-scene-lists] dormant post-finalizer borrow hook installed at 0x{:X} preservedChain={}",
			callSite,
			CommunityShadersMainCullingFinalizeChainPolicy::ToString(chainKind));
		return true;
	}

	bool HookInstalled() noexcept
	{
		return g_hookInstalled.load(std::memory_order_acquire);
	}

	void SetEnabled(bool a_enabled) noexcept
	{
		g_enabled.store(
			a_enabled && Requested() && HookInstalled() && !Faulted(),
			std::memory_order_release);
	}

	bool Enabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire);
	}

	bool Active() noexcept
	{
		return Enabled() && g_borrow.token.active &&
			g_borrow.token.threadID == GetCurrentThreadId() && !g_borrow.token.faulted;
	}

	bool Faulted() noexcept
	{
		return g_faulted.load(std::memory_order_acquire);
	}

	bool RunDeferredCapture() noexcept
	{
		if (!g_borrow.token.active)
			return false;
		if (!g_enabled.load(std::memory_order_acquire)) {
			// A fault may disable the module after Begin retained a partial or complete
			// snapshot. Cleanup ownership is independent of activation and must still
			// close before returning to the shared post-world owner.
			EndBorrow();
			return false;
		}
		return RunBorrowedCaptureWithSEH();
	}

	bool Acquire(BorrowView& a_view) noexcept
	{
		if (!Active())
			return false;
		const auto transition = Policy::Acquire(
			g_borrow.token, GetCurrentThreadId(), g_borrow.token.generation);
		if (transition != Policy::Transition::kAccepted) {
			if (transition == Policy::Transition::kWrongThread)
				g_diagnostics.wrongThread.fetch_add(1, std::memory_order_relaxed);
			else
				g_diagnostics.generationRejects.fetch_add(1, std::memory_order_relaxed);
			LatchFault(false);
			return false;
		}
		a_view = { g_borrow.lists.data(), g_borrow.listCount,
			g_borrow.nonEmptyListCount, g_borrow.totalEntries,
			g_borrow.token.threadID, g_borrow.token.generation };
		g_diagnostics.acquires.fetch_add(1, std::memory_order_relaxed);
		++g_borrow.replayAcquisitions;
		g_borrow.replayCleanupComplete = false;
		return true;
	}

	bool RearmForNextMirrorCapture(std::size_t frameBudget) noexcept
	{
		if (!Active() || g_derived.active || Faulted())
			return false;
		// A geometric skip before Acquire has not consumed any native replay.
		if (!g_borrow.token.consumed)
			return true;
		const BorrowView view{ g_borrow.lists.data(), g_borrow.listCount,
			g_borrow.nonEmptyListCount, g_borrow.totalEntries,
			g_borrow.token.threadID, g_borrow.token.generation };
		if (!Policy::CanRearmReplay(g_borrow.token, GetCurrentThreadId(), view.generation,
				g_borrow.replayCleanupComplete, g_borrow.replayAcquisitions, frameBudget) ||
			!Revalidate(view))
			return false;
		g_borrow.token.consumed = false;
		g_borrow.publicationReady = false;
		g_borrow.replayCleanupComplete = false;
		return true;
	}

	DerivedReplayBuildResult BuildDerivedReplay(
		const BorrowView& a_original,
		const LODPolicy::SupplementalCoverageEvidence& a_coverage,
		DerivedReplayView& a_derived) noexcept
	{
		a_derived = {};
		DerivedReplayBuildResult result{};
		if (g_derived.active) {
			g_diagnostics.derivedBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(false);
			(void)ClearDerivedReplayState();
			return result;
		}
		(void)ClearDerivedReplayState();
		if (!Revalidate(a_original)) {
			g_diagnostics.derivedBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			return result;
		}

		result.coverageStatus = SnapshotCoverage(a_coverage);
		if (result.coverageStatus != LODPolicy::CoverageStatus::kComplete) {
			result.status = DerivedReplayBuildStatus::kCoverageRejected;
			g_diagnostics.derivedCoverageRejects.fetch_add(
				1, std::memory_order_relaxed);
			(void)ClearDerivedReplayState();
			return result;
		}

		bool built = false;
		__try {
			Policy::DerivedReplayCounts counts{};
			bool valid = true;
			for (std::size_t listIndex = 0;
				 listIndex < a_original.listCount && valid; ++listIndex) {
				const auto& sourceList = a_original.lists[listIndex];
				const auto sourceCount = sourceList.header.size;
				auto** sourceEntries =
					static_cast<RE::NiAVObject**>(sourceList.header.data);
				const auto derivedOffset = g_derived.ownedEntryCount;
				std::uint32_t retainedCount = 0;
				for (std::uint32_t entryIndex = 0;
					 entryIndex < sourceCount; ++entryIndex) {
					if (!sourceEntries || !sourceEntries[entryIndex]) {
						valid = false;
						break;
					}
					const auto root = reinterpret_cast<std::uintptr_t>(
						sourceEntries[entryIndex]);
					if (CoveredBySupplemental(root))
						continue;
					if (g_derived.ownedEntryCount >=
						g_derived.entries.values.size()) {
						valid = false;
						break;
					}
					const auto destination = g_derived.ownedEntryCount;
					if (!RetainEntrySEH(
							g_derived.entries, destination,
							sourceEntries[entryIndex])) {
						valid = false;
						LatchFault(true);
						break;
					}
					++g_derived.ownedEntryCount;
					++retainedCount;
				}
				if (!valid || Policy::AddDerivedList(
						counts, sourceCount, retainedCount) !=
						Policy::DerivedListStatus::kAccepted) {
					valid = false;
					break;
				}

				auto& header = g_derived.headers[listIndex];
				header.data = retainedCount != 0 ?
					static_cast<void*>(
						g_derived.entries.values.data() + derivedOffset) :
					nullptr;
				header.capacity = retainedCount;
				header.size = retainedCount;
				g_derived.lists[listIndex] = {
					&header,
					{ header.data, header.capacity, header.size },
					sourceList.kind,
					sourceList.unk61
				};
			}

			valid = valid &&
				counts.listCount == a_original.listCount &&
				static_cast<std::uint64_t>(counts.totalEntries) +
					counts.filteredEntries == a_original.totalEntries &&
				Policy::DerivedReplayCountsAreValid(counts);
			if (valid) {
				g_derived.counts = counts;
				g_derived.threadID = a_original.threadID;
				g_derived.generation = a_original.generation;
				g_derived.active = true;
				built = true;
			}
		} __except (BorrowExceptionFilter(GetExceptionCode())) {
			built = false;
		}

		if (!built) {
			g_diagnostics.derivedBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			if (!Faulted())
				LatchFault(false);
			(void)ClearDerivedReplayState();
			return result;
		}

		a_derived = {
			g_derived.lists.data(),
			g_derived.counts.listCount,
			g_derived.counts.nonEmptyListCount,
			g_derived.counts.totalEntries,
			g_derived.counts.filteredEntries,
			g_derived.threadID,
			g_derived.generation
		};
		result.status = DerivedReplayBuildStatus::kBuilt;
		g_diagnostics.derivedBuilds.fetch_add(1, std::memory_order_relaxed);
		g_diagnostics.derivedNonEmptyLists.fetch_add(
			a_derived.nonEmptyListCount, std::memory_order_relaxed);
		g_diagnostics.derivedEntries.fetch_add(
			a_derived.totalEntries, std::memory_order_relaxed);
		g_diagnostics.derivedFilteredEntries.fetch_add(
			a_derived.filteredEntries, std::memory_order_relaxed);
		return result;
	}

	Policy::PlayerCoverage ClassifyPlayerCoverage(
		const BorrowView& a_view, RE::NiAVObject* a_playerRoot) noexcept
	{
		// MirrorPlayerInclusion returns no additional root only after proving the
		// retained body root is a WorldRoot descendant. Exact-main target probes
		// corroborate player/skinned draws from the borrowed lists in this state.
		// Do not manufacture a duplicate cull here: unlike the split water/material
		// accumulators, a second player descent in this primary accumulator has no
		// accepted drain/lifetime proof.
		if (!a_playerRoot)
			return Policy::PlayerCoverage::kCovered;
		Policy::PlayerCoverage result = Policy::PlayerCoverage::kInvalid;
		__try {
			std::array<std::uintptr_t, Policy::kMaximumAncestryDepth> ancestry{};
			std::size_t ancestryCount = 0;
			bool terminated = false;
			auto* cursor = a_playerRoot;
			while (cursor && ancestryCount < ancestry.size()) {
				const auto address = reinterpret_cast<std::uintptr_t>(cursor);
				for (std::size_t i = 0; i < ancestryCount; ++i) {
					if (ancestry[i] == address) {
						result = Policy::PlayerCoverage::kInvalid;
						return result;
					}
				}
				ancestry[ancestryCount++] = address;
				cursor = cursor->parent;
			}
			terminated = cursor == nullptr;
			if (!terminated) {
				result = Policy::PlayerCoverage::kInvalid;
				return result;
			}
			for (std::size_t listIndex = 0; listIndex < a_view.listCount; ++listIndex) {
				const auto& list = a_view.lists[listIndex];
				auto** entries = static_cast<void**>(list.header.data);
				for (std::uint32_t entryIndex = 0; entryIndex < list.header.size; ++entryIndex) {
					const auto entry = reinterpret_cast<std::uintptr_t>(entries[entryIndex]);
					for (std::size_t ancestryIndex = 0;
						 ancestryIndex < ancestryCount; ++ancestryIndex) {
						if (entry != 0 && entry == ancestry[ancestryIndex]) {
							result = Policy::PlayerCoverage::kCovered;
							return result;
						}
					}
				}
			}
			result = Policy::ClassifyPlayerCoverage(
				{}, std::span<const std::uintptr_t>{ ancestry.data(), ancestryCount },
				terminated);
		} __except (BorrowExceptionFilter(GetExceptionCode())) {
			result = Policy::PlayerCoverage::kInvalid;
		}
		if (result == Policy::PlayerCoverage::kInvalid)
			LatchFault(false);
		return result;
	}

	bool CompleteReplay(
		const BorrowView& a_view, bool a_finished, bool a_accumulator178Restored,
		std::size_t a_listCulls, std::size_t a_listReturns) noexcept
	{
		if (g_derived.active) {
			g_diagnostics.derivedBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(false);
			(void)ClearDerivedReplayState();
			return false;
		}
		const bool current = Revalidate(a_view);
		const bool complete = a_finished &&
			a_listCulls == a_view.nonEmptyListCount && a_listReturns == a_listCulls;
		g_borrow.publicationReady = Policy::PublicationAllowed(
			Active(), complete, current, a_accumulator178Restored, Faulted());
		if (!g_borrow.publicationReady && !Faulted() && a_view.totalEntries != 0)
			LatchFault(false);
		g_borrow.replayCleanupComplete = g_borrow.publicationReady;
		return g_borrow.publicationReady;
	}

	bool CompleteDerivedReplay(
		const BorrowView& a_original, const DerivedReplayView& a_derived,
		bool a_finished, bool a_accumulator178Restored,
		std::size_t a_listCulls, std::size_t a_listReturns) noexcept
	{
		const bool current = RevalidateDerived(a_original, a_derived);
		const bool complete = Policy::DerivedReplayComplete(
			g_derived.counts, a_finished, a_listCulls, a_listReturns);
		g_borrow.publicationReady = Policy::PublicationAllowed(
			Active(), complete, current, a_accumulator178Restored, Faulted());
		if (g_borrow.publicationReady) {
			g_diagnostics.derivedCompletions.fetch_add(
				1, std::memory_order_relaxed);
		} else if (!Faulted() && a_original.totalEntries != 0) {
			LatchFault(false);
		}
		const bool released = ClearDerivedReplayState();
		g_borrow.replayCleanupComplete = released && g_borrow.publicationReady && !Faulted();
		return g_borrow.publicationReady;
	}

	bool DiscardDerivedReplay(
		const BorrowView& a_original,
		const DerivedReplayView& a_derived) noexcept
	{
		const bool current = RevalidateDerived(a_original, a_derived);
		g_borrow.publicationReady = false;
		if (current && Active() && !Faulted()) {
			g_diagnostics.derivedDiscards.fetch_add(
				1, std::memory_order_relaxed);
		}
		const bool released = ClearDerivedReplayState();
		g_borrow.replayCleanupComplete = released && current && Active() && !Faulted();
		return g_borrow.replayCleanupComplete;
	}

	bool AbandonReplay(const BorrowView& a_view) noexcept
	{
		// A reflected-camera lease can legitimately miss a frame when no eligible
		// upload occurs. The retained engine lists still require exact revalidation,
		// but an intact borrow may be discarded without quarantining the session.
		if (g_derived.active) {
			g_diagnostics.derivedBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(false);
			(void)ClearDerivedReplayState();
			return false;
		}
		const bool current = Revalidate(a_view);
		g_borrow.publicationReady = false;
		g_borrow.replayCleanupComplete = current && Active() && !Faulted();
		return g_borrow.replayCleanupComplete;
	}

	bool PublicationReady() noexcept
	{
		return Active() && g_borrow.token.consumed && g_borrow.publicationReady &&
			!Faulted();
	}

	void FailCurrentBorrow() noexcept
	{
		LatchFault(false);
		// This public endpoint runs in an __except handler or ordinary control
		// flow, not inside the exception filter itself, so releasing retained
		// pointers here is safe. EndBorrow remains the final cleanup barrier.
		(void)ClearDerivedReplayState();
	}

	void RecordFallbackSkip() noexcept
	{
		g_diagnostics.fallbackSkips.fetch_add(1, std::memory_order_relaxed);
	}

	void RecordCull(bool a_returned) noexcept
	{
		g_diagnostics.culls.fetch_add(1, std::memory_order_relaxed);
		if (a_returned)
			g_diagnostics.returns.fetch_add(1, std::memory_order_relaxed);
	}

	void RecordAccumulator178Apply() noexcept
	{
		g_diagnostics.accumulator178Applies.fetch_add(1, std::memory_order_relaxed);
	}

	void RecordAccumulator178Restore() noexcept
	{
		g_diagnostics.accumulator178Restores.fetch_add(1, std::memory_order_relaxed);
	}

	DiagnosticsSnapshot Diagnostics() noexcept
	{
		return {
			g_diagnostics.hookCalls.load(std::memory_order_relaxed),
			g_diagnostics.begins.load(std::memory_order_relaxed),
			g_diagnostics.acquires.load(std::memory_order_relaxed),
			g_diagnostics.unused.load(std::memory_order_relaxed),
			g_diagnostics.nested.load(std::memory_order_relaxed),
			g_diagnostics.wrongThread.load(std::memory_order_relaxed),
			g_diagnostics.generationRejects.load(std::memory_order_relaxed),
			g_diagnostics.headerRejects.load(std::memory_order_relaxed),
			g_diagnostics.sehFaults.load(std::memory_order_relaxed),
			g_diagnostics.faults.load(std::memory_order_relaxed),
			g_diagnostics.regularLists.load(std::memory_order_relaxed),
			g_diagnostics.portalLists.load(std::memory_order_relaxed),
			g_diagnostics.inlineLists.load(std::memory_order_relaxed),
			g_diagnostics.entries.load(std::memory_order_relaxed),
			g_diagnostics.culls.load(std::memory_order_relaxed),
			g_diagnostics.returns.load(std::memory_order_relaxed),
			g_diagnostics.fallbackSkips.load(std::memory_order_relaxed),
			g_diagnostics.accumulator178Applies.load(std::memory_order_relaxed),
			g_diagnostics.accumulator178Restores.load(std::memory_order_relaxed),
			g_diagnostics.derivedBuilds.load(std::memory_order_relaxed),
			g_diagnostics.derivedCoverageRejects.load(std::memory_order_relaxed),
			g_diagnostics.derivedBorrowRejects.load(std::memory_order_relaxed),
			g_diagnostics.derivedNonEmptyLists.load(std::memory_order_relaxed),
			g_diagnostics.derivedEntries.load(std::memory_order_relaxed),
			g_diagnostics.derivedFilteredEntries.load(std::memory_order_relaxed),
			g_diagnostics.derivedCompletions.load(std::memory_order_relaxed),
			g_diagnostics.derivedDiscards.load(std::memory_order_relaxed)
		};
	}
}
