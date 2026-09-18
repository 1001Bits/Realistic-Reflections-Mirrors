#pragma once
#include "MirrorPrivateShadow.h"
#include "MultiMirrorPolicy.h"
#include <vector>

namespace MirrorShadowBatch
{
    struct Work
    {
        MirrorPrivateShadow::Plan map{}, request{};
        MirrorShadowCasterVolume receiver{};
        MirrorShadowCasterSet casters;
    };
    class Planner
    {
        struct Entry
        {
            MultiMirrorPolicy::Identity identity{};
            std::uint64_t frame{};
            MirrorPrivateShadow::Plan plan{};
            MirrorShadowCasterVolume volume{};
        };
        std::vector<Entry> entries_;
        std::uint64_t epoch_{},frame_{};
        static bool Compatible(const MirrorPrivateShadow::Plan& a,const MirrorPrivateShadow::Plan& b) noexcept
        {
            return a.extent==b.extent && a.posePhase==b.posePhase && a.sceneryOnly==b.sceneryOnly;
        }
        static DirectX::XMFLOAT3 Coordinates(const MirrorPrivateShadow::Plan& plan) noexcept
        {
            using namespace DirectX;const auto p=XMLoadFloat3(&plan.eye);
            return {XMVectorGetX(XMVector3Dot(p,XMLoadFloat3(&plan.right))),
                XMVectorGetX(XMVector3Dot(p,XMLoadFloat3(&plan.up))),
                XMVectorGetX(XMVector3Dot(p,XMLoadFloat3(&plan.forward)))};
        }
        static bool Center(const MirrorPrivateShadow::Plan& current,const DirectX::XMFLOAT3& low,
            const DirectX::XMFLOAT3& high,MirrorPrivateShadow::Plan& result) noexcept
        {
            using namespace DirectX;
            // Preserve the exact light basis: normalizing an already normalized
            // direction again can change bits and defeat safe current-frame reuse.
            result=current;
            const float grid=16.0f*(2.0f*current.extent/current.resolution);
            const auto snap=[grid](float value){return std::floor(value/grid+.5f)*grid;};
            const float x=snap((low.x+high.x)*.5f),y=snap((low.y+high.y)*.5f),
                z=snap((low.z+high.z)*.5f+3*MirrorPrivateShadow::kExtent)-3*MirrorPrivateShadow::kExtent;
            XMStoreFloat3(&result.eye,XMLoadFloat3(&current.right)*x+
                XMLoadFloat3(&current.up)*y+XMLoadFloat3(&current.forward)*z);
            const XMMATRIX view{
                current.right.x,current.up.x,current.forward.x,0,
                current.right.y,current.up.y,current.forward.y,0,
                current.right.z,current.up.z,current.forward.z,0,-x,-y,-z,1};
            const auto projection=XMMatrixOrthographicLH(2*current.extent,2*current.extent,
                MirrorPrivateShadow::kNear,MirrorPrivateShadow::kDepth);
            const XMMATRIX texture{.5f,0,0,0,0,-.5f,0,0,0,0,1,0,.5f,.5f,0,1};
            XMStoreFloat4x4(&result.worldToTexture,view*projection*texture);
            return MirrorShadowCasterVolume::Finite(result.eye);
        }
    public:
        Work Prepare(MultiMirrorPolicy::Identity identity,std::uint64_t frame,std::uint64_t epoch,
            const MirrorPrivateShadow::Plan& current,const MirrorShadowCasterVolume& volume)
        {
            Work out;out.map=out.request=current;out.receiver=volume;out.casters.Reset(volume);
            if (epoch!=epoch_ || frame<frame_) { entries_.clear();epoch_=epoch; }
            frame_=frame;
            std::erase_if(entries_,[&](const Entry& e){return frame<e.frame || frame-e.frame>4;});
            if (!frame || MirrorPrivateShadow::Detail(current) || !volume.valid) return out;
            auto low=Coordinates(current),high=low;
            std::vector<MirrorPrivateShadow::Plan> selected;
            for (const auto& e:entries_) {
                if (e.identity==identity || !e.volume.valid || !Compatible(current,e.plan)) continue;
                auto predicted=e.plan;
                if (std::memcmp(&current.forward,&e.plan.forward,sizeof(DirectX::XMFLOAT3)*3)!=0) {
                    // Historical camera positions predict a group, not old sun
                    // pixels. Re-anchor them in this frame's exact light basis.
                    // Their collected hull remains honest; the current receiver
                    // must still pass full caster coverage before any reuse.
                    auto at=current;at.eye=e.volume.origin;
                    auto center=Coordinates(at);center.z-=3*MirrorPrivateShadow::kExtent;
                    if (!Center(current,center,center,predicted)) continue;
                }
                const auto point=Coordinates(predicted);
                DirectX::XMFLOAT3 lo{(std::min)(low.x,point.x),(std::min)(low.y,point.y),(std::min)(low.z,point.z)},
                    hi{(std::max)(high.x,point.x),(std::max)(high.y,point.y),(std::max)(high.z,point.z)};
                MirrorPrivateShadow::Plan candidate;
                if (!Center(current,lo,hi,candidate) || !MirrorPrivateShadow::Covers(candidate,current) ||
                    !MirrorPrivateShadow::Covers(candidate,predicted) ||
                    !std::all_of(selected.begin(),selected.end(),[&](const auto& prior){return MirrorPrivateShadow::Covers(candidate,prior);})) continue;
                if (!out.casters.Add(e.volume)) continue;
                selected.push_back(predicted);out.map=candidate;low=lo;high=hi;
            }
            auto found=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& e){return e.identity==identity;});
            Entry next{identity,frame,current,volume};
            if (found!=entries_.end()) *found=next;else entries_.push_back(next);
            return out;
        }
        std::size_t Size() const noexcept { return entries_.size(); }
    };
}
