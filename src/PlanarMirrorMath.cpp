#include "PlanarMirrorMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace
{
	constexpr float kEpsilon = 1.0e-6f;

	bool Finite(float value) noexcept
	{
		return std::isfinite(value);
	}

	bool Finite(const DirectX::XMFLOAT3& value) noexcept
	{
		return Finite(value.x) && Finite(value.y) && Finite(value.z);
	}

	bool Finite(const DirectX::XMFLOAT4& value) noexcept
	{
		return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
	}

	float Dot(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
	}

	DirectX::XMFLOAT3 Scale(const DirectX::XMFLOAT3& value, float scale) noexcept
	{
		return { value.x * scale, value.y * scale, value.z * scale };
	}

	DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return {
			lhs.y * rhs.z - lhs.z * rhs.y,
			lhs.z * rhs.x - lhs.x * rhs.z,
			lhs.x * rhs.y - lhs.y * rhs.x
		};
	}

	bool Normalize(DirectX::XMFLOAT3& value) noexcept
	{
		if (!Finite(value))
			return false;
		const float lengthSquared = Dot(value, value);
		if (!Finite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon)
			return false;
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		value = Scale(value, inverseLength);
		return true;
	}

	DirectX::XMFLOAT3 Add(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) noexcept
	{
		return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
	}

	bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
	{
		const float* values = &matrix._11;
		for (std::size_t index = 0; index < 16; ++index) {
			if (!Finite(values[index]))
				return false;
		}
		return true;
	}
    bool SolvePlane(const DirectX::XMFLOAT4X4& matrix, const std::array<double, 4>& equation,
        std::array<double, 4>& solution) noexcept
    {
        if (!FiniteMatrix(matrix)) return false;
        double rows[4][5]{};
        for (unsigned r = 0; r < 4; ++r) {
            for (unsigned c = 0; c < 4; ++c) rows[r][c] = matrix.m[r][c];
            rows[r][4] = equation[r];
        }
        double determinantMagnitude = 1;
        for (unsigned column = 0; column < 4; ++column) {
            unsigned pivot = column;
            for (unsigned r = column + 1; r < 4; ++r)
                if (std::abs(rows[r][column]) > std::abs(rows[pivot][column])) pivot = r;
            if (rows[pivot][column] == 0) return false;
            if (pivot != column) for (unsigned c = column; c <= 4; ++c) std::swap(rows[pivot][c], rows[column][c]);
            const double diagonal = rows[column][column];
            determinantMagnitude *= std::abs(diagonal);
            for (unsigned c = column; c <= 4; ++c) rows[column][c] /= diagonal;
            for (unsigned r = 0; r < 4; ++r) {
                if (r == column) continue;
                const double factor = rows[r][column];
                for (unsigned c = column; c <= 4; ++c) rows[r][c] -= factor * rows[column][c];
            }
        }
        if (!std::isfinite(determinantMagnitude) || determinantMagnitude <= kEpsilon) return false;
        for (unsigned r = 0; r < 4; ++r) {
            if (!std::isfinite(rows[r][4])) return false;
            solution[r] = rows[r][4];
        }
        return true;
    }

}

namespace PlanarMirrorMath
{
	bool BuildPaneFit(
		const PlaneSelection& plane,
		const std::array<float, 3>& worldHalfExtents,
		const std::array<DirectX::XMFLOAT3, 3>& worldAxes,
		PaneFit& output) noexcept
	{
		output = {};
		if (!plane.renderable || !Finite(plane.center) ||
			!Finite(plane.tangent) || !Finite(plane.bitangent)) {
			return false;
		}
		float halfTangent = 0.0f;
		float halfBitangent = 0.0f;
		for (std::size_t axis = 0; axis < worldHalfExtents.size(); ++axis) {
			const float extent = worldHalfExtents[axis];
			if (!Finite(extent) || extent < 0.0f || !Finite(worldAxes[axis]))
				return false;
			halfTangent += std::abs(Dot(plane.tangent, worldAxes[axis])) * extent;
			halfBitangent +=
				std::abs(Dot(plane.bitangent, worldAxes[axis])) * extent;
		}
		output.center = plane.center;
		output.tangent = plane.tangent;
		output.bitangent = plane.bitangent;
		output.halfTangent = halfTangent;
		output.halfBitangent = halfBitangent;
		return SamePaneFit(output, output);
	}

