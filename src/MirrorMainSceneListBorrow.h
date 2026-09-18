#pragma once

#include "MirrorMainSceneListBorrowPolicy.h"
#include "MirrorPrivateLODStabilizationPolicy.h"

#include <cstddef>
#include <cstdint>

namespace RE
{
	class NiAVObject;
}

namespace MirrorMainSceneListBorrow
{
	using CaptureCallback = void (*)() noexcept;

	struct HeaderSnapshot
	{
		// Read-only shape metadata only.  This is deliberately not the engine's
		// 0x18 native array descriptor and must never be passed to a native cull.
		// ListView::headerAddress is the sole native-descriptor address.
		void* data{ nullptr };
		std::uint32_t capacity{ 0 };
		std::uint32_t size{ 0 };
	};

	struct ListView
	{
		void* headerAddress{ nullptr };
		HeaderSnapshot header{};
		MirrorMainSceneListBorrowPolicy::ListKind kind{
			MirrorMainSceneListBorrowPolicy::ListKind::kRegular };
		bool unk61{ false };
	};

	struct BorrowView
	{
		const ListView* lists{ nullptr };
		std::size_t listCount{ 0 };
		std::size_t nonEmptyListCount{ 0 };
		std::uint32_t totalEntries{ 0 };
		std::uint32_t threadID{ 0 };
		std::uint64_t generation{ 0 };
	};

	/**
	 * A one-borrow, plugin-owned replay descriptor set derived from BorrowView.
	 * It is intentionally a distinct type: callers cannot accidentally complete
	 * or replay it as the immutable original retained borrow.
	 */
	struct DerivedReplayView
	{
		const ListView* lists{ nullptr };
		std::size_t listCount{ 0 };
		std::size_t nonEmptyListCount{ 0 };
		std::uint32_t totalEntries{ 0 };
		std::uint32_t filteredEntries{ 0 };
		std::uint32_t threadID{ 0 };
		std::uint64_t generation{ 0 };
	};

	enum class DerivedReplayBuildStatus : std::uint8_t
	{
		kBuilt,
		// Complete-borrow state is still healthy.  The caller may abandon this
		// frame without quarantining the main-scene-list module.
		kCoverageRejected,
		// The immutable original borrow or derived storage failed validation and
		// the module has fail-stopped.
		kBorrowRejected
	};

	struct DerivedReplayBuildResult
	{
		DerivedReplayBuildStatus status{ DerivedReplayBuildStatus::kBorrowRejected };
		MirrorPrivateLODStabilizationPolicy::CoverageStatus coverageStatus{
			MirrorPrivateLODStabilizationPolicy::CoverageStatus::kComplete };

		[[nodiscard]] explicit constexpr operator bool() const noexcept
		{
			return status == DerivedReplayBuildStatus::kBuilt;
		}
	};

	struct DiagnosticsSnapshot
	{
		std::uint64_t hookCalls{ 0 };
		std::uint64_t begins{ 0 };
		std::uint64_t acquires{ 0 };
		std::uint64_t unused{ 0 };
		std::uint64_t nested{ 0 };
		std::uint64_t wrongThread{ 0 };
		std::uint64_t generationRejects{ 0 };
		std::uint64_t headerRejects{ 0 };
		std::uint64_t sehFaults{ 0 };
		std::uint64_t faults{ 0 };
		std::uint64_t regularLists{ 0 };
		std::uint64_t portalLists{ 0 };
		std::uint64_t inlineLists{ 0 };
		std::uint64_t entries{ 0 };
		std::uint64_t culls{ 0 };
		std::uint64_t returns{ 0 };
		std::uint64_t fallbackSkips{ 0 };
		std::uint64_t accumulator178Applies{ 0 };
		std::uint64_t accumulator178Restores{ 0 };
		std::uint64_t derivedBuilds{ 0 };
		std::uint64_t derivedCoverageRejects{ 0 };
		std::uint64_t derivedBorrowRejects{ 0 };
		std::uint64_t derivedNonEmptyLists{ 0 };
		std::uint64_t derivedEntries{ 0 };
		std::uint64_t derivedFilteredEntries{ 0 };
		std::uint64_t derivedCompletions{ 0 };
		std::uint64_t derivedDiscards{ 0 };
	};

	void SetRequested(bool a_requested) noexcept;
	[[nodiscard]] bool Requested() noexcept;
	[[nodiscard]] bool InstallHook(CaptureCallback a_callback) noexcept;
	[[nodiscard]] bool HookInstalled() noexcept;
	void SetEnabled(bool a_enabled) noexcept;
	[[nodiscard]] bool Enabled() noexcept;
	[[nodiscard]] bool Active() noexcept;
	/** Returns only after EndBorrow released every retained scene-list value. */
	[[nodiscard]] bool RunDeferredCapture() noexcept;
	[[nodiscard]] bool Faulted() noexcept;
	[[nodiscard]] bool Acquire(BorrowView& a_view) noexcept;

	/** Revalidate the immutable lists after a clean private capture, before the
	 * next mirror in the same bounded frame batch. Never releases the borrow.
	 */
	[[nodiscard]] bool RearmForNextMirrorCapture(std::size_t frameBudget) noexcept;
	[[nodiscard]] DerivedReplayBuildResult BuildDerivedReplay(
		const BorrowView& a_original,
		const MirrorPrivateLODStabilizationPolicy::SupplementalCoverageEvidence&
			a_coverage,
		DerivedReplayView& a_derived) noexcept;
	[[nodiscard]] MirrorMainSceneListBorrowPolicy::PlayerCoverage
		ClassifyPlayerCoverage(const BorrowView& a_view, RE::NiAVObject* a_playerRoot) noexcept;
	[[nodiscard]] bool CompleteReplay(
		const BorrowView& a_view, bool a_finished, bool a_accumulator178Restored,
		std::size_t a_listCulls, std::size_t a_listReturns) noexcept;
	[[nodiscard]] bool CompleteDerivedReplay(
		const BorrowView& a_original, const DerivedReplayView& a_derived,
		bool a_finished, bool a_accumulator178Restored,
		std::size_t a_listCulls, std::size_t a_listReturns) noexcept;
	[[nodiscard]] bool DiscardDerivedReplay(
		const BorrowView& a_original, const DerivedReplayView& a_derived) noexcept;
	[[nodiscard]] bool AbandonReplay(const BorrowView& a_view) noexcept;
	[[nodiscard]] bool PublicationReady() noexcept;
	void FailCurrentBorrow() noexcept;
	void RecordFallbackSkip() noexcept;
	void RecordCull(bool a_returned) noexcept;
	void RecordAccumulator178Apply() noexcept;
	void RecordAccumulator178Restore() noexcept;
	[[nodiscard]] DiagnosticsSnapshot Diagnostics() noexcept;
}
