#pragma once

#include "HandMirrorRuntimeBridgePolicy.h"
#include "HandMirrorStereoPresentation.h"
#include "MirrorCameraMath.h"
#include "MirrorPaneRenderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <DirectXMath.h>
#include <d3d11.h>

namespace RE { class BSRenderPass; }

namespace MirrorPaneDelivery
{
	/**
	 * AddRef-retained identity for the exact immediate main-view target set.
	 *
	 * The values are intentionally D3D-only: no Skyrim scene pointer crosses the
	 * synchronous render callback.  Copies share one guarded-release owner, so a
	 * raw COM-address ABA cannot occur between the pane raster observation,
	 * post-world capture, and the delayed first-person composition.
	 */
	struct RetainedMainTarget
	{
		ID3D11Device* device{ nullptr };
		ID3D11DeviceContext* context{ nullptr };
		ID3D11RenderTargetView* colorRTV{ nullptr };
		ID3D11RenderTargetView* motionRTV{ nullptr };
		ID3D11DepthStencilView* depthDSV{ nullptr };
		ID3D11Resource* colorResource{ nullptr };
		ID3D11Resource* motionResource{ nullptr };
		ID3D11Resource* depthResource{ nullptr };
	};

	/**
	 * Value-only identity used to keep mandatory presentation ownership separate
	 * from the optional motion-vector attachment.  Slot 7 may legitimately be
	 * absent or rotate while the color/depth presentation allocation is stable.
	 */
	struct RetainedMainTargetCoreIdentity
	{
		std::uintptr_t device{ 0 };
		std::uintptr_t context{ 0 };
		std::uintptr_t colorView{ 0 };
		std::uintptr_t depthView{ 0 };
		std::uintptr_t colorResource{ 0 };
		std::uintptr_t depthResource{ 0 };

		[[nodiscard]] friend constexpr bool operator==(
			const RetainedMainTargetCoreIdentity&,
			const RetainedMainTargetCoreIdentity&) noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidRetainedMainTargetCoreIdentity(
		const RetainedMainTargetCoreIdentity& identity) noexcept
	{
		return identity.device != 0 && identity.context != 0 &&
			identity.colorView != 0 && identity.depthView != 0 &&
			identity.colorResource != 0 && identity.depthResource != 0 &&
			identity.colorResource != identity.depthResource;
	}

	[[nodiscard]] constexpr bool SameRetainedMainTargetCoreIdentity(
		const RetainedMainTargetCoreIdentity& left,
		const RetainedMainTargetCoreIdentity& right) noexcept
	{
		return IsValidRetainedMainTargetCoreIdentity(left) &&
			IsValidRetainedMainTargetCoreIdentity(right) && left == right;
	}

	struct OptionalMotionTargetIdentity
	{
		std::uintptr_t view{ 0 };
		std::uintptr_t resource{ 0 };
		std::uintptr_t viewDevice{ 0 };
		std::uintptr_t resourceDevice{ 0 };
	};

	enum class OptionalMotionRetentionDisposition : std::uint8_t
	{
		kAbsent,
		kRetain,
		kDiscardInvalid
	};

	/**
	 * Classify only ordinary, successfully queried COM identity shapes.  A native
	 * exception is handled separately and remains process-terminal.
	 */
	[[nodiscard]] constexpr OptionalMotionRetentionDisposition
	ClassifyOptionalMotionRetention(
		const RetainedMainTargetCoreIdentity& core,
		const OptionalMotionTargetIdentity& motion) noexcept
	{
		if (motion.view == 0) {
			return motion.resource == 0 ?
				OptionalMotionRetentionDisposition::kAbsent :
				OptionalMotionRetentionDisposition::kDiscardInvalid;
		}
		return IsValidRetainedMainTargetCoreIdentity(core) &&
			motion.resource != 0 && motion.view != core.colorView &&
			motion.resource != core.colorResource &&
			motion.resource != core.depthResource &&
			motion.viewDevice == core.device &&
			motion.resourceDevice == core.device ?
			OptionalMotionRetentionDisposition::kRetain :
			OptionalMotionRetentionDisposition::kDiscardInvalid;
	}

	using RetainedMainTargetHandle =
		std::shared_ptr<const RetainedMainTarget>;

