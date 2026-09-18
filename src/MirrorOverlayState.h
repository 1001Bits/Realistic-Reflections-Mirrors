#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>

namespace MirrorOverlay
{
// Separate D3D11.1 context state preserves the complete game pipeline,
// including UAVs, predication and all shader stages. No backbuffer is retained.
class State
{
public:
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() { End(); }
    bool Begin(ID3D11DeviceContext* context, ID3D11RenderTargetView* target) noexcept
    {
        if (previous_ || !context || !target || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
        if (source_.Get() != context) {
            menu_.Reset(); context_.Reset(); device_.Reset(); source_ = context;
            context->GetDevice(device_.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            if (!device_ || FAILED(device_.As(&device1)) || FAILED(source_.As(&context_))) return false;
            const auto level = device_->GetFeatureLevel();
            const UINT flags = (device_->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ?
                D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
            if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
                __uuidof(ID3D11Device1), nullptr, menu_.GetAddressOf()))) return false;
        }
        if (!context_ || !menu_) return false;
        Microsoft::WRL::ComPtr<ID3D11Device> targetDevice; target->GetDevice(&targetDevice);
        if (targetDevice.Get() != device_.Get()) return false;
        context_->SwapDeviceContextState(menu_.Get(), previous_.GetAddressOf());
        context_->OMSetRenderTargets(1, &target, nullptr);
        return true;
    }
    void End() noexcept
    {
        if (!previous_) return;
        // Inactive states retain bindings: clear ours before swapping back, so
        // neither ResizeBuffers nor the next game frame sees an overlay alias.
        context_->ClearState();
        context_->SwapDeviceContextState(previous_.Get(), nullptr);
        previous_.Reset();
    }
private:
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> source_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> menu_, previous_;
};
}
