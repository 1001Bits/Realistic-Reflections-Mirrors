#pragma once

#include "MirrorDefinitionRegistry.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace MirrorAuthoringGeometry
{
	using Vec3 = MirrorDefinitionRegistry::Float3;
	struct Vec2 { float x{}, y{}; };
	struct Surface {
		MirrorDefinitionRegistry::LocalPane pane{};
		std::vector<Vec2> triangles;
		float area{};
	};
	inline constexpr std::size_t kMaxVertices = 65535, kMaxTriangleVertices = 196605;
	inline bool Finite(float x) noexcept { return (std::bit_cast<unsigned>(x) & 0x7f800000u) != 0x7f800000u; }
	inline bool Finite(Vec3 v) noexcept { return Finite(v.x) && Finite(v.y) && Finite(v.z); }
	inline Vec3 Sub(Vec3 a, Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
	inline Vec3 Add(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
	inline Vec3 Scale(Vec3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
	inline float Dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
	inline Vec3 Cross(Vec3 a, Vec3 b) noexcept { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
	inline bool Normalize(Vec3 a, Vec3& out) noexcept {
		const float d=Dot(a,a);
		if (!Finite(d) || d<1e-16f) return false;
		out=Scale(a,1.0f/std::sqrt(d)); return Finite(out);
	}
	inline float Half(std::uint16_t h) noexcept {
		const auto e=(h>>10)&31, m=h&1023;
		float value=e==31 ? std::bit_cast<float>(0x7f800000u | (m ? 0x400000u : 0u)) :
			std::ldexp(e ? 1.0f+m/1024.0f : m/1024.0f,e ? e-15 : -14);
		return h&0x8000 ? -value : value;
	}
	inline bool Decode(std::span<const std::byte> bytes, std::span<const std::uint16_t> indices,
		std::uint32_t count, std::uint64_t descriptor, std::vector<Vec3>& output)
	{
		output.clear();
		const auto stride=(descriptor&15u)*4, flags=descriptor>>44;
		const bool full=(flags&0x400)!=0;
		if (!count || count>kMaxVertices || !(flags&1) || (flags&0x1c0) ||
			stride<(full ? 16u : 8u) || stride>60 || bytes.size()!=count*stride ||
			indices.empty() || indices.size()%3 || indices.size()>kMaxTriangleVertices) return false;
		std::vector<Vec3> decoded; decoded.reserve(indices.size());
		for (auto index:indices) {
			if (index>=count) return false;
			Vec3 p{};
			if (full) std::memcpy(&p,bytes.data()+index*stride,sizeof(p));
			else { std::uint16_t h[3]; std::memcpy(h,bytes.data()+index*stride,sizeof(h)); p={Half(h[0]),Half(h[1]),Half(h[2])}; }
			if (!Finite(p) || std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e6f) return false;
			decoded.push_back(p);
		}
		output=std::move(decoded); return true;
	}
	inline bool Build(std::span<const Vec3> vertices, Surface& output)
	{
		output={};
		if (vertices.empty() || vertices.size()%3 || vertices.size()>kMaxTriangleVertices) return false;
		Vec3 lo=vertices[0],hi=lo;
		for (auto v:vertices) {
			if (!Finite(v) || std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>1e6f) return false;
			lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};
			hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};
		}
		const auto extent=Sub(hi,lo),origin=vertices[0];
		const float tolerance=std::max(1e-5f,std::max({extent.x,extent.y,extent.z})*1e-5f);
		Vec3 normal{},right{},up{};
		if (!Normalize(Cross(Sub(vertices[1],origin),Sub(vertices[2],origin)),normal) ||
			!Normalize(Cross(std::abs(normal.z)<0.95f ? Vec3{0,0,1} : Vec3{0,1,0},normal),right)) return false;
		up=Cross(normal,right);
		float minU=0,maxU=0,minV=0,maxV=0,area=0;
		for (std::size_t i=0;i<vertices.size();i+=3) {
			const auto cross=Cross(Sub(vertices[i+1],vertices[i]),Sub(vertices[i+2],vertices[i]));
			Vec3 facing{};
			if (!Normalize(cross,facing) || Dot(facing,normal)<0.99996f) return false;
			area+=std::sqrt(Dot(cross,cross))*0.5f;
			for (std::size_t j=0;j<3;++j) {
				const auto p=Sub(vertices[i+j],origin);
				if (std::abs(Dot(p,normal))>tolerance) return false;
				const auto u=Dot(p,right),v=Dot(p,up);
				minU=std::min(minU,u);maxU=std::max(maxU,u);minV=std::min(minV,v);maxV=std::max(maxV,v);
			}
		}
		const float halfU=(maxU-minU)*0.5f,halfV=(maxV-minV)*0.5f;
		if (!Finite(area) || halfU<0.01f || halfV<0.01f || halfU>5000 || halfV>5000) return false;
		Surface result;
		result.pane.center=Add(origin,Add(Scale(right,(maxU+minU)*0.5f),Scale(up,(maxV+minV)*0.5f)));
		result.pane.normal=normal; result.pane.tangent=right; result.pane.bitangent=up;
		result.pane.halfWidth=halfU;result.pane.halfHeight=halfV;
		result.pane.bounds={result.pane.center,0.05f,halfU,halfV};result.area=area;
		result.triangles.reserve(vertices.size());
		for (auto v:vertices) { const auto p=Sub(v,result.pane.center); result.triangles.push_back({Dot(p,right)/halfU,Dot(p,up)/halfV}); }
		output=std::move(result); return true;
	}
}
