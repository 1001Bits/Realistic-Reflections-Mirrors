#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MirrorMainSceneListBorrowPolicy
{
	inline constexpr std::uint32_t kMaximumWorkers = 128;
	inline constexpr std::uint32_t kMaximumEntriesPerList = 8192;
	inline constexpr std::uint32_t kMaximumTotalEntries = 16384;
	inline constexpr std::size_t kMaximumLists = kMaximumWorkers + 2;
	inline constexpr std::size_t kMaximumAncestryDepth = 128;
	inline constexpr std::uint32_t kNativeRenderFlags = 8;

	/**
	 * Replay-safe ownership metadata for one retained NiAVObject reference.
	 * The contiguous native replay array is stored separately as raw pointer
	 * values; this scalar slot is never passed to the engine.
	 */
	struct RetainedReferenceSlot
	{
		std::uintptr_t identity{ 0 };
		bool owned{ false };
	};

	enum class RetainReferenceStatus : std::uint8_t
	{
		kCommitted,
		kInvalidIdentity,
		kSlotOccupied,
		// IncRef did not return normally. Its side effect is unknowable, so the
		// identity is intentionally not stored and must never be balanced/retried.
		kUncertainNativeFault
	};

	[[nodiscard]] constexpr RetainReferenceStatus CommitRetainedReference(
		RetainedReferenceSlot& a_slot,
		std::uintptr_t a_identity,
		bool a_retainReturnedNormally) noexcept
	{
		if (a_slot.owned || a_slot.identity != 0)
			return RetainReferenceStatus::kSlotOccupied;
		if (a_identity == 0)
			return RetainReferenceStatus::kInvalidIdentity;
		if (!a_retainReturnedNormally)
			return RetainReferenceStatus::kUncertainNativeFault;
		a_slot = { a_identity, true };
		return RetainReferenceStatus::kCommitted;
	}

	/**
	 * Remove every replayable ownership bit before the native DecRef call. A
	 * failed release therefore leaks one quarantined reference at worst; it can
	 * never make teardown retry the same object.
	 */
	[[nodiscard]] constexpr std::uintptr_t TombstoneForRelease(
		RetainedReferenceSlot& a_slot) noexcept
	{
		if (!a_slot.owned || a_slot.identity == 0) {
			a_slot = {};
			return 0;
		}
		const auto identity = a_slot.identity;
		a_slot = {};
		return identity;
	}

	enum class Runtime : std::uint8_t
	{
		kUnsupported,
		kSE1597,
		kAE161170,
		kVR1415
	};

	struct ActivationInputs
	{
		bool markerRequested{ false };
		Runtime runtime{ Runtime::kUnsupported };
		bool mirrorEnabled{ false };
		bool reflectedCameraEnabled{ false };
		bool hookInstalled{ false };
		bool auxiliaryFirstRequested{ false };
		bool allPassRequested{ false };
		bool nativeFlags0Requested{ false };
	};

	[[nodiscard]] constexpr bool IsExactSupportedRuntime(Runtime a_runtime) noexcept
	{
		return a_runtime == Runtime::kSE1597 || a_runtime == Runtime::kAE161170 ||
			a_runtime == Runtime::kVR1415;
	}

	/** Public Mirror may claim readiness only when its selected exact-main route exists. */
	[[nodiscard]] constexpr bool ProductReady(
		bool a_exactMainRequired, bool a_hookInstalled) noexcept
	{
		return !a_exactMainRequired || a_hookInstalled;
	}

	[[nodiscard]] constexpr bool HasConflict(const ActivationInputs& a_inputs) noexcept
	{
		return a_inputs.auxiliaryFirstRequested || a_inputs.allPassRequested ||
			a_inputs.nativeFlags0Requested;
	}

	[[nodiscard]] constexpr bool ShouldActivate(const ActivationInputs& a_inputs) noexcept
	{
		return a_inputs.markerRequested && IsExactSupportedRuntime(a_inputs.runtime) &&
			a_inputs.mirrorEnabled && a_inputs.reflectedCameraEnabled &&
			a_inputs.hookInstalled && !HasConflict(a_inputs);
	}

	struct HeaderShape
	{
		std::uintptr_t data{ 0 };
		std::uint32_t capacity{ 0 };
		std::uint32_t size{ 0 };
	};

	[[nodiscard]] constexpr bool HeaderIsValid(const HeaderShape& a_header) noexcept
	{
		return a_header.size <= a_header.capacity &&
			a_header.capacity <= kMaximumEntriesPerList &&
			(a_header.size == 0 || a_header.data != 0);
	}

	[[nodiscard]] constexpr bool WorkerSetIsValid(
		std::uint32_t a_workerCount,
		const HeaderShape& a_outer,
		const HeaderShape& a_process) noexcept
	{
		return a_workerCount != 0 && a_workerCount <= kMaximumWorkers &&
			HeaderIsValid(a_outer) && HeaderIsValid(a_process) &&
			a_outer.data != 0 && a_process.data != 0 &&
			a_outer.size >= a_workerCount && a_process.size >= a_workerCount;
	}

	enum class ListKind : std::uint8_t
	{
		kRegular,
		kPortal,
		kInline
	};

	struct ListStep
	{
		ListKind kind{ ListKind::kRegular };
		std::uint32_t workerIndex{ 0 };
	};

	struct ReplaySequence
	{
		std::array<ListStep, kMaximumLists> steps{};
		std::size_t count{ 0 };
	};

	[[nodiscard]] constexpr ReplaySequence BuildReplaySequence(
		std::uint32_t a_workerCount, bool a_hasPortal) noexcept
	{
		ReplaySequence result{};
		if (a_workerCount == 0 || a_workerCount > kMaximumWorkers)
			return result;
		result.steps[result.count++] = { ListKind::kRegular, 0 };
		if (a_hasPortal)
			result.steps[result.count++] = { ListKind::kPortal, 0 };
		result.steps[result.count++] = { ListKind::kInline, 0 };
		for (std::uint32_t i = 1; i < a_workerCount; ++i)
			result.steps[result.count++] = { ListKind::kRegular, i };
		return result;
	}

	struct BorrowToken
	{
		std::uint32_t threadID{ 0 };
		std::uint64_t generation{ 0 };
		bool active{ false };
		bool consumed{ false };
		bool faulted{ false };
	};

	enum class Transition : std::uint8_t
	{
		kAccepted,
		kNested,
		kWrongThread,
		kWrongGeneration,
		kAlreadyConsumed,
		kFaulted
	};

	[[nodiscard]] constexpr Transition Begin(
		BorrowToken& a_token, std::uint32_t a_threadID,
		std::uint64_t a_generation) noexcept
	{
		if (a_token.faulted)
			return Transition::kFaulted;
		if (a_token.active)
			return Transition::kNested;
		if (a_threadID == 0 || a_generation == 0)
			return Transition::kWrongGeneration;
		a_token = { a_threadID, a_generation, true, false, false };
		return Transition::kAccepted;
	}

	[[nodiscard]] constexpr Transition Acquire(
		BorrowToken& a_token, std::uint32_t a_threadID,
		std::uint64_t a_generation) noexcept
	{
		if (a_token.faulted)
			return Transition::kFaulted;
		if (!a_token.active || a_token.generation != a_generation)
			return Transition::kWrongGeneration;
		if (a_token.threadID != a_threadID)
			return Transition::kWrongThread;
		if (a_token.consumed)
			return Transition::kAlreadyConsumed;
		a_token.consumed = true;
		return Transition::kAccepted;
	}

	[[nodiscard]] constexpr Transition End(
		BorrowToken& a_token, std::uint32_t a_threadID,
		std::uint64_t a_generation) noexcept
	{
		if (!a_token.active || a_token.generation != a_generation)
			return Transition::kWrongGeneration;
		if (a_token.threadID != a_threadID)
			return Transition::kWrongThread;
		a_token.active = false;
		return Transition::kAccepted;
	}

	/** Only the separately enabled every-frame mirror batch may reuse a retained list.
	 * A previous native replay must have completed/discarded with full cleanup.
	 * Identity, thread, capacity and fault checks survive between each capture.
	 */
	[[nodiscard]] constexpr bool CanRearmReplay(
		const BorrowToken& token, std::uint32_t threadID, std::uint64_t generation,
		bool cleanupComplete, std::size_t acquisitions, std::size_t budget) noexcept
	{
		return token.active && token.consumed && !token.faulted &&
			threadID != 0 && token.threadID == threadID && generation != 0 &&
			token.generation == generation && cleanupComplete &&
			budget > 1 && acquisitions != 0 && acquisitions < budget;
	}

	enum class PlayerCoverage : std::uint8_t
	{
		kCovered,
		kNotCovered,
		kInvalid
	};

	[[nodiscard]] constexpr PlayerCoverage ClassifyPlayerCoverage(
		std::span<const std::uintptr_t> a_entries,
		std::span<const std::uintptr_t> a_ancestry,
		bool a_chainTerminated) noexcept
	{
		if (a_ancestry.empty() || a_ancestry.size() > kMaximumAncestryDepth)
			return PlayerCoverage::kInvalid;
		for (std::size_t i = 0; i < a_ancestry.size(); ++i) {
			if (a_ancestry[i] == 0)
				return PlayerCoverage::kInvalid;
			for (std::size_t j = 0; j < i; ++j) {
				if (a_ancestry[i] == a_ancestry[j])
					return PlayerCoverage::kInvalid;
			}
			for (const auto entry : a_entries) {
				if (entry == a_ancestry[i])
					return PlayerCoverage::kCovered;
			}
		}
		return a_chainTerminated ? PlayerCoverage::kNotCovered :
			PlayerCoverage::kInvalid;
	}

	struct CaptureTopology
	{
		std::uint32_t targetClearCount{ 1 };
		std::uint32_t accumulatorCycleCount{ 1 };
		std::uint32_t listCullCount{ 0 };
		std::uint32_t playerCullCount{ 0 };
		bool syntheticWorldRoot{ false };
		bool auxiliaryRoots{ false };
		std::uint32_t renderFlags{ kNativeRenderFlags };
		bool accumulator178Applied{ true };
		bool accumulator178Restored{ true };
	};

	[[nodiscard]] constexpr CaptureTopology BuildTopology(
		std::uint32_t a_nonEmptyLists, PlayerCoverage a_playerCoverage) noexcept
	{
		CaptureTopology result{};
		result.listCullCount = a_nonEmptyLists;
		result.playerCullCount =
			a_playerCoverage == PlayerCoverage::kNotCovered ? 1u : 0u;
		return result;
	}

	/**
	 * Shape accounting for a plugin-owned replay derived from the immutable
	 * retained main-scene lists.  totalEntries is the retained entry count;
	 * filteredEntries is the exact-root subset removed because a complete
	 * supplemental cycle covers it.
	 */
	struct DerivedReplayCounts
	{
		std::size_t listCount{ 0 };
		std::size_t nonEmptyListCount{ 0 };
		std::uint32_t totalEntries{ 0 };
		std::uint32_t filteredEntries{ 0 };
	};

	enum class DerivedListStatus : std::uint8_t
	{
		kAccepted,
		kMalformed,
		kListOverflow,
		kEntryOverflow
	};

	/** Add one source list atomically; rejection leaves the counters unchanged. */
	[[nodiscard]] constexpr DerivedListStatus AddDerivedList(
		DerivedReplayCounts& a_counts, std::uint32_t a_sourceEntries,
		std::uint32_t a_retainedEntries) noexcept
	{
		if (a_retainedEntries > a_sourceEntries ||
			a_sourceEntries > kMaximumEntriesPerList)
			return DerivedListStatus::kMalformed;
		if (a_counts.listCount >= kMaximumLists)
			return DerivedListStatus::kListOverflow;
		const auto accounted = static_cast<std::uint64_t>(a_counts.totalEntries) +
			a_counts.filteredEntries;
		if (accounted > kMaximumTotalEntries ||
			a_sourceEntries > kMaximumTotalEntries - accounted)
			return DerivedListStatus::kEntryOverflow;

		++a_counts.listCount;
		if (a_retainedEntries != 0)
			++a_counts.nonEmptyListCount;
		a_counts.totalEntries += a_retainedEntries;
		a_counts.filteredEntries += a_sourceEntries - a_retainedEntries;
		return DerivedListStatus::kAccepted;
	}

	[[nodiscard]] constexpr bool DerivedReplayCountsAreValid(
		const DerivedReplayCounts& a_counts) noexcept
	{
		return a_counts.listCount <= kMaximumLists &&
			a_counts.nonEmptyListCount <= a_counts.listCount &&
			static_cast<std::uint64_t>(a_counts.totalEntries) +
				a_counts.filteredEntries <= kMaximumTotalEntries;
	}

	[[nodiscard]] constexpr bool DerivedReplayComplete(
		const DerivedReplayCounts& a_counts, bool a_finished,
		std::size_t a_listCulls, std::size_t a_listReturns) noexcept
	{
		return DerivedReplayCountsAreValid(a_counts) && a_finished &&
			a_listCulls == a_counts.nonEmptyListCount &&
			a_listReturns == a_listCulls;
	}

	// Both exact-main accumulators call the private pre/post wrapper directly, so
	// neither may bypass the known refraction-normals group clear. Keep the
	// corruption probes after their respective clears: they validate completed
	// cleanup, not the wrappers' expected pre-clear group-5 residue.
	enum class ExactMainDrainStep : std::uint8_t
	{
		kPrimaryWrapper,
		kPrimaryRefractionGroupClear,
		kPrimaryInventoryValidation,
		kSupplementalWrapper,
		kSupplementalRefractionGroupClear,
		kSupplementalInventoryValidation
	};

	struct ExactMainDrainTopology
	{
		std::array<ExactMainDrainStep, 6> steps{};
		std::uint8_t stepCount{ 0 };
	};

	[[nodiscard]] constexpr ExactMainDrainTopology BuildExactMainDrainTopology(
		bool a_withSupplementalCycle) noexcept
	{
		ExactMainDrainTopology result{};
		result.steps[0] = ExactMainDrainStep::kPrimaryWrapper;
		result.steps[1] = ExactMainDrainStep::kPrimaryRefractionGroupClear;
		result.steps[2] = ExactMainDrainStep::kPrimaryInventoryValidation;
		result.stepCount = 3;
		if (a_withSupplementalCycle) {
			result.steps[3] = ExactMainDrainStep::kSupplementalWrapper;
			result.steps[4] = ExactMainDrainStep::kSupplementalRefractionGroupClear;
			result.steps[5] = ExactMainDrainStep::kSupplementalInventoryValidation;
			result.stepCount = 6;
		}
		return result;
	}

	[[nodiscard]] constexpr bool IsValidExactMainDrainTopology(
		const ExactMainDrainTopology& a_topology,
		bool a_withSupplementalCycle) noexcept
	{
		if (a_topology.stepCount != (a_withSupplementalCycle ? 6u : 3u) ||
			a_topology.steps[0] != ExactMainDrainStep::kPrimaryWrapper ||
			a_topology.steps[1] != ExactMainDrainStep::kPrimaryRefractionGroupClear ||
			a_topology.steps[2] != ExactMainDrainStep::kPrimaryInventoryValidation)
			return false;
		if (!a_withSupplementalCycle)
			return true;
		return a_topology.steps[3] == ExactMainDrainStep::kSupplementalWrapper &&
			a_topology.steps[4] ==
				ExactMainDrainStep::kSupplementalRefractionGroupClear &&
			a_topology.steps[5] ==
				ExactMainDrainStep::kSupplementalInventoryValidation;
	}

	[[nodiscard]] constexpr bool PublicationAllowed(
		bool a_currentBorrow, bool a_completeListSet, bool a_headersCurrent,
		bool a_accumulator178Restored, bool a_faulted) noexcept
	{
		return a_currentBorrow && a_completeListSet && a_headersCurrent &&
			a_accumulator178Restored && !a_faulted;
	}

	enum class InventoryProbe : std::uint8_t
	{
		kClean,
		kLeftovers,
		kUnavailable
	};

	enum class SupplementalInventoryAction : std::uint8_t
	{
		kProceed,
		kProceedUncorroborated,
		kStructuralReject
	};

	// The batch-inventory inspector is sacrificial telemetry: it self-disables
	// after a bounded number of SEH faults. Only a probe that positively proves
	// leftover passes is structural corruption; an unavailable probe (disabled,
	// faulted, or unreadable renderer state) must never reject the supplemental
	// cycle, or the telemetry valve becomes a permanent module latch (observed
	// 2026-08-18: four inspector faults disabled the probe, and the fifth
	// supplemental cycle treated the disabled probe as corruption).
	[[nodiscard]] constexpr SupplementalInventoryAction
		ClassifySupplementalInventoryProbe(InventoryProbe a_probe) noexcept
	{
		switch (a_probe) {
		case InventoryProbe::kClean:
			return SupplementalInventoryAction::kProceed;
		case InventoryProbe::kLeftovers:
			return SupplementalInventoryAction::kStructuralReject;
		case InventoryProbe::kUnavailable:
		default:
			return SupplementalInventoryAction::kProceedUncorroborated;
		}
	}
}
