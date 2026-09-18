#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace HandMirrorPhysicalFragmentClipPolicy
{
	inline constexpr float kMinimumCameraSideMargin = 0.125F;
	inline constexpr float kMinimumRetainedVisibleMargin = 0.125F;

	enum class Route : std::uint8_t
	{
		kConventional,
		kPhysicalOblique,
		kReject
	};

	struct AdmissionInputs
	{
		bool featureEnabled{ false };
		bool exactSE1597{ false };
		bool exactAE161170{ false };
		bool raisedPortrait{ false };
		bool physicalPaneFresh{ false };
		bool halfSpaceReady{ false };
		bool retainedBoundsReady{ false };
		bool cameraBehindPlane{ false };
		bool retainedBoundsReachVisibleHalfSpace{ false };
		bool conventionalSoftDepthReady{ false };
	};

	/**
	 * The disabled/lowered result is deliberately the historical conventional route.
	 * An enabled raised portrait never falls back after the physical route has
	 * been selected: missing evidence rejects this refresh so presentation can
	 * reuse the last complete frame instead of publishing an unclipped pole.
	 */
	[[nodiscard]] constexpr Route SelectRoute(
		const AdmissionInputs& inputs) noexcept
	{
		if (!inputs.featureEnabled || !inputs.raisedPortrait)
			return Route::kConventional;
		return (inputs.exactSE1597 || inputs.exactAE161170) &&
			inputs.physicalPaneFresh &&
			inputs.halfSpaceReady && inputs.retainedBoundsReady &&
			inputs.cameraBehindPlane &&
			inputs.retainedBoundsReachVisibleHalfSpace &&
			inputs.conventionalSoftDepthReady ?
			Route::kPhysicalOblique : Route::kReject;
	}

	struct PlaneDomainInputs
	{
		float normalX{ 0.0F };
		float normalY{ 0.0F };
		float normalZ{ 0.0F };
		float distance{ 0.0F };
		float cameraX{ 0.0F };
		float cameraY{ 0.0F };
		float cameraZ{ 0.0F };
		float minimumX{ 0.0F };
		float minimumY{ 0.0F };
		float minimumZ{ 0.0F };
		float maximumX{ 0.0F };
		float maximumY{ 0.0F };
		float maximumZ{ 0.0F };
	};

	struct PlaneDomain
	{
		float cameraSignedDistance{ 0.0F };
		float retainedMaximumSignedDistance{ 0.0F };
		bool cameraBehindPlane{ false };
		bool retainedBoundsReachVisibleHalfSpace{ false };
		bool valid{ false };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return valid && cameraBehindPlane &&
				retainedBoundsReachVisibleHalfSpace;
		}
	};

	enum class HeadClearance : std::uint8_t
	{
		kClear,
		kCameraInsideHead,
		kPaneCutsHead,
		kHeadOutsidePortrait,
		kInvalid
	};

	// Use the already validated face/headgear bounds, never the whole-body box:
	// hands and arms may cross the mirror without slicing the reflected head.
	// A wholly excluded head is normal for a lowered physical mirror.
	[[nodiscard]] inline HeadClearance CheckHeadClearance(
		const PlaneDomainInputs& p, bool portrait) noexcept
	{
		const auto finite = [](float value) noexcept {
			return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
		};
		const float values[]{p.normalX, p.normalY, p.normalZ, p.distance,
			p.cameraX, p.cameraY, p.cameraZ, p.minimumX, p.minimumY, p.minimumZ,
			p.maximumX, p.maximumY, p.maximumZ};
		for (float value : values)
			if (!finite(value))
				return HeadClearance::kInvalid;
		const float length = p.normalX * p.normalX + p.normalY * p.normalY + p.normalZ * p.normalZ;
		if (!finite(length) || std::abs(length - 1.0F) > 1.0e-3F ||
			p.minimumX >= p.maximumX || p.minimumY >= p.maximumY || p.minimumZ >= p.maximumZ)
			return HeadClearance::kInvalid;
		constexpr float margin = kMinimumRetainedVisibleMargin;
		if (p.cameraX >= p.minimumX - margin && p.cameraX <= p.maximumX + margin &&
			p.cameraY >= p.minimumY - margin && p.cameraY <= p.maximumY + margin &&
			p.cameraZ >= p.minimumZ - margin && p.cameraZ <= p.maximumZ + margin)
			return HeadClearance::kCameraInsideHead;
		const float minimum =
			p.normalX * (p.normalX >= 0 ? p.minimumX : p.maximumX) +
			p.normalY * (p.normalY >= 0 ? p.minimumY : p.maximumY) +
			p.normalZ * (p.normalZ >= 0 ? p.minimumZ : p.maximumZ) - p.distance;
		const float maximum =
			p.normalX * (p.normalX >= 0 ? p.maximumX : p.minimumX) +
			p.normalY * (p.normalY >= 0 ? p.maximumY : p.minimumY) +
			p.normalZ * (p.normalZ >= 0 ? p.maximumZ : p.minimumZ) - p.distance;
		if (!finite(minimum) || !finite(maximum))
			return HeadClearance::kInvalid;
		if (minimum > margin)
			return HeadClearance::kClear;
		if (maximum < -margin)
			return portrait ? HeadClearance::kHeadOutsidePortrait : HeadClearance::kClear;
		return HeadClearance::kPaneCutsHead;
	}

	// The portrait camera is synthetic. Its animated third-person pane must not
	// slice the head: keep the raster plane in front of the complete retained head
	// instead of rejecting every frame during an ordinary walking animation.
	[[nodiscard]] inline bool KeepHeadAheadOfPlane(PlaneDomainInputs& p) noexcept
	{
		if (CheckHeadClearance(p, true) == HeadClearance::kInvalid)
			return false;
		const float support =
			p.normalX * (p.normalX >= 0 ? p.minimumX : p.maximumX) +
			p.normalY * (p.normalY >= 0 ? p.minimumY : p.maximumY) +
			p.normalZ * (p.normalZ >= 0 ? p.minimumZ : p.maximumZ);
		p.distance = (std::min)(p.distance, support - 2 * kMinimumRetainedVisibleMargin);
		return CheckHeadClearance(p, true) != HeadClearance::kInvalid;
	}

	struct Point { float x{}, y{}, z{}; };

	// Conventional hand captures clip at the camera's near depth, not at the
	// reflecting surface. Testing the pane in that route rejects safe moving
	// views even though no raster clip can slice the head there.
	[[nodiscard]] inline bool UseCameraNearPlane(
		PlaneDomainInputs& p, Point forward, float nearDepth) noexcept
	{
		const auto finite = [](float v) noexcept {
			return (std::bit_cast<std::uint32_t>(v) & 0x7F800000u) != 0x7F800000u;
		};
		if (!finite(nearDepth) || nearDepth <= 0 || !finite(forward.x) ||
			!finite(forward.y) || !finite(forward.z))
			return false;
		auto cameraPlane = p;
		cameraPlane.normalX = forward.x;
		cameraPlane.normalY = forward.y;
		cameraPlane.normalZ = forward.z;
		cameraPlane.distance = forward.x * p.cameraX + forward.y * p.cameraY +
			forward.z * p.cameraZ + nearDepth;
		if (CheckHeadClearance(cameraPlane, false) == HeadClearance::kInvalid)
			return false;
		p = cameraPlane;
		return true;
	}

	struct Aperture
	{
		Point center{}, tangent{}, bitangent{};
		float halfWidth{}, halfHeight{};
	};

	// A broad helmet/head AABB intersecting the infinite plane is harmless when
	// it lies outside the finite lowered pane. Only a proven separation permits
	// this bypass; invalid or degenerate evidence retains the head guard.
	[[nodiscard]] inline bool HeadOutsideAperture(
		const PlaneDomainInputs& p, const Aperture& pane,
		bool rasterClipsAtPane = true) noexcept
	{
		if (CheckHeadClearance(p, false) == HeadClearance::kInvalid)
			return false;
		const auto finite = [](float v) {
			return (std::bit_cast<std::uint32_t>(v) & 0x7F800000u) != 0x7F800000u;
		};
		for (float v : {pane.center.x, pane.center.y, pane.center.z,
			pane.tangent.x, pane.tangent.y, pane.tangent.z,
			pane.bitangent.x, pane.bitangent.y, pane.bitangent.z,
			pane.halfWidth, pane.halfHeight})
			if (!finite(v)) return false;
		if (pane.halfWidth <= 0 || pane.halfHeight <= 0)
			return false;
		const auto dot = [](Point a, Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; };
		const auto cross = [](Point a, Point b) {
			return Point{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
		};
		const Point normal{p.normalX,p.normalY,p.normalZ};
		if (std::abs(dot(pane.tangent,pane.tangent)-1) > .001f ||
			std::abs(dot(pane.bitangent,pane.bitangent)-1) > .001f ||
			std::abs(dot(pane.tangent,pane.bitangent)) > .001f ||
			std::abs(dot(normal,pane.tangent)) > .001f ||
			std::abs(dot(normal,pane.bitangent)) > .001f)
			return false;
		const Point eye{p.cameraX,p.cameraY,p.cameraZ};
		const Point center{pane.center.x-eye.x,pane.center.y-eye.y,pane.center.z-eye.z};
		if (dot(normal,eye)-p.distance >= -kMinimumCameraSideMargin ||
			dot(normal,center) <= kMinimumCameraSideMargin)
			return false;
		// Relative coordinates avoid subtracting two large absolute plane distances.
		const Point low{p.minimumX-eye.x,p.minimumY-eye.y,p.minimumZ-eye.z};
		const Point high{p.maximumX-eye.x,p.maximumY-eye.y,p.maximumZ-eye.z};
		const auto maxDot = [&](Point n) {
			return n.x*(n.x >= 0 ? high.x : low.x) +
				n.y*(n.y >= 0 ? high.y : low.y) + n.z*(n.z >= 0 ? high.z : low.z);
		};
		// Geometry wholly on the discarded side cannot expose the inside of a head,
		// even when the reflected eye happens to lie inside its bounding box.
		if (rasterClipsAtPane &&
			maxDot(normal) < p.distance-dot(normal,eye)-kMinimumRetainedVisibleMargin)
			return true;
		// Test only the head volume that survives hardware clipping. Using the
		// complete box when the virtual eye is inside it can never separate that
		// box from the aperture, even if every visible part lies above the pane.
		Point visible[20]{};
		unsigned visibleCount=0;
		if (rasterClipsAtPane) {
			Point corners[8]{};
			float distances[8]{};
			const float boundary=p.distance-dot(normal,eye)-kMinimumRetainedVisibleMargin;
			for(unsigned i=0;i<8;++i) {
				corners[i]={(i&1)?high.x:low.x,(i&2)?high.y:low.y,(i&4)?high.z:low.z};
				distances[i]=dot(normal,corners[i])-boundary;
				if (!finite(distances[i])) return false;
				if(distances[i]>=0) visible[visibleCount++]=corners[i];
			}
			for(unsigned i=0;i<8;++i) for(unsigned bit:{1u,2u,4u}) {
				if(i&bit) continue;
				const unsigned j=i|bit;
				if((distances[i]<0)==(distances[j]<0)) continue;
				const float t=distances[i]/(distances[i]-distances[j]);
				visible[visibleCount++]={corners[i].x+t*(corners[j].x-corners[i].x),
					corners[i].y+t*(corners[j].y-corners[i].y),
					corners[i].z+t*(corners[j].z-corners[i].z)};
			}
			if(!visibleCount) return true;
			for(unsigned i=0;i<visibleCount;++i)
				if(!finite(visible[i].x) || !finite(visible[i].y) || !finite(visible[i].z)) return false;
		}
		Point rays[4];
		constexpr float signs[4][2]{{-1,-1},{1,-1},{1,1},{-1,1}};
		for (unsigned i=0;i<4;++i) {
			const float x=signs[i][0]*pane.halfWidth, y=signs[i][1]*pane.halfHeight;
			rays[i]={center.x+x*pane.tangent.x+y*pane.bitangent.x,
				center.y+x*pane.tangent.y+y*pane.bitangent.y,
				center.z+x*pane.tangent.z+y*pane.bitangent.z};
		}
		bool separated=false;
		for (unsigned i=0;i<4;++i) {
			auto side=cross(rays[i],rays[(i+1)%4]);
			const float length=std::sqrt(dot(side,side));
			if (!finite(length) || length <= .0001f) return false;
			const float sign=dot(side,center) >= 0 ? 1.f : -1.f;
			side={side.x*sign/length,side.y*sign/length,side.z*sign/length};
			float maximum=maxDot(side);
			if(rasterClipsAtPane) {
				maximum=dot(side,visible[0]);
				for(unsigned j=1;j<visibleCount;++j)
					maximum=(std::max)(maximum,dot(side,visible[j]));
			}
			if (!finite(maximum)) return false;
			separated |= maximum < -kMinimumRetainedVisibleMargin;
		}
		return separated;
	}

	/**
	 * Validate a normalized retained-positive plane against a complete AABB.
	 *
	 * The physical plane is also the raster clip plane.  A retained-player AABB
	 * may therefore straddle it: the GPU clips only the fragments physically
	 * behind the pane while continuing to render the visible fragments.  The
	 * admission proof must reject only when the complete AABB is behind the
	 * pane, not whenever one arm/body corner crosses it.
	 */
	[[nodiscard]] inline PlaneDomain EvaluatePlaneDomain(
		const PlaneDomainInputs& inputs) noexcept
	{
		const float values[]{
			inputs.normalX, inputs.normalY, inputs.normalZ, inputs.distance,
			inputs.cameraX, inputs.cameraY, inputs.cameraZ,
			inputs.minimumX, inputs.minimumY, inputs.minimumZ,
			inputs.maximumX, inputs.maximumY, inputs.maximumZ
		};
		for (const float value : values) {
			if (!std::isfinite(value))
				return {};
		}
		if (inputs.minimumX > inputs.maximumX ||
			inputs.minimumY > inputs.maximumY ||
			inputs.minimumZ > inputs.maximumZ) {
			return {};
		}
		const float normalLengthSquared =
			inputs.normalX * inputs.normalX +
			inputs.normalY * inputs.normalY +
			inputs.normalZ * inputs.normalZ;
		if (!std::isfinite(normalLengthSquared) ||
			std::abs(normalLengthSquared - 1.0F) > 1.0e-3F) {
			return {};
		}

		const float cameraSignedDistance =
			inputs.normalX * inputs.cameraX +
			inputs.normalY * inputs.cameraY +
			inputs.normalZ * inputs.cameraZ - inputs.distance;
		// The AABB support point along the retained-positive normal has the
		// maximum signed distance.  A positive support proves that at least some
		// retained geometry can survive the raster plane.
		const float retainedMaximumSignedDistance =
			inputs.normalX * (inputs.normalX >= 0.0F ?
				inputs.maximumX : inputs.minimumX) +
			inputs.normalY * (inputs.normalY >= 0.0F ?
				inputs.maximumY : inputs.minimumY) +
			inputs.normalZ * (inputs.normalZ >= 0.0F ?
				inputs.maximumZ : inputs.minimumZ) - inputs.distance;
		if (!std::isfinite(cameraSignedDistance) ||
			!std::isfinite(retainedMaximumSignedDistance)) {
			return {};
		}
		return {
			.cameraSignedDistance = cameraSignedDistance,
			.retainedMaximumSignedDistance = retainedMaximumSignedDistance,
			.cameraBehindPlane =
				cameraSignedDistance < -kMinimumCameraSideMargin,
			.retainedBoundsReachVisibleHalfSpace =
				retainedMaximumSignedDistance > kMinimumRetainedVisibleMargin,
			.valid = true
		};
	}
}
