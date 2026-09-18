#include "MirrorSunShadowShader.h"
#include "MirrorPrivateShadow.h"
#include "../extern/dxbc/d3d12TokenizedProgramFormat.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <format>
#include <limits>
#include <stdexcept>
#include <wrl/client.h>

namespace MirrorSunShadowContainerHash
{
#include "../extern/dxbc/DxilHash.inl"
}

namespace MirrorSunShadowShader
{
namespace
{
using Words = std::vector<std::uint32_t>;
using Microsoft::WRL::ComPtr;
void Require(bool ok, const char *why)
{
	if (!ok)
		throw std::runtime_error(why);
}
std::uint32_t Word(std::span<const std::uint8_t> bytes, std::size_t offset)
{
	Require(offset <= bytes.size() && bytes.size() - offset >= 4, "truncated container");
	std::uint32_t value;
	std::memcpy(&value, bytes.data() + offset, 4);
	return value;
}
void Put(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
{
	std::memcpy(bytes.data() + offset, &value, 4);
}
constexpr std::uint32_t Tag(char a, char b, char c, char d)
{
	return static_cast<unsigned>(a) | (static_cast<unsigned>(b) << 8) | (static_cast<unsigned>(c) << 16) |
		   (static_cast<unsigned>(d) << 24);
}
struct Chunk
{
	std::uint32_t tag;
	std::vector<std::uint8_t> data;
};
std::vector<Chunk> Chunks(std::span<const std::uint8_t> bytes)
{
	Require(bytes.size() <= 4 * 1024 * 1024 && bytes.size() >= 32 &&
				Word(bytes, 0) == Tag('D', 'X', 'B', 'C') && Word(bytes, 24) == bytes.size(),
			"invalid DXBC");
	const auto count = Word(bytes, 28);
	Require(count < 64 && 32 + 4 * count <= bytes.size(), "invalid chunk count");
	std::vector<Chunk> result;
	for (unsigned i = 0; i < count; ++i)
	{
		const auto offset = Word(bytes, 32 + i * 4);
		const auto size = Word(bytes, offset + 4);
		Require(offset >= 32 + 4 * count && offset <= bytes.size() - 8 && size <= bytes.size() - offset - 8,
				"invalid chunk bounds");
		result.push_back(
			{Word(bytes, offset), {bytes.begin() + offset + 8, bytes.begin() + offset + 8 + size}});
	}
	return result;
}
Words Code(const std::vector<Chunk> &chunks)
{
	Words result;
	for (const auto &chunk : chunks)
		if (chunk.tag == Tag('S', 'H', 'E', 'X') || chunk.tag == Tag('S', 'H', 'D', 'R'))
		{
			Require(result.empty() && chunk.data.size() % 4 == 0, "ambiguous shader program");
			result.resize(chunk.data.size() / 4);
			std::memcpy(result.data(), chunk.data.data(), chunk.data.size());
		}
	Require(result.size() >= 3 && result[1] == result.size() && result[0] == 0x50, "only ps_5_0 admitted");
	return result;
}
unsigned OperandCount(unsigned opcode)
{
	switch (opcode)
	{
#include "../extern/dxbc/OperandCounts.inl"
	default:
		throw std::runtime_error("unknown opcode");
	}
}
struct Instruction
{
	Words words;
	unsigned opcode;
};
std::vector<Instruction> Instructions(const Words &code)
{
	std::vector<Instruction> result;
	for (std::size_t i = 2; i < code.size();)
	{
		const unsigned op = code[i] & 0x7ff;
		const std::size_t n =
			op == D3D10_SB_OPCODE_CUSTOMDATA && i + 1 < code.size() ? code[i + 1] : (code[i] >> 24) & 0x7f;
		Require(n && n <= code.size() - i, "invalid instruction length");
		result.push_back({{code.begin() + i, code.begin() + i + n}, op});
		i += n;
	}
	return result;
}
bool Declaration(unsigned op)
{
	return (op >= D3D10_SB_OPCODE_DCL_RESOURCE && op <= D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) ||
		   op == D3D11_SB_OPCODE_DCL_RESOURCE_RAW || op == D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED ||
		   op == D3D10_SB_OPCODE_CUSTOMDATA;
}
struct Rewrite
{
	bool prefix{};
	unsigned temps{}, output{}, position{}, lightSlot{}, lightVector{}, reads{};
    Words prelude;
    unsigned scratchCount{};
    unsigned maxTemp{};
    bool privateColorOnly{};
};
void Operand(const Words &src, std::size_t &p, Words &dst, Rewrite &r, unsigned depth = 0)
{
	Require(p < src.size() && depth < 8, "invalid operand");
	const auto token = src[p++];
	const auto start = dst.size();
	dst.push_back(token);
	unsigned extension = token;
	while (extension & 0x80000000u)
	{
		Require(p < src.size(), "truncated operand extension");
		extension = src[p++];
		dst.push_back(extension);
	}
	const auto type = (token >> 12) & 0xff;
	const auto dimensions = (token >> 20) & 3;
    const auto headerEnd = dst.size();
	std::array<std::size_t, 3> indices{};
	std::array<bool, 3> immediate{};
    std::array<unsigned,3> representations{};
    std::array<Words,3> relative;
	for (unsigned d = 0; d < dimensions; ++d)
	{
		const auto rep = (token >> (22 + 3 * d)) & 7;
        representations[d] = rep;
		Require(rep <= 4, "unknown operand index encoding");
		if (rep == 0 || rep == 1 || rep == 3 || rep == 4)
		{
			Require(p < src.size(), "truncated index");
			indices[d] = dst.size();
			immediate[d] = rep == 0;
			dst.push_back(src[p++]);
			if (rep == 1 || rep == 4)
			{
				Require(p < src.size(), "truncated 64 bit index");
				dst.push_back(src[p++]);
			}
		}
		if (rep >= 2) {
            const auto begin = dst.size();
			Operand(src, p, dst, r, depth + 1);
            relative[d].assign(dst.begin()+begin,dst.end());
        }
	}
	if (type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32 || type == D3D10_SB_OPERAND_TYPE_IMMEDIATE64)
	{
		const auto components = (token & 3) == 1 ? 1u : (token & 3) == 2 ? 4u : 0u;
		const auto n = components * (type == D3D10_SB_OPERAND_TYPE_IMMEDIATE64 ? 2u : 1u);
		Require(n && n <= src.size() - p, "truncated immediate");
		dst.insert(dst.end(), src.begin() + p, src.begin() + p + n);
		p += n;
	}
    if (type == D3D10_SB_OPERAND_TYPE_TEMP) {
        Require(dimensions == 1 && immediate[0] && dst[indices[0]] < 4096,
                "unsupported temporary register");
        r.maxTemp = std::max(r.maxTemp, dst[indices[0]] + 1);
    }
    if (r.privateColorOnly && type == D3D10_SB_OPERAND_TYPE_OUTPUT) {
        Require(dimensions == 1 && immediate[0] && dst[indices[0]] < 8,
                "unsupported private output");
        if (dst[indices[0]] != 0) {
            dst[start] &= ~(0xffu << 12);
            dst[indices[0]] += r.output + 1;
        }
    }
    if (r.privateColorOnly && type == D3D10_SB_OPERAND_TYPE_RESOURCE) {
        Require(dimensions == 1 && immediate[0] && dst[indices[0]] != 37,
                "unrecognized ENB mask use");
    }
	if (r.prefix && (type == D3D10_SB_OPERAND_TYPE_TEMP || type == D3D10_SB_OPERAND_TYPE_INPUT ||
					 type == D3D10_SB_OPERAND_TYPE_OUTPUT))
	{
		Require(dimensions == 1 && immediate[0], "unsupported prefix register");
		if (type == D3D10_SB_OPERAND_TYPE_TEMP)
			dst[indices[0]] += r.temps;
		else if (type == D3D10_SB_OPERAND_TYPE_INPUT)
		{
			Require(dst[indices[0]] == 0, "unexpected prefix input");
			dst[indices[0]] = r.position;
		}
		else
		{
			Require(dst[indices[0]] == 0, "unexpected prefix output");
			dst[start] &= ~(0xffu << 12);
			dst[indices[0]] = r.output;
		}
	}
	if (!r.prefix && type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)
	{
		Require(dimensions == 2 && immediate[0], "unsupported constant buffer indexing");
		if (dst[indices[0]] == r.lightSlot)
		{
            if (!immediate[1]) {
                // Preserve arbitrary indexed point-light reads, including an
                // index which actually aliases DirLightColor. Load the original
                // vector, then select the private light only for that exact index.
                // No assumed loop bounds, C++ light layouts or CB mutation.
                Require((representations[1] == 2 || representations[1] == 3) &&
                        !relative[1].empty() && r.scratchCount + 2 <= 16,
                        "unsupported dynamic light index");
                const auto value = r.output + 1 + r.scratchCount++;
                const auto condition = r.output + 1 + r.scratchCount++;
                const auto emit = [&](unsigned op, const Words& a, const Words& b, const Words& c, const Words& d) {
                    r.prelude.push_back(op | (static_cast<unsigned>(1+a.size()+b.size()+c.size()+d.size())<<24));
                    for (const auto* words : {&a,&b,&c,&d}) r.prelude.insert(r.prelude.end(),words->begin(),words->end());
                };
                const auto offset = representations[1] == 3 ? dst[indices[1]] : 0u;
                Words unmodified{(token & ~(0x80000000u | 0xfffu)) | 0xe46u};
                unmodified.insert(unmodified.end(),dst.begin()+headerEnd,dst.end());
                emit(D3D10_SB_OPCODE_MOV, {0x001000f2u,value}, unmodified, {}, {});
                emit(D3D10_SB_OPCODE_IEQ, {0x00100012u,condition}, relative[1],
                     {0x00004001u,r.lightVector-offset}, {});
                emit(D3D10_SB_OPCODE_MOVC, {0x001000f2u,value}, {0x0010000au,condition},
                     {0x00100e46u,r.output}, {0x00100e46u,value});
                dst.resize(headerEnd);
                dst[start] = (token & ~((0xffu << 12) | (3u << 20) | (0x1ffu << 22))) | (1u << 20);
                dst.push_back(value);
            } else if (dst[indices[1]] == r.lightVector)
			{
				dst[start] = (token & ~((0xffu << 12) | (3u << 20) | (0x1ffu << 22))) | (1u << 20);
				dst[indices[0]] = r.output;
				dst.pop_back();
				++r.reads;
			}
		}
	}
}
Words Recode(const Instruction &instruction, Rewrite &r)
{
	r.prelude.clear(); r.scratchCount = 0;
	Words out{instruction.words[0]};
	std::size_t p = 1;
	auto ext = instruction.words[0];
	while (ext & 0x80000000u)
	{
		Require(p < instruction.words.size(), "invalid opcode extension");
		ext = instruction.words[p++];
		out.push_back(ext);
	}
	const auto n = OperandCount(instruction.opcode);
	for (unsigned i = 0; i < n; ++i)
		Operand(instruction.words, p, out, r);
	Require(p == instruction.words.size() && out.size() < 128, "unhandled instruction payload");
	out[0] = (out[0] & ~0x7f000000u) | (static_cast<unsigned>(out.size()) << 24);
    if (!r.prelude.empty()) {
        r.prelude.insert(r.prelude.end(), out.begin(),out.end());
        return r.prelude;
    }
	return out;
}
std::string PrefixSource(unsigned lightSlot, unsigned lightVector, int gammaSlot, unsigned gammaVector,
                         bool privateSampler, bool privateDepth, bool enbLighting = false)
{
	std::string gammaDeclaration, gammaCorrection;
	if (gammaSlot >= 0)
	{
		gammaDeclaration =
			std::format("cbuffer MirrorGamma : register(b{}) {{ uint4 Settings : packoffset(c{}); }}\n",
						gammaSlot, gammaVector);
		gammaCorrection = "float gamma=asfloat(Settings.w); if (Settings.x && !Settings.y && isfinite(gamma) "
						  "&& gamma>0) visibility=pow(visibility,1/gamma);\n";
	}
	if (privateDepth) {
        return std::format("#define PRIVATE_DEPTH_RANGE {:.1f}\ncbuffer NativeLight : register(b{}) {{ float4 Sun : packoffset(c{}); }}\n",
                           MirrorPrivateShadow::kDepth-MirrorPrivateShadow::kNear, lightSlot, lightVector) + gammaDeclaration +
        (enbLighting ? "cbuffer NativeFallback : register(b2) { float4 FallbackSun : packoffset(c1); };\n" : "") + R"(
cbuffer MirrorProjection : register(b13) {
    row_major float4x4 InverseVP; float4 Origin; float4 Viewport;
};
struct SunData { column_major float4x4 Proj[2]; column_major float4x4 Inv[2]; float2 End; float2 Start; };
Texture2D<float> ShadowDepth : register(t126);
StructuredBuffer<SunData> Matrices : register(t127);
Texture2D<float> DetailDepth : register(t124);
StructuredBuffer<SunData> DetailMatrices : register(t125);
float sampleDepth(Texture2D<float> map, float4x4 transform, float3 world,
                  float3 worldDx, float3 worldDy, float bias, out float coverage) {
    float3 p = mul(transform,float4(world,1)).xyz;
    uint w,h; map.GetDimensions(w,h);
    float visibility=1;
    coverage=0;
    // Compare depth before filtering; never linearly filter stored depth.
    float2 uv=p.xy*float2(w,h)-.5;
    int2 base=int2(floor(uv)); float2 f=frac(uv);
    // Screen-pixel depth deltas are not shadow-texel deltas: their scale changes
    // with mirror resolution and distance. That creates stripes nearby and
    // erases real occluders farther away. Fit the receiver in shadow texels and
    // compare each tap with the plane's depth at that tap's exact centre.
    // Compute screen derivatives before choosing a cascade. Derivatives inside
    // a pixel-divergent near/far branch are undefined at the cascade boundary.
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
        // Combine the overlapping bilinear taps of a 3x3 kernel: sixteen
        // unique depth fetches produce the same result as thirty-six fetches.
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
float4 main(float4 position : SV_Position) : SV_Target {
    float2 xy = (position.xy-Viewport.xy)*Viewport.zw*float2(2,-2)+float2(-1,1);
    float4 relative = mul(float4(xy,position.z,1),InverseVP);
    // Derive slopes before adding the large absolute camera origin. Otherwise
    // close-up face pixels can quantize to identical world coordinates.
    float3 relativeWorld = relative.xyz / relative.w;
    float3 world = relativeWorld + Origin.xyz;
    float3 worldDx=ddx(relativeWorld), worldDy=ddy(relativeWorld);
    float visibility=1;
    if (all(isfinite(world)) && abs(relative.w)>1e-7) {
        float nearCoverage, farCoverage;
        // Same combination as the mask pass, and for the same reason: the
        // detail map is a 512-unit box around the player, so letting it win
        // where its coverage is full deleted shadows cast from outside that
        // box. Two maps over disjoint caster sets combine by minimum, with the
        // detail map faded to "lit" across its border band so its 512-unit
        // edge blends instead of cutting a hard edge into the wide map.
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
    )" + gammaCorrection + (enbLighting ?
        R"(float3 selected = asuint(Sun.w) != 0 ? Sun.xyz : FallbackSun.xyz;
           return float4(selected*visibility,1); })" :
        R"(return float4(Sun.xyz*visibility,Sun.w); })");
    }
	return std::format("#define PRIVATE_SAMPLER {}\ncbuffer NativeLight : register(b{}) {{ float4 Sun : "
					   "packoffset(c{}); }}\n",
					   privateSampler ? 1 : 0, lightSlot, lightVector) +
		   gammaDeclaration + R"(
cbuffer MirrorProjection : register(b13) {
    row_major float4x4 InverseVP;
    float4 Origin;
    float4 Viewport; // left, top, inverse width, inverse height
};
struct SunData { column_major float4x4 Proj[2]; column_major float4x4 Inv[2]; float2 End; float2 Start; };
Texture2D<float2> Moments : register(t126);
StructuredBuffer<SunData> Matrices : register(t127);
#if PRIVATE_SAMPLER
SamplerState LinearClamp : register(s13);
#endif
float2 sampleMoments(float2 uv,uint mip) {
#if PRIVATE_SAMPLER
    return Moments.SampleLevel(LinearClamp,uv,mip);
#else
    // Some landscape programs occupy all sixteen samplers. Preserve every
    // material sampler and perform the same bilinear filtering with Load.
    uint w,h,levels; Moments.GetDimensions(mip,w,h,levels);
    float2 p=uv*float2(w,h)-0.5;
    int2 b=int2(floor(p)),limit=int2(w,h)-1;
    float2 f=frac(p);
    float2 a=Moments.Load(int3(clamp(b,int2(0,0),limit),mip));
    float2 c=Moments.Load(int3(clamp(b+int2(1,0),int2(0,0),limit),mip));
    float2 d=Moments.Load(int3(clamp(b+int2(0,1),int2(0,0),limit),mip));
    float2 e=Moments.Load(int3(clamp(b+int2(1,1),int2(0,0),limit),mip));
    return lerp(lerp(a,c,f.x),lerp(d,e,f.x),f.y);
#endif
}
float cascade(float3 world, uint i, out float coverage) {
    float3 p = mul(Matrices[0].Proj[i], float4(world,1)).xyz;
    // Never smear a cascade edge across scenery outside its actual coverage.
    bool inside = all(p > 0) && all(p < 1) && all(isfinite(p));
    float edge = min(min(p.x, 1-p.x), min(p.y, 1-p.y));
    coverage = inside ? saturate(edge * 32) : 0;
    if (!inside) return 1;
    float2 m = sampleMoments(p.xy, 1-i);
    // Clear depth (1,1) contains no caster evidence. Native camera-fitted
    // cascades can leave such a hole even while this point projects inside
    // their XY bounds. Let the wider cascade supply a shadow there, rather
    // than treating the clear near texel as proof that the surface is lit.
    // Populated near samples still take precedence, including lit receivers
    // in front of their occluders; taking min(near,far) would over-shadow them.
    if (all(m >= 1)) { coverage=0; return 1; }
    float variance = max(m.y-m.x*m.x,0.00001);
    float d = p.z-m.x;
    return p.z <= m.x ? 1 : saturate((variance/(variance+d*d)-0.2)/0.8);
}
float4 main(float4 position : SV_Position) : SV_Target {
    float2 xy = (position.xy-Viewport.xy)*Viewport.zw*float2(2,-2)+float2(-1,1);
    float4 relative = mul(float4(xy,position.z,1),InverseVP);
    float3 world = relative.xyz / relative.w + Origin.xyz;
    float nearCoverage, farCoverage;
    float farShadow = cascade(world,1,farCoverage);
    float nearShadow = cascade(world,0,nearCoverage);
    float visibility = lerp(lerp(1,farShadow,farCoverage),nearShadow,nearCoverage);
    if (!all(isfinite(world)) || abs(relative.w) < 0.0000001) visibility=1;
    )" + gammaCorrection +
		   R"(return float4(Sun.xyz*visibility,Sun.w);
})";
}
} // namespace

