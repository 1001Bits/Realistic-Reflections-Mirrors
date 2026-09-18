#pragma once
#include "HandMirrorSettings.h"
#include "MirrorFleetPolicy.h"
#include "MirrorRenderDistance.h"
#include "MirrorShadowSettings.h"
#include "MirrorAdaptiveQuality.h"
#include <atomic>
#include <cstdint>

// One quality control instead of five, with the five still there for anyone who
// wants them. Owner, 2026-09-15: "remove dynamic resolution option from mcm and
// instead make a low - medium - high setting for the mirror quality ... Call it
// automatic quality setting and when ticked off people can set res, hz and
// shadows + distance themselves."
//
// These are the owner's chosen presets, not portable performance estimates.
// Historical run-36 windows mixed scenes/settings and cannot establish that
// resolution is free or predict a fixed High/Medium cost ratio. Resolution
// changes raster work and memory; cadence changes the number of scene renders.
// Compare matched, warmed scenes per renderer before changing these defaults.
namespace MirrorQualityPreset
{
enum class Level : std::uint32_t
{
    kLow = 0,
    kMedium = 1,
    kHigh = 2
};

struct Settings
{
    int resolution{};
    int refreshHz{};
    int objectDistance{};
    int shadowDistance{};
    bool shadows{};
    // Hand captures have their own caps, so quality must control both owners.
    // Its lowered pose is a thumbnail in the corner of the screen, so it drops
    // a step below the raised one at every level.
    int handRaisedResolution{};
    int handLoweredResolution{};
    int handRefreshHz{};
    int handLoweredRefreshHz{};
};

// Object reach is NOT, at any level. Owner, 2026-09-15, after a tester's report:
// "remove any far distance limitations that could cause issues with far away
// objects or LOD except for the shadow distance limit." The tester's log had the
// sphere rejecting 6.36 million nodes, 27.6% of the cull, which is town-scale
// scenery missing from the reflection. A quality level may not reintroduce that,
// so every level uses the prune-nothing reach and pays for quality in the two
// dials that do not delete the world: refresh rate and shadow reach.
inline constexpr Settings kLowSettings{
    1024, 30, MirrorRenderDistance::kMaximum, 1500, false, 1024, 512, 30, 15 };
inline constexpr Settings kMediumSettings{
    // Owner, 2026-09-17: Medium needs the visible smoothness of 60 Hz;
    // only the Low placed-mirror preset stays at 30 Hz.
    2048, 60, MirrorRenderDistance::kMaximum, 2500, true, 2048, 1024, 30, 20 };
// Owner, 2026-09-15: "high setting should be every frame capture." A refresh of
// 0 is the unthrottled path (Refresh(0) == 0, Interval(0) == 0), so every mirror
// re-captures on every frame it is visible.
//
// This is deliberately the expensive end. Run 36 measured the unthrottled case
// at 9.66 ms a frame against 9.83 ms of CPU per capture -- 98% of a capture,
// paid every frame, where 30 Hz near 90 FPS pays about a third. High is for
// headroom, not a linear step up from Medium.
inline constexpr Settings kHighSettings{
    4096, 0, MirrorRenderDistance::kMaximum, 2500, true, 4096, 2048, 0, 0 };

inline constexpr Settings For(Level level) noexcept
{
    switch (level) {
    case Level::kLow:
        return kLowSettings;
    case Level::kHigh:
        return kHighSettings;
    case Level::kMedium:
    default:
        return kMediumSettings;
    }
}

inline constexpr Level Clamp(std::uint32_t value) noexcept
{
    return value > static_cast<std::uint32_t>(Level::kHigh) ? Level::kMedium
                                                            : static_cast<Level>(value);
}

// Owner, 2026-09-16 (core MCM): one mutually exclusive selector. Manual keeps
// every individual value as the player set it; Preset applies a Low/Medium/High
// bundle; Automatic adapts resolution and cadence together (MirrorAdaptiveQuality), and leaves
// distances and shadows exactly as configured.
enum class Mode : std::uint32_t
{
    kManual = 0,
    kPreset = 1,
    kAutomatic = 2
};
inline constexpr Mode kDefaultMode = Mode::kAutomatic;
inline std::atomic<std::uint32_t> mode{ static_cast<std::uint32_t>(kDefaultMode) };
inline std::atomic<std::uint32_t> level{ static_cast<std::uint32_t>(Level::kMedium) };
inline std::atomic<int> targetFPS{ static_cast<int>(MirrorAdaptiveQuality::kDefaultTargetFPS) };

inline constexpr Mode ClampMode(int value) noexcept
{
    return value < 0 || value > static_cast<int>(Mode::kAutomatic) ? kDefaultMode
                                                                    : static_cast<Mode>(value);
}

[[nodiscard]] inline Mode CurrentMode() noexcept
{
    return ClampMode(static_cast<int>(mode.load(std::memory_order_relaxed)));
}

[[nodiscard]] inline Level CurrentLevel() noexcept
{
    return Clamp(level.load(std::memory_order_relaxed));
}

// Preset owns the individual values; this is what the combined MCM still calls
// "automatic quality" and what the legacy INI key bAutomaticQuality records.
[[nodiscard]] inline bool Automatic() noexcept
{
    return CurrentMode() == Mode::kPreset;
}

[[nodiscard]] inline bool Adaptive() noexcept
{
    return CurrentMode() == Mode::kAutomatic;
}

inline void SetAutomatic(bool preset) noexcept
{
    mode.store(static_cast<std::uint32_t>(preset ? Mode::kPreset : Mode::kManual),
        std::memory_order_relaxed);
}

// Push the current level into the individual settings. With automatic off this
// does nothing, so the manual values a player chose are never overwritten by a
// stale level sitting in the INI.
inline void Apply() noexcept
{
    if (!Automatic())
        return;
    const auto chosen = For(CurrentLevel());
    MirrorFleetPolicy::resolution.store(MirrorFleetPolicy::Resolution(chosen.resolution),
        std::memory_order_relaxed);
    MirrorFleetPolicy::refreshHz.store(MirrorFleetPolicy::Refresh(chosen.refreshHz),
        std::memory_order_relaxed);
    // The dynamic-resolution option the level replaces: a quality level that
    // then let the resolution drift would not be a level at all.
    MirrorFleetPolicy::dynamicResolution.store(false, std::memory_order_relaxed);
    MirrorShadowSettings::hand.store(chosen.shadows, std::memory_order_relaxed);
    MirrorShadowSettings::placed.store(chosen.shadows, std::memory_order_relaxed);
    (void)MirrorRenderDistance::Set(true, chosen.objectDistance);
    (void)MirrorRenderDistance::Set(false, chosen.objectDistance);
    (void)MirrorRenderDistance::SetShadow(true, chosen.shadowDistance);
    (void)MirrorRenderDistance::SetShadow(false, chosen.shadowDistance);
    (void)HandMirrorSettings::SetRaisedResolution(chosen.handRaisedResolution);
    (void)HandMirrorSettings::SetLoweredResolution(chosen.handLoweredResolution);
    MirrorFleetPolicy::handRefreshHz.store(MirrorFleetPolicy::Refresh(chosen.handRefreshHz),
        std::memory_order_relaxed);
    MirrorFleetPolicy::handLoweredRefreshHz.store(
        MirrorFleetPolicy::Refresh(chosen.handLoweredRefreshHz), std::memory_order_relaxed);
}
}
