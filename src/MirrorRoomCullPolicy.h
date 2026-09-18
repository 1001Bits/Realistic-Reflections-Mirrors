#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Owner, core run 9 ("do all 5": room-based culling). Interiors built with room
// markers (among the core cells: Vlindrel Hall and the Silver-Blood Inn) attach
// every reference's 3D under the room its bound centre is in, and rooms connect
// only through portals. A reflection enters the room in front of the pane and
// can reach another room only through a portal inside its view, and then only
// through that portal. A reference in a room the view never reaches -- or
// outside the view narrowed by the portals on the way -- cannot reach a pixel.
//
// Each portal is treated as the sphere around it, so no portal orientation is
// read: the view through the sphere contains the view through the portal. Every
// test carries slack, and anything unusual turns the filter off for the capture.
namespace MirrorRoomCullPolicy
{
	using Point = DirectX::XMFLOAT3;
	using Plane = DirectX::XMFLOAT4; // inside: n.p - w >= 0
	inline constexpr std::size_t kMaximumDepth = 8;
	inline constexpr std::size_t kMaximumPlanes = 5 + 4 * kMaximumDepth;
	inline constexpr std::size_t kMaximumRegionsPerRoom = 8;
	inline constexpr std::size_t kMaximumRooms = 256;
	inline constexpr std::size_t kMaximumPortals = 512;
	inline constexpr std::size_t kMaximumExpansions = 4096;

	// NiTPointerList stores next/prev/element nodes, not a contiguous pointer
	// array. Validate the complete bounded chain before using any portal values.
	// The runtime caller contains pointer reads in its existing SEH boundary.
	template <class Node>
	[[nodiscard]] bool SnapshotPortalList(Node* first, Node* last, std::size_t count,
		std::span<std::uintptr_t> values) noexcept
	{
		if (count > values.size()) return false;
		Node* previous = nullptr;
		auto* node = first;
		for (std::size_t i = 0; i < count; ++i) {
			if (!node || node->prev != previous || !node->element) return false;
			values[i] = reinterpret_cast<std::uintptr_t>(node->element);
			previous = node;
			node = node->next;
		}
		return node == nullptr && previous == last;
	}

	struct Region
	{
		std::array<Plane, kMaximumPlanes> planes{};
		std::size_t count{};
	};
	struct Portal
	{
		Point center{};
		float radius{};
		std::uint32_t a{}, b{}; // room indices
	};
	struct Visibility
	{
		// Per room: the views that reach it. Empty: not reached.
		std::vector<std::vector<Region>> regions;
		bool valid{};
	};

