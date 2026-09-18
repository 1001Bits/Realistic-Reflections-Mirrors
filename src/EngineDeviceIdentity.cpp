#include "PCH.h"

#include "EngineDeviceIdentity.h"
#include "PeerDetection.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace EngineDeviceIdentity
{
	namespace
	{
		std::once_flag g_acceptanceOnce{};
		bool g_accepted{ false };
		std::atomic<std::uintptr_t> g_resourceDevice{ 0 };
		std::atomic<bool> g_logged{ false };

		[[nodiscard]] bool RefuseMarkerPresent() noexcept
		{
			try {
				std::array<wchar_t, MAX_PATH> executablePath{};
				const DWORD length = GetModuleFileNameW(
					nullptr, executablePath.data(),
					static_cast<DWORD>(executablePath.size()));
				std::filesystem::path root = L"Data";
				if (length != 0 &&
					length < static_cast<DWORD>(executablePath.size())) {
					root = std::filesystem::path{ executablePath.data() }
						.parent_path() / L"Data";
				}
				std::error_code error{};
				const bool present =
					std::filesystem::exists(root / kRefuseMarker, error);
				// An unreadable Data folder refuses the forwarding shape; the
				// strict exact-device rule then applies as before.
				return error ? true : present;
			} catch (...) {
				return true;
			}
		}
	}

	bool ForwardingDeviceAccepted() noexcept
	{
		std::call_once(g_acceptanceOnce, []() noexcept {
			const bool enb = PeerDetection::ENBPresent();
			const bool refused = RefuseMarkerPresent();
			g_accepted = enb && !refused;
			logger::info(
				"[RR][EngineDevice] forwarding device acceptance: enb={} refuseMarker={} accepted={}",
				enb, refused, g_accepted);
		});
		return g_accepted;
	}

	void NoteResourceDevice(const std::uintptr_t resourceDevice) noexcept
	{
		g_resourceDevice.store(resourceDevice, std::memory_order_release);
	}

	EngineDeviceIdentityPolicy::EngineDeviceIdentity Current(
		const std::uintptr_t engineDevice) noexcept
	{
		return {
			.engineDevice = engineDevice,
			.resourceDevice = ForwardingDeviceAccepted() ?
				g_resourceDevice.load(std::memory_order_acquire) :
				0
		};
	}

	void LogForwardedAcceptanceOnce(
		const std::uintptr_t engineDevice,
		const std::uintptr_t resourceDevice) noexcept
	{
		if (g_logged.exchange(true, std::memory_order_acq_rel))
			return;
		logger::info(
			"[RR][EngineDevice] main target retained through a forwarding device: engine={:#x} resource={:#x}",
			engineDevice, resourceDevice);
	}
}
