#include "PCH.h"
#include "MirrorPerformance.h"
#include "PeerDetection.h"

#include "HandMirrorReflectionRuntime.h"
#include "HandMirrorSafetySettings.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorRuntimeBridgePolicy.h"
#include "HandMirrorVRRuntimePolicy.h"
#include "EngineDeviceIdentity.h"
#include "GraphicsFrameCounterPolicy.h"
#include "MainViewVariantGuardPolicy.h"
#include "MirrorsOfSkyrimCameraOverride.h"
#include "MirrorFramePublication.h"
#include "MirrorFleetRuntime.h"
#include "MirrorInteriorEligibility.h"
#include "MirrorsOfSkyrimPaneDelivery.h"
#if defined(MOS_VERIFY_HARNESS)
#include "EvidenceFrameCapture.h"
#endif
#include "MirrorPaneDeliverySignature.h"
#include "MirrorPaneRenderer.h"
#include "MirrorOcclusionRuntime.h"
#include "MirrorPaneSurface.h"
#include "MirrorsOfSkyrimRecognition.h"
#include "MirrorSelectionPolicy.h"
#include "MirrorActivation.h"
#include "SecondView.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>

namespace MirrorPaneRenderer
{
	// Defined in MirrorPaneRenderer.cpp; the renderer owns no log sink, so the
	// delivery layer reports the first multi-slice colour-view refusal.
	[[nodiscard]] bool TakeArrayColorTargetRejectionNotice() noexcept;
}

namespace MirrorPaneDelivery
{
	namespace
	{
		// AUTHORED PANE RECTANGLE — must stay in lockstep with
		// the owned `TrueMirror:0` quad and its OBND. The source-boundary test
		// asserts all three agree, because a stale value here silently draws the
		// reflection at the wrong size: it stayed at the original square 128 across
		// two mesh resizes and produced the 2026-08-07 "reflection sticks out past
		// the frame" report.
		// Local axes: Y = width (tangent), Z = height (bitangent).
		constexpr float kAuthoredPaneHalfTangent = 52.0f;
		constexpr float kAuthoredPaneHalfBitangent = 87.0f;
		constexpr float kPaneCenterTolerance = 0.5f;
		constexpr float kPaneNormalMinimumCosine = 0.999f;
		constexpr float kCameraSideEpsilon = 1.0e-3f;
		constexpr auto kDiagnosticPeriod = std::chrono::seconds(10);

