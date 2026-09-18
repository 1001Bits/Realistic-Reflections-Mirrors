#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

// World-space influence, deliberately independent of the viewing camera and
// the native four-light shadow-map assignment. No light lifetime ownership.
namespace MirrorSurfaceLightPolicy
{
// Verified native BSLight constructors: SE 101289, AE 108276. Require both
// the entry shape and the RIP-relative assignment of the exact BSLight vtable.
// A detoured/mismatched constructor fails closed before allocating anything.
inline bool FaceLightConstructorMatches(std::span<const std::uint8_t> bytes,
    std::uintptr_t address, std::uintptr_t vtable, bool ae) noexcept
{
    constexpr std::array<std::uint8_t, 8> seEntry{ 0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83 };
    constexpr std::array<std::uint8_t, 8> aeEntry{ 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x56, 0x57 };
    const auto& entry = ae ? aeEntry : seEntry;
    const std::size_t offset = ae ? 0x2E : 0x36;
    if (!address || !vtable || bytes.size() < offset + 10 ||
        !std::equal(entry.begin(), entry.end(), bytes.begin()) ||
        bytes[offset] != 0x48 || bytes[offset + 1] != 0x8D || bytes[offset + 2] != 0x05 ||
        bytes[offset + 7] != 0x48 || bytes[offset + 8] != 0x89 || bytes[offset + 9] != 0x01) return false;
    std::int32_t relative{};
    std::memcpy(&relative, bytes.data() + offset + 3, sizeof(relative));
    return address + offset + 7 + relative == vtable;
}

// Only the private Draw list is passed here. No native pass, scene candidate
// list or choice cache ever receives the private light identity.
template <class Light>
inline unsigned AppendFaceKey(Light** lights, unsigned count, unsigned maximum, Light* face) noexcept
{
    if (!face || !lights || count < 1 || count >= maximum || maximum > 16) return count;
    for (unsigned i = count; i > 1; --i) lights[i] = lights[i - 1];
    lights[1] = face;
    return count + 1;
}
// A moving/flickering candidate set invalidates every object's cached choice.
// Admit storage/lookups only after two identical consecutive snapshots. Exact
// signature, geometry, material and live-light validation remain mandatory.
class ChoiceCacheAdmission
{
public:
    [[nodiscard]] bool Observe(std::uintptr_t scene, std::uint64_t signature) noexcept
    {
        const bool stable = scene && signature && scene == scene_ && signature == signature_;
        scene_ = scene;
        signature_ = signature;
        return stable;
    }
private:
    std::uintptr_t scene_{};
    std::uint64_t signature_{};
};

struct Admission
{
    bool surfaceLights{}, supportedFlat{}, communityShaders{}, shadowLimitFix{};
    bool enbPresent{}, enbOptIn{}, enbMaskReady{};
    bool communityShaderBridgeReady{};
};
inline constexpr bool Admit(const Admission& a) noexcept
{
    return a.surfaceLights && a.supportedFlat && !a.shadowLimitFix &&
        (!a.communityShaders || (!a.enbPresent && a.communityShaderBridgeReady)) &&
        (!a.enbPresent || (a.enbOptIn && a.enbMaskReady));
}
inline constexpr std::uint32_t kLightingBase = 0x4800002Du;
// Effect SetupGeometry packs sun plus four locals independently of Lighting's
// descriptor count bits (SE 12FF8F2/12FF9C5, AE 1.7.104 155741E/15574FD).
// Membrane bit 15 takes a separate branch; preserve its contract. The Effect
// technique, including falloff/soft-depth/alpha flags, is never rewritten.
inline constexpr std::uint32_t kEffectBase = 0x4000002Cu;
inline constexpr unsigned kEffectMaximumLights = 5;
// Thin falloff cards such as vanilla cobwebs multiply their lighting by a
// steep view-angle opacity curve. Four local lights can therefore turn a tiny
// angular alpha change into a yellow-white flash. Keep only the stable
// sun/ambient entry. A selected local can still flare, and can switch identity
// as the reflected camera moves; neither belongs in this view-dependent alpha
// path. The authored falloff, soft depth and transparency remain unchanged.
inline constexpr unsigned kFalloffEffectMaximumLights = 1;
inline constexpr unsigned EffectMaximumLights(bool falloff) noexcept
{
    return falloff ? kFalloffEffectMaximumLights : kEffectMaximumLights;
}
inline constexpr bool LitEffectTechnique(std::uint32_t technique) noexcept
{
    if (technique < kEffectBase || technique >= kLightingBase) return false;
    const auto descriptor = technique - kEffectBase;
    return (descriptor & ((1u << 16) | (1u << 15))) == (1u << 16);
}
// Descriptor fields are relative to the base, not bits of passEnum itself.
// Native GetRenderPasses: SE RVA 12C6AD0 (local count), 12C6AE8 (shadow
// count), 12C6B78/83 (deferred/directional mask), 12C6EC0 (add base).
// Vanilla packs the light count into descriptor bits 3-5. Community Shaders'
// True PBR (1.8.x, when that feature is loaded) strips those bits in its
// BSLightingShaderProperty::GetRenderPasses hook, reuses bit 3 as its TruePbr
// flag, and re-packs the count in its BSLightingShader::SetupGeometry hook from
// pass->numLights. Re-inserting the vanilla count there set bit 3 on every
// even-count draw, so CS cast vanilla materials to PBR and faulted
// (2026-09-14 run 17, PBR_BSLightingShader_SetupMaterial::thunk, TruePBR.cpp:1503).
// With peerPacksLightCount the count bits are left exactly as received.
// Bits 13 (ShadowDir) and 14 (DefShadow) gate the engine's screen-space shadow
// mask (t14); Community Shaders 1.8.4 uses the same two, Common/Permutation.hlsli
// `ShadowDir = (1 << 13)`, `DefShadow = (1 << 14)`. The private pass normally
// clears both, because that mask belongs to the main camera and its reflected
// lookup produced camera-dependent contact shadows (owner rule, 2026-09-14
// run 6: stable or none).
//
// With a private mask rendered for this capture they are SET, not merely kept.
// Keeping them was the 2026-09-14 conservative choice — "never add a bit a
// native pass lacked" — and it is why reflected shadows stayed wrong under
// Community Shaders through runs 27-32 while every counter looked healthy.
// CS reads the mask only inside two gates, both in its shipped Lighting.hlsl:
//
//   :1600  if ((DefShadow) && ((ShadowDir) || inWorld) || numShadowLights > 0)
//              shadowColor = TexShadowMaskSampler.Sample(...);
//   :2140  if ((DefShadow) && (ShadowDir)) dirDetailedShadow *= shadowColor.x;
//
// A descriptor carries those bits when the *main* camera treated the geometry
// as a directional-shadow receiver. A reflection draws a different set of
// surfaces, so most of them arrive without the bits: run 32 corrected
// 3,393,310 private draws and only 222,081 of them (6.5%, and that counter
// tests the wider 0x61C0) had any cached shadow membership. The mask was
// rendered 3,840 times and bound 3,616,385 times, and over nine draws in ten
// then left `dirDetailedShadow` at 1.0 and ignored it. That is exactly the
// owner's report: nothing shadowed ahead, a thin contact-like darkening on the
// few surfaces behind them that the main view had flagged, changing as they
// walked. Under vanilla the rebuilt private-shadow variant replaces the
// lighting program and computes visibility whatever the bits say, which is why
// run 33 with Community Shaders removed was correct on the same build.
//
// The mask is valid for every pixel of the capture, so declaring it for every
// private draw is the truthful descriptor, not a loosened one.
inline constexpr std::uint32_t kShadowMaskBits = 0x6000u;
inline constexpr std::uint32_t PrivateTechnique(std::uint32_t technique, unsigned count,
    bool peerPacksLightCount = false, bool privateShadowMask = false) noexcept
{
    const auto descriptor = technique - kLightingBase;
    const auto forced = privateShadowMask ? kShadowMaskBits : 0u;
    if (peerPacksLightCount)
        return kLightingBase + ((descriptor & ~0x61C0u) | forced);
    return kLightingBase + ((descriptor & ~0x61F8u) | forced |
        ((std::min)(count - 1, 7u) << 3));
}
struct Sphere { float x{}, y{}, z{}, radius{}; };
inline bool Finite(float v) noexcept
{
    return (std::bit_cast<std::uint32_t>(v) & 0x7F800000u) != 0x7F800000u;
}
inline bool Valid(Sphere s) noexcept
{
    return Finite(s.x) && Finite(s.y) && Finite(s.z) && Finite(s.radius) && s.radius >= 0;
}
// Distance to the receiver bound, normalized by the light radius. A bound is
// only a broad phase: native attenuation still determines individual pixels.
// reachScale > 1 widens admission past the authored radius. Community Shaders'
// Light Limit Fix lights every surface from a screen-space cluster grid built
// for the main camera, so a mirror pixel indexes the wrong cell and its cluster
// lights are neutralized (MirrorSunShadows); the strict per-draw list must then
// carry every light CS would have found, and CS's own attenuation decides the
// contribution (2026-09-14 run 19: a bottle 450 units from a 447-unit candle,
// bright in the room, dark in the standing mirror, flickering in the hand one).
inline double Influence(Sphere receiver, Sphere light, double reachScale = 1.0) noexcept
{
    if (!Valid(receiver) || !Valid(light) || light.radius <= 0 ||
        !(reachScale >= 1.0) || !std::isfinite(reachScale)) return -1;
    const double x = double(receiver.x)-light.x, y = double(receiver.y)-light.y,
                 z = double(receiver.z)-light.z;
    const double gap = (std::max)(0.0, std::sqrt(x*x+y*y+z*z)-receiver.radius);
    const double reach = double(light.radius) * reachScale;
    return gap < reach ? 1.0-gap/reach : -1;
}
struct Candidate { std::uintptr_t identity{}; double influence{}; };
struct Selection
{
    // Native call families allow at most sixteen entries, including the sun.
    std::array<Candidate, 15> entries{};
    unsigned count{};
    void Add(Candidate c, unsigned capacity) noexcept
    {
        capacity = (std::min)(capacity, unsigned(entries.size()));
        if (!c.identity || c.influence < 0 || !std::isfinite(c.influence)) return;
        for (unsigned i=0; i<count; ++i) if (entries[i].identity == c.identity) return;
        unsigned at=0;
        while (at<count && (entries[at].influence > c.influence ||
               (entries[at].influence == c.influence && entries[at].identity < c.identity))) ++at;
        if (at>=capacity) return;
        const unsigned last=(std::min)(count,capacity-1);
        for (unsigned i=last; i>at; --i) entries[i]=entries[i-1];
        entries[at]=c;
        count=(std::min)(count+1,capacity);
    }
};

// Owner, 2026-09-17: "when a player is in front of a mirror their face should
// never be in the shadow" (ScreenShot325: a few steps back, the reflected face
// is grey; 326: close up, it is warm). Each core mirror's face light
// (MOSMirrorWarmFaceLight: radius 240, colour 255/240/222) hangs a short way in
// front of the glass, so a face a few steps back is past most of its reach.
// Run 13: the key is now a detached private light, used only in the reflected
// player's draw list. The authored emitter has zero fade; neither reflected
// scenery nor the normal view receives our added light.
struct Point { double x{}, y{}, z{}; };
inline constexpr double kFaceLightRadius = 240.0;
inline constexpr std::array<double, 3> kFaceLightColor{ 1.0, 240.0 / 255.0, 222.0 / 255.0 };
// Owner, core run 8 (ScreenShot327): "there is too much light, the light on
// the player face should be equal". A full-strength light a hand's width from
// the face blew it out, so the key sits further out and its strength is set so
// the face receives the same moderate amount at any distance from the glass.
// Core run 9 (ScreenShot328, Hjerim): "light in this mirror is still too
// strong", then "can we have a reflected face light slider? Don't change the
// room light". The strength is a setting (percent, default 25); 0 leaves the
// reflected face with the room's own light, and the room light never changes.
inline constexpr double kFaceKeyDistance = 110.0;
inline constexpr double kFaceKeyLift = 8.0;
inline constexpr int kDefaultFaceLightPercent = 25;
inline constexpr double kFaceKeyIntensity = kDefaultFaceLightPercent / 100.0;
inline std::atomic<int> faceLightPercent{ kDefaultFaceLightPercent };
[[nodiscard]] constexpr int ClampFaceLight(int percent) noexcept { return (std::clamp)(percent, 0, 100); }
[[nodiscard]] inline double FaceKeyIntensity() noexcept
{
    return ClampFaceLight(faceLightPercent.load(std::memory_order_relaxed)) / 100.0;
}

[[nodiscard]] inline double Dot(const Point& a, const Point& b) noexcept { return a.x*b.x + a.y*b.y + a.z*b.z; }
[[nodiscard]] inline Point Minus(const Point& a, const Point& b) noexcept { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
[[nodiscard]] inline bool Finite(const Point& p) noexcept
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The mod's own face light, recognised by its authored radius and colour.
[[nodiscard]] inline bool IsMirrorFaceLight(double radius, double r, double g, double b) noexcept
{
    return std::fabs(radius - kFaceLightRadius) <= 0.5 && std::fabs(r - kFaceLightColor[0]) <= 0.02 &&
        std::fabs(g - kFaceLightColor[1]) <= 0.02 && std::fabs(b - kFaceLightColor[2]) <= 0.02;
}

// The preserved authored emitters now have zero fade. Do not spend a private
// draw slot on them; the detached key is added explicitly for the player.
[[nodiscard]] inline bool DormantFaceEmitter(double fade, double radius, double r, double g, double b) noexcept
{
    return fade == 0.0 && IsMirrorFaceLight(radius, r, g, b);
}

// A face light belongs to this pane when it hangs in front of it, on the side
// the player stands, near its centre (authored 44-136 units out, +/-18 across).
[[nodiscard]] inline bool FaceLightBelongsToPane(const Point& light, const Point& center, const Point& normal,
    const Point& head) noexcept
{
    if (!Finite(light) || !Finite(center) || !Finite(normal) || !Finite(head)) return false;
    const double length = std::sqrt(Dot(normal, normal));
    if (!(length > 0.5)) return false;
    const Point n{ normal.x/length, normal.y/length, normal.z/length };
    const Point offset = Minus(light, center);
    const double front = Dot(offset, n), headFront = Dot(Minus(head, center), n);
    if (front * headFront <= 0 || std::fabs(front) < 10.0 || std::fabs(front) > 160.0) return false;
    const Point across{ offset.x - n.x*front, offset.y - n.y*front, offset.z - n.z*front };
    return Dot(across, across) <= 120.0 * 120.0;
}

// Light fade for the key at `distance` from the face: the quadratic falloff
// (1 - d^2/r^2) times the fade is held at `intensity` (capped at full fade).
[[nodiscard]] inline double FaceKeyFade(double distance, double radius,
    double intensity = kFaceKeyIntensity) noexcept
{
    if (!std::isfinite(intensity)) intensity = kFaceKeyIntensity;
    intensity = std::clamp(intensity, 0.0, 1.0);
    if (!std::isfinite(distance) || !std::isfinite(radius) || radius <= 0.0) return intensity;
    const double ratio = distance / radius;
    const double falloff = (std::max)(0.05, 1.0 - ratio * ratio);
    return std::clamp(intensity / falloff, 0.0, 1.0);
}

// Key position: from the head toward the pane centre, never past the glass.
[[nodiscard]] inline bool FaceKeyPosition(const Point& head, const Point& center, const Point& normal, Point& out) noexcept
{
    if (!Finite(head) || !Finite(center) || !Finite(normal)) return false;
    const double length = std::sqrt(Dot(normal, normal));
    if (!(length > 0.5)) return false;
    const Point n{ normal.x/length, normal.y/length, normal.z/length };
    const double headFront = std::fabs(Dot(Minus(head, center), n));
    Point toward = Minus(center, head);
    const double distance = std::sqrt(Dot(toward, toward));
    if (headFront < 1.0 || distance < 1.0) return false;
    toward = { toward.x/distance, toward.y/distance, toward.z/distance };
    // Along `toward` the plane distance falls by at most the step, so 80% of
    // it keeps the key on the player's side of the glass.
    const double step = (std::min)(kFaceKeyDistance, headFront * 0.8);
    out = { head.x + toward.x*step, head.y + toward.y*step, head.z + toward.z*step + kFaceKeyLift };
    return Finite(out);
}
}
