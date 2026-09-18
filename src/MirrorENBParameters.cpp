#include "PCH.h"

#include "MirrorENBParameters.h"

#include "PeerDetection.h"

#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace MirrorENBParameters
{
	namespace
	{
		// ENBSeriesSDK.h, published by the ENB author for mod use.
		struct ENBParameter
		{
			unsigned char data[16];
			std::uint32_t type;
			std::int32_t size;
		};

		using GetVersion = std::uint32_t(__stdcall*)();
		using GetParameter = bool(__stdcall*)(const char*, const char*, const char*,
			ENBParameter*);
		using SetParameter = bool(__stdcall*)(const char*, const char*, const char*,
			ENBParameter*);

		constexpr const char* kFile = "enbseries.ini";

		std::atomic_bool g_resolved{ false };
		std::atomic_bool g_available{ false };
		GetVersion g_sdkVersion{ nullptr };
		GetVersion g_version{ nullptr };
		GetParameter g_getParameter{ nullptr };
		SetParameter g_setParameter{ nullptr };

		std::atomic<std::uint64_t> g_suppressions{ 0 };
		std::atomic<std::uint64_t> g_restores{ 0 };
		std::atomic<std::uint64_t> g_failures{ 0 };

		struct LiveKey
		{
			const char* category{ nullptr };
			const char* key{ nullptr };
			ENBParameter original{};
		};
		// Written once during PrepareCaptureSuppression, read-only afterwards.
		std::vector<LiveKey> g_liveKeys{};
		// Only the render thread inside a capture touches this.
		thread_local bool g_suppressionHeld{ false };

		void Resolve() noexcept
		{
			if (g_resolved.exchange(true, std::memory_order_acq_rel))
				return;
			if (!PeerDetection::ENBPresent())
				return;
			const auto module = ::GetModuleHandleW(L"d3d11.dll");
			if (!module)
				return;
			g_sdkVersion = reinterpret_cast<GetVersion>(
				::GetProcAddress(module, "ENBGetSDKVersion"));
			g_version = reinterpret_cast<GetVersion>(
				::GetProcAddress(module, "ENBGetVersion"));
			g_getParameter = reinterpret_cast<GetParameter>(
				::GetProcAddress(module, "ENBGetParameter"));
			g_setParameter = reinterpret_cast<SetParameter>(
				::GetProcAddress(module, "ENBSetParameter"));
			g_available.store(g_sdkVersion && g_getParameter && g_setParameter,
				std::memory_order_release);
		}

		[[nodiscard]] __declspec(noinline) bool ReadSEH(
			const char* category, const char* key, ENBParameter& out) noexcept
		{
			__try {
				return g_getParameter(kFile, category, key, &out);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] __declspec(noinline) bool WriteSEH(
			const char* category, const char* key, ENBParameter& value) noexcept
		{
			__try {
				return g_setParameter(kFile, category, key, &value);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] std::uint32_t CallVersionSEH(GetVersion function) noexcept
		{
			if (!function)
				return 0;
			__try {
				return function();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return 0;
			}
		}

		// The value is a 16-byte union whose meaning depends on `type`. Only the
		// shapes ENB documents are decoded; anything else is reported raw so a
		// surprise is visible rather than silently mistranslated.
		[[nodiscard]] std::string Describe(const ENBParameter& parameter)
		{
			switch (parameter.type) {
			case 1: {
				float value{};
				std::memcpy(&value, parameter.data, sizeof(value));
				return std::format("float {:.4f}", value);
			}
			case 2: {
				std::int32_t value{};
				std::memcpy(&value, parameter.data, sizeof(value));
				return std::format("int {}", value);
			}
			case 3:
				return std::format("hex 0x{:02X}{:02X}{:02X}{:02X}",
					parameter.data[3], parameter.data[2], parameter.data[1],
					parameter.data[0]);
			case 4:
				return std::format("bool {}", parameter.data[0] ? "true" : "false");
			default:
				return std::format("type={} size={}", parameter.type, parameter.size);
			}
		}

		/** The same parameter with its value zeroed: false, 0, or 0.0. */
		[[nodiscard]] ENBParameter Zeroed(const ENBParameter& original) noexcept
		{
			ENBParameter off = original;
			std::memset(off.data, 0, sizeof(off.data));
			return off;
		}

		[[nodiscard]] bool AlreadyOff(const ENBParameter& parameter) noexcept
		{
			for (const auto byte : parameter.data) {
				if (byte != 0)
					return false;
			}
			return true;
		}
	}

	bool Available() noexcept
	{
		Resolve();
		return g_available.load(std::memory_order_acquire);
	}

	std::uint32_t SDKVersion() noexcept { Resolve(); return CallVersionSEH(g_sdkVersion); }
	std::uint32_t Version() noexcept { Resolve(); return CallVersionSEH(g_version); }

	void PrepareCaptureSuppression() noexcept
	{
		static std::atomic_bool prepared{ false };
		if (prepared.exchange(true, std::memory_order_acq_rel))
			return;
		if (!Available()) {
			if (PeerDetection::ENBPresent()) {
				logger::info(
					"[MOS][ENBParameters] ENB is present but exports no usable SDK; "
					"its water and screen-space reflection stay as ENB renders them");
			}
			return;
		}
		logger::info("[MOS][ENBParameters] ENB SDK {} (ENB version {})",
			SDKVersion(), Version());

		// Candidates drawn from the strings this build carries (EnableWater,
		// WATER_SELFREFLECTION, E_SSRQUALITY*). Names that do not exist report
		// absent, which is the point of probing rather than assuming.
		struct Candidate { const char* category; const char* key; };
		static constexpr std::array kCandidates{
			Candidate{ "WATER", "EnableWater" },
			Candidate{ "WATER", "Reflection" },
			Candidate{ "WATER", "EnableSelfReflection" },
			Candidate{ "WATER", "SelfReflection" },
			Candidate{ "WATER", "EnableReflection" },
			Candidate{ "WATER", "EnableDisplacement" },
			Candidate{ "WATER", "EnableCaustics" },
			Candidate{ "REFLECTION", "Enable" },
			Candidate{ "REFLECTION", "EnableReflection" },
			Candidate{ "SSR", "Enable" },
			Candidate{ "SSAO_SSIL", "EnableSSR" },
			Candidate{ "EFFECT", "EnableWater" },
			Candidate{ "EFFECT", "EnableReflection" },
		};
		try {
			g_liveKeys.reserve(kCandidates.size());
			for (const auto& candidate : kCandidates) {
				ENBParameter parameter{};
				if (!ReadSEH(candidate.category, candidate.key, parameter))
					continue;
				logger::info("[MOS][ENBParameters]   [{}] {} = {}",
					candidate.category, candidate.key, Describe(parameter));
				if (AlreadyOff(parameter))
					continue;  // nothing to suppress, nothing to restore
				g_liveKeys.push_back({ candidate.category, candidate.key, parameter });
			}
		} catch (...) {
			g_liveKeys.clear();
		}
		logger::info(
			"[MOS][ENBParameters] {} live water/reflection keys will be zeroed for the "
			"duration of each private capture and restored before the frame ends, so the "
			"main view keeps exactly the values the user set.",
			g_liveKeys.size());
	}

	bool BeginCaptureSuppression() noexcept
	{
		// Run 5 measured capture(suppressions/restores/failures)=0/0/293305: the
		// SDK reads that succeed at start-up fail on every call from the render
		// thread inside a capture. ENB evidently does not serve its parameter API
		// there, and a quarter of a million failed calls a session is pure cost.
		//
		// So this stops after a run of consecutive failures rather than retrying
		// forever. The correct fix for ENB's water is a different mechanism -- its
		// parameters are not reachable from where a capture runs -- and pretending
		// otherwise would just burn frame time.
		static std::atomic<std::uint32_t> consecutiveFailures{ 0 };
		constexpr std::uint32_t kGiveUpAfter = 64;
		if (g_suppressionHeld || g_liveKeys.empty() ||
			!g_available.load(std::memory_order_acquire) ||
			consecutiveFailures.load(std::memory_order_relaxed) >= kGiveUpAfter) {
			return false;
		}
		bool changed = false;
		for (auto& live : g_liveKeys) {
			// Re-read first: the user may have changed it in ENB's own UI since
			// start-up, and the value we restore must be the current one.
			ENBParameter current{};
			if (!ReadSEH(live.category, live.key, current)) {
				g_failures.fetch_add(1, std::memory_order_relaxed);
				continue;
			}
			if (AlreadyOff(current))
				continue;
			live.original = current;
			auto off = Zeroed(current);
			if (!WriteSEH(live.category, live.key, off)) {
				g_failures.fetch_add(1, std::memory_order_relaxed);
				continue;
			}
			changed = true;
		}
		if (changed) {
			g_suppressionHeld = true;
			consecutiveFailures.store(0, std::memory_order_relaxed);
			g_suppressions.fetch_add(1, std::memory_order_relaxed);
		} else if (consecutiveFailures.fetch_add(1, std::memory_order_relaxed) + 1 ==
			kGiveUpAfter) {
			logger::warn(
				"[MOS][ENBParameters] ENB's parameter API refused {} consecutive "
				"capture suppressions; standing down. ENB keeps its own water and "
				"screen-space reflection inside reflections.",
				kGiveUpAfter);
		}
		return changed;
	}

	void EndCaptureSuppression() noexcept
	{
		if (!g_suppressionHeld)
			return;
		g_suppressionHeld = false;
		for (auto& live : g_liveKeys) {
			if (AlreadyOff(live.original))
				continue;
			if (!WriteSEH(live.category, live.key, live.original))
				g_failures.fetch_add(1, std::memory_order_relaxed);
		}
		g_restores.fetch_add(1, std::memory_order_relaxed);
	}

	Diagnostics ReadDiagnostics() noexcept
	{
		return {
			.sdkVersion = SDKVersion(),
			.version = Version(),
			.liveKeys = static_cast<std::uint32_t>(g_liveKeys.size()),
			.suppressions = g_suppressions.load(std::memory_order_relaxed),
			.restores = g_restores.load(std::memory_order_relaxed),
			.failures = g_failures.load(std::memory_order_relaxed),
			.available = g_available.load(std::memory_order_acquire)
		};
	}
}
