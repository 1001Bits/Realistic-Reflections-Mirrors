#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <DirectXMath.h>

// Public, model-local schema produced by Mirror Creator. No FormID, filename,
// ESP registration or first-party asset hash is part of this declaration.
namespace MirrorNifContract
{
	inline constexpr std::string_view kPaneName = "MOSReflectiveSurface:0";
	inline constexpr std::string_view kSchemaName = "MOSMirrorSurface";
	inline constexpr std::string_view kPlaneName = "MOSMirrorPlane";
	inline constexpr std::string_view kTrianglesName = "MOSMirrorTriangles";
	inline constexpr std::string_view kProfileName = "MOSMirrorProfile";
	inline constexpr std::int32_t kVersion = 1;
	inline constexpr std::size_t kMaxTriangleVertices = 65535 * 3;
	using PlaneValues = std::array<float, 14>;
	using Vec3 = DirectX::XMFLOAT3;
	using Vec2 = DirectX::XMFLOAT2;

	[[nodiscard]] inline bool Finite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u;
	}
	[[nodiscard]] inline bool Finite(Vec3 v) noexcept
	{
		return Finite(v.x) && Finite(v.y) && Finite(v.z);
	}
	[[nodiscard]] inline Vec3 Add(Vec3 a, Vec3 b) noexcept { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
	[[nodiscard]] inline Vec3 Scale(Vec3 a, float s) noexcept { return { a.x*s, a.y*s, a.z*s }; }
	[[nodiscard]] inline float Dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
	[[nodiscard]] inline Vec3 Cross(Vec3 a, Vec3 b) noexcept
	{
		return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
	}
	[[nodiscard]] inline bool Unit(Vec3 v) noexcept
	{
		return Finite(v) && std::abs(Dot(v, v)-1.0f) <= 0.0002f;
	}
	[[nodiscard]] inline Vec3 At(const PlaneValues& p, std::size_t i) noexcept
	{
		return { p[i], p[i+1], p[i+2] };
	}
	[[nodiscard]] inline bool ValidPlane(const PlaneValues& p) noexcept
	{
		for (const auto v : p)
			if (!Finite(v) || std::abs(v) > 1.0e6f) return false;
		const auto normal = At(p, 3), right = At(p, 6), up = At(p, 9);
		return Unit(normal) && Unit(right) && Unit(up) &&
			std::abs(Dot(right, up)) <= 0.0002f &&
			std::abs(Dot(normal, right)) <= 0.0002f && std::abs(Dot(normal, up)) <= 0.0002f &&
			Dot(Cross(right, up), normal) >= 0.9998f &&
			p[12] > 1.0e-4f && p[13] > 1.0e-4f;
	}
	template <class Reader>
	[[nodiscard]] inline bool ValidTriangleVertices(std::size_t count, Reader vertex) noexcept
	{
		if (count == 0 || count > kMaxTriangleVertices || count%3 != 0)
			return false;
		double area = 0.0;
		for (std::size_t i = 0; i < count; ++i) {
			const auto v = vertex(i);
			if (!Finite(v.x) || !Finite(v.y) || std::abs(v.x) > 1.00001f || std::abs(v.y) > 1.00001f)
				return false;
		}
		for (std::size_t i = 0; i < count; i += 3) {
			const auto a = vertex(i), b = vertex(i+1), c = vertex(i+2);
			const double twice = (static_cast<double>(b.x)-a.x)*(static_cast<double>(c.y)-a.y)-
				(static_cast<double>(b.y)-a.y)*(static_cast<double>(c.x)-a.x);
			if (!(twice > 0.0)) return false;
			area += twice*0.5;
		}
		return area > 0.0 && area <= 4.0001;
	}
	[[nodiscard]] inline bool ValidTriangles(std::span<const Vec2> vertices) noexcept
	{
		return ValidTriangleVertices(vertices.size(),[&](std::size_t i) { return vertices[i]; });
	}
	[[nodiscard]] inline bool ValidFloatTriangles(std::span<const float> values) noexcept
	{
		return values.size()%2 == 0 && ValidTriangleVertices(values.size()/2,[&](std::size_t i) {
			return Vec2{values[2*i],values[2*i+1]};
		});
	}

	struct Transform
	{
		Vec3 translation{};
		std::array<Vec3, 3> rotation{};
		float scale{ 0.0f };
	};
	struct WorldPane
	{
		Vec3 center{};
		Vec3 normal{};
		Vec3 tangent{};
		Vec3 bitangent{};
		float halfWidth{ 0.0f };
		float halfHeight{ 0.0f };
	};
	[[nodiscard]] inline bool ToWorld(const PlaneValues& p, const Transform& t, WorldPane& out) noexcept
	{
		out = {};
		if (!ValidPlane(p) || !Finite(t.translation) || !Finite(t.scale) ||
			std::abs(t.scale) <= 1.0e-6f || std::abs(t.scale) > 1.0e6f)
			return false;
		for (const auto axis : t.rotation)
			if (!Unit(axis)) return false;
		if (std::abs(Dot(t.rotation[0],t.rotation[1])) > 0.0002f ||
			std::abs(Dot(t.rotation[1],t.rotation[2])) > 0.0002f ||
			std::abs(Dot(t.rotation[2],t.rotation[0])) > 0.0002f)
			return false;
		const auto rotate = [&](Vec3 v) {
			return Add(Add(Scale(t.rotation[0],v.x),Scale(t.rotation[1],v.y)),Scale(t.rotation[2],v.z));
		};
		WorldPane pane{};
		pane.center = Add(t.translation, Scale(rotate(At(p,0)),t.scale));
		pane.tangent = Scale(rotate(At(p,6)), t.scale < 0.0f ? -1.0f : 1.0f);
		pane.bitangent = Scale(rotate(At(p,9)), t.scale < 0.0f ? -1.0f : 1.0f);
		// Winding is transformed with the surface. A negative uniform scale does
		// not reverse the oriented normal independently of the two surface axes.
		pane.normal = Cross(pane.tangent, pane.bitangent);
		pane.halfWidth = p[12]*std::abs(t.scale);
		pane.halfHeight = p[13]*std::abs(t.scale);
		if (!Finite(pane.center) || !Unit(pane.normal) || !Finite(pane.halfWidth) ||
			!Finite(pane.halfHeight) || pane.halfWidth <= 1.0e-4f || pane.halfHeight <= 1.0e-4f ||
			pane.halfWidth > 1.0e6f || pane.halfHeight > 1.0e6f)
			return false;
		out = pane;
		return true;
	}
}