	/** Value-only source camera paired with the unique pane raster observation. */
	struct FrozenSourceCamera
	{
		std::uintptr_t cameraIdentity{ 0 };
		DirectX::XMFLOAT3 origin{};
		DirectX::XMFLOAT3 forward{};
		DirectX::XMFLOAT3 up{};
		DirectX::XMFLOAT3 right{};
		DirectX::XMFLOAT4X4 rasterView{};
		DirectX::XMFLOAT4X4 rasterProjection{};
		MirrorCameraOverride::RasterPerspectiveFrustum rasterFrustum{};
		DirectX::XMFLOAT4X4 rasterViewProjectionUnjittered{};
		DirectX::XMFLOAT4X4 rasterProjectionUnjittered{};
		MirrorCameraOverride::RasterPerspectiveFrustum rasterFrustumUnjittered{};
		// Flat NiCamera::RUNTIME_DATA2 is verified 0x38 bytes on SE/AE.  Keeping
		// exact non-frustum metadata avoids reading a later live camera during
		// post-world replay; the frustum is overwritten from the caller's validated
		// frozen raster projection.
		std::array<std::byte, 0x38> runtimeData2{};
		bool rasterAuthoritative{ false };
		bool unjitteredRasterAuthoritative{ false };
		bool valid{ false };
	};

	struct DeferredCaptureMainView
	{
		DirectX::XMFLOAT4X4 viewProjection{};
		DirectX::XMFLOAT3 origin{};
		HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity mainTarget{};
		std::uintptr_t deviceIdentity{ 0 };
		std::uintptr_t contextIdentity{ 0 };
		D3D11_VIEWPORT viewport{};
		std::uint64_t mainWorldFrame{ 0 };
		std::uint32_t graphicsFrame{ 0 };
		std::uint32_t observationCount{ 0 };
		FrozenSourceCamera sourceCamera{};
		RetainedMainTargetHandle retainedTarget{};
		bool handPaneView{ false };
		bool valid{ false };
	};

	/** Proves the current equipped VR pane before observing its main-view sample. */
	[[nodiscard]] bool ObserveCurrentVRHandMainView(RE::BSRenderPass* pass) noexcept;

	struct CurrentMainDrawState
	{
		DirectX::XMFLOAT4X4 view{};
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 viewProjection{};
		DirectX::XMFLOAT3 origin{};
		ID3D11Device* device{ nullptr };
		ID3D11DeviceContext* context{ nullptr };
		ID3D11RenderTargetView* colorRTV{ nullptr };
		ID3D11RenderTargetView* motionRTV{ nullptr };
		ID3D11DepthStencilView* depthDSV{ nullptr };
		// Frozen pane world-target identity which binds this synchronous first-person
		// state to the exact capture receipt.  This is deliberately not the live
		// first-person delivery target: Skyrim may bind a distinct depth target for
		// the native first-person pass.  Live delivery identity is proven only by
		// RetainedMainTarget.
		HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity captureReceiptTarget{};
		D3D11_VIEWPORT viewport{};
		std::uint64_t mainWorldFrame{ 0 };
		std::uint32_t graphicsFrame{ 0 };
		bool valid{ false };
		std::array<HandMirrorStereoPresentation::Eye, 2> eyes{};
		std::uint32_t eyeCount{ 0 };
	};

	/**
	 * Exact reason a synchronous first-person draw could not bind the retained
	 * pane main-view sample. This is diagnostic value data only: every non-ready
	 * result remains the same coherent-dark/fail-closed outcome as the legacy
	 * boolean query.
	 */
	enum class CurrentMainDrawStateStatus : std::uint8_t
	{
		kReady,
		kDeliveryFaulted,
		kPrivatePassActive,
		kMainWorldActive,
		kMainWorldFrameMissing,
		kDeferredSnapshotInvalid,
		kDeferredMainWorldFrameMismatch,
		kDeferredGraphicsFrameMissing,
		kEngineStateUnavailable,
		kGraphicsFrameMismatch,
		/**
		 * Skyrim is rendering the refraction-normals layer (first-person geometry
		 * re-drawn while a refractive object is on screen).  The layer is
		 * distortion data for the refraction composite, so the caller must skip
		 * delivery; the frame's deferred main view is deliberately kept.
		 */
		kRefractionPassActive
	};

