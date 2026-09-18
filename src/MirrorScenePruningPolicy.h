#pragma once
#include "MirrorRenderDistance.h"
#include "MirrorShadowCasterSet.h"
namespace MirrorScenePruning
{
    struct Filter
    {
        MirrorRenderDistance::Scope distance{};
        const MirrorShadowCasterSet* casters{};
        bool active{};
        bool Outside(MirrorRenderDistance::Point center,float radius) const noexcept
        {
            if (!active || !MirrorRenderDistance::Position(center) || !MirrorRenderDistance::Finite(radius) ||
                radius<=1 || radius>=1.e7f) return false;
            return casters ? !casters->Intersects({center.x,center.y,center.z},radius) :
                MirrorRenderDistance::Outside(distance,center,radius);
        }
    };
}
