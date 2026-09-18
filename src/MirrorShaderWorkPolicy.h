#pragma once

namespace MirrorShaderWorkPolicy
{
    // CS always uses the native program plus our resource leases. Receiver
    // variants are never bound on that route. Caster classification/alpha-test
    // variants remain necessary for private shadow maps (including the add-on).
    [[nodiscard]] constexpr bool BuildReceiver(bool communityShaders, bool nativeENB) noexcept
    {
        return nativeENB || !communityShaders;
    }
}
