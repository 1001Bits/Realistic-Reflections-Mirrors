#pragma once

namespace MirrorCameraOverride
{
	/** True after the shared camera-upload hook is installed and healthy. */
	[[nodiscard]] bool HookReady() noexcept;
	/** Enable or disable only the equipped-mirror camera owner. */
	void SetHandMirrorRuntimeEnabled(bool enabled) noexcept;
	/** True while the equipped-mirror owner may arm a private-camera lease. */
	[[nodiscard]] bool HandMirrorRuntimeReady() noexcept;
}
