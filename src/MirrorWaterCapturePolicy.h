#pragma once
#include <cstdint>

namespace MirrorWaterCapturePolicy
{
    // Exact BSWaterShader::SetupTechnique contract, recorded in
    // docs/re/Water_Wave_Descriptor_Family.md. Every descriptor in 0..0x7ff
    // has paired stock VS/PS entries; flow/normal/colour variants stay intact.
    inline constexpr std::uint32_t kTechniqueBase=0x5C000071U;
    inline constexpr std::uint32_t kRefractions=1U<<3;
    inline constexpr std::uint32_t kDepth=1U<<4;
    inline constexpr std::uint32_t kForeignInputs=kRefractions|kDepth;

    [[nodiscard]] constexpr std::uint32_t Select(bool privateColour, bool water,
                                                std::uint32_t technique) noexcept
    {
        if (!privateColour || !water || technique<kTechniqueBase) return technique;
        const auto descriptor=technique-kTechniqueBase;
        // Special underwater/LOD/stencil techniques use distinct contracts.
        if (descriptor>0x7ffU) return technique;
        // There is no reflected refraction/depth publication for this camera.
        // The native no-refraction variant uses authored water colour, normals,
        // flow and environment reflections. Sampling the main view here paints
        // its pane/depth silhouette into the water (owner images 299/300).
        return kTechniqueBase+(descriptor&~kForeignInputs);
    }
}
