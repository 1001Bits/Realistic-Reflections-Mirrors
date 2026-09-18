#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

// ENB's screen mask uses integer SV_POSITION loads. A 1x1 neutral texture
// would still return zero outside (0,0); cover the whole private target.
namespace MirrorENBScreenMaskD3D
{
    inline constexpr UINT kSlot = 37;
    inline constexpr UINT kExtent = 4096;
    using DrawIndexed = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);

    inline HRESULT CreateNeutral(ID3D11Device* device, ID3D11ShaderResourceView** output)
    {
        if (!device || !output) return E_INVALIDARG;
        *output = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = kExtent;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kExtent) * kExtent, 0xFFFFFFFF);
        const D3D11_SUBRESOURCE_DATA initial{ pixels.data(), kExtent * 4, 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        const auto hr = device->CreateTexture2D(&desc, &initial, &texture);
        return FAILED(hr) ? hr : device->CreateShaderResourceView(texture.Get(), nullptr, output);
    }

    struct Result
    {
        bool targetMatched{}, maskBound{}, drawCompleted{}, restored{};
    };

    // Raw SEH scopes deliberately contain no C++ objects with destructors.
    // Each getter reference and the original binding survive a faulting draw;
    // the exception propagates to the existing second-view fault boundary.
    inline void Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* expected,
        ID3D11ShaderResourceView* neutral, DrawIndexed originalDraw,
        UINT count, UINT start, INT base, Result& result)
    {
        ID3D11RenderTargetView* actual{};
        ID3D11ShaderResourceView* previous{};
        ID3D11ShaderResourceView* observed{};
        bool bindingTouched = false;
        __try {
            context->OMGetRenderTargets(1, &actual, nullptr);
            result.targetMatched = actual && actual == expected;
            if (result.targetMatched) context->PSGetShaderResources(kSlot, 1, &previous);
            __try {
                if (previous && neutral && previous != neutral) {
                    bindingTouched = true;
                    context->PSSetShaderResources(kSlot, 1, &neutral);
                    result.maskBound = true;
                }
                originalDraw(context, count, start, base);
                result.drawCompleted = true;
            } __finally {
                if (bindingTouched) {
                    context->PSSetShaderResources(kSlot, 1, &previous);
                    context->PSGetShaderResources(kSlot, 1, &observed);
                    result.restored = observed == previous;
                }
            }
        } __finally {
            // Nested cleanup ensures a failed release cannot skip the rest.
            __try { if (observed) observed->Release(); }
            __finally {
                __try { if (previous) previous->Release(); }
                __finally { if (actual) actual->Release(); }
            }
        }
    }
}
