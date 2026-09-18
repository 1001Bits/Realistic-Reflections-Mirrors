#include "MirrorShadowMaskPass.h"

#include "MirrorPrivateShadow.h"

#include <d3dcompiler.h>

#include <atomic>
#include <array>
#include <cstring>
#include <format>
#include <string>

namespace MirrorShadowMaskPass
{
namespace
{
using Microsoft::WRL::ComPtr;

// A fullscreen triangle needs no vertex buffer or input layout: the vertex id
// generates clip-space corners, so the pass cannot disturb IA state beyond the
// topology and the (null) layout it restores.
std::string Source()
{
	return std::format(
		"#define PRIVATE_DEPTH_RANGE {:.1f}\n"
		"#define CAPTURE_DEPTH_SLOT t{}\n"
		"#define SHADOW_DEPTH_SLOT t{}\n"
		"#define MATRICES_SLOT t{}\n"
		"#define DETAIL_DEPTH_SLOT t{}\n"
		"#define DETAIL_MATRICES_SLOT t{}\n"
		"#define PROJECTION_SLOT b{}\n",
		MirrorPrivateShadow::kDepth - MirrorPrivateShadow::kNear,
		kCaptureDepth, kShadowDepth, kMatrices, kDetailDepth, kDetailMatrices,
		kProjectionConstants) +
		R"(
cbuffer MirrorProjection : register(PROJECTION_SLOT) {
    row_major float4x4 InverseVP; float4 Origin; float4 Viewport;
};
struct SunData { column_major float4x4 Proj[2]; column_major float4x4 Inv[2]; float2 End; float2 Start; };
Texture2D<float> CaptureDepth : register(CAPTURE_DEPTH_SLOT);
Texture2D<float> ShadowDepth : register(SHADOW_DEPTH_SLOT);
StructuredBuffer<SunData> Matrices : register(MATRICES_SLOT);
Texture2D<float> DetailDepth : register(DETAIL_DEPTH_SLOT);
StructuredBuffer<SunData> DetailMatrices : register(DETAIL_MATRICES_SLOT);

float4 VS(uint id : SV_VertexID) : SV_Position {
    // (-1,-1) (3,-1) (-1,3): one triangle covering the whole viewport.
    return float4(float2((id << 1) & 2, id & 2) * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Verbatim from the private-depth variant (MirrorSunShadowShader.cpp): the mask
// must agree with the shadow the rebuilt vanilla programs compute.
float sampleDepth(Texture2D<float> map, float4x4 transform, float3 world,
                  float3 worldDx, float3 worldDy, float bias, out float coverage) {
    float3 p = mul(transform,float4(world,1)).xyz;
    uint w,h; map.GetDimensions(w,h);
    float visibility=1;
    coverage=0;
    float2 uv=p.xy*float2(w,h)-.5;
    int2 base=int2(floor(uv)); float2 f=frac(uv);
    float3 dx=mul(transform,float4(worldDx,0)).xyz*float3(w,h,1);
    float3 dy=mul(transform,float4(worldDy,0)).xyz*float3(w,h,1);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    float scale=max(dot(dx.xy,dx.xy)*dot(dy.xy,dy.xy),1e-20);
    bool validPlane=all(isfinite(dx)) && all(isfinite(dy)) &&
                    determinant*determinant>scale*1e-8;
    float2 gradient=float2(dx.z*dy.y-dx.y*dy.z,dx.x*dy.z-dx.z*dy.x)/
                    (validPlane ? determinant : 1);
    // worldDx/worldDy reach this function as derivatives of a position
    // reconstructed from a depth *texture*, so a 2x2 quad straddling a
    // silhouette -- every hair edge, nostril and lip on a reflected face --
    // fits a plane belonging to neither surface. Its slope can exceed the whole
    // 4x4 footprint's depth span and drops the comparison far behind the
    // occluder, which is the black triangular patches the owner photographed on
    // the face in the hand mirror (ScreenShot266, 2026-09-15). A real receiver
    // plane never needs that much: bound the correction to a few texels of
    // depth, generous enough to keep grazing surfaces (86 degrees at the detail
    // map's half-unit texel) and tight enough that a bogus plane cannot run away.
    float slopeLimit=8.0*max(bias,1.0)/PRIVATE_DEPTH_RANGE;
    gradient=clamp(gradient,-slopeLimit,slopeLimit);
    float centreDepth=p.z-dot(f,gradient)-bias/PRIVATE_DEPTH_RANGE;
    if (all(isfinite(world)) &&
        validPlane && all(isfinite(gradient)) &&
        all(p>0) && all(p<1) && w>4 && h>4 &&
        all(base>int2(0,0)) && all(base<int2(w,h)-3)) {
        float edge=min(min(p.x,1-p.x),min(p.y,1-p.y));
        coverage=saturate((edge-3.0/min(w,h))/.10);
        visibility=0;
        float4 wx=float4(1-f.x,1,1,f.x), wy=float4(1-f.y,1,1,f.y);
        [unroll] for(int y=-1;y<=2;y++) {
            [unroll] for(int x=-1;x<=2;x++) {
                int2 q=base+int2(x,y);
                float compareDepth=centreDepth+dot(float2(x,y),gradient);
                float a=compareDepth<=map.Load(int3(q,0));
                visibility+=a*wx[x+1]*wy[y+1]/9;
            }
        }
    }
    return visibility;
}

float4 PS(float4 position : SV_Position) : SV_Target {
    // The variant shades a rasterized surface and uses its own SV_Position.z.
    // This pass evaluates the same surface through the completed pre-pass depth.
    float sceneDepth = CaptureDepth.Load(int3(int2(position.xy), 0));
    float2 xy = (position.xy-Viewport.xy)*Viewport.zw*float2(2,-2)+float2(-1,1);
    float4 relative = mul(float4(xy,sceneDepth,1),InverseVP);
    float3 relativeWorld = relative.xyz / relative.w;
    float3 world = relativeWorld + Origin.xyz;
    float3 worldDx=ddx(relativeWorld), worldDy=ddy(relativeWorld);
    float visibility=1;
    // A cleared texel is sky or an unwritten pixel: nothing there receives a
    // reflected shadow, and its reconstruction is meaningless.
    if (sceneDepth < 1 && all(isfinite(world)) && abs(relative.w)>1e-7) {
        float nearCoverage, farCoverage;
        // The detail map is a 512-unit box around the player and holds only the
        // casters standing inside it; the wide map holds everything else. Letting
        // the detail map win wherever its coverage is full deleted every shadow
        // whose caster stood outside that small box -- a tree a few paces away
        // casts from a canopy well outside 512 units, so its shadow vanished as
        // the player walked up to it (owner, run 29: "no shadow from the tree in
        // front of me at all ... when i come close to them they disappear").
        // Two maps over disjoint caster sets combine by minimum: either one
        // finding an occluder means the receiver is occluded. The detail map's answer
        // first fades to "lit" across its own border band, so where it stops it
        // contributes nothing instead of cutting a hard edge into the wide
        // map's shadow; a raw minimum of the two showed exactly that seam at
        // the 512-unit border. The wide map is taken as it is, as before.
        // The detail bias was 0.05 world units against a 36,863-unit depth
        // range -- 23 ticks of a 24-bit buffer, far less than one face triangle
        // spans across a texel. Owner, 2026-09-15: "incorrect shadow tri's on
        // the face". Whole triangles flipping to shadowed is the signature of a
        // bias below the caster's own depth slope, so it rises to half a unit:
        // small against a head (~20 units) but larger than a triangle's span.
        float nearVisibility=sampleDepth(DetailDepth,DetailMatrices[0].Proj[0],
            world,worldDx,worldDy,.5,nearCoverage);
        float farVisibility=sampleDepth(ShadowDepth,Matrices[0].Proj[0],
            world,worldDx,worldDy,2,farCoverage);
        visibility=min(farVisibility,lerp(1,nearVisibility,nearCoverage));
    }
    // The engine's shadow mask carries sun visibility in .x; the remaining
    // channels stay lit so no consumer of the other masks is darkened.
    return float4(visibility,1,1,1);
})";
}

struct Resources
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11VertexShader> vertexShader;
	ComPtr<ID3D11PixelShader> pixelShader;
	ComPtr<ID3D11RasterizerState> rasterizer;
	ComPtr<ID3D11DepthStencilState> depthState;
	ComPtr<ID3D11BlendState> blend;
	ComPtr<ID3D11Texture2D> mask;
	ComPtr<ID3D11RenderTargetView> maskRTV;
	ComPtr<ID3D11ShaderResourceView> maskSRV;
	std::uint32_t width{}, height{};
	bool attempted{};
};

thread_local Resources g_resources{};
thread_local std::uint64_t g_sequence{ 0 };
std::atomic_uint64_t g_renders{}, g_rejects{}, g_faults{}, g_allocations{};

[[nodiscard]] bool EnsurePrograms(ID3D11Device* device) noexcept
{
	auto& r = g_resources;
	if (r.device.Get() != device) {
		r = {};
		r.device = device;
	}
	if (r.vertexShader && r.pixelShader && r.rasterizer && r.depthState && r.blend)
		return true;
	if (r.attempted)
		return false;
	r.attempted = true;
	const auto source = Source();
	ComPtr<ID3DBlob> vs, ps;
	if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
			"VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, nullptr)) ||
		FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
			"PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, nullptr)))
		return false;
	D3D11_RASTERIZER_DESC rasterizer{};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	D3D11_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = FALSE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
	D3D11_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	return SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &r.vertexShader)) &&
		SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &r.pixelShader)) &&
		SUCCEEDED(device->CreateRasterizerState(&rasterizer, &r.rasterizer)) &&
		SUCCEEDED(device->CreateDepthStencilState(&depth, &r.depthState)) &&
		SUCCEEDED(device->CreateBlendState(&blend, &r.blend));
}

