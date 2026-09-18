#include "PCH.h"
#include "MirrorScenePruning.h"
#include "MirrorScenePruningPolicy.h"

namespace MirrorScenePruning
{
    namespace
    {
        using Cull=void(*)(RE::NiAVObject*,RE::NiCullingProcess*,std::int32_t);
        Cull original{};
        std::uintptr_t patchedTarget{};
        std::array<std::uint8_t,29> patchedBytes{};
        std::atomic_bool installed{false};
        std::atomic_uint64_t epoch{1};
        struct State
        {
            MirrorRenderDistance::Scope distance{};
            const MirrorShadowCasterSet* casters{};
            RE::NiAVObject* scene{};RE::NiAVObject* sky{};
            std::uint64_t visited{},distanceRejected{},casterRejected{};
            std::uint64_t epoch{};
            bool active{},skyPass{};
        };
        thread_local State state;
        bool Reject(RE::NiAVObject* object) noexcept
        {
            __try {
                if (!object || object==state.scene || object==state.sky) return false;
                const auto& b=object->worldBound;
                // Unknown structural/animation bounds are not pruning authority.
                const Filter filter{state.distance,state.casters,state.active};
                const bool outside=filter.Outside({b.center.x,b.center.y,b.center.z},b.radius);
                if (!outside) return false;
                // Skinned/actor bounds can be refreshed by native collection.
                // Keep that path, and never prune an unowned aggregate root.
                auto* geometry=object->AsGeometry();
                if (geometry && geometry->GetGeometryRuntimeData().skinInstance) return false;
                if (geometry && RetainsRegardlessOfDistance(geometry)) return false;
                if (!geometry && !object->AsNode()) return false; // lights/cameras retain native handling
                auto* root=object;unsigned depth=0;
                while (root && root->parent && depth++<64) root=root->parent;
                if (!root || root->parent) return false;
                auto* reference=object->GetUserData();
                if ((!geometry && !reference) || (reference && reference->GetFormType()==RE::FormType::ActorCharacter)) return false;
                if (!geometry) {
                    auto* base=reference->GetBaseObject();
                    if (!base) return false;
                    const auto kind=base->GetFormType();
                    // Limit whole-branch rejection to scenery. Light/effect and
                    // other object roots still reach native collection; their
                    // individual eligible meshes can use the geometry filter.
                    if (kind!=RE::FormType::Static && kind!=RE::FormType::MovableStatic &&
                        kind!=RE::FormType::Tree && kind!=RE::FormType::Flora) return false;
                }
                if (state.casters) {
                    ++state.casterRejected;return true;
                } else {
                    ++state.distanceRejected;return true;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            return false;
        }
        void Hook(RE::NiAVObject* object,RE::NiCullingProcess* culler,std::int32_t alphaGroup)
        {
            if (!state.active || state.skyPass || state.epoch!=epoch.load(std::memory_order_relaxed)) { original(object,culler,alphaGroup);return; }
            ++state.visited;
            if (Reject(object)) return;
            const bool sky=object && object==state.sky;
            if (sky) state.skyPass=true;
            __try { original(object,culler,alphaGroup); }
            __finally { if (sky) state.skyPass=false; }
        }
        bool Exact(std::uintptr_t target,std::span<const std::uint8_t> bytes) noexcept
        {
            __try { return target && !bytes.empty() && std::memcmp(reinterpret_cast<const void*>(target),bytes.data(),bytes.size())==0; }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        RE::NiAVObject* SkyRoot() noexcept
        {
            __try { auto* tes=RE::TES::GetSingleton();return tes && tes->sky && tes->sky->root ? tes->sky->root.get() : nullptr; }
            __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
        }
    }
    bool Install(std::uintptr_t target,std::span<const std::uint8_t> expected) noexcept
    {
        if (installed.load()) return OwnsHook(target);
        if (expected.size()!=patchedBytes.size() || !Exact(target,expected)) return false;
        original=reinterpret_cast<Cull>(target);
        if (DetourTransactionBegin()!=NO_ERROR) return false;
        if (DetourUpdateThread(GetCurrentThread())!=NO_ERROR ||
            DetourAttach(reinterpret_cast<PVOID*>(&original),reinterpret_cast<PVOID>(Hook))!=NO_ERROR) {
            DetourTransactionAbort();return false;
        }
        const bool ready=DetourTransactionCommit()==NO_ERROR;
        if (ready) { patchedTarget=target;std::memcpy(patchedBytes.data(),reinterpret_cast<const void*>(target),patchedBytes.size()); }
        installed.store(ready,std::memory_order_release);return ready;
    }
    bool RetainsRegardlessOfDistance(RE::BSGeometry* geometry) noexcept
    {
        __try {
            if (!geometry) return false;
            auto* property=geometry->lightingShaderProp_cast();
            auto* material=property ? property->GetBaseMaterial() : nullptr;
            if (!material) return false;
            using Feature=RE::BSShaderMaterial::Feature;
            const auto feature=material->GetFeature();
            if (static_cast<std::uint32_t>(feature)==13u) return true;  // LODObjects (unnamed in CommonLib)
            switch (feature) {
            case Feature::kMultiTexLand: case Feature::kLODLand: case Feature::kLODLandNoise:
            case Feature::kMultiTexLandLODBlend: case Feature::kLODObjectsHD:
                return true;
            default: return false;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Enabled() noexcept { return installed.load(std::memory_order_acquire); }
    bool OwnsHook(std::uintptr_t target) noexcept { return Enabled() && target==patchedTarget && Exact(target,patchedBytes); }
    bool Begin(MirrorRenderDistance::Scope distance,const MirrorShadowCasterSet* casters,RE::NiAVObject* scene) noexcept
    {
        if (!Enabled() || state.active || (!distance.distance && (!casters || casters->Unpruned()))) return false;
        state={};state.distance=distance;state.casters=casters;state.scene=scene;state.sky=SkyRoot();
        state.epoch=Epoch();
        // Without the authoritative sky root, retain the existing draw-time path.
        if (!casters && !state.sky) return false;
        state.active=true;return true;
    }
    void End() noexcept
    {
        if (!state.active) return;
        state.active=false;state.skyPass=false;state.casters=nullptr;
        static std::uint64_t seen{},distance{},casters{},last{};
        seen+=state.visited;distance+=state.distanceRejected;casters+=state.casterRejected;
        const auto now=GetTickCount64();
        if (now-last<5000) return;last=now;
        try { logger::info("[MOS][ScenePruning] visited={} distanceRejected={} casterRejected={}",seen,distance,casters); }
        catch (...) {}
    }
    void Invalidate() noexcept { epoch.fetch_add(1,std::memory_order_release); }
    std::uint64_t Epoch() noexcept { return epoch.load(std::memory_order_acquire); }
}
