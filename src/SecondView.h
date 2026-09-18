#pragma once

#include "HandMirrorRuntimeBridgePolicy.h"

struct ID3D11DeviceContext;
struct ID3D11DepthStencilView;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace RE
{
	class BSRenderPass;
}

namespace SecondView
{
	/** Only filters casters in our owned private shadow targets. */
	[[nodiscard]] bool ShouldSkipMirrorDistanceDraw(const RE::BSRenderPass* pass) noexcept;
	/** An effect card cut by the placed mirror's own plane, right over the pane. */
	[[nodiscard]] bool ShouldSkipPaneCrossingEffectDraw(const RE::BSRenderPass* pass) noexcept;
	[[nodiscard]] std::uint64_t PaneCrossingEffectSkips() noexcept;
	[[nodiscard]] bool ShouldSkipSceneryShadowDraw(const RE::BSRenderPass* pass) noexcept;
	/** Replay-cycle draw of a reference already drawn by the supplemental cycle (MirrorReplayDuplicatePolicy). */
	[[nodiscard]] bool ShouldSkipDuplicateReplayDraw(const RE::BSRenderPass* pass,
		std::uint32_t technique, std::uint8_t geometryMode) noexcept;
	void NoteMirrorCoverageDrawReturned(const RE::BSRenderPass* pass,
		std::uint32_t technique, std::uint8_t geometryMode) noexcept;

	struct ExactHandSoftDepthBindings
	{
		ID3D11DeviceContext* context{ nullptr };
		ID3D11RenderTargetView* colorRTV{ nullptr };
		ID3D11DepthStencilView* writableDepthDSV{ nullptr };
		ID3D11DepthStencilView* readOnlyDepthDSV{ nullptr };
		ID3D11ShaderResourceView* depthSRV{ nullptr };
		std::uintptr_t targetIdentity{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};

	struct ExactHandEffectDraw
	{
		RE::BSRenderPass* pass{ nullptr };
		std::uint32_t technique{ 0 };
		std::uint32_t descriptor{ 0 };
		// Placed-mirror draw: a failed private-depth transaction falls back to
		// fallbackTechnique (no SoftEffect fade) instead of stopping captures.
		std::uint32_t fallbackTechnique{ 0 };
		bool privateDepthRequired{ false };
		bool placed{ false };
	};

	/** Install dormant M3 hooks after every peer's PostPostLoad callback has completed. */
	void OnInputLoaded();

	/** Arm an already-installed M3 path only after Skyrim reports DataLoaded. */
	void OnDataLoaded();

	/** Promote committed product features after ProductActivation::Commit succeeds. */
	void OnProductActivationCommitted() noexcept;

	/** Revoke any published reflected frame before a save/new-game transition is rescanned. */
	void OnGameLoaded() noexcept;

	/** Stop admitting second-view work and revoke publications before world teardown begins. */
	void OnPreLoadGame() noexcept;

	/** True after the dirty-state detour and RenderWorld caller driver are both installed. */
	[[nodiscard]] bool HooksReady() noexcept;

	/**
	 * Read-only exact recheck of the already-owned RenderWorld caller branch.
	 * This does not install or arm any SecondView/capture hook family.
	 */
	[[nodiscard]] bool RenderWorldDriverReadyForReadOnlyObserver() noexcept;

	/** True only after both M3 hooks remain valid at DataLoaded and the path was armed. */
	[[nodiscard]] bool IsEnabled() noexcept;

	/** True only when the scheduler may consider the mirror channel. */
	[[nodiscard]] bool MirrorCaptureEnabled() noexcept;
	/** Marker-only hand dependencies, excluding the runtime's final policy gate. */
	[[nodiscard]] bool HandCaptureDependenciesReady() noexcept;
	/** Pinned CS strict-light adapter, shared by hand and standing captures. */
	[[nodiscard]] bool CommunityShadersLightBridgeReady() noexcept;
	/** Value-identity check used before releasing an internal hand target role. */
	[[nodiscard]] bool IsHandPrivateTargetUnbound(
		const HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity& target) noexcept;

	/**
	 * Exact source epoch for the active main-world invocation or, after its
	 * normal return, the immediately completed invocation consumed by the later
	 * first-person draw.  Reset at the next scene-list fence and on load.
	 */
	[[nodiscard]] std::uint64_t CurrentMainWorldSourceSequence() noexcept;


	/**
	 * True while this thread is inside the private reflected capture, i.e.
	 * anywhere between target-begin and the final restore.
	 *
	 * Consumers that modify per-draw engine state need this because the engine
	 * re-renders the *whole scene* inside our capture - water included. A water
	 * consumer that fires there would be decorating our own reflection instead of
	 * the main view it is aimed at, and it would be doing D3D work while our pass
	 * holds renderer state, which is the documented way into the GPU livelock.
	 */
	[[nodiscard]] bool IsInsidePrivateCapture() noexcept;