[[nodiscard]] bool EnsureMask(ID3D11Device* device, std::uint32_t width, std::uint32_t height) noexcept
{
	auto& r = g_resources;
	if (r.mask && r.width == width && r.height == height)
		return true;
	r.mask.Reset();
	r.maskRTV.Reset();
	r.maskSRV.Reset();
	r.width = r.height = 0;
	D3D11_TEXTURE2D_DESC description{};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	// The engine's own shadow mask format, so every consumer samples it exactly
	// as it samples the main view's.
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&description, nullptr, &r.mask)) ||
		FAILED(device->CreateRenderTargetView(r.mask.Get(), nullptr, &r.maskRTV)) ||
		FAILED(device->CreateShaderResourceView(r.mask.Get(), nullptr, &r.maskSRV))) {
		r.mask.Reset();
		r.maskRTV.Reset();
		r.maskSRV.Reset();
		return false;
	}
	r.width = width;
	r.height = height;
	g_allocations.fetch_add(1, std::memory_order_relaxed);
	return true;
}

/**
 * One draw's saved pipeline state. The private pass runs inside the engine's
 * own render sequence, so every stage this pass writes is read back first and
 * restored, exactly like the shadow bridge's per-draw lease.
 */
struct StateScope
{
	ID3D11DeviceContext* context{};
	ComPtr<ID3D11RenderTargetView> targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
	ComPtr<ID3D11DepthStencilView> depthTarget;
	ComPtr<ID3D11VertexShader> vertexShader;
	ComPtr<ID3D11PixelShader> pixelShader;
	ComPtr<ID3D11GeometryShader> geometryShader;
	ComPtr<ID3D11InputLayout> layout;
	ComPtr<ID3D11RasterizerState> rasterizer;
	ComPtr<ID3D11DepthStencilState> depthState;
	ComPtr<ID3D11BlendState> blend;
	ComPtr<ID3D11Buffer> constants;
	ComPtr<ID3D11ShaderResourceView> resources[5];
	std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
	UINT viewportCount{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
	D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
	FLOAT blendFactor[4]{};
	UINT sampleMask{ 0xFFFFFFFFu };
	UINT stencilReference{};
	static constexpr std::array<unsigned, 5> kSlots{
		kCaptureDepth, kShadowDepth, kMatrices, kDetailDepth, kDetailMatrices
	};

	void Save(ID3D11DeviceContext* a_context) noexcept
	{
		context = a_context;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
			reinterpret_cast<ID3D11RenderTargetView**>(targets), &depthTarget);
		context->VSGetShader(&vertexShader, nullptr, nullptr);
		context->PSGetShader(&pixelShader, nullptr, nullptr);
		context->GSGetShader(&geometryShader, nullptr, nullptr);
		context->IAGetInputLayout(&layout);
		context->IAGetPrimitiveTopology(&topology);
		context->RSGetState(&rasterizer);
		context->RSGetViewports(&viewportCount, viewports.data());
		context->OMGetDepthStencilState(&depthState, &stencilReference);
		context->OMGetBlendState(&blend, blendFactor, &sampleMask);
		context->PSGetConstantBuffers(kProjectionConstants, 1, &constants);
		for (std::size_t i = 0; i < kSlots.size(); ++i)
			context->PSGetShaderResources(kSlots[i], 1, &resources[i]);
	}

