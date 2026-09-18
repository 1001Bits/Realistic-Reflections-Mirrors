#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MirrorSunShadowShader
{
// Private D3D slots are admitted only when absent from the original shader.
inline constexpr unsigned kConstants = 13;
inline constexpr unsigned kDetailDepth = 124;
inline constexpr unsigned kDetailMatrices = 125;
inline constexpr unsigned kMoments = 126;
inline constexpr unsigned kMatrices = 127;
inline constexpr unsigned kSampler = 13;
struct Variant
{
	std::vector<std::uint8_t> bytecode;
	std::string reason;
	unsigned replacedReads{};
	bool usesPrivateSampler{true};
};
// Worker/offline only. Preserves the original material program and its alpha
// tests; replaces reads of DirLightColor with a per-pixel shadowed temporary.
// nativeLightingLayout is admitted by the BSLightingShader geometry hook only;
// stripped vanilla programs use its audited PS PerGeometry b2/c1 light colour.
[[nodiscard]] Variant Build(std::span<const std::uint8_t> original,
                           bool privateDepth = false, bool nativeLightingLayout = false);
// Native ENB shader only: validates its screen-mask/sun-selection sequence,
// substitutes private sunlight, neutralizes the foreign mask and retains color0.
// Worker/offline only; does not enable ENB shadow generation or runtime hooks.
[[nodiscard]] Variant BuildENB(std::span<const std::uint8_t> original);
[[nodiscard]] bool CanRenderDepthOnly(std::span<const std::uint8_t> original) noexcept;
struct CasterVariant
{
    std::vector<std::uint8_t> bytecode;
    std::string reason;
    unsigned originalInstructions{}, retainedInstructions{};
};
// Worker/offline only. Retains exact alpha/discard operations and dependencies;
// shaders requiring conditional alpha work or unsupported side effects keep
// their native program. The draw owner must reject alpha-to-coverage separately.
[[nodiscard]] CasterVariant BuildCaster(std::span<const std::uint8_t> original);
} // namespace MirrorSunShadowShader