bool CanRenderDepthOnly(std::span<const std::uint8_t> original) noexcept
{
    try {
        const auto code=Instructions(Code(Chunks(original)));
        for (const auto& i:code)
            if (i.opcode==D3D10_SB_OPCODE_DISCARD ||
                i.opcode==D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED ||
                i.opcode==D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW ||
                i.opcode==D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED) return false;
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(original.data(),original.size(),IID_PPV_ARGS(&reflection)))) return false;
        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc)) || !desc.OutputParameters || desc.OutputParameters>4) return false;
        for(unsigned i=0;i<desc.OutputParameters;i++) {
            D3D11_SIGNATURE_PARAMETER_DESC output{};
            if (FAILED(reflection->GetOutputParameterDesc(i,&output)) || !output.SemanticName ||
                _stricmp(output.SemanticName,"SV_Target")!=0) return false;
        }
        return true;
    } catch (...) {return false;}
}

#include "MirrorCasterShaderSlice.inl"

static Variant BuildImpl(std::span<const std::uint8_t> original, bool privateDepth,
                         bool nativeLightingLayout, bool enbLighting)
{
	Variant result;
	if (privateDepth) result.usesPrivateSampler = false;
	try
	{
        auto chunks = Chunks(original);
		auto originalCode = Code(chunks);
        if (enbLighting) Require(std::count_if(chunks.begin(),chunks.end(),[](const Chunk& c) {
            return c.tag == Tag('O','S','G','N');
        }) == 1 && std::none_of(chunks.begin(),chunks.end(),[](const Chunk& c) {
            return c.tag == Tag('O','S','G','1');
        }), "unsupported ENB output signature format");
        const bool strippedNative = nativeLightingLayout && std::none_of(chunks.begin(), chunks.end(), [](const Chunk& c) {
            return c.tag == Tag('R','D','E','F');
        });
		ComPtr<ID3D11ShaderReflection> reflect;
		Require(SUCCEEDED(D3DReflect(original.data(), original.size(), IID_PPV_ARGS(&reflect))),
				"reflection metadata unavailable");
		D3D11_SHADER_DESC description{};
		Require(SUCCEEDED(reflect->GetDesc(&description)) && description.OutputParameters >= 1 &&
                description.OutputParameters <= (enbLighting ? 8u : strippedNative ? 4u : 2u),
				"not forward lighting");
		unsigned slot = ~0u, vector = ~0u, position = ~0u, gammaVector = 0;
		int gammaSlot = -1;
		for (unsigned i = 0; !enbLighting && i < description.ConstantBuffers; ++i)
		{
			auto *cb = reflect->GetConstantBufferByIndex(i);
			D3D11_SHADER_BUFFER_DESC cbd{};
			Require(SUCCEEDED(cb->GetDesc(&cbd)), "constant buffer description");
			D3D11_SHADER_VARIABLE_DESC light{};
			if (SUCCEEDED(cb->GetVariableByName("DirLightColor")->GetDesc(&light)))
			{
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				Require(SUCCEEDED(reflect->GetResourceBindingDescByName(cbd.Name, &binding)),
						"light buffer binding");
				Require(light.Size == 12 && light.StartOffset % 16 == 0 && slot == ~0u,
						"unexpected directional light layout");
				slot = binding.BindPoint;
				vector = light.StartOffset / 16;
			}
			auto *gamma = cb->GetVariableByName("SharedData::linearLightingSettings");
			D3D11_SHADER_VARIABLE_DESC gd{};
			if (SUCCEEDED(gamma->GetDesc(&gd)))
			{
				D3D11_SHADER_TYPE_DESC enable{}, linear{}, exponent{};
				auto *type = gamma->GetType();
				Require(SUCCEEDED(type->GetMemberTypeByName("enableLinearLighting")->GetDesc(&enable)) &&
							enable.Offset == 0 &&
							SUCCEEDED(type->GetMemberTypeByName("isDirLightLinear")->GetDesc(&linear)) &&
							linear.Offset == 4 &&
							SUCCEEDED(type->GetMemberTypeByName("lightGamma")->GetDesc(&exponent)) &&
							exponent.Offset == 12 && gd.StartOffset % 16 == 0,
						"unrecognized linear-light conversion");
				D3D11_SHADER_INPUT_BIND_DESC b{};
				Require(SUCCEEDED(reflect->GetResourceBindingDescByName(cbd.Name, &b)),
						"gamma buffer binding");
				gammaSlot = static_cast<int>(b.BindPoint);
				gammaVector = gd.StartOffset / 16;
			}
		}
        if (enbLighting) { slot = 6; vector = 11; }
        else if (slot == ~0u && nativeLightingLayout) {
            Require(strippedNative, "unrecognized named lighting layout");
            slot = 2; vector = 1;
        }
		Require(slot != ~0u && slot != kConstants, "not a supported lighting shader");
        // Stripped native DXBC has no RDEF resource list. Validate executable
        // declarations too, including structured/raw resources and UAVs.
        bool lightDeclared = false, fallbackDeclared = false, maskDeclared = false;
        // The ENB layout below is a fingerprint of one ENB build's shaders, and
        // a build that does not match is refused wholesale: AE + ENB on
        // 2026-09-15 rejected 20,211 of 22,329 shaders and reflected shadows
        // simply never appeared. Record what each rejected shader actually
        // declares so the matcher can be widened on evidence instead of guessed
        // at -- the worker logs only the first eight rejections, so this costs
        // eight strings a session.
        std::string observed;
        for (const auto& i : Instructions(originalCode)) {
            if (i.opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) {
                Require(i.words.size() == 4, "unexpected CB declaration");
                Require(i.words[2] != kConstants, "private CB slot occupied");
                if (i.words[2] == slot && i.words[3] > vector) lightDeclared = true;
                if (i.words[2] == 2 && i.words[3] > 1) fallbackDeclared = true;
                observed += std::format(" cb{}[{}]", i.words[2], i.words[3]);
            }
            if (i.opcode == D3D10_SB_OPCODE_DCL_RESOURCE ||
                i.opcode == D3D11_SB_OPCODE_DCL_RESOURCE_RAW ||
                i.opcode == D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED) {
                Require(i.words.size() >= 3 && i.words[2] < (privateDepth ? kDetailDepth : kMoments),
                        "private SRV slot occupied or indirect declaration");
                if (i.opcode == D3D10_SB_OPCODE_DCL_RESOURCE && i.words.size() == 4 &&
                    i.words[2] == 37 && ((i.words[0] >> 11) & 0x1f) == D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D &&
                    i.words[3] == 0x5555) maskDeclared = true;
                if (i.opcode == D3D10_SB_OPCODE_DCL_RESOURCE && i.words.size() == 4)
                    observed += std::format(" t{}(dim{},ret{:04X})", i.words[2],
                        (i.words[0] >> 11) & 0x1f, i.words[3]);
            }
            if (i.opcode == D3D10_SB_OPCODE_DCL_SAMPLER) {
                Require(i.words.size() == 3, "unexpected sampler declaration");
                if (i.words[2] == kSampler) result.usesPrivateSampler = false;
            }
            Require(i.opcode != D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED &&
                    i.opcode != D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW &&
                    i.opcode != D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED,
                    "UAV shader not admitted");
        }
        if (!lightDeclared) {
            const auto why = std::format(
                "missing light declaration (wanted cb{}[>{}]; shader declares{})",
                slot, vector, observed.empty() ? std::string(" nothing") : observed);
            Require(false, why.c_str());
        }
        if (enbLighting && !(fallbackDeclared && maskDeclared)) {
            const auto why = std::format(
                "unrecognized ENB resource layout (wanted cb2[>1]{} and t37 Texture2D ret5555{}; declares{})",
                fallbackDeclared ? " ok" : " MISSING",
                maskDeclared ? " ok" : " MISSING",
                observed.empty() ? std::string(" nothing") : observed);
            Require(false, why.c_str());
        }
		for (unsigned i = 0; i < description.BoundResources; ++i)
		{
			D3D11_SHADER_INPUT_BIND_DESC b{};
			reflect->GetResourceBindingDesc(i, &b);
			const auto contains = [&](unsigned n) {
				return b.BindPoint <= n && n - b.BindPoint < b.BindCount;
			};
			Require(b.Type != D3D_SIT_CBUFFER || !contains(kConstants), "private CB slot occupied");
			if (b.Type == D3D_SIT_SAMPLER && contains(kSampler))
				result.usesPrivateSampler = false;
			Require((b.Type == D3D_SIT_CBUFFER || b.Type == D3D_SIT_SAMPLER) ||
						(!contains(kMoments) && !contains(kMatrices) &&
                         (!privateDepth || (!contains(kDetailDepth) && !contains(kDetailMatrices)))),
					"private SRV slot occupied");
		}
		for (unsigned i = 0; i < description.InputParameters; ++i)
		{
			D3D11_SIGNATURE_PARAMETER_DESC d{};
			reflect->GetInputParameterDesc(i, &d);
			if (d.SystemValueType == D3D_NAME_POSITION)
				position = d.Register;
		}
		Require(position != ~0u, "no raster position");
		ComPtr<ID3DBlob> prefix, errors;
		const auto source = PrefixSource(slot, vector, gammaSlot, gammaVector, result.usesPrivateSampler, privateDepth, enbLighting);
		static thread_local std::string cachedSource;
		static thread_local ComPtr<ID3DBlob> cachedPrefix;
		if (source == cachedSource && cachedPrefix)
			prefix = cachedPrefix;
		else
		{
			const auto hr = D3DCompile(source.data(), source.size(), "MirrorSunShadow", nullptr, nullptr,
									   "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &prefix, &errors);
			if (FAILED(hr))
				throw std::runtime_error(errors ? static_cast<const char *>(errors->GetBufferPointer())
												: "prefix compilation failed");
			cachedSource = source;
			cachedPrefix = prefix;
		}
		const auto prefixChunks =
			Chunks({static_cast<const std::uint8_t *>(prefix->GetBufferPointer()), prefix->GetBufferSize()});
		auto base = Instructions(originalCode);
		auto extra = Instructions(Code(prefixChunks));

        // ENB's executable can use extra registers not reflected by dcl_temps.
        // Inspect every operand before allocating our temporaries; never overlap
        // its injected material, ambient or sunlight calculation registers.
        Rewrite scan{}; scan.lightSlot = ~0u;
        if (enbLighting) for (const auto& i : base) if (!Declaration(i.opcode)) (void)Recode(i, scan);
		unsigned baseTemps = 0, extraTemps = 0;
		for (const auto &i : base)
			if (i.opcode == D3D10_SB_OPCODE_DCL_TEMPS)
				baseTemps = i.words.at(1);
        if (enbLighting) baseTemps = std::max(baseTemps, scan.maxTemp);
		for (const auto &i : extra)
			if (i.opcode == D3D10_SB_OPCODE_DCL_TEMPS)
				extraTemps = i.words.at(1);
		Require(baseTemps + extraTemps + 17 <= 4096, "temporary register limit");
		Rewrite pre{true, baseTemps, baseTemps + extraTemps, position, slot, vector, 0};
		Rewrite post = pre;
		post.prefix = false;
        if (enbLighting) { post.lightSlot = ~0u; post.privateColorOnly = true; }

        std::size_t enbMask = ~std::size_t{}, enbSun = ~std::size_t{};
        if (enbLighting) {
            unsigned maskRegister = ~0u;
            for (std::size_t n = 0; n < base.size(); ++n) {
                const auto& w = base[n].words;
                if (w.size() == 7 && w[0] == ((7u << 24) | D3D10_SB_OPCODE_LD) &&
                    w[1] == 0x001000f2 && w[3] == 0x00100e46 &&
                    w[5] == 0x00107e46 && w[6] == 37) {
                    Require(enbMask == ~std::size_t{}, "multiple ENB mask loads");
                    enbMask = n; maskRegister = w[2];
                }
                if (w.size() == 11 && w[0] == ((11u << 24) | D3D10_SB_OPCODE_MOVC) &&
                    w[1] == 0x001000f2 && w[3] == 0x00208ff6 && w[4] == 6 && w[5] == 11 &&
                    w[6] == 0x00100e46 && w[7] == w[2] && w[8] == 0x00208a46 && w[9] == 2 && w[10] == 1) {
                    Require(enbSun == ~std::size_t{} && n >= 2, "ambiguous ENB sunlight");
                    const Words init{(6u << 24) | D3D10_SB_OPCODE_MOV, 0x001000f2,w[2],0x00208a46,6,11};
                    const Words mask{(7u << 24) | D3D10_SB_OPCODE_MUL,0x00100072,w[2],0x00100556,maskRegister,0x00100a46,w[2]};
                    Require(base[n-2].words == init && base[n-1].words == mask && enbMask < n-2,
                            "unrecognized ENB sunlight sequence");
                    enbSun = n;
                }
            }
            Require(enbMask != ~std::size_t{} && enbSun != ~std::size_t{}, "no ENB sunlight sequence");
            // The source still owns all material operations. Its foreign view
            // mask becomes neutral; only the selected sun is privately shadowed.
            base[enbMask].words = {(8u << 24) | D3D10_SB_OPCODE_MOV,0x001000f2,maskRegister,
                                  0x00004002,0x3f800000,0x3f800000,0x3f800000,0x3f800000};
            base[enbMask].opcode = D3D10_SB_OPCODE_MOV;
            const auto sunRegister = base[enbSun].words[2];
            base[enbSun].words = {(5u << 24) | D3D10_SB_OPCODE_MOV,0x001000f2,sunRegister,0x00100a46,pre.output};
            base[enbSun].opcode = D3D10_SB_OPCODE_MOV;
        }
		Words declarations, body;
		bool declaredPosition = false;
		for (auto &i : base)
			if (Declaration(i.opcode))
			{
				if (i.opcode == D3D10_SB_OPCODE_DCL_TEMPS)
					continue;
                if (enbLighting && i.opcode == D3D10_SB_OPCODE_DCL_OUTPUT && i.words.size() == 3 && i.words[2] != 0)
                    continue;
				if (i.opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SIV && i.words.size() == 4 &&
					i.words[2] == position && i.words[3] == D3D10_SB_NAME_POSITION)
				{
					i.words[1] |= 0x70; // preserve an authored SV_Position.w read
					declaredPosition = true;
				}
				declarations.insert(declarations.end(), i.words.begin(), i.words.end());
			}
        if (!declaredPosition) {
            // Vanilla often leaves SV_Position unused in the executable while
            // retaining its signature. Introduce the compiler's exact SIV input.
            // Any other declaration for this register would alias it: reject.
            for (const auto& i : base)
                if (i.opcode == D3D10_SB_OPCODE_DCL_INPUT_PS ||
                    i.opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SGV ||
                    i.opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SIV)
                    Require(i.words.size() >= 3 && i.words[2] != position, "conflicting raster position declaration");
            for (const auto& i : extra)
                if (i.opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SIV && i.words.size() == 4 &&
                    i.words[3] == D3D10_SB_NAME_POSITION) {
                    auto input = i.words; input[2] = position;
                    declarations.insert(declarations.end(), input.begin(), input.end());
                    declaredPosition = true; break;
                }
        }
        Require(declaredPosition, "unexpected raster position declaration");
		for (const auto &i : extra)
			if (Declaration(i.opcode))
			{
				if (i.opcode == D3D10_SB_OPCODE_DCL_TEMPS || i.opcode == D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS ||
					i.opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SIV || i.opcode == D3D10_SB_OPCODE_DCL_OUTPUT)
					continue;
				if (i.opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER && i.words.at(2) == slot)
					continue;
                if (enbLighting && i.opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER && i.words.at(2) == 2)
                    continue;
				if (i.opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER && gammaSlot >= 0 &&
					i.words.at(2) == static_cast<unsigned>(gammaSlot))
					continue;
				Require(i.opcode != D3D10_SB_OPCODE_CUSTOMDATA, "prefix immediate array unsupported");
				declarations.insert(declarations.end(), i.words.begin(), i.words.end());
			}
		declarations.push_back((2u << 24) | D3D10_SB_OPCODE_DCL_TEMPS);
		declarations.push_back(baseTemps + extraTemps + 17);
		Require(extra.back().opcode == D3D10_SB_OPCODE_RET, "prefix missing final return");
		for (std::size_t n = 0; n + 1 < extra.size(); ++n)
			if (!Declaration(extra[n].opcode))
			{
				Require(extra[n].opcode != D3D10_SB_OPCODE_RET && extra[n].opcode != D3D10_SB_OPCODE_RETC,
						"prefix early return unsupported");
				auto code = Recode(extra[n], pre);
				body.insert(body.end(), code.begin(), code.end());
			}
		for (const auto &i : base)
			if (!Declaration(i.opcode))
			{
				auto code = Recode(i, post);
				body.insert(body.end(), code.begin(), code.end());
			}
		Require(enbLighting || post.reads > 0, "no directional light reads");
		Words code{0x50, static_cast<unsigned>(2 + declarations.size() + body.size())};
		code.insert(code.end(), declarations.begin(), declarations.end());
		code.insert(code.end(), body.begin(), body.end());
		std::vector<Chunk> output;
		for (auto &c : chunks)
		{
			if (c.tag == Tag('S', 'H', 'E', 'X') || c.tag == Tag('S', 'H', 'D', 'R'))
			{
				c.data.resize(code.size() * 4);
				std::memcpy(c.data.data(), code.data(), c.data.size());
			}
            else if (enbLighting && c.tag == Tag('O','S','G','N')) {
                const auto count = Word(c.data,0);
                Require(count > 0 && count < 9 && 8 + count * 24 <= c.data.size(), "invalid ENB output signature");
                std::size_t color = ~std::size_t{};
                for (unsigned n=0;n<count;++n) if (Word(c.data,8+n*24+16)==0) {
                    Require(color==~std::size_t{}, "ambiguous color output"); color=8+n*24;
                }
                Require(color!=~std::size_t{}, "missing color output");
                const auto name = Word(c.data,color);
                // OSGN stores SV_Target with system-value 0 on compiler/native
                // DXBC; D3DReflect derives D3D_NAME_TARGET from the semantic name.
                Require(Word(c.data,color+4)==0 &&
                        (Word(c.data,color+8)==0 || Word(c.data,color+8)==D3D_NAME_TARGET) &&
                        Word(c.data,color+12)==D3D_REGISTER_COMPONENT_FLOAT32 &&
                        name>=8+count*24 && name<=c.data.size() && c.data.size()-name>=10 &&
                        _strnicmp(reinterpret_cast<const char*>(c.data.data()+name),"SV_Target",10)==0,
                        "unrecognized color output");
                std::vector<std::uint8_t> signature(8+24+10,0);
                Put(signature,0,1);Put(signature,4,8);
                std::memcpy(signature.data()+8,c.data.data()+color,24);
                Put(signature,8,32);std::memcpy(signature.data()+32,"SV_Target",10);
                while (signature.size()%4) signature.push_back(0);
                c.data=std::move(signature);
            }
			else if (c.tag == Tag('I', 'S', 'G', 'N') || c.tag == Tag('I', 'S', 'G', '1'))
			{
				const unsigned stride = c.tag == Tag('I', 'S', 'G', '1') ? 32 : 24,
							   shift = stride == 32 ? 4 : 0;
				const auto count = Word(c.data, 0);
				Require(count < 64 && 8 + count * stride <= c.data.size(), "bad input signature");
				for (unsigned n = 0; n < count; ++n)
				{
					const auto off = 8 + n * stride + shift;
					if (Word(c.data, off + 16) == position)
					{
						c.data[off + 20] |= 7;
						c.data[off + 21] |= 7;
					}
				}
			}
			// Reflection/stats describe the original; the engine continues using
			// the original shader object for those layouts. The private copy only
			// needs executable code and exact input/output signatures.
			else if (c.tag != Tag('O', 'S', 'G', 'N') && c.tag != Tag('O', 'S', 'G', '1') &&
					 c.tag != Tag('S', 'F', 'I', '0'))
				continue;
			output.push_back(std::move(c));
		}
		auto &bytes = result.bytecode;
		bytes.resize(32 + output.size() * 4);
		Put(bytes, 0, Tag('D', 'X', 'B', 'C'));
		Put(bytes, 20, 1);
		Put(bytes, 28, static_cast<unsigned>(output.size()));
		for (unsigned n = 0; n < output.size(); ++n)
		{
			auto &c = output[n];
			const auto off = bytes.size();
			Put(bytes, 32 + 4 * n, static_cast<unsigned>(off));
			bytes.resize(off + 8 + c.data.size());
			Put(bytes, off, c.tag);
			Put(bytes, off + 4, static_cast<unsigned>(c.data.size()));
			std::memcpy(bytes.data() + off + 8, c.data.data(), c.data.size());
		}
		Put(bytes, 24, static_cast<unsigned>(bytes.size()));
		MirrorSunShadowContainerHash::ComputeHashRetail(
			bytes.data() + 20, static_cast<unsigned>(bytes.size() - 20), bytes.data() + 4);
		result.replacedReads = enbLighting ? 2 : post.reads;
		result.reason = "ready";
	}
	catch (const std::exception &e)
	{
		result.bytecode.clear();
		result.reason = e.what();
	}
	return result;
}
Variant Build(std::span<const std::uint8_t> original, bool privateDepth, bool nativeLightingLayout)
{
    return BuildImpl(original,privateDepth,nativeLightingLayout,false);
}
Variant BuildENB(std::span<const std::uint8_t> original)
{
    return BuildImpl(original,true,true,true);
}
} // namespace MirrorSunShadowShader
