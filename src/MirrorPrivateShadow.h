#pragma once
#include "MirrorShadowCasterSet.h"
#include <DirectXMath.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>

namespace MirrorPrivateShadow
{
// Updated once at the source-frame fence. Per-draw map lookup reads values;
// it never calls the platform clock or treats generated frames as scene ticks.
inline std::atomic<std::uint64_t> sourceFrame{}, sourceMicroseconds{};
inline void ObserveSourceTime(std::uint64_t frame, std::uint64_t microseconds) noexcept
{
    sourceMicroseconds.store(microseconds, std::memory_order_relaxed);
    sourceFrame.store(frame, std::memory_order_release);
}
// 2048: a 4096 trial on 2026-09-14 (run 11) raised the GPU frame from ~10 ms to
// 33-54 ms outdoors (16-tap sampling of two 4096 maps from a 4096 mirror
// target) for 15-22 FPS.
//
// Owner, 2026-09-15: "We don't need high detail shadows but the problem with
// lower shadow res is that it makes shadows shimmer/flutter, if we can prevent
// this we can use lower res." The shimmer cure is already here and is not
// resolution-dependent: Fit() snaps the map origin to a 16-texel grid derived
// from the map's own size, so a coarser map still steps in whole texels instead
// of crawling. Both sizes are therefore free to move, and both are settings.
//
// The detail map's old 2048 over a 512-unit box was 0.25 units per texel --
// finer than any face mesh resolves -- for a quarter of the whole frame budget.
// 1024 gives half-unit texels, still well past what a head shows, at a quarter
// of the cost.
inline constexpr unsigned kDefaultResolution = 2048;
inline constexpr unsigned kDefaultDetailResolution = 1024;
inline constexpr unsigned kMinimumResolution = 256;
inline constexpr unsigned kMaximumResolution = 4096;
[[nodiscard]] inline bool ValidResolution(unsigned value) noexcept
{
    return value >= kMinimumResolution && value <= kMaximumResolution &&
        (value & (value - 1)) == 0; // power of two: the snap grid divides evenly
}
inline std::atomic_uint32_t resolution{kDefaultResolution};
inline std::atomic_uint32_t detailResolution{kDefaultDetailResolution};
[[nodiscard]] inline unsigned Resolution(bool detail) noexcept
{
    const auto value = (detail ? detailResolution : resolution).load(std::memory_order_relaxed);
    return ValidResolution(value) ? value :
        (detail ? kDefaultDetailResolution : kDefaultResolution);
}
// Retained for the fixed-size arithmetic below that predates the setting.
inline constexpr unsigned kResolution = kDefaultResolution;
// A fixed receiver volume avoids camera-angle-dependent shadow coverage. The
// upstream half includes casters outside the reflected camera's visible frustum.
inline constexpr float kReceiverRadius = 8192.0f;
// Default receiver footprint when no render-distance control is active. Owner
// (2026-09-14, run 11): "cut all shadows off after 1000 distance to reduce cost",
// then (2026-09-15, after run 34) "Can we cut off shadows after 1500 distance?".
// The wide texel grows with it -- 2.0 units at 1000, 2.46 at 1500 with a 2048
// map -- and the 16-texel snap below keeps the footprint from crawling either
// way, so the cost of the extra reach is texel size, not stability.
inline constexpr float kDefaultReceiverRadius = 1500.0f;
inline constexpr float kGuard = 1024.0f;
inline constexpr float kExtent = kReceiverRadius + kGuard;
inline constexpr float kTexel = 2.0f * kExtent / kResolution;
inline constexpr float kSnap = 16.0f * kTexel; // integral texels prevent grid-phase swimming
inline constexpr float kNear = 1.0f;
inline constexpr float kDepth = 4.0f * kExtent;
// The wide map has nine-unit texels, too coarse for an animated face. A
// separate, player-centred map keeps the same upstream caster depth and adds
// quarter-unit detail without sacrificing the wide map's scenery coverage.
inline constexpr float kDetailExtent = 256.0f;
inline constexpr float kDetailGuard = 32.0f;
struct Plan
{
    DirectX::XMFLOAT3 eye{}, forward{}, up{}, right{};
    DirectX::XMFLOAT4X4 worldToTexture{};
    // Late hand capture can refresh the hidden player after placed mirrors.
    // Share maps only within the same pose phase of the source frame.
    std::uint32_t posePhase{};
    float extent{kExtent};
    std::uint32_t sceneryOnly{};
    std::uint32_t playerShadow{1};
    // The map size is a setting, so it belongs to the plan: the snap grid, the
    // guard band in Covers() and the render target all derive from it, and a
    // map published at one size must never satisfy a request at another.
    std::uint32_t resolution{kDefaultResolution};
};
inline bool Detail(const Plan& plan) noexcept { return plan.extent == kDetailExtent; }
inline bool Fit(DirectX::XMFLOAT3 receiver, DirectX::XMFLOAT3 lightRays, Plan& out,
                bool detail = false, float receiverRadius = kReceiverRadius,
                unsigned mapResolution = 0) noexcept
{
    using namespace DirectX;
    out = {};
    out.resolution = mapResolution ? mapResolution : Resolution(detail);
    if (!ValidResolution(out.resolution)) return false;
    // Colour distance limits the receiver footprint, not the upstream light
    // depth: a distant building may still shade geometry inside that footprint.
    if ((std::bit_cast<std::uint32_t>(receiverRadius) & 0x7f800000u) == 0x7f800000u ||
        receiverRadius < 500.0f || receiverRadius > kReceiverRadius) return false;
    out.extent = detail ? kDetailExtent : receiverRadius + kGuard;
    const auto finite = [](const XMFLOAT3& p) {
        return MirrorShadowCasterVolume::Finite(p) &&
               std::abs(p.x) < 1.0e7f && std::abs(p.y) < 1.0e7f && std::abs(p.z) < 1.0e7f;
    };
    if (!finite(receiver) || !finite(lightRays)) return false;
    auto f = XMLoadFloat3(&lightRays);
    if (XMVectorGetX(XMVector3LengthSq(f)) < 0.0001f) return false;
    f = XMVector3Normalize(f);
    // The sun direction is used exactly as given.
    //
    // Run 36 quantized it to 1/1024 instead, to make Covers() -- which compared
    // the basis bit-exactly -- match across frames so a map could be reused.
    // That worked, and it is also what made outdoor shadows pulse: quantizing
    // turns the sun's smooth drift into a 0.06 degree jerk every twenty frames,
    // the whole light basis rotates at once, and the map's texel lattice rotates
    // with it. A lattice that turns under stationary geometry re-quantizes every
    // shadow edge in the map simultaneously -- one visible pop, several times a
    // second, which is what the owner reported outdoors on 2026-09-15.
    //
    // Reuse does not need a quantized direction; it needs a tolerant
    // comparison, which is where the tolerance now lives (see Covers()). With
    // the exact direction the basis drifts by about 0.002 degrees a frame, which
    // at the far edge of a 2500-unit map is under a twentieth of a texel, and
    // while a map is being reused its content does not change at all.
    const auto seed = std::abs(XMVectorGetZ(f)) > .9f ? XMVectorSet(0,1,0,0) : XMVectorSet(0,0,1,0);
    const auto r = XMVector3Normalize(XMVector3Cross(f, seed));
    const auto u = XMVector3Cross(r, f);
    const auto p = XMLoadFloat3(&receiver);
    // Snap the map origin to whole texels so the shadow pattern does not crawl
    // as the footprint follows the eye -- but snap *near the receiver*, not at
    // the world origin.
    //
    // Projecting the absolute world position onto r/u makes the snapped
    // coordinate proportional to |p|, which in Skyrim's exterior worldspace is
    // tens of thousands of units. Riverwood sits at about 56,000 units from the
    // origin. The basis turns with the sun, and the quantum above turns it by
    // 0.06 degrees at a time: 56,000 * 0.00105 is about 59 units of movement in
    // the snapped coordinate, against a snap cell of 16 texels -- 55 units at
    // 2048 over a 2500 reach. So every quantum step shifted the entire map by
    // roughly one whole cell, about three times a second, which is the pulsing
    // the owner reported outdoors on 2026-09-15. Unquantized (before run 36)
    // the same term moved continuously at about 0.6 texel a frame, which is the
    // shimmer reported before that. One defect, two appearances.
    //
    // Measuring from a coarse anchor beside the receiver removes the leverage:
    // the offset being projected is at most the anchor grid, so basis rotation
    // moves it by that times the angle instead of by |p| times the angle. The
    // anchor is quantized to Skyrim's own cell size, so it is constant for a
    // whole cell of travel and its contribution is added exactly, unrounded --
    // the texel grid it defines is therefore stable for as long as the player
    // stays in the cell, and re-anchors once at a boundary, where the scene is
    // being rebuilt anyway.
    constexpr float kAnchorGrid = 4096.0f;  // one exterior cell
    const auto quantize = [](float n, float step) { return std::floor(n / step + .5f) * step; };
    const auto anchor = XMVectorSet(quantize(receiver.x, kAnchorGrid),
                                    quantize(receiver.y, kAnchorGrid),
                                    quantize(receiver.z, kAnchorGrid), 0.0f);
    const auto local = XMVectorSubtract(p, anchor);
    const float grid = 16.0f * (2.0f * out.extent / out.resolution);
    const auto place = [&](FXMVECTOR axis) {
        return XMVectorGetX(XMVector3Dot(anchor, axis)) +
               quantize(XMVectorGetX(XMVector3Dot(local, axis)), grid);
    };
    const float x = place(r);
    const float y = place(u);
    const float z = place(f) - 3.0f * kExtent;
    const auto eye = r*x + u*y + f*z;
    XMStoreFloat3(&out.eye, eye);
    XMStoreFloat3(&out.forward, f);
    XMStoreFloat3(&out.up, u);
    XMStoreFloat3(&out.right, r);
    // The native camera uses col0=forward, col1=up, col2=right. Its view is
    // camera-relative; this absolute matrix adds exactly the missing translation.
    const XMMATRIX view{
        out.right.x, out.up.x, out.forward.x, 0,
        out.right.y, out.up.y, out.forward.y, 0,
        out.right.z, out.up.z, out.forward.z, 0,
        -x, -y, -z, 1};
    const auto projection = XMMatrixOrthographicLH(2*out.extent, 2*out.extent, kNear, kDepth);
    const XMMATRIX texture{.5f,0,0,0, 0,-.5f,0,0, 0,0,1,0, .5f,.5f,0,1};
    XMStoreFloat4x4(&out.worldToTexture, view * projection * texture);
    return true;
}
inline bool Same(const Plan& a, const Plan& b) noexcept
{
    return std::memcmp(&a, &b, sizeof(Plan)) == 0;
}
inline bool Covers(const Plan& volume, const Plan& request) noexcept
{
    if (volume.posePhase != request.posePhase || volume.extent != request.extent ||
        volume.sceneryOnly != request.sceneryOnly ||
        volume.playerShadow != request.playerShadow ||
        volume.resolution != request.resolution) return false;
    using namespace DirectX;
    // A stored map serves a request whose sun has drifted by less than the angle
    // that moves a shadow one texel at the far edge of the map. Demanding an
    // exact basis instead is what forced run 36 to quantize the direction, and
    // the quantization is what made the shadows pulse; the error budget is the
    // same, but it is spent as a bounded staleness rather than as a jerk.
    const float edgeTexel = 2.0f * volume.extent / volume.resolution;
    const float tolerance = edgeTexel / (std::max)(volume.extent, 1.0f);
    const auto drift = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&volume.forward),
                                                 XMLoadFloat3(&request.forward)));
    if (!MirrorShadowCasterVolume::Finite(drift) ||
        drift < std::cos(tolerance)) return false;
    const auto delta = XMLoadFloat3(&request.eye)-XMLoadFloat3(&volume.eye);
    for (const auto axis : {volume.right, volume.up, volume.forward}) {
        const auto distance = std::abs(XMVectorGetX(XMVector3Dot(delta, XMLoadFloat3(&axis))));
        const float guard = Detail(volume) ? kDetailGuard : kGuard;
        const float texel = 2.0f * volume.extent / volume.resolution;
        if (!MirrorShadowCasterVolume::Finite(distance) || distance > guard - 3*texel) return false;
    }
    return true;
}

