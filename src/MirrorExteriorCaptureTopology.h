#pragma once

#include <array>
#include <cstdint>

namespace MirrorExteriorCaptureTopology
{
	enum class Cycle : std::uint8_t
	{
		kAuxiliary,
		kPrimaryWorldPlayer
	};

	enum class AuxiliaryRoot : std::uint8_t
	{
		kSelectedLodLand,
		kSelectedLodObject,
		kLodTrees,
		kSky
	};

	enum class RootRelation : std::uint8_t
	{
		kIndependent,
		kSame,
		kDescendantOfWorld,
		kAncestorOfWorld,
		kMalformed
	};

	[[nodiscard]] constexpr bool ShouldSubmitAuxiliaryRoot(
		const RootRelation relation) noexcept
	{
		// Descendants are unsafe only when combined with WorldRoot in the same
		// accumulator cycle. The auxiliary cycle is isolated by construction, so
		// selected descendant roots are precisely the roots it must submit.
		return relation == RootRelation::kIndependent ||
			relation == RootRelation::kDescendantOfWorld;
	}

	[[nodiscard]] constexpr bool ShouldSubmitAfterPrimaryWorld(
		const RootRelation relation) noexcept
	{
		// A post-primary material/water cycle may add only an independent graph.
		// Same, descendant, and ancestor roots have already been traversed through
		// WorldRoot and would otherwise submit duplicate geometry.
		return relation == RootRelation::kIndependent;
	}

	struct Plan
	{
		std::array<Cycle, 2> cycles{};
		std::array<AuxiliaryRoot, 4> auxiliaryRoots{};
		std::uint8_t cycleCount{ 0 };
		std::uint8_t auxiliaryRootCount{ 0 };
		std::uint8_t colorClearCount{ 0 };
		std::uint8_t depthStencilClearCount{ 0 };
		std::uint8_t cameraOverrideArmCount{ 0 };
		std::uint8_t cameraOverrideEndCount{ 0 };
		std::uint8_t auxiliaryRefractionGroupClearCount{ 0 };
		std::uint8_t primaryRefractionGroupClearCount{ 0 };
		std::uint8_t primaryWorldCullCount{ 0 };
		std::uint8_t primaryPlayerCullCount{ 0 };
		bool auxiliaryColorPreservedForPrimary{ false };
		bool auxiliaryDepthPreservedForPrimary{ false };
	};

	[[nodiscard]] constexpr Plan Build(const bool hasSeparatePlayerRoot) noexcept
	{
		return {
			.cycles = { Cycle::kAuxiliary, Cycle::kPrimaryWorldPlayer },
			.auxiliaryRoots = { AuxiliaryRoot::kSelectedLodLand,
				AuxiliaryRoot::kSelectedLodObject, AuxiliaryRoot::kLodTrees,
				AuxiliaryRoot::kSky },
			.cycleCount = 2,
			.auxiliaryRootCount = 4,
			.colorClearCount = 1,
			.depthStencilClearCount = 2,
			.cameraOverrideArmCount = 2,
			.cameraOverrideEndCount = 2,
			.auxiliaryRefractionGroupClearCount = 1,
			.primaryRefractionGroupClearCount = 0,
			.primaryWorldCullCount = 1,
			.primaryPlayerCullCount = static_cast<std::uint8_t>(hasSeparatePlayerRoot),
			.auxiliaryColorPreservedForPrimary = true,
			.auxiliaryDepthPreservedForPrimary = false
		};
	}

	[[nodiscard]] constexpr bool IsValid(const Plan& plan) noexcept
	{
		return plan.cycleCount == 2 &&
			plan.cycles[0] == Cycle::kAuxiliary &&
			plan.cycles[1] == Cycle::kPrimaryWorldPlayer &&
			plan.auxiliaryRootCount == 4 &&
			plan.auxiliaryRoots[0] == AuxiliaryRoot::kSelectedLodLand &&
			plan.auxiliaryRoots[1] == AuxiliaryRoot::kSelectedLodObject &&
			plan.auxiliaryRoots[2] == AuxiliaryRoot::kLodTrees &&
			plan.auxiliaryRoots[3] == AuxiliaryRoot::kSky &&
			plan.colorClearCount == 1 &&
			plan.depthStencilClearCount == 2 &&
			plan.cameraOverrideArmCount == 2 &&
			plan.cameraOverrideEndCount == 2 &&
			plan.auxiliaryRefractionGroupClearCount == 1 &&
			plan.primaryRefractionGroupClearCount == 0 &&
			plan.primaryWorldCullCount == 1 &&
			plan.primaryPlayerCullCount <= 1 &&
			plan.auxiliaryColorPreservedForPrimary &&
			!plan.auxiliaryDepthPreservedForPrimary;
	}
}
