#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/**
 * Engine-independent ownership rules for the D3D11 device behind the engine's
 * main render targets.
 *
 * Skyrim stores the device it was created with in BSGraphics::Renderer.  A
 * d3d11.dll wrapper such as ENB hands the engine a forwarding device and
 * context and forwards creation calls to the real device.  Views and resources
 * created that way report the real device from ID3D11DeviceChild::GetDevice,
 * so a strict "every device equals the renderer device" rule rejects every
 * main target while such a wrapper is loaded (AE 1.7.104 with ENB 0.505,
 * 2026-09-09: every deferred observation failed at the retain step, none at
 * raster/source/origin, while every wrapper-free run retained on every frame).
 *
 * The forwarding shape is accepted only when the caller explicitly allows it
 * (a detected wrapper and no refuse marker) and only when every target-side
 * object agrees on one real device.  Mixed devices are still rejected.
 */
namespace EngineDeviceIdentityPolicy
{
	struct MainTargetDeviceOwnership
	{
		/** The renderer's own device pointer token. */
		std::uintptr_t engineDevice{ 0 };
		/** ID3D11DeviceContext::GetDevice of the renderer's immediate context. */
		std::uintptr_t contextDevice{ 0 };
		/** GetDevice of each observed view/resource, in any order. */
		std::array<std::uintptr_t, 4> targetDevices{};
		std::size_t targetDeviceCount{ 0 };
	};

	enum class MainTargetDeviceDisposition : std::uint8_t
	{
		kReject,
		/** Every object reports the renderer device itself. */
		kExact,
		/** Every target object reports one other device; the renderer device forwards. */
		kForwarded
	};

	[[nodiscard]] constexpr MainTargetDeviceDisposition
	ClassifyMainTargetDeviceOwnership(
		const MainTargetDeviceOwnership& ownership,
		const bool forwardingDeviceAccepted) noexcept
	{
		if (ownership.engineDevice == 0 || ownership.contextDevice == 0 ||
			ownership.targetDeviceCount == 0 ||
			ownership.targetDeviceCount > ownership.targetDevices.size()) {
			return MainTargetDeviceDisposition::kReject;
		}
		const std::uintptr_t native = ownership.targetDevices[0];
		for (std::size_t index = 0; index < ownership.targetDeviceCount; ++index) {
			const auto device = ownership.targetDevices[index];
			if (device == 0 || device != native)
				return MainTargetDeviceDisposition::kReject;
		}
		if (native == ownership.engineDevice) {
			return ownership.contextDevice == ownership.engineDevice ?
				MainTargetDeviceDisposition::kExact :
				MainTargetDeviceDisposition::kReject;
		}
		if (!forwardingDeviceAccepted)
			return MainTargetDeviceDisposition::kReject;
		// A forwarding context may report either the forwarding device or the
		// real one; both are the same ownership.
		return ownership.contextDevice == ownership.engineDevice ||
			ownership.contextDevice == native ?
			MainTargetDeviceDisposition::kForwarded :
			MainTargetDeviceDisposition::kReject;
	}

	/** The device that owns the target-side objects for an accepted shape, else 0. */
	[[nodiscard]] constexpr std::uintptr_t MainTargetResourceDevice(
		const MainTargetDeviceOwnership& ownership,
		const MainTargetDeviceDisposition disposition) noexcept
	{
		switch (disposition) {
		case MainTargetDeviceDisposition::kExact:
			return ownership.engineDevice;
		case MainTargetDeviceDisposition::kForwarded:
			return ownership.targetDeviceCount != 0 ? ownership.targetDevices[0] : 0;
		default:
			return 0;
		}
	}

	/**
	 * The two device tokens a later per-object check may accept: the renderer
	 * device and, once a forwarded main target has been retained, the real
	 * device behind it.  `resourceDevice` is 0 until that is known or when
	 * forwarding is not accepted.
	 */
	struct EngineDeviceIdentity
	{
		std::uintptr_t engineDevice{ 0 };
		std::uintptr_t resourceDevice{ 0 };
	};

	[[nodiscard]] constexpr bool OwnedByEngineDevice(
		const EngineDeviceIdentity& identity,
		const std::uintptr_t observed) noexcept
	{
		return observed != 0 && identity.engineDevice != 0 &&
			(observed == identity.engineDevice ||
				(identity.resourceDevice != 0 &&
					observed == identity.resourceDevice));
	}
}
