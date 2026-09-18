#pragma once

#include <cstdint>

#include <DirectXMath.h>

#include "PlanarMirrorMath.h"

namespace MirrorCameraOverride
{
	/** Change the coordinate origin while preserving every world-to-clip result. */
	[[nodiscard]] bool RebaseViewProjection(
		const DirectX::XMFLOAT4X4& viewProjection,
		const DirectX::XMFLOAT3& oldOrigin,
		const DirectX::XMFLOAT3& newOrigin,
		DirectX::XMFLOAT4X4& output) noexcept;

	/** Perspective frustum reconstructed from the exact raster projection. */
	struct RasterPerspectiveFrustum
	{
		float left{ 0.0F };
		float right{ 0.0F };
		float top{ 0.0F };
		float bottom{ 0.0F };
		float nearPlane{ 0.0F };
		float farPlane{ 0.0F };
		bool valid{ false };
	};

	/** NiCamera world-rotation columns in Skyrim order: forward, up, right. */
	struct CameraPose
	{
		DirectX::XMFLOAT3 origin{};
		DirectX::XMFLOAT3 forward{};
		DirectX::XMFLOAT3 up{};
		DirectX::XMFLOAT3 right{};
	};

	/**
	 * Values copied from one BSGraphics::ViewData sample.  This deliberately
	 * carries the explicit raster axes as well as the matrices: validation can
	 * prove the engine's axis convention against the same view matrix instead of
	 * guessing from a later live NiCamera node.
	 */
	struct RasterCameraInput
	{
		DirectX::XMFLOAT3 origin{};
		DirectX::XMFLOAT3 viewForward{};
		DirectX::XMFLOAT3 viewUp{};
		DirectX::XMFLOAT3 viewRight{};
		DirectX::XMFLOAT4X4 view{};
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 viewProjection{};
	};

	/** Validated, value-only camera frozen at the exact raster sample. */
	struct RasterCameraSample
	{
		CameraPose pose{};
		DirectX::XMFLOAT4X4 view{};
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 viewProjection{};
		RasterPerspectiveFrustum frustum{};
		bool valid{ false };
	};

	/** Exact fail-closed stage for one raster ViewData validation attempt. */
	enum class RasterCameraValidationStatus : std::uint8_t
	{
		kReady,
		kInputNotFinite,
		kInputBasisInvalid,
		kViewBasisInvalid,
		kExplicitAxisMismatch,
		kViewProjectionProductMismatch,
		kViewProjectionMismatch = kViewProjectionProductMismatch,
		kProjectionShapeInvalid,
		kFrustumInvalid,
		kRightSignConventionInvalid
	};

	/**
	 * Validate one row-vector D3D raster sample, prove view * projection equals
	 * the retained VP, prove the explicit ViewData axes match the view matrix,
	 * and reconstruct its finite standard-Z off-center perspective frustum.
	 */
	[[nodiscard]] RasterCameraValidationStatus ValidateRasterCameraSample(
		const RasterCameraInput& input,
		RasterCameraSample& output) noexcept;
	/** Boolean compatibility wrapper; true only for kReady. */
	[[nodiscard]] bool BuildValidatedRasterCameraSample(
		const RasterCameraInput& input,
		RasterCameraSample& output) noexcept;

	/** Require a private upload view matrix to encode the exact expected pose. */
	[[nodiscard]] bool ViewMatrixMatchesCameraPose(
		const DirectX::XMFLOAT4X4& view,
		const CameraPose& pose) noexcept;

	/**
	 * Reflect a proper camera pose about a normalized plane. Forward and up are reflected, while right is
	 * reconstructed to preserve the source basis handedness; directly reflecting all three axes would create an
	 * improper (determinant-negative) camera transform.
	 */
	[[nodiscard]] bool BuildReflectedCameraPose(
		const CameraPose& source,
		const PlanarMirrorMath::Plane& mirrorPlane,
		CameraPose& output) noexcept;
	/** Keep the exact reflected eye, with all pane corners in one forward plane. */
	[[nodiscard]] bool BuildPaneAlignedReflectedCameraPose(
		const CameraPose& source, const PlanarMirrorMath::Plane& mirrorPlane,
		const PlanarMirrorMath::PaneFit& pane, CameraPose& output) noexcept;
	/** Adapt the private near distance while retaining the authored slab and clip-bias clearance. */
	[[nodiscard]] bool SelectPaneAlignedNearPlane(float sourceDistance, float normalHalfThickness,
		float nativeNear, float clipBias, float& output) noexcept;

	/** Verify the reflected eye is finite and strictly behind the viewer-oriented mirror plane. */
	[[nodiscard]] bool IsCameraBehindMirrorPlane(
		const PlanarMirrorMath::Plane& mirrorPlane,
		const DirectX::XMFLOAT3& cameraOrigin,
		float epsilon = 1.0e-4f) noexcept;
}