	[[nodiscard]] constexpr bool Finite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
	}
	[[nodiscard]] inline bool Finite(const Point& p) noexcept
	{
		return Finite(p.x) && Finite(p.y) && Finite(p.z);
	}
	[[nodiscard]] inline float Dot(const Point& a, const Point& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
	[[nodiscard]] inline Point Sub(const Point& a, const Point& b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	[[nodiscard]] inline Point Add(const Point& a, const Point& b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	[[nodiscard]] inline Point Scale(const Point& a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
	[[nodiscard]] inline Point Cross(const Point& a, const Point& b) noexcept
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	[[nodiscard]] inline bool Normalize(Point& p) noexcept
	{
		const float length = std::sqrt(Dot(p, p));
		if (!Finite(length) || length < 1.0e-6F) return false;
		p = Scale(p, 1.0F / length);
		return true;
	}

	// True unless the sphere lies wholly outside one plane (by more than slack).
	[[nodiscard]] inline bool SphereInside(const Region& region, const Point& center, float radius, float slack) noexcept
	{
		if (!Finite(center) || !Finite(radius) || radius < 0) return true;
		for (std::size_t i = 0; i < region.count; ++i) {
			const auto& p = region.planes[i];
			const float side = p.x * center.x + p.y * center.y + p.z * center.z - p.w;
			if (Finite(side) && side < -(radius + slack)) return false;
		}
		return true;
	}

	// The view through a portal sphere seen from `eye`: four planes through the eye
	// bounding the sphere's silhouette, added to `region`. False (caller keeps the
	// wider region) when the eye is inside the sphere or the planes do not fit.
	[[nodiscard]] inline bool Narrow(const Region& region, const Point& eye, const Portal& portal, Region& out) noexcept
	{
		if (region.count + 4 > kMaximumPlanes || !Finite(eye) || !Finite(portal.center)) return false;
		Point forward = Sub(portal.center, eye);
		const float distance = std::sqrt(Dot(forward, forward));
		if (!Finite(distance) || distance <= portal.radius * 1.01F + 1.0F) return false;
		forward = Scale(forward, 1.0F / distance);
		Point helper = std::fabs(forward.z) < 0.9F ? Point{ 0, 0, 1 } : Point{ 1, 0, 0 };
		Point u = Cross(forward, helper);
		if (!Normalize(u)) return false;
		Point v = Cross(u, forward);
		if (!Normalize(v)) return false;
		// A square facing the eye through the sphere centre whose pyramid contains
		// the whole sphere: half-size d*tan(asin(r/d)).
		const float half = portal.radius * distance / std::sqrt(distance * distance - portal.radius * portal.radius);
		if (!Finite(half)) return false;
		const std::array<Point, 4> corners{
			Add(portal.center, Add(Scale(u, -half), Scale(v, -half))),
			Add(portal.center, Add(Scale(u, half), Scale(v, -half))),
			Add(portal.center, Add(Scale(u, half), Scale(v, half))),
			Add(portal.center, Add(Scale(u, -half), Scale(v, half)))
		};
		out = region;
		for (std::size_t i = 0; i < 4; ++i) {
			Point n = Cross(Sub(corners[i], eye), Sub(corners[(i + 1) % 4], eye));
			if (!Normalize(n)) return false;
			float w = Dot(n, eye);
			if (Dot(n, portal.center) - w < 0) { n = Scale(n, -1.0F); w = -w; }
			out.planes[out.count++] = { n.x, n.y, n.z, w };
		}
		return true;
	}

	struct TraversalItem
	{
		std::uint32_t room;
		Region region;
		std::size_t depth;
		std::array<std::uint32_t, kMaximumDepth + 1> path;
	};

	// Storage, not visibility, survives a capture. No engine objects are kept.
	// Repeated traversals of an unchanged graph allocate nothing after warmup.
	class Workspace
	{
	public:
		[[nodiscard]] const Visibility& Traverse(std::size_t rooms, std::span<const Portal> portals,
			std::span<const std::uint32_t> starts, const Region& start, const Point& eye, float slack) noexcept
		{
			result_.valid = false;
			for (auto& regions : result_.regions) regions.clear();
			queue_.clear();
			if (!rooms || rooms > kMaximumRooms || portals.size() > kMaximumPortals || starts.empty() ||
				starts.size() > kMaximumRooms || !Finite(eye) || !Finite(slack) || slack < 0 ||
				start.count > kMaximumPlanes)
				return result_;
			for (std::size_t i = 0; i < start.count; ++i) {
				const auto& p = start.planes[i];
				if (!Finite(p.x) || !Finite(p.y) || !Finite(p.z) || !Finite(p.w)) return result_;
			}
			for (const auto& portal : portals)
				if (portal.a >= rooms || portal.b >= rooms || !Finite(portal.center) || !Finite(portal.radius) ||
					!(portal.radius > 0))
					return result_;
			try {
				result_.regions.resize(rooms);
				for (const auto room : starts) {
					if (room >= rooms) return result_;
					if (!result_.regions[room].empty()) continue;
					result_.regions[room].push_back(start);
					TraversalItem item{ room, start, 0, {} };
					item.path[0] = room;
					queue_.push_back(item);
				}
				std::size_t expansions = 0;
				for (std::size_t head = 0; head < queue_.size(); ++head) {
					const auto item = queue_[head];
					for (const auto& portal : portals) {
						if (portal.a != item.room && portal.b != item.room) continue;
						if (!SphereInside(item.region, portal.center, portal.radius, slack)) continue;
						const auto other = portal.a == item.room ? portal.b : portal.a;
						if (std::find(item.path.begin(), item.path.begin() + item.depth + 1, other) !=
							item.path.begin() + item.depth + 1) continue;
						if (item.depth + 1 > kMaximumDepth || ++expansions > kMaximumExpansions) return result_;
						Region next{};
						if (!Narrow(item.region, eye, portal, next)) next = item.region;
						auto& reached = result_.regions[other];
						if (reached.size() >= kMaximumRegionsPerRoom) return result_;
						reached.push_back(next);
						TraversalItem child{ other, next, item.depth + 1, item.path };
						child.path[child.depth] = other;
						queue_.push_back(child);
					}
				}
				result_.valid = true;
			} catch (...) {
				result_.valid = false;
			}
			return result_;
		}
	private:
		Visibility result_;
		std::vector<TraversalItem> queue_;
	};

	// Value-returning convenience for offline callers. Runtime owns one workspace.
	[[nodiscard]] inline Visibility Traverse(std::size_t rooms, std::span<const Portal> portals,
		std::span<const std::uint32_t> starts, const Region& start, const Point& eye, float slack)
	{
		Workspace workspace;
		return workspace.Traverse(rooms, portals, starts, start, eye, slack);
	}

	// A reference in `room` (its bound) is drawn when some view reaching the room
	// contains it.
	[[nodiscard]] inline bool Visible(const Visibility& visibility, std::uint32_t room,
		const Point& center, float radius, float slack) noexcept
	{
		if (!visibility.valid) return true;
		if (room >= visibility.regions.size()) return true;
		for (const auto& region : visibility.regions[room])
			if (SphereInside(region, center, radius, slack)) return true;
		return false;
	}
	// A reference touching one portal hangs under the portal itself: drawn when
	// either side is reached.
	[[nodiscard]] inline bool EitherReached(const Visibility& visibility, std::uint32_t a, std::uint32_t b) noexcept
	{
		if (!visibility.valid) return true;
		const auto reached = [&](std::uint32_t room) {
			return room >= visibility.regions.size() || !visibility.regions[room].empty();
		};
		return reached(a) || reached(b);
	}
}
