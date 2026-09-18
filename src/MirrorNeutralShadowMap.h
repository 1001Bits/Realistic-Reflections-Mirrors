#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace MirrorShadowSettings
{
// Zero projection yields no covered cascade in the existing receiver shader;
// white moments also neutralize the ordinary CS sunlight term. No new shader
// variant, shadow render or per-frame upload is needed for shadows OFF.
class NeutralMap
{
public:
    bool Prepare(ID3D11Device* device)
    {
        if(!device) return false;
        if(device_.Get()==device && depth_ && matrices_) return true;
        *this={};device_=device;
        constexpr std::uint16_t white[]{65535,65535,65535,65535,65535,65535,65535,65535};
        D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=texture.MipLevels=2;
        texture.ArraySize=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R16G16_UNORM;
        texture.Usage=D3D11_USAGE_IMMUTABLE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels[]{{white,8,0},{white,4,0}};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
        if(FAILED(device->CreateTexture2D(&texture,pixels,&image)) ||
            FAILED(device->CreateShaderResourceView(image.Get(),nullptr,&depth_))) { *this={};return false; }
        // Existing SunData structured element: two projection matrices, two
        // inverses and four fade floats, all zero and never updated.
        constexpr float zero[68]{};
        D3D11_BUFFER_DESC data{};data.ByteWidth=data.StructureByteStride=sizeof(zero);
        data.Usage=D3D11_USAGE_IMMUTABLE;data.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        data.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        D3D11_SUBRESOURCE_DATA initial{zero,0,0};
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if(FAILED(device->CreateBuffer(&data,&initial,&buffer)) ||
            FAILED(device->CreateShaderResourceView(buffer.Get(),nullptr,&matrices_))) { *this={};return false; }
        return true;
    }
    ID3D11ShaderResourceView* Depth() const noexcept {return depth_.Get();}
    ID3D11ShaderResourceView* Matrices() const noexcept {return matrices_.Get();}
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth_,matrices_;
};
}
