#pragma once

#include <bit>
#include <cstdint>

// Cast-shadow selection only. Player shadow reception keeps the existing
// detailed environmental map; reflected colour geometry is never filtered.
namespace MirrorShadowCasterPolicy
{
enum class Kind : std::uint8_t { Unknown, Static, Door, Terrain, Vegetation, Actor, Other };
struct Caster
{
    Kind kind{Kind::Unknown};
    float geometryRadius{}, objectRadius{};
    bool skinned{}, cutout{};
};
inline constexpr float kMinimumSceneryRadius = 192.0f;
inline bool Large(float radius) noexcept
{
    return (std::bit_cast<std::uint32_t>(radius) & 0x7f800000u) != 0x7f800000u &&
        radius >= kMinimumSceneryRadius;
}
inline bool Allow(const Caster& caster, bool playerDetail) noexcept
{
    if (caster.kind == Kind::Actor ||
        (caster.kind == Kind::Unknown && caster.skinned)) return false;
    // Trees, props and other environmental occluders must still shade the
    // player, even when they do not cast shadows across the reflected scenery.
    if (playerDetail) return true;
    switch (caster.kind) {
    case Kind::Terrain: return true;
    case Kind::Static:
    case Kind::Door:
        // Use the complete reference bound: small parts of a large building
        // must remain casters, or its windows and wall pieces leave holes.
        return Large(caster.objectRadius);
    case Kind::Unknown:
        // Loaded terrain/LOD may have no reference. Keep substantial opaque
        // pieces; reject unowned foliage billboards and small fragments.
        return !caster.cutout && Large(caster.geometryRadius);
    default: return false;
    }
}
} // namespace MirrorShadowCasterPolicy