	/** Only the outermost private-pass close may release transient pane culling. */
	[[nodiscard]] constexpr bool ShouldRestorePaneAfterPrivatePassClose(
		std::uint32_t a_remainingDepth) noexcept
	{
		return a_remainingDepth == 0;
	}

	class MainWorldScope
	{
	public:
		MainWorldScope() noexcept;
		~MainWorldScope();
		MainWorldScope(const MainWorldScope&) = delete;
		MainWorldScope& operator=(const MainWorldScope&) = delete;

	private:
		bool active{ false };
	};

	class PrivatePassScope
	{
	public:
		PrivatePassScope() noexcept;
		~PrivatePassScope();
		PrivatePassScope(const PrivatePassScope&) = delete;
		PrivatePassScope& operator=(const PrivatePassScope&) = delete;

		/** Idempotent TLS restoration for explicit SEH-finally cleanup. */
		[[nodiscard]] bool Close() noexcept;

	private:
		bool active{ false };
		bool closed{ false };
		bool closeSucceeded{ false };
	};

	/** Install the dormant pane seam only when the shared scene hooks are ready. */
	void OnInputLoaded(bool secondViewHooksReady);

	/** Activate requested diagnostics/draw stages only after Skyrim reports DataLoaded. */
	void OnDataLoaded();

	/** Promote the committed mirror features after their readiness gate passes. */
	void OnMirrorActivationCommitted() noexcept;

	/** Restore any changed pane bit before candidate discovery is rebuilt. */
	void OnGameLoaded() noexcept;

	/** Immediately restore an owned pane when M2 invalidates its candidate lifetime. */
	void OnCandidateInvalidated() noexcept;
	void OnCandidateInvalidated(std::uint32_t formID) noexcept;

	/** Restore the fallback pane when a reflected attempt cannot republish a frame. */
	void OnFrameInvalidated() noexcept;

	/**
	 * Idempotently restore any prior-frame suppression before the next chained
	 * main-world render, including private-pass early-return paths.
	 */
	void RestoreFallbackBeforeMainWorld() noexcept;

	[[nodiscard]] bool IsEnabled() noexcept;
	/** Hook ownership validated at DataLoaded; does not imply mirror drawing. */
	[[nodiscard]] bool SharedSeamReady() noexcept;
	/** Sticky guarded/native delivery fault, distinct from coherent target drift. */
	[[nodiscard]] bool Faulted() noexcept;

