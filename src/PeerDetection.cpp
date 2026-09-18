#include "PCH.h"

#include "PeerDetection.h"

#include <array>
#include <atomic>
#include <mutex>

#include <Psapi.h>

namespace PeerDetection
{
	namespace
	{
		// The ENB SDK entry point. ENB exports this from whichever module it
		// wrapped; it is the one identifier that is stable across versions and
		// across the file renames users perform. Matching "d3d11.dll" by name
		// alone is wrong - other wrappers (ReShade, DXVK front ends, upscaler
		// shims) claim the same name and export nothing of the sort.
		constexpr const char* kENBSDKExport = "ENBGetSDKVersion";
		// ENB's helper plugin, present in most installs but not all; used only as
		// a corroborating signal, never on its own.
		constexpr const wchar_t* kENBHelperModule = L"enbhelperse.dll";

		[[nodiscard]] bool ModuleExports(HMODULE a_module, const char* a_symbol) noexcept
		{
			if (!a_module || !a_symbol)
				return false;
			return GetProcAddress(a_module, a_symbol) != nullptr;
		}

		[[nodiscard]] bool AnyLoadedModuleExports(const char* a_symbol) noexcept
		{
			// Enumerate rather than guess a file name. A first pass with a fixed
			// buffer covers every realistic process; if the process somehow has
			// more modules than that we accept the partial answer rather than
			// allocate on a probe path.
			std::array<HMODULE, 1024> modules{};
			DWORD needed = 0;
			if (!EnumProcessModules(
					GetCurrentProcess(), modules.data(),
					static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
				return false;
			}
			const std::size_t count = std::min<std::size_t>(
				needed / sizeof(HMODULE), modules.size());
			for (std::size_t index = 0; index < count; ++index) {
				if (ModuleExports(modules[index], a_symbol))
					return true;
			}
			return false;
		}

		Peers g_peers{};
		std::once_flag g_detectOnce{};
		std::atomic<bool> g_logged{ false };
	}

	const Peers& Detect() noexcept
	{
		std::call_once(g_detectOnce, []() noexcept {
			g_peers.communityShaders =
				GetModuleHandleW(L"CommunityShaders.dll") != nullptr;
			g_peers.enb =
				ModuleExports(GetModuleHandleW(L"d3d11.dll"), kENBSDKExport) ||
				AnyLoadedModuleExports(kENBSDKExport) ||
				GetModuleHandleW(kENBHelperModule) != nullptr;
		});
		return g_peers;
	}

	bool ENBPresent() noexcept
	{
		return Detect().enb;
	}

	bool CommunityShadersPresent() noexcept
	{
		return Detect().communityShaders;
	}

	void LogOnce() noexcept
	{
		if (g_logged.exchange(true, std::memory_order_acq_rel))
			return;
		const Peers& peers = Detect();
		try {
			logger::info(
				"[RR][Peers] communityShaders={} enb={}",
				peers.communityShaders, peers.enb);
			if (peers.communityShaders) {
#if defined(MIRRORS_OF_SKYRIM_STANDALONE)
				logger::warn(
					"[MOS][Peers] Community Shaders is loaded. Mirrors of Skyrim installs "
					"after peer PostPostLoad hooks and chains outward without replacing them.");
#else
				logger::warn(
					"[RR][Peers] Community Shaders is loaded. We install at InputLoaded, after every peer's "
					"PostPostLoad, so our hooks chain outward through its rather than replacing them. "
					"One consumer-side caveat: its Water.hlsl scales the t3 cube term by saturate(distance/1024) "
					"and forces it to zero when HideSky is set, so the t3 water route contributes nothing in "
					"interiors and nearly nothing near the camera. That is the CS shader discarding our write, "
					"not a broken hook; the t10/t11 resolve path is the primary water route here.");
#endif
			}
			if (peers.enb) {
				logger::warn(
					"[RR][Peers] ENB is loaded (detected via the {} export). ENB is closed source, so unlike "
					"Community Shaders we cannot verify interaction from source - we can only say what we do "
					"not know. ENB also drives its own device/resource reset lifecycle, which our "
					"process-lifetime resource storage does not yet respond to; if reflections go black after "
					"an ENB reset, that is the known open issue, not a new defect.",
					kENBSDKExport);
			}
		} catch (...) {
		}
	}
}
