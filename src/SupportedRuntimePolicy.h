#pragma once

#include "REL/Module.h"
#include "REL/Version.h"

/**
 * Exact runtimes the flat (non-VR) code paths are validated against.  Every
 * hook, raw offset and phase assumption in the plugin was measured on SE
 * 1.5.97 and AE 1.6.1170; Skyrim 1.7.104 (2026) and GOG AE 1.6.1179 share the
 * AE Address Library ID space and, as far as the plugin's own probes can tell,
 * the AE layouts, so they are admitted as AE runtimes here and verified by the
 * same runtime probes at load.  Hardcoded Steam 1.6.1170 RVAs must never be
 * applied to GOG 1.6.1179; those sites use IsExactAE161179Runtime().
 *
 * Skyrim VR 1.4.15 is the single VR runtime the port (2026-09-04) was derived
 * against in Ghidra; every VR hook still verifies its own byte window at
 * install time, so the exact version test here is a precondition, not proof.
 */
namespace SupportedRuntimePolicy
{
	inline constexpr REL::Version kAE161170{ 1, 6, 1170, 0 };
	inline constexpr REL::Version kAE161179{ 1, 6, 1179, 0 };
	inline constexpr REL::Version kAE17104{ 1, 7, 104, 0 };
	inline constexpr REL::Version kVR1415{ 1, 4, 15, 0 };

	[[nodiscard]] inline bool IsSupportedAEVersion(const REL::Version& a_version) noexcept
	{
		return a_version == kAE161170 || a_version == kAE161179 ||
			a_version == REL::Version{ 1, 6, 1179, 1 } ||
			a_version == kAE17104;
	}

	/**
	 * AE runtimes share one Address Library ID space, so `REL::ID` resolves on
	 * each of them, but hardcoded RVAs and a few call offsets inside
	 * Main::RenderPlayerView differ.  `REL::Relocate` reports a single AE slot
	 * and cannot tell them apart, so every such site branches on these
	 * exact-runtime predicates instead.
	 */
	[[nodiscard]] inline bool IsExactAE161170Runtime() noexcept
	{
		return REL::Module::IsAE() && REL::Module::get().version() == kAE161170;
	}

	[[nodiscard]] inline bool IsExactAE161179Runtime() noexcept
	{
		if (!REL::Module::IsAE())
			return false;
		const auto version = REL::Module::get().version();
		// PE is 1.6.1179.0; SKSE GOG packs RUNTIME_TYPE_GOG as 1.6.1179.1.
		return version == kAE161179 || version == REL::Version{ 1, 6, 1179, 1 };
	}

	[[nodiscard]] inline bool IsExactAE17104Runtime() noexcept
	{
		return REL::Module::IsAE() && REL::Module::get().version() == kAE17104;
	}

	[[nodiscard]] inline bool IsExactVRRuntime() noexcept
	{
		return REL::Module::IsVR() &&
		       REL::Module::get().version() == REL::Version{ 1, 4, 15, 0 };
	}
}
