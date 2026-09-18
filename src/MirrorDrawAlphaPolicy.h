#pragma once

#include <cstdint>
#include <type_traits>

namespace MirrorDrawAlphaPolicy
{
    inline constexpr std::uint32_t kLightingBase = 0x4800002D;
    inline constexpr std::uint32_t kAlphaTest = 1u << 20;

    // The batch's alpha-test argument and its cached lighting technique can
    // disagree. The same native defect is documented by Engine Fixes:
    // alandtse/EngineFixesSkyrim64, fixes/bslightingshader_force_alpha_test.h.
    //
    // Two reasons a family is excluded, and they are not the same reason.
    //
    // 1, 3, 11, 16 (EnvironmentMap, Parallax, MultilayerParallax, Eye) have no
    // compatible alpha-test variant at all, so the bit cannot be honoured.
    //
    // 8, 9, 13, 15, 18, 19 are terrain and LOD, and their alpha is a *blend
    // weight* -- land blending, LOD noise -- not a cutout mask.
    //
    // Snow (10, 14) was excluded here too on 2026-09-15 and is not any more: the
    // census added the same day measured `capture(snow/snowAlpha/terrain)` as
    // 0/0/3130902 over a whole run -- not one snow-family draw ever reaches a
    // capture, so the exclusion could not have been doing anything. The owner's
    // disappearing snow was the WorldRoot coverage, not this. Testing it erodes the surface into a screen-door pattern
    // wherever the weight falls below the reference, which is the black stipple
    // the owner reported on distant mountains (ScreenShot282, 2026-09-15). It
    // shows only in the reflection because only the reflection applies this
    // correction: the main view draws those passes with the native defect
    // intact, and for these families the defect is the better-looking answer.
    // (13 is LODObjects, which CommonLibSSE's Feature enum does not name.)
    //
    // Tree foliage (12) was the case this was added for, and on 2026-09-16 the
    // owner showed it was the case it got wrong: ScreenShot293 and 294 are the
    // same conifer, reflected and real, and the reflected one is skeletal --
    // thin wisps with the trunk showing through -- while the real one is dense.
    // The correction is the only thing that renders foliage differently inside a
    // capture, so it is the only thing that can be doing it.
    //
    // The wider point: this correction reproduces what the engine *meant*, and a
    // reflection is not judged against what the engine meant. It is judged
    // against the window next to it. Where the two disagree, matching the main
    // view wins, because a mirror that disagrees with the world is the defect no
    // matter which side is technically more correct.
    [[nodiscard]] constexpr std::uint32_t Select(std::uint32_t technique,
        bool privateColor, bool lightingShader, bool alphaTest) noexcept
    {
        if (!privateColor || !lightingShader || !alphaTest ||
            technique < kLightingBase || technique - kLightingBase >= 0x14000000u)
            return technique;
        const auto feature = (technique - kLightingBase) >> 24;
        switch (feature) {
        case 1: case 3: case 11: case 16:          // no alpha-test variant
        case 8: case 9: case 13: case 15:          // land and object LOD
        case 18: case 19:                          // LOD noise and land blend
        case 12:                                   // tree foliage
            return technique;
        default:
            return technique | kAlphaTest;
        }
    }

    // Only the synchronous call receives this copy. Neither cached passEnum nor
    // any engine-owned list/link is modified, including on a native exception.
    template<class Pass>
    [[nodiscard]] bool Prepare(const Pass& original, std::uint32_t technique,
        bool privateColor, bool lightingShader, bool alphaTest, Pass& copy) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Pass>);
        if (original.passEnum != technique) return false;
        const auto selected = Select(technique, privateColor, lightingShader, alphaTest);
        if (selected == technique) return false;
        copy = original;
        copy.passEnum = selected;
        return true;
    }
}
