#pragma once

#include "MirrorsOfSkyrimHandMirrorCaptureReservationPolicy.h"
#include "HandMirrorInternalPublicationStore.h"
#include "HandMirrorRuntimeBridgePolicy.h"
#include "MirrorPaneRenderer.h"

#include <cstdint>

namespace RE
{
	class BSRenderPass;
}

namespace HandMirrorReflectionRuntime
{
	/**
	 * Hand private target edge for the current settings: the larger of the raised
	 * and lowered resolutions (512-4096) with the lowered-resolution controls, the
	 * fixed 2048 base without them. The other pose renders into a reduced mip view.
	 */
	[[nodiscard]] std::uint32_t PrivateTargetDimension() noexcept;
	[[nodiscard]] std::uint32_t PrivateTargetHeight() noexcept;
	[[nodiscard]] std::uint32_t PrivateCaptureMip(bool raised) noexcept;
	[[nodiscard]] std::uint32_t PrivateTargetMipLevels() noexcept;
	[[nodiscard]] std::uint32_t PrivateTargetReducedViews() noexcept;

	/** Request dormant shared owners for supported exact hand-mirror content. */
	void PrepareAtInputLoaded() noexcept;

	/** Hook-owner request only; never implies that content or runtime gates passed. */
	[[nodiscard]] bool Requested() noexcept;

	/** Promote only after exact content and every shared owner are proven. */
	void OnDataLoaded() noexcept;
	/** Final promotion after SecondView and the shared main-draw seam validate. */
	void CompleteDataLoadedActivation() noexcept;
	/** Revoke hand capture authorization before save-game world teardown begins. */
	void OnPreLoadGame() noexcept;
	void OnGameLoaded() noexcept;
	/** Suspend cleanly across InventoryMenu and resume after its first clean fence. */
	void OnInventoryMenuOpenChanged(bool open) noexcept;
	/** Revalidate exact mirror ownership after any player equipment change. */
	void OnPlayerEquipWake() noexcept;
	/** Read-only lifecycle gate for cleanup-only aborts of an in-flight capture. */
	[[nodiscard]] bool LifecycleTransitionSuspended() noexcept;
	void FailStopCallbackFault() noexcept;

	enum class MirrorSelection : std::uint8_t
	{
		kDark,
		kWall,
		kHand
	};

	struct FenceSelection
	{
		MirrorSelection selection{ MirrorSelection::kDark };
		HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity owner{};
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t ownerLeaseSequence{ 0 };
		bool exactPriorFrameHandVisible{ false };
		bool legacyWallPublicationRetirementRequired{ false };
		bool valid{ false };
		bool handSlotReserved{ false };
		/** Pose the selection was evaluated for (selects the raised/lowered refresh cap). */
		bool raisedPresentation{ false };
	};

	/**
	 * Evaluate hand-versus-wall ownership before the existing scheduler spends its
	 * one kMirror opportunity.  Hand admission uses only the previous frame's
	 * exact, normally returned first-person pane observation.
	 */
	[[nodiscard]] FenceSelection EvaluateFenceOwner(
		std::uint64_t sourceSequence,
		std::uint32_t wallFormID,
		std::uint64_t wallGeneration,
		bool exactWallEligible,
		bool legacyWallPublicationExists,
		const HandMirrorRuntimeBridgePolicy::PriorPublicationIdentity&
			legacyWallPublication, bool independentSlot = false) noexcept;

	/** Finalize a staged hand/dark grant only after wall API publication is gone. */
	[[nodiscard]] FenceSelection CompleteFenceOwnerAfterWallRetirement(
		const FenceSelection& staged,
		const HandMirrorInternalPublicationStore::Store::PhysicalTargets&
			handTargets) noexcept;
	/**
	 * Abandon an unselected fence proposal. Staged-prior transitions preserve the
	 * exact wall; every fresh issued owner lease is burned exactly once.
	 */
	void AbandonFenceSelection(const FenceSelection& selection) noexcept;

	/** Commit the policy result only after the shared scheduler selected kMirror. */
	void CommitFenceSelection(
		const FenceSelection& selection,
		bool mirrorSchedulerOpportunitySelected) noexcept;

	/** Initialize/advance the two-role internal store and reserve this kMirror win. */
	[[nodiscard]] bool ReserveHandAtFence(
		const FenceSelection& selection,
		std::uint64_t mainViewFrame,
		const HandMirrorInternalPublicationStore::Store::PhysicalTargets& targets)
		noexcept;

	[[nodiscard]] bool HasPendingHandReservation(
		std::uint64_t sourceSequence,
		std::uint64_t mainViewFrame = 0) noexcept;
	/** Dark-cancel an exact fence ticket which cannot reach the post-world seam. */
	[[nodiscard]] bool CancelPendingHandReservation(
		std::uint64_t sourceSequence) noexcept;

	struct CaptureLease
	{
		HandMirrorCaptureReservationPolicy::CaptureTicket ticket{};
		bool valid{ false };
	};

