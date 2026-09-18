#pragma once

#include "HandMirrorPortraitClipPolicy.h"

namespace HandMirrorPortraitClipPlanePolicy
{
    struct Direction { float x{}, y{}, z{}; };

    // A stable selfie camera must not inherit the shield animation's clipping
    // tilt. Ordinary physical mirrors keep their authored normal unchanged.
    inline auto Select(HandMirrorPortraitClipPolicy::PhysicalWorldHalfSpaceInputs pane,
        Direction portraitForward, bool stablePortrait) noexcept
    {
        if (stablePortrait) {
            pane.normalX = portraitForward.x;
            pane.normalY = portraitForward.y;
            pane.normalZ = portraitForward.z;
        }
        return HandMirrorPortraitClipPolicy::SelectPhysicalWorldHalfSpace(pane);
    }
}
