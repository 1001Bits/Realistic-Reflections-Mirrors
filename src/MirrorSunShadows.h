#pragma once
#include <cstdint>
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
namespace MirrorPrivateShadow { struct Plan; }
struct MirrorShadowCasterVolume;
class MirrorShadowCasterSet;
namespace RE { class BSRenderPass; }
namespace MirrorSunShadows
{
void Install();
void OnSetupGeometry(const RE::BSRenderPass* pass) noexcept;
void OnCommit(ID3D11DeviceContext *context);
[[nodiscard]] bool Restore() noexcept;
void LogDiagnostics();
[[nodiscard]] bool PrivateGenerationEnabled() noexcept;
/**
 * Private screen-space sun mask (owner 2026-09-14: reflected shadows must work
 * with every Community Shaders setting). Default off behind
 * `Data\\MirrorsOfSkyrim_ShadowMask.enable`.
 */
[[nodiscard]] bool ShadowMaskRouteEnabled() noexcept;
// Both private sun maps published for the current main-world source frame.
// Interiors never have them: no sun plan, no maps, nothing for a depth
// pre-pass or shadow mask to do.
[[nodiscard]] bool PrivateSunMapsCurrent() noexcept;
/** True while the current capture's technique rewrites must keep bits 13/14. */
[[nodiscard]] bool ShadowMaskActive() noexcept;
/** Render the capture's mask from its completed depth pre-pass. */
[[nodiscard]] bool RenderShadowMask(ID3D11DeviceContext* context,
                                    ID3D11ShaderResourceView* captureDepth,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint64_t sequence) noexcept;
/**
 * Present a private capture's own dimensions in the renderer's screen-size
 * globals for the whole capture, so every screen-space lookup inside it -- the
 * shadow mask, water's depth and refraction sampling, any peer's screen-space
 * effect -- addresses at the capture's scale instead of the main framebuffer's.
 *
 * Safe to call more than once for the same extent; the first call saves the
 * renderer's real values. Undone only by EndCaptureScreenSize(),
 * which the private-pass cleanup in SecondView calls on every exit path.
 */
void PresentCaptureScreenSize(std::uint32_t width, std::uint32_t height) noexcept;
/** Restore dimensions/ratios only at the outer private-capture exit. */
void EndCaptureScreenSize() noexcept;
/** Drop the mask when a capture ends or its depth is no longer valid. */
void EndShadowMask() noexcept;
[[nodiscard]] bool PrivateMapMatches(const MirrorPrivateShadow::Plan& plan,
                                    const MirrorShadowCasterVolume* receiver = nullptr) noexcept;
void InvalidatePrivateMap(bool detail = false) noexcept;
[[nodiscard]] bool PublishPrivateMap(ID3D11DeviceContext*, ID3D11ShaderResourceView*,
                                     const MirrorPrivateShadow::Plan&, const MirrorShadowCasterSet* coverage = nullptr);
[[nodiscard]] bool BeginPrivateCamera(const MirrorPrivateShadow::Plan&) noexcept;
[[nodiscard]] bool OnCameraUpload() noexcept;
[[nodiscard]] bool PrivateCameraUploaded() noexcept;
[[nodiscard]] bool EndPrivateCamera() noexcept;
[[nodiscard]] bool RestoreCasterDraw() noexcept;
void OnCasterCommit(ID3D11DeviceContext*);
// Exact native ENB seam, enabled only by its separate empty marker.
void EnableENB(ID3D11DeviceContext* nativeContext) noexcept;
[[nodiscard]] bool ENBEnabled() noexcept;
using NativeDraw = void(__stdcall*)(ID3D11DeviceContext*, unsigned, unsigned, int);
[[nodiscard]] bool DrawENB(ID3D11DeviceContext*, ID3D11RenderTargetView* expected,
                         NativeDraw, unsigned count, unsigned start, int base, bool caster);
} // namespace MirrorSunShadows