	[[nodiscard]] bool BeginPostWorldCapture(
		const HandMirrorRuntimeBridgePolicy::MovingSurfaceSample& surface,
		const HandMirrorFrameReceiptPolicy::FrameReceipt& receipt,
		const HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity& mainViewTarget,
		CaptureLease& output) noexcept;
	void AbortPostWorldCapture(const CaptureLease& lease) noexcept;

	struct CompletedNativeFrame
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV{};
		HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity targetIdentity{};
		HandMirrorRuntimeBridgePolicy::MovingSurfaceSample capturedSurface{};
		DirectX::XMFLOAT4X4 reflectedViewProjection{};
		// The camera-relative oblique view-projection that rasterized the target
		// (reflectedViewProjection is the portrait/selfie sampling projection).
		DirectX::XMFLOAT4X4 rasterViewProjection{};
		DirectX::XMFLOAT3 reflectedOrigin{};
		DirectX::XMFLOAT3 frozenSourceOrigin{};
		PlanarMirrorMath::Plane capturePlane{};
		std::uintptr_t deviceIdentity{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		DXGI_FORMAT colorFormat{ DXGI_FORMAT_UNKNOWN };
		bool raisedPresentation{ false };
		bool valid{ false };
	};

	/**
	 * Detach and logically zero a never-published native frame before individually
	 * guarded COM releases.  Lifecycle owners must use this instead of allowing a
	 * CompletedNativeFrame destructor/reset to invoke native Release directly.
	 */
	[[nodiscard]] bool DiscardCompletedNativeFrame(
		CompletedNativeFrame& nativeFrame) noexcept;

	[[nodiscard]] bool CompletePostWorldCapture(
		const CaptureLease& lease,
		const HandMirrorFrameReceiptPolicy::FrameReceipt& currentReceipt,
		const HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity& mainViewTarget,
		const HandMirrorInternalPublicationStore::PublicationCleanupProof& cleanup,
		CompletedNativeFrame&& nativeFrame) noexcept;

	/** Stack-only value receipt owned by the one existing generic-draw fanout. */
	struct GenericDrawToken
	{
		HandMirrorRuntimeBridgePolicy::MovingSurfaceSample entrySurface{};
		DirectX::XMFLOAT4X4 entryView{};
		DirectX::XMFLOAT4X4 entryProjection{};
		DirectX::XMFLOAT4X4 entryViewProjection{};
		DirectX::XMFLOAT3 entryOrigin{};
		D3D11_VIEWPORT entryViewport{};
		HandMirrorFrameReceiptPolicy::FrameReceipt receipt{};
		// Frozen M5 capture target which binds the token to its frame receipt.
		// The live first-person delivery target is owned separately by the TLS
		// RetainedMainTarget handle and the D3D identities below.
		HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity captureReceiptTarget{};
		std::uintptr_t passIdentity{ 0 };
		std::uintptr_t deviceIdentity{ 0 };
		std::uintptr_t contextIdentity{ 0 };
		std::uintptr_t colorResourceIdentity{ 0 };
		std::uintptr_t depthResourceIdentity{ 0 };
		bool active{ false };
		bool framePresentationActive{ false };
		bool framePresentationRaised{ false };
	};

	void OnFirstPersonEnter() noexcept;
	void OnFirstPersonReturnedNormally() noexcept;
	void OnFirstPersonFinally() noexcept;
	[[nodiscard]] GenericDrawToken OnGenericDrawEntry(
		RE::BSRenderPass* pass) noexcept;
	void OnGenericDrawReturnedNormally(
		GenericDrawToken& token,
		RE::BSRenderPass* pass) noexcept;
	void OnGenericDrawFinally(GenericDrawToken& token) noexcept;

	[[nodiscard]] bool IsEnabled() noexcept;
	/** Third-person pane of the equipped hand mirror as a private capture sees it. */
	struct NestedHandPaneGeometry
	{
		DirectX::XMFLOAT3 center{};
		DirectX::XMFLOAT3 tangentExtent{};
		DirectX::XMFLOAT3 bitangentExtent{};
	};
	[[nodiscard]] MirrorPaneRenderer::DrawStatus DrawHandPaneBacking(
		ID3D11DeviceContext* context, const NestedHandPaneGeometry& pane,
		const MirrorPaneRenderer::MainView& view,
		const MirrorPaneRenderer::DrawTargets& targets) noexcept;
	/**
	 * Draw the hand mirror's last-good reflection (one frame old) onto a pane
	 * inside a private capture: the captured first-person pane basis is
	 * retargeted onto this pane and the frame is drawn by the hand renderer
	 * viewed from the capture's reflected camera. Render thread only; the
	 * caller owns SEH.
	 */
	[[nodiscard]] MirrorPaneRenderer::DrawStatus DrawLastGoodHandFrameOnPane(
		ID3D11DeviceContext* context,
		const NestedHandPaneGeometry& pane,
		const MirrorPaneRenderer::MainView& view,
		const MirrorPaneRenderer::DrawTargets& targets) noexcept;
	[[nodiscard]] bool IsFaultStopped() noexcept;
	void LogDiagnostics(const char* reason);
}
