#pragma once
#include "MirrorsOfSkyrimRecognition.h"
#include "MultiMirrorPolicy.h"
#include <atomic>
struct ID3D11DeviceContext;
namespace MirrorPaneDelivery { struct DeferredCaptureMainView; }

namespace MirrorOcclusionRuntime
{
    inline std::atomic_bool enabled{false};
    void Invalidate() noexcept;
    void FilterCaptureDemand(std::span<MirrorRecognition::ActiveMirror> mirrors,
        const DirectX::XMFLOAT3& origin,const DirectX::XMFLOAT3& forward,
        ID3D11DeviceContext* context) noexcept;
    void ObserveOpaqueMainDepth(const MirrorPaneDelivery::DeferredCaptureMainView& view) noexcept;
    bool AllowRetainedImage(MultiMirrorPolicy::Identity id,std::uint64_t frame) noexcept;
}
