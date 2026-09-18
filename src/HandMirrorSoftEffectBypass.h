#pragma once

#include <cstdint>

struct ID3D11DeviceContext;

namespace HandMirrorSoftEffectBypass
{
	// Release the water screen-space lease if one is still held.
	//
	// The lease is applied after BSWaterShader::SetupGeometry and released in
	// RestoreGeometry, which the engine pairs per pass -- but a neutral texture
	// left bound at t0 would follow the capture out into the main view, and
	// capture-scoped state escaping a capture is the exact class of defect that
	// cost 2026-09-15 (the screen-size globals). So the private-pass cleanup
	// anchor closes it too, unconditionally and cheaply.
	void EndWaterSSRNeutralLease() noexcept;

	enum class CommitResult : std::uint8_t
	{
		kInactive,
		kApplied,
		kFailed
	};

	struct Diagnostics
	{
		std::uint64_t arms{ 0 };
		std::uint64_t commits{ 0 };
		std::uint64_t reapplies{ 0 };
		std::uint64_t restores{ 0 };
		std::uint64_t playerMatches{ 0 };
		std::uint64_t playerArms{ 0 };
		std::uint64_t playerCommits{ 0 };
		std::uint64_t playerReapplies{ 0 };
		std::uint64_t playerRestores{ 0 };
		std::uint64_t playerRejects{ 0 };
		std::uint64_t faults{ 0 };
		std::uint64_t alphaDrawCorrections{ 0 };
		std::uint64_t alphaDrawReadFaults{ 0 };
		/** Material families a private capture is handed (snow 10/14, LOD 8/9/13/15/18/19). */
		std::uint64_t captureSnowDraws{ 0 };
		std::uint64_t captureSnowAlphaTest{ 0 };
		std::uint64_t captureTerrainDraws{ 0 };
		std::uint64_t waterSSRNeutralBinds{ 0 };
		std::uint64_t waterSSRNeutralFailures{ 0 };
		std::uint64_t waterPrivateTechniqueRewrites{ 0 };
		// Effect (7) / Particle (10) shader passes seen by the dispatcher inside a
		// placed-mirror capture, and how many were skipped before the native draw.
		std::uint64_t placedEffectDrawsSeen{ 0 };
		std::uint64_t placedParticleDrawsSeen{ 0 };
		std::uint64_t placedEffectParticleSkipped{ 0 };
		std::uint64_t placedEffectPrivateDepth{ 0 };
		// Placed SoftEffect draws that fell back to the fade-less technique.
		std::uint64_t placedSoftDepthFallbacks{ 0 };
		std::uint64_t placedFirstPersonEffectSkips{ 0 };
		// Passes with no shader that reached the seam inside a private capture and were skipped.
		std::uint64_t nullShaderPassSkips{ 0 };
		bool alphaDrawEnabled{ false };
	};

	/**
	 * Install the exact SE 1.5.97 batch-dispatch argument hook from either the
	 * native entry or the fully pinned Engine Fixes SafetyHook chain; the SE
	 * standalone test opt-in also admits that same owner when CS is absent.
	 */
	[[nodiscard]] bool EnsureInstalled() noexcept;

	/**
	 * Resolve the already-loaded stock Community Shaders v1.8.4 module only
	 * after its exact release-file length and SHA-256 have both matched.  This
	 * shares the compatibility pin with the SoftEffect chain and exposes no CS
	 * C++ layout.
	 */
	[[nodiscard]] bool TryGetPinnedCommunityShaders184Module(
		std::uintptr_t& moduleBase) noexcept;

	[[nodiscard]] bool IsInstalled() noexcept;
	[[nodiscard]] bool Faulted() noexcept;
	/** Remove an already-applied private t3 before the writable target rebind. */
	[[nodiscard]] bool PrepareForWritableRebind(
		ID3D11DeviceContext* context) noexcept;

	/** Commit exact-hand SoftEffect private depth after native SetDirtyStates. */
	[[nodiscard]] CommitResult OnSetDirtyStatesCommitted(
		ID3D11DeviceContext* context) noexcept;

	[[nodiscard]] Diagnostics ReadDiagnostics() noexcept;
}
