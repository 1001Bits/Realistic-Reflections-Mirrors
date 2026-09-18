#include "PCH.h"
#include "MirrorSurfaceLights.h"
#include "MirrorSurfaceLightPolicy.h"
#include "MirrorENBScreenMask.h"
#include "HandMirrorSafetySettings.h"
#include "PeerDetection.h"
#include "SecondView.h"
#include "MirrorSunShadows.h"
#include "HandMirrorSoftEffectBypass.h"
#include "SupportedRuntimePolicy.h"
#include "MirrorCaptureProfile.h"
#include "MirrorCaptureOptimizations.h"
#include <algorithm>
#include "MirrorBoundedCache.h"
#include <bit>
#include <vector>

namespace MirrorSurfaceLights
{
namespace
{
std::atomic<bool> enabled{}, faulted{};
bool enbPath{}; // Set before enabled is published; never toggled by ENB UseEffect.
bool communityShadersPath{};
// True when a peer (Community Shaders with True PBR loaded) owns the lighting
// pass builder: BSLightingShaderProperty vtable slot 0x2A (GetRenderPasses)
// points outside the game's text segment while CS is present. Such a peer
// strips the light-count bits and re-packs them at SetupGeometry from
// pass->numLights, so PrivateTechnique must not write them.
bool peerPacksLightCount{};
[[nodiscard]] bool ProbePeerPacksLightCount() noexcept
{
    if (!PeerDetection::CommunityShadersPresent()) return false;
    __try {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSLightingShaderProperty[0] };
        const auto slot = *reinterpret_cast<const std::uintptr_t*>(vtable.address() + 0x2A * sizeof(void*));
        const auto text = REL::Module::get().segment(REL::Segment::textx);
        return slot < text.address() || slot >= text.address() + text.size();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
std::atomic<std::uint64_t> contexts{}, selections{}, rejected{};
std::atomic<std::uint64_t> drawCorrections{}, cachedMasks{}, drawFallbacks{}, effectDrawCorrections{};
// The player's own reflected body (owner run 5, 2026-09-17: "Is it correct that
// the face is so dark", ScreenShot319/320).
std::atomic<std::uint64_t> playerDraws{}, playerLights{}, playerPortalAdmits{};
std::atomic<std::uint64_t> playerNoLights{}, playerSunMismatch{};
// Twelve samples per cell, at most sixteen cells in a session. A lifetime-only
// budget was exhausted in Vlindrel before run 12 reached overlit Hjerim.
// Values only, render thread; no retained cell or light pointers.
struct PlayerSampleBudget { RE::FormID cell{}; unsigned samples{}; };
thread_local std::array<PlayerSampleBudget, 16> playerSampleBudgets{};
std::atomic<std::uint64_t> faceKeyLeases{}, faceKeyNoLight{}, faceKeyNoHead{}, faceKeyFaults{};

// One private engine light, constructed once and never registered with a scene,
// attached to a node, or inserted into a native pass. Only a stack-owned
// reflected-player Draw can reference it. No scene light is moved or brightened.
// Process lifetime is intentional: the engine heaps/string pool can disappear
// before DLL static destruction. This bounded allocation also survives cell loads.
RE::BSLight* privateFaceLight{};
bool privateFaceAttempted{};
struct FaceKeyLease
{
    RE::BSLight* bsLight{};
    bool active{};
};
thread_local FaceKeyLease faceKey{};

[[nodiscard]] bool CreatePrivateFaceLightSEH() noexcept
{
    __try {
        if (privateFaceLight) return true;
        if (privateFaceAttempted) return false;
        privateFaceAttempted = true;
        // Read-only Combined-project decompilation and Address Library reverse
        // mapping: docs/re/Mirror_Private_Face_Light.md. The stock constructor
        // initializes the intrusive list fence and reference-counted members.
        // Do not fabricate a BSLight by copying another scene's wrapper.
        REL::Relocation<RE::BSLight*(RE::BSLight*)> ctor{ RELOCATION_ID(101289, 108276) };
        REL::Relocation<std::uintptr_t> bsVtable{ RE::VTABLE_BSLight[0] };
        if (!MirrorSurfaceLightPolicy::FaceLightConstructorMatches(
                { reinterpret_cast<const std::uint8_t*>(ctor.address()), 0x40 },
                ctor.address(), bsVtable.address(), REL::Module::IsAE())) return false;

        // Cross-VR CommonLib declares only the common prefix (0x110). This
        // feature is flat-only; both native flat constructors require 0x150.
        // Check allocation before memset (the vendored Create helper does not).
        auto* niStorage = RE::malloc(0x150);
        auto* bsStorage = RE::malloc(sizeof(RE::BSLight));
        if (!niStorage || !bsStorage) {
            RE::free(niStorage); RE::free(bsStorage);
            return false;
        }
        std::memset(niStorage, 0, 0x150);
        std::memset(bsStorage, 0, sizeof(RE::BSLight));
        REL::Relocation<RE::NiPointLight*(void*)> niCtor{ RELOCATION_ID(69583, 70967) };
        auto* ni = niCtor(niStorage);
        auto* light = ctor(static_cast<RE::BSLight*>(bsStorage));
        REL::Relocation<std::uintptr_t> niVtable{ RE::VTABLE_NiPointLight[0] };
        if (!ni || !light || *reinterpret_cast<std::uintptr_t*>(ni) != niVtable.address() ||
            *reinterpret_cast<std::uintptr_t*>(light) != bsVtable.address()) return false;
        // Neither object is published until all initialization has succeeded.
        ni->name = "MOS Reflection Face Light";
        ni->local = RE::NiTransform{};
        ni->world = RE::NiTransform{};
        ni->SetLightAttenuation(float(MirrorSurfaceLightPolicy::kFaceLightRadius));
        auto& data = ni->GetLightRuntimeData();
        data.ambient = { 0.0F, 0.0F, 0.0F };
        data.diffuse = { 1.0F, 240.0F / 255.0F, 222.0F / 255.0F };
        data.radius = { 240.0F, 240.0F, 240.0F };
        data.fade = 0.0F;
        light->light.reset(ni);
        light->pointLight = true;
        light->ambientLight = false;
        light->dynamic = true;
        light->portalStrict = true;
        light->affectLand = false;
        light->affectWater = false;
        light->lodFade = false;
        light->lodDimmer = 1.0F;
        light->IncRefCount();
        privateFaceLight = light;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++faceKeyFaults;
        return false;
    }
}

// Never destroyed: the engine's string pool is gone before static destructors run.
[[nodiscard]] const RE::BSFixedString* HeadBoneName() noexcept
{
    static const RE::BSFixedString* name = new RE::BSFixedString("NPC Head [Head]");
    return name;
}

// In first person the mirror shows the hidden third-person body. The engine
// never registers hidden geometry with the lights around it (neither in the
// property's light list nor in the light's geometry list), so a portal-strict
// test rejects every portal-strict room light for that body -- which is how the
// reflected player ended up darker than the room behind them.
[[nodiscard]] bool PlayerBodyGeometry(const RE::BSGeometry* geometry) noexcept
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !geometry) return false;
    const RE::NiAVObject* root = player->Get3D1(false);
    if (!root) return false;
    const RE::NiAVObject* node = geometry;
    for (unsigned depth = 0; node && depth < 64; ++depth, node = node->parent)
        if (node == root) return true;
    return false;
}

struct LightSample
{
    float radius{};
    float distance{};
    float r{}, g{}, b{};
    float x{}, y{}, z{}, fade{};
    bool portalStrict{};
    bool pointLight{};
};

[[nodiscard]] bool ReadLightSampleSEH(RE::BSLight* light, const RE::NiPoint3& at, LightSample& out) noexcept
{
    __try {
        auto* ni = light->light.get();
        if (!ni) return false;
        const auto& ld = ni->GetLightRuntimeData();
        out.radius = ld.radius.x;
        out.distance = ni->world.translate.GetDistance(at);
        out.r = ld.diffuse.red; out.g = ld.diffuse.green; out.b = ld.diffuse.blue;
        out.x = ni->world.translate.x; out.y = ni->world.translate.y; out.z = ni->world.translate.z;
        out.fade = ld.fade;
        out.portalStrict = light->portalStrict;
        out.pointLight = light->pointLight;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] RE::FormID PlayerCellIdSEH() noexcept
{
    __try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        return cell ? cell->GetFormID() : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Bounded evidence of what the reflected body is lit by. Called at most once
// per 4096 body draws; this does not change selection, position or intensity.
void SamplePlayerDraw(const RE::BSGeometry* geometry, RE::BSLight** lights, std::uint32_t count) noexcept
{
    try {
        const auto cell = PlayerCellIdSEH();
        if (!cell) return;
        auto sample = std::find_if(playerSampleBudgets.begin(), playerSampleBudgets.end(),
            [cell](const auto& slot) { return slot.cell == cell || !slot.cell; });
        if (sample == playerSampleBudgets.end() || sample->samples >= 12) return;
        sample->cell = cell;
        ++sample->samples;
        const auto at = geometry->worldBound.center;
        std::string text;
        for (std::uint32_t i = 1; i < count && i < 16; ++i) {
            LightSample s{};
            if (!lights[i] || !ReadLightSampleSEH(lights[i], at, s)) { text += " [unreadable]"; continue; }
            text += fmt::format(" [r={:.0f} d={:.0f} rgb={:.2f}/{:.2f}/{:.2f} fade={:.3f} pos=({:.1f},{:.1f},{:.1f}) portal={} point={} faceKey={}]",
                s.radius, s.distance, s.r, s.g, s.b, s.fade, s.x, s.y, s.z, s.portalStrict, s.pointLight,
                faceKey.active && faceKey.bsLight == lights[i]);
        }
        logger::info("[MOS][SurfaceLights] player body draw lights={} at=({:.0f},{:.0f},{:.0f}) cell={:08X} facePercent={} faceKeyActive={}:{}",
            count - 1, at.x, at.y, at.z, cell, MirrorSurfaceLightPolicy::faceLightPercent.load(std::memory_order_relaxed),
            faceKey.active, text.empty() ? std::string(" none") : text);
    } catch (...) {
    }
}

[[nodiscard]] MirrorSurfaceLightPolicy::Point ToPoint(const RE::NiPoint3& p) noexcept
{
    return { p.x, p.y, p.z };
}

// Head of the hidden third-person body, the one the mirror shows.
[[nodiscard]] bool HeadPositionSEH(const RE::BSFixedString* name, RE::NiPoint3& out) noexcept
{
    __try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* root = player ? player->Get3D1(false) : nullptr;
        auto* head = root && name ? root->GetObjectByName(*name) : nullptr;
        if (!head) return false;
        out = head->world.translate;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void BeginFaceKeySEH(const RE::BSFixedString* name) noexcept
{
    __try {
        if (faceKey.active) return;
        // 0 on the MCM slider: the reflected face keeps the room's own light.
        const double intensity = MirrorSurfaceLightPolicy::FaceKeyIntensity();
        if (!(intensity > 0.0)) return;
        if (!privateFaceLight) { ++faceKeyNoLight; return; }
        std::array<float, 3> center{}, normal{};
        if (!SecondView::GetActiveMirrorPane(center, normal)) return;
        RE::NiPoint3 head{};
        if (!HeadPositionSEH(name, head)) { ++faceKeyNoHead; return; }
        const MirrorSurfaceLightPolicy::Point c{ center[0], center[1], center[2] };
        const MirrorSurfaceLightPolicy::Point n{ normal[0], normal[1], normal[2] };
        MirrorSurfaceLightPolicy::Point key{};
        if (!MirrorSurfaceLightPolicy::FaceKeyPosition(ToPoint(head), c, n, key)) return;
        auto* ni = privateFaceLight->light.get();
        auto& lightData = ni->GetLightRuntimeData();
        const RE::NiPoint3 to{ float(key.x), float(key.y), float(key.z) };
        const auto toFace = MirrorSurfaceLightPolicy::Minus(key, ToPoint(head));
        const double keyDistance = std::sqrt(MirrorSurfaceLightPolicy::Dot(toFace, toFace));
        lightData.fade = float(MirrorSurfaceLightPolicy::FaceKeyFade(keyDistance, lightData.radius.x, intensity));
        privateFaceLight->worldTranslate = to;
        ni->world.translate = to;
        ni->local.translate = to;
        faceKey.bsLight = privateFaceLight;
        faceKey.active = true;
        ++faceKeyLeases;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++faceKeyFaults;
    }
}

void EndFaceKeySEH() noexcept
{
    // There is no scene mutation to restore. The only exposed pointer was in
    // this synchronous draw's stack list; the private light is never a candidate
    // for scenery or a main-view draw, including nested dispatcher calls.
    faceKey.active = false;
    faceKey.bsLight = nullptr;
}

bool PortalMember(RE::BSLight* light, RE::BSShaderPropertyLightData* data,
    RE::BSGeometry* geometry, bool playerBody) noexcept
{
    if (!light->portalStrict) return true;
    // The player stands in the room the mirror is in; a light within reach of
    // the body is that room's light.
    if (playerBody) { ++playerPortalAdmits; return true; }
    // A radius alone must not admit a portal-restricted light through a wall.
    // Compare pointer values from native associations; never dereference a
    // stale light from a property's list or acquire a light keepalive.
    if (data && data->lights.size() <= 4096)
        for (auto* member : data->lights) if (member==light) return true;
    unsigned walked=0;
    for (const auto& member : light->geomList) {
        if (++walked>65536) return false;
        if (member.get()==geometry) return true;
    }
    return false;
}

// Per-capture light candidates (owner, 2026-09-17: "Cheaper per-draw work of
// our own"). Run 6 corrected ~30 million draws, and each one walked every
// active light -- reading the NiLight, its cull flag and radius -- and, for a
// portal-restricted light, that light's whole geometry list. Lights do not
// change inside one private pass, so the pass reads them once: position,
// radius, and a sorted copy of each portal-restricted light's geometry list
// for a binary search. The selection itself is unchanged (same influence, same
// membership, same ordering), so the reflection is lit exactly as before.
struct CachedLight
{
    RE::BSLight* light{};
    RE::NiLight* ni{};
    float x{}, y{}, z{}, radius{};
    bool portalStrict{}, affectLand{};
    std::uint32_t firstMember{}, endMember{};
};
thread_local std::vector<CachedLight> t_candidates;
thread_local std::vector<std::uintptr_t> t_members;
thread_local std::uint64_t t_candidateKey{ ~0ULL };
thread_local RE::ShadowSceneNode* t_candidateScene{};
thread_local bool t_candidatesValid{};
std::atomic<std::uint64_t> candidateBuilds{}, candidateFailures{};

// Core run 9 ("cache each object's light choice"): a static object in front of
// the same lights gets the same choice every capture. The choice is reused only
// while the scene's candidate set is the same -- light identities, positions and
// radii exactly, portal membership and land admission -- and the object's bound, material and
// light-data list are unchanged; every reused light is re-checked against this
// pass's candidates before it is handed to the engine. The reflected body is
// never cached (its face light moves for its draws).
thread_local std::uint64_t t_candidateSignature{};
thread_local bool t_anyPortalStrict{};
thread_local std::vector<std::uintptr_t> t_candidateLights; // sorted, this pass
struct CachedChoice
{
    std::uint64_t signature{}, dataHash{};
    RE::BSShaderProperty* property{};
    RE::BSShaderPropertyLightData* data{};
    RE::BSLight* sun{};
    float x{}, y{}, z{}, radius{};
    double reachScale{};
    std::int32_t maximum{};
    std::uint32_t count{};
    std::array<RE::BSLight*, 16> lights{};
};
thread_local MirrorBoundedCache<CachedChoice> t_choices;
thread_local RE::ShadowSceneNode* t_choiceScene{};
std::atomic<std::uint64_t> choiceHits{}, choiceMisses{};
thread_local MirrorSurfaceLightPolicy::ChoiceCacheAdmission t_choiceAdmission;
thread_local bool t_choiceStable{};
std::atomic<std::uint64_t> choiceStableCaptures{}, choiceChangingCaptures{};

[[nodiscard]] constexpr std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) noexcept
{
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6) + (hash >> 2);
    return hash;
}
[[nodiscard]] std::uint64_t ExactFloat(float value) noexcept
{
    return std::bit_cast<std::uint32_t>(value);
}
constexpr std::size_t kMaximumCandidates = 8192;
constexpr std::size_t kMaximumMembersPerLight = 65536;

[[nodiscard]] bool PushCandidate(const CachedLight& light) noexcept
{
    try { t_candidates.push_back(light); return true; } catch (...) { return false; }
}
[[nodiscard]] bool PushMember(std::uintptr_t member) noexcept
{
    try { t_members.push_back(member); return true; } catch (...) { return false; }
}
void SortMembers(std::uint32_t first, std::uint32_t end) noexcept
{
    std::sort(t_members.begin() + first, t_members.begin() + end);
}

// Sorted copy of a portal-restricted light's geometry list (no SEH here: the
// list iterator needs unwinding; the caller's handler covers a bad read).
[[nodiscard]] bool CollectMembers(RE::BSLight* light) noexcept
{
    std::size_t walked = 0;
    for (const auto& member : light->geomList) {
        if (++walked > kMaximumMembersPerLight) break;
        if (!PushMember(reinterpret_cast<std::uintptr_t>(member.get()))) return false;
    }
    return true;
}

[[nodiscard]] bool CollectCandidateSEH(RE::BSLight* light) noexcept
{
    __try {
        if (!light || !light->pointLight || !light->light) return true;
        auto* ni = light->light.get();
        if (ni->GetAppCulled()) return true;
        const auto& ld = ni->GetLightRuntimeData();
        if (MirrorSurfaceLightPolicy::DormantFaceEmitter(ld.fade, ld.radius.x,
                ld.diffuse.red, ld.diffuse.green, ld.diffuse.blue)) return true;
        const auto at = ni->world.translate;
        CachedLight entry{ light, ni, at.x, at.y, at.z, ld.radius.x, light->portalStrict, light->affectLand,
            static_cast<std::uint32_t>(t_members.size()), static_cast<std::uint32_t>(t_members.size()) };
        if (entry.portalStrict) {
            if (!CollectMembers(light)) return false;
            entry.endMember = static_cast<std::uint32_t>(t_members.size());
            SortMembers(entry.firstMember, entry.endMember);
        }
        return PushCandidate(entry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool CollectCandidatesSEH(RE::ShadowSceneNode* scene) noexcept
{
    __try {
        auto& runtime = scene->GetRuntimeData();
        if (runtime.activeLights.size() + runtime.activeShadowLights.size() > kMaximumCandidates) return false;
        for (const auto& light : runtime.activeLights)
            if (!CollectCandidateSEH(light.get())) return false;
        for (const auto& light : runtime.activeShadowLights)
            if (!CollectCandidateSEH(light.get())) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Valid candidates for the current private pass, built on first use.
[[nodiscard]] bool EnsureCandidates(RE::ShadowSceneNode* scene) noexcept
{
    const auto key = SecondView::PrivatePassSequence();
    if (key == t_candidateKey && scene == t_candidateScene) return t_candidatesValid;
    t_candidateKey = key;
    t_candidateScene = scene;
    t_candidates.clear();
    t_members.clear();
    try {
        t_candidates.reserve(256);
        t_members.reserve(4096);
    } catch (...) {
    }
    t_candidatesValid = scene && CollectCandidatesSEH(scene);
    ++(t_candidatesValid ? candidateBuilds : candidateFailures);
    t_candidateSignature = 0;
    t_anyPortalStrict = false;
    t_candidateLights.clear();
    if (t_candidatesValid && MirrorCaptureOptimizations::lightChoiceCache.load(std::memory_order_relaxed)) {
        std::uint64_t hash = 1469598103934665603ULL;
        try {
            t_candidateLights.reserve(t_candidates.size());
            for (const auto& c : t_candidates) {
                hash = Mix(hash, reinterpret_cast<std::uintptr_t>(c.light));
                hash = Mix(hash, reinterpret_cast<std::uintptr_t>(c.ni));
                hash = Mix(hash, ExactFloat(c.x));
                hash = Mix(hash, ExactFloat(c.y));
                hash = Mix(hash, ExactFloat(c.z));
                hash = Mix(hash, ExactFloat(c.radius));
                hash = Mix(hash, c.portalStrict);
                hash = Mix(hash, c.affectLand);
                hash = Mix(hash, c.endMember - c.firstMember);
                if (c.portalStrict) {
                    t_anyPortalStrict = true;
                    for (auto i = c.firstMember; i < c.endMember; ++i) hash = Mix(hash, t_members[i]);
                }
                t_candidateLights.push_back(reinterpret_cast<std::uintptr_t>(c.light));
            }
            std::sort(t_candidateLights.begin(), t_candidateLights.end());
            t_candidateSignature = hash ? hash : 1;
        } catch (...) {
            t_candidateSignature = 0;
            t_candidateLights.clear();
        }
    }
    if (scene != t_choiceScene) {
        t_choices.Clear();
        t_choiceScene = scene;
    }
    t_choiceStable = t_choiceAdmission.Observe(reinterpret_cast<std::uintptr_t>(scene), t_candidateSignature);
    if (t_candidateSignature) ++(t_choiceStable ? choiceStableCaptures : choiceChangingCaptures);
    return t_candidatesValid;
}

[[nodiscard]] bool CachedPortalMember(const CachedLight& c, RE::BSShaderPropertyLightData* data,
    RE::BSGeometry* geometry, bool playerBody) noexcept
{
    if (!c.portalStrict) return true;
    if (playerBody) { ++playerPortalAdmits; return true; }
    if (data && data->lights.size() <= 4096)
        for (auto* member : data->lights) if (member == c.light) return true;
    return std::binary_search(t_members.begin() + c.firstMember, t_members.begin() + c.endMember,
        reinterpret_cast<std::uintptr_t>(geometry));
}

std::uint32_t SelectRaw(RE::BSGeometry* geometry, RE::BSShaderPropertyLightData* data, RE::BSLight** lights,
    std::uint32_t count, std::int32_t maximum, std::int32_t* shadowCount,
    RE::ShadowSceneNode* scene, RE::BSShaderProperty* property,
    bool* useShadowSun, double reachScale = 1.0, bool playerBody = false)
{
    if (!geometry || !property || !scene || !lights || !shadowCount ||
        !useShadowSun || maximum<1 || maximum>16 || count<1 || count>unsigned(maximum)) {
        ++rejected; return count;
    }
    const auto& bound=geometry->worldBound;
    const MirrorSurfaceLightPolicy::Sphere receiver{
        bound.center.x,bound.center.y,bound.center.z,bound.radius};
    auto& runtime=scene->GetRuntimeData();
    if (!MirrorSurfaceLightPolicy::Valid(receiver) || runtime.activeLights.size()>4096 ||
        runtime.activeShadowLights.size()>4096) { ++rejected; return count; }
    MirrorSurfaceLightPolicy::Selection selected{};
    auto add=[&](RE::BSLight* light) {
        // These entries are owned by the current engine manager. In particular,
        // do not promote entries from shadowLightsAccum or a cached token list.
        if (!light || light==lights[0] || !light->pointLight || !light->light) return;
        auto* ni=light->light.get();
        if (ni->GetAppCulled()) return;
        const auto& ld=ni->GetLightRuntimeData();
        if (MirrorSurfaceLightPolicy::DormantFaceEmitter(ld.fade, ld.radius.x,
                ld.diffuse.red, ld.diffuse.green, ld.diffuse.blue)) return;
        const auto& pos=ni->world.translate;
        const double influence=MirrorSurfaceLightPolicy::Influence(receiver,
            {pos.x,pos.y,pos.z,ld.radius.x}, reachScale);
        if (influence<0 || !PortalMember(light,data,geometry,playerBody)) return;
        // Verified native affectLand/material predicate, not a spatial predicate.
        using Allows=bool(RE::BSShaderProperty*,RE::BSLight*);
        static REL::Relocation<Allows> allows{RELOCATION_ID(98902, 105550)};
        if (!allows(property,light)) return;
        selected.Add({reinterpret_cast<std::uintptr_t>(light),influence},unsigned(maximum-1));
    };
    if (MirrorCaptureOptimizations::lightCandidateCache.load(std::memory_order_relaxed) && EnsureCandidates(scene)) {
        using Allows=bool(RE::BSShaderProperty*,RE::BSLight*);
        static REL::Relocation<Allows> allows{RELOCATION_ID(98902, 105550)};
        const bool useChoice = t_choiceStable && t_candidateSignature != 0 && !playerBody && !faceKey.active &&
            MirrorCaptureOptimizations::lightChoiceCache.load(std::memory_order_relaxed);
        // Material/land admission can change without replacing the property.
        std::uint64_t dataHash = 0;
        if (useChoice) {
            dataHash = Mix(property->flags.underlying(), reinterpret_cast<std::uintptr_t>(property->material));
            if (property->material) dataHash = Mix(dataHash,
                static_cast<std::uint64_t>(property->material->GetFeature()));
        }
        if (useChoice && t_anyPortalStrict && data) {
            if (data->lights.size() > 4096) { ++rejected; return count; }
            dataHash = Mix(dataHash, data->lights.size());
            for (auto* member : data->lights) dataHash = Mix(dataHash, reinterpret_cast<std::uintptr_t>(member));
        }
        const auto key = reinterpret_cast<std::uintptr_t>(geometry);
        if (useChoice) {
            if (const auto* cached = t_choices.Find(key)) {
                const auto& choice = *cached;
                bool reusable = choice.signature == t_candidateSignature && choice.dataHash == dataHash &&
                    choice.property == property && choice.data == data && choice.sun == lights[0] &&
                    choice.x == bound.center.x && choice.y == bound.center.y && choice.z == bound.center.z &&
                    choice.radius == bound.radius && choice.reachScale == reachScale &&
                    choice.maximum == maximum && choice.count < unsigned(maximum);
                for (unsigned i = 0; reusable && i < choice.count; ++i)
                    reusable = std::binary_search(t_candidateLights.begin(), t_candidateLights.end(),
                        reinterpret_cast<std::uintptr_t>(choice.lights[i])) && allows(property, choice.lights[i]);
                if (reusable) {
                    for (unsigned i = 0; i < choice.count; ++i) lights[i + 1] = choice.lights[i];
                    *shadowCount = 0; *useShadowSun = false;
                    ++selections; ++choiceHits;
                    return choice.count + 1;
                }
            }
            ++choiceMisses;
        }
        for (const auto& c : t_candidates) {
            if (c.light == lights[0]) continue;
            const double influence = MirrorSurfaceLightPolicy::Influence(receiver, { c.x, c.y, c.z, c.radius }, reachScale);
            if (influence < 0 || !CachedPortalMember(c, data, geometry, playerBody)) continue;
            if (!allows(property, c.light)) continue;
            selected.Add({ reinterpret_cast<std::uintptr_t>(c.light), influence }, unsigned(maximum - 1));
        }
        if (useChoice) {
            CachedChoice choice{ t_candidateSignature, dataHash, property, data, lights[0],
                bound.center.x, bound.center.y, bound.center.z, bound.radius, reachScale, maximum,
                selected.count };
            for (unsigned i = 0; i < selected.count && i < choice.lights.size(); ++i)
                choice.lights[i] = reinterpret_cast<RE::BSLight*>(selected.entries[i].identity);
            (void)t_choices.Store(key, choice);
        }
    } else {
        for (const auto& light : runtime.activeLights) add(light.get());
        for (const auto& light : runtime.activeShadowLights) add(light.get());
    }
    // Commit only a completely constructed output. Keep the exact native sun;
    // private sunlight is supplied by MirrorSunShadows. Local shadow-map slots
    // belong to the main view and must not be forwarded into this camera.
    for (unsigned i=0;i<selected.count;++i)
        lights[i+1]=reinterpret_cast<RE::BSLight*>(selected.entries[i].identity);
    *shadowCount=0; *useShadowSun=false;
    ++selections;
    return selected.count+1;
}
}

bool Enabled() noexcept
{
    return enabled.load(std::memory_order_acquire) && !faulted.load(std::memory_order_relaxed) &&
        (!enbPath || MirrorENBScreenMask::Ready()) &&
        (!communityShadersPath || SecondView::CommunityShadersLightBridgeReady());
}

namespace
{
bool PrepareDrawImpl(const RE::BSRenderPass* pass, std::uint32_t technique, Draw& draw) noexcept;
}

bool PrepareDraw(const RE::BSRenderPass* pass, std::uint32_t technique, Draw& draw) noexcept
{
    // F8 breakdown: the light selection's share of this mod's per-draw work.
    if (!MirrorCaptureProfile::Active() || !MirrorCaptureProfile::InCapture())
        return PrepareDrawImpl(pass, technique, draw);
    const auto start = MirrorCaptureProfile::Ticks();
    const bool ready = PrepareDrawImpl(pass, technique, draw);
    MirrorCaptureProfile::AddTicks(MirrorCaptureProfile::Part::kLights, MirrorCaptureProfile::Ticks() - start);
    return ready;
}

namespace
{
bool PrepareDrawImpl(const RE::BSRenderPass* pass, std::uint32_t technique, Draw& draw) noexcept
{
    // The exterior auxiliary cycle (distant LOD) draws into the same reflected
    // view and otherwise keeps main-camera shadow-mask assignments.
    if (!Enabled() || !(SecondView::IsInsideMirrorPrimaryCapture() ||
                        SecondView::IsInsideMirrorAuxiliaryCapture())) return false;
    ++contexts;
    bool ready = false;
    __try {
        // Cached Effect passes have the same camera-dependent light selector
        // as Lighting. Correct lit Effects with their own native five-slot
        // contract; utility/depth, unlit effects and membranes stay untouched.
        if (!pass || !pass->shader || !pass->geometry || !pass->shaderProperty ||
            technique != pass->passEnum) return false;
        const auto shaderType = pass->shader->shaderType.underlying();
        const bool effect = shaderType == RE::BSShader::Type::Effect &&
            MirrorSurfaceLightPolicy::LitEffectTechnique(technique);
        const bool lighting = shaderType == RE::BSShader::Type::Lighting &&
            technique >= MirrorSurfaceLightPolicy::kLightingBase &&
            technique - MirrorSurfaceLightPolicy::kLightingBase < 0x14000000u;
        if (!effect && !lighting) return false;
        const bool playerBody = lighting && PlayerBodyGeometry(pass->geometry);
        if (!pass->numLights || !pass->sceneLights) {
            if (playerBody) ++playerNoLights;
            return false;
        }
        auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
        if (!scene) return false;
        auto& runtime = scene->GetRuntimeData();
        auto* sun = pass->sceneLights[0];
        if (!sun || (sun != runtime.sunLight &&
            sun != reinterpret_cast<RE::BSLight*>(runtime.sunShadowDirLight))) {
            if (playerBody) ++playerSunMismatch;
            return false;
        }
        auto* property = pass->shaderProperty;
        draw.lights[0] = sun;
        // The face key is private to the reflected player. In particular a
        // nested scenery draw must not inherit an outer player's active key.
        if (playerBody) BeginFaceKeySEH(HeadBoneName());
        const bool useFaceKey = playerBody && faceKey.active;
        std::int32_t shadowCount = -1;
        bool shadowSun = true;
        // Admit every light within twice its radius, on every path.
        //
        // Under Community Shaders this was always needed: its cluster lights are
        // neutralized for the draw, so the strict list has to carry everything
        // CS would otherwise have found, and CS's own attenuation weights it.
        //
        // Vanilla and ENB need it for a different reason. Influence() rejects a
        // light the moment its gap reaches its radius, and run 19 caught the
        // Sleeping Giant candle sitting exactly there: gap 449.7 against an
        // authored radius of 447.3. Moving the hand mirror a few units flips it
        // in and out of the set -- and what the player sees is not the candle's
        // contribution, which is nil at that distance, but numLights changing,
        // which changes the packed light-count bits and therefore the shader
        // permutation. On the alchemy set's env-mapped alpha-blended glass that
        // reads as the glass switching on and off (owner, run 21 and again on
        // AE run 2: "the glass of the alchemy set can still flicker on/off
        // depending on position of the hand mirror holding it high or low").
        //
        // Doubling the reach moves the boundary to where a light contributes
        // nothing at all, so crossing it cannot be seen, and it makes the cap
        // more likely to saturate -- a list pinned at its maximum cannot flap
        // at all. The cap itself stays as it was: vanilla packs count-1 into
        // three descriptor bits, so eight is a format limit, not a choice.
        using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
        const bool falloffEffect = effect && property->flags.any(ShaderFlag::kFalloff);
        const unsigned maximum = effect ? MirrorSurfaceLightPolicy::EffectMaximumLights(falloffEffect) :
            (communityShadersPath ? 16u : 8u);
        auto count = SelectRaw(pass->geometry, property->lightData, draw.lights, 1,
            static_cast<int>(maximum - unsigned(useFaceKey)), &shadowCount, scene, property, &shadowSun, 2.0, playerBody);
        if (shadowCount != 0 || shadowSun) { ++drawFallbacks; EndFaceKeySEH(); return false; }
        // Reserve one local slot so even a crowded room cannot evict the
        // owner's face slider. Keep sun at zero and the native pass unchanged.
        count = MirrorSurfaceLightPolicy::AppendFaceKey(draw.lights, count, maximum,
            useFaceKey ? privateFaceLight : nullptr);
        if (playerBody) {
            ++playerDraws;
            playerLights += count - 1;
            if (playerDraws.load(std::memory_order_relaxed) % 4096 == 1)
                SamplePlayerDraw(pass->geometry, draw.lights, count);
        }
        draw.pass = *pass;
        draw.pass.sceneLights = draw.lights;
        draw.pass.numLights = static_cast<std::uint8_t>(count);
        draw.pass.numShadowLights = 0;
        if (lighting) {
            draw.pass.extraParam = 0; // native main-view cascade membership
            draw.pass.passEnum = MirrorSurfaceLightPolicy::PrivateTechnique(
                technique, count, peerPacksLightCount, MirrorSunShadows::ShadowMaskActive());
            if ((technique - MirrorSurfaceLightPolicy::kLightingBase) & 0x61C0u) ++cachedMasks;
        } else {
            // No Lighting descriptor edits, face key, scene mutation or shader
            // replacement: native Effect SetupGeometry fills its own constants.
            ++effectDrawCorrections;
        }
        ++drawCorrections;
        ready = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { faulted.store(true); }
    return ready;
}
}

void EndDrawLeases() noexcept
{
    if (faceKey.active) EndFaceKeySEH();
}

void LogDiagnostics() noexcept
{
    if (!enabled.load()) return;
    logger::info("[MOS][SurfaceLights] contexts={} selections={} rejected={} drawCorrections={} cachedMasks={} drawFallbacks={} faulted={} player(draws/lights/portalAdmits/noLights/sunMismatch)={}/{}/{}/{}/{} faceKey(leases/noLight/noHead/faults)={}/{}/{}/{} candidates(builds/failures)={}/{} choices(hits/misses)={}/{} choiceCaptures(stable/changing)={}/{} privateFaceOnly=true effectDrawCorrections={}",
        contexts.load(), selections.load(), rejected.load(), drawCorrections.load(),
        cachedMasks.load(), drawFallbacks.load(), faulted.load(),
        playerDraws.load(), playerLights.load(), playerPortalAdmits.load(),
        playerNoLights.load(), playerSunMismatch.load(),
        faceKeyLeases.load(), faceKeyNoLight.load(), faceKeyNoHead.load(), faceKeyFaults.load(),
        candidateBuilds.load(), candidateFailures.load(), choiceHits.load(), choiceMisses.load(), choiceStableCaptures.load(), choiceChangingCaptures.load(), effectDrawCorrections.load());
}

void Install() noexcept
{
    if (enabled.load() || faulted.load()) return;
    const bool flat = (REL::Module::IsSE() && REL::Module::get().version()==REL::Version{1,5,97,0}) ||
        (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(REL::Module::get().version()));
    const bool enb = PeerDetection::ENBPresent();
    const MirrorSurfaceLightPolicy::Admission admission{
            .surfaceLights = HandMirrorSafetySettings::EmptyMarker(L"Data\\MirrorsOfSkyrim_SurfaceLights.enable"),
            .supportedFlat = flat,
            .communityShaders = PeerDetection::CommunityShadersPresent(),
            .shadowLimitFix = GetModuleHandleW(L"ShadowLimitFix.dll") != nullptr,
            .enbPresent = enb,
            .enbOptIn = HandMirrorSafetySettings::EmptyMarker(L"Data\\MirrorsOfSkyrim_ENBSurfaceLights.enable"),
            .enbMaskReady = MirrorENBScreenMask::Ready(),
            .communityShaderBridgeReady = SecondView::CommunityShadersLightBridgeReady()};
    if (!admission.surfaceLights) return;
    if (!MirrorSurfaceLightPolicy::Admit(admission)) {
        logger::info("[MOS][SurfaceLights] requested=true enabled=false supportedFlat={} communityShaders={} shadowLimitFix={} enb={} enbOptIn={} enbMaskReady={} csBridgeReady={}",
            admission.supportedFlat, admission.communityShaders, admission.shadowLimitFix,
            admission.enbPresent, admission.enbOptIn, admission.enbMaskReady, admission.communityShaderBridgeReady);
        return;
    }
    // Use the existing exact-byte-verified synchronous draw dispatcher. No
    // GetRenderPasses hook, pass-list rebuilding, or native cache writes.
    enbPath = enb;
    communityShadersPath = admission.communityShaders;
    peerPacksLightCount = ProbePeerPacksLightCount();
    const bool privateFaceReady = CreatePrivateFaceLightSEH();
    logger::info("[MOS][SurfaceLights] private face light ready={}; detached from world, allocated once, reflected player draws only", privateFaceReady);
    logger::info("[MOS][SurfaceLights] lighting pass builder slot: peerPacksLightCount={} (Community Shaders True PBR hook present)",
        peerPacksLightCount);
    enabled.store(HandMirrorSoftEffectBypass::EnsureInstalled(), std::memory_order_release);
    logger::info("[MOS][SurfaceLights] opt-in SE/AE draw correction enabled={} enb={} communityShaders={}; cached and rebuilt hand/standing lighting passes covered; native and shadow caster views unchanged",Enabled(),enbPath,communityShadersPath);
}
}
