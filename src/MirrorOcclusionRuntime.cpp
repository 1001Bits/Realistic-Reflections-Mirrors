#include "PCH.h"
#include "MirrorOcclusionRuntime.h"
#include "MirrorOcclusionQueries.h"
#include "MirrorsOfSkyrimPaneDelivery.h"
#include "MirrorFleetPolicy.h"
#include "MirrorPerformance.h"

namespace MirrorOcclusionRuntime
{
    namespace
    {
        struct State
        {
            MirrorOcclusion::Queries queries;
            std::vector<MirrorOcclusion::Probe> probes;
            std::uint64_t epoch{}, frame{}, submitted{}, reportTime{};
        };
        // Main render thread owns all COM/query state; load messages only revoke an
        // atomic epoch, never release resources concurrently with a GPU submission.
        std::unique_ptr<State> state;
        std::atomic_uint64_t epoch{1};
        std::atomic_bool faulted{false};
        DWORD ownerThread{};
        std::uint64_t Now() noexcept { return GetTickCount64(); }
        void Fail() noexcept
        {
            if (!faulted.exchange(true,std::memory_order_acq_rel)) {
                try { logger::error("[MOS][Occlusion] depth-query fault; optimization disabled, full mirror captures remain eligible"); }
                catch (...) {}
            }
        }
        bool Enabled() noexcept
        {
            return enabled.load(std::memory_order_relaxed) && !faulted.load(std::memory_order_relaxed) &&
                MirrorFleetPolicy::enabled.load(std::memory_order_relaxed) && MirrorPerformance::RenderingEnabled();
        }
        bool Current() noexcept
        {
            return Enabled() && state && ownerThread==GetCurrentThreadId() && state->epoch==epoch.load(std::memory_order_acquire);
        }
        MirrorOcclusion::Probe ToProbe(const MirrorRecognition::ActiveMirror& mirror) noexcept
        {
            MirrorOcclusion::Probe probe{{mirror.formID,mirror.candidateGeneration},{}};
            probe.bounds.center=mirror.plane.center;
            for (unsigned axis=0;axis<3;++axis) {
                const auto& v=mirror.worldAxes[axis]; const auto e=mirror.worldHalfExtents[axis];
                probe.bounds.extents[axis]={v.x*e,v.y*e,v.z*e};
            }
            return probe;
        }
        bool ForwardDepth(const MirrorPaneDelivery::DeferredCaptureMainView& view) noexcept
        {
            // Establish increasing D3D depth along the actual frozen camera forward.
            // An unsupported/reversed projection simply forgoes the optimization.
            const auto& f=view.sourceCamera.forward;
            float previous=-1;
            for (float distance:{64.0f,1024.0f}) {
                DirectX::XMFLOAT4 clip{};
                DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(
                    DirectX::XMVectorSet(f.x*distance,f.y*distance,f.z*distance,1),
                    DirectX::XMLoadFloat4x4(&view.viewProjection)));
                if (!MirrorOcclusion::Finite(clip.w) || !MirrorOcclusion::Finite(clip.z) || clip.w<=0) return false;
                const float depth=clip.z/clip.w;
                if (!MirrorOcclusion::Finite(depth) || depth<=previous || depth<0 || depth>1) return false;
                previous=depth;
            }
            return true;
        }
        void Report() noexcept
        {
            if (!state || Now()-state->reportTime<5000) return;
            state->reportTime=Now(); const auto& c=state->queries.Stats();
            try {
                logger::info("[MOS][Occlusion] retained={} probes={} visibleResults={} hiddenResults={} pending={} unavailable={} rejected={} skippedCaptures={} discarded={} queryCreations={} faulted={}",
                    state->queries.Size(),c.issued,c.visible,c.hidden,c.pending,c.unavailable,c.rejected,c.skipped,c.discarded,c.queryCreations,faulted.load());
            } catch (...) {}
        }
        void FilterCpp(std::span<MirrorRecognition::ActiveMirror> mirrors,const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& forward,ID3D11DeviceContext* context) noexcept
        {
            try {
                if (!Enabled()) return;
                const auto currentEpoch=epoch.load(std::memory_order_acquire);
                if (!ownerThread) ownerThread=GetCurrentThreadId();
                if (ownerThread!=GetCurrentThreadId()) return;
                if (!state || state->epoch!=currentEpoch) {
                    state=std::make_unique<State>(); state->epoch=currentEpoch;
                }
                const auto frame=MirrorPaneDelivery::UpcomingMainWorldFrame();
                if (!frame || frame==state->frame) return;
                state->probes.clear(); state->probes.reserve(mirrors.size());
                for (const auto& mirror:mirrors) state->probes.push_back(ToProbe(mirror));
                state->queries.Reconcile(state->probes);
                state->queries.Poll(context);
                if (state->queries.Faulted()) { Fail(); return; }
                state->frame=frame;
                const MirrorOcclusion::Pose pose{origin,forward}; const auto now=Now();
                for (std::size_t i=0;i<mirrors.size();++i) {
                    const bool hidden=state->queries.Occluded(state->probes[i],pose,frame,now);
                    if (hidden) mirrors[i].captureRequested=false;
                }
                Report();
            } catch (...) { Fail(); }
        }
        void ObserveCpp(const MirrorPaneDelivery::DeferredCaptureMainView& view) noexcept
        {
            try {
                if (!Current() || state->probes.empty()) return;
                // Called synchronously with the authoritative M5 raster observation:
                // opaque world depth is complete, before water/transparent passes can
                // write depth for surfaces through which the pane may remain visible.
                if (view.mainWorldFrame==state->submitted) return;
                if (!MirrorPaneDelivery::IsInsideMainWorld() || !view.valid || !view.retainedTarget || view.handPaneView ||
                    !view.sourceCamera.rasterAuthoritative || view.mainWorldFrame!=state->frame ||
                    !ForwardDepth(view)) { state->queries.InvalidateEvidence(); return; }
                state->submitted=view.mainWorldFrame;
                const auto& target=view.retainedTarget;
                // No native scene/light state is touched, and the DSV is read-only.
                if (!state->queries.Submit(target->device,target->context,target->depthDSV,view.viewport,
                    view.viewProjection,{view.origin,view.sourceCamera.forward},view.mainWorldFrame,Now(),state->probes)) {
                    // Submit revokes completed and pending evidence on failure. Keep
                    // the allocation cache and report clock: unsupported CS targets
                    // otherwise rebuilt State and logged once per frame indefinitely.
                    if (state->queries.Faulted()) Fail();
                }
            } catch (...) { Fail(); }
        }
    }
    void Invalidate() noexcept { epoch.fetch_add(1,std::memory_order_release); }
    void FilterCaptureDemand(std::span<MirrorRecognition::ActiveMirror> mirrors,
        const DirectX::XMFLOAT3& origin,const DirectX::XMFLOAT3& forward,ID3D11DeviceContext* context) noexcept
    {
        __try { FilterCpp(mirrors,origin,forward,context); }
        __except(EXCEPTION_EXECUTE_HANDLER) { Fail(); }
    }
    void ObserveOpaqueMainDepth(const MirrorPaneDelivery::DeferredCaptureMainView& view) noexcept
    {
        __try { ObserveCpp(view); }
        __except(EXCEPTION_EXECUTE_HANDLER) { Fail(); }
    }
    bool AllowRetainedImage(MultiMirrorPolicy::Identity id,std::uint64_t frame) noexcept
    {
        return Current() && state->queries.RetainedImage(id,frame);
    }
}
