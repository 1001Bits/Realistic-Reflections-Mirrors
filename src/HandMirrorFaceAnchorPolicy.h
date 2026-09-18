#pragma once

#include "HandMirrorPortraitClipPolicy.h"
#include <bit>

namespace HandMirrorFaceAnchorPolicy
{
    enum class Part { Other, Face, Eyes };
    struct Point { float x{}, y{}, z{}; };

    // Composition and clipping have different bounds. Long hair, beards and
    // helmet crests must remain in the clipping guard, but never aim the camera.
    struct Selection
    {
        HandMirrorPortraitClipPolicy::RetainedGeometryBounds face{}, eyes{};

        void Add(Part part, Point center, float radius) noexcept
        {
            if (part != Part::Face && part != Part::Eyes) return;
            // The plugin uses /fp:fast; reject invalid bounds by their bits.
            for (float value : {center.x,center.y,center.z,radius,
                    center.x-radius,center.y-radius,center.z-radius,
                    center.x+radius,center.y+radius,center.z+radius})
                if ((std::bit_cast<std::uint32_t>(value)&0x7F800000u)==0x7F800000u) return;
            auto& bounds = part == Part::Face ? face : eyes;
            (void)HandMirrorPortraitClipPolicy::AccumulateRetainedGeometrySphere(
                bounds, center.x, center.y, center.z, radius);
        }

        bool Resolve(Point& point) const noexcept
        {
            const auto& bounds = face.valid ? face : eyes;
            if (!bounds.valid || !bounds.geometryCount) return false;
            point = {
                bounds.minimumX * .5F + bounds.maximumX * .5F,
                bounds.minimumY * .5F + bounds.maximumY * .5F,
                bounds.minimumZ * .5F + bounds.maximumZ * .5F};
            return true;
        }
    };
}
