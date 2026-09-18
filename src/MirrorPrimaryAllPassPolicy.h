#pragma once

#include <cstdint>

namespace MirrorPrimaryAllPassPolicy
{
	enum class Root : std::uint8_t
	{
		kWorldRoot,
		kRetainedPlayerRoot,
		kAuxiliary
	};

	struct Inputs
	{
		bool markerEnabled{ false };
		bool exactSE1597{ false };
		bool exactMirrorPrimaryExecutor{ false };
		bool mirrorTargetActive{ false };
		bool cullsPrimaryRoots{ false };
		Root root{ Root::kAuxiliary };
	};

	inline constexpr std::uint32_t kAllPass = 1;
	inline constexpr std::uint32_t kIgnoreMultiBounds = 3;

	[[nodiscard]] constexpr bool UseAllPass(const Inputs& a_inputs) noexcept
	{
		return a_inputs.markerEnabled && a_inputs.exactSE1597 &&
		       a_inputs.exactMirrorPrimaryExecutor &&
		       a_inputs.mirrorTargetActive && a_inputs.cullsPrimaryRoots &&
		       a_inputs.root == Root::kWorldRoot;
	}

	[[nodiscard]] constexpr std::uint32_t SelectCullMode(
		const Inputs& a_inputs) noexcept
	{
		return UseAllPass(a_inputs) ? kAllPass : kIgnoreMultiBounds;
	}
}

// Diagnostic-only discriminator for comparing the exact-SE mirror WorldRoot
// primary native finish against Skyrim's pinned main-view flags. Keep the
// baseline literal here so every non-positive row remains the shipped flags=8
// path rather than inheriting a caller-specific default.
namespace MirrorPrimaryNativeFlagsPolicy
{
	using Root = MirrorPrimaryAllPassPolicy::Root;

	struct Inputs
	{
		bool markerEnabled{ false };
		bool exactSE1597{ false };
		bool exactMirrorPrimaryExecutor{ false };
		bool mirrorTargetActive{ false };
		bool cullsPrimaryRoots{ false };
		bool auxiliaryFirstActive{ false };
		Root root{ Root::kAuxiliary };
	};

	inline constexpr std::uint32_t kBaselineFlags = 8u;
	inline constexpr std::uint32_t kPinnedMainViewFlags = 0u;

	[[nodiscard]] constexpr bool UsePinnedMainViewFlags(const Inputs& a_inputs) noexcept
	{
		return a_inputs.markerEnabled &&
		       a_inputs.exactSE1597 &&
		       a_inputs.exactMirrorPrimaryExecutor &&
		       a_inputs.mirrorTargetActive &&
		       a_inputs.cullsPrimaryRoots &&
		       !a_inputs.auxiliaryFirstActive &&
		       a_inputs.root == Root::kWorldRoot;
	}

	[[nodiscard]] constexpr std::uint32_t SelectRenderFlags(const Inputs& a_inputs) noexcept
	{
		return UsePinnedMainViewFlags(a_inputs) ? kPinnedMainViewFlags : kBaselineFlags;
	}
}
