#pragma once

struct ID3D11DeviceContext;

namespace RE
{
	class BSRenderPass;
}

namespace MirrorShadowMapBypass
{
	/** Explicitly opt in to the exact AE 1.7.104 lighting comparison before preparation. */
	void ConfigureAE17104Comparison(bool requested) noexcept;

	/** Prepare the shared lighting hooks for the explicitly requested mirror roles. */
	void OnDataLoaded(bool wallMirrorRequested, bool handMirrorRequested);

	/** Enable prepared stabilization only for roles admitted by the caller's activation transaction. */
	void OnActivationCommitted(
		bool wallMirrorEnabled,
		bool handMirrorEnabled) noexcept;

	/** Revoke any incomplete per-draw state at a save/new-game transition. */
	void OnGameLoaded() noexcept;

	/** Called after native BSLightingShader::SetupGeometry returns. */
	void OnSetupGeometryReturned(const RE::BSRenderPass* pass = nullptr) noexcept;

	/** Called after native BSGraphics::SetDirtyStates commits graphics state. */
	void OnSetDirtyStatesCommitted(ID3D11DeviceContext* context) noexcept;

	/** Restore the exact pre-bypass t14 binding before native RestoreGeometry. */
	void OnBeforeRestoreGeometry() noexcept;

	/** Detect an incomplete restore after native RestoreGeometry. */
	void OnAfterRestoreGeometry() noexcept;

	/** Idempotent SEH-finally fallback; false retains the original t14 for retry. */
	[[nodiscard]] bool OnPrivateCaptureCleanup() noexcept;

	[[nodiscard]] bool IsEnabled() noexcept;
	void LogDiagnostics(const char* reason);
}
