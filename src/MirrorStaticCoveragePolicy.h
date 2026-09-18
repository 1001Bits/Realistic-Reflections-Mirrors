#pragma once

#include <cstddef>
#include <cstdint>

namespace MirrorStaticCoveragePolicy
{
	// Mirror colour and private sun captures submit WorldRoot and the player into one
	// native accumulator. The player must be excluded from WorldRoot first:
	// registering a cached BSRenderPass twice can cycle passGroupNext forever.
	struct PlayerCullInputs
	{
		bool mirrorComposition{}, supportedFlatRuntime{}, primaryWorld{}, cullsPrimary{};
		bool additionalPlayer{}, playerScope{}, includesExteriorRoots{};
	};
	enum class PlayerCull { kUnchanged, kExcludeThenCull, kReject };
	[[nodiscard]] constexpr PlayerCull SelectPlayerCull(PlayerCullInputs in) noexcept
	{
		if (!in.mirrorComposition || !in.cullsPrimary || !in.additionalPlayer)
			return PlayerCull::kUnchanged;
		return in.supportedFlatRuntime && in.primaryWorld && in.playerScope && !in.includesExteriorRoots ?
			PlayerCull::kExcludeThenCull : PlayerCull::kReject;
	}

	// The optional fleet path must not discard a complete scene merely because
	// it exceeds the old 1024-candidate / 1280-root scratch limits. Allocation
	// failures still reject this capture before any visibility mutation.
	template <class Container, class Candidate>
	[[nodiscard]] bool AppendSceneCandidate(Container& candidates,
		const Candidate& candidate, bool grow, std::size_t legacyLimit) noexcept
	{
		if (!grow && candidates.size() >= legacyLimit)
			return false;
		try {
			candidates.push_back(candidate);
			return true;
		} catch (...) {
			return false;
		}
	}

	template <class Rows, class Order>
	[[nodiscard]] bool EnsureSceneRosterStorage(Rows& rows, Order& order,
		std::size_t required) noexcept
	{
		if (required > rows.max_size() || required > order.max_size())
			return false;
		try {
			if (rows.size() < required)
				rows.resize(required);
			if (order.size() < required)
				order.resize(required);
			return true;
		} catch (...) {
			// A column may already have grown. Callers reject the capture and
			// keep the previously admitted count; all old values remain owned.
			return false;
		}
	}

	enum class Route : std::uint8_t
	{
		kDisabled,
		kActorsOnly,
		kBoundedNearReferenceFallback,
		kReflectedWorldRoot
	};

	enum class PlanRejection : std::uint8_t
	{
		kNone,
		kSupplementalCycleUnavailable,
		kWorldRootUnavailable
	};

	struct Inputs
	{
		bool supplementalCycleEnabled{ false };
		bool reflectedWorldRootRequested{ false };
		bool worldRootAvailable{ false };
		bool nearReferenceFallbackRequested{ false };
		/**
		 * Exteriors only. Indoors the engine's portal graph already bounds the
		 * main view's lists to the room the mirror is in, so a second traversal
		 * of the whole root buys nothing and costs a full cull per capture
		 * (owner, run 4: 32.1 ms per capture, frame at 42.8 ms). Interiors fall
		 * back to the bounded near-reference route, which is what they used
		 * before coverage existed.
		 */
		bool exteriorCell{ false };
	};