// Value-only frame/volume ownership; no main-view shadow descriptors are touched.
// A failed new render invalidates the old publication before the DSV is cleared.
class Publication
{
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ShaderResourceView> depth, matrices;
    Ptr<ID3D11Buffer> buffer;
    Ptr<ID3D11Device> device;
    Plan plan{};
    std::uint64_t frame{};
    std::uint64_t publishedMicroseconds{};
    MirrorShadowCasterSet casterCoverage;
public:
    void Invalidate() noexcept { frame = 0; depth.Reset(); }
    // A map was usable only in the frame that rendered it, so every capture
    // rebuilt both 2048 maps from scratch: run 34 logged mapReuses=0 against
    // 6,000 generations while the owner stood still, and reflected shadows
    // measured +10.5 ms a frame (49 -> 33 FPS in front of a standing mirror).
    // Nothing about a shadow map expires with the frame counter. Covers()
    // already decides validity geometrically -- same light basis, same pose
    // phase, and the request still inside the snapped footprint's guard band --
    // so the age bound limits caster movement, which Covers cannot see. A wide
    // map lasts its creation frame plus at most four subsequent source frames,
    // also bounded to 66.7 ms so low FPS cannot stretch the lag to 133+ ms.
    // The detail map keeps an age of zero because it is centred
    // on the player and holds the player as a caster, so freezing it would lag
    // the face's own shadow against its animation.
    static constexpr std::uint64_t kWideMapMaxAge = 4;
    static constexpr std::uint64_t kWideMapMaxMicroseconds = 66'667;
    [[nodiscard]] std::uint64_t MaxAge() const noexcept
    { return Detail(plan) ? 0u : kWideMapMaxAge; }
    [[nodiscard]] bool Fresh(std::uint64_t epoch) const noexcept
    {
        if (!epoch || !frame || epoch < frame || epoch - frame > MaxAge() || !depth) return false;
        if (epoch == frame) return true;
        if (!publishedMicroseconds || sourceFrame.load(std::memory_order_acquire) != epoch) return false;
        const auto now = sourceMicroseconds.load(std::memory_order_relaxed);
        return now >= publishedMicroseconds && now - publishedMicroseconds <= kWideMapMaxMicroseconds;
    }
    bool Matches(std::uint64_t epoch, const Plan& candidate,
                 const MirrorShadowCasterVolume* receiver=nullptr) const noexcept
    { return Fresh(epoch) && Covers(plan, candidate) &&
        (receiver ? casterCoverage.Covers(*receiver) : casterCoverage.Unpruned()); }
    bool Current(std::uint64_t epoch) const noexcept { return Fresh(epoch) && matrices; }
    ID3D11ShaderResourceView* Depth() const noexcept { return depth.Get(); }
    ID3D11ShaderResourceView* Matrices() const noexcept { return matrices.Get(); }
    bool Publish(ID3D11DeviceContext* context, ID3D11ShaderResourceView* map,
                 std::uint64_t epoch, const Plan& candidate,
                 const MirrorShadowCasterSet* coverage=nullptr)
    {
        Invalidate();
        if (!context || !map || !epoch) return false;
        Ptr<ID3D11Device> current, owner;
        context->GetDevice(&current);
        map->GetDevice(&owner);
        if (current.Get() != owner.Get()) return false;
        if (current.Get() != device.Get()) { *this = {}; device = current; }
        if (coverage) { if (!casterCoverage.Assign(*coverage)) return false; }
        else casterCoverage.Reset({});
        // Same public GPU matrix shape as the older shader adapter, but entirely
        // private. Proj[0] contains an absolute world-to-shadow transform.
        struct Data { DirectX::XMFLOAT4X4 proj[2], inverse[2]; float end[2], start[2]; } data{};
        static_assert(sizeof(Data) == 272);
        // HLSL uses mul(column_major_matrix, column_vector). The raw rows of
        // our row-vector matrix are already the columns of that transpose.
        data.proj[0] = candidate.worldToTexture;
        if (!buffer) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(Data);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(Data);
            if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)) ||
                FAILED(device->CreateShaderResourceView(buffer.Get(), nullptr, &matrices))) {
                *this = {}; return false;
            }
        }
        context->UpdateSubresource(buffer.Get(), 0, nullptr, &data, 0, 0);
        depth = map;
        plan = candidate;
        frame = epoch;
        publishedMicroseconds = sourceFrame.load(std::memory_order_acquire) == epoch ?
            sourceMicroseconds.load(std::memory_order_relaxed) : 0;
        return true;
    }
};