	void Restore() noexcept
	{
		if (!context)
			return;
		ID3D11RenderTargetView* rawTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		for (std::size_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			rawTargets[i] = targets[i].Get();
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawTargets, depthTarget.Get());
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->GSSetShader(geometryShader.Get(), nullptr, 0);
		context->IASetInputLayout(layout.Get());
		context->IASetPrimitiveTopology(topology);
		context->RSSetState(rasterizer.Get());
		context->RSSetViewports(viewportCount, viewports.data());
		context->OMSetDepthStencilState(depthState.Get(), stencilReference);
		context->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
		auto* buffer = constants.Get();
		context->PSSetConstantBuffers(kProjectionConstants, 1, &buffer);
		for (std::size_t i = 0; i < kSlots.size(); ++i) {
			auto* view = resources[i].Get();
			context->PSSetShaderResources(kSlots[i], 1, &view);
		}
		context = nullptr;
	}
};

[[nodiscard]] bool RenderBody(ID3D11DeviceContext* context, const Inputs& inputs) noexcept
{
	ComPtr<ID3D11Device> device;
	context->GetDevice(&device);
	if (!device || !EnsurePrograms(device.Get()) ||
		!EnsureMask(device.Get(), inputs.width, inputs.height))
		return false;

	auto& r = g_resources;
	StateScope scope{};
	scope.Save(context);

	ID3D11RenderTargetView* target = r.maskRTV.Get();
	context->OMSetRenderTargets(1, &target, nullptr);
	const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(inputs.width),
		static_cast<float>(inputs.height), 0, 1 };
	context->RSSetViewports(1, &viewport);
	context->RSSetState(r.rasterizer.Get());
	context->OMSetDepthStencilState(r.depthState.Get(), 0);
	constexpr FLOAT factor[4]{ 1, 1, 1, 1 };
	context->OMSetBlendState(r.blend.Get(), factor, 0xFFFFFFFFu);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(r.vertexShader.Get(), nullptr, 0);
	context->PSSetShader(r.pixelShader.Get(), nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	auto* projection = inputs.projection;
	context->PSSetConstantBuffers(kProjectionConstants, 1, &projection);
	ID3D11ShaderResourceView* views[StateScope::kSlots.size()]{
		inputs.captureDepth, inputs.shadowDepth, inputs.matrices,
		inputs.detailDepth, inputs.detailMatrices
	};
	for (std::size_t i = 0; i < StateScope::kSlots.size(); ++i)
		context->PSSetShaderResources(StateScope::kSlots[i], 1, &views[i]);
	context->Draw(3, 0);
	scope.Restore();
	return true;
}
}