	/** True only during the corrected-depth primary draw of the mirror channel. */
	[[nodiscard]] bool IsInsideMirrorPrimaryCapture() noexcept;
	// The standing/wall pane this thread is capturing (centre, plane normal);
	// false outside such a capture, and for hand and private-shadow passes.
	[[nodiscard]] bool GetActiveMirrorPane(std::array<float, 3>& a_center, std::array<float, 3>& a_normal) noexcept;
	// After BSLightingShader::SetupGeometry: swap the diffuse texture of the
	// player's hand pane inside a placed-mirror capture (reflection in a reflection).
	// True when the pass geometry hangs under the player's first-person skeleton.
	[[nodiscard]] bool IsFirstPersonPlayerGeometry(const RE::BSRenderPass* pass) noexcept;
	/** True only during the exterior auxiliary (LOD/sky) cycle of the mirror channel. */
	[[nodiscard]] bool IsInsideMirrorAuxiliaryCapture() noexcept;
	[[nodiscard]] bool MirrorPrimaryShadowsEnabled() noexcept;
	/** Borrow this thread's owned primary color target; no AddRef or device calls. */
	[[nodiscard]] ID3D11RenderTargetView* MirrorPrimaryColorTarget(std::uint32_t maximumExtent) noexcept;
    [[nodiscard]] ID3D11RenderTargetView* MirrorPrivateShadowColorTarget() noexcept;
	/** Default-off bounded CPU evidence; never mutates engine or graphics state. */
	void ObserveLightingEvidence(
		const RE::BSRenderPass* pass, std::uint32_t exactPlayerFlags = 0) noexcept;

	/**
	 * True only while the exact raised hand lane owns a valid physical-oblique
	 * projection.  Generic batch hooks use this to exempt retained-player draws
	 * from z clipping without changing the world/NPC half-space contract.
	 */
	[[nodiscard]] bool IsInsideExactHandPhysicalRasterClip() noexcept;

	/** Omit an unsafe private hand batch before native shader setup; no state is mutated. */
	[[nodiscard]] bool ShouldSkipExactHandRetiredLightDraw(
		RE::BSRenderPass* pass, std::uint32_t technique) noexcept;
	/** Default-off device-loss stop before private native batch dispatch. Does
	 * not reuse GPU query storage or unwind a native wait already in progress. */
	[[nodiscard]] bool ShouldSkipDeviceLostDraw() noexcept;

	/**
	 * Classify one exact hand BSEffect SoftEffect draw. The native technique is
	 * preserved; a true return requests the scoped private-depth transaction.
	 */
	[[nodiscard]] bool TrySelectExactHandEffectTechnique(
		RE::BSRenderPass* pass,
		std::uint32_t nativeTechnique,
		std::uint32_t& selectedTechnique) noexcept;

	/**
	 * Describe every exact-hand BSEffect draw. The descriptor and native
	 * technique are copied for the synchronous batch transaction; no shader
	 * permutation is changed.
	 */
	[[nodiscard]] bool TryDescribeExactHandEffectDraw(
		RE::BSRenderPass* pass,
		std::uint32_t nativeTechnique,
		ExactHandEffectDraw& output) noexcept;

	/** The device context of the private capture in progress, or null. */
	[[nodiscard]] ID3D11DeviceContext* ActiveCaptureContext() noexcept;

	/** Borrow the exact bound hand target views for one synchronous native draw. */
	[[nodiscard]] bool TryGetExactHandSoftDepthBindings(
		ExactHandSoftDepthBindings& output) noexcept;

	/** Make any private-depth binding failure terminal before publication. */
	void FailStopExactHandSoftDepth() noexcept;

	/**
	 * Monotonic count of begun mirror private passes, so per-pass consumers can
	 * attribute multiple draws of the same geometry within one pass (the
	 * watched plant draws twice: borrowed-replay then supplemental;
	 * 2026-08-19 RenderDoc private-pass capture).
	 */
	[[nodiscard]] std::uint64_t MirrorPrimaryPassSequence() noexcept;
	// Advances once per private pass of any kind (placed, hand, shadow).
	[[nodiscard]] std::uint64_t PrivatePassSequence() noexcept;
	// Width of the last placed-mirror capture target (on-screen sizing); 0 before any.
	[[nodiscard]] int LastPlacedCaptureResolution() noexcept;
	[[nodiscard]] int LastPlacedCaptureHeight() noexcept;

	/** Emit the M3 counters without changing renderer state. */
	void LogDiagnostics(const char* a_reason);
}