// A null replacement requires proven opaque depth-only semantics; a supplied
// replacement must retain the original discard operations and dependencies.
// Both preserve native geometry/depth state and reject alpha-to-coverage.
class CasterDraw
{
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
    bool pending{};
public:
    bool Begin(ID3D11DeviceContext* ctx, ID3D11PixelShader* replacement = nullptr) {
        if (pending || !ctx) return false;
        // A peer may bind A2C directly without updating Skyrim's cached flags.
        Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
        ctx->OMGetBlendState(&blend,nullptr,nullptr);
        if (blend) {
            D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);
            if (desc.AlphaToCoverageEnable) return false;
        }
        UINT count=0;ctx->PSGetShader(&shader,nullptr,&count);
        if (!shader || count) {shader.Reset();return false;}
        context=ctx;pending=true;ctx->PSSetShader(replacement,nullptr,0);return true;
    }
    bool Restore() noexcept {
        if (!pending) return true;
        if (!RestoreSEH()) return false;
        context.Reset();shader.Reset();pending=false;return true;
    }
private:
    bool RestoreSEH() noexcept {
        __try {
            context->PSSetShader(shader.Get(),nullptr,0);
            ID3D11PixelShader* observed=nullptr;
            context->PSGetShader(&observed,nullptr,nullptr);
            const bool same=observed==shader.Get();
            if (observed) observed->Release();
            return same;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
} // namespace MirrorPrivateShadow
