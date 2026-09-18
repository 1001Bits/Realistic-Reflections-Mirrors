#pragma once
#include "MirrorRenderDistance.h"
#include "MirrorShadowCasterSet.h"
#include <span>
namespace RE { class NiAVObject; class BSGeometry; }
namespace MirrorScenePruning
{
    bool Install(std::uintptr_t verifiedCull,std::span<const std::uint8_t> expected) noexcept;
    bool Enabled() noexcept;
    bool OwnsHook(std::uintptr_t target) noexcept;
    // Caller owns the values/pointers until End in its native-exception finally.
    bool Begin(MirrorRenderDistance::Scope distance,const MirrorShadowCasterSet* casters,
        RE::NiAVObject* sceneRoot) noexcept;
    void End() noexcept;
    // Terrain and LOD (land, LOD land/noise/blend, LOD objects) are never distance-pruned:
    // they are the cheap far scenery whose absence shows as holes/bad mountains.
    bool RetainsRegardlessOfDistance(RE::BSGeometry* geometry) noexcept;
    void Invalidate() noexcept;
    std::uint64_t Epoch() noexcept;
}
