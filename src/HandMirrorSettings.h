#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace RE::BSScript
{
	class IVirtualMachine;
}

/**
 * Live, user-adjustable hand-mirror settings.
 *
 * The raised hand mirror samples a fixed portrait window out of the private
 * capture; `portraitZoom` scales that window (larger = tighter crop of the
 * player). The owner-selected default is 2x. The value is read every
 * capture and written from the SkyUI MCM
 * (through the MirrorsOfSkyrimNative Papyrus bridge). SE/AE reset to face
 * zoom for a fresh draw/equip or loaded game; VR retains its INI behavior,
 * so it is a relaxed atomic; consumers must clamp through `ClampPortraitZoom`.
 *
 * This header is intentionally free of engine dependencies so SecondView.cpp
 * can read the value in both plugin products; persistence lives in
 * HandMirrorSettings.cpp in both mirror-bearing plugin targets.
 */
namespace HandMirrorSettings
{
	// V153: the owner judged 2x (face only) the right default; 1x reads as too far.
	inline constexpr float kDefaultPortraitZoom = 2.0F;
	inline constexpr float kMinimumPortraitZoom = 1.0F;
	inline constexpr float kMaximumPortraitZoom = 3.0F;
	inline constexpr bool kDefaultWheelZoom = true;
	inline constexpr bool kDefaultReflectWhenLowered = true;
	inline constexpr int kDefaultRaisedResolution = 2048;
	inline constexpr int kDefaultLoweredResolution = 1024;
	inline constexpr int kMinimumResolution = 512;
	inline constexpr int kMaximumResolution = 4096;
	/**
	 * Menu sliders step 512..4096; hand captures are mip views of one allocation,
	 * so a value snaps to the nearest power of two (ties round up). Out-of-range
	 * or unparsable values keep the fallback.
	 */
	[[nodiscard]] constexpr int NormalizeResolution(int value, int fallback) noexcept
	{
		if (value < kMinimumResolution || value > kMaximumResolution)
			return fallback;
		int best = kMinimumResolution;
		for (int candidate = kMinimumResolution; candidate <= kMaximumResolution; candidate *= 2) {
			const int distance = value > candidate ? value - candidate : candidate - value;
			const int bestDistance = value > best ? value - best : best - value;
			if (distance <= bestDistance)
				best = candidate;
		}
		return best;
	}

	[[nodiscard]] inline float ClampPortraitZoom(const float a_zoom) noexcept
	{
		if (!std::isfinite(a_zoom))
			return kDefaultPortraitZoom;
		return (std::clamp)(a_zoom, kMinimumPortraitZoom, kMaximumPortraitZoom);
	}

	namespace Detail
	{
		inline std::atomic<float> g_portraitZoom{ kDefaultPortraitZoom };
		inline std::atomic_bool g_wheelZoom{ kDefaultWheelZoom };
		inline std::atomic_bool g_reflectWhenLowered{ kDefaultReflectWhenLowered };
		inline std::atomic<int> g_raisedResolution{ kDefaultRaisedResolution };
		inline std::atomic<int> g_loweredResolution{ kDefaultLoweredResolution };
	}

	[[nodiscard]] inline int RaisedResolution() noexcept
	{
		return Detail::g_raisedResolution.load(std::memory_order_relaxed);
	}
	[[nodiscard]] inline int LoweredResolution() noexcept
	{
		return Detail::g_loweredResolution.load(std::memory_order_relaxed);
	}
	inline int SetRaisedResolution(int value) noexcept
	{
		value = NormalizeResolution(value, kDefaultRaisedResolution);
		Detail::g_raisedResolution.store(value, std::memory_order_relaxed);
		return value;
	}
	inline int SetLoweredResolution(int value) noexcept
	{
		value = NormalizeResolution(value, kDefaultLoweredResolution);
		Detail::g_loweredResolution.store(value, std::memory_order_relaxed);
		return value;
	}

	[[nodiscard]] inline float PortraitZoom() noexcept
	{
		return ClampPortraitZoom(
			Detail::g_portraitZoom.load(std::memory_order_relaxed));
	}

	/** Returns the clamped value that was stored. Does not persist. */
	inline float SetPortraitZoom(const float a_zoom) noexcept
	{
		const float clamped = ClampPortraitZoom(a_zoom);
		Detail::g_portraitZoom.store(clamped, std::memory_order_relaxed);
		return clamped;
	}

	// Wheel/MCM changes last for the current draw. Raising/lowering and menus
	// do not reset them; a fresh equip, draw or loaded session starts at face zoom.
	inline void ResetForNewDraw() noexcept
	{
		(void)SetPortraitZoom(kDefaultPortraitZoom);
	}

	[[nodiscard]] inline bool WheelZoomEnabled() noexcept
	{
		return Detail::g_wheelZoom.load(std::memory_order_relaxed);
	}

	/** Changes input ownership immediately. Does not persist. */
	inline bool SetWheelZoomEnabled(const bool a_enabled) noexcept
	{
		Detail::g_wheelZoom.store(a_enabled, std::memory_order_relaxed);
		return a_enabled;
	}

	[[nodiscard]] inline bool ReflectWhenLowered() noexcept
	{
		return Detail::g_reflectWhenLowered.load(std::memory_order_relaxed);
	}
	inline bool SetReflectWhenLowered(bool enabled) noexcept
	{
		Detail::g_reflectWhenLowered.store(enabled, std::memory_order_relaxed);
		return enabled;
	}
	[[nodiscard]] inline bool ReflectInPose(bool raised) noexcept
	{
		return raised || ReflectWhenLowered();
	}

	/** INI persistence (Data\SKSE\Plugins\MirrorsOfSkyrim.ini). */
	void Load();
	bool Save();
	/** Prepend the optional wheel sink before vanilla player controls. */
	void OnInputLoaded();
	/** Register the Papyrus bridge used by the core MCM (`RealisticReflectionsMirrorsNative`). */
	bool RegisterCorePapyrus(RE::BSScript::IVirtualMachine* a_vm);
	/** Register the Papyrus bridge (`MirrorsOfSkyrimNative`), including the core natives. */
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
