#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>

namespace MirrorRenderDistance
{
// 20000 lets iRenderDistance and the MCM slider reach whole-town scenery
// (owner: 5000 helped but was the ceiling, 2026-09-14).
inline constexpr int kMinimum = 500, kMaximum = 20000;
// Owner 2026-09-15, after a tester's LOD report: "remove any far distance
// limitations that could cause issues with far away objects or LOD except for
// the shadow distance limit."
//
// Both mirrors therefore start at the prune-nothing end. 5000 was not a small
// cut: the tester's log shows the sphere rejecting a quarter of everything the
// cull walked --
//
//   [MOS][ScenePruning] visited=22994638 distanceRejected=6357384
//
// 6.36 million nodes, 27.6%, which is whole-town scenery missing from the
// reflection while the same scenery stands on screen behind the player.
// Terrain and LOD materials were already exempt (RetainsRegardlessOfDistance,
// 2026-09-14), so what this still dropped is the full-model statics beyond the
// sphere -- the buildings and rocks that sit between the terrain and the LOD
// rings, which is exactly what "LOD disappearing" looks like from inside.
//
// The slider stays, because a weak machine may still want it, and 20000 reaches
// past the loaded cell grid, so this default prunes nothing. Shadow reach is a
// separate control and keeps its own, much tighter, default.
inline constexpr int kHandDefault = kMaximum, kPlacedDefault = kMaximum;
inline constexpr int Clamp(int value) noexcept { return (std::clamp)(value, kMinimum, kMaximum); }
inline std::atomic_bool controls{false};
inline std::atomic<int> hand{kHandDefault}, placed{kPlacedDefault};
inline int Get(bool handMirror) noexcept
{
    return (handMirror ? hand : placed).load(std::memory_order_relaxed);
}
inline int Set(bool handMirror, int value) noexcept
{
    value = Clamp(value);
    (handMirror ? hand : placed).store(value, std::memory_order_relaxed);
    return value;
}
// Snapshot once per capture; zero retains the legacy path on other runtimes.
//
// A reach at the maximum reports zero as well, which is not a shortcut but the
// truthful answer: 20000 already reaches past the loaded cell grid, so the test
// can only ever return "keep". Reporting zero skips the per-node distance test
// instead of running it several million times a capture to decide nothing --
// the tester's log walked 22,994,638 nodes -- and it makes the prune-nothing
// setting genuinely free rather than merely harmless.
inline int ForCapture(bool handMirror) noexcept
{
    if (!controls.load(std::memory_order_relaxed)) return 0;
    const auto reach = Get(handMirror);
    return reach >= kMaximum ? 0 : reach;
}

// --- shadow reach, separate from object reach -------------------------------
//
// One distance drove both, and run 36 measured what that costs. At 4096 and
// 30 Hz, one standing mirror:
//
//            distance 2500   distance max
//   no sun     2.07 ms         3.59 ms
//   sun        2.70 ms         5.11 ms
//
// Shadows cost 0.63 ms at 2500 and 1.52 ms at max -- yet the shadowed *area* is
// identical, because the receiver footprint was pinned well below either value.
// The extra 0.89 ms is caster traversal widened by a slider that cannot move a
// single visible shadow. Owner, 2026-09-15: "can we make separate sliders for
// shadow distance and object draw distance, standard object draw should be 5000
// and standard shadow draw 2500."
//
// The shadow slider now sets the receiver footprint directly and bounds the
// caster scope, so raising object reach no longer drags the shadow cost up with
// it.
inline constexpr int kShadowMinimum = 500;
inline constexpr int kShadowMaximum = 6000;
inline constexpr int kShadowDefault = 2500;

// The map is a fixed grid, so the footprint it covers is what divides it and
// units-per-texel is the real quality invariant: 2*(radius + guard) over the
// map's own resolution. 1500 over a 2048 map -- 2.46 units per texel -- is the
// setting verified good; 5.9 and above is the setting that lost canopy and
// branch shadows entirely and made what survived pop in and out as the
// footprint recentred (run 27). 3.5 is the ceiling between them, and it is what
// makes the owner's 2500 legal at 2048 (3.44) while keeping the old runaway
// values out. Want more reach at the same quality: raise iShadowResolution.
inline constexpr float kMaximumUnitsPerTexel = 3.5f;
[[nodiscard]] inline int ShadowCeiling(unsigned mapResolution, float guard) noexcept
{
    const float ceiling = .5f * kMaximumUnitsPerTexel * static_cast<float>(mapResolution) - guard;
    if (!(ceiling > static_cast<float>(kShadowMinimum))) return kShadowMinimum;
    return (std::min)(kShadowMaximum, static_cast<int>(ceiling));
}
inline constexpr int ClampShadow(int value) noexcept
{
    return (std::clamp)(value, kShadowMinimum, kShadowMaximum);
}
inline std::atomic<int> handShadow{kShadowDefault}, placedShadow{kShadowDefault};
inline int GetShadow(bool handMirror) noexcept
{
    return (handMirror ? handShadow : placedShadow).load(std::memory_order_relaxed);
}
inline int SetShadow(bool handMirror, int value) noexcept
{
    value = ClampShadow(value);
    (handMirror ? handShadow : placedShadow).store(value, std::memory_order_relaxed);
    return value;
}
// Unlike the object reach this is never zero: the shadow footprint always has a
// size, whether or not the distance controls are exposed.
inline int ShadowForCapture(bool handMirror) noexcept { return GetShadow(handMirror); }
struct Point { float x{}, y{}, z{}; };
struct Scope { Point origin{}; int distance{}; };
inline bool Finite(float value) noexcept
{
    // The renderer builds with /fp:fast. NaN/Inf checks must survive it.
    return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
}
inline bool Position(Point p) noexcept
{
    return Finite(p.x) && Finite(p.y) && Finite(p.z) &&
        p.x > -1.0e7f && p.x < 1.0e7f && p.y > -1.0e7f && p.y < 1.0e7f &&
        p.z > -1.0e7f && p.z < 1.0e7f;
}
// Conservative whole-geometry culling: keep any bound touching the distance
// sphere, including a large terrain tile whose centre is farther away. Invalid
// bounds retain the native draw instead of hiding a possibly visible actor.
inline bool Outside(const Scope& scope, Point center, float radius) noexcept
{
    if (scope.distance < kMinimum || scope.distance > kMaximum ||
        !Position(scope.origin) || !Position(center) || !Finite(radius) ||
        radius < 0.0f || radius >= 1.0e7f) return false;
    const float x = center.x - scope.origin.x, y = center.y - scope.origin.y,
        z = center.z - scope.origin.z, reach = scope.distance + radius;
    return x*x + y*y + z*z > reach*reach;
}
}
