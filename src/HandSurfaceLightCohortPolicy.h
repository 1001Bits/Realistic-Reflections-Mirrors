#pragma once

#include <cstdint>
#include <limits>

namespace HandSurfaceLightCohortPolicy
{
	enum class CallFamily : std::uint8_t
	{
		kMaximum16,
		kMaximum5,
		kMaximum7
	};

	[[nodiscard]] constexpr std::int32_t ExpectedMaximum(
		const CallFamily family) noexcept
	{
		switch (family) {
		case CallFamily::kMaximum16:
			return 16;
		case CallFamily::kMaximum5:
			return 5;
		case CallFamily::kMaximum7:
			return 7;
		default:
			return 0;
		}
	}

	// The stable identity is the rendered surface property plus every selector
	// mode input and the exact caller family. Per-pass lightData and
	// ShadowSceneNode pointers are deliberately not identities: Skyrim may rebuild
	// those contexts for the reflected camera.
	struct StableKey
	{
		std::uintptr_t shaderProperty{ 0 };
		std::uint32_t firstPersonMask{ 0 };
		std::int32_t maximumCount{ 0 };
		CallFamily family{ CallFamily::kMaximum16 };
		bool addShadow{ false };
		bool hasUseShadowSun{ false };
		bool useShadowSunInput{ false };
		bool firstPerson{ false };

		[[nodiscard]] constexpr bool operator==(
			const StableKey&) const noexcept = default;
	};

	[[nodiscard]] constexpr StableKey MakeStableKey(
		const std::uintptr_t shaderProperty,
		const std::int32_t maximumCount,
		const CallFamily family,
		const bool addShadow,
		const bool hasUseShadowSun,
		const bool useShadowSunInput,
		const bool firstPerson,
		const std::uint32_t firstPersonMask) noexcept
	{
		const bool effectiveFirstPerson = addShadow && firstPerson;
		return {
			.shaderProperty = shaderProperty,
			.firstPersonMask = effectiveFirstPerson ? firstPersonMask : 0,
			.maximumCount = maximumCount,
			.family = family,
			.addShadow = addShadow,
			.hasUseShadowSun = hasUseShadowSun,
			.useShadowSunInput = useShadowSunInput,
			.firstPerson = effectiveFirstPerson
		};
	}

	[[nodiscard]] constexpr bool IsStructurallyValid(
		const StableKey& key) noexcept
	{
		return key.shaderProperty != 0 && key.maximumCount > 0 &&
			key.maximumCount == ExpectedMaximum(key.family) &&
			key.hasUseShadowSun;
	}

	enum class IncumbentAction : std::uint8_t
	{
		kEmitManaged,
		kEmitGrace,
		kRetire
	};

	struct IncumbentInputs
	{
		bool identityValid{ false };
		bool stillAffectsSurface{ false };
		bool managerResolved{ false };
		std::uint8_t consecutiveManagerMisses{ 0 };
		std::uint64_t lastManagerMissSource{ 0 };
		std::uint64_t sourceSequence{ 0 };
		std::uint8_t retirementSources{ 0 };
	};

	struct IncumbentDecision
	{
		IncumbentAction action{ IncumbentAction::kRetire };
		std::uint8_t consecutiveManagerMisses{ 0 };
		std::uint64_t lastManagerMissSource{ 0 };
	};

	[[nodiscard]] constexpr IncumbentDecision EvaluateIncumbent(
		const IncumbentInputs& inputs) noexcept
	{
		if (!inputs.identityValid || !inputs.stillAffectsSurface ||
			inputs.sourceSequence == 0 || inputs.retirementSources == 0) {
			return { IncumbentAction::kRetire };
		}
		if (inputs.managerResolved)
			return { IncumbentAction::kEmitManaged };

		auto misses = inputs.consecutiveManagerMisses;
		if (inputs.lastManagerMissSource != inputs.sourceSequence &&
			misses < (std::numeric_limits<std::uint8_t>::max)()) {
			++misses;
		}
		return {
			.action = misses >= inputs.retirementSources ?
				IncumbentAction::kRetire : IncumbentAction::kEmitGrace,
			.consecutiveManagerMisses = misses,
			.lastManagerMissSource = inputs.sourceSequence
		};
	}

	[[nodiscard]] constexpr bool IsDemandRecent(
		const std::uint64_t lastDemandSource,
		const std::uint64_t currentSource,
		const std::uint64_t tailSources) noexcept
	{
		return lastDemandSource != 0 && currentSource != 0 && tailSources != 0 &&
			currentSource >= lastDemandSource &&
			currentSource - lastDemandSource <= tailSources;
	}

	/**
	 * Select the main-view light-build generation visible to a selector call.
	 *
	 * Skyrim can execute CalculateActiveLightsForSurface on a child job rather
	 * than on the thread which owns DrawWorld_BuildSceneLists.  The owner TLS is
	 * therefore authoritative when present, while a child may borrow only the
	 * explicitly active, nonzero shared generation. The shared generation remains
	 * live only until the already-owned JobList::Finish fence seals it. A sealed
	 * build never leaks its last monotonically increasing generation into later
	 * draws.
	 */
	[[nodiscard]] constexpr std::uint64_t SelectMainBuildGeneration(
		const std::uint64_t ownerThreadGeneration,
		const bool sharedBuildActive,
		const std::uint64_t sharedBuildGeneration) noexcept
	{
		if (ownerThreadGeneration != 0)
			return ownerThreadGeneration;
		return sharedBuildActive ? sharedBuildGeneration : 0;
	}
}