	bool BuildOrientedBounds(
		const SignedShortBounds& bounds,
		const UniformWorldTransform& transform,
		OrientedBounds& output) noexcept
	{
		output = {};
		if (!Finite(transform.translation) || !Finite(transform.scale) || transform.scale <= kEpsilon)
			return false;

		std::array<DirectX::XMFLOAT3, 3> axes = transform.rotationColumns;
		for (auto& axis : axes) {
			if (!Normalize(axis))
				return false;
		}
		constexpr float kMaximumAxisDot = 1.0e-3f;
		if (std::abs(Dot(axes[0], axes[1])) > kMaximumAxisDot ||
			std::abs(Dot(axes[0], axes[2])) > kMaximumAxisDot ||
			std::abs(Dot(axes[1], axes[2])) > kMaximumAxisDot)
			return false;

		std::array<float, 3> localCenter{};
		std::array<float, 3> localHalfExtents{};
		for (std::size_t axis = 0; axis < 3; ++axis) {
			const float minimum = static_cast<float>(bounds.minimum[axis]);
			const float maximum = static_cast<float>(bounds.maximum[axis]);
			if (!(maximum > minimum))
				return false;
			localCenter[axis] = (minimum + maximum) * 0.5f;
			localHalfExtents[axis] = (maximum - minimum) * 0.5f;
		}

		DirectX::XMFLOAT3 worldCenter = transform.translation;
		for (std::size_t axis = 0; axis < 3; ++axis)
			worldCenter = Add(worldCenter, Scale(axes[axis], localCenter[axis] * transform.scale));
		if (!Finite(worldCenter))
			return false;

		std::array<float, 3> worldHalfExtents{};
		for (std::size_t axis = 0; axis < 3; ++axis) {
			worldHalfExtents[axis] = localHalfExtents[axis] * transform.scale;
			if (!Finite(worldHalfExtents[axis]))
				return false;
		}

		output.localCenter = { localCenter[0], localCenter[1], localCenter[2] };
		output.worldCenter = worldCenter;
		output.worldAxes = axes;
		output.localHalfExtents = localHalfExtents;
		output.worldHalfExtents = worldHalfExtents;
		return true;
	}

	bool NormalizePlane(Plane& plane) noexcept
	{

        const double magnitude = std::hypot(double(plane.normal.x), double(plane.normal.y), double(plane.normal.z));
        if (!std::isfinite(magnitude) || magnitude <= kEpsilon || !Finite(plane.distance)) return false;
        Plane result{ {float(plane.normal.x / magnitude), float(plane.normal.y / magnitude), float(plane.normal.z / magnitude)},
            float(plane.distance / magnitude) };
        if (!Finite(result.normal) || !Finite(result.distance)) return false;
        plane = result;
        return true;
	}

