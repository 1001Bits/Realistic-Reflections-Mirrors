#include "MirrorCameraMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace MirrorCameraOverride
{
	namespace
	{
		constexpr float kAxisTolerance = 2.0e-3F;
		constexpr float kMatrixTolerance = 5.0e-4F;
		constexpr float kProjectionShapeTolerance = 1.0e-5F;

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT3& value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y) &&
				std::isfinite(value.z);
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT4X4& value) noexcept
		{
			const float* values = &value._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!std::isfinite(values[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] float Dot(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right) noexcept
		{
			return left.x * right.x + left.y * right.y + left.z * right.z;
		}

		[[nodiscard]] bool NearlyEqual(
			const float left,
			const float right,
			const float tolerance) noexcept
		{
			if (!std::isfinite(left) || !std::isfinite(right) ||
				!std::isfinite(tolerance) || tolerance < 0.0F) {
				return false;
			}
			const float scale = std::max({ 1.0F, std::abs(left), std::abs(right) });
			return std::abs(left - right) <= tolerance * scale;
		}

		[[nodiscard]] bool SameVector(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right,
			const float tolerance = kAxisTolerance) noexcept
		{
			return NearlyEqual(left.x, right.x, tolerance) &&
				NearlyEqual(left.y, right.y, tolerance) &&
				NearlyEqual(left.z, right.z, tolerance);
		}

		[[nodiscard]] bool OrthonormalBasis(
			const DirectX::XMFLOAT3& forward,
			const DirectX::XMFLOAT3& up,
			const DirectX::XMFLOAT3& right) noexcept
		{
			if (!Finite(forward) || !Finite(up) || !Finite(right) ||
				!NearlyEqual(Dot(forward, forward), 1.0F, kAxisTolerance) ||
				!NearlyEqual(Dot(up, up), 1.0F, kAxisTolerance) ||
				!NearlyEqual(Dot(right, right), 1.0F, kAxisTolerance) ||
				std::abs(Dot(forward, up)) > kAxisTolerance ||
				std::abs(Dot(forward, right)) > kAxisTolerance ||
				std::abs(Dot(up, right)) > kAxisTolerance) {
				return false;
			}
			const DirectX::XMFLOAT3 cross{
				forward.y * up.z - forward.z * up.y,
				forward.z * up.x - forward.x * up.z,
				forward.x * up.y - forward.y * up.x };
			return NearlyEqual(std::abs(Dot(cross, right)), 1.0F, kAxisTolerance);
		}

		[[nodiscard]] bool ProperBasis(
			const DirectX::XMFLOAT3& forward,
			const DirectX::XMFLOAT3& up,
			const DirectX::XMFLOAT3& right) noexcept
		{
			if (!OrthonormalBasis(forward, up, right))
				return false;
			const DirectX::XMFLOAT3 cross{
				forward.y * up.z - forward.z * up.y,
				forward.z * up.x - forward.x * up.z,
				forward.x * up.y - forward.y * up.x };
			return NearlyEqual(Dot(cross, right), 1.0F, kAxisTolerance);
		}

		[[nodiscard]] CameraPose PoseAxesFromView(
			const DirectX::XMFLOAT4X4& view) noexcept
		{
			// DirectX row-vector view convention: the world-space right/up/forward
			// axes are the first three columns of the view matrix.
			return {
				.origin = {},
				.forward = { view._13, view._23, view._33 },
				.up = { view._12, view._22, view._32 },
				.right = { view._11, view._21, view._31 }
			};
		}

		[[nodiscard]] bool SameMatrix(
			const DirectX::XMFLOAT4X4& left,
			const DirectX::XMFLOAT4X4& right) noexcept
		{
			const float* leftValues = &left._11;
			const float* rightValues = &right._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!NearlyEqual(
						leftValues[index], rightValues[index], kMatrixTolerance)) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool StandardPerspectiveProjectionShape(
			const DirectX::XMFLOAT4X4& projection) noexcept
		{
			return Finite(projection) && projection._11 > 0.0F &&
				projection._22 > 0.0F &&
				std::abs(projection._33) > kProjectionShapeTolerance &&
				std::abs(projection._33 - 1.0F) > kProjectionShapeTolerance &&
				NearlyEqual(projection._34, 1.0F, kProjectionShapeTolerance) &&
				NearlyEqual(projection._44, 0.0F, kProjectionShapeTolerance) &&
				std::abs(projection._12) <= kProjectionShapeTolerance &&
				std::abs(projection._13) <= kProjectionShapeTolerance &&
				std::abs(projection._14) <= kProjectionShapeTolerance &&
				std::abs(projection._21) <= kProjectionShapeTolerance &&
				std::abs(projection._23) <= kProjectionShapeTolerance &&
				std::abs(projection._24) <= kProjectionShapeTolerance &&
				std::abs(projection._41) <= kProjectionShapeTolerance &&
				std::abs(projection._42) <= kProjectionShapeTolerance;
		}

		[[nodiscard]] bool BuildPerspectiveFrustum(
			const DirectX::XMFLOAT4X4& projection,
			RasterPerspectiveFrustum& output) noexcept
		{
			output = {};
			if (!StandardPerspectiveProjectionShape(projection)) {
				return false;
			}

			const float nearPlane = -projection._43 / projection._33;
			const float farPlane =
				projection._33 * nearPlane / (projection._33 - 1.0F);
			if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
				nearPlane <= 1.0e-5F || farPlane <= nearPlane) {
				return false;
			}
			const float left =
				nearPlane * (-1.0F - projection._31) / projection._11;
			const float right =
				nearPlane * (1.0F - projection._31) / projection._11;
			const float bottom =
				nearPlane * (-1.0F - projection._32) / projection._22;
			const float top =
				nearPlane * (1.0F - projection._32) / projection._22;
			if (!std::isfinite(left) || !std::isfinite(right) ||
				!std::isfinite(top) || !std::isfinite(bottom) || left >= right ||
				bottom >= top) {
				return false;
			}
			output = {
				.left = left,
				.right = right,
				.top = top,
				.bottom = bottom,
				.nearPlane = nearPlane,
				.farPlane = farPlane,
				.valid = true
			};
			return true;
		}
	}

	RasterCameraValidationStatus ValidateRasterCameraSample(
		const RasterCameraInput& input,
		RasterCameraSample& output) noexcept
	{
		output = {};
		if (!Finite(input.origin) || !Finite(input.view) ||
			!Finite(input.projection) || !Finite(input.viewProjection))
			return RasterCameraValidationStatus::kInputNotFinite;
		if (!OrthonormalBasis(input.viewForward, input.viewUp, input.viewRight))
			return RasterCameraValidationStatus::kInputBasisInvalid;
		const CameraPose viewAxes = PoseAxesFromView(input.view);
		if (!OrthonormalBasis(viewAxes.forward, viewAxes.up, viewAxes.right))
			return RasterCameraValidationStatus::kViewBasisInvalid;
		if (!SameVector(input.viewForward, viewAxes.forward) ||
			!SameVector(input.viewUp, viewAxes.up) ||
			!SameVector(input.viewRight, viewAxes.right))
			return RasterCameraValidationStatus::kExplicitAxisMismatch;

		DirectX::XMFLOAT4X4 multiplied{};
		DirectX::XMStoreFloat4x4(
			&multiplied,
			DirectX::XMMatrixMultiply(
				DirectX::XMLoadFloat4x4(&input.view),
				DirectX::XMLoadFloat4x4(&input.projection)));
		if (!SameMatrix(multiplied, input.viewProjection))
			return RasterCameraValidationStatus::kViewProjectionProductMismatch;
		if (!StandardPerspectiveProjectionShape(input.projection))
			return RasterCameraValidationStatus::kProjectionShapeInvalid;
		RasterPerspectiveFrustum frustum{};
		if (!BuildPerspectiveFrustum(input.projection, frustum))
			return RasterCameraValidationStatus::kFrustumInvalid;

		// Reverse-engineered SetCameraData copies NiCamera right directly into
		// ViewData::viewRight and the first view-matrix column.  A negative
		// F x U dot R is therefore invalid, not a second raster convention.
		if (!ProperBasis(
				input.viewForward, input.viewUp, input.viewRight) ||
			!ProperBasis(viewAxes.forward, viewAxes.up, viewAxes.right) ||
			!SameVector(viewAxes.right, input.viewRight)) {
			return RasterCameraValidationStatus::kRightSignConventionInvalid;
		}

		output = {
			.pose = {
				.origin = input.origin,
				.forward = input.viewForward,
				.up = input.viewUp,
				.right = input.viewRight },
			.view = input.view,
			.projection = input.projection,
			.viewProjection = input.viewProjection,
			.frustum = frustum,
			.valid = true
		};
		return RasterCameraValidationStatus::kReady;
	}

	bool BuildValidatedRasterCameraSample(
		const RasterCameraInput& input,
		RasterCameraSample& output) noexcept
	{
		return ValidateRasterCameraSample(input, output) ==
			RasterCameraValidationStatus::kReady;
	}

	bool ViewMatrixMatchesCameraPose(
		const DirectX::XMFLOAT4X4& view,
		const CameraPose& pose) noexcept
	{
		if (!Finite(view) || !ProperBasis(pose.forward, pose.up, pose.right))
			return false;
		const CameraPose viewAxes = PoseAxesFromView(view);
		return ProperBasis(viewAxes.forward, viewAxes.up, viewAxes.right) &&
			SameVector(viewAxes.forward, pose.forward) &&
			SameVector(viewAxes.up, pose.up) &&
			SameVector(viewAxes.right, pose.right);
	}

	bool BuildReflectedCameraPose(
		const CameraPose& source,
		const PlanarMirrorMath::Plane& mirrorPlane,
		CameraPose& output) noexcept
	{
		using namespace DirectX;
		output = {};
		PlanarMirrorMath::Plane plane = mirrorPlane;
		if (!PlanarMirrorMath::NormalizePlane(plane))
			return false;

		auto finite3 = [](const XMFLOAT3& value) noexcept {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		if (!finite3(source.origin) || !finite3(source.forward) ||
			!finite3(source.up) || !finite3(source.right))
			return false;

		XMVECTOR sourceForward = XMLoadFloat3(&source.forward);
		XMVECTOR sourceUp = XMLoadFloat3(&source.up);
		XMVECTOR sourceRight = XMLoadFloat3(&source.right);
		const float forwardLength = XMVectorGetX(XMVector3Length(sourceForward));
		const float upLength = XMVectorGetX(XMVector3Length(sourceUp));
		const float rightLength = XMVectorGetX(XMVector3Length(sourceRight));
		if (!std::isfinite(forwardLength) || !std::isfinite(upLength) || !std::isfinite(rightLength) ||
			forwardLength <= 1.0e-6f || upLength <= 1.0e-6f || rightLength <= 1.0e-6f)
			return false;
		sourceForward = XMVectorScale(sourceForward, 1.0f / forwardLength);
		sourceUp = XMVectorScale(sourceUp, 1.0f / upLength);
		sourceRight = XMVectorScale(sourceRight, 1.0f / rightLength);

		const float handedness = XMVectorGetX(
			XMVector3Dot(XMVector3Cross(sourceForward, sourceUp), sourceRight));
		if (!std::isfinite(handedness) || std::abs(handedness) <= 1.0e-4f)
			return false;

		XMFLOAT3 reflectedForwardValue = PlanarMirrorMath::ReflectVector(plane, source.forward);
		XMFLOAT3 reflectedUpValue = PlanarMirrorMath::ReflectVector(plane, source.up);
		XMVECTOR reflectedForward = XMVector3Normalize(XMLoadFloat3(&reflectedForwardValue));
		XMVECTOR reflectedUp = XMLoadFloat3(&reflectedUpValue);
		reflectedUp = XMVectorSubtract(
			reflectedUp,
			XMVectorScale(reflectedForward, XMVectorGetX(XMVector3Dot(reflectedUp, reflectedForward))));
		const float reflectedUpLength = XMVectorGetX(XMVector3Length(reflectedUp));
		if (!std::isfinite(reflectedUpLength) || reflectedUpLength <= 1.0e-6f)
			return false;
		reflectedUp = XMVectorScale(reflectedUp, 1.0f / reflectedUpLength);

		XMVECTOR reflectedRight = XMVector3Cross(reflectedForward, reflectedUp);
		if (handedness < 0.0f)
			reflectedRight = XMVectorNegate(reflectedRight);
		reflectedRight = XMVector3Normalize(reflectedRight);
		if (handedness >= 0.0f)
			reflectedUp = XMVector3Cross(reflectedRight, reflectedForward);
		else
			reflectedUp = XMVector3Cross(reflectedForward, reflectedRight);
		reflectedUp = XMVector3Normalize(reflectedUp);

		output.origin = PlanarMirrorMath::ReflectPoint(plane, source.origin);
		XMStoreFloat3(&output.forward, reflectedForward);
		XMStoreFloat3(&output.up, reflectedUp);
		XMStoreFloat3(&output.right, reflectedRight);
		return finite3(output.origin) && finite3(output.forward) &&
		       finite3(output.up) && finite3(output.right);
	}

	bool BuildPaneAlignedReflectedCameraPose(
		const CameraPose& source, const PlanarMirrorMath::Plane& mirrorPlane,
		const PlanarMirrorMath::PaneFit& pane, CameraPose& output) noexcept
	{
		using namespace DirectX;
		output = {};
		CameraPose reflected{};
		auto plane = mirrorPlane;
		if (!PlanarMirrorMath::NormalizePlane(plane) || !Finite(pane.bitangent) ||
			!BuildReflectedCameraPose(source, plane, reflected) ||
			!IsCameraBehindMirrorPlane(plane, reflected.origin)) return false;
		const auto forward = XMLoadFloat3(&plane.normal);
		auto up = XMLoadFloat3(&pane.bitangent);
		up -= forward * XMVector3Dot(up, forward);
		const auto length = XMVectorGetX(XMVector3Length(up));
		if (!std::isfinite(length) || length < 1.0e-5F) return false;
		up /= length;
		auto right = XMVector3Cross(forward, up);
		const auto handedness = XMVectorGetX(XMVector3Dot(
			XMVector3Cross(XMLoadFloat3(&reflected.forward), XMLoadFloat3(&reflected.up)),
			XMLoadFloat3(&reflected.right)));
		if (handedness < 0.0F) right = XMVectorNegate(right);
		CameraPose aligned{ .origin = reflected.origin };
		XMStoreFloat3(&aligned.forward, forward);
		XMStoreFloat3(&aligned.up, up);
		XMStoreFloat3(&aligned.right, right);
		if (!OrthonormalBasis(aligned.forward, aligned.up, aligned.right)) return false;
		output = aligned;
		return true;
	}

	bool SelectPaneAlignedNearPlane(float sourceDistance, float normalHalfThickness,
		float nativeNear, float clipBias, float& output) noexcept
	{
		output = 0.0F;
		if (!std::isfinite(sourceDistance) || !std::isfinite(normalHalfThickness) ||
			!std::isfinite(nativeNear) || !std::isfinite(clipBias) ||
			normalHalfThickness < 0 || nativeNear <= 0 || clipBias < 0) return false;
		const float gap = sourceDistance - normalHalfThickness - clipBias;
		if (!std::isfinite(gap)) return false;
		output = (std::min)(nativeNear, (std::max)(1.0F, gap * 0.5F));
		// The caller still applies the strict slab + selected near + clip bias
		// check. A camera inside that band stays rejected, including behind-plane eyes.
		return true;
	}

	bool IsCameraBehindMirrorPlane(
		const PlanarMirrorMath::Plane& mirrorPlane,
		const DirectX::XMFLOAT3& cameraOrigin,
		float epsilon) noexcept
	{
		PlanarMirrorMath::Plane plane = mirrorPlane;
		if (!PlanarMirrorMath::NormalizePlane(plane) || !std::isfinite(cameraOrigin.x) ||
			!std::isfinite(cameraOrigin.y) || !std::isfinite(cameraOrigin.z) ||
			!std::isfinite(epsilon) || epsilon < 0.0f)
			return false;
		const float distance = PlanarMirrorMath::SignedDistance(plane, cameraOrigin);
		return std::isfinite(distance) && distance < -epsilon;
	}

	bool RebaseViewProjection(
		const DirectX::XMFLOAT4X4& viewProjection,
		const DirectX::XMFLOAT3& oldOrigin,
		const DirectX::XMFLOAT3& newOrigin,
		DirectX::XMFLOAT4X4& output) noexcept
	{
		if (!Finite(viewProjection) || !Finite(oldOrigin) || !Finite(newOrigin))
			return false;
		const DirectX::XMFLOAT3 delta{ newOrigin.x - oldOrigin.x,
			newOrigin.y - oldOrigin.y, newOrigin.z - oldOrigin.z };
		if (!Finite(delta))
			return false;
		DirectX::XMFLOAT4X4 rebased{};
		DirectX::XMStoreFloat4x4(&rebased, DirectX::XMMatrixTranslation(
			delta.x, delta.y, delta.z) * DirectX::XMLoadFloat4x4(&viewProjection));
		if (!Finite(rebased))
			return false;
		output = rebased;
		return true;
	}
}