	struct Plan
	{
		Route route{ Route::kDisabled };
		PlanRejection rejection{ PlanRejection::kNone };
		bool collectNearReferences{ false };
		bool cullWorldRoot{ false };
		bool requireCompleteActorExclusion{ false };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return rejection == PlanRejection::kNone;
		}
	};

	/**
	 * Choose exactly one static-coverage route. A reflected WorldRoot traversal
	 * supersedes the bounded near-reference fallback; running both would duplicate
	 * every nearby static and retain the fallback's capacity/anchor instability.
	 */
	[[nodiscard]] constexpr Plan BuildPlan(const Inputs& inputs) noexcept
	{
		if (!inputs.supplementalCycleEnabled) {
			return {
				Route::kDisabled,
				(inputs.reflectedWorldRootRequested ||
					inputs.nearReferenceFallbackRequested) ?
					PlanRejection::kSupplementalCycleUnavailable :
					PlanRejection::kNone
			};
		}
		if (inputs.reflectedWorldRootRequested && inputs.exteriorCell) {
			if (!inputs.worldRootAvailable) {
				return { Route::kDisabled, PlanRejection::kWorldRootUnavailable };
			}
			return {
				Route::kReflectedWorldRoot,
				PlanRejection::kNone,
				false,
				true,
				true
			};
		}
		if (inputs.nearReferenceFallbackRequested) {
			return {
				Route::kBoundedNearReferenceFallback,
				PlanRejection::kNone,
				true,
				false,
				false
			};
		}
		return { Route::kActorsOnly, PlanRejection::kNone };
	}

	enum WorldRootFailure : std::uint8_t
	{
		kNoWorldRootFailure = 0,
		kActorEnumerationIncomplete = 1u << 0,
		kExclusionPreparationFailed = 1u << 1,
		kWorldRootCullDidNotReturn = 1u << 2,
		kActorExclusionRestoreFailed = 1u << 3
	};

	struct WorldRootEvidence
	{
		bool actorEnumerationComplete{ false };
		bool exclusionPrepared{ false };
		bool cullReturned{ false };
		bool exclusionRestored{ false };
	};

	[[nodiscard]] constexpr std::uint8_t FailureMask(
		const WorldRootEvidence& evidence) noexcept
	{
		std::uint8_t result = kNoWorldRootFailure;
		if (!evidence.actorEnumerationComplete)
			result |= kActorEnumerationIncomplete;
		if (!evidence.exclusionPrepared)
			result |= kExclusionPreparationFailed;
		if (!evidence.cullReturned)
			result |= kWorldRootCullDidNotReturn;
		if (!evidence.exclusionRestored)
			result |= kActorExclusionRestoreFailed;
		return result;
	}

	[[nodiscard]] constexpr bool WorldRootCompleted(
		const WorldRootEvidence& evidence) noexcept
	{
		return FailureMask(evidence) == kNoWorldRootFailure;
	}

	/** Number of sorted candidates deterministically left outside a full roster. */
	[[nodiscard]] constexpr std::size_t TailDrops(
		std::size_t nextCandidate,
		std::size_t candidateCount) noexcept
	{
		return nextCandidate < candidateCount ?
			candidateCount - nextCandidate : 0;
	}

	/** Complete-roster admission: partial surroundings are never publishable. */
	[[nodiscard]] constexpr bool CompleteRosterFits(
		std::size_t actorCount,
		std::size_t staticCandidateCount,
		std::size_t capacity) noexcept
	{
		return actorCount <= capacity &&
			staticCandidateCount <= capacity - actorCount;
	}

	struct UnifiedHandActorActivationInputs
	{
		bool exactEmptyMarkerRequested{ false };
		bool reflectiveHandRuntimeRequested{ false };
		bool handCaptureDependenciesReady{ false };
		bool supportedFlatRuntime{ false };
	};

	/**
	 * The reflected-actor correction mutates live actor visibility and performs
	 * additional native descriptor culls. Keep it dormant until one exact empty
	 * diagnostic marker and every already-proven hand dependency agree.
	 */
	[[nodiscard]] constexpr bool EnableUnifiedHandActors(
		const UnifiedHandActorActivationInputs& inputs) noexcept
	{
		return inputs.exactEmptyMarkerRequested &&
			inputs.reflectiveHandRuntimeRequested &&
			inputs.handCaptureDependenciesReady && inputs.supportedFlatRuntime;
	}

	struct UnifiedHandActorTopology
	{
		bool collectCompleteNonPlayerRoster{ true };
		bool excludeRosterDuringWorldCull{ true };
		bool restoreRosterBeforeActorCulls{ true };
		bool cullActorsIntoPrimaryAccumulator{ true };
		bool playerCullLast{ true };
		bool oneNativeWrapper{ true };
		bool separateSupplementalWrapper{ false };
	};

	inline constexpr UnifiedHandActorTopology kUnifiedHandActorTopology{};

	[[nodiscard]] constexpr bool UnifiedHandActorTopologyValid(
		const UnifiedHandActorTopology& topology) noexcept
	{
		return topology.collectCompleteNonPlayerRoster &&
			topology.excludeRosterDuringWorldCull &&
			topology.restoreRosterBeforeActorCulls &&
			topology.cullActorsIntoPrimaryAccumulator && topology.playerCullLast &&
			topology.oneNativeWrapper && !topology.separateSupplementalWrapper;
	}

	struct UnifiedHandActorRootAdmissionInputs
	{
		bool fadeNodeValidated{ false };
		bool independentOfPlayerRoot{ false };
		bool independentOfPriorActorRoots{ false };
	};

	/**
	 * A retained actor root may be submitted only once. Mounted or attached
	 * actors can expose nested roots, so pointer inequality alone is insufficient.
	 */
	[[nodiscard]] constexpr bool AdmitUnifiedHandActorRoot(
		const UnifiedHandActorRootAdmissionInputs& inputs) noexcept
	{
		return inputs.fadeNodeValidated && inputs.independentOfPlayerRoot &&
			inputs.independentOfPriorActorRoots;
	}
}
