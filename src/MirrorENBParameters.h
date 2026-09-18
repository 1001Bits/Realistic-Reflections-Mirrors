#pragma once

#include <cstdint>

/**
 * ENB's own published settings, through its exported SDK.
 *
 * ENB is closed source, and this project's standing rule is not to claim
 * compatibility with behaviour we cannot verify. Its SDK is the exception: ENB
 * exports `ENBGetSDKVersion`, `ENBGetVersion`, `ENBGetParameter` and
 * `ENBSetParameter` deliberately, for mods to use.
 *
 * Owner, 2026-09-16: ENB's water in a reflection is flat blue and its
 * screen-space reflection shows the main view, because ENB replaces the water
 * shader and its screen-space sources belong to the main camera. The
 * requirement is "turn SSR and ENB water off without changing the main view",
 * so the suppression is scoped to a private capture and undone before the frame
 * ends -- at frame boundaries every value is exactly what the user set.
 *
 * Self-configuring: at start-up the candidate keys are probed and only the ones
 * this ENB build actually publishes are kept. An ENB without the SDK, or
 * without those keys, leaves everything untouched.
 */
namespace MirrorENBParameters
{
/** Resolve the ENB SDK once. False when ENB is absent or exports no SDK. */
[[nodiscard]] bool Available() noexcept;

/** ENB's own version numbers, zero when unavailable. */
[[nodiscard]] std::uint32_t SDKVersion() noexcept;
[[nodiscard]] std::uint32_t Version() noexcept;

/**
 * Probe the water/reflection keys once, log what exists, and keep the live ones
 * for capture suppression. Safe on any runtime; a no-op without ENB.
 */
void PrepareCaptureSuppression() noexcept;

/**
 * Zero ENB's water and reflection keys for the duration of one private capture.
 * Returns true when at least one key was changed, in which case
 * EndCaptureSuppression() must run before the frame ends.
 */
[[nodiscard]] bool BeginCaptureSuppression() noexcept;

/** Restore every value taken by BeginCaptureSuppression(). Always safe. */
void EndCaptureSuppression() noexcept;

struct Diagnostics
{
	std::uint32_t sdkVersion{ 0 };
	std::uint32_t version{ 0 };
	std::uint32_t liveKeys{ 0 };
	std::uint64_t suppressions{ 0 };
	std::uint64_t restores{ 0 };
	std::uint64_t failures{ 0 };
	bool available{ false };
};
[[nodiscard]] Diagnostics ReadDiagnostics() noexcept;
}
