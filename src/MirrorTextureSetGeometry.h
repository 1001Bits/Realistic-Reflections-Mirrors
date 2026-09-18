#pragma once

#include "MirrorNifContract.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

// Static SSE vertex streams and exact planar contours. No engine pointers or
// GPU access; inputs are bounded copies owned by the caller.
namespace MirrorTextureSetGeometry
{
	using namespace MirrorNifContract;
	struct Surface
	{
		PlaneValues plane{};
		std::vector<Vec2> triangles{};
	};
	[[nodiscard]] inline bool Decode(std::span<const std::byte> vertices,
		std::span<const std::uint16_t> indices, std::uint32_t vertexCount,
		std::uint64_t descriptor, std::vector<Vec3>& output)
	{
		output.clear();
		const auto stride = static_cast<std::size_t>(descriptor & 15u)*4;
		const auto flags = descriptor >> 44;
		if (vertexCount == 0 || vertexCount > 65535 || stride < 16 || stride > 60 ||
			(descriptor & 0xf0u) || !(flags & 1u) || (flags & (1u<<6)) ||
			vertices.size() != vertexCount*stride || indices.empty() ||
			indices.size()%3 || indices.size() > kMaxTriangleVertices) return false;
		std::vector<Vec3> decoded;
		decoded.reserve(indices.size());
		for (const auto index:indices) {
			if (index >= vertexCount) return false;
			Vec3 v{};
			std::memcpy(&v,vertices.data()+index*stride,sizeof(v));
			if (!Finite(v) || std::abs(v.x)>1.0e6f || std::abs(v.y)>1.0e6f || std::abs(v.z)>1.0e6f)
				return false;
			decoded.push_back(v);
		}
		output = std::move(decoded);
		return true;
	}
	[[nodiscard]] inline bool Normalize(Vec3 value, Vec3& output) noexcept
	{
		const auto square = Dot(value,value);
		if (!Finite(square) || square <= 1.0e-20f) return false;
		output = Scale(value,1.0f/std::sqrt(square));
		return Unit(output);
	}
	[[nodiscard]] inline bool Build(std::span<const Vec3> vertices, Surface& output)
	{
		output = {};
		if (vertices.empty() || vertices.size()%3 || vertices.size()>kMaxTriangleVertices) return false;
		Vec3 low=vertices[0], high=low;
		for (auto v:vertices) {
			if (!Finite(v) || std::abs(v.x)>1.0e6f || std::abs(v.y)>1.0e6f || std::abs(v.z)>1.0e6f)
				return false;
			low = {(std::min)(low.x,v.x),(std::min)(low.y,v.y),(std::min)(low.z,v.z)};
			high = {(std::max)(high.x,v.x),(std::max)(high.y,v.y),(std::max)(high.z,v.z)};
		}
		const auto difference=[](Vec3 a, Vec3 b) { return Add(a,Scale(b,-1.0f)); };
		const auto extent=difference(high,low);
		const auto tolerance=(std::max)(1.0e-6f,(std::max)({extent.x,extent.y,extent.z})*1.0e-5f);
		const auto origin=vertices[0];
		Vec3 normal{},right{},up{};
		if (!Normalize(Cross(difference(vertices[1],origin),difference(vertices[2],origin)),normal)) return false;
		if (!Normalize(Cross(std::abs(normal.z)<0.95f ? Vec3{0,0,1} : Vec3{0,1,0},normal),right)) return false;
		up=Cross(normal,right);
		float minU=0,maxU=0,minV=0,maxV=0;
		for (std::size_t i=0;i<vertices.size();i+=3) {
			Vec3 n{};
			if (!Normalize(Cross(difference(vertices[i+1],vertices[i]),difference(vertices[i+2],vertices[i])),n) ||
				Dot(n,normal)<0.999961923f) return false; // cos(0.5 degrees)
			for (std::size_t j=0;j<3;++j) {
				const auto p=difference(vertices[i+j],origin);
				if (std::abs(Dot(p,normal))>tolerance) return false;
				const auto u=Dot(p,right),v=Dot(p,up);
				minU=(std::min)(minU,u);maxU=(std::max)(maxU,u);
				minV=(std::min)(minV,v);maxV=(std::max)(maxV,v);
			}
		}
		const auto halfU=(maxU-minU)*0.5f,halfV=(maxV-minV)*0.5f;
		const auto center=Add(origin,Add(Scale(right,(maxU+minU)*0.5f),Scale(up,(maxV+minV)*0.5f)));
		Surface result;
		result.plane={center.x,center.y,center.z,normal.x,normal.y,normal.z,right.x,right.y,right.z,
			up.x,up.y,up.z,halfU,halfV};
		if (!ValidPlane(result.plane)) return false;
		result.triangles.reserve(vertices.size());
		for (auto vertex:vertices) {
			const auto v=difference(vertex,center);
			result.triangles.push_back({Dot(v,right)/halfU,Dot(v,up)/halfV});
		}
		if (!ValidTriangles(result.triangles)) return false;
		output=std::move(result);
		return true;
	}
}