bool Render(ID3D11DeviceContext* context, const Inputs& inputs) noexcept
{
	if (!context || !inputs.captureDepth || !inputs.shadowDepth || !inputs.matrices ||
		!inputs.detailDepth || !inputs.detailMatrices || !inputs.projection ||
		inputs.width < 4 || inputs.height < 4 ||
		inputs.width > 8192 || inputs.height > 8192) {
		g_rejects.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	bool rendered = false;
#if defined(_MSC_VER)
	__try {
		rendered = RenderBody(context, inputs);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_faults.fetch_add(1, std::memory_order_relaxed);
		g_sequence = 0;
		return false;
	}
#else
	rendered = RenderBody(context, inputs);
#endif
	if (rendered)
		g_renders.fetch_add(1, std::memory_order_relaxed);
	else
		g_rejects.fetch_add(1, std::memory_order_relaxed);
	return rendered;
}

ID3D11ShaderResourceView* Mask() noexcept
{
	return g_resources.maskSRV.Get();
}

std::uint64_t MaskSequence() noexcept
{
	return g_sequence;
}

void Publish(std::uint64_t sequence) noexcept
{
	g_sequence = g_resources.maskSRV ? sequence : 0;
}

bool Current(std::uint64_t sequence) noexcept
{
	return sequence != 0 && g_sequence == sequence && g_resources.maskSRV != nullptr;
}

void Invalidate() noexcept
{
	g_sequence = 0;
}

void Release() noexcept
{
	g_resources = {};
	g_sequence = 0;
}

Diagnostics Counters() noexcept
{
	return Diagnostics{ g_renders.load(std::memory_order_relaxed),
		g_rejects.load(std::memory_order_relaxed),
		g_faults.load(std::memory_order_relaxed),
		g_allocations.load(std::memory_order_relaxed) };
}
}
