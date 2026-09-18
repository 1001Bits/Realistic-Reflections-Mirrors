#pragma once

#include "MultiMirrorPolicy.h"
#include <DirectXMath.h>
#include <array>
#include <bit>
#include <cmath>

namespace MirrorOcclusion
{
    // Delayed GPU results are hints, never a reason to stall a frame or retire a mirror.
    inline constexpr std::size_t kQuerySlots = 3;
    inline constexpr std::uint64_t kMaximumAgeFrames = 4;
    inline constexpr std::uint64_t kMaximumAgeMilliseconds = 500;
    struct Bounds
    {
        DirectX::XMFLOAT3 center{};
        std::array<DirectX::XMFLOAT3, 3> extents{};
    };
    struct Pose
    {
        DirectX::XMFLOAT3 origin{}, forward{};
    };
    struct Rectangle
    {
        float left{}, bottom{}, right{}, top{}, depth{};
        bool valid{}, outside{};
    };
    inline bool Finite(float x) noexcept
    {
        return (std::bit_cast<std::uint32_t>(x) & 0x7F800000u) != 0x7F800000u;
    }
    inline bool Finite(const DirectX::XMFLOAT3& p) noexcept
    {
        return Finite(p.x) && Finite(p.y) && Finite(p.z);
    }
    inline bool Same(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
    {
        return Finite(a) && Finite(b) && a.x == b.x && a.y == b.y && a.z == b.z;
    }
    inline bool Same(const Bounds& a, const Bounds& b) noexcept
    {
        return Same(a.center,b.center) && Same(a.extents[0],b.extents[0]) &&
            Same(a.extents[1],b.extents[1]) && Same(a.extents[2],b.extents[2]);
    }
    inline bool Nearby(const Pose& a, const Pose& b) noexcept
    {
        if (!Finite(a.origin) || !Finite(b.origin) || !Finite(a.forward) || !Finite(b.forward)) return false;
        const float x=a.origin.x-b.origin.x, y=a.origin.y-b.origin.y, z=a.origin.z-b.origin.z;
        const float aa=a.forward.x*a.forward.x+a.forward.y*a.forward.y+a.forward.z*a.forward.z;
        const float bb=b.forward.x*b.forward.x+b.forward.y*b.forward.y+b.forward.z*b.forward.z;
        const float dot=a.forward.x*b.forward.x+a.forward.y*b.forward.y+a.forward.z*b.forward.z;
        // A quick pan, teleport or changed view must wake immediately. Ordinary walking
        // can consume recent depth evidence, with a fresh probe submitted every frame.
        return Finite(aa) && Finite(bb) && aa > 0.99f && aa < 1.01f && bb > 0.99f && bb < 1.01f &&
            Finite(dot) && dot > 0.9962f && x*x+y*y+z*z <= 32.0f*32.0f;
    }

    inline Rectangle Project(const Bounds& bounds, const Pose& pose,
        const DirectX::XMFLOAT4X4& matrix, float width, float height) noexcept
    {
        Rectangle result{};
        if (!Finite(bounds.center) || !Finite(pose.origin) || !Finite(width) || !Finite(height) ||
            width < 1 || height < 1) return result;
        for (const auto& row : matrix.m) for (const float x : row) if (!Finite(x)) return result;
        for (const auto& extent : bounds.extents) if (!Finite(extent)) return result;
        float left=1.0e30f, bottom=1.0e30f, right=-1.0e30f, top=-1.0e30f, depth=1.0f;
        for (unsigned i=0; i<8; ++i) {
            DirectX::XMFLOAT3 p{bounds.center.x-pose.origin.x,bounds.center.y-pose.origin.y,bounds.center.z-pose.origin.z};
            for (unsigned axis=0; axis<3; ++axis) {
                const float sign=(i & (1u<<axis)) ? 1.0f : -1.0f;
                p.x+=sign*bounds.extents[axis].x; p.y+=sign*bounds.extents[axis].y; p.z+=sign*bounds.extents[axis].z;
            }
            DirectX::XMFLOAT4 clip{};
            DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(
                DirectX::XMVectorSet(p.x,p.y,p.z,1),DirectX::XMLoadFloat4x4(&matrix)));
            if (!Finite(clip.x) || !Finite(clip.y) || !Finite(clip.z) || !Finite(clip.w) ||
                clip.w <= 0.001f || clip.z <= 0) return {}; // camera/near-plane intersection: keep capturing
            const float x=clip.x/clip.w, y=clip.y/clip.w, z=clip.z/clip.w;
            if (!Finite(x) || !Finite(y) || !Finite(z)) return {};
            left=(std::min)(left,x); right=(std::max)(right,x);
            bottom=(std::min)(bottom,y); top=(std::max)(top,y); depth=(std::min)(depth,z);
        }
        // Cover the whole projected bound at its NEAREST depth. Padding and a front
        // bias favour extra rendering over dropping any visible corner/doorway sliver.
        const float padX=8.0f/width, padY=8.0f/height;
        left-=padX; right+=padX; bottom-=padY; top+=padY;
        result.valid=true;
        result.outside=right <= -1 || left >= 1 || top <= -1 || bottom >= 1;
        result.left=(std::max)(-1.0f,left); result.right=(std::min)(1.0f,right);
        result.bottom=(std::max)(-1.0f,bottom); result.top=(std::min)(1.0f,top);
        result.depth=(std::max)(0.0f,(std::min)(depth,1.0f)-0.000004f);
        return result;
    }

    struct Evidence
    {
        std::uint64_t frame{}, milliseconds{}, samples{};
        Pose pose{};
        Bounds bounds{};
        bool valid{};
        bool Occluded(const Bounds& currentBounds, const Pose& currentPose,
            std::uint64_t currentFrame, std::uint64_t now) const noexcept
        {
            return valid && samples == 0 && frame != 0 && currentFrame > frame &&
                currentFrame-frame <= kMaximumAgeFrames && now >= milliseconds &&
                now-milliseconds <= kMaximumAgeMilliseconds &&
                Same(bounds,currentBounds) && Nearby(pose,currentPose);
        }
    };
}
