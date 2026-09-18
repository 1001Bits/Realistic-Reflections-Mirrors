#pragma once

struct ID3D11DeviceContext;

namespace MirrorShadowMapBypass
{
	/** Read the default-off marker and join the existing shared lighting hooks. */
	void OnDataLoaded();

	/** Revoke any incomplete per-draw state at a save/new-game transition. */
	void OnGameLoaded() noexcept;

	/** Called after native BSLightingShader::SetupGeometry returns. */
	void OnSetupGeometryReturned() noexcept;

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
