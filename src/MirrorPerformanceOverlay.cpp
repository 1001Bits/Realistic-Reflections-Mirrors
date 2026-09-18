#include "MirrorPerformanceOverlay.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <vector>

namespace MirrorOverlay
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr unsigned kWidth=512, kHeight=192, kCell=32, kMaxVertices=16384, kMaxLines=24;
struct Vertex { float x,y,u,v; std::uint32_t color; };
struct FontBitmap
{
    HDC dc{}; HBITMAP bitmap{}; HFONT font{}; HGDIOBJ oldBitmap{},oldFont{};
    ~FontBitmap()
    {
        if(dc && oldFont) SelectObject(dc,oldFont);
        if(dc && oldBitmap) SelectObject(dc,oldBitmap);
        if(font) DeleteObject(font);
        if(bitmap) DeleteObject(bitmap);
        if(dc) DeleteDC(dc);
    }
};
bool MakeFont(ID3D11Device* device, ID3D11ShaderResourceView** result)
{
    FontBitmap g;
    g.dc=CreateCompatibleDC(nullptr); if(!g.dc) return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=kWidth;info.bmiHeader.biHeight=-int(kHeight);
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::uint32_t* pixels{};
    g.bitmap=CreateDIBSection(g.dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
    g.font=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
    if(!g.bitmap || !g.font || !pixels) return false;
    g.oldBitmap=SelectObject(g.dc,g.bitmap);g.oldFont=SelectObject(g.dc,g.font);
    if(!g.oldBitmap || !g.oldFont || g.oldBitmap==HGDI_ERROR || g.oldFont==HGDI_ERROR) return false;
    std::memset(pixels,0,kWidth*kHeight*4);
    SetBkMode(g.dc,TRANSPARENT);SetTextColor(g.dc,RGB(255,255,255));
    for(unsigned ch=33;ch<127;++ch) {
        const auto index=ch-32;const wchar_t glyph=static_cast<wchar_t>(ch);
        if(!TextOutW(g.dc,(index%16)*kCell,(index/16)*kCell,&glyph,1)) return false;
    }
    GdiFlush();
    std::vector<std::uint8_t> alpha(kWidth*kHeight);
    for(unsigned i=0;i<alpha.size();++i) alpha[i]=static_cast<std::uint8_t>(
        (std::max)({pixels[i]&255,(pixels[i]>>8)&255,(pixels[i]>>16)&255}));
    alpha[0]=255; // A solid texel for the panel; spaces do not draw glyph quads.
    D3D11_TEXTURE2D_DESC d{};d.Width=kWidth;d.Height=kHeight;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8_UNORM;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{alpha.data(),kWidth,0};ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device->CreateTexture2D(&d,&initial,&texture)) &&
        SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,result));
}
}

bool Renderer::Prepare(ID3D11Device* device)
{
    if(attempted_) return ready_ && device_.Get()==device;
    attempted_=true;device_=device;
    constexpr char source[]=R"(
struct V {float2 position:POSITION; float2 uv:TEXCOORD0; float4 color:COLOR0;};
struct P {float4 position:SV_Position; float2 uv:TEXCOORD0; float4 color:COLOR0;};
P VS(V v) {P p;p.position=float4(v.position,0,1);p.uv=v.uv;p.color=v.color;return p;}
Texture2D<float> Atlas:register(t0);SamplerState FontSampler:register(s0);
float4 PS(P p):SV_Target {return float4(p.color.rgb,p.color.a*Atlas.Sample(FontSampler,p.uv));}
)";
    ComPtr<ID3DBlob> vs,ps;
    if(FAILED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,nullptr)) ||
       FAILED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,nullptr))) return false;
    constexpr D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertexShader_)) ||
       FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixelShader_)) ||
       FAILED(device->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_)) ||
       !MakeFont(device,&font_)) return false;
    D3D11_BUFFER_DESC b{};b.ByteWidth=kMaxVertices*sizeof(Vertex);b.Usage=D3D11_USAGE_DYNAMIC;
    b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;
    rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=FALSE;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    ready_=SUCCEEDED(device->CreateBuffer(&b,nullptr,&vertices_)) &&
        SUCCEEDED(device->CreateSamplerState(&s,&sampler_)) && SUCCEEDED(device->CreateBlendState(&blend,&blend_)) &&
        SUCCEEDED(device->CreateRasterizerState(&r,&raster_)) && SUCCEEDED(device->CreateDepthStencilState(&z,&depth_));
    return ready_;
}

bool Renderer::Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
    unsigned width,unsigned height,std::span<const std::string_view> lines) noexcept
{
    if(!context || !target || width<64 || height<64 || lines.empty() || lines.size()>kMaxLines) return false;
    std::size_t chars=0,longest=0;
    for(auto line:lines) {chars+=line.size();longest=(std::max)(longest,line.size());}
    if(chars>(kMaxVertices-6)/6) return false;
    bool mapped=false;
    try {
        ComPtr<ID3D11Device> device;context->GetDevice(&device);
        if(!device || FAILED(device->GetDeviceRemovedReason()) || !Prepare(device.Get()) || !state_.Begin(context,target)) return false;
        D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(context->Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map))) {state_.End();return false;}
        mapped=true;auto* output=static_cast<Vertex*>(map.pData);unsigned count=0;
        auto quad=[&](float x,float y,float w,float h,float u,float v,float du,float dv,std::uint32_t color) {
            const auto vertex=[&](float px,float py,float tu,float tv) {return Vertex{px/float(width)*2-1,1-py/float(height)*2,tu,tv,color};};
            const Vertex corners[]{vertex(x,y,u,v),vertex(x+w,y,u+du,v),vertex(x+w,y+h,u+du,v+dv),vertex(x,y+h,u,v+dv)};
            for(unsigned index:{0u,1u,2u,0u,2u,3u}) output[count++]=corners[index];
        };
        quad(16,16,float(longest)*11+32,float(lines.size())*27+24,.5f/kWidth,.5f/kHeight,0,0,0xe61c1712u);
        for(std::size_t row=0;row<lines.size();++row) {
            float x=30;
            for(unsigned char ch:lines[row]) {
                if(ch>32 && ch<127) {
                    unsigned index=ch-32;
                    quad(x,27+float(row)*27,kCell,kCell,float(index%16*kCell)/kWidth,
                        float(index/16*kCell)/kHeight,float(kCell)/kWidth,float(kCell)/kHeight,
                        row==0?0xffffdcc0u:0xfff0f0f0u);
                }
                x+=11;
            }
        }
        context->Unmap(vertices_.Get(),0);mapped=false;
        const UINT stride=sizeof(Vertex),offset=0;auto* vb=vertices_.Get();
        context->IASetInputLayout(layout_.Get());context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader_.Get(),nullptr,0);context->PSSetShader(pixelShader_.Get(),nullptr,0);
        auto* font=font_.Get();auto* sampler=sampler_.Get();context->PSSetShaderResources(0,1,&font);context->PSSetSamplers(0,1,&sampler);
        const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
        context->RSSetState(raster_.Get());context->OMSetBlendState(blend_.Get(),nullptr,~0u);context->OMSetDepthStencilState(depth_.Get(),0);
        context->Draw(count,0);state_.End();return true;
    } catch(...) {
        if(mapped) context->Unmap(vertices_.Get(),0);
        state_.End();return false;
    }
}
}
