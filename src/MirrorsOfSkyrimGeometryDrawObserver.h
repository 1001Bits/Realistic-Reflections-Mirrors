#pragma once

#include "MirrorsOfSkyrimGeometryDrawObserverOwner.h"

#include <cstdint>

namespace RE
{
	class NiAVObject;
}

struct ID3D11RenderTargetView;

namespace MirrorsOfSkyrimGeometryDrawObserver
{
	inline constexpr std::uint32_t kCallbackContractVersion = 1;

	struct Result
	{
		std::uint64_t drawCalls{ 0 };
		std::uint64_t drawReturns{ 0 };
		std::uint64_t eligibleGeometryCalls{ 0 };
		std::uint64_t eligibleGeometryReturns{ 0 };
		std::uint64_t invalidPassSnapshots{ 0 };
		std::uint64_t missingGeometrySnapshots{ 0 };
		std::uint64_t unsupportedGeometryTypes{ 0 };
		std::uint64_t snapshotFaults{ 0 };
		std::uint64_t targetMatchedReturns{ 0 };
		std::uint64_t targetMismatches{ 0 };
		std::uint64_t targetQueryFaults{ 0 };
		std::uint64_t selectedRootReturns{ 0 };
		std::uint64_t selectedRootSkinnedReturns{ 0 };
		std::uint64_t bodyRootReturns{ 0 };
		std::uint64_t bodyRootSkinnedReturns{ 0 };
		std::uint64_t playerReturns{ 0 };
		std::uint64_t skinnedPlayerReturns{ 0 };
		std::uint64_t unrelatedReturns{ 0 };
		std::uint64_t unclassifiedReturns{ 0 };
		std::uint64_t truncatedParentWalks{ 0 };
		std::uint64_t malformedParentWalks{ 0 };
		std::uint64_t classificationFaults{ 0 };
		bool rootsAliased{ false };
		bool lifetimeCleanupFault{ false };
		bool valid{ false };
	};

	struct Session
	{
		Result result{};
		std::uint64_t token{ 0 };
		std::uint32_t threadID{ 0 };
		bool active{ false };
	};

	struct RootOwnershipShape
	{
		std::uint8_t uniqueRootCount{ 0 };
		bool retainSelectedRoot{ false };
		bool retainBodyRoot{ false };
	};

	[[nodiscard]] constexpr RootOwnershipShape ClassifyRootOwnership(
		const bool selectedRootPresent,
		const bool bodyRootPresent,
		const bool rootsAliased) noexcept
	{
		const bool retainBody = bodyRootPresent &&
			(!selectedRootPresent || !rootsAliased);
		return {
			.uniqueRootCount = static_cast<std::uint8_t>(
				(selectedRootPresent ? 1 : 0) + (retainBody ? 1 : 0)),
			.retainSelectedRoot = selectedRootPresent,
			.retainBodyRoot = retainBody
		};
	}

	struct OwnedReferenceReleaseOutcome
	{
		bool targetReleased{ false };
		bool bodyRootReleased{ false };
		bool selectedRootReleased{ false };
	};

	[[nodiscard]] constexpr Result ApplyOwnedReferenceReleaseOutcome(
		Result result,
		const OwnedReferenceReleaseOutcome outcome) noexcept
	{
		if (!outcome.targetReleased || !outcome.bodyRootReleased ||
			!outcome.selectedRootReleased) {
			result.lifetimeCleanupFault = true;
			result.valid = false;
		}
		return result;
	}

	/**
	 * Begin one same-thread observation. Every unique nonnull root and the exact
	 * expected target are independently retained until a matching End call.
	 */
	[[nodiscard]] bool Begin(
		Session& session,
		RE::NiAVObject* selectedRoot,
		RE::NiAVObject* bodyRoot,
		ID3D11RenderTargetView* expectedTargetRTV) noexcept;

	[[nodiscard]] Result Snapshot(const Session& session) noexcept;
	[[nodiscard]] Result End(Session& session) noexcept;

	void LogDiagnostics(const char* reason);
}