	float SignedDistance(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept
	{
		return Dot(plane.normal, point) - plane.distance;
	}

	bool EvaluateMirrorCaptureClearance(
		const Plane& orientedPlane,
		const DirectX::XMFLOAT3& sourceEye,
		float normalHalfThickness,
		float cameraNearPlane,
		float clipBias,
		MirrorCaptureClearance& output,
		MirrorCaptureClearancePolicy policy) noexcept
	{
		output = {};
		if (!Finite(sourceEye) || !Finite(normalHalfThickness) ||
			!Finite(cameraNearPlane) || !Finite(clipBias) ||
			normalHalfThickness < 0.0f || cameraNearPlane <= 0.0f ||
			clipBias < 0.0f) {
			return false;
		}

		Plane plane = orientedPlane;
		if (!NormalizePlane(plane))
			return false;

		const float sourcePlaneDistance = SignedDistance(plane, sourceEye);
		const float requiredDistance = normalHalfThickness + cameraNearPlane +
			(policy == MirrorCaptureClearancePolicy::kLegacyNearPlaneSlabAndClipBias ?
				clipBias : 0.0f);
		if (!Finite(sourcePlaneDistance) || !Finite(requiredDistance))
			return false;

		output.sourcePlaneDistance = sourcePlaneDistance;
		output.requiredDistance = requiredDistance;
		// Strictly greater keeps the native near plane outside the authored slab.
		// The oblique replacement plane is shifted toward the retained half-space,
		// which increases its distance from the reflected eye. It therefore cannot
		// make this native near-plane boundary larger.
		output.safe = sourcePlaneDistance > requiredDistance;
		return true;
	}

	DirectX::XMFLOAT3 ReflectPoint(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept
	{

        const auto reflectedDirection = ReflectVector(plane, point);
        DirectX::XMFLOAT3 reflected;
        DirectX::XMStoreFloat3(&reflected, DirectX::XMVectorMultiplyAdd(
            DirectX::XMLoadFloat3(&plane.normal), DirectX::XMVectorReplicate(plane.distance * 2),
            DirectX::XMLoadFloat3(&reflectedDirection)));
        return reflected;
	}

	DirectX::XMFLOAT3 ReflectVector(const Plane& plane, const DirectX::XMFLOAT3& direction) noexcept
	{

        DirectX::XMFLOAT3 reflected;
        DirectX::XMStoreFloat3(&reflected, DirectX::XMVector3Reflect(
            DirectX::XMLoadFloat3(&direction), DirectX::XMLoadFloat3(&plane.normal)));
        return reflected;
	}

	bool SelectPlaneFromBasis(const PlaneSelectionInput& input, PlaneSelection& output) noexcept
	{

        output = {};
        if (!Finite(input.center) || !Finite(input.eye)) return false;
        auto directions = input.worldAxes;
        for (auto& direction : directions) if (!Normalize(direction)) return false;
        auto sight = Subtract(input.eye, input.center);
        const bool eyeDefined = Normalize(sight);
        const auto& sizes = input.localHalfExtents;
        int chosen = input.normalAxisHint;
        float thinScore = 0;
        if (chosen < 0 || chosen >= 3) {
            const bool usable = std::all_of(sizes.begin(), sizes.end(), [](float size) { return Finite(size) && size > kEpsilon; });
            if (usable) {
                const int smallest = int(std::min_element(sizes.begin(), sizes.end()) - sizes.begin());
                const float ratio = sizes[smallest] / std::min(sizes[(smallest+1)%3], sizes[(smallest+2)%3]);
                if (ratio <= std::clamp(input.maximumThicknessRatio, .01f, .95f)) {
                    chosen = smallest;
                    thinScore = 1 - ratio;
                }
            }
            if (chosen < 0 || chosen >= 3) {
                if (input.requireThinAxis || !eyeDefined) return false;
                const std::array<float, 3> weights{std::abs(Dot(sight,directions[0])), std::abs(Dot(sight,directions[1])), std::abs(Dot(sight,directions[2]))};
                chosen = int(std::max_element(weights.begin(), weights.end()) - weights.begin());
            }
        }
        const auto authored = directions[chosen];
        std::array<int, 2> candidates{(chosen+1)%3, (chosen+2)%3};
        if (sizes[candidates[1]] > sizes[candidates[0]]) std::swap(candidates[0], candidates[1]);
        DirectX::XMFLOAT3 across{}, up{};
        bool basisValid = false;
        for (int axis : candidates) {
            up = Cross(authored, directions[axis]);
            if (!Normalize(up)) continue;
            across = Cross(up, authored);
            if (!Normalize(across)) continue;
            if (Dot(up, directions[3-chosen-axis]) < 0) {
                across = Scale(across,-1);
                up = Scale(up,-1);
            }
            basisValid = true;
            break;
        }
        if (!basisValid) return false;
        const float incidence = eyeDefined ? Dot(authored,sight) : 0;
        if (!Finite(incidence)) return false;
        PlaneSelection selected{};
        selected.center = input.center;
        selected.authoredNormal = authored;
        selected.normalAxis = chosen;
        selected.tangent = across;
        selected.bitangent = up;
        selected.plane.normal = Scale(authored, incidence < 0 ? -1.f : 1.f);
        selected.plane.distance = Dot(selected.plane.normal,input.center);
        selected.facingCosine = std::abs(incidence);
        selected.viewOriented = eyeDefined;
        selected.renderable = eyeDefined && selected.facingCosine >= std::clamp(input.minimumFacingCosine,0.f,.99f);
        selected.confidence = std::clamp(thinScore > 0 ? .65f*thinScore + .35f*selected.facingCosine : selected.facingCosine,0.f,1.f);
        if (!NormalizePlane(selected.plane)) return false;
        output = selected;
        return true;
	}

	bool BuildPaneCullSlopes(
		const PaneFit& pane,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT3& forward,
		const DirectX::XMFLOAT3& up,
		const DirectX::XMFLOAT3& right,
		float marginScale,
		PaneCullSlopes& output) noexcept
	{
		output = {};
		if (!Finite(pane.center) || !Finite(pane.tangent) || !Finite(pane.bitangent) ||
			!Finite(pane.halfTangent) || pane.halfTangent <= 0.0f ||
			!Finite(pane.halfBitangent) || pane.halfBitangent <= 0.0f ||
			!Finite(cameraOrigin) || !Finite(forward) || !Finite(up) || !Finite(right) ||
			!Finite(marginScale) || marginScale < 1.0f)
			return false;

		// A corner at or behind the camera's forward plane must NOT fail the
		// build: falling back to the narrow copied frustum for those poses made
		// pane-edge content (fence, plant, NPCs) cull at "certain angles", and
		// forward/back movement swept corners across the threshold so frames
		// alternated between widened and narrow culling — visible as popping
		// corruption (2026-08-18 owner run, 5.3% fallback rate). Clamp the
		// forward distance and saturate the slopes instead so the widened cull
		// is continuous across every pose; the pane-fitted projection rejects
		// unrenderable views on its own.
		constexpr float kMinimumForwardDistance = 1.0e-3f;
		constexpr float kMaximumSlope = kMaximumCullSlope;
		float minRight = 0.0f;
		float maxRight = 0.0f;
		float minUp = 0.0f;
		float maxUp = 0.0f;
		bool first = true;
		for (float tangentSign : { -1.0f, 1.0f }) {
			for (float bitangentSign : { -1.0f, 1.0f }) {
				const DirectX::XMFLOAT3 corner = Add(
					pane.center,
					Add(Scale(pane.tangent, tangentSign * pane.halfTangent),
						Scale(pane.bitangent, bitangentSign * pane.halfBitangent)));
				const DirectX::XMFLOAT3 toCorner = Subtract(corner, cameraOrigin);
				const float forwardRaw = Dot(toCorner, forward);
				const float lateralRight = Dot(toCorner, right);
				const float lateralUp = Dot(toCorner, up);
				if (!Finite(forwardRaw) || !Finite(lateralRight) ||
					!Finite(lateralUp))
					return false;
				const float forwardDistance =
					std::max(forwardRaw, kMinimumForwardDistance);
				const float rightSlope = std::clamp(
					lateralRight / forwardDistance, -kMaximumSlope, kMaximumSlope);
				const float upSlope = std::clamp(
					lateralUp / forwardDistance, -kMaximumSlope, kMaximumSlope);
				minRight = first ? rightSlope : std::min(minRight, rightSlope);
				maxRight = first ? rightSlope : std::max(maxRight, rightSlope);
				minUp = first ? upSlope : std::min(minUp, upSlope);
				maxUp = first ? upSlope : std::max(maxUp, upSlope);
				first = false;
			}
		}

		const float rightCentre = 0.5f * (minRight + maxRight);
		const float rightHalf = 0.5f * (maxRight - minRight) * marginScale;
		const float upCentre = 0.5f * (minUp + maxUp);
		const float upHalf = 0.5f * (maxUp - minUp) * marginScale;
		if (!Finite(rightHalf) || rightHalf <= 0.0f ||
			!Finite(upHalf) || upHalf <= 0.0f)
			return false;
		output.minRight = std::clamp(
			rightCentre - rightHalf, -kMaximumSlope, kMaximumSlope);
		output.maxRight = std::clamp(
			rightCentre + rightHalf, -kMaximumSlope, kMaximumSlope);
		output.minUp = std::clamp(upCentre - upHalf, -kMaximumSlope, kMaximumSlope);
		output.maxUp = std::clamp(upCentre + upHalf, -kMaximumSlope, kMaximumSlope);
		return Finite(output.minRight) && Finite(output.maxRight) &&
			Finite(output.minUp) && Finite(output.maxUp) &&
			output.minRight < output.maxRight && output.minUp < output.maxUp;
	}

	bool BuildPaneVisibilityCone(
		const PaneFit& pane,
		const DirectX::XMFLOAT3& reflectedEye,
		PaneVisibilityCone& output) noexcept
	{
		output = {};
		if (!Finite(pane.center) || !Finite(pane.tangent) || !Finite(pane.bitangent) ||
			!Finite(pane.halfTangent) || pane.halfTangent <= 0.0f ||
			!Finite(pane.halfBitangent) || pane.halfBitangent <= 0.0f ||
			!Finite(reflectedEye))
			return false;

		const std::array<DirectX::XMFLOAT3, 4> corners{
			Add(pane.center,
				Add(Scale(pane.tangent, -pane.halfTangent),
					Scale(pane.bitangent, -pane.halfBitangent))),
			Add(pane.center,
				Add(Scale(pane.tangent, pane.halfTangent),
					Scale(pane.bitangent, -pane.halfBitangent))),
			Add(pane.center,
				Add(Scale(pane.tangent, pane.halfTangent),
					Scale(pane.bitangent, pane.halfBitangent))),
			Add(pane.center,
				Add(Scale(pane.tangent, -pane.halfTangent),
					Scale(pane.bitangent, pane.halfBitangent)))
		};

		const auto emit = [&output](
			DirectX::XMFLOAT3 a_normal, const DirectX::XMFLOAT3& a_point,
			const DirectX::XMFLOAT3& a_inside) noexcept {
			if (!Normalize(a_normal))
				return false;
			float distance = Dot(a_normal, a_point);
			if (Dot(a_normal, a_inside) - distance < 0.0f) {
				a_normal = Scale(a_normal, -1.0f);
				distance = -distance;
			}
			output.planes[output.planeCount++] = {
				a_normal.x, a_normal.y, a_normal.z, distance };
			return true;
		};

		// Side planes through the eye and each pane edge, oriented so the pane
		// centre is inside.
		for (std::size_t index = 0; index < corners.size(); ++index) {
			const auto& first = corners[index];
			const auto& second = corners[(index + 1) % corners.size()];
			const DirectX::XMFLOAT3 normal = Cross(
				Subtract(first, reflectedEye), Subtract(second, reflectedEye));
			if (!emit(normal, reflectedEye, pane.center))
				return false;
		}
		// The pane plane itself, oriented so the far side (visible content,
		// away from the reflected eye) is inside.
		DirectX::XMFLOAT3 paneNormal = Cross(pane.tangent, pane.bitangent);
		if (!Normalize(paneNormal))
			return false;
		const float paneDistance = Dot(paneNormal, pane.center);
		const float eyeSide = Dot(paneNormal, reflectedEye) - paneDistance;
		if (!Finite(eyeSide) || std::abs(eyeSide) <= kEpsilon)
			return false;
		if (eyeSide > 0.0f)
			paneNormal = Scale(paneNormal, -1.0f);
		output.planes[output.planeCount++] = {
			paneNormal.x, paneNormal.y, paneNormal.z,
			Dot(paneNormal, pane.center) };
		return output.planeCount == 5;
	}

	bool SphereIntersectsPaneCone(
		const PaneVisibilityCone& cone,
		const DirectX::XMFLOAT3& center,
		float radius,
		float slack,
		std::size_t* rejectPlane,
		float* rejectDistance) noexcept
	{
		if (cone.planeCount == 0 || !Finite(center) ||
			!Finite(radius) || radius < 0.0f || !Finite(slack) || slack < 0.0f)
			return true;
		for (std::size_t index = 0; index < cone.planeCount; ++index) {
			const auto& plane = cone.planes[index];
			const float signedDistance =
				plane.x * center.x + plane.y * center.y + plane.z * center.z -
				plane.w;
			if (!Finite(signedDistance))
				return true;
			if (signedDistance < -(radius + slack)) {
				if (rejectPlane)
					*rejectPlane = index;
				if (rejectDistance)
					*rejectDistance = signedDistance;
				return false;
			}
		}
		return true;
	}

	bool TransformPlaneToView(
		const Plane& plane,
		const DirectX::XMFLOAT4X4& view,
		DirectX::XMFLOAT4& cameraPlane) noexcept
	{

        Plane unit = plane;
        if (!NormalizePlane(unit)) return false;
        std::array<double, 4> coefficients{unit.normal.x, unit.normal.y, unit.normal.z, -unit.distance};
        std::array<double, 4> solved{};
        if (!SolvePlane(view, coefficients, solved)) return false;
        const double scale = std::hypot(solved[0], solved[1], solved[2]);
        if (!std::isfinite(scale) || scale <= kEpsilon) return false;
        DirectX::XMFLOAT4 result{float(solved[0]/scale), float(solved[1]/scale), float(solved[2]/scale), float(solved[3]/scale)};
        if (!Finite(result)) return false;
        cameraPlane = result;
        return true;
	}

	bool BuildObliqueNearProjection(
		const DirectX::XMFLOAT4X4& projection,
		const DirectX::XMFLOAT4& cameraPlane,
		DirectX::XMFLOAT4X4& output) noexcept
	{

        if (!Finite(cameraPlane) || std::hypot(cameraPlane.x, cameraPlane.y, cameraPlane.z) <= kEpsilon) return false;
        const std::array<double, 4> equation{cameraPlane.x, cameraPlane.y, cameraPlane.z, cameraPlane.w};
        std::array<double, 4> clipEquation{};
        if (!SolvePlane(projection, equation, clipEquation)) return false;
        // D3D's retained near half-space is z >= 0. Scale its replacement so
        // the opposite far corner remains at z == w, without changing x/y/w.
        const double divisor = clipEquation[0] * (cameraPlane.x < 0 ? -1 : 1) +
            clipEquation[1] * (cameraPlane.y < 0 ? -1 : 1) + clipEquation[2] + clipEquation[3];
        if (!std::isfinite(divisor) || divisor <= kEpsilon) return false;
        auto result = projection;
        for (unsigned row = 0; row != 4; ++row) result.m[row][2] = float(equation[row] / divisor);
        if (!FiniteMatrix(result)) return false;
        output = result;
        return true;
	}

	bool BuildPaneFittedProjection(
		const PaneFit& pane,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& referenceProjection,
		float marginScale,
		DirectX::XMFLOAT4X4& output,
		PaneFitFailure* failure,
		bool* cornerClamped) noexcept
	{
		if (cornerClamped)
			*cornerClamped = false;
		constexpr float kMinimumSlopeHalf = 1.0e-4f;
		constexpr float kMaximumSlopeHalf = 16.0f;
		if (failure)
			*failure = PaneFitFailure::kNone;
		const auto reject = [failure](PaneFitFailure reason) noexcept {
			if (failure)
				*failure = reason;
			return false;
		};
		if (!Finite(cameraOrigin) || !Finite(pane.center) ||
			!Finite(pane.tangent) || !Finite(pane.bitangent) ||
			!std::isfinite(pane.halfTangent) || !std::isfinite(pane.halfBitangent) ||
			pane.halfTangent <= kEpsilon || pane.halfBitangent <= kEpsilon ||
			!std::isfinite(marginScale) || marginScale < 1.0f ||
			!FiniteMatrix(view) || !FiniteMatrix(referenceProjection))
			return reject(PaneFitFailure::kInvalidInput);

		// Row-vector standard-Z projection: _33 = f/(f-n), _43 = -n*f/(f-n).
		const float a33 = referenceProjection._33;
		const float a43 = referenceProjection._43;
		if (!std::isfinite(a33) || !std::isfinite(a43) ||
			std::abs(a33) <= kEpsilon || std::abs(a33 - 1.0f) <= kEpsilon)
			return reject(PaneFitFailure::kInvalidReferenceProjection);
		const float nearPlane = -a43 / a33;
		const float farPlane = a33 * nearPlane / (a33 - 1.0f);
		if (!(nearPlane > kEpsilon) || !(farPlane > nearPlane))
			return reject(PaneFitFailure::kInvalidReferenceProjection);

		using namespace DirectX;
		const XMMATRIX viewMatrix = XMLoadFloat4x4(&view);
		std::array<XMFLOAT3, 4> viewCorners{};
		float deepestCorner = 0.0f;
		std::size_t cornerIndex = 0;
		for (int signBitangent = -1; signBitangent <= 1; signBitangent += 2) {
			for (int signTangent = -1; signTangent <= 1; signTangent += 2) {
				const float st = static_cast<float>(signTangent) * pane.halfTangent;
				const float sb = static_cast<float>(signBitangent) * pane.halfBitangent;
				const XMVECTOR corner = XMVectorSet(
					pane.center.x + pane.tangent.x * st + pane.bitangent.x * sb -
						cameraOrigin.x,
					pane.center.y + pane.tangent.y * st + pane.bitangent.y * sb -
						cameraOrigin.y,
					pane.center.z + pane.tangent.z * st + pane.bitangent.z * sb -
						cameraOrigin.z,
					1.0f);
				const XMVECTOR viewCorner = XMVector4Transform(corner, viewMatrix);
				XMStoreFloat3(&viewCorners[cornerIndex], viewCorner);
				if (!std::isfinite(viewCorners[cornerIndex].x) ||
					!std::isfinite(viewCorners[cornerIndex].y) ||
					!std::isfinite(viewCorners[cornerIndex].z))
					return reject(PaneFitFailure::kPaneNotFullyInFront);
				deepestCorner = std::max(deepestCorner, viewCorners[cornerIndex].z);
				++cornerIndex;
			}
		}
		// Only a pane entirely at/behind the camera plane is unfittable; a
		// grazing pose with one near corner clamps instead (the 2026-08-19
		// all-or-nothing rejection here made every such frame fall back to the
		// unfitted projection and drop pane-edge content).
		if (deepestCorner <= kEpsilon)
			return reject(PaneFitFailure::kPaneNotFullyInFront);
		float minimumX = 0.0f;
		float maximumX = 0.0f;
		float minimumY = 0.0f;
		float maximumY = 0.0f;
		bool first = true;
		for (const auto& viewCorner : viewCorners) {
			float depth = viewCorner.z;
			const bool nearClamped = depth < kMinimumPaneFitCornerDepth;
			if (nearClamped) {
				depth = kMinimumPaneFitCornerDepth;
				if (cornerClamped)
					*cornerClamped = true;
			}
			float slopeX = viewCorner.x / depth;
			float slopeY = viewCorner.y / depth;
			if (!std::isfinite(slopeX) || !std::isfinite(slopeY))
				return reject(PaneFitFailure::kInvalidSlope);
			if (nearClamped) {
				// A clamped corner widens the frustum toward its side only up
				// to the slope cap; feeding its exploded slope into the
				// extent would drag the fit's center and push the fully
				// visible deep corners outside NDC.
				slopeX = std::clamp(slopeX, -kMaximumSlopeHalf, kMaximumSlopeHalf);
				slopeY = std::clamp(slopeY, -kMaximumSlopeHalf, kMaximumSlopeHalf);
			}
			if (first) {
				minimumX = maximumX = slopeX;
				minimumY = maximumY = slopeY;
				first = false;
			} else {
				minimumX = std::min(minimumX, slopeX);
				maximumX = std::max(maximumX, slopeX);
				minimumY = std::min(minimumY, slopeY);
				maximumY = std::max(maximumY, slopeY);
			}
		}

		const float centerX = 0.5f * (minimumX + maximumX);
		const float centerY = 0.5f * (minimumY + maximumY);
		const float halfX = std::clamp(
			0.5f * (maximumX - minimumX) * marginScale,
			kMinimumSlopeHalf, kMaximumSlopeHalf);
		const float halfY = std::clamp(
			0.5f * (maximumY - minimumY) * marginScale,
			kMinimumSlopeHalf, kMaximumSlopeHalf);
		const float left = centerX - halfX;
		const float right = centerX + halfX;
		const float bottom = centerY - halfY;
		const float top = centerY + halfY;

		output = {};
		output._11 = 2.0f / (right - left);
		output._22 = 2.0f / (top - bottom);
		output._31 = -(right + left) / (right - left);
		output._32 = -(top + bottom) / (top - bottom);
		output._33 = a33;
		output._34 = 1.0f;
		output._43 = a43;
		return FiniteMatrix(output) ? true : reject(PaneFitFailure::kInvalidOutput);
	}

	bool BuildObliqueViewProjection(
		const Plane& absoluteWorldPlane,
		float clipBias,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection,
		DirectX::XMFLOAT4X4& outputProjection,
		DirectX::XMFLOAT4X4& outputViewProjection,
		DirectX::XMFLOAT4* outputCameraPlane) noexcept
	{

        if (!Finite(cameraOrigin) || !Finite(clipBias)) return false;
        Plane translated = absoluteWorldPlane;
        if (!NormalizePlane(translated)) return false;
        translated.distance += clipBias - Dot(translated.normal,cameraOrigin);
        DirectX::XMFLOAT4 coefficients;
        DirectX::XMFLOAT4X4 clipped;
        if (!TransformPlaneToView(translated,view,coefficients)) return false;
        if (!BuildObliqueNearProjection(projection,coefficients,clipped)) return false;
        DirectX::XMFLOAT4X4 product{};
        for (unsigned r=0;r<4;++r)
            for (unsigned c=0;c<4;++c)
                for (unsigned k=0;k<4;++k) product.m[r][c] += view.m[r][k]*clipped.m[k][c];
        if (!FiniteMatrix(product)) return false;
        outputProjection = clipped;
        outputViewProjection = product;
        if (outputCameraPlane != nullptr) *outputCameraPlane = coefficients;
        return true;
	}
}