		// Runtime-selected RendererShadowState layout (flat SE/AE vs VR 1.4.15).
		// VR fields carry two eyes (posAdjust[2] at 0x3A4, cameraData[2] at
		// 0x3E0, eye 1 following eye 0 by sizeof(NiPoint3) / sizeof(ViewData)).
		// Eye 0 (left) is the single `mainView` every mirror-side test and the
		// deferred-capture pairing reads; the pane itself is drawn once per eye.
		struct ShadowStateLayout
		{
			std::ptrdiff_t renderTargets{ 0 };
			std::ptrdiff_t depthStencil{ 0 };
			std::ptrdiff_t depthStencilSlice{ 0 };
			std::ptrdiff_t viewPort{ 0 };
			std::ptrdiff_t posAdjust{ 0 };
			std::ptrdiff_t cameraData{ 0 };
			std::uint32_t eyeCount{ 1 };
			std::ptrdiff_t posAdjustEyeStride{ 0x0C };
			std::ptrdiff_t viewDataEyeStride{ 0x250 };
		};
		constexpr ShadowStateLayout kFlatShadowStateLayout{
			.renderTargets = 0x18,
			.depthStencil = 0x38,
			.depthStencilSlice = 0x3C,
			.viewPort = 0x70,
			.posAdjust = 0x35C,
			.cameraData = 0x380,
			.eyeCount = 1
		};
		constexpr ShadowStateLayout kVRShadowStateLayout{
			.renderTargets = 0x20,
			.depthStencil = 0x40,
			.depthStencilSlice = 0x44,
			.viewPort = 0x78,
			.posAdjust = 0x3A4,
			.cameraData = 0x3E0,
			.eyeCount = 2
		};
		// Largest eye count any supported runtime carries (Skyrim VR: 2).
		constexpr std::uint32_t kMaximumEyeCount = 2;
		static_assert(kFlatShadowStateLayout.eyeCount <= kMaximumEyeCount);
		static_assert(kVRShadowStateLayout.eyeCount <= kMaximumEyeCount);
		static_assert(sizeof(RE::NiPoint3) == 0x0C);
		static_assert(sizeof(RE::BSGraphics::ViewData) == 0x250);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, renderTargets) == kFlatShadowStateLayout.renderTargets);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, depthStencil) == kFlatShadowStateLayout.depthStencil);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, depthStencilSlice) == kFlatShadowStateLayout.depthStencilSlice);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, viewPort) == kFlatShadowStateLayout.viewPort);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, posAdjust) == kFlatShadowStateLayout.posAdjust);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, cameraData) == kFlatShadowStateLayout.cameraData);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, renderTargets) == kVRShadowStateLayout.renderTargets);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, depthStencil) == kVRShadowStateLayout.depthStencil);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, depthStencilSlice) == kVRShadowStateLayout.depthStencilSlice);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, viewPort) == kVRShadowStateLayout.viewPort);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, posAdjust) == kVRShadowStateLayout.posAdjust);
		static_assert(offsetof(RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, cameraData) == kVRShadowStateLayout.cameraData);

		[[nodiscard]] const ShadowStateLayout& CurrentShadowStateLayout() noexcept
		{
			return SupportedRuntimePolicy::IsExactVRRuntime() ?
				kVRShadowStateLayout : kFlatShadowStateLayout;
		}
		static_assert(offsetof(RE::BSGraphics::ViewData, viewUp) == 0x00);
		static_assert(offsetof(RE::BSGraphics::ViewData, viewRight) == 0x10);
		static_assert(offsetof(RE::BSGraphics::ViewData, viewForward) == 0x20);
		static_assert(offsetof(RE::BSGraphics::ViewData, viewMat) == 0x30);
		static_assert(offsetof(RE::BSGraphics::ViewData, projMat) == 0x70);
		static_assert(offsetof(RE::BSGraphics::ViewData, viewProjMat) == 0xB0);
		static_assert(offsetof(
			RE::BSGraphics::ViewData, viewProjMatrixUnjittered) == 0x130);
		static_assert(offsetof(
			RE::BSGraphics::ViewData, projMatrixUnjittered) == 0x1B0);

		struct Counters
		{
			std::atomic<std::uint64_t> allQueryCalls{ 0 };
			std::atomic<std::uint64_t> mainWorldHits{ 0 };
			std::atomic<std::uint64_t> privatePassSkips{ 0 };
			std::atomic<std::uint64_t> outsidePassSkips{ 0 };
			std::atomic<std::uint64_t> reentries{ 0 };
			std::atomic<std::uint64_t> noPublishedFrame{ 0 };
			// Frames served from the previous publication because this one had
			// none; without it the pane shows its authored black instead.
			std::atomic<std::uint64_t> retainedFrameRedraws{ 0 };
			std::atomic<std::uint64_t> retainedFrameExpiries{ 0 };
			std::atomic<std::uint64_t> candidateMismatch{ 0 };
			std::atomic<std::uint64_t> locationContainmentRejects{ 0 };
			std::atomic<std::uint64_t> paneResolveFailures{ 0 };
			std::atomic<std::uint64_t> paneBaseFailures{ 0 };
			std::atomic<std::uint64_t> paneReferenceFailures{ 0 };
			std::atomic<std::uint64_t> paneRootFailures{ 0 };
			std::atomic<std::uint64_t> paneIdentityFailures{ 0 };
			std::atomic<std::uint64_t> paneTransformFailures{ 0 };
			std::atomic<std::uint64_t> paneResolveExceptions{ 0 };
			std::atomic<std::uint64_t> panePlaneMismatch{ 0 };
			std::atomic<std::uint64_t> panePlaneIdentityRejects{ 0 };
			std::atomic<std::uint64_t> panePlaneNormalRejects{ 0 };
			std::atomic<std::uint64_t> panePlaneOffPlaneRejects{ 0 };
			std::atomic<std::uint64_t> panePlaneCameraSideRejects{ 0 };
			std::atomic<std::uint64_t> panePlaneExtentRejects{ 0 };
			std::atomic<std::uint64_t> engineTargetFailures{ 0 };
			std::atomic<std::uint64_t> engineStateMissing{ 0 };
			std::atomic<std::uint64_t> engineIndexFailures{ 0 };
			std::atomic<std::uint64_t> enginePointerFailures{ 0 };
			std::atomic<std::uint64_t> engineStateExceptions{ 0 };
			// Pane callbacks raised while Skyrim renders the refraction-normals
			// layer (kREFRACTION_NORMALS bound as colour target 0).  That 8-bit
			// layer is consumed by the refraction composite as per-pixel distortion,
			// never as colour, so no reflection may ever be delivered into it.
			std::atomic<std::uint64_t> engineRefractionPassSkips{ 0 };
			std::atomic<std::uint64_t> rendererInitFailures{ 0 };
			std::atomic<std::uint64_t> projectionRejected{ 0 };
			std::atomic<std::uint64_t> projectionInvalid{ 0 };
			std::atomic<std::uint64_t> projectionMainViewNotVisible{ 0 };
			std::atomic<std::uint64_t> projectionReflectedUncovered{ 0 };
			std::atomic<std::uint64_t> projectionContinuityLeaseAttempts{ 0 };
			std::atomic<std::uint64_t> projectionContinuityLeaseRenewals{ 0 };
			std::atomic<std::uint64_t> projectionContinuityLeaseRejects{ 0 };
			std::atomic<std::uint64_t> projectionContinuityNativeVisibilityMissing{ 0 };
			std::atomic<std::uint64_t> projectionContinuityExceptions{ 0 };
			std::atomic<std::uint64_t> ageRejectProofAttempts{ 0 };
			std::atomic<std::uint64_t> ageRejectProofAccepted{ 0 };
			std::atomic<std::uint64_t> ageRejectProofMainNotVisible{ 0 };
			std::atomic<std::uint64_t> ageRejectProofProjectionInvalid{ 0 };
			std::atomic<std::uint64_t> ageRejectProofIdentityLocationRejects{ 0 };
			std::atomic<std::uint64_t> ageRejectProofNativeVisibilityMissing{ 0 };
			std::atomic<std::uint64_t> projectionPartialDraws{ 0 };
			std::atomic<std::uint64_t> targetRejected{ 0 };
			std::atomic<std::uint64_t> rendererRejections{ 0 };
			std::atomic<std::uint64_t> rendererSetupRejected{ 0 };
			std::atomic<std::uint64_t> rendererTargetInvalid{ 0 };
			std::atomic<std::uint64_t> shaderResourceAliasSkips{ 0 };
			std::atomic<std::uint64_t> computeUAVAliasSkips{ 0 };
			std::atomic<std::uint64_t> outputMergerUAVSkips{ 0 };
			std::atomic<std::uint64_t> constantBufferMapFailures{ 0 };
			std::atomic<std::uint64_t> streamOutputSkips{ 0 };
			std::atomic<std::uint64_t> draws{ 0 };
			std::atomic<std::uint64_t> panePairedDraws{ 0 };
			std::atomic<std::uint64_t> panePairedFallbacks{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveAttempts{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveBasicShapeRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterFiniteRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterInputBasisRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterViewBasisRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterAxisRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterVPRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterProjectionRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterFrustumRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRasterRightSignRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredAttempts{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredFiniteRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredInputBasisRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredViewBasisRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredAxisRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredVPRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredProjectionRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredFrustumRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveHandUnjitteredRightSignRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveSourceCameraRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveOriginRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveRetainRejects{ 0 };
			std::atomic<std::uint64_t> deferredPairObserveTargetIdentityRejects{ 0 };
			std::atomic<std::uint32_t> deferredPairObserveOriginDeltaMaxBits{ 0 };
			std::atomic<std::uint64_t> deferredPairSnapshots{ 0 };
			std::atomic<std::uint64_t> deferredPairDraws{ 0 };
			std::atomic<std::uint64_t> deferredPairLaggedDraws{ 0 };
			std::atomic<std::uint64_t> deferredPairMissing{ 0 };
			std::atomic<std::uint64_t> deferredPairCurrentViewUnverified{ 0 };
			std::atomic<std::uint64_t> deferredPairAgeRejected{ 0 };
			std::atomic<std::uint64_t> deferredPairOriginMismatch{ 0 };
			std::atomic<std::uint64_t> deferredPairAcceptedAgeMax{ 0 };
			std::atomic<std::uint32_t> deferredPairOriginDeltaMaxBits{ 0 };
			std::atomic<std::uint64_t> paneStaleOriginFrames{ 0 };
			std::atomic<std::uint32_t> paneOriginDeltaMaxBits{ 0 };
			std::atomic<std::uint64_t> variantChecks{ 0 };
			std::atomic<std::uint64_t> variantMismatches{ 0 };
			std::atomic<std::uint64_t> variantStaleDraws{ 0 };
			std::atomic<std::uint64_t> variantSkips{ 0 };
			std::atomic<std::uint64_t> variantUnavailable{ 0 };
			std::atomic<std::uint32_t> variantDeltaMaxBits{ 0 };
			std::atomic<std::uint64_t> motionTargetMissing{ 0 };
			std::atomic<std::uint64_t> motionVelocityEligible{ 0 };
			std::atomic<std::uint64_t> motionVelocityIneligible{ 0 };
			std::atomic<std::uint64_t> paneSuppressions{ 0 };
			std::atomic<std::uint64_t> paneRestores{ 0 };
			// 'Store Mirror' flicker (owner, 2026-09-15): is the game still
			// offering the prompt on the frames it disappears, or not?
			std::atomic<std::uint64_t> crosshairHeld{ 0 };
			std::atomic<std::uint64_t> crosshairLost{ 0 };
			std::atomic<std::uint64_t> crosshairChanges{ 0 };
			std::atomic<std::uint64_t> paneRestoreFailures{ 0 };
			std::atomic<std::uint64_t> stateRestores{ 0 };
			// Stereo presentation: extra (eye > 0) pane draws issued/refused, and
			// frames whose eye rectangles had to be derived by halving the single
			// bound viewport because the engine bound fewer viewports than eyes.
			std::atomic<std::uint64_t> stereoEyeDraws{ 0 };
			std::atomic<std::uint64_t> stereoEyeDrawRejects{ 0 };
			std::atomic<std::uint64_t> stereoViewportSplits{ 0 };
			std::atomic<std::uint64_t> stereoViewportQueryFailures{ 0 };
			std::atomic<std::uint64_t> faults{ 0 };
		};

		struct EngineDeliveryState
		{
			ID3D11Device* device{ nullptr };
			ID3D11DeviceContext* context{ nullptr };
			// Eye 0.  Identical to eyeViews[0]; kept so every existing reader of
			// the single main view (mirror tests, deferred capture pairing, the
			// variant guard, the hand runtime query) keeps its eye-0 semantics.
			MirrorPaneRenderer::MainView mainView{};
			// targets.viewport is the eye-0 rectangle; see eyeViewports.
			MirrorPaneRenderer::DrawTargets targets{};
			MirrorCameraOverride::RasterCameraInput rasterCamera{};
			MirrorCameraOverride::RasterCameraInput unjitteredRasterCamera{};
			// One raster main view and viewport per eye (1 flat, 2 VR).  The pane
			// is a plain non-instanced quad draw, so VR delivery issues it once per
			// eye with that eye's view/projection and viewport rectangle.
			std::array<MirrorPaneRenderer::MainView, kMaximumEyeCount> eyeViews{};
			std::array<D3D11_VIEWPORT, kMaximumEyeCount> eyeViewports{};
			std::uint32_t eyeCount{ 1 };
			std::uint32_t graphicsFrame{ 0 };
		};

		enum class EngineDeliveryStateStatus : std::uint8_t
		{
			kReady,
			kGraphicsStateMissing,
			kShadowStateMissing,
			kRendererMissing,
			kColorIndexOutOfRange,
			kDepthIndexOutOfRange,
			kDepthSliceOutOfRange,
			kDeviceMissing,
			kContextMissing,
			kColorTargetMissing,
			kDepthTargetMissing,
			kRefractionPassTarget,
			kException
		};

		Counters g_counters{};
		std::atomic_bool g_installAttempted{ false };
		std::atomic_bool g_hookInstalled{ false };
		std::atomic_bool g_sharedSeamReady{ false };
		std::atomic<std::uintptr_t> g_hookCallSite{ 0 };
		std::atomic<std::uintptr_t> g_installedHookBranchTarget{ 0 };
		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_drawEnabled{ false };
		// Acquires and retains exact SE slot 7 for the hand presentation path.
		std::atomic_bool g_motionRequested{ false };
		std::atomic_bool g_variantGuardRequested{ false };
		std::atomic_bool g_projectionContinuityLeaseRequested{ false };
		std::atomic_bool g_projectionContinuityLeaseEnabled{ false };
		std::atomic_bool g_deliveryFaulted{ false };
		std::atomic<unsigned long> g_lastException{ 0 };
		std::atomic<MirrorPaneSurface::ResolveStatus> g_lastPaneResolveStatus{
			MirrorPaneSurface::ResolveStatus::kResolved };
		std::atomic<EngineDeliveryStateStatus> g_lastEngineTargetStatus{
			EngineDeliveryStateStatus::kReady };
		std::atomic<MirrorPaneRenderer::InitializationStatus> g_lastRendererInitStatus{
			MirrorPaneRenderer::InitializationStatus::kReady };
		std::atomic<MirrorPaneRenderer::DrawStatus> g_lastRendererStatus{
			MirrorPaneRenderer::DrawStatus::kDrawn };
		std::atomic<std::int64_t> g_lastPeriodicLogMilliseconds{ 0 };
		std::atomic_bool g_loggedFirstDraw{ false };
		std::atomic_bool g_loggedFirstLocationContainmentReject{ false };
		stl::no_destructor<MirrorPaneRenderer::Renderer> g_renderer{};
		std::atomic_bool g_coplanarFallbackReplacement{ false };
		std::vector<MirrorPaneRenderer::MotionHistory> g_motionHistories{};
		// The previous delivery frame's raster main view, from this module's own
		// proven draw-time read. The fence-time VP snapshot was falsified (it
		// holds a different camera variant: the paired v1 pane floated); the
		// capture is measured one frame behind the live raster, so pairing uses
		// the previous frame's OWN mainView instead.
		// Per-eye: the held view must substitute every eye's own raster view,
		// never eye 0's for both, or the right eye would draw the pane at the
		// left eye's SV_Position.
		struct PreviousMainView
		{
			std::array<MirrorPaneRenderer::MainView, kMaximumEyeCount> eyeViews{};
			std::uint32_t eyeCount{ 0 };
			std::uint64_t deliveryFrame{ 0 };
			bool valid{ false };
		};
		std::vector<PreviousMainView> g_previousMainViews{};
		std::vector<MultiMirrorPolicy::Identity> g_deliveredIdentities{};
		std::atomic_bool g_loggedArrayColorTargetReject{ false };
		// Mips are generated by the producer before a completed frame is published.
		// Counts every delivery call (one per main-world frame) so velocity
		// output can require a history exactly one frame old.
		std::atomic<std::uint64_t> g_deliveryFrameIndex{ 0 };
		// First few pane-transform rejects log their comparison values so the
		// failing branch can be attributed without a debugger attached.
		std::atomic<std::uint32_t> g_paneTransformRejectLogBudget{ 8 };
		thread_local std::uint32_t g_mainWorldDepth = 0;
		thread_local std::uint32_t g_privatePassDepth = 0;
		thread_local bool g_inCallback = false;
		thread_local std::uint64_t g_mainWorldFrame = 0;
		thread_local HandMirrorVRRuntimePolicy::CaptureViewCohorts<DeferredCaptureMainView> g_captureViews{};
		thread_local DeferredCaptureMainView& g_deferredCaptureMainView = g_captureViews.Get(0);
		struct MainTargetIdentityState
		{
			std::uintptr_t colorResource{ 0 };
			std::uintptr_t depthResource{ 0 };
			std::uint64_t allocationGeneration{ 0x8000000000000000ULL };
			// Keeping the previous resource pair AddRef-retained makes an equal raw
			// address an actual equal live COM object, not an allocator ABA.
			RetainedMainTargetHandle retainedAllocation{};
		};
		thread_local MainTargetIdentityState g_mainTargetIdentity{};
		thread_local MainTargetIdentityState g_handMainTargetIdentity{};
		std::atomic<std::uint32_t> g_cohortDriftLogCount{ 0 };

		[[nodiscard]] bool SeparateVRViews() noexcept
		{
			return HandMirrorApprovedContentReadOnlyObserver::VRViewCohortFixEnabled();
		}

		[[nodiscard]] std::size_t ViewIndex(const bool handPane) noexcept
		{
			return HandMirrorVRRuntimePolicy::ViewCohortIndex(SeparateVRViews(), handPane);
		}

		[[nodiscard]] bool IsSupportedRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			return (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
			       (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			       SupportedRuntimePolicy::IsExactVRRuntime();
		}

		[[nodiscard]] bool IsProjectionContinuityAcceptedRuntime() noexcept
		{
			return REL::Module::IsSE() &&
				REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 };
		}

		struct DeliveryLocationSnapshot
		{
			MirrorInteriorEligibility::Observation observation{};
			MirrorInteriorEligibility::LocationIdentity location{};
			bool readFault{ false };
		};

		[[nodiscard]] MirrorInteriorEligibility::Status ReadDeliveryLocationSEH(
			DeliveryLocationSnapshot& a_snapshot) noexcept
		{
			a_snapshot = {};
			__try {
				auto* world = RE::TES::GetSingleton();
				a_snapshot.observation.worldAvailable = world != nullptr;
				auto* player = RE::PlayerCharacter::GetSingleton();
				a_snapshot.observation.playerAvailable = player != nullptr;

				auto* worldInterior = world ? world->interiorCell : nullptr;
				a_snapshot.observation.worldInteriorPresent = worldInterior != nullptr;
				if (worldInterior) {
					a_snapshot.observation.worldInteriorFlag =
						worldInterior->IsInteriorCell();
					a_snapshot.observation.worldInteriorAttached =
						worldInterior->IsAttached();
				}

				auto* playerCell = player ? player->GetParentCell() : nullptr;
				a_snapshot.observation.playerCellPresent = playerCell != nullptr;
				if (playerCell) {
					a_snapshot.location.playerCell.address =
						reinterpret_cast<std::uintptr_t>(playerCell);
					a_snapshot.location.playerCell.formID = playerCell->GetFormID();
					a_snapshot.observation.playerCellInteriorFlag =
						playerCell->IsInteriorCell();
					a_snapshot.observation.playerCellAttached = playerCell->IsAttached();
				}
				a_snapshot.observation.sameCell =
					worldInterior && playerCell && worldInterior == playerCell;

				auto* playerWorldSpace = player ? player->GetWorldspace() : nullptr;
				a_snapshot.observation.playerWorldSpacePresent =
					playerWorldSpace != nullptr;
				if (playerWorldSpace) {
					a_snapshot.location.worldspace.address =
						reinterpret_cast<std::uintptr_t>(playerWorldSpace);
					a_snapshot.location.worldspace.formID = playerWorldSpace->GetFormID();
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				a_snapshot.readFault = true;
			}

			return a_snapshot.readFault ?
			           MirrorInteriorEligibility::Status::kUnavailable :
			           MirrorInteriorEligibility::Classify(a_snapshot.observation);
		}

		[[nodiscard]] bool MirrorDeliveryLocationCurrent(
			const MirrorRecognition::ActiveMirror& a_mirror,
			DeliveryLocationSnapshot& a_snapshot) noexcept
		{
			const auto status = ReadDeliveryLocationSEH(a_snapshot);
			if (!MirrorInteriorEligibility::AllowsMirrorCapture(status))
				return false;
			if (MirrorInteriorEligibility::RequiresCellIdentity(status) &&
				!MirrorInteriorEligibility::SameValidCell(
					a_mirror.parentCell, a_snapshot.location.playerCell)) {
				return false;
			}
			return MirrorRecognition::IsCandidateCurrentAtLocation(
				a_mirror.formID, a_mirror.candidateGeneration, status,
				a_snapshot.location);
		}

		[[nodiscard]] bool CopyWindowSEH(
			std::uintptr_t source,
			std::uint8_t* destination,
			std::size_t size) noexcept
		{
			if (!source || !destination || size == 0)
				return false;
			__try {
				std::memcpy(destination, reinterpret_cast<const void*>(source), size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool IsExecutableAddress(const void* address) noexcept
		{
			if (!address)
				return false;
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0 ||
				(info.Protect & PAGE_NOACCESS) != 0) {
				return false;
			}
			const DWORD protection = info.Protect & 0xFF;
			return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
			       protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool ReadCallTargetSEH(
			std::uintptr_t callSite,
			std::uintptr_t& target) noexcept
		{
			target = 0;
			std::array<std::uint8_t, 5> instruction{};
			if (!CopyWindowSEH(callSite, instruction.data(), instruction.size()) ||
				instruction[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, instruction.data() + 1, sizeof(displacement));
			target = callSite + instruction.size() + displacement;
			return target != 0;
		}

		[[nodiscard]] bool ValidateFiveByteBranchStubSEH(
			std::uintptr_t branchTarget,
			std::uintptr_t expectedDestination) noexcept
		{
			if (!branchTarget || !expectedDestination ||
				!IsExecutableAddress(reinterpret_cast<const void*>(branchTarget)) ||
				!IsExecutableAddress(reinterpret_cast<const void*>(expectedDestination))) {
				return false;
			}

			// CommonLibSSE-NG's pinned write_call<5> target is exactly
			// `jmp qword ptr [rip]` followed by the absolute destination.
			std::array<std::uint8_t, 14> stub{};
			if (!CopyWindowSEH(branchTarget, stub.data(), stub.size()) ||
				stub[0] != 0xFF || stub[1] != 0x25 || stub[2] != 0 || stub[3] != 0 ||
				stub[4] != 0 || stub[5] != 0) {
				return false;
			}

			std::uint64_t destination = 0;
			std::memcpy(&destination, stub.data() + 6, sizeof(destination));
			return static_cast<std::uintptr_t>(destination) == expectedDestination;
		}

		// "Store Mirror" flicker (owner, 2026-09-15): the prompt blinks only where
		// it overlaps the pane. Either the game stops offering it on those frames
		// (the crosshair pick dropped the mirror) or it offers it throughout and
		// something else changes underneath. Every counter we have is downstream of
		// our own draw, so none of them separates the two.
		//
		// This watches the game's crosshair target handle for churn. It reads the
		// raw 32-bit handle at the flat layout's documented offset and compares it
		// with the previous frame's -- no resolution, no retention, no form lookup,
		// because this runs on the render thread where none of that is safe. A
		// handle that holds steady exonerates the pick; one that churns convicts it.
		//
		// CommonLibSSE's CrosshairPickData declares the VR layout unless
		// EXCLUSIVE_SKYRIM_FLAT is set, and VR's target array sits at different
		// offsets, so the struct is not used: the read is confined to the two exact
		// flat runtimes whose 0x38-byte layout puts `target` at +0x04.
		void SampleCrosshairTarget() noexcept
		{
			static thread_local std::uint32_t previous = 0;
			static thread_local bool seeded = false;
			if (!SupportedRuntimePolicy::IsExactAE17104Runtime() &&
				!(REL::Module::IsSE() &&
					REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 }))
				return;
			std::uint32_t handle = 0;
			__try {
				const auto* pick = RE::CrosshairPickData::GetSingleton();
				if (!pick)
					return;
				std::memcpy(&handle,
					reinterpret_cast<const std::byte*>(pick) + 0x04, sizeof(handle));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return;
			}
			(handle ? g_counters.crosshairHeld : g_counters.crosshairLost)
				.fetch_add(1, std::memory_order_relaxed);
			if (seeded && handle != previous)
				g_counters.crosshairChanges.fetch_add(1, std::memory_order_relaxed);
			previous = handle;
			seeded = true;
		}

		bool RestorePaneSuppression(std::uint32_t formID = 0) noexcept
		{
			const auto result = MirrorPaneSurface::RestoreSuppression(formID);
			if (result == MirrorPaneSurface::RestoreResult::kRestored) {
				g_counters.paneRestores.fetch_add(1, std::memory_order_relaxed);
				return true;
			}
			if (result == MirrorPaneSurface::RestoreResult::kFailed) {
				g_counters.paneRestoreFailures.fetch_add(1, std::memory_order_relaxed);
				g_deliveryFaulted.store(true, std::memory_order_release);
				return false;
			}
			return true;
		}

		void RejectLocationContainment(
			const MirrorRecognition::ActiveMirror& a_mirror,
			const DeliveryLocationSnapshot& a_snapshot,
			const char* a_phase) noexcept
		{
			g_counters.locationContainmentRejects.fetch_add(
				1, std::memory_order_relaxed);
			if (!g_loggedFirstLocationContainmentReject.exchange(
					true, std::memory_order_relaxed)) {
				try {
					logger::warn(
						"[MirrorsOfSkyrim][PaneDelivery] mirror {:08X} generation={} {} skipped by location validation: "
						"playerCell={:08X} worldspace={:08X} mirrorParent={:08X} "
						"playerAttached={} playerInterior={} sameCell={} readFault={}",
						a_mirror.formID, a_mirror.candidateGeneration,
						a_phase ? a_phase : "delivery",
						a_snapshot.location.playerCell.formID,
						a_snapshot.location.worldspace.formID,
						a_mirror.parentCell.formID,
						a_snapshot.observation.playerCellAttached,
						a_snapshot.observation.playerCellInteriorFlag,
						a_snapshot.observation.sameCell,
						a_snapshot.readFault);
				} catch (...) {
				}
			}
			if (MirrorFramePublication::MultiMirrorEnabled()) {
				MirrorFramePublication::InvalidateCandidate(a_mirror.formID, "delivery-location");
				RestorePaneSuppression(a_mirror.formID);
			} else {
				MirrorFramePublication::Invalidate("delivery-location");
				RestorePaneSuppression();
			}
		}

		[[nodiscard]] const char* ToString(EngineDeliveryStateStatus status) noexcept
		{
			switch (status) {
			case EngineDeliveryStateStatus::kReady:
				return "ready";
			case EngineDeliveryStateStatus::kGraphicsStateMissing:
				return "graphics-state-missing";
			case EngineDeliveryStateStatus::kShadowStateMissing:
				return "shadow-state-missing";
			case EngineDeliveryStateStatus::kRendererMissing:
				return "renderer-missing";
			case EngineDeliveryStateStatus::kColorIndexOutOfRange:
				return "color-index-out-of-range";
			case EngineDeliveryStateStatus::kDepthIndexOutOfRange:
				return "depth-index-out-of-range";
			case EngineDeliveryStateStatus::kDepthSliceOutOfRange:
				return "depth-slice-out-of-range";
			case EngineDeliveryStateStatus::kDeviceMissing:
				return "device-missing";
			case EngineDeliveryStateStatus::kContextMissing:
				return "context-missing";
			case EngineDeliveryStateStatus::kColorTargetMissing:
				return "color-target-missing";
			case EngineDeliveryStateStatus::kDepthTargetMissing:
				return "depth-target-missing";
			case EngineDeliveryStateStatus::kRefractionPassTarget:
				return "refraction-pass-target";
			case EngineDeliveryStateStatus::kException:
				return "exception";
			default:
				return "unknown";
			}
		}

		void LatchEngineDeliveryStateException(
			const unsigned long exceptionCode) noexcept
		{
			// A guarded engine-state AV is not a healthy missing target.  Tombstone
			// the completed pane evidence before latching the process-terminal fault so
			// neither deferred capture nor a later generic pass can reuse it.
			g_captureViews.Reset();
			g_lastException.store(exceptionCode, std::memory_order_relaxed);
			g_lastEngineTargetStatus.store(
				EngineDeliveryStateStatus::kException,
				std::memory_order_relaxed);
			g_counters.engineTargetFailures.fetch_add(
				1, std::memory_order_relaxed);
			g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
			g_counters.engineStateExceptions.fetch_add(
				1, std::memory_order_relaxed);
			if (!g_deliveryFaulted.exchange(true, std::memory_order_acq_rel))
				g_counters.faults.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] EngineDeliveryStateStatus ReadEngineDeliveryStateSEH(
			EngineDeliveryState& output) noexcept
		{
			output = {};
			__try {
				auto* graphicsState = RE::BSGraphics::State::GetSingleton();
				auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
				auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
				if (!graphicsState)
					return EngineDeliveryStateStatus::kGraphicsStateMissing;
				if (!shadowState)
					return EngineDeliveryStateStatus::kShadowStateMissing;
				if (!renderer)
					return EngineDeliveryStateStatus::kRendererMissing;

				// Only Anniversary Edition shifts the counter; SE and VR keep 0x4C.
				output.graphicsFrame = GraphicsFrameCounterPolicy::Read(
					graphicsState, REL::Module::IsAE());
				// Raw, runtime-selected reads: CommonLib's GetRuntimeData() is the
				// flat view and must not be dereferenced on VR.
				const auto& layout = CurrentShadowStateLayout();
				const auto* shadowBytes =
					reinterpret_cast<const std::byte*>(shadowState);
				std::uint32_t targetIndex = 0;
				std::uint32_t depthIndex = 0;
				std::uint32_t depthSlice = 0;
				std::memcpy(&targetIndex, shadowBytes + layout.renderTargets,
					sizeof(targetIndex));
				std::memcpy(&depthIndex, shadowBytes + layout.depthStencil,
					sizeof(depthIndex));
				std::memcpy(&depthSlice, shadowBytes + layout.depthStencilSlice,
					sizeof(depthSlice));
				if (targetIndex >= static_cast<std::uint32_t>(RE::RENDER_TARGETS::kTOTAL))
					return EngineDeliveryStateStatus::kColorIndexOutOfRange;
				if (depthIndex >= static_cast<std::uint32_t>(
						RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL))
					return EngineDeliveryStateStatus::kDepthIndexOutOfRange;
				if (depthSlice >= 8)
					return EngineDeliveryStateStatus::kDepthSliceOutOfRange;
				// Skyrim re-renders the first-person geometry (hands and the held
				// mirror) into the refraction-normals layer whenever a refractive
				// object such as a fire is on screen.  The pane callback fires there
				// too, but that layer encodes distortion for the refraction composite;
				// a reflection written into it warps the main image by its own pixel
				// values (2026-09-03 RenderDoc frame 6752: torn helmet, doubled face,
				// window cells showing the depth silhouette).  Refuse the pass.
				if (targetIndex == static_cast<std::uint32_t>(
						RE::RENDER_TARGETS::kREFRACTION_NORMALS)) {
					return EngineDeliveryStateStatus::kRefractionPassTarget;
				}

				auto& rendererData = renderer->GetRuntimeData();
				auto& depthData = renderer->GetDepthStencilData();
				output.device = reinterpret_cast<ID3D11Device*>(
					RE::BSGraphics::Renderer::GetDevice());
				output.context = reinterpret_cast<ID3D11DeviceContext*>(rendererData.context);
				output.targets.colorRTV = rendererData.renderTargets[targetIndex].RTV;
				if (g_motionRequested.load(std::memory_order_acquire)) {
					constexpr auto motionIndex = static_cast<std::uint32_t>(
						RE::RENDER_TARGETS::kMOTION_VECTOR);
					static_assert(motionIndex <
						static_cast<std::uint32_t>(RE::RENDER_TARGETS::kTOTAL));
					output.targets.motionRTV = rendererData.renderTargets[motionIndex].RTV;
				}
				output.targets.depthDSV = depthData.depthStencils[depthIndex].views[depthSlice];
				std::memcpy(&output.targets.viewport, shadowBytes + layout.viewPort,
					sizeof(output.targets.viewport));

				const auto* cameraBytes = shadowBytes + layout.cameraData;
				std::memcpy(
					&output.mainView.origin, shadowBytes + layout.posAdjust,
					sizeof(output.mainView.origin));
				output.rasterCamera.origin = output.mainView.origin;
				std::memcpy(
					&output.rasterCamera.viewUp, cameraBytes + 0x00,
					sizeof(output.rasterCamera.viewUp));
				std::memcpy(
					&output.rasterCamera.viewRight, cameraBytes + 0x10,
					sizeof(output.rasterCamera.viewRight));
				std::memcpy(
					&output.rasterCamera.viewForward, cameraBytes + 0x20,
					sizeof(output.rasterCamera.viewForward));
				std::memcpy(
					&output.rasterCamera.view, cameraBytes + 0x30,
					sizeof(output.rasterCamera.view));
				std::memcpy(
					&output.rasterCamera.projection, cameraBytes + 0x70,
					sizeof(output.rasterCamera.projection));
				std::memcpy(
					&output.mainView.viewProjection, cameraBytes + 0xB0,
					sizeof(output.mainView.viewProjection));
				output.rasterCamera.viewProjection =
					output.mainView.viewProjection;
				// These matrices belong to the same flat ViewData object as the
				// primary raster sample above.  Preserve the live jittered VP for
				// pane placement; only the hand capture may consume this cohort.
				output.unjitteredRasterCamera = output.rasterCamera;
				std::memcpy(
					&output.unjitteredRasterCamera.viewProjection,
					cameraBytes + 0x130,
					sizeof(output.unjitteredRasterCamera.viewProjection));
				std::memcpy(
					&output.unjitteredRasterCamera.projection,
					cameraBytes + 0x1B0,
					sizeof(output.unjitteredRasterCamera.projection));
				if (!output.device)
					return EngineDeliveryStateStatus::kDeviceMissing;
				if (!output.context)
					return EngineDeliveryStateStatus::kContextMissing;
				if (!output.targets.colorRTV)
					return EngineDeliveryStateStatus::kColorTargetMissing;
				if (!output.targets.depthDSV)
					return EngineDeliveryStateStatus::kDepthTargetMissing;

				// Per-eye main views.  Eye 0 is the sample above; every further eye
				// slot is read through the same layout at its stride.  On flat this
				// only records the single eye.
				output.eyeCount = layout.eyeCount;
				if (output.eyeCount == 0 || output.eyeCount > kMaximumEyeCount)
					return EngineDeliveryStateStatus::kColorIndexOutOfRange;
				output.eyeViews[0] = output.mainView;
				output.eyeViewports[0] = output.targets.viewport;
				for (std::uint32_t eye = 1; eye < output.eyeCount; ++eye) {
					auto& eyeView = output.eyeViews[eye];
					std::memcpy(
						&eyeView.origin,
						shadowBytes + layout.posAdjust +
							static_cast<std::ptrdiff_t>(eye) * layout.posAdjustEyeStride,
						sizeof(eyeView.origin));
					std::memcpy(
						&eyeView.viewProjection,
						cameraBytes +
							static_cast<std::ptrdiff_t>(eye) * layout.viewDataEyeStride +
							0xB0,
						sizeof(eyeView.viewProjection));
					output.eyeViewports[eye] = output.targets.viewport;
				}
				if (output.eyeCount > 1) {
					// Skyrim VR selects each eye's rectangle by viewport index from
					// the rasterizer state, so the bound viewports are the authority.
					// If fewer viewports than eyes are bound, the eyes share one
					// side-by-side rectangle: left half = eye 0, right half = eye 1.
					std::array<D3D11_VIEWPORT, kMaximumEyeCount> boundViewports{};
					UINT boundViewportCount = kMaximumEyeCount;
					output.context->RSGetViewports(
						&boundViewportCount, boundViewports.data());
					if (boundViewportCount >= output.eyeCount) {
						for (std::uint32_t eye = 0; eye < output.eyeCount; ++eye)
							output.eyeViewports[eye] = boundViewports[eye];
					} else {
						const D3D11_VIEWPORT whole = boundViewportCount != 0 ?
							boundViewports[0] : output.targets.viewport;
						if (boundViewportCount == 0) {
							g_counters.stereoViewportQueryFailures.fetch_add(
								1, std::memory_order_relaxed);
						}
						const float eyeWidth =
							whole.Width / static_cast<float>(output.eyeCount);
						for (std::uint32_t eye = 0; eye < output.eyeCount; ++eye) {
							D3D11_VIEWPORT rect = whole;
							rect.TopLeftX =
								whole.TopLeftX + eyeWidth * static_cast<float>(eye);
							rect.Width = eyeWidth;
							output.eyeViewports[eye] = rect;
						}
						g_counters.stereoViewportSplits.fetch_add(
							1, std::memory_order_relaxed);
					}
					// Every single-viewport reader (deferred capture, hand query,
					// SameViewportBits) keeps eye 0.
					output.targets.viewport = output.eyeViewports[0];
				}
				return EngineDeliveryStateStatus::kReady;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				LatchEngineDeliveryStateException(GetExceptionCode());
				return EngineDeliveryStateStatus::kException;
			}
		}

		// Ground-truth main camera for the variant guard: the same WorldRoot
		// NiCamera the producer poses captures from. The delivery-time shadow
		// state holds a different camera variant on alternating third-person
		// frames (2026-08-19 session: 284-unit origin gap, per-frame pane
		// rejection bursts), so its origin is validated against this node.
		[[nodiscard]] bool ReadMainCameraOriginSEH(
			DirectX::XMFLOAT3& a_output) noexcept
		{
			a_output = {};
			RE::NiCamera* camera = nullptr;
			__try {
				camera = RE::Main::WorldRootCamera();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
			MirrorCameraOverride::CameraPose pose{};
			if (!MirrorCameraOverride::ReadFlatCameraPose(camera, pose))
				return false;
			a_output = pose.origin;
			return true;
		}

		[[nodiscard]] bool SameBytes(
			const void* left, const void* right, std::size_t size) noexcept;

		[[nodiscard]] bool ReadSourceCameraSampleSEH(
			FrozenSourceCamera& output) noexcept
		{
			output = {};
			static_assert(sizeof(RE::NiCamera::RUNTIME_DATA2) == 0x38);
			RE::NiCamera* camera = nullptr;
			__try {
				camera = RE::Main::WorldRootCamera();
				if (camera) {
					std::memcpy(
						output.runtimeData2.data(), &camera->GetRuntimeData2(),
						output.runtimeData2.size());
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_lastException.store(GetExceptionCode(), std::memory_order_relaxed);
				g_deliveryFaulted.store(true, std::memory_order_release);
				return false;
			}
			if (!camera)
				return false;
			MirrorCameraOverride::CameraPose pose{};
			if (!MirrorCameraOverride::ReadFlatCameraPose(camera, pose))
				return false;
			output.cameraIdentity = reinterpret_cast<std::uintptr_t>(camera);
			output.origin = pose.origin;
			output.forward = pose.forward;
			output.up = pose.up;
			output.right = pose.right;
			output.rasterAuthoritative = false;
			output.valid = true;
			return true;
		}

		[[nodiscard]] bool SameFrozenSourceCameraValue(
			const FrozenSourceCamera& left,
			const FrozenSourceCamera& right) noexcept
		{
			return left.valid && right.valid && left.cameraIdentity != 0 &&
				left.cameraIdentity == right.cameraIdentity &&
				SameBytes(&left.origin, &right.origin, sizeof(left.origin)) &&
				SameBytes(&left.forward, &right.forward, sizeof(left.forward)) &&
				SameBytes(&left.up, &right.up, sizeof(left.up)) &&
				SameBytes(&left.right, &right.right, sizeof(left.right)) &&
				SameBytes(
					&left.rasterView, &right.rasterView,
					sizeof(left.rasterView)) &&
				SameBytes(
					&left.rasterProjection, &right.rasterProjection,
					sizeof(left.rasterProjection)) &&
				SameBytes(
					&left.rasterFrustum.left, &right.rasterFrustum.left,
					sizeof(left.rasterFrustum.left)) &&
				SameBytes(
					&left.rasterFrustum.right, &right.rasterFrustum.right,
					sizeof(left.rasterFrustum.right)) &&
				SameBytes(
					&left.rasterFrustum.top, &right.rasterFrustum.top,
					sizeof(left.rasterFrustum.top)) &&
				SameBytes(
					&left.rasterFrustum.bottom, &right.rasterFrustum.bottom,
					sizeof(left.rasterFrustum.bottom)) &&
				SameBytes(
					&left.rasterFrustum.nearPlane,
					&right.rasterFrustum.nearPlane,
					sizeof(left.rasterFrustum.nearPlane)) &&
				SameBytes(
					&left.rasterFrustum.farPlane,
					&right.rasterFrustum.farPlane,
					sizeof(left.rasterFrustum.farPlane)) &&
				left.rasterFrustum.valid == right.rasterFrustum.valid &&
				SameBytes(
					&left.rasterViewProjectionUnjittered,
					&right.rasterViewProjectionUnjittered,
					sizeof(left.rasterViewProjectionUnjittered)) &&
				SameBytes(
					&left.rasterProjectionUnjittered,
					&right.rasterProjectionUnjittered,
					sizeof(left.rasterProjectionUnjittered)) &&
				SameBytes(
					&left.rasterFrustumUnjittered.left,
					&right.rasterFrustumUnjittered.left,
					sizeof(left.rasterFrustumUnjittered.left)) &&
				SameBytes(
					&left.rasterFrustumUnjittered.right,
					&right.rasterFrustumUnjittered.right,
					sizeof(left.rasterFrustumUnjittered.right)) &&
				SameBytes(
					&left.rasterFrustumUnjittered.top,
					&right.rasterFrustumUnjittered.top,
					sizeof(left.rasterFrustumUnjittered.top)) &&
				SameBytes(
					&left.rasterFrustumUnjittered.bottom,
					&right.rasterFrustumUnjittered.bottom,
					sizeof(left.rasterFrustumUnjittered.bottom)) &&
				SameBytes(
					&left.rasterFrustumUnjittered.nearPlane,
					&right.rasterFrustumUnjittered.nearPlane,
					sizeof(left.rasterFrustumUnjittered.nearPlane)) &&
				SameBytes(
					&left.rasterFrustumUnjittered.farPlane,
					&right.rasterFrustumUnjittered.farPlane,
					sizeof(left.rasterFrustumUnjittered.farPlane)) &&
				left.rasterFrustumUnjittered.valid ==
					right.rasterFrustumUnjittered.valid &&
				SameBytes(
					left.runtimeData2.data(), right.runtimeData2.data(),
					left.runtimeData2.size()) &&
				left.rasterAuthoritative == right.rasterAuthoritative &&
				left.unjitteredRasterAuthoritative ==
					right.unjitteredRasterAuthoritative;
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT3& value) noexcept
		{
			return std::isfinite(value.x) && std::isfinite(value.y) &&
			       std::isfinite(value.z);
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT4X4& value) noexcept
		{
			for (std::size_t row = 0; row < 4; ++row) {
				for (std::size_t column = 0; column < 4; ++column) {
					if (!std::isfinite(value.m[row][column]))
						return false;
				}
			}
			return true;
		}

		void ReleaseRetainedInterfaceSEH(IUnknown* value) noexcept
		{
			if (!value)
				return;
			__try {
				value->Release();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_lastException.store(GetExceptionCode(), std::memory_order_relaxed);
				g_deliveryFaulted.store(true, std::memory_order_release);
				g_counters.faults.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void DestroyRetainedMainTarget(
			const RetainedMainTarget* retained) noexcept
		{
			if (!retained)
				return;
			auto value = *retained;
			delete retained;
			// The value object is already gone before any native vtable call.  A bad
			// Release therefore cannot preserve a reachable authorization or prevent
			// best-effort release of the other seven independent references.
			ReleaseRetainedInterfaceSEH(value.depthResource);
			ReleaseRetainedInterfaceSEH(value.motionResource);
			ReleaseRetainedInterfaceSEH(value.colorResource);
			ReleaseRetainedInterfaceSEH(value.depthDSV);
			ReleaseRetainedInterfaceSEH(value.motionRTV);
			ReleaseRetainedInterfaceSEH(value.colorRTV);
			ReleaseRetainedInterfaceSEH(value.context);
			ReleaseRetainedInterfaceSEH(value.device);
		}

		struct RawRetainedMainTarget
		{
			RetainedMainTarget value{};
			bool deviceRetained{ false };
			bool contextRetained{ false };
			bool colorViewRetained{ false };
			bool motionViewRetained{ false };
			bool depthViewRetained{ false };
		};

		void ReleaseRawRetainedMainTarget(
			RawRetainedMainTarget& retained) noexcept
		{
			ReleaseRetainedInterfaceSEH(retained.value.depthResource);
			ReleaseRetainedInterfaceSEH(retained.value.motionResource);
			ReleaseRetainedInterfaceSEH(retained.value.colorResource);
			if (retained.depthViewRetained)
				ReleaseRetainedInterfaceSEH(retained.value.depthDSV);
			if (retained.motionViewRetained)
				ReleaseRetainedInterfaceSEH(retained.value.motionRTV);
			if (retained.colorViewRetained)
				ReleaseRetainedInterfaceSEH(retained.value.colorRTV);
			if (retained.contextRetained)
				ReleaseRetainedInterfaceSEH(retained.value.context);
			if (retained.deviceRetained)
				ReleaseRetainedInterfaceSEH(retained.value.device);
			retained = {};
		}

		void DiscardRetainedOptionalMotion(
			RawRetainedMainTarget& retained) noexcept
		{
			ReleaseRetainedInterfaceSEH(retained.value.motionResource);
			retained.value.motionResource = nullptr;
			if (retained.motionViewRetained)
				ReleaseRetainedInterfaceSEH(retained.value.motionRTV);
			retained.value.motionRTV = nullptr;
			retained.motionViewRetained = false;
		}

		[[nodiscard]] RetainedMainTargetCoreIdentity MakeCoreIdentity(
			const RetainedMainTarget& retained) noexcept
		{
			return {
				.device = reinterpret_cast<std::uintptr_t>(retained.device),
				.context = reinterpret_cast<std::uintptr_t>(retained.context),
				.colorView = reinterpret_cast<std::uintptr_t>(retained.colorRTV),
				.depthView = reinterpret_cast<std::uintptr_t>(retained.depthDSV),
				.colorResource = reinterpret_cast<std::uintptr_t>(
					retained.colorResource),
				.depthResource = reinterpret_cast<std::uintptr_t>(
					retained.depthResource)
			};
		}

		[[nodiscard]] bool RetainMainTargetRawSEH(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			ID3D11RenderTargetView* colorRTV,
			ID3D11RenderTargetView* motionRTV,
			ID3D11DepthStencilView* depthDSV,
			RawRetainedMainTarget& retained,
			bool& nativeFaulted) noexcept
		{
			retained = {};
			nativeFaulted = false;
			if (!device || !context || !colorRTV || !depthDSV)
				return false;
			ID3D11Device* contextDevice = nullptr;
			ID3D11Device* colorViewDevice = nullptr;
			ID3D11Device* motionViewDevice = nullptr;
			ID3D11Device* depthViewDevice = nullptr;
			ID3D11Device* colorResourceDevice = nullptr;
			ID3D11Device* motionResourceDevice = nullptr;
			ID3D11Device* depthResourceDevice = nullptr;
			bool coreExact = false;
			// Evaluated outside the SEH block: the acceptance is a cached process
			// fact, and a d3d11 wrapper makes every view report the real device
			// while the renderer holds the forwarding one.
			const bool forwardingDeviceAccepted =
				EngineDeviceIdentity::ForwardingDeviceAccepted();
			EngineDeviceIdentityPolicy::MainTargetDeviceOwnership ownership{};
			auto deviceDisposition =
				EngineDeviceIdentityPolicy::MainTargetDeviceDisposition::kReject;
			__try {
				// Retain and validate the mandatory presentation allocation first.
				// Optional slot 7 can never invalidate this color/depth ownership.
				device->AddRef();
				retained.value.device = device;
				retained.deviceRetained = true;
				context->AddRef();
				retained.value.context = context;
				retained.contextRetained = true;
				colorRTV->AddRef();
				retained.value.colorRTV = colorRTV;
				retained.colorViewRetained = true;
				depthDSV->AddRef();
				retained.value.depthDSV = depthDSV;
				retained.depthViewRetained = true;

				colorRTV->GetResource(&retained.value.colorResource);
				depthDSV->GetResource(&retained.value.depthResource);
				context->GetDevice(&contextDevice);
				colorRTV->GetDevice(&colorViewDevice);
				depthDSV->GetDevice(&depthViewDevice);
				if (retained.value.colorResource)
					retained.value.colorResource->GetDevice(&colorResourceDevice);
				if (retained.value.depthResource)
					retained.value.depthResource->GetDevice(&depthResourceDevice);
				ownership = {
					.engineDevice = reinterpret_cast<std::uintptr_t>(device),
					.contextDevice = reinterpret_cast<std::uintptr_t>(contextDevice),
					.targetDevices = {
						reinterpret_cast<std::uintptr_t>(colorViewDevice),
						reinterpret_cast<std::uintptr_t>(depthViewDevice),
						reinterpret_cast<std::uintptr_t>(colorResourceDevice),
						reinterpret_cast<std::uintptr_t>(depthResourceDevice) },
					.targetDeviceCount = 4
				};
				deviceDisposition =
					EngineDeviceIdentityPolicy::ClassifyMainTargetDeviceOwnership(
						ownership, forwardingDeviceAccepted);
				coreExact = retained.value.colorResource &&
					retained.value.depthResource &&
					retained.value.colorResource != retained.value.depthResource &&
					deviceDisposition !=
						EngineDeviceIdentityPolicy::MainTargetDeviceDisposition::kReject;

				if (coreExact && motionRTV) {
					motionRTV->AddRef();
					retained.value.motionRTV = motionRTV;
					retained.motionViewRetained = true;
					motionRTV->GetResource(&retained.value.motionResource);
					motionRTV->GetDevice(&motionViewDevice);
					if (retained.value.motionResource) {
						retained.value.motionResource->GetDevice(
							&motionResourceDevice);
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
				g_lastException.store(GetExceptionCode(), std::memory_order_relaxed);
			}
			ReleaseRetainedInterfaceSEH(depthResourceDevice);
			ReleaseRetainedInterfaceSEH(motionResourceDevice);
			ReleaseRetainedInterfaceSEH(colorResourceDevice);
			ReleaseRetainedInterfaceSEH(depthViewDevice);
			ReleaseRetainedInterfaceSEH(motionViewDevice);
			ReleaseRetainedInterfaceSEH(colorViewDevice);
			ReleaseRetainedInterfaceSEH(contextDevice);
			if (nativeFaulted || !coreExact)
				return false;

			// The optional motion attachment must live on the device that owns the
			// color/depth objects; through a forwarding device that is the real one.
			const auto resourceDevice =
				EngineDeviceIdentityPolicy::MainTargetResourceDevice(
					ownership, deviceDisposition);
			if (deviceDisposition ==
				EngineDeviceIdentityPolicy::MainTargetDeviceDisposition::kForwarded) {
				EngineDeviceIdentity::NoteResourceDevice(resourceDevice);
				EngineDeviceIdentity::LogForwardedAcceptanceOnce(
					ownership.engineDevice, resourceDevice);
			}
			auto motionCore = MakeCoreIdentity(retained.value);
			motionCore.device = resourceDevice;
			const auto motionDisposition = ClassifyOptionalMotionRetention(
				motionCore,
				{
					.view = reinterpret_cast<std::uintptr_t>(
						retained.value.motionRTV),
					.resource = reinterpret_cast<std::uintptr_t>(
						retained.value.motionResource),
					.viewDevice = reinterpret_cast<std::uintptr_t>(motionViewDevice),
					.resourceDevice = reinterpret_cast<std::uintptr_t>(
						motionResourceDevice)
				});
			if (motionDisposition ==
				OptionalMotionRetentionDisposition::kDiscardInvalid) {
				DiscardRetainedOptionalMotion(retained);
			}
			return !g_deliveryFaulted.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool RetainMainTargetSEH(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			ID3D11RenderTargetView* colorRTV,
			ID3D11RenderTargetView* motionRTV,
			ID3D11DepthStencilView* depthDSV,
			RetainedMainTargetHandle& output) noexcept
		{
			output.reset();
			if (!device || !context || !colorRTV || !depthDSV)
				return false;

			RawRetainedMainTarget retained{};
			bool faulted = false;
			const bool exact = RetainMainTargetRawSEH(
				device, context, colorRTV, motionRTV, depthDSV, retained, faulted);
			if (faulted || g_deliveryFaulted.load(std::memory_order_acquire)) {
				g_deliveryFaulted.store(true, std::memory_order_release);
				g_counters.faults.fetch_add(1, std::memory_order_relaxed);
				ReleaseRawRetainedMainTarget(retained);
				return false;
			}
			if (!exact) {
				ReleaseRawRetainedMainTarget(retained);
				return false;
			}

			auto* value = new (std::nothrow) RetainedMainTarget{ retained.value };
			if (!value) {
				ReleaseRawRetainedMainTarget(retained);
				g_deliveryFaulted.store(true, std::memory_order_release);
				return false;
			}
			// Ownership has moved to the custom shared owner; prevent the raw
			// rollback helper from releasing it a second time.
			retained = {};
			try {
				output = RetainedMainTargetHandle(
					value, &DestroyRetainedMainTarget);
			} catch (...) {
				DestroyRetainedMainTarget(value);
				g_deliveryFaulted.store(true, std::memory_order_release);
				return false;
			}
			return static_cast<bool>(output);
		}

		[[nodiscard]] bool SameRetainedColorDepthResources(
			const RetainedMainTargetHandle& left,
			const RetainedMainTargetHandle& right) noexcept
		{
			return left && right && left->colorResource && left->depthResource &&
				left->colorResource == right->colorResource &&
				left->depthResource == right->depthResource;
		}

		[[nodiscard]] bool SameRetainedMainTargetCore(
			const RetainedMainTargetHandle& left,
			const RetainedMainTargetHandle& right) noexcept
		{
			return left && right &&
				SameRetainedColorDepthResources(left, right) &&
				SameRetainedMainTargetCoreIdentity(
					MakeCoreIdentity(*left), MakeCoreIdentity(*right));
		}

		[[nodiscard]] bool SameRetainedMainTarget(
			const RetainedMainTargetHandle& left,
			const RetainedMainTargetHandle& right) noexcept
		{
			return SameRetainedMainTargetCore(left, right) &&
				(left->motionRTV == nullptr) ==
					(left->motionResource == nullptr) &&
				(right->motionRTV == nullptr) ==
					(right->motionResource == nullptr) &&
				left->motionRTV == right->motionRTV &&
				left->motionResource == right->motionResource;
		}

		[[nodiscard]] bool SameBytes(
			const void* left, const void* right, const std::size_t size) noexcept
		{
			return left && right && size != 0 &&
				std::memcmp(left, right, size) == 0;
		}

		[[nodiscard]] bool SameViewportBits(
			const D3D11_VIEWPORT& left,
			const D3D11_VIEWPORT& right) noexcept
		{
			const auto same = [](const float a, const float b) noexcept {
				return std::bit_cast<std::uint32_t>(a) ==
					std::bit_cast<std::uint32_t>(b);
			};
			return same(left.TopLeftX, right.TopLeftX) &&
				same(left.TopLeftY, right.TopLeftY) &&
				same(left.Width, right.Width) && same(left.Height, right.Height) &&
				same(left.MinDepth, right.MinDepth) &&
				same(left.MaxDepth, right.MaxDepth);
		}

		// Retain the exact raster view observed during this outer main-world
		// invocation.  Exact-main capture runs only after that invocation returns,
		// so this is the only source-view sample that can be paired without
		// guessing from a later delivery call.  Gross shadow-state camera variants
		// fail closed here; publication performs the stricter reflected-origin check.
		void ObserveDeferredCaptureMainView(
			const EngineDeliveryState& engine, const bool handPane = false) noexcept
		{
			const auto index = ViewIndex(handPane);
			auto& observed = g_captureViews.Get(index);
			auto& allocation = index == 1 ? g_handMainTargetIdentity : g_mainTargetIdentity;
			g_counters.deferredPairObserveAttempts.fetch_add(
				1, std::memory_order_relaxed);
			if (g_mainWorldDepth != 1 || g_mainWorldFrame == 0 ||
				engine.graphicsFrame == 0 ||
				!engine.device || !engine.context || !engine.targets.colorRTV ||
				!engine.targets.depthDSV ||
				!Finite(engine.mainView.origin) ||
				!Finite(engine.mainView.viewProjection)) {
				g_counters.deferredPairObserveBasicShapeRejects.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}

			MirrorCameraOverride::RasterCameraSample rasterCamera{};
			const auto rasterStatus =
				MirrorCameraOverride::ValidateRasterCameraSample(
					engine.rasterCamera, rasterCamera);
			if (rasterStatus !=
					MirrorCameraOverride::RasterCameraValidationStatus::kReady ||
				!rasterCamera.valid) {
				g_counters.deferredPairObserveRasterRejects.fetch_add(
					1, std::memory_order_relaxed);
				switch (rasterStatus) {
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kInputNotFinite:
					g_counters.deferredPairObserveRasterFiniteRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kInputBasisInvalid:
					g_counters.deferredPairObserveRasterInputBasisRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kViewBasisInvalid:
					g_counters.deferredPairObserveRasterViewBasisRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kExplicitAxisMismatch:
					g_counters.deferredPairObserveRasterAxisRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kViewProjectionMismatch:
					g_counters.deferredPairObserveRasterVPRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kProjectionShapeInvalid:
					g_counters.deferredPairObserveRasterProjectionRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kFrustumInvalid:
					g_counters.deferredPairObserveRasterFrustumRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kRightSignConventionInvalid:
					g_counters.deferredPairObserveRasterRightSignRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::kReady:
					// A ready status with an invalid output is still aggregate-dark; the
					// impossible shape is deliberately visible as an unpartitioned delta.
					break;
				}
				return;
			}
			g_counters.deferredPairObserveHandUnjitteredAttempts.fetch_add(
				1, std::memory_order_relaxed);
			MirrorCameraOverride::RasterCameraSample unjitteredRasterCamera{};
			const auto unjitteredRasterStatus =
				MirrorCameraOverride::ValidateRasterCameraSample(
					engine.unjitteredRasterCamera, unjitteredRasterCamera);
			const bool unjitteredRasterReady =
				unjitteredRasterStatus ==
					MirrorCameraOverride::RasterCameraValidationStatus::kReady &&
				unjitteredRasterCamera.valid;
			if (!unjitteredRasterReady) {
				g_counters.deferredPairObserveHandUnjitteredRejects.fetch_add(
					1, std::memory_order_relaxed);
				switch (unjitteredRasterStatus) {
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kInputNotFinite:
					g_counters.deferredPairObserveHandUnjitteredFiniteRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kInputBasisInvalid:
					g_counters.deferredPairObserveHandUnjitteredInputBasisRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kViewBasisInvalid:
					g_counters.deferredPairObserveHandUnjitteredViewBasisRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kExplicitAxisMismatch:
					g_counters.deferredPairObserveHandUnjitteredAxisRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kViewProjectionMismatch:
					g_counters.deferredPairObserveHandUnjitteredVPRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kProjectionShapeInvalid:
					g_counters.deferredPairObserveHandUnjitteredProjectionRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kFrustumInvalid:
					g_counters.deferredPairObserveHandUnjitteredFrustumRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::
					kRightSignConventionInvalid:
					g_counters.deferredPairObserveHandUnjitteredRightSignRejects.fetch_add(
						1, std::memory_order_relaxed);
					break;
				case MirrorCameraOverride::RasterCameraValidationStatus::kReady:
					// A ready status with an invalid output remains visible as an
					// aggregate rejection rather than being misclassified.
					break;
				}
			}

			FrozenSourceCamera sourceCamera{};
			if (!ReadSourceCameraSampleSEH(sourceCamera) ||
				!Finite(sourceCamera.origin)) {
				g_counters.deferredPairObserveSourceCameraRejects.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}

			const float deltaX = engine.mainView.origin.x - sourceCamera.origin.x;
			const float deltaY = engine.mainView.origin.y - sourceCamera.origin.y;
			const float deltaZ = engine.mainView.origin.z - sourceCamera.origin.z;
			const float originDelta = std::sqrt(
				deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
			if (std::isfinite(originDelta)) {
				auto storedBits =
					g_counters.deferredPairObserveOriginDeltaMaxBits.load(
						std::memory_order_relaxed);
				while (originDelta > std::bit_cast<float>(storedBits) &&
					!g_counters.deferredPairObserveOriginDeltaMaxBits.
						compare_exchange_weak(
							storedBits, std::bit_cast<std::uint32_t>(originDelta),
							std::memory_order_relaxed)) {
				}
			}
			if (MainViewVariantGuardPolicy::Mismatch(
					originDelta,
					MainViewVariantGuardPolicy::kOriginToleranceUnits)) {
				g_counters.deferredPairObserveOriginRejects.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			// The exact pane ViewData sample is authoritative for eye, basis, view,
			// projection and frustum.  The live NiCamera contributes only its exact
			// identity and non-frustum runtime metadata; it may be one frame ahead.
			sourceCamera.origin = rasterCamera.pose.origin;
			sourceCamera.forward = rasterCamera.pose.forward;
			sourceCamera.up = rasterCamera.pose.up;
			sourceCamera.right = rasterCamera.pose.right;
			sourceCamera.rasterView = rasterCamera.view;
			sourceCamera.rasterProjection = rasterCamera.projection;
			sourceCamera.rasterFrustum = rasterCamera.frustum;
			sourceCamera.rasterAuthoritative = true;
			if (unjitteredRasterReady) {
				sourceCamera.rasterViewProjectionUnjittered =
					unjitteredRasterCamera.viewProjection;
				sourceCamera.rasterProjectionUnjittered =
					unjitteredRasterCamera.projection;
				sourceCamera.rasterFrustumUnjittered =
					unjitteredRasterCamera.frustum;
				sourceCamera.unjitteredRasterAuthoritative = true;
			}
			RetainedMainTargetHandle retained{};
			if (!RetainMainTargetSEH(
					engine.device, engine.context, engine.targets.colorRTV,
					engine.targets.motionRTV, engine.targets.depthDSV, retained)) {
				g_counters.deferredPairObserveRetainRejects.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			const auto colorResource = reinterpret_cast<std::uintptr_t>(
				retained->colorResource);
			const auto depthResource = reinterpret_cast<std::uintptr_t>(
				retained->depthResource);
			const auto deviceIdentity =
				reinterpret_cast<std::uintptr_t>(retained->device);
			if (allocation.colorResource != colorResource ||
				allocation.depthResource != depthResource) {
				if (allocation.allocationGeneration ==
					(std::numeric_limits<std::uint64_t>::max)()) {
					g_counters.deferredPairObserveTargetIdentityRejects.fetch_add(
						1, std::memory_order_relaxed);
					g_deliveryFaulted.store(true, std::memory_order_release);
					return;
				}
				++allocation.allocationGeneration;
				allocation.colorResource = colorResource;
				allocation.depthResource = depthResource;
				allocation.retainedAllocation = retained;
			} else if (!SameRetainedColorDepthResources(
					allocation.retainedAllocation, retained)) {
				// Equal raw tokens while the prior pair is retained cannot identify a
				// different object. Treat any contradictory state as terminal.
				g_counters.deferredPairObserveTargetIdentityRejects.fetch_add(
					1, std::memory_order_relaxed);
				g_deliveryFaulted.store(true, std::memory_order_release);
				return;
			} else if (!SameRetainedMainTarget(
					allocation.retainedAllocation, retained)) {
				// Motion targets may legitimately ping-pong while the authoritative
				// color/depth allocation stays fixed.  Refresh the retained optional
				// identity without changing the color/depth allocation generation.
				allocation.retainedAllocation = retained;
			}
			const HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity mainTarget{
				allocation.allocationGeneration,
				colorResource, depthResource };
			if (!HandMirrorRuntimeBridgePolicy::IsValidPrivateTarget(mainTarget)) {
				g_counters.deferredPairObserveTargetIdentityRejects.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}

			DeferredCaptureMainView candidate{
				.viewProjection = engine.mainView.viewProjection,
				.origin = engine.mainView.origin,
				.mainTarget = mainTarget,
				.deviceIdentity = deviceIdentity,
				.contextIdentity = reinterpret_cast<std::uintptr_t>(
					retained->context),
				.viewport = engine.targets.viewport,
				.mainWorldFrame = g_mainWorldFrame,
				.graphicsFrame = engine.graphicsFrame,
				.observationCount = 1,
				.sourceCamera = sourceCamera,
				.retainedTarget = std::move(retained),
				.handPaneView = index == 1,
				.valid = true
			};
			const auto difference = [](const DeferredCaptureMainView& left,
				const DeferredCaptureMainView& right) noexcept {
				std::uint32_t mask = 0;
				if (left.mainWorldFrame != right.mainWorldFrame) mask |= 1u;
				if (left.graphicsFrame != right.graphicsFrame) mask |= 2u;
				if (left.mainTarget != right.mainTarget) mask |= 4u;
				if (left.deviceIdentity != right.deviceIdentity) mask |= 8u;
				if (left.contextIdentity != right.contextIdentity) mask |= 16u;
				if (!SameRetainedMainTargetCore(left.retainedTarget, right.retainedTarget)) mask |= 32u;
				if (!SameBytes(&left.viewProjection, &right.viewProjection, sizeof(left.viewProjection))) mask |= 64u;
				if (!SameBytes(&left.origin, &right.origin, sizeof(left.origin))) mask |= 128u;
				if (!SameViewportBits(left.viewport, right.viewport)) mask |= 256u;
				if (!SameFrozenSourceCameraValue(left.sourceCamera, right.sourceCamera)) mask |= 512u;
				if (left.handPaneView != right.handPaneView) mask |= 1024u;
				return mask;
			};
			const auto mask = observed.valid ? difference(observed, candidate) : 0u;
			if (mask != 0 && SeparateVRViews() &&
				g_cohortDriftLogCount.fetch_add(1, std::memory_order_relaxed) < 16) {
				try {
					logger::warn("[MOS][VR][view-cohort] domain={} frame={} graphics={} mismatch=0x{:04X} "
						"target(old/new)={}/{} viewport(old/new)={}x{}/{}x{} origin(old/new)=({},{},{})/({},{},{}); discard this domain for this frame",
						index == 1 ? "hand" : "world", g_mainWorldFrame, engine.graphicsFrame, mask,
						observed.mainTarget.allocationGeneration, candidate.mainTarget.allocationGeneration,
						observed.viewport.Width, observed.viewport.Height, candidate.viewport.Width, candidate.viewport.Height,
						observed.origin.x, observed.origin.y, observed.origin.z,
						candidate.origin.x, candidate.origin.y, candidate.origin.z);
				} catch (...) { }
			}
			const bool recoverableDrift = SeparateVRViews() && (mask & (1u | 2u | 8u | 16u | 1024u)) == 0;
			const auto observation = g_captureViews.Observe(index, std::move(candidate), recoverableDrift,
				[&difference](const auto& left, const auto& right) noexcept { return difference(left, right) == 0; });
			if (observation == HandMirrorVRRuntimePolicy::ViewObservation::kRejectedFrame ||
				observation == HandMirrorVRRuntimePolicy::ViewObservation::kTerminal) {
				g_counters.deferredPairObserveTargetIdentityRejects.fetch_add(1, std::memory_order_relaxed);
				if (observation == HandMirrorVRRuntimePolicy::ViewObservation::kTerminal) {
					g_deliveryFaulted.store(true, std::memory_order_release);
					g_counters.faults.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}
			g_counters.deferredPairSnapshots.fetch_add(
				1, std::memory_order_relaxed);
		}

		[[nodiscard]] float LengthSquared(const DirectX::XMFLOAT3& value) noexcept
		{
			return value.x * value.x + value.y * value.y + value.z * value.z;
		}

		[[nodiscard]] bool Normalize(DirectX::XMFLOAT3& value) noexcept
		{
			const float lengthSquared = LengthSquared(value);
			if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8F)
				return false;
			const float inverseLength = 1.0F / std::sqrt(lengthSquared);
			value.x *= inverseLength;
			value.y *= inverseLength;
			value.z *= inverseLength;
			return Finite(value);
		}

		template <class Plane>
		[[nodiscard]] float MirrorSignedDistance(
			const Plane& plane,
			const DirectX::XMFLOAT3& point) noexcept
		{
			return plane.normal.x * point.x + plane.normal.y * point.y +
				plane.normal.z * point.z - plane.distance;
		}

		template <class Plane>
		[[nodiscard]] DirectX::XMFLOAT3 MirrorReflectedPoint(
			const Plane& plane,
			const DirectX::XMFLOAT3& point) noexcept
		{
			const float distance = MirrorSignedDistance(plane, point);
			return {
				point.x - 2.0F * distance * plane.normal.x,
				point.y - 2.0F * distance * plane.normal.y,
				point.z - 2.0F * distance * plane.normal.z
			};
		}

		// Reason a pane transform failed to build; separates the lumped
		// panePlaneMismatch counter so the motion-correlated ~12% mismatch rate
		// observed on 2026-08-18 can be attributed to a specific comparison.
		enum class PaneTransformReject : std::uint8_t
		{
			kNone,
			kIdentity,
			kNormal,
			kOffPlane,
			kCameraSide,
			kExtent
		};

		[[nodiscard]] constexpr const char* PaneTransformRejectName(
			PaneTransformReject a_reject) noexcept
		{
			switch (a_reject) {
			case PaneTransformReject::kIdentity: return "identity";
			case PaneTransformReject::kNormal: return "normal-cosine";
			case PaneTransformReject::kOffPlane: return "off-plane";
			case PaneTransformReject::kCameraSide: return "camera-side";
			case PaneTransformReject::kExtent: return "extent";
			case PaneTransformReject::kNone:
			default:
				return "none";
			}
		}

		struct PaneTransformDiagnostics
		{
			float normalCosine{ 0.0f };
			float nodePlaneDistance{ 0.0f };
			float centerPlaneDistance{ 0.0f };
			float mainSide{ 0.0f };
			float reflectedSide{ 0.0f };
			float scale{ 0.0f };
		};

		[[nodiscard]] bool BuildPaneTransform(
			const MirrorPaneSurface::Snapshot& surface,
			const MirrorRecognition::ActiveMirror& mirror,
			const MirrorFramePublication::Snapshot& frame,
			const DirectX::XMFLOAT3& mainOrigin,
			MirrorPaneRenderer::PaneTransform& output,
			PaneTransformReject& reject,
			PaneTransformDiagnostics& diagnostics) noexcept
		{
			output = {};
			reject = PaneTransformReject::kNone;
			diagnostics = {};
			if (!surface.geometry || surface.formID == 0 || mirror.formID != surface.formID ||
				mirror.candidateGeneration == 0 ||
				mirror.candidateGeneration != frame.candidateGeneration ||
				mirror.plane.normalAxis != 0 || !mirror.plane.renderable) {
				reject = PaneTransformReject::kIdentity;
				return false;
			}

			if (surface.nif.geometry) {
				const auto& authored = surface.nif.plane;
				if (mirror.nifPaneToken != reinterpret_cast<std::uintptr_t>(surface.geometry.get()) ||
					mirror.nifSignature != surface.nif.signature || surface.nif.triangles.empty()) {
					reject = PaneTransformReject::kIdentity;
					return false;
				}
				const auto delta = MirrorNifContract::Add(authored.center,MirrorNifContract::Scale(mirror.plane.center,-1.0f));
				diagnostics.normalCosine = MirrorNifContract::Dot(authored.normal,mirror.plane.authoredNormal);
				diagnostics.centerPlaneDistance = MirrorSignedDistance(frame.capturePlane,authored.center);
				diagnostics.mainSide = MirrorSignedDistance(frame.capturePlane,mainOrigin);
				diagnostics.reflectedSide = MirrorSignedDistance(frame.capturePlane,frame.reflectedOrigin);
				if (MirrorNifContract::Dot(delta,delta) > 1.0e-6f ||
					diagnostics.normalCosine < kPaneNormalMinimumCosine ||
					std::abs(authored.halfWidth-mirror.worldHalfExtents[1]) > 1.0e-4f ||
					std::abs(authored.halfHeight-mirror.worldHalfExtents[2]) > 1.0e-4f ||
					MirrorNifContract::Dot(authored.tangent,mirror.worldAxes[1]) < 0.99999f ||
					MirrorNifContract::Dot(authored.bitangent,mirror.worldAxes[2]) < 0.99999f ||
					!MirrorNifContract::Finite(diagnostics.centerPlaneDistance) ||
					std::abs(diagnostics.centerPlaneDistance) > kPaneCenterTolerance) {
					reject = PaneTransformReject::kOffPlane;
					return false;
				}
				if (!MirrorNifContract::Finite(diagnostics.mainSide) || !MirrorNifContract::Finite(diagnostics.reflectedSide) ||
					diagnostics.mainSide <= kCameraSideEpsilon || diagnostics.reflectedSide >= -kCameraSideEpsilon) {
					reject = PaneTransformReject::kCameraSide;
					return false;
				}
				output.center = authored.center;
				output.tangentExtent = MirrorNifContract::Scale(authored.tangent,authored.halfWidth);
				output.bitangentExtent = MirrorNifContract::Scale(authored.bitangent,authored.halfHeight);
				output.owner = HandMirrorRuntimeBridgePolicy::MakeWallOwner({mirror.formID,mirror.candidateGeneration});
				return true;
			}
			if (mirror.nifPaneToken != 0) {
				reject = PaneTransformReject::kIdentity;
				return false;
			}
			const RE::NiPoint3 paneXRaw = surface.world.rotate.GetVectorX();
			const RE::NiPoint3 paneYRaw = surface.world.rotate.GetVectorY();
			const RE::NiPoint3 paneZRaw = surface.world.rotate.GetVectorZ();
			DirectX::XMFLOAT3 paneNormal{ paneXRaw.x, paneXRaw.y, paneXRaw.z };
			DirectX::XMFLOAT3 authoredNormal = mirror.plane.authoredNormal;
			if (!Normalize(paneNormal) || !Normalize(authoredNormal)) {
				reject = PaneTransformReject::kNormal;
				return false;
			}
			const float normalCosine = std::abs(
				paneNormal.x * authoredNormal.x + paneNormal.y * authoredNormal.y +
				paneNormal.z * authoredNormal.z);
			diagnostics.normalCosine = normalCosine;
			if (!std::isfinite(normalCosine) || normalCosine < kPaneNormalMinimumCosine) {
				reject = PaneTransformReject::kNormal;
				return false;
			}

			// The pane quad is centred on the OBND centre by authoring, but the pane
			// NODE's world translation is the reference origin — which is the mesh
			// BASE, not its centre. Requiring the two to coincide broke every
			// capture the moment the mesh moved to a base origin. Take the centre
			// from the authoritative OBND-derived plane (it is what the capture was
			// projected with) and require only that the pane node genuinely lies on
			// that plane, which is the property that actually matters.
			const DirectX::XMFLOAT3 paneNodeOrigin{
				surface.world.translate.x,
				surface.world.translate.y,
				surface.world.translate.z };
			const DirectX::XMFLOAT3 paneCenter = mirror.plane.center;
			diagnostics.nodePlaneDistance = MirrorSignedDistance(
				mirror.plane.plane, paneNodeOrigin);
			diagnostics.centerPlaneDistance = MirrorSignedDistance(
				mirror.plane.plane, paneCenter);
			if (!Finite(paneNodeOrigin) || !Finite(paneCenter) ||
				std::abs(diagnostics.nodePlaneDistance) > kPaneCenterTolerance ||
				std::abs(diagnostics.centerPlaneDistance) > kPaneCenterTolerance) {
				reject = PaneTransformReject::kOffPlane;
				return false;
			}

			const float mainSide = MirrorSignedDistance(
				mirror.plane.plane, mainOrigin);
			const float reflectedSide = MirrorSignedDistance(
				mirror.plane.plane, frame.reflectedOrigin);
			diagnostics.mainSide = mainSide;
			diagnostics.reflectedSide = reflectedSide;
			if (!std::isfinite(mainSide) || !std::isfinite(reflectedSide) ||
				mainSide <= kCameraSideEpsilon || reflectedSide >= -kCameraSideEpsilon) {
				reject = PaneTransformReject::kCameraSide;
				return false;
			}

			const float tangentExtent =
				kAuthoredPaneHalfTangent * surface.world.scale;
			const float bitangentExtent =
				kAuthoredPaneHalfBitangent * surface.world.scale;
			diagnostics.scale = surface.world.scale;
			if (!std::isfinite(tangentExtent) || tangentExtent <= 1.0e-4F ||
				!std::isfinite(bitangentExtent) || bitangentExtent <= 1.0e-4F) {
				reject = PaneTransformReject::kExtent;
				return false;
			}
			output.center = paneCenter;
			output.tangentExtent = {
				paneYRaw.x * tangentExtent, paneYRaw.y * tangentExtent,
				paneYRaw.z * tangentExtent };
			output.bitangentExtent = {
				paneZRaw.x * bitangentExtent, paneZRaw.y * bitangentExtent,
				paneZRaw.z * bitangentExtent };
			output.owner = HandMirrorRuntimeBridgePolicy::MakeWallOwner({
				mirror.formID, mirror.candidateGeneration });
			return Finite(output.tangentExtent) && Finite(output.bitangentExtent);
		}

		[[nodiscard]] MirrorPaneRenderer::PublishedFrame ToRendererFrame(
			const MirrorFramePublication::Snapshot& source) noexcept
		{
			MirrorPaneRenderer::PublishedFrame output{};
			output.colorSRV = source.colorSRV;
			// Placed mirrors retain their proven color-only presentation. Hand motion
			// is composed through the separately owned synchronous hand path.
			output.reflectedViewProjection = source.reflectedViewProjection;
			output.reflectedOrigin = source.reflectedOrigin;
			output.capturePlane = source.capturePlane;
			output.owner = HandMirrorRuntimeBridgePolicy::MakeWallOwner({
				source.candidateFormID, source.candidateGeneration });
			output.audience =
				HandMirrorRuntimeBridgePolicy::PublicationAudience::kWallPane;
			output.captureSequence = source.captureSequence;
			output.width = source.width;
			output.height = source.height;
			output.colorFormat = source.colorFormat;
			output.valid = source.valid;
			return output;
		}

		struct CandidateBoundDraw
		{
			ID3D11DeviceContext* context{ nullptr };
			const MirrorPaneRenderer::DrawRequest* request{ nullptr };
			MirrorPaneRenderer::DrawStatus status{
				MirrorPaneRenderer::DrawStatus::kCandidateMismatch };
			bool motionOutputEnabled{ false };
		};

		void DrawWhileCandidateCurrent(void* opaque) noexcept
		{
			auto* draw = static_cast<CandidateBoundDraw*>(opaque);
			if (draw && draw->request)
				draw->status = g_renderer->Draw(
					draw->context, *draw->request, &draw->motionOutputEnabled);
		}

		void RecordPaneResolveFailure(MirrorPaneSurface::ResolveStatus status) noexcept
		{
			g_counters.paneResolveFailures.fetch_add(1, std::memory_order_relaxed);
			g_lastPaneResolveStatus.store(status, std::memory_order_relaxed);
			switch (status) {
			case MirrorPaneSurface::ResolveStatus::kBaseUnavailable:
				g_counters.paneBaseFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneSurface::ResolveStatus::kInvalidFormID:
			case MirrorPaneSurface::ResolveStatus::kReferenceMissing:
			case MirrorPaneSurface::ResolveStatus::kReferenceNotOwned:
				g_counters.paneReferenceFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneSurface::ResolveStatus::kRootMissing:
				g_counters.paneRootFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneSurface::ResolveStatus::kPaneMissing:
			case MirrorPaneSurface::ResolveStatus::kPaneWrongType:
			case MirrorPaneSurface::ResolveStatus::kPaneNameMismatch:
			case MirrorPaneSurface::ResolveStatus::kPaneTagMissing:
			case MirrorPaneSurface::ResolveStatus::kPaneTagWrongType:
			case MirrorPaneSurface::ResolveStatus::kPaneOwnerMissing:
			case MirrorPaneSurface::ResolveStatus::kPaneOwnerMismatch:
				g_counters.paneIdentityFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneSurface::ResolveStatus::kTransformInvalid:
				g_counters.paneTransformFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneSurface::ResolveStatus::kException:
				g_counters.paneResolveExceptions.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneSurface::ResolveStatus::kResolved:
			default:
				break;
			}
		}

		void RecordEngineTargetFailure(EngineDeliveryStateStatus status) noexcept
		{
			g_counters.engineTargetFailures.fetch_add(1, std::memory_order_relaxed);
			g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
			g_lastEngineTargetStatus.store(status, std::memory_order_relaxed);
			switch (status) {
			case EngineDeliveryStateStatus::kGraphicsStateMissing:
			case EngineDeliveryStateStatus::kShadowStateMissing:
			case EngineDeliveryStateStatus::kRendererMissing:
				g_counters.engineStateMissing.fetch_add(1, std::memory_order_relaxed);
				break;
			case EngineDeliveryStateStatus::kColorIndexOutOfRange:
			case EngineDeliveryStateStatus::kDepthIndexOutOfRange:
			case EngineDeliveryStateStatus::kDepthSliceOutOfRange:
				g_counters.engineIndexFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case EngineDeliveryStateStatus::kDeviceMissing:
			case EngineDeliveryStateStatus::kContextMissing:
			case EngineDeliveryStateStatus::kColorTargetMissing:
			case EngineDeliveryStateStatus::kDepthTargetMissing:
				g_counters.enginePointerFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case EngineDeliveryStateStatus::kException:
				g_counters.engineStateExceptions.fetch_add(1, std::memory_order_relaxed);
				break;
			case EngineDeliveryStateStatus::kRefractionPassTarget:
				g_counters.engineRefractionPassSkips.fetch_add(1, std::memory_order_relaxed);
				break;
			case EngineDeliveryStateStatus::kReady:
			default:
				break;
			}
		}

		void RecordDrawRejection(MirrorPaneRenderer::DrawStatus status) noexcept
		{
			g_counters.rendererRejections.fetch_add(1, std::memory_order_relaxed);
			g_lastRendererStatus.store(status, std::memory_order_relaxed);
			switch (status) {
			case MirrorPaneRenderer::DrawStatus::kNoPublishedFrame:
				g_counters.noPublishedFrame.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kCandidateMismatch:
				g_counters.candidateMismatch.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kProjectionRejected:
				g_counters.projectionRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.projectionInvalid.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kProjectionMainViewNotVisible:
				g_counters.projectionRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.projectionMainViewNotVisible.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kProjectionReflectedUncovered:
				g_counters.projectionRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.projectionReflectedUncovered.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kNotInitialized:
			case MirrorPaneRenderer::DrawStatus::kInvalidArgument:
			case MirrorPaneRenderer::DrawStatus::kDeviceMismatch:
				g_counters.rendererSetupRejected.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kColorTargetMissing:
			case MirrorPaneRenderer::DrawStatus::kDepthTargetMissing:
			case MirrorPaneRenderer::DrawStatus::kViewportRejected:
			case MirrorPaneRenderer::DrawStatus::kTargetDeviceMismatch:
			case MirrorPaneRenderer::DrawStatus::kDepthTargetReadOnly:
			case MirrorPaneRenderer::DrawStatus::kTargetResourceRejected:
			case MirrorPaneRenderer::DrawStatus::kTargetShapeRejected:
				g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.rendererTargetInvalid.fetch_add(1, std::memory_order_relaxed);
				// The renderer refuses texture-array colour views with more than
				// one slice (stereo is delivered per eye, never per slice).  Log
				// that condition exactly once; the renderer owns no log sink.
				if (MirrorPaneRenderer::TakeArrayColorTargetRejectionNotice() &&
					!g_loggedArrayColorTargetReject.exchange(
						true, std::memory_order_relaxed)) {
					try {
						logger::warn(
							"[MirrorsOfSkyrim][PaneDelivery] pane draw refused: the bound colour view addresses more than one texture-array slice; "
							"stereo delivery draws once per eye into side-by-side or single-eye rectangles only");
					} catch (...) {
					}
				}
				break;
			case MirrorPaneRenderer::DrawStatus::kTargetAliasRejected:
			case MirrorPaneRenderer::DrawStatus::kTargetBindFlagsRejected:
				g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.rendererTargetInvalid.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kShaderResourceAliasBound:
				g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.shaderResourceAliasSkips.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kComputeUAVAliasBound:
				g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.computeUAVAliasSkips.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kOutputMergerUAVUnsafe:
				g_counters.targetRejected.fetch_add(1, std::memory_order_relaxed);
				g_counters.outputMergerUAVSkips.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kStreamOutputBound:
				g_counters.streamOutputSkips.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kConstantBufferMapFailed:
				g_counters.constantBufferMapFailures.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorPaneRenderer::DrawStatus::kDrawnPartialCoverage:
			case MirrorPaneRenderer::DrawStatus::kDrawn:
			default:
				break;
			}
		}

		[[nodiscard]] bool TryRenewProjectionContinuityLease(
			const MirrorRecognition::ActiveMirror& mirror,
			const DeliveryLocationSnapshot& location,
			MirrorPaneRenderer::DrawStatus projectionStatus,
			bool ageRejectProof) noexcept
		{
			if (!g_projectionContinuityLeaseEnabled.load(std::memory_order_acquire))
				return false;

			g_counters.projectionContinuityLeaseAttempts.fetch_add(
				1, std::memory_order_relaxed);
			if (ageRejectProof) {
				g_counters.ageRejectProofAttempts.fetch_add(
					1, std::memory_order_relaxed);
			}

			const bool projectionValid = projectionStatus !=
				MirrorPaneRenderer::DrawStatus::kProjectionRejected;
			const bool mainViewVisible = projectionValid && projectionStatus !=
				MirrorPaneRenderer::DrawStatus::kProjectionMainViewNotVisible;
			const auto locationStatus =
				MirrorInteriorEligibility::Classify(location.observation);
			const auto admission =
				MirrorSelectionPolicy::ClassifyValidatedVisibleContinuity({
					.featureEnabled = true,
					.exactIdentityCurrent = mirror.formID != 0 &&
						mirror.candidateGeneration != 0,
					.locationCurrent = !location.readFault &&
						MirrorInteriorEligibility::AllowsMirrorCapture(locationStatus),
					.projectionValid = projectionValid,
					.mainViewVisible = mainViewVisible });
			if (admission != MirrorSelectionPolicy::
					ValidatedVisibleContinuityAction::kAttemptNativeVisibilityProof) {
				g_counters.projectionContinuityLeaseRejects.fetch_add(
					1, std::memory_order_relaxed);
				if (ageRejectProof) {
					if (admission == MirrorSelectionPolicy::
							ValidatedVisibleContinuityAction::kMainViewNotVisible) {
						g_counters.ageRejectProofMainNotVisible.fetch_add(
							1, std::memory_order_relaxed);
					} else if (admission == MirrorSelectionPolicy::
							ValidatedVisibleContinuityAction::kProjectionInvalid) {
						g_counters.ageRejectProofProjectionInvalid.fetch_add(
							1, std::memory_order_relaxed);
					} else {
						g_counters.ageRejectProofIdentityLocationRejects.fetch_add(
							1, std::memory_order_relaxed);
					}
				}
				return false;
			}

			const auto result =
				MirrorRecognition::RecordValidatedVisibleContinuityOwner(
					mirror.formID, mirror.candidateGeneration,
					locationStatus, location.location);
			switch (result) {
			case MirrorRecognition::ValidatedVisibleContinuityResult::kRecorded:
				g_counters.projectionContinuityLeaseRenewals.fetch_add(
					1, std::memory_order_relaxed);
				if (ageRejectProof) {
					g_counters.ageRejectProofAccepted.fetch_add(
						1, std::memory_order_relaxed);
				}
				return true;
			case MirrorRecognition::ValidatedVisibleContinuityResult::
					kNativeVisibilityMissing:
				g_counters.projectionContinuityNativeVisibilityMissing.fetch_add(
					1, std::memory_order_relaxed);
				if (ageRejectProof) {
					g_counters.ageRejectProofNativeVisibilityMissing.fetch_add(
						1, std::memory_order_relaxed);
				}
				break;
			case MirrorRecognition::ValidatedVisibleContinuityResult::kException:
				g_counters.projectionContinuityExceptions.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case MirrorRecognition::ValidatedVisibleContinuityResult::
					kInvalidIdentity:
			case MirrorRecognition::ValidatedVisibleContinuityResult::
					kLocationRejected:
			default:
				if (ageRejectProof) {
					g_counters.ageRejectProofIdentityLocationRejects.fetch_add(
						1, std::memory_order_relaxed);
				}
				break;
			}
			g_counters.projectionContinuityLeaseRejects.fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}

		void RecordPaneTransformReject(
			PaneTransformReject a_reject,
			const PaneTransformDiagnostics& a_diagnostics,
			std::uint32_t a_formID) noexcept
		{
			g_counters.panePlaneMismatch.fetch_add(1, std::memory_order_relaxed);
			switch (a_reject) {
			case PaneTransformReject::kIdentity:
				g_counters.panePlaneIdentityRejects.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case PaneTransformReject::kNormal:
				g_counters.panePlaneNormalRejects.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case PaneTransformReject::kOffPlane:
				g_counters.panePlaneOffPlaneRejects.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case PaneTransformReject::kCameraSide:
				g_counters.panePlaneCameraSideRejects.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case PaneTransformReject::kExtent:
				g_counters.panePlaneExtentRejects.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case PaneTransformReject::kNone:
			default:
				break;
			}
			auto budget = g_paneTransformRejectLogBudget.load(std::memory_order_acquire);
			while (budget != 0) {
				if (g_paneTransformRejectLogBudget.compare_exchange_weak(
						budget, budget - 1, std::memory_order_acq_rel)) {
					try {
						logger::warn(
							"[MirrorsOfSkyrim][PaneDelivery] pane transform rejected ({}) mirror={:08X} "
							"normalCosine={:.6f} nodePlaneDistance={:.4f} "
							"centerPlaneDistance={:.4f} mainSide={:.4f} "
							"reflectedSide={:.4f} scale={:.4f}",
							PaneTransformRejectName(a_reject), a_formID,
							a_diagnostics.normalCosine,
							a_diagnostics.nodePlaneDistance,
							a_diagnostics.centerPlaneDistance,
							a_diagnostics.mainSide, a_diagnostics.reflectedSide,
							a_diagnostics.scale);
					} catch (...) {
						// Diagnostics must never unwind through delivery.
					}
					return;
				}
			}
		}

		void DeliverSnapshot(const MirrorFramePublication::Snapshot& published,
			std::uint64_t deliveryFrame)
		{
			try {
				const auto needed = published.slot + 1;
				if (g_motionHistories.size() < needed) g_motionHistories.resize(needed);
				if (g_previousMainViews.size() < needed) g_previousMainViews.resize(needed);
				if (g_deliveredIdentities.size() < needed) g_deliveredIdentities.resize(needed);
			} catch (...) {
				RestorePaneSuppression(published.candidateFormID);
				return;
			}
			auto& g_motionHistory = g_motionHistories[published.slot];
			auto& g_previousMainView = g_previousMainViews[published.slot];
			const MultiMirrorPolicy::Identity identity{ published.candidateFormID, published.candidateGeneration };
			if (g_deliveredIdentities[published.slot] != identity) {
				g_motionHistory = {};
				g_previousMainView = {};
				g_deliveredIdentities[published.slot] = identity;
			}
			const auto restoreThisPane = [&]() { return RestorePaneSuppression(published.candidateFormID); };
			if (g_deliveryFaulted.load(std::memory_order_acquire)) {
				restoreThisPane();
				return;
			}

			EngineDeliveryState engine{};
			const auto engineStatus = ReadEngineDeliveryStateSEH(engine);
			if (engineStatus == EngineDeliveryStateStatus::kRefractionPassTarget) {
				// Not a delivery failure: the frame's main-pass evidence stays intact
				// and the engine draws its own neutral refraction normals for the pane.
				g_counters.engineRefractionPassSkips.fetch_add(
					1, std::memory_order_relaxed);
				g_lastEngineTargetStatus.store(engineStatus, std::memory_order_relaxed);
				restoreThisPane();
				return;
			}
			if (engineStatus != EngineDeliveryStateStatus::kReady) {
				g_deferredCaptureMainView = {};
				if (engineStatus != EngineDeliveryStateStatus::kException)
					RecordEngineTargetFailure(engineStatus);
				restoreThisPane();
				return;
			}

			// Camera-variant guard (2026-08-19 third-person defect): before the
			// engine main-view sample is used, validate the shadow-state origin
			// against the WorldRoot NiCamera. A wrong-variant frame substitutes
			// the previous accepted view (self-consistent, at most one frame
			// late) or fails closed exactly like a projection rejection.
			bool currentRasterMainViewVerified = false;
			if (g_variantGuardRequested.load(std::memory_order_acquire)) {
				MainViewVariantGuardPolicy::Observation observation{};
				DirectX::XMFLOAT3 cameraOrigin{};
				observation.cameraAvailable = ReadMainCameraOriginSEH(cameraOrigin);
				if (observation.cameraAvailable) {
					const float deltaX = engine.mainView.origin.x - cameraOrigin.x;
					const float deltaY = engine.mainView.origin.y - cameraOrigin.y;
					const float deltaZ = engine.mainView.origin.z - cameraOrigin.z;
					observation.originDelta = std::sqrt(
						deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
					g_counters.variantChecks.fetch_add(1, std::memory_order_relaxed);
					if (std::isfinite(observation.originDelta)) {
						auto storedBits = g_counters.variantDeltaMaxBits.load(
							std::memory_order_relaxed);
						while (observation.originDelta >
								std::bit_cast<float>(storedBits) &&
							!g_counters.variantDeltaMaxBits.compare_exchange_weak(
								storedBits,
								std::bit_cast<std::uint32_t>(observation.originDelta),
								std::memory_order_relaxed)) {
						}
					}
				} else {
					g_counters.variantUnavailable.fetch_add(
						1, std::memory_order_relaxed);
				}
				// The guard measures eye 0 only (VR eye 1 sits ~2.3 units away,
				// far inside kOriginToleranceUnits).  A held view is usable only
				// when it carries the same eye count as the live frame.
				observation.heldViewValid = g_previousMainView.valid &&
					g_previousMainView.eyeCount == engine.eyeCount;
				observation.heldViewConsecutive =
					g_previousMainView.deliveryFrame + 1 == deliveryFrame;
				switch (MainViewVariantGuardPolicy::Classify(observation)) {
				case MainViewVariantGuardPolicy::Action::kUseHeldView:
					g_counters.variantMismatches.fetch_add(
						1, std::memory_order_relaxed);
					if (published.sourceMainWorldFrame == 0) {
						g_counters.variantStaleDraws.fetch_add(
							1, std::memory_order_relaxed);
						engine.eyeViews = g_previousMainView.eyeViews;
						engine.mainView = engine.eyeViews[0];
					}
					break;
				case MainViewVariantGuardPolicy::Action::kSkipDelivery:
					g_counters.variantMismatches.fetch_add(
						1, std::memory_order_relaxed);
					g_counters.variantSkips.fetch_add(1, std::memory_order_relaxed);
					restoreThisPane();
					return;
				case MainViewVariantGuardPolicy::Action::kAcceptLive:
					currentRasterMainViewVerified = true;
					break;
				case MainViewVariantGuardPolicy::Action::kAcceptUnverified:
				default:
					break;
				}
			}

			// Exact-main captures are produced only after their source main-world
			// invocation returns. Source view identity validates the capture and its
			// bounded age; it never places the pane on a later main target. The pane's
			// SV_Position/depth must use the current variant-verified raster view while
			// reflected UVs continue to use the published reflected view-projection.
			bool useDeferredSourceValidation = false;
			bool deferredAgeRejectPending = false;
			if (published.sourceMainWorldFrame != 0) {
				const auto expectedReflected = MirrorReflectedPoint(
					published.capturePlane, published.mainOrigin);
				const float deltaX =
					expectedReflected.x - published.reflectedOrigin.x;
				const float deltaY =
					expectedReflected.y - published.reflectedOrigin.y;
				const float deltaZ =
					expectedReflected.z - published.reflectedOrigin.z;
				const float originDelta = std::sqrt(
					deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
				if (std::isfinite(originDelta)) {
					auto storedBits = g_counters.deferredPairOriginDeltaMaxBits.load(
						std::memory_order_relaxed);
					while (originDelta > std::bit_cast<float>(storedBits) &&
						!g_counters.deferredPairOriginDeltaMaxBits.compare_exchange_weak(
							storedBits, std::bit_cast<std::uint32_t>(originDelta),
							std::memory_order_relaxed)) {
					}
				}

				const MainViewVariantGuardPolicy::DeferredPairObservation observation{
					published.mainViewValid,
					currentRasterMainViewVerified,
					published.sourceMainWorldFrame,
					g_mainWorldFrame,
					originDelta };
				// Keep each completed image drawable while the other mirrors receive
				// their capture turns. Exact reference, location and raster checks
				// still apply on every delivery.
				constexpr std::uint64_t kDeferredPairAgeFrames = 60;
				switch (MainViewVariantGuardPolicy::ClassifyDeferredPair(
					observation,
					MainViewVariantGuardPolicy::kDeferredPairOriginToleranceUnits,
					kDeferredPairAgeFrames)) {
				case MainViewVariantGuardPolicy::DeferredPairAction::kUseCurrentRasterView: {
					useDeferredSourceValidation = true;
					g_counters.deferredPairDraws.fetch_add(
						1, std::memory_order_relaxed);
					const auto age = MainViewVariantGuardPolicy::DeferredPairAgeFrames(
						published.sourceMainWorldFrame, g_mainWorldFrame, kDeferredPairAgeFrames);
					if (age > 1) {
						g_counters.deferredPairLaggedDraws.fetch_add(
							1, std::memory_order_relaxed);
					}
					auto maximumAge = g_counters.deferredPairAcceptedAgeMax.load(
						std::memory_order_relaxed);
					while (age > maximumAge &&
						!g_counters.deferredPairAcceptedAgeMax.compare_exchange_weak(
							maximumAge, age, std::memory_order_relaxed)) {
					}
					break;
				}
				case MainViewVariantGuardPolicy::DeferredPairAction::kSkipMissingPair:
					g_counters.deferredPairMissing.fetch_add(
						1, std::memory_order_relaxed);
					restoreThisPane();
					return;
				case MainViewVariantGuardPolicy::DeferredPairAction::kSkipCurrentRasterViewUnverified:
					g_counters.deferredPairCurrentViewUnverified.fetch_add(
						1, std::memory_order_relaxed);
					restoreThisPane();
					return;
				case MainViewVariantGuardPolicy::DeferredPairAction::kSkipAgeOutsideWindow:
					// The pair classifier checks age before origin; a returning image
					// must still pass that origin proof before any age-only grace.
					if (MainViewVariantGuardPolicy::Mismatch(originDelta,
						MainViewVariantGuardPolicy::kDeferredPairOriginToleranceUnits)) {
						g_counters.deferredPairOriginMismatch.fetch_add(1, std::memory_order_relaxed);
						restoreThisPane();
						return;
					}
					MirrorFleetRuntime::Expired({ published.candidateFormID, published.candidateGeneration },
						published.captureSequence);
					g_counters.deferredPairAgeRejected.fetch_add(
						1, std::memory_order_relaxed);
					if (g_projectionContinuityLeaseEnabled.load(std::memory_order_acquire) ||
						MirrorFleetPolicy::enabled.load(std::memory_order_relaxed)) {
						// Continue through exact candidate, location, pane and raster
						// coverage checks. Only the bounded fleet return lease below
						// can allow this older image to draw while capture resumes.
						useDeferredSourceValidation = true;
						deferredAgeRejectPending = true;
						break;
					}
					restoreThisPane();
					return;
				case MainViewVariantGuardPolicy::DeferredPairAction::kSkipOriginMismatch:
				default:
					g_counters.deferredPairOriginMismatch.fetch_add(
						1, std::memory_order_relaxed);
					restoreThisPane();
					return;
				}
			}

			MirrorRecognition::ActiveMirror activeMirror{};
			if (!(MirrorFramePublication::MultiMirrorEnabled() ?
				MirrorRecognition::TryGetMirror(published.candidateFormID, published.candidateGeneration,
					engine.mainView.origin, activeMirror) :
				MirrorRecognition::TryGetActiveMirror(engine.mainView.origin, activeMirror)) ||
				activeMirror.formID != published.candidateFormID ||
				activeMirror.candidateGeneration != published.candidateGeneration) {
				g_counters.candidateMismatch.fetch_add(1, std::memory_order_relaxed);
				restoreThisPane();
				return;
			}

			DeliveryLocationSnapshot earlyLocation{};
			if (!MirrorDeliveryLocationCurrent(activeMirror, earlyLocation)) {
				RejectLocationContainment(activeMirror, earlyLocation, "pre-resolve");
				return;
			}

			MirrorPaneSurface::Snapshot surface{};
			const auto paneStatus = MirrorPaneSurface::Resolve(
				published.candidateFormID, surface);
			if (paneStatus != MirrorPaneSurface::ResolveStatus::kResolved) {
				RecordPaneResolveFailure(paneStatus);
				restoreThisPane();
				return;
			}

			MirrorPaneRenderer::PaneTransform pane{};
			PaneTransformReject paneReject{ PaneTransformReject::kNone };
			PaneTransformDiagnostics paneDiagnostics{};
			if (!BuildPaneTransform(
					surface, activeMirror, published, engine.mainView.origin, pane,
					paneReject, paneDiagnostics)) {
				RecordPaneTransformReject(
					paneReject, paneDiagnostics, activeMirror.formID);
				restoreThisPane();
				return;
			}

			MirrorPaneRenderer::InitializationStatus initializationStatus{};
			constexpr auto samplingMode =
				MirrorPaneRenderer::SamplingMode::kFullMipChain;
			if (!g_renderer->Initialize(
					engine.device, &initializationStatus, samplingMode,
					g_coplanarFallbackReplacement.load(std::memory_order_relaxed))) {
				g_counters.rendererInitFailures.fetch_add(1, std::memory_order_relaxed);
				g_lastRendererInitStatus.store(
					initializationStatus, std::memory_order_relaxed);
				restoreThisPane();
				return;
			}

			// Staleness probe for the owner-reported moving ghosting (persists
			// with TAA off, so it is not history blending): if the published
			// frame's reflected origin is not the mirror image of its paired main
			// origin (or the live main origin for legacy publications), the pane is
			// sampling a mismatched camera pose. One frame of walking is roughly
			// 5-10 units, so a delta above one unit marks a stale frame.
			{
				const DirectX::XMFLOAT3 tangent = pane.tangentExtent;
				const DirectX::XMFLOAT3 bitangent = pane.bitangentExtent;
				DirectX::XMFLOAT3 normal{
					tangent.y * bitangent.z - tangent.z * bitangent.y,
					tangent.z * bitangent.x - tangent.x * bitangent.z,
					tangent.x * bitangent.y - tangent.y * bitangent.x };
				const float lengthSquared = normal.x * normal.x +
					normal.y * normal.y + normal.z * normal.z;
				if (std::isfinite(lengthSquared) && lengthSquared > 1.0e-12f) {
					const float inverseLength = 1.0f / std::sqrt(lengthSquared);
					normal.x *= inverseLength;
					normal.y *= inverseLength;
					normal.z *= inverseLength;
					const float planeDistance = normal.x * pane.center.x +
						normal.y * pane.center.y + normal.z * pane.center.z;
					const auto& stalenessMainOrigin = useDeferredSourceValidation ?
						published.mainOrigin : engine.mainView.origin;
					const float mainSideDistance =
						normal.x * stalenessMainOrigin.x +
						normal.y * stalenessMainOrigin.y +
						normal.z * stalenessMainOrigin.z - planeDistance;
					const DirectX::XMFLOAT3 expectedReflected{
						stalenessMainOrigin.x - 2.0f * mainSideDistance * normal.x,
						stalenessMainOrigin.y - 2.0f * mainSideDistance * normal.y,
						stalenessMainOrigin.z - 2.0f * mainSideDistance * normal.z };
					const float deltaX =
						expectedReflected.x - published.reflectedOrigin.x;
					const float deltaY =
						expectedReflected.y - published.reflectedOrigin.y;
					const float deltaZ =
						expectedReflected.z - published.reflectedOrigin.z;
					const float delta = std::sqrt(
						deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
					if (std::isfinite(delta)) {
						auto storedBits =
							g_counters.paneOriginDeltaMaxBits.load(
								std::memory_order_relaxed);
						while (delta > std::bit_cast<float>(storedBits) &&
							!g_counters.paneOriginDeltaMaxBits
								.compare_exchange_weak(
									storedBits, std::bit_cast<std::uint32_t>(delta),
									std::memory_order_relaxed)) {
						}
						if (delta > 1.0f)
							g_counters.paneStaleOriginFrames.fetch_add(
								1, std::memory_order_relaxed);
					}
				}
			}
			MirrorPaneRenderer::DrawRequest request{};
			request.frame = ToRendererFrame(published);
			request.pane = pane;
			request.customPaneTriangles = surface.DrawTriangles();
			// This view owns SV_Position and depth on the currently bound main target.
			// The tagged source view above is deliberately capture metadata only.
			// Eye 0 here; the per-eye loop below re-targets mainView and viewport.
			request.mainView = engine.mainView;
			// Retain the last verified views for the active variant guard fallback.
			g_previousMainView.eyeViews = engine.eyeViews;
			g_previousMainView.eyeCount = engine.eyeCount;
			g_previousMainView.deliveryFrame = deliveryFrame;
			g_previousMainView.valid = true;
			request.targets = engine.targets;
			// CS temporal reconstruction needs the reflected scene's velocity.
			// Its capture owns independent retained depth; the renderer validates
			// the engine motion target and preserves it on every alias/shape miss.
			if (!PeerDetection::CommunityShadersPresent())
				request.targets.motionRTV = nullptr;
			request.motionHistory = g_motionHistory;
			request.deliveryFrame = deliveryFrame;
			DeliveryLocationSnapshot drawLocation{};
			if (!MirrorDeliveryLocationCurrent(activeMirror, drawLocation)) {
				RejectLocationContainment(activeMirror, drawLocation, "pre-draw");
				return;
			}
			if (deferredAgeRejectPending) {
				const auto projectionStatus =
					MirrorPaneRenderer::ClassifyProjectionCoverage(
						request.pane, request.mainView, request.frame, request.customPaneTriangles);
				(void) TryRenewProjectionContinuityLease(
					activeMirror, drawLocation, projectionStatus, true);
				// Only a fully covered, exact current pane may bridge its first
				// returning frames. Identity/location/resource checks and the actual
				// renderer draw below remain mandatory; no partial image is stretched.
				if (projectionStatus != MirrorPaneRenderer::DrawStatus::kDrawn ||
					(!MirrorOcclusionRuntime::AllowRetainedImage(
						{ published.candidateFormID, published.candidateGeneration },g_mainWorldFrame) &&
					 !MirrorFleetRuntime::ResumeImage(
						{ published.candidateFormID, published.candidateGeneration },
						g_mainWorldFrame, published.sourceMainWorldFrame, published.captureSequence))) {
					restoreThisPane();
					return;
				}
			}
			// One pane draw per eye against the same frame and pane: each eye
			// supplies its own raster view (SV_Position/depth) and its own
			// viewport rectangle.  Flat runtimes run exactly one iteration.  The
			// authored pane is suppressed only when every eye draw succeeded.
			const std::uint32_t eyeCount =
				std::min(engine.eyeCount, kMaximumEyeCount);
			if (eyeCount == 0) {
				RecordDrawRejection(MirrorPaneRenderer::DrawStatus::kInvalidArgument);
				restoreThisPane();
				return;
			}
			MirrorPaneRenderer::DrawStatus status =
				MirrorPaneRenderer::DrawStatus::kDrawn;
			bool partialCoverage = false;
			bool motionOutputEnabled = false;
			for (std::uint32_t eye = 0; eye < eyeCount; ++eye) {
				request.mainView = engine.eyeViews[eye];
				request.targets.viewport = engine.eyeViewports[eye];
				CandidateBoundDraw draw{ engine.context, &request };
				if (!MirrorRecognition::RunWhileCandidateCurrent(
						activeMirror.formID, activeMirror.candidateGeneration,
						DrawWhileCandidateCurrent, &draw)) {
					g_counters.candidateMismatch.fetch_add(1, std::memory_order_relaxed);
					restoreThisPane();
					return;
				}
				if (eye == 0)
					motionOutputEnabled = draw.motionOutputEnabled;
				if (!MirrorPaneRenderer::IsSuccessfulDraw(draw.status)) {
					status = draw.status;
					if (eye > 0) {
						g_counters.stereoEyeDrawRejects.fetch_add(
							1, std::memory_order_relaxed);
					}
					break;
				}
				if (draw.status ==
					MirrorPaneRenderer::DrawStatus::kDrawnPartialCoverage) {
					partialCoverage = true;
				}
				if (eye > 0)
					g_counters.stereoEyeDraws.fetch_add(1, std::memory_order_relaxed);
			}
			// Later readers (motion history, diagnostics) see the eye-0 request.
			request.mainView = engine.eyeViews[0];
			request.targets.viewport = engine.eyeViewports[0];
			if (MirrorPaneRenderer::IsSuccessfulDraw(status) && partialCoverage)
				status = MirrorPaneRenderer::DrawStatus::kDrawnPartialCoverage;
			if (!MirrorPaneRenderer::IsSuccessfulDraw(status)) {
				RecordDrawRejection(status);
				// This is the sole non-draw continuity path. Candidate, pane identity,
				// plane, location, target, and pipeline checks have all passed, and the
				// renderer proved a positive-area main-visible pane. The authored pane
				// is still restored below; this observation never increments draw counts.
				if (status == MirrorPaneRenderer::DrawStatus::
						kProjectionReflectedUncovered) {
					(void) TryRenewProjectionContinuityLease(
						activeMirror, drawLocation, status, false);
				}
				restoreThisPane();
				return;
			}

			g_lastRendererStatus.store(status, std::memory_order_relaxed);
			g_counters.draws.fetch_add(1, std::memory_order_relaxed);
			g_counters.stateRestores.fetch_add(1, std::memory_order_relaxed);
			MirrorRecognition::RecordSuccessfulDeliveryOwner(
				activeMirror.formID, activeMirror.candidateGeneration);
			// Velocity-engagement telemetry, evaluated with the renderer's exact
			// eligibility predicate before the history advances: proves whether
			// the RT1 motion path is actually writing on drawn frames.
			if (!request.targets.motionRTV) {
				g_counters.motionTargetMissing.fetch_add(1, std::memory_order_relaxed);
			} else if (motionOutputEnabled) {
				g_counters.motionVelocityEligible.fetch_add(
					1, std::memory_order_relaxed);
			} else {
				g_counters.motionVelocityIneligible.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (request.targets.motionRTV && request.frame.depthSRV) {
				// Store the mainView the draw actually used (paired or live) so
				// velocity reprojection stays frame-consistent with the mapping.
				g_motionHistory.mainViewProjection = request.mainView.viewProjection;
				g_motionHistory.reflectedViewProjection = published.reflectedViewProjection;
				g_motionHistory.mainOrigin = request.mainView.origin;
				g_motionHistory.reflectedOrigin = published.reflectedOrigin;
				g_motionHistory.pane = pane;
				g_motionHistory.captureSequence = published.captureSequence;
				g_motionHistory.deliveryFrame = deliveryFrame;
				g_motionHistory.valid = true;
			}
			// This seam is strictly after the custom main-world pane draw and after
			// private capture cleanup.
#if defined(MOS_VERIFY_HARNESS)
			EvidenceFrameCapture::OnPaneDrawCompleted(
				engine.device, engine.context, published);
#endif
			if (partialCoverage) {
				g_counters.projectionPartialDraws.fetch_add(1, std::memory_order_relaxed);
			}
			SampleCrosshairTarget();
			if (!MirrorPaneRenderer::AllowsPaneSuppression(status)) {
				restoreThisPane();
			} else {
				const auto result = MirrorPaneSurface::Suppress(
					surface, published.candidateGeneration);
				if (result == MirrorPaneSurface::SuppressResult::kSuppressed)
					g_counters.paneSuppressions.fetch_add(1, std::memory_order_relaxed);
				else if (result == MirrorPaneSurface::SuppressResult::kCandidateInvalid)
					g_counters.candidateMismatch.fetch_add(1, std::memory_order_relaxed);
				else if (result == MirrorPaneSurface::SuppressResult::kFailed)
					restoreThisPane();
			}

			if (!g_loggedFirstDraw.exchange(true, std::memory_order_relaxed)) {
				logger::warn(
					"[MirrorsOfSkyrim][PaneDelivery] first custom mirror pane draw completed for mirror {:08X}, capture={} coverage={} suppression=true",
					published.candidateFormID, published.captureSequence,
					partialCoverage ? "partial" : "full");
			}
		}

		// The last published set, kept alive so a throttled frame can be redrawn
		// rather than dropped to the authored black pane. Snapshots own their SRVs
		// through ComPtr, so this retains the texture as well as the description.
		MirrorFramePublication::SnapshotSet g_retainedSnapshots{};
		std::uint64_t g_retainedSnapshotMicroseconds{ 0 };
		// Bounded in TIME, not frames.
		//
		// This was four frames, chosen against 30 Hz at 34 FPS. The owner then ran
		// the Markarth inn at 3.78 ms a frame -- 264 FPS -- where a 30 Hz capture
		// arrives every *nine* frames, so the retained frame expired in the middle
		// of every gap and the pane fell back to its authored surface for most of
		// it: `noFrame=11829` against `retained(redraws/expiries)=68/17`. The
		// reflection simply stopped showing, and the faster the machine the worse
		// it got, which is the wrong way round.
		//
		// A capture interval is a duration, so the bound is one too: long enough to
		// bridge the slowest refresh the MCM offers (15 Hz = 67 ms) with margin,
		// short enough that a mirror which has genuinely stopped publishing --
		// stored, invalidated, out of range -- still reverts within a blink.
		constexpr std::uint64_t kRetainedMaxMicroseconds = 150'000;

		void ForgetRetainedSnapshots() noexcept
		{
			g_retainedSnapshots.clear();
			g_retainedSnapshotMicroseconds = 0;
		}

		void DeliverCurrentFrame()
		{
			const auto frame = g_deliveryFrameIndex.fetch_add(1, std::memory_order_relaxed) + 1;
			if (!RestorePaneSuppression() || g_deliveryFaulted.load(std::memory_order_acquire)) {
				ForgetRetainedSnapshots();
				return;
			}
			if (!MirrorPerformance::RenderingEnabled()) {
				ForgetRetainedSnapshots();
				return;
			}
			MirrorFramePublication::SnapshotSet snapshots{};
			if (MirrorFramePublication::GetSnapshots(snapshots) == 0) {
				// No capture published this frame -- at a 30 Hz refresh against a
				// higher frame rate that is ordinary. Redraw the frame we already
				// have instead of letting the authored black pane show through,
				// which is the 8.4% black flash behind the HUD text the owner
				// reported on 2026-09-15.
				g_counters.noPublishedFrame.fetch_add(1, std::memory_order_relaxed);
				const auto now = MirrorFleetRuntime::Now();
				if (g_retainedSnapshots.empty() ||
					now <= g_retainedSnapshotMicroseconds ||
					now - g_retainedSnapshotMicroseconds > kRetainedMaxMicroseconds) {
					if (!g_retainedSnapshots.empty()) {
						g_counters.retainedFrameExpiries.fetch_add(
							1, std::memory_order_relaxed);
						ForgetRetainedSnapshots();
					}
					return;
				}
				snapshots = g_retainedSnapshots;
				g_counters.retainedFrameRedraws.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_retainedSnapshots = snapshots;
				g_retainedSnapshotMicroseconds = MirrorFleetRuntime::Now();
			}
			for (const auto& snapshot : snapshots) {
				if (snapshot.valid && snapshot.slot < snapshots.size())
					DeliverSnapshot(snapshot, frame);
				if (g_deliveryFaulted.load(std::memory_order_acquire)) {
					RestorePaneSuppression();
					return;
				}
			}
		}

		[[nodiscard]] int RecordDeliveryException(unsigned long code) noexcept
		{
			g_lastException.store(code, std::memory_order_relaxed);
			g_deliveryFaulted.store(true, std::memory_order_release);
			g_counters.faults.fetch_add(1, std::memory_order_relaxed);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[nodiscard]] bool DeliverCppSafe() noexcept
		{
			try {
				DeliverCurrentFrame();
				return true;
			} catch (...) {
				(void) RecordDeliveryException(0xE06D7363UL);
				return false;
			}
		}

		[[nodiscard]] bool DeliverSEHSafe() noexcept
		{
			bool completed = false;
			__try {
				completed = DeliverCppSafe();
			} __except (RecordDeliveryException(GetExceptionCode())) {
				completed = false;
			}
			return completed;
		}

		void MaybeLogPeriodicDiagnostics() noexcept
		{
			try {
				const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count();
				auto previous = g_lastPeriodicLogMilliseconds.load(std::memory_order_relaxed);
				if (now - previous <
					std::chrono::duration_cast<std::chrono::milliseconds>(kDiagnosticPeriod).count())
					return;
				if (!g_lastPeriodicLogMilliseconds.compare_exchange_strong(
						previous, now, std::memory_order_relaxed))
					return;
				LogDiagnostics("periodic");
			} catch (...) {
				// Diagnostics must never unwind through Skyrim's render hook.
			}
		}

		class CallbackScope
		{
		public:
			CallbackScope() noexcept { g_inCallback = true; }
			~CallbackScope() { g_inCallback = false; }
		};

		struct QueryHook
		{
			static std::uintptr_t thunk(
				void* batchRenderer,
				std::uint32_t lowerPass,
				std::uint32_t upperPass)
			{
				const std::uintptr_t nativeResult = func(batchRenderer, lowerPass, upperPass);
				if (!g_enabled.load(std::memory_order_acquire))
					return nativeResult;
				g_counters.allQueryCalls.fetch_add(1, std::memory_order_relaxed);
				if (g_mainWorldDepth == 0) {
					if (g_privatePassDepth > 0)
						g_counters.privatePassSkips.fetch_add(1, std::memory_order_relaxed);
					else
						g_counters.outsidePassSkips.fetch_add(1, std::memory_order_relaxed);
					return nativeResult;
				}
				if (g_inCallback) {
					g_counters.reentries.fetch_add(1, std::memory_order_relaxed);
					return nativeResult;
				}

				CallbackScope callbackScope{};
				g_counters.mainWorldHits.fetch_add(1, std::memory_order_relaxed);
				// Sample the source raster view even when no mirror publication exists
				// yet.  The exact-main producer is deferred until this main-world call
				// returns, so waiting for a prior publication deadlocks the first valid
				// capture/view pair.
				EngineDeliveryState deferredPairState{};
				const auto deferredPairStatus =
					ReadEngineDeliveryStateSEH(deferredPairState);
				if (deferredPairStatus == EngineDeliveryStateStatus::kReady) {
					const auto previousSnapshots = g_counters.deferredPairSnapshots.load(std::memory_order_relaxed);
					ObserveDeferredCaptureMainView(deferredPairState);
					if (g_counters.deferredPairSnapshots.load(std::memory_order_relaxed) != previousSnapshots)
						MirrorOcclusionRuntime::ObserveOpaqueMainDepth(g_deferredCaptureMainView);
				} else {
					g_deferredCaptureMainView = {};
					if (deferredPairStatus == EngineDeliveryStateStatus::kException)
						return nativeResult;
				}
#if defined(MOS_VERIFY_HARNESS)
				EvidenceFrameCapture::OnMainWorldFrame();
#endif
				if (g_drawEnabled.load(std::memory_order_acquire) && !DeliverSEHSafe()) {
					MirrorFramePublication::Invalidate("delivery-fault");
					RestorePaneSuppression();
					logger::critical(
						"[MirrorsOfSkyrim][PaneDelivery] delivery fault-latched (SEH=0x{:08X}); custom draws disabled for this process",
						g_lastException.load(std::memory_order_relaxed));
				}
				MaybeLogPeriodicDiagnostics();
				return nativeResult;
			}

			static inline REL::Relocation<decltype(thunk)> func{};
		};

		[[nodiscard]] bool DeliveryHookStillOwned() noexcept
		{
			const std::uintptr_t callSite = g_hookCallSite.load(std::memory_order_acquire);
			const std::uintptr_t installedTarget =
				g_installedHookBranchTarget.load(std::memory_order_acquire);
			std::uintptr_t currentTarget = 0;
			const auto thunkAddress = std::bit_cast<std::uintptr_t>(&QueryHook::thunk);
			return callSite != 0 && installedTarget != 0 &&
				ReadCallTargetSEH(callSite, currentTarget) && currentTarget == installedTarget &&
				ValidateFiveByteBranchStubSEH(installedTarget, thunkAddress);
		}
	}

	void OnInputLoaded(bool secondViewHooksReady)
	{
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		if (!IsSupportedRuntime()) {
			logger::info(
				"[MirrorsOfSkyrim][PaneDelivery] unavailable on this runtime");
			return;
		}

		const auto prepared = MirrorActivation::GlobalLifecycle().Prepared();
		const auto hookRequest = prepared && !prepared->IsConflicted() ?
			prepared->Masks().HookRequest() : 0;
		const bool mirrorDeliveryRequested = MirrorActivation::Contains(
			hookRequest, MirrorActivation::Feature::kMirrorPaneDelivery);
		const bool mirrorDrawRequested = MirrorActivation::Contains(
			hookRequest, MirrorActivation::Feature::kMirrorPaneDraw);
		const bool handSeamRequested = HandMirrorReflectionRuntime::Requested();
		if (!mirrorDeliveryRequested && !handSeamRequested) {
			logger::info(
				"[MirrorsOfSkyrim][PaneDelivery] no mirror owner requested the shared seam");
			return;
		}
		if (!secondViewHooksReady) {
			logger::warn(
				"[MirrorsOfSkyrim][PaneDelivery] shared scene hooks are not ready; pane seam remains disabled");
			return;
		}

		const bool exactSE1597 = REL::Module::IsSE() &&
			REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 };
		const bool exactVR1415 = SupportedRuntimePolicy::IsExactVRRuntime();
		// V177: owner binary proves slot 7 is R16G16_FLOAT and TAA consumes its
		// SRV at t2 (docs/HAND_MIRROR_V177_STABILITY.md). Other AE versions retain
		// their previous gate. Resource shape/device/alias checks remain mandatory.
		const bool exactLiveAE17104 = SupportedRuntimePolicy::IsExactAE17104Runtime() &&
			HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled();
		g_motionRequested.store(
			(exactSE1597 || exactVR1415 || exactLiveAE17104) && handSeamRequested,
			std::memory_order_release);
		g_variantGuardRequested.store(false, std::memory_order_release);
		g_projectionContinuityLeaseRequested.store(
			false, std::memory_order_release);
		g_projectionContinuityLeaseEnabled.store(
			false, std::memory_order_release);
		const bool mirrorDrawReady = mirrorDeliveryRequested &&
			mirrorDrawRequested && MirrorCameraOverride::HookReady();

		// FinishAccumulating: 99938 (VR CSV 0x13093A0).  The query call sits at
		// SE +0x3C5 / AE +0x3B4 / VR +0x3D2; the VR prefix differs only in the
		// accumulator's batchRenderer offset (0x158 vs 0x130).  Skyrim 1.7.104 was
		// verified to keep the AE +0x3B4 offset and the AE prefix/suffix bytes, so
		// the AE branch of REL::Relocate is correct for it as well.
		const std::uintptr_t functionBase =
			REL::Relocation<std::uintptr_t>{ RELOCATION_ID(99938, 106583) }.address();
		const std::ptrdiff_t queryCallOffset = exactVR1415 ?
			MirrorPaneDeliverySignature::kVRCallOffsetFromFinishAccumulating :
			REL::Relocate(
				MirrorPaneDeliverySignature::kSECallOffsetFromFinishAccumulating,
				MirrorPaneDeliverySignature::kAECallOffsetFromFinishAccumulating);
		const std::uintptr_t callSite =
			functionBase + static_cast<std::uintptr_t>(queryCallOffset);
		std::array<std::uint8_t, MirrorPaneDeliverySignature::kWindowSize> window{};
		if (!CopyWindowSEH(
				callSite - MirrorPaneDeliverySignature::kCallOffset,
				window.data(), window.size()) ||
			!MirrorPaneDeliverySignature::MatchesFor(exactVR1415, window)) {
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] refused hook: query signature mismatch at 0x{:X}",
				callSite);
			return;
		}

		std::uintptr_t currentTarget = 0;
		if (!ReadCallTargetSEH(callSite, currentTarget) ||
			!IsExecutableAddress(reinterpret_cast<const void*>(currentTarget))) {
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] refused hook: query target is unavailable at 0x{:X}",
				callSite);
			return;
		}

		QueryHook::func = currentTarget;
		std::uintptr_t chainedTarget = 0;
		try {
			chainedTarget =
				SKSE::GetTrampoline().write_call<5>(callSite, QueryHook::thunk);
		} catch (...) {
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] refused hook: trampoline installation threw");
			return;
		}
		if (!chainedTarget ||
			!IsExecutableAddress(reinterpret_cast<const void*>(chainedTarget))) {
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] refused hook: chained target is unavailable");
			return;
		}
		if (chainedTarget != currentTarget) {
			logger::warn(
				"[MirrorsOfSkyrim][PaneDelivery] query target changed during installation; chaining the returned target");
		}
		QueryHook::func = chainedTarget;

		std::uintptr_t installedBranchTarget = 0;
		const auto thunkAddress =
			std::bit_cast<std::uintptr_t>(&QueryHook::thunk);
		if (!ReadCallTargetSEH(callSite, installedBranchTarget) ||
			installedBranchTarget == currentTarget ||
			!ValidateFiveByteBranchStubSEH(
				installedBranchTarget, thunkAddress)) {
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] refused hook: installed branch ownership could not be proven");
			return;
		}

		g_hookCallSite.store(callSite, std::memory_order_release);
		g_installedHookBranchTarget.store(
			installedBranchTarget, std::memory_order_release);
		g_hookInstalled.store(true, std::memory_order_release);
		logger::info(
			"[MirrorsOfSkyrim][PaneDelivery] dormant seam installed; mirrorDrawReady={} handSeamRequested={}",
			mirrorDrawReady, handSeamRequested);
	}

	void OnDataLoaded()
	{
		// Read once before any placed-mirror draw; never probe the filesystem in
		// the render loop. SE and AE share this renderer and conventional depth
		// convention; both need the same tolerance against the native black pane.
		const auto version = REL::Module::get().version();
		const bool supportedFlat =
			(REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
			(REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version));
		const bool coplanarReplacement = supportedFlat &&
			HandMirrorSafetySettings::EmptyMarker(
				L"Data\\MirrorsOfSkyrim_CoplanarPaneDepth.enable");
		g_coplanarFallbackReplacement.store(coplanarReplacement, std::memory_order_relaxed);
		logger::info("[MOS][PaneDepth] coplanar fallback replacement enabled={} bias={} slopeBias={} (default-off SE/AE correction)",
			coplanarReplacement, coplanarReplacement ? -4 : 0,
			coplanarReplacement ? -2.0f / static_cast<float>(1u << D3D11_SUBPIXEL_FRACTIONAL_BIT_COUNT) : 0.0f);
		if (!g_hookInstalled.load(std::memory_order_acquire))
			return;
		if (!DeliveryHookStillOwned()) {
			g_enabled.store(false, std::memory_order_release);
			g_drawEnabled.store(false, std::memory_order_release);
			g_projectionContinuityLeaseEnabled.store(
				false, std::memory_order_release);
			g_hookInstalled.store(false, std::memory_order_release);
			g_sharedSeamReady.store(false, std::memory_order_release);
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] query hook ownership changed after installation; delivery remains disabled");
			return;
		}

		g_sharedSeamReady.store(true, std::memory_order_release);
		const bool handSeam = HandMirrorReflectionRuntime::Requested() &&
			SecondView::HandCaptureDependenciesReady();
		g_enabled.store(handSeam, std::memory_order_release);
		g_drawEnabled.store(false, std::memory_order_release);
		g_projectionContinuityLeaseEnabled.store(
			false, std::memory_order_release);
		if (handSeam) {
			logger::info(
				"[MirrorsOfSkyrim][PaneDelivery] main-view seam armed for hand mirror delivery; placed-mirror draw awaits activation");
		} else {
			logger::info(
				"[MirrorsOfSkyrim][PaneDelivery] seam ownership validated; placed-mirror draw awaits activation");
		}
	}

	void OnMirrorActivationCommitted() noexcept
	{
		const auto committed = MirrorActivation::GlobalLifecycle().Committed();
		if (!committed || committed->IsConflicted())
			return;
		const auto runtimeEnable = committed->Masks().RuntimeEnable();
		const bool mirrorActive = committed->Masks().MirrorsOfSkyrimActive();
		const bool mirrorDelivery = MirrorActivation::Contains(
			runtimeEnable, MirrorActivation::Feature::kMirrorPaneDelivery);
		const bool mirrorDraw = MirrorActivation::Contains(
			runtimeEnable, MirrorActivation::Feature::kMirrorPaneDraw);
		if (!mirrorDelivery)
			return;
		if (!SharedSeamReady()) {
			logger::critical(
				"[MirrorsOfSkyrim][PaneDelivery] activation refused: validated seam is unavailable");
			return;
		}

		g_enabled.store(true, std::memory_order_release);
		if (mirrorDraw && MirrorCameraOverride::IsEnabled()) {
			MirrorPaneSurface::OnDataLoaded();
			g_drawEnabled.store(true, std::memory_order_release);
		}
		if (mirrorActive && IsProjectionContinuityAcceptedRuntime() &&
			MirrorActivation::kMirrorCorrectnessProfile.projectionContinuityLease) {
			g_projectionContinuityLeaseRequested.store(
				true, std::memory_order_release);
		}
		g_projectionContinuityLeaseEnabled.store(
			g_drawEnabled.load(std::memory_order_acquire) &&
				g_projectionContinuityLeaseRequested.load(
					std::memory_order_acquire),
			std::memory_order_release);
		if (mirrorActive &&
			MirrorActivation::kMirrorCorrectnessProfile.cameraVariantGuard) {
			g_variantGuardRequested.store(true, std::memory_order_release);
		}
		logger::info(
			"[MirrorsOfSkyrim][PaneDelivery] activation committed (draw={} variantGuard={} taggedPair={} continuity={})",
			g_drawEnabled.load(std::memory_order_acquire),
			g_variantGuardRequested.load(std::memory_order_acquire),
			mirrorActive &&
				MirrorActivation::kMirrorCorrectnessProfile.taggedDeferredMainViewPair,
			g_projectionContinuityLeaseEnabled.load(std::memory_order_acquire));
	}

	void OnGameLoaded() noexcept
	{
		RestorePaneSuppression();
		g_motionHistories = {};
		g_previousMainViews = {};
		g_deliveredIdentities = {};
		// Drop only retained D3D evidence, never reset the nonwrapping allocation
		// generation.  The custom shared owner guards every native Release.
		g_captureViews.Reset();
		g_handMainTargetIdentity.retainedAllocation.reset();
		g_handMainTargetIdentity.colorResource = 0;
		g_handMainTargetIdentity.depthResource = 0;
		g_mainTargetIdentity.retainedAllocation.reset();
		g_mainTargetIdentity.colorResource = 0;
		g_mainTargetIdentity.depthResource = 0;
	}

	void OnCandidateInvalidated() noexcept
	{
		RestorePaneSuppression();
	}

	void OnCandidateInvalidated(std::uint32_t formID) noexcept
	{
		RestorePaneSuppression(formID);
	}

	void OnFrameInvalidated() noexcept
	{
		RestorePaneSuppression();
	}

	void RestoreFallbackBeforeMainWorld() noexcept
	{
		RestorePaneSuppression();
	}

	MainWorldScope::MainWorldScope() noexcept
	{
		if (g_mainWorldDepth == 0) {
			g_mainWorldFrame = MainViewVariantGuardPolicy::NextNonZeroFrame(
				g_mainWorldFrame);
			g_captureViews.Reset(g_mainWorldFrame);
		}
		if (g_mainWorldDepth != (std::numeric_limits<std::uint32_t>::max)()) {
			++g_mainWorldDepth;
			active = true;
		}
	}

	MainWorldScope::~MainWorldScope()
	{
		if (active && g_mainWorldDepth != 0)
			--g_mainWorldDepth;
	}

	PrivatePassScope::PrivatePassScope() noexcept
	{
		if (g_privatePassDepth != (std::numeric_limits<std::uint32_t>::max)()) {
			++g_privatePassDepth;
			active = true;
		}
	}

	PrivatePassScope::~PrivatePassScope()
	{
		(void) Close();
	}

	bool PrivatePassScope::Close() noexcept
	{
		if (closed)
			return closeSucceeded;
		closed = true;
		if (!active)
			return false;
		if (g_privatePassDepth == 0) {
			active = false;
			return false;
		}
		--g_privatePassDepth;
		active = false;
		if (ShouldRestorePaneAfterPrivatePassClose(g_privatePassDepth)) {
			// Full-coverage suppression exists only to keep the authored pane out of
			// the immediately following private capture.  Restore it before the next
			// main frame so a later partial projective draw always has its fallback.
			if (!RestorePaneSuppression())
				return false;
		}
		closeSucceeded = true;
		return closeSucceeded;
	}

	bool IsEnabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire);
	}

	bool SharedSeamReady() noexcept
	{
		return g_sharedSeamReady.load(std::memory_order_acquire);
	}

	bool Faulted() noexcept
	{
		return g_deliveryFaulted.load(std::memory_order_acquire);
	}

	namespace
	{
		// Standing mirrors recently drawn nested into another capture; read by the
		// capture-demand loop so an off-screen mirror seen through the hand mirror
		// keeps refreshing. Render thread only (both writers and the reader).
		struct NestedDrawRecord { std::uint32_t formID{ 0 }; std::uint64_t microseconds{ 0 }; };
		std::array<NestedDrawRecord, 16> g_nestedDrawRecords{};
		inline constexpr std::uint64_t kNestedDrawDemandMicroseconds = 250'000;
		void RecordNestedDraw(std::uint32_t formID) noexcept
		{
			const auto now = MirrorFleetRuntime::Now();
			NestedDrawRecord* slot = nullptr;
			for (auto& record : g_nestedDrawRecords) {
				if (record.formID == formID) { slot = &record; break; }
				if (!slot && (record.formID == 0 || now - record.microseconds > kNestedDrawDemandMicroseconds)) slot = &record;
			}
			if (slot) *slot = { formID, now };
		}

		bool DrawResidentPanesIntoPrivateViewBody(
			ID3D11DeviceContext* context,
			const MirrorPaneRenderer::MainView& view,
			const MirrorPaneRenderer::DrawTargets& targets,
			std::uint32_t excludeFormID,
			PrivateViewPaneDrawResult& result)
		{
			result = {};
			if (!context || !g_enabled.load(std::memory_order_acquire) ||
				g_deliveryFaulted.load(std::memory_order_acquire) || !g_renderer->Ready()) {
				// A normal readiness miss is a completed no-op, not an SEH fault.
				result.lastStatus = MirrorPaneRenderer::DrawStatus::kNotInitialized;
				return true;
			}
			MirrorFramePublication::SnapshotSet snapshots{};
			if (MirrorFramePublication::GetSnapshots(snapshots) == 0)
				return true;
			for (const auto& published : snapshots) {
				if (!published.valid || !published.colorSRV ||
					published.candidateFormID == 0 ||
					published.candidateFormID == excludeFormID)
					continue;
				++result.attempted;
				MirrorRecognition::ActiveMirror activeMirror{};
				if (!(MirrorFramePublication::MultiMirrorEnabled() ?
						MirrorRecognition::TryGetMirror(published.candidateFormID,
							published.candidateGeneration, view.origin, activeMirror) :
						MirrorRecognition::TryGetActiveMirror(view.origin, activeMirror)) ||
					activeMirror.formID != published.candidateFormID ||
					activeMirror.candidateGeneration != published.candidateGeneration) {
					result.lastStatus = MirrorPaneRenderer::DrawStatus::kCandidateMismatch;
					continue;
				}
				MirrorPaneSurface::Snapshot surface{};
				if (MirrorPaneSurface::Resolve(published.candidateFormID, surface) !=
					MirrorPaneSurface::ResolveStatus::kResolved) {
					result.lastStatus = MirrorPaneRenderer::DrawStatus::kInvalidArgument;
					continue;
				}
				MirrorPaneRenderer::PaneTransform pane{};
				PaneTransformReject paneReject{ PaneTransformReject::kNone };
				PaneTransformDiagnostics paneDiagnostics{};
				// The private reflected eye stands in for the main origin: the same
				// front-side and on-plane checks apply to a pane seen from a capture.
				if (!BuildPaneTransform(surface, activeMirror, published, view.origin,
						pane, paneReject, paneDiagnostics)) {
					result.lastStatus = MirrorPaneRenderer::DrawStatus::kProjectionRejected;
					continue;
				}
				MirrorPaneRenderer::DrawRequest request{};
				request.frame = ToRendererFrame(published);
				request.pane = pane;
				request.customPaneTriangles = surface.DrawTriangles();
				request.mainView = view;
				request.targets = targets;
				request.targets.motionRTV = nullptr;
				request.motionMode = MirrorPaneRenderer::MotionMode::kRejectHistory;
				request.deliveryFrame = 0;
				CandidateBoundDraw draw{ context, &request };
				if (!MirrorRecognition::RunWhileCandidateCurrent(
						activeMirror.formID, activeMirror.candidateGeneration,
						DrawWhileCandidateCurrent, &draw)) {
					result.lastStatus = MirrorPaneRenderer::DrawStatus::kCandidateMismatch;
					continue;
				}
				result.lastStatus = draw.status;
				if (MirrorPaneRenderer::IsSuccessfulDraw(draw.status)) {
					++result.drawn;
					RecordNestedDraw(published.candidateFormID);
				}
			}
			return true;
		}
	}

	unsigned long LastDeliveryExceptionCode() noexcept
	{
		return g_lastException.load(std::memory_order_relaxed);
	}

	bool NestedDrawDemand(std::uint32_t formID, std::uint64_t nowMicroseconds) noexcept
	{
		for (const auto& record : g_nestedDrawRecords)
			if (record.formID == formID && nowMicroseconds >= record.microseconds &&
				nowMicroseconds - record.microseconds <= kNestedDrawDemandMicroseconds)
				return true;
		return false;
	}

	bool DrawResidentPanesIntoPrivateView(
		ID3D11DeviceContext* context,
		const MirrorPaneRenderer::MainView& view,
		const MirrorPaneRenderer::DrawTargets& targets,
		std::uint32_t excludeFormID,
		PrivateViewPaneDrawResult& result) noexcept
	{
		__try {
			return DrawResidentPanesIntoPrivateViewBody(
				context, view, targets, excludeFormID, result);
		} __except (RecordDeliveryException(GetExceptionCode())) {
			return false;
		}
	}

	bool MirrorDrawReady() noexcept
	{
		return g_drawEnabled.load(std::memory_order_acquire);
	}

	bool IsInsideMainWorld() noexcept
	{
		return g_mainWorldDepth > 0 && g_privatePassDepth == 0;
	}

	std::uint64_t UpcomingMainWorldFrame() noexcept
	{
		if (g_mainWorldDepth != 0)
			return 0;
		return MainViewVariantGuardPolicy::NextNonZeroFrame(g_mainWorldFrame);
	}

	bool TryGetDeferredCaptureMainView(
		DeferredCaptureMainView& output, const bool handPane) noexcept
	{
		output = {};
		const auto& observed = g_captureViews.Get(ViewIndex(handPane));
		if (g_deliveryFaulted.load(std::memory_order_acquire) ||
			g_mainWorldDepth != 0 || !observed.valid ||
			observed.mainWorldFrame == 0 ||
			observed.graphicsFrame == 0 ||
			observed.observationCount == 0 ||
			!observed.sourceCamera.valid ||
			!observed.retainedTarget) {
			return false;
		}
		output = observed;
		return true;
	}

	bool RevalidateDeferredCaptureMainTarget(
		const DeferredCaptureMainView& frozen) noexcept
	{
		if (frozen.handPaneView && !SeparateVRViews()) return false;
		const auto& observed = g_captureViews.Get(ViewIndex(frozen.handPaneView));
		if (!frozen.valid || frozen.mainWorldFrame == 0 ||
			frozen.graphicsFrame == 0 ||
			frozen.observationCount == 0 || !frozen.retainedTarget ||
			!frozen.sourceCamera.valid ||
			!HandMirrorRuntimeBridgePolicy::IsValidPrivateTarget(
				frozen.mainTarget) ||
			frozen.deviceIdentity == 0 || frozen.contextIdentity == 0 ||
			g_privatePassDepth != 0 || g_mainWorldDepth != 0) {
			return false;
		}
		// The post-finalizer callback runs after Skyrim has restored its pass-local
		// output-merger state.  That live RTV/DSV pair is therefore not required to
		// equal the authoritative pane world target. Revalidate the immutable global
		// snapshot and its AddRef-held target instead; the private pass saves and
		// restores whatever output-merger state is live at this exact callback.
		const auto& retained = frozen.retainedTarget;
		return observed.valid &&
			observed.handPaneView == frozen.handPaneView &&
			observed.mainWorldFrame == frozen.mainWorldFrame &&
			observed.graphicsFrame == frozen.graphicsFrame &&
			observed.observationCount == frozen.observationCount &&
			observed.mainTarget == frozen.mainTarget &&
			observed.deviceIdentity == frozen.deviceIdentity &&
			observed.contextIdentity == frozen.contextIdentity &&
			SameRetainedMainTargetCore(
				observed.retainedTarget, retained) &&
			SameBytes(
				&observed.viewProjection,
				&frozen.viewProjection, sizeof(frozen.viewProjection)) &&
			SameBytes(
				&observed.origin, &frozen.origin,
				sizeof(frozen.origin)) &&
			SameViewportBits(observed.viewport, frozen.viewport) &&
			SameFrozenSourceCameraValue(
				observed.sourceCamera, frozen.sourceCamera) &&
			reinterpret_cast<std::uintptr_t>(retained->device) ==
				frozen.deviceIdentity &&
			reinterpret_cast<std::uintptr_t>(retained->context) ==
				frozen.contextIdentity &&
			reinterpret_cast<std::uintptr_t>(retained->colorResource) ==
				frozen.mainTarget.colorResourceToken &&
			reinterpret_cast<std::uintptr_t>(retained->depthResource) ==
				frozen.mainTarget.depthResourceToken;
	}

	bool ObserveCurrentVRHandMainView(RE::BSRenderPass* pass) noexcept
	{
		if (!HandMirrorVRRuntimePolicy::AllowEarlyPaneObservation(
				HandMirrorApprovedContentReadOnlyObserver::VRPresentationFixEnabled(),
				SupportedRuntimePolicy::IsExactVRRuntime(), g_mainWorldDepth, g_privatePassDepth) ||
			g_deliveryFaulted.load(std::memory_order_acquire) ||
			!HandMirrorApprovedContentReadOnlyObserver::IsExactCurrentVRPanePass(pass))
			return false;
		EngineDeliveryState engine{};
		if (ReadEngineDeliveryStateSEH(engine) != EngineDeliveryStateStatus::kReady)
			return false;
		auto& observed = g_captureViews.Get(ViewIndex(true));
		const auto priorObservations = observed.observationCount;
		ObserveDeferredCaptureMainView(engine, true);
		return observed.valid &&
			observed.observationCount > priorObservations &&
			observed.mainWorldFrame == g_mainWorldFrame &&
			observed.graphicsFrame == engine.graphicsFrame;
	}

	CurrentMainDrawStateStatus QueryCurrentMainDrawState(
		CurrentMainDrawState& output, const bool allowVRWorld) noexcept
	{
		output = {};
		auto& observed = g_captureViews.Get(ViewIndex(allowVRWorld));
		if (g_deliveryFaulted.load(std::memory_order_acquire))
			return CurrentMainDrawStateStatus::kDeliveryFaulted;
		if (g_privatePassDepth != 0)
			return CurrentMainDrawStateStatus::kPrivatePassActive;
		if (!HandMirrorVRRuntimePolicy::AcceptMainDrawScope(
				allowVRWorld, SupportedRuntimePolicy::IsExactVRRuntime(),
				g_mainWorldDepth, g_privatePassDepth))
			return CurrentMainDrawStateStatus::kMainWorldActive;
		if (g_mainWorldFrame == 0)
			return CurrentMainDrawStateStatus::kMainWorldFrameMissing;
		if (!observed.valid)
			return CurrentMainDrawStateStatus::kDeferredSnapshotInvalid;
		if (observed.mainWorldFrame != g_mainWorldFrame)
			return CurrentMainDrawStateStatus::kDeferredMainWorldFrameMismatch;
		if (observed.graphicsFrame == 0)
			return CurrentMainDrawStateStatus::kDeferredGraphicsFrameMissing;
		EngineDeliveryState engine{};
		const auto status = ReadEngineDeliveryStateSEH(engine);
		if (status == EngineDeliveryStateStatus::kRefractionPassTarget) {
			// The refraction-normals pass is skipped without tombstoning the
			// frame's deferred main view; the main first-person pass already
			// delivered and nothing else in this frame may reuse the snapshot.
			g_counters.engineRefractionPassSkips.fetch_add(
				1, std::memory_order_relaxed);
			g_lastEngineTargetStatus.store(status, std::memory_order_relaxed);
			return CurrentMainDrawStateStatus::kRefractionPassActive;
		}
		if (status != EngineDeliveryStateStatus::kReady) {
			observed = {};
			return CurrentMainDrawStateStatus::kEngineStateUnavailable;
		}
		if (engine.graphicsFrame != observed.graphicsFrame)
			return CurrentMainDrawStateStatus::kGraphicsFrameMismatch;
		output = {
			.view = engine.rasterCamera.view,
			.projection = engine.rasterCamera.projection,
			.viewProjection = engine.mainView.viewProjection,
			.origin = engine.mainView.origin,
			.device = engine.device,
			.context = engine.context,
			.colorRTV = engine.targets.colorRTV,
			.motionRTV = engine.targets.motionRTV,
			.depthDSV = engine.targets.depthDSV,
			.captureReceiptTarget = observed.mainTarget,
			.viewport = engine.targets.viewport,
			.mainWorldFrame = g_mainWorldFrame,
			.graphicsFrame = observed.graphicsFrame,
			.valid = true
		};
		output.eyeCount = engine.eyeCount;
		for (std::uint32_t eye = 0; eye < output.eyeCount; ++eye)
			output.eyes[eye] = { engine.eyeViews[eye], engine.eyeViewports[eye] };
		return CurrentMainDrawStateStatus::kReady;
	}

	bool TryGetCurrentMainDrawState(
		CurrentMainDrawState& output, const bool allowVRWorld) noexcept
	{
		return QueryCurrentMainDrawState(output, allowVRWorld) ==
			CurrentMainDrawStateStatus::kReady;
	}

	bool TryRetainCurrentMainDrawTarget(
		const CurrentMainDrawState& current,
		RetainedMainTargetHandle& output) noexcept
	{
		output.reset();
		if (!current.valid)
			return false;
		return RetainMainTargetSEH(
			current.device, current.context, current.colorRTV, current.motionRTV,
			current.depthDSV, output);
	}

	bool TryRetainCurrentOutputMergerTarget(
		const CurrentMainDrawState& current,
		RetainedMainTargetHandle& output,
		D3D11_VIEWPORT& viewport) noexcept
	{
		// Single-viewport form: eye 0 (the first bound viewport).
		std::array<D3D11_VIEWPORT, kMaximumEyeCount> viewports{};
		std::uint32_t viewportCount = 0;
		const bool retained = TryRetainCurrentOutputMergerTargets(
			current, output, viewports, viewportCount);
		viewport = retained && viewportCount != 0 ? viewports[0] : D3D11_VIEWPORT{};
		return retained;
	}

	bool TryRetainCurrentOutputMergerTargets(
		const CurrentMainDrawState& current,
		RetainedMainTargetHandle& output,
		std::array<D3D11_VIEWPORT, 2>& viewports,
		std::uint32_t& viewportCount) noexcept
	{
		static_assert(std::tuple_size_v<std::remove_reference_t<
			decltype(viewports)>> == kMaximumEyeCount);
		output.reset();
		viewports = {};
		viewportCount = 0;
		if (!current.valid || !current.device || !current.context ||
			!current.depthDSV) {
			return false;
		}

		ID3D11RenderTargetView* colorRTV = nullptr;
		ID3D11DepthStencilView* boundDepthDSV = nullptr;
		// Query up to one viewport per eye and keep every one that is bound
		// (one on flat, two on VR).
		UINT boundViewportCount = kMaximumEyeCount;
		bool queried = false;
		__try {
			current.context->OMGetRenderTargets(
				1, &colorRTV, &boundDepthDSV);
			current.context->RSGetViewports(
				&boundViewportCount, viewports.data());
			queried = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			LatchEngineDeliveryStateException(GetExceptionCode());
		}
		if (boundViewportCount > kMaximumEyeCount)
			boundViewportCount = kMaximumEyeCount;

		const bool retained = queried && colorRTV && boundViewportCount != 0 &&
			RetainMainTargetSEH(
				current.device, current.context, colorRTV, current.motionRTV,
				boundDepthDSV ? boundDepthDSV : current.depthDSV, output);
		ReleaseRetainedInterfaceSEH(boundDepthDSV);
		ReleaseRetainedInterfaceSEH(colorRTV);
		if (!retained) {
			output.reset();
			viewports = {};
			return false;
		}
		viewportCount = boundViewportCount;
		return true;
	}

	bool TryRefreshCurrentMainDrawTarget(
		const CurrentMainDrawState& current,
		const RetainedMainTargetHandle& retained,
		RetainedMainTargetHandle& refreshed) noexcept
	{
		refreshed.reset();
		// The retained handle owns the live first-person RTV/DSV/resource identity.
		// Do not compare it with captureReceiptTarget: that identity belongs to the
		// frozen pane world target and Skyrim may use a different first-person depth
		// target while still drawing into the correct presentation surface.
		if (!current.valid || !retained)
			return false;
		RetainedMainTargetHandle currentRetained{};
		if (!RetainMainTargetSEH(
				current.device, current.context, current.colorRTV,
				current.motionRTV, current.depthDSV, currentRetained) ||
			!SameRetainedMainTargetCore(currentRetained, retained)) {
			return false;
		}
		// Return the just-retained handle rather than discarding it: its optional
		// motion view is the only slot-7 identity safe for an outer-final write.
		refreshed = std::move(currentRetained);
		return true;
	}

	bool RevalidateCurrentMainDrawTarget(
		const CurrentMainDrawState& current,
		const RetainedMainTargetHandle& retained) noexcept
	{
		RetainedMainTargetHandle refreshed{};
		return TryRefreshCurrentMainDrawTarget(current, retained, refreshed);
	}

	bool TryAttachCurrentOptionalMotionTarget(
		const CurrentMainDrawState& current,
		const RetainedMainTargetHandle& retainedCore,
		RetainedMainTargetHandle& attached) noexcept
	{
		attached.reset();
		// The exact pane callback retained the authoritative pass-local color/depth
		// pair.  At the outer-final seam Skyrim may already have restored unrelated
		// live OM bindings, so only slot 7 is borrowed from the current engine state.
		if (!current.valid || !retainedCore ||
			current.device != retainedCore->device ||
			current.context != retainedCore->context) {
			return false;
		}

		RetainedMainTargetHandle composite{};
		if (!RetainMainTargetSEH(
				retainedCore->device, retainedCore->context,
				retainedCore->colorRTV, current.motionRTV,
				retainedCore->depthDSV, composite) ||
			!SameRetainedMainTargetCore(composite, retainedCore)) {
			return false;
		}
		// Ordinary invalid or absent motion degrades inside RetainMainTargetSEH to
		// a valid color/depth handle with no motion attachment.  Native faults remain
		// terminal and are observed by the caller through Faulted().
		attached = std::move(composite);
		return true;
	}

	bool TryGetCurrentSourceCamera(FrozenSourceCamera& output) noexcept
	{
		return ReadSourceCameraSampleSEH(output);
	}

	bool SameFrozenSourceCamera(
		const FrozenSourceCamera& left,
		const FrozenSourceCamera& right) noexcept
	{
		return SameFrozenSourceCameraValue(left, right);
	}

	bool FenceCameraMatchesFrozenRaster(
		const FrozenSourceCamera& fence,
		const FrozenSourceCamera& raster) noexcept
	{
		// The fence sample is taken before Skyrim's main-world SetCameraData work,
		// while `raster` is the authoritative post-world ViewData sample.  NiCamera
		// RUNTIME_DATA2 is almost entirely mutable frustum/viewport state, so requiring
		// its 0x38 bytes to remain identical across those two engine phases rejects the
		// correct camera after every normal main-world upload.  Cohort ownership is the
		// stable camera object plus its world origin; projection, axes and both jittered
		// and unjittered frusta are validated from the authoritative raster sample.
		if (!fence.valid || !raster.valid || fence.cameraIdentity == 0 ||
			fence.cameraIdentity != raster.cameraIdentity ||
			fence.rasterAuthoritative || !raster.rasterAuthoritative ||
			!raster.unjitteredRasterAuthoritative ||
			!raster.rasterFrustum.valid || !Finite(raster.rasterView) ||
			!Finite(raster.rasterProjection) ||
			!raster.rasterFrustumUnjittered.valid ||
			!Finite(raster.rasterViewProjectionUnjittered) ||
			!Finite(raster.rasterProjectionUnjittered)) {
			return false;
		}
		const float x = fence.origin.x - raster.origin.x;
		const float y = fence.origin.y - raster.origin.y;
		const float z = fence.origin.z - raster.origin.z;
		const float delta = std::sqrt(x * x + y * y + z * z);
		return !MainViewVariantGuardPolicy::Mismatch(
			delta, MainViewVariantGuardPolicy::kOriginToleranceUnits);
	}

	void LogDiagnostics(const char* reason)
	{
		logger::info(
			"[MirrorsOfSkyrim][PaneDelivery] diagnostics ({}) enabled={} draw={} faulted={} "
			"allQueryCalls={} mainWorldHits={} privatePassSkips={} outsidePassSkips={} reentries={} "
			"noFrame={} retained(redraws/expiries)={}/{} candidateMismatch={} locationContainmentRejects={} paneResolveFailures={} paneResolveLast={} "
			"paneResolveBreakdown(base={} reference={} root={} identity={} transform={} exception={}) "
			"panePlaneMismatch={} "
			"panePlaneReasons(identity/normal/plane/side/extent)={}/{}/{}/{}/{} "
			"engineTargetFailures={} engineTargetLast={} "
			"engineTargetBreakdown(state={} index={} pointer={} exception={}) refractionPassSkips={} "
			"rendererInitFailures={} rendererInitLast={} projectionRejected={} "
			"projectionReasons(invalid/mainNotVisible/reflectedUncovered)={}/{}/{} "
			"projectionContinuityLease(requested/enabled/attempts/renewals/rejects/nativeVisibilityMissing/exceptions)={}/{}/{}/{}/{}/{}/{} "
			"ageRejectProof(attempts/accepted/mainNotVisible/projectionInvalid/identityLocationReject/nativeVisibilityMissing)={}/{}/{}/{}/{}/{} "
			"projectionPartialDraws={} targetRejected={} "
			"rendererRejections={} rendererLast={} rendererSetupRejected={} rendererTargetInvalid={} "
			"shaderResourceAliasSkips={} computeUAVAliasSkips={} outputMergerUAVSkips={} "
			"streamOutputSkips={} constantBufferMapFailures={} "
			"draws={} paired(draws/fallbacks)={}/{} "
			"deferredObserve(attempts/basic/raster/source/origin/retain/targetIdentity/success/maxOriginDelta)={}/{}/{}/{}/{}/{}/{}/{}/{:.3f} "
			"deferredRasterReasons(finite/inputBasis/viewBasis/axes/vp/projection/frustum/rightSign)={}/{}/{}/{}/{}/{}/{}/{} "
			"handUnjitteredRaster(attempts/rejects/finite/inputBasis/viewBasis/axes/vp/projection/frustum/rightSign)={}/{}/{}/{}/{}/{}/{}/{}/{}/{} "
			"deferredPair(snapshots/draws/lagged/missing/currentUnverified/ageRejected/originMismatch/maxAge/maxDelta)={}/{}/{}/{}/{}/{}/{}/{}/{:.3f} "
			"variantGuard(requested/checks/mismatches/staleDraws/skips/unavailable/maxDelta)={}/{}/{}/{}/{}/{}/{:.3f} "
			"staleOrigin(frames/maxDelta)={}/{:.3f} "
			"motion(targetMissing/eligible/ineligible)={}/{}/{} "
			"paneSuppressions={} paneRestores={} paneRestoreFailures={} "
			"crosshair(held/lost/changes)={}/{}/{} "
			"stateRestores={} faults={} lastSEH=0x{:08X} "
			"stereoEyeDraws={} stereoEyeRejects={} stereoViewportSplits={} "
			"stereoViewportQueryFailures={}",
			reason ? reason : "unspecified",
			g_enabled.load(std::memory_order_relaxed),
			g_drawEnabled.load(std::memory_order_relaxed),
			g_deliveryFaulted.load(std::memory_order_relaxed),
			g_counters.allQueryCalls.load(std::memory_order_relaxed),
			g_counters.mainWorldHits.load(std::memory_order_relaxed),
			g_counters.privatePassSkips.load(std::memory_order_relaxed),
			g_counters.outsidePassSkips.load(std::memory_order_relaxed),
			g_counters.reentries.load(std::memory_order_relaxed),
			g_counters.noPublishedFrame.load(std::memory_order_relaxed),
			g_counters.retainedFrameRedraws.load(std::memory_order_relaxed),
			g_counters.retainedFrameExpiries.load(std::memory_order_relaxed),
			g_counters.candidateMismatch.load(std::memory_order_relaxed),
			g_counters.locationContainmentRejects.load(std::memory_order_relaxed),
			g_counters.paneResolveFailures.load(std::memory_order_relaxed),
			MirrorPaneSurface::ToString(
				g_lastPaneResolveStatus.load(std::memory_order_relaxed)),
			g_counters.paneBaseFailures.load(std::memory_order_relaxed),
			g_counters.paneReferenceFailures.load(std::memory_order_relaxed),
			g_counters.paneRootFailures.load(std::memory_order_relaxed),
			g_counters.paneIdentityFailures.load(std::memory_order_relaxed),
			g_counters.paneTransformFailures.load(std::memory_order_relaxed),
			g_counters.paneResolveExceptions.load(std::memory_order_relaxed),
			g_counters.panePlaneMismatch.load(std::memory_order_relaxed),
			g_counters.panePlaneIdentityRejects.load(std::memory_order_relaxed),
			g_counters.panePlaneNormalRejects.load(std::memory_order_relaxed),
			g_counters.panePlaneOffPlaneRejects.load(std::memory_order_relaxed),
			g_counters.panePlaneCameraSideRejects.load(std::memory_order_relaxed),
			g_counters.panePlaneExtentRejects.load(std::memory_order_relaxed),
			g_counters.engineTargetFailures.load(std::memory_order_relaxed),
			ToString(g_lastEngineTargetStatus.load(std::memory_order_relaxed)),
			g_counters.engineStateMissing.load(std::memory_order_relaxed),
			g_counters.engineIndexFailures.load(std::memory_order_relaxed),
			g_counters.enginePointerFailures.load(std::memory_order_relaxed),
			g_counters.engineStateExceptions.load(std::memory_order_relaxed),
			g_counters.engineRefractionPassSkips.load(std::memory_order_relaxed),
			g_counters.rendererInitFailures.load(std::memory_order_relaxed),
			MirrorPaneRenderer::ToString(
				g_lastRendererInitStatus.load(std::memory_order_relaxed)),
			g_counters.projectionRejected.load(std::memory_order_relaxed),
			g_counters.projectionInvalid.load(std::memory_order_relaxed),
			g_counters.projectionMainViewNotVisible.load(std::memory_order_relaxed),
			g_counters.projectionReflectedUncovered.load(std::memory_order_relaxed),
			g_projectionContinuityLeaseRequested.load(std::memory_order_relaxed),
			g_projectionContinuityLeaseEnabled.load(std::memory_order_relaxed),
			g_counters.projectionContinuityLeaseAttempts.load(
				std::memory_order_relaxed),
			g_counters.projectionContinuityLeaseRenewals.load(
				std::memory_order_relaxed),
			g_counters.projectionContinuityLeaseRejects.load(
				std::memory_order_relaxed),
			g_counters.projectionContinuityNativeVisibilityMissing.load(
				std::memory_order_relaxed),
			g_counters.projectionContinuityExceptions.load(
				std::memory_order_relaxed),
			g_counters.ageRejectProofAttempts.load(std::memory_order_relaxed),
			g_counters.ageRejectProofAccepted.load(std::memory_order_relaxed),
			g_counters.ageRejectProofMainNotVisible.load(
				std::memory_order_relaxed),
			g_counters.ageRejectProofProjectionInvalid.load(
				std::memory_order_relaxed),
			g_counters.ageRejectProofIdentityLocationRejects.load(
				std::memory_order_relaxed),
			g_counters.ageRejectProofNativeVisibilityMissing.load(
				std::memory_order_relaxed),
			g_counters.projectionPartialDraws.load(std::memory_order_relaxed),
			g_counters.targetRejected.load(std::memory_order_relaxed),
			g_counters.rendererRejections.load(std::memory_order_relaxed),
			MirrorPaneRenderer::ToString(
				g_lastRendererStatus.load(std::memory_order_relaxed)),
			g_counters.rendererSetupRejected.load(std::memory_order_relaxed),
			g_counters.rendererTargetInvalid.load(std::memory_order_relaxed),
			g_counters.shaderResourceAliasSkips.load(std::memory_order_relaxed),
			g_counters.computeUAVAliasSkips.load(std::memory_order_relaxed),
			g_counters.outputMergerUAVSkips.load(std::memory_order_relaxed),
			g_counters.streamOutputSkips.load(std::memory_order_relaxed),
			g_counters.constantBufferMapFailures.load(std::memory_order_relaxed),
			g_counters.draws.load(std::memory_order_relaxed),
			g_counters.panePairedDraws.load(std::memory_order_relaxed),
			g_counters.panePairedFallbacks.load(std::memory_order_relaxed),
			g_counters.deferredPairObserveAttempts.load(std::memory_order_relaxed),
			g_counters.deferredPairObserveBasicShapeRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveSourceCameraRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveOriginRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRetainRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveTargetIdentityRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairSnapshots.load(std::memory_order_relaxed),
			std::bit_cast<float>(
				g_counters.deferredPairObserveOriginDeltaMaxBits.load(
					std::memory_order_relaxed)),
			g_counters.deferredPairObserveRasterFiniteRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterInputBasisRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterViewBasisRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterAxisRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterVPRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterProjectionRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterFrustumRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveRasterRightSignRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredAttempts.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredFiniteRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredInputBasisRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredViewBasisRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredAxisRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredVPRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredProjectionRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredFrustumRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairObserveHandUnjitteredRightSignRejects.load(
				std::memory_order_relaxed),
			g_counters.deferredPairSnapshots.load(std::memory_order_relaxed),
			g_counters.deferredPairDraws.load(std::memory_order_relaxed),
			g_counters.deferredPairLaggedDraws.load(std::memory_order_relaxed),
			g_counters.deferredPairMissing.load(std::memory_order_relaxed),
			g_counters.deferredPairCurrentViewUnverified.load(
				std::memory_order_relaxed),
			g_counters.deferredPairAgeRejected.load(std::memory_order_relaxed),
			g_counters.deferredPairOriginMismatch.load(std::memory_order_relaxed),
			g_counters.deferredPairAcceptedAgeMax.load(std::memory_order_relaxed),
			std::bit_cast<float>(
				g_counters.deferredPairOriginDeltaMaxBits.load(
					std::memory_order_relaxed)),
			g_variantGuardRequested.load(std::memory_order_relaxed),
			g_counters.variantChecks.load(std::memory_order_relaxed),
			g_counters.variantMismatches.load(std::memory_order_relaxed),
			g_counters.variantStaleDraws.load(std::memory_order_relaxed),
			g_counters.variantSkips.load(std::memory_order_relaxed),
			g_counters.variantUnavailable.load(std::memory_order_relaxed),
			std::bit_cast<float>(
				g_counters.variantDeltaMaxBits.load(std::memory_order_relaxed)),
			g_counters.paneStaleOriginFrames.load(std::memory_order_relaxed),
			std::bit_cast<float>(
				g_counters.paneOriginDeltaMaxBits.load(std::memory_order_relaxed)),
			g_counters.motionTargetMissing.load(std::memory_order_relaxed),
			g_counters.motionVelocityEligible.load(std::memory_order_relaxed),
			g_counters.motionVelocityIneligible.load(std::memory_order_relaxed),
			g_counters.paneSuppressions.load(std::memory_order_relaxed),
			g_counters.paneRestores.load(std::memory_order_relaxed),
			g_counters.paneRestoreFailures.load(std::memory_order_relaxed),
			g_counters.crosshairHeld.load(std::memory_order_relaxed),
			g_counters.crosshairLost.load(std::memory_order_relaxed),
			g_counters.crosshairChanges.load(std::memory_order_relaxed),
			g_counters.stateRestores.load(std::memory_order_relaxed),
			g_counters.faults.load(std::memory_order_relaxed),
			g_lastException.load(std::memory_order_relaxed),
			g_counters.stereoEyeDraws.load(std::memory_order_relaxed),
			g_counters.stereoEyeDrawRejects.load(std::memory_order_relaxed),
			g_counters.stereoViewportSplits.load(std::memory_order_relaxed),
			g_counters.stereoViewportQueryFailures.load(std::memory_order_relaxed));
	}
}
