#pragma once

#include <cstdint>

#include <DirectXMath.h>

#include "MirrorCameraHandRuntimeSeam.h"
#include "MirrorCameraMath.h"
#include "PlanarMirrorMath.h"

namespace RE
{
	class NiCamera;
}

namespace MirrorCameraOverride
{
	inline constexpr float kDefaultClipBias = 1.5f;

	/**
	 * Snapshot of the mirror projection hook. Counters are process-wide; active/patched describe the calling
	 * thread because each override lease is deliberately render-thread-local.
	 */
	struct Diagnostics
	{
		std::uint64_t arms{ 0 };
		std::uint64_t hookCalls{ 0 };
		std::uint64_t patchedUploads{ 0 };
		std::uint64_t failedUploads{ 0 };
		std::uint64_t paneFitApplied{ 0 };
		std::uint64_t paneFitFallbacks{ 0 };
		std::uint64_t paneFitInvalidInputs{ 0 };
		std::uint64_t paneFitInvalidReferenceProjections{ 0 };
		std::uint64_t paneFitNotFullyInFront{ 0 };
		std::uint64_t paneFitInvalidSlopes{ 0 };
		std::uint64_t paneFitInvalidOutputs{ 0 };
		std::uint64_t paneFitCornerClamps{ 0 };
		std::uint64_t variantMismatchedUploads{ 0 };
		std::uint64_t handPoseMismatchSkips{ 0 };
		std::uint64_t handStandardProjectionApplied{ 0 };
		std::uint64_t handPhysicalObliqueProjectionApplied{ 0 };
		std::uint64_t repeatedUploadAttempts{ 0 };
		std::uint64_t repeatedPatchedUploads{ 0 };
		std::uint64_t unownedUploadSkips{ 0 };
		std::uint64_t repeatedIdentityRejects{ 0 };
		std::uint64_t reentrantUploadRejects{ 0 };
		float variantUploadDeltaMax{ 0.0f };
		float lastFitHalfSlopeX{ 0.0f };
		float lastFitHalfSlopeY{ 0.0f };
		float phaseFitHalfSlopeX[2]{};
		float phaseOriginX[2]{};
		float phaseOriginY[2]{};
		float phaseOriginZ[2]{};
		std::uint64_t unexpectedFlags{ 0 };
		std::uint64_t nestedBeginRejects{ 0 };
		std::uint64_t cameraSideRejects{ 0 };
		std::uint64_t exceptions{ 0 };
		std::uint64_t skyRecenters{ 0 };
		std::uint64_t skyRecenterRejects{ 0 };
		/** Whether the recenter also had the reflected origin to correct toward. */
		std::uint64_t skyCameraTranslationLeases{ 0 };
		std::uint64_t skyCameraTranslationRefusals{ 0 };
		bool skyRecenterInstalled{ false };
		std::uint32_t lastException{ 0 };
		bool hookInstalled{ false };
		float paneFitMarginScale{ 1.15f };
		bool active{ false };
		bool projectionPatched{ false };
	};

	/** Paired raster/SoftEffect projections for one exact physical hand capture. */
	struct HandPhysicalDepthProjection
	{
		DirectX::XMFLOAT4X4 conventional{};
		DirectX::XMFLOAT4X4 oblique{};
		std::uint64_t generation{ 0 };
		bool valid{ false };
	};

	/** Return true only for the two exact flat runtimes whose layouts and hook ABI were verified offline. */
	[[nodiscard]] bool IsSupportedFlatRuntime() noexcept;

	/** True only after wall-mirror activation commits against a prepared upload hook. */
	[[nodiscard]] bool IsEnabled() noexcept;

	/** True when either mirror role explicitly requested the shared upload hook. */
	[[nodiscard]] bool Requested() noexcept;
	/** Default-off, exact-AE sky shader camera-origin correction. Set before InputLoaded. */
	void SetSkyRecenterRequested(bool requested) noexcept;

