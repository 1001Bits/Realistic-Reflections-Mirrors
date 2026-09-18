#pragma once

namespace MirrorSurfaceLights
{
void Install() noexcept;
bool Enabled() noexcept;
void LogDiagnostics() noexcept;
// Stack-owned draw arguments. The cached native pass and its light array are
// never changed or retained by this correction.
struct Draw
{
    RE::BSRenderPass pass{};
    // Sun plus up to seven locals (vanilla) or fifteen (Community Shaders
    // strict-light packer, whose cluster lights are neutralized in mirrors).
    RE::BSLight* lights[16]{};
};
bool PrepareDraw(const RE::BSRenderPass* original, std::uint32_t technique, Draw& draw) noexcept;
// Ends private face-light admission for the synchronous draw. No scene-owned
// light is mutated. Called when the outermost draw returns, on every path.
void EndDrawLeases() noexcept;
}
