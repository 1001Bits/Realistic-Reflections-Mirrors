#pragma once

#include "EngineDeviceIdentityPolicy.h"

#include <cstdint>

/**
 * Process-wide runtime facts behind EngineDeviceIdentityPolicy: whether a
 * forwarding (wrapper) device may be accepted at all, and which real device the
 * engine's main targets were last proven to live on.
 */
namespace EngineDeviceIdentity
{
	/** The optional empty marker that refuses the forwarding-device shape. */
	inline constexpr const wchar_t* kRefuseMarker =
		L"RealisticReflections_ForwardingDeviceRefuse.enable";

	/**
	 * True only when a known d3d11 wrapper (ENB) is loaded and the refuse marker
	 * is absent.  Evaluated once on first use, which happens after peer plugins
	 * have loaded; safe from any thread afterwards.
	 */
	[[nodiscard]] bool ForwardingDeviceAccepted() noexcept;

	/** Record the real device behind an accepted forwarded main target. */
	void NoteResourceDevice(std::uintptr_t resourceDevice) noexcept;

	/** Identity for per-object ownership checks against `engineDevice`. */
	[[nodiscard]] EngineDeviceIdentityPolicy::EngineDeviceIdentity Current(
		std::uintptr_t engineDevice) noexcept;

	/** Emit the one-time acceptance line; repeated calls are no-ops. */
	void LogForwardedAcceptanceOnce(
		std::uintptr_t engineDevice, std::uintptr_t resourceDevice) noexcept;
}
