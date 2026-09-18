#pragma once
#include <atomic>

namespace MirrorShadowSettings
{
inline std::atomic_bool controls{false};
inline std::atomic_bool hand{true}, placed{true};
// Owner, 2026-09-15, on the reflected face in the hand mirror: "if i turn 180
// degrees my face is always shadowed ... Can we turn off the player shadow to
// see if that helps?" With this false the player is not retained as a caster in
// either private map, so the head stops shadowing its own face while everything
// the world casts onto the player is unchanged. The detail map still renders,
// so the Community Shaders mask route keeps both of its cascades.
inline std::atomic_bool playerShadow{true};
// Owner, 2026-09-15: "we don't need outdoor shadows when it is night or rainy
// overcast." The sun's own directional light already says so -- Skyrim dims its
// diffuse and fade through dusk, cloud and rain -- so one reading covers all
// three conditions without guessing at weather flags, and it tracks the
// transitions the engine itself blends. Below this luminance the two private
// maps are not generated at all: nothing they could contain would be visible.
//
// The default is deliberately timid. Nobody has yet seen what this value reads
// across a Skyrim day, so it is set low enough to skip only plainly dark
// conditions, and the observed luminance is logged so the next run can raise it
// on evidence rather than on my arithmetic.
inline std::atomic<float> minimumSunLuminance{0.15f};
// Snapshot once before a capture: its generation and receiver draws must agree
// even if a Papyrus setting changes concurrently.
inline bool EnabledFor(bool handMirror) noexcept
{
    return !controls.load(std::memory_order_relaxed) ||
        (handMirror?hand:placed).load(std::memory_order_relaxed);
}
}