	/** Promote only the equipped-mirror adapter; the placed-mirror path remains unchanged. */
	[[nodiscard]] bool BeginHandMirrorCaptureScope(
		const DirectX::XMFLOAT4X4& frozenRasterProjection) noexcept;
	/** Require a later hand camera restore to reproduce the scope's exact frozen projection. */
	[[nodiscard]] bool HandMirrorCaptureProjectionMatches(
		const DirectX::XMFLOAT4X4& candidate) noexcept;
	void EndHandMirrorCaptureScope() noexcept;
	/** Sticky while the hand adapter remains enabled; reserved for native/invariant faults. */
	[[nodiscard]] bool HandMirrorCaptureNativeFaulted() noexcept;

	/** Fixed runtime pane-fit/cull margin shared by placed and equipped mirrors. */
	[[nodiscard]] float PaneFitMarginScale() noexcept;

	/** Read the verified flat NiCamera world transform (+0x7C) into Skyrim's forward/up/right column order. */
	[[nodiscard]] bool ReadFlatCameraPose(const RE::NiCamera* camera, CameraPose& output) noexcept;

	/**
	 * Prepare the UpdateCameraData detour when at least one caller-requested mirror role is present and the
	 * second-view hooks are ready. This does not activate the placed-mirror path before commit.
	 */
	void OnInputLoaded(
		bool secondViewHooksReady,
		bool wallMirrorRequested,
		bool handMirrorRequested);

	/** Promote the placed-mirror path after the caller's clean activation transaction commits. */
	void OnActivationCommitted(bool wallMirrorEnabled) noexcept;

	/**
	 * Arm the calling render thread's private mirror-camera upload lease. The validated projection is reapplied
	 * to every owned flags-8/12 upload until End.
	 * The camera argument is an ownership token for matched End/Get calls; UpdateCameraData itself has no camera
	 * parameter. Call this immediately before native private-camera dispatch and always call End afterward.
	 */
	[[nodiscard]] bool Begin(
		const RE::NiCamera* reflectedCamera,
		const PlanarMirrorMath::Plane& mirrorPlane,
		float clipBias = kDefaultClipBias,
		const PlanarMirrorMath::PaneFit* paneFit = nullptr,
		bool handPhysicalRasterClip = false) noexcept;

	/** Disarm the calling thread. A non-null camera prevents an unrelated nested caller from clearing the state. */
	void End(const RE::NiCamera* reflectedCamera = nullptr) noexcept;

	[[nodiscard]] bool Active(const RE::NiCamera* reflectedCamera = nullptr) noexcept;

	/** Copy the oblique reflected view-projection produced at the upload seam. */
	[[nodiscard]] bool GetPatchedViewProjection(
		const RE::NiCamera* reflectedCamera,
		DirectX::XMFLOAT4X4& output) noexcept;

	/** Copy the paired camera-relative oblique VP and renderer posAdjust origin. */
	[[nodiscard]] bool GetPatchedFrameData(
		const RE::NiCamera* reflectedCamera,
		DirectX::XMFLOAT4X4& viewProjection,
		DirectX::XMFLOAT3& origin) noexcept;

	/** Copy the oblique raster/conventional SoftEffect projection pair while armed. */
	[[nodiscard]] bool GetHandPhysicalDepthProjection(
		const RE::NiCamera* reflectedCamera,
		HandPhysicalDepthProjection& output) noexcept;

	/**
	 * The same pair for an ordinary placed-mirror lease: the oblique raster
	 * projection and the conventional one SoftEffect reconstructs depth with.
	 * One generation per Begin, so each capture cycle resolves its own depth.
	 */
	[[nodiscard]] bool GetPlacedDepthProjection(
		const RE::NiCamera* reflectedCamera,
		HandPhysicalDepthProjection& output) noexcept;

	[[nodiscard]] Diagnostics GetDiagnostics() noexcept;
}
