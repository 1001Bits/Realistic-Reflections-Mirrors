#pragma once

#include <array>
#include <cstdint>
#include <span>

// Built-in benchmark (owner, 2026-09-17). Started from the MCM Development
// menu; runs MirrorPerformance::kBenchmarkSteps at the player's spot, one
// configuration per step, and restores every setting it touched afterwards.
// Nothing is saved to the INI.
namespace MirrorBenchmark
{
	using Line = std::array<char, 112>;

	void Request() noexcept;
	[[nodiscard]] bool Running() noexcept;
	[[nodiscard]] bool Showing() noexcept;
	// Once per real engine frame, including the mirrors-off baseline.
	void OnSourceFrame(std::uint64_t source, std::uint64_t now) noexcept;
	// Once per application Present. `eligible` is false while paused,
	// loading or unfocused; the schedule waits.
	void OnPresent(std::uint64_t nowMicroseconds, bool eligible) noexcept;
	void Abort(const char* reason) noexcept;
	void Dismiss() noexcept;
	// Overlay lines while running or after it finished.
	[[nodiscard]] std::size_t Lines(std::span<Line> out, std::uint64_t nowMicroseconds) noexcept;
}
