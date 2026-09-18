#pragma once

namespace AutonomousTestControl
{
	/**
	 * Inspect the default-off marker and the controller-provided session.
	 *
	 * When both are valid, installs process-local ClipCursor/SetCursorPos guards.
	 * Failure to install both guards leaves the autonomous protocol disabled.
	 */
	void Initialize() noexcept;

	/**
	 * Arm protocol pumping only when the verified RenderPlayerView driver hook is
	 * installed. An optional scenario.json bootstrapLoad.saveName is dispatched
	 * through BGSSaveLoadManager here; requests remain unavailable until the first
	 * resulting main-world return.
	 */
	void OnDataLoaded(bool a_mainRenderHookReady) noexcept;

	/** Pump after the signature-verified outer main-world render call returns. */
	void PumpMainThread() noexcept;

	/** True only after session validation and both cursor guards succeeded. */
	[[nodiscard]] bool IsEnabled() noexcept;
}
