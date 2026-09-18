#pragma once
#include "MirrorOverlayState.h"
#include <span>
#include <string_view>

namespace MirrorOverlay
{
class Renderer
{
public:
    bool Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        unsigned width, unsigned height, std::span<const std::string_view> lines) noexcept;
private:
    bool Prepare(ID3D11Device* device);
    State state_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> font_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
    bool ready_{}, attempted_{};
};
}
