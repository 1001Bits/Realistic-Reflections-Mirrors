#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Where a standing-mirror capture spends its time (owner, 2026-09-17: "Can F8
// split out ... the capture's CPU time into its parts"). Collected only while
// the F8 panel is open or the benchmark runs; otherwise every entry point
// returns after one atomic read.
//
//   capture     wall time of one capture on the render thread
//   cull        the engine's culls for that capture (both cycles)
//   draw        the engine's render calls for that capture, including the
//               per-draw work below
//   hooks       this mod's per-draw work inside those renders (light
//               selection, effect handling, face light, skips)
//   lights      the light selection part of `hooks`
//   depth       the Community Shaders depth pre-pass and shadow mask
//   roster      finding the nearby references to draw (core run 9)
//   other       capture - cull - draw - depth - roster: setup, target, mips, handover
namespace MirrorCaptureProfile
{
	enum class Part : std::uint8_t { kCull, kDraw, kHooks, kLights, kDepth, kRoster, kCount };

	inline std::atomic_bool active{ false };
	[[nodiscard]] inline bool Active() noexcept { return active.load(std::memory_order_relaxed); }

	// Microseconds from QueryPerformanceCounter ticks; 0 when unavailable.
	[[nodiscard]] std::uint64_t Ticks() noexcept;
	[[nodiscard]] std::uint64_t Microseconds(std::uint64_t ticks) noexcept;

	// Render thread. Part time is attributed to the capture in progress.
	void BeginCapture() noexcept;
	void AddTicks(Part part, std::uint64_t ticks) noexcept;
	void EndCapture(std::uint64_t microseconds) noexcept;
	void RecordPlayerDraw(bool confirmed) noexcept;
	[[nodiscard]] bool InCapture() noexcept;

	struct Summary
	{
		std::uint64_t captures{};
		std::uint64_t playerDrawFrames{}, playerMissingFrames{};
		double seconds{};
		double captureMs{}, captureP95Ms{}, captureP99Ms{}, captureMaxMs{};
		std::array<double, static_cast<std::size_t>(Part::kCount)> partMs{};
		double otherMs{};
		double gpuMs{};
		std::uint64_t gpuSamples{};
	};

	// Averages per capture since the last Reset (any thread).
	[[nodiscard]] Summary Read(std::uint64_t nowMicroseconds) noexcept;
	void Reset(std::uint64_t nowMicroseconds) noexcept;
	// GPU time of the standing phase, cumulative (MirrorFleetRuntime's timer).
	void SetGpuSource(bool (*source)(double& totalMs, std::uint64_t& samples) noexcept) noexcept;
}