	/** Outcome of drawing the published resident panes into a private capture. */
	struct PrivateViewPaneDrawResult
	{
		std::uint32_t attempted{ 0 };
		std::uint32_t drawn{ 0 };
		MirrorPaneRenderer::DrawStatus lastStatus{
			MirrorPaneRenderer::DrawStatus::kNoPublishedFrame };
	};
	/**
	 * Nested pane content: draw every published resident standing pane except
	 * excludeFormID into a private capture target with the pane renderer,
	 * viewed from that capture's reflected camera (view = camera-relative
	 * oblique view-projection and its posAdjust origin). Render thread only,
	 * SEH inside; false means the draw pass itself faulted or was unavailable.
	 */
	[[nodiscard]] bool DrawResidentPanesIntoPrivateView(
		ID3D11DeviceContext* context,
		const MirrorPaneRenderer::MainView& view,
		const MirrorPaneRenderer::DrawTargets& targets,
		std::uint32_t excludeFormID,
		PrivateViewPaneDrawResult& result) noexcept;
	/**
	 * True when this standing mirror was drawn nested into another capture within
	 * the last quarter second (run 23: a standing mirror seen only through the
	 * hand mirror is not main-view visible, so the fleet never refreshed it and
	 * the hand mirror showed a frozen frame). Render thread only.
	 */
	[[nodiscard]] bool NestedDrawDemand(std::uint32_t formID, std::uint64_t nowMicroseconds) noexcept;
	/** SEH code of the most recent delivery/nested-draw exception (0 = none). */
	[[nodiscard]] unsigned long LastDeliveryExceptionCode() noexcept;
	/** True only while the mirror pane renderer itself is armed. */
	[[nodiscard]] bool MirrorDrawReady() noexcept;
	/** Exact thread-local scope discriminator for the owned main-world presentation. */
	[[nodiscard]] bool IsInsideMainWorld() noexcept;
	/** Exact frame serial the next outer MainWorldScope will assign at this fence. */
	[[nodiscard]] std::uint64_t UpcomingMainWorldFrame() noexcept;
	/**
	 * Copy the raster main view observed during the just-completed outer
	 * main-world scope.  Exact-main capture calls this only after that scope has
	 * returned; the frame serial is later enforced by pane delivery.
	 */
	[[nodiscard]] bool TryGetDeferredCaptureMainView(
		DeferredCaptureMainView& output, bool handPane = false) noexcept;
	/** Re-read the post-world D3D state and require the frozen resource pair. */
	[[nodiscard]] bool RevalidateDeferredCaptureMainTarget(
		const DeferredCaptureMainView& frozen) noexcept;
	/** Borrowed D3D pointers are valid only for the caller's synchronous draw. */
	[[nodiscard]] CurrentMainDrawStateStatus QueryCurrentMainDrawState(
		CurrentMainDrawState& output, bool allowVRWorld = false) noexcept;
	/** Boolean compatibility wrapper; true only for kReady. */
	[[nodiscard]] bool TryGetCurrentMainDrawState(
		CurrentMainDrawState& output, bool allowVRWorld = false) noexcept;
	/** Guardedly retain the exact synchronous main target and its resources. */
	[[nodiscard]] bool TryRetainCurrentMainDrawTarget(
		const CurrentMainDrawState& current,
		RetainedMainTargetHandle& output) noexcept;
	/** Retain RTV0 and viewport actually bound at this synchronous draw seam. */
	[[nodiscard]] bool TryRetainCurrentOutputMergerTarget(
		const CurrentMainDrawState& current,
		RetainedMainTargetHandle& output,
		D3D11_VIEWPORT& viewport) noexcept;
	/**
	 * Retain RTV0 and every viewport bound at this seam, one per eye: a single
	 * rectangle on flat runtimes, both eye rectangles on Skyrim VR.  Entry 0 is
	 * always eye 0 and equals the single-viewport form above.
	 */
	[[nodiscard]] bool TryRetainCurrentOutputMergerTargets(
		const CurrentMainDrawState& current,
		RetainedMainTargetHandle& output,
		std::array<D3D11_VIEWPORT, 2>& viewports,
		std::uint32_t& viewportCount) noexcept;
	/** Guardedly re-read and compare a current draw target to a retained owner. */
	[[nodiscard]] bool RevalidateCurrentMainDrawTarget(
		const CurrentMainDrawState& current,
		const RetainedMainTargetHandle& retained) noexcept;
	/**
	 * Revalidate mandatory color/depth ownership and return a newly retained
	 * handle carrying the current optional motion target (or no motion target).
	 */
	[[nodiscard]] bool TryRefreshCurrentMainDrawTarget(
		const CurrentMainDrawState& current,
		const RetainedMainTargetHandle& retained,
		RetainedMainTargetHandle& refreshed) noexcept;
	/**
	 * Keep an authoritative pass-local color/depth owner while attaching only the
	 * current optional motion target.  This is for the outer-final hand draw,
	 * where Skyrim may already have restored another live OM color/depth pair.
	 */
	[[nodiscard]] bool TryAttachCurrentOptionalMotionTarget(
		const CurrentMainDrawState& current,
		const RetainedMainTargetHandle& retainedCore,
		RetainedMainTargetHandle& attached) noexcept;
	/** Capture the current WorldRootCamera as value-only pose/runtime evidence. */
	[[nodiscard]] bool TryGetCurrentSourceCamera(
		FrozenSourceCamera& output) noexcept;
	[[nodiscard]] bool SameFrozenSourceCamera(
		const FrozenSourceCamera& left,
		const FrozenSourceCamera& right) noexcept;
	/** Match the stable fence camera identity/origin to authoritative raster data. */
	[[nodiscard]] bool FenceCameraMatchesFrozenRaster(
		const FrozenSourceCamera& fence,
		const FrozenSourceCamera& raster) noexcept;
	void LogDiagnostics(const char* reason);
}
