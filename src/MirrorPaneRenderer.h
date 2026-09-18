#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "HandMirrorRuntimeBridgePolicy.h"
#include "PlanarMirrorMath.h"

namespace MirrorPaneRenderer
{
	/**
	 * Renderer-facing reflected frame.  This is deliberately independent of the
	 * wall-only MirrorFramePublication store: integration adapters must provide a
	 * complete tagged owner and a compatible closed audience.
	 */
	struct PublishedFrame
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV{};
		DirectX::XMFLOAT4X4 reflectedViewProjection{};
		DirectX::XMFLOAT3 reflectedOrigin{};
		PlanarMirrorMath::Plane capturePlane{};
		HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity owner{};
		HandMirrorRuntimeBridgePolicy::PublicationAudience audience{
			HandMirrorRuntimeBridgePolicy::PublicationAudience::kUnknown };
		std::uint64_t captureSequence{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		DXGI_FORMAT colorFormat{ DXGI_FORMAT_UNKNOWN };
		bool valid{ false };
	};

	/** World-space aperture derived from the authored pane geometry itself. */
	struct PaneTransform
	{
		DirectX::XMFLOAT3 center{};
		DirectX::XMFLOAT3 tangentExtent{};    // pane rotate column Y * scaled half-width
		DirectX::XMFLOAT3 bitangentExtent{};  // pane rotate column Z * scaled half-height
		HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity owner{};
	};

	/** Restored main-view constants at the M5 pre-water delivery seam. */
	struct MainView
	{
		DirectX::XMFLOAT4X4 viewProjection{};
		DirectX::XMFLOAT3 origin{};
	};

	/**
	 * Explicit writable target selected from Skyrim's renderer shadow state by
	 * the integration layer.  The renderer borrows these pointers only for the
	 * synchronous Draw call and never mutates Bethesda's shadow-state arrays.
	 */
	struct DrawTargets
	{
		ID3D11RenderTargetView* colorRTV{ nullptr };
		ID3D11RenderTargetView* motionRTV{ nullptr };
		ID3D11DepthStencilView* depthDSV{ nullptr };
		D3D11_VIEWPORT viewport{};
	};

	struct MotionHistory
	{
		DirectX::XMFLOAT4X4 mainViewProjection{};
		DirectX::XMFLOAT4X4 reflectedViewProjection{};
		DirectX::XMFLOAT3 mainOrigin{};
		DirectX::XMFLOAT3 reflectedOrigin{};
		PaneTransform pane{};
		std::uint64_t captureSequence{ 0 };
		// Delivery-call index the history was recorded on. TAA's colour history
		// is exactly one main frame old, so velocity may only be emitted against
		// a history recorded on the immediately previous delivery call; a
		// history that skipped delivery frames spans multiple frames of camera
		// motion and reprojects reflected content to the wrong place (the
		// 2026-08-18 moving "imprint").
		std::uint64_t deliveryFrame{ 0 };
		bool valid{ false };
	};

	/**
	 * Selects how an eligible previous-frame history produces velocity.
	 *
	 * Wall mirrors retain the historical reflected-depth reconstruction.  A
	 * moving pane whose publication has deliberately dropped reflected depth
	 * can explicitly request pane-affine velocity: the same authored pane-local
	 * point is projected through the previous pane and previous main view.
	 */
	enum class MotionMode : std::uint8_t
	{
		kDepthReconstructedWorld,
		kPaneAffine,
		/**
		 * Write an off-screen velocity with full validity so vanilla TAA cannot
		 * blend any colour history into the pane.  Reflected content that moves
		 * independently of the pane (the player's head turning inside a raised
		 * hand mirror) has no per-pixel velocity in either mode above, and the
		 * pane-static or pane-affine vector then blends the previous pose into
		 * the current one: a doubled, smeared player with stair-stepped edges
		 * while the static room stays sharp (ScreenShot99/100, 2026-09-02).
		 */
		kRejectHistory
	};

	struct DrawRequest
	{
		PublishedFrame frame{};
		PaneTransform pane{};
		MainView mainView{};
		DrawTargets targets{};
		MotionHistory motionHistory{};
		MotionMode motionMode{ MotionMode::kDepthReconstructedWorld };
		// Monotonic index of this delivery call; velocity output requires a
		// nonzero deliveryFrame and motionHistory.deliveryFrame == deliveryFrame - 1.
		std::uint64_t deliveryFrame{ 0 };
		// Actual selected contour of a prepared world NIF, borrowed for Draw.
		std::span<const DirectX::XMFLOAT2> customPaneTriangles{};
		// Nested pane draws (a pane inside another capture, one frame old): a
		// projected footprint outside the captured region is drawn with clamped
		// sampling instead of being rejected as reflected-uncovered.
		bool clampReflectedCoverage{ false };
		// A nested hand pane is a world surface: retain depth occlusion instead
		// of borrowing the first-person overlay's no-depth presentation.
		bool worldSpaceDepth{ false };
		// Opaque material fallback; needs an exact pane owner, no publication.
		bool opaqueBacking{ false };
	};

	enum class InitializationStatus : std::uint8_t
	{
		kReady,
		kInvalidDevice,
		kUnsupportedFeatureLevel,
		kVertexShaderCreationFailed,
		kPixelShaderCreationFailed,
		kInputLayoutCreationFailed,
		kVertexBufferCreationFailed,
		kIndexBufferCreationFailed,
		kConstantBufferCreationFailed,
		kSamplerCreationFailed,
		kBlendCreationFailed,
		kRasterizerCreationFailed,
		kDepthStencilCreationFailed
	};

	enum class SamplingMode : std::uint8_t
	{
		kFullMipChain,
		// Legacy hand diagnostic: complete chain, x16 anisotropy and the
		// quarter-mip stability bias.
		kStableHandFullMipChain,
		// Diagnostic only: distinguishes output-projective minification from
		// source-scene geometry/material LOD changes.
		kBaseMipOnly,
		// Production hand lane: complete chain, trilinear, no anisotropy, and the
		// quarter-mip stability bias. This prevents the pane/capture yaw relation
		// from steering a long anisotropic tap footprint across the portrait.
		kStableHandIsotropic
	};

	enum class DrawStatus : std::uint8_t
	{
		kDrawn,
		kDrawnPartialCoverage,
		kNotInitialized,
		kInvalidArgument,
		kNoPublishedFrame,
		kCandidateMismatch,
		kDeviceMismatch,
		// Projection inputs or clipping arithmetic were invalid. This is distinct
		// from either valid geometric non-coverage case and remains fail-closed.
		kProjectionRejected,
		// No positive-area portion of the authored pane reaches the main raster.
		kProjectionMainViewNotVisible,
		// The pane reaches the main raster, but none of that visible region is
		// covered by the retained reflected projection.
		kProjectionReflectedUncovered,
		kColorTargetMissing,
		kDepthTargetMissing,
		kViewportRejected,
		kTargetDeviceMismatch,
		kDepthTargetReadOnly,
		kTargetResourceRejected,
		kTargetAliasRejected,
		kTargetShapeRejected,
		kTargetBindFlagsRejected,
		kShaderResourceAliasBound,
		kComputeUAVAliasBound,
		kOutputMergerUAVUnsafe,
		kStreamOutputBound,
		kConstantBufferMapFailed,
		kCustomVertexBufferCreationFailed,
		kCustomVertexBufferMapFailed,
		// The renderer captured no mutable state and refused to issue the draw.
		kContextStateCaptureFailed,
		// Every restore/release step was attempted, but at least one native call
		// faulted.  The caller must retire the frame and fail-stop its route.
		kContextStateRestoreFailed
	};

	[[nodiscard]] constexpr bool IsSuccessfulDraw(DrawStatus a_status) noexcept
	{
		return a_status == DrawStatus::kDrawn ||
		       a_status == DrawStatus::kDrawnPartialCoverage;
	}

	/** Any successful opaque custom draw replaces the authored native fallback. */
	[[nodiscard]] constexpr bool AllowsPaneSuppression(DrawStatus a_status) noexcept
	{
		return IsSuccessfulDraw(a_status);
	}

	[[nodiscard]] const char* ToString(InitializationStatus status) noexcept;
	[[nodiscard]] const char* ToString(DrawStatus a_status) noexcept;

	// Internal depth-query adapter. The callback may change only the same IA,
	// shader, RS and OM slots as Draw; no SRV/UAV/SO writes. State is restored even
	// on a native exception. The bool says whether HS/DS can safely be touched.
	using DepthProbeOperation = bool (*)(ID3D11DeviceContext*, bool, void*) noexcept;
	[[nodiscard]] DrawStatus RunDepthProbeWithPreservedState(
		ID3D11DeviceContext* context, DepthProbeOperation operation, void* opaque) noexcept;

	/**
	 * Evidence for the most recent native fault while restoring or releasing a
	 * captured context state.  `lastStep` is the RestoreStep ordinal, 0xFE for
	 * a snapshot release, 0xFD for a shutdown release, 0xFF when none occurred.
	 */
	struct RestoreFaultTelemetry
	{
		std::uint32_t faults{ 0 };
		std::uint32_t lastExceptionCode{ 0 };
		std::uint8_t lastStep{ 0xFF };
		// First fault of the process: kind 0 = restore step (index = RestoreStep
		// ordinal), 1 = snapshot release (index = SnapshotOrdinal), 2 = shutdown
		// release; pointer = the object being released (0 for a restore step);
		// pointerModule = base of the loaded image containing its vtable's
		// Release slot, 0 when the pointer or vtable is not inside any image.
		std::uint8_t firstKind{ 0xFF };
		std::uint8_t firstIndex{ 0xFF };
		std::uint32_t firstExceptionCode{ 0 };
		std::uintptr_t firstPointer{ 0 };
		std::uintptr_t firstPointerModule{ 0 };
		std::uint32_t releaseOrdinalMask{ 0 };  // bit per SnapshotOrdinal
		std::uint32_t restoreStepMask{ 0 };     // bit per RestoreStep
	};
	[[nodiscard]] RestoreFaultTelemetry GetRestoreFaultTelemetry() noexcept;

	/** Stable ordinal of every object the context-state snapshot releases. */
	enum class SnapshotOrdinal : std::uint8_t
	{
		kPredicate,
		kDepthStencilView,
		kRenderTarget0,
		kRenderTarget1,
		kRenderTarget2,
		kRenderTarget3,
		kRenderTarget4,
		kRenderTarget5,
		kRenderTarget6,
		kRenderTarget7,
		kDepthStencilState,
		kBlendState,
		kRasterizerState,
		kPixelSampler,
		kPixelResource0,
		kPixelResource1,
		kPixelConstantBuffer,
		kVertexConstantBuffer,
		kDomainShader,
		kDomainInstances,
		kHullShader,
		kHullInstances,
		kGeometryShader,
		kGeometryInstances,
		kPixelShader,
		kPixelInstances,
		kVertexShader,
		kVertexInstances,
		kIndexBuffer,
		kVertexBuffer,
		kInputLayout,
		kCount
	};
	[[nodiscard]] const char* ToString(SnapshotOrdinal a_ordinal) noexcept;

	/**
	 * Optional diagnostic text sink (the renderer has no logger of its own).
	 * Called synchronously on the render thread with a short single line.
	 */
	using DiagnosticSink = void (*)(const char* a_message) noexcept;
	void SetDiagnosticSink(DiagnosticSink a_sink) noexcept;

	/**
	 * One-shot classification of what the wrapped immediate context hands back
	 * from the five XSGetShader calls, taken before the first draw whenever a
	 * forwarding device is accepted.  Objects whose vtable Release slot lies in
	 * a loaded image are released again under SEH; anything else is left alone.
	 */
	struct WrapperShaderGetterProbe
	{
		struct Stage
		{
			std::uintptr_t shader{ 0 };
			std::uint32_t instanceCount{ 0 };
			std::uintptr_t shaderModule{ 0 };
			std::uint8_t shape{ 0 };  // 0 null, 1 unreadable, 2 vtable unreadable, 3 not in an image, 4 callable
			bool getterFaulted{ false };
			bool released{ false };
			std::uint32_t releaseExceptionCode{ 0 };
		};
		bool done{ false };
		std::array<Stage, 5> stages{};  // vertex, pixel, geometry, hull, domain
	};
	[[nodiscard]] WrapperShaderGetterProbe GetWrapperShaderGetterProbe() noexcept;

	/**
	 * Bounded read-only evidence for the three vertices of the hand aperture's
	 * single presentation primitive.  Coverage admission intentionally remains
	 * based on the exact authored 16/24-gon; this only exposes whether the larger
	 * cover primitive would cross either projective W plane before rasterization.
	 */
	struct HandCoverProjectionTelemetry
	{
		float minimumMainClipW{ 0.0f };
		float minimumReflectedClipW{ 0.0f };
		std::uint8_t coverVertexCount{ 0 };
		std::uint8_t mainPositiveWVertices{ 0 };
		std::uint8_t reflectedPositiveWVertices{ 0 };
		bool handAperture{ false };
		bool finite{ false };
		bool valid{ false };

		[[nodiscard]] constexpr bool MainClipWAtRisk() const noexcept
		{
			return valid && coverVertexCount != 0 &&
			       mainPositiveWVertices != coverVertexCount;
		}

		[[nodiscard]] constexpr bool ReflectedClipWAtRisk() const noexcept
		{
			return valid && coverVertexCount != 0 &&
			       reflectedPositiveWVertices != coverVertexCount;
		}
	};

	/** No D3D state is queried or changed. */
	[[nodiscard]] HandCoverProjectionTelemetry InspectHandCoverProjection(
		const PaneTransform& pane,
		const MainView& mainView,
		const PublishedFrame& frame) noexcept;

	/**
	 * Read-only projection admission used by M5 before deciding whether an old
	 * exact-main frame may renew ownership. No D3D state is queried or changed.
	 */
	[[nodiscard]] DrawStatus ClassifyProjectionCoverage(
		const PaneTransform& pane,
		const MainView& mainView,
		const PublishedFrame& frame,
		std::span<const DirectX::XMFLOAT2> customPaneTriangles = {}) noexcept;

	/**
	 * Proves that the complete authored hand aperture, rather than only the part
	 * visible in one main view, lies inside the reflected clip volume.  Detached
	 * last-good presentation uses this stronger read-only gate before replacing a
	 * snapshot which may need to cover a different live-view subset later.
	 * No D3D state is queried or changed; wall/public frames fail closed.
	 */
	[[nodiscard]] bool HasFullHandReflectedApertureCoverage(
		const PaneTransform& pane,
		const PublishedFrame& frame) noexcept;

	/**
	 * Self-contained M5b/M5c D3D11 quad renderer.
	 *
	 * It owns only plugin-private immutable/dynamic resources.  Draw performs no
	 * hook installation and never changes pane cull state.  Every context state
	 * it changes is captured with owning COM references and restored before the
	 * call returns.  If any stream-output target is bound, Draw refuses to touch
	 * the pipeline because D3D11 cannot report the original SO offsets.
	 */
	class Renderer
	{
	public:
		Renderer() = default;
		~Renderer() = default;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		Renderer& operator=(Renderer&&) = delete;

		[[nodiscard]] bool Initialize(
			ID3D11Device* a_device,
			InitializationStatus* status = nullptr,
			SamplingMode samplingMode = SamplingMode::kFullMipChain,
			bool replaceCoplanarFallback = false) noexcept;
		/** Sampling mode of the ready renderer; Initialize with another mode rebuilds it. */
		[[nodiscard]] SamplingMode ActiveSamplingMode() const noexcept
		{
			return activeSamplingMode;
		}
		/** Four depth units toward the eye; no slope bias or change to the aperture. */
		[[nodiscard]] bool ReplacesCoplanarFallback() const noexcept
		{
			return coplanarFallbackReplacement;
		}
		void Shutdown() noexcept;
		/**
		 * Remove every logical resource owner before making any native Release call,
		 * then attempt each Release behind its own SEH boundary.  False means at
		 * least one native Release faulted; the object is still logically empty.
		 */
		[[nodiscard]] bool ShutdownSafely() noexcept;
		[[nodiscard]] bool Ready() const noexcept;
		/** Read-only quality contract used by offline/runtime diagnostics. */
		[[nodiscard]] bool TryGetSamplerDescription(
			D3D11_SAMPLER_DESC& a_description) const noexcept;

		[[nodiscard]] DrawStatus Draw(
			ID3D11DeviceContext* a_context,
			const DrawRequest& a_request,
			bool* a_motionOutputEnabled = nullptr) noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
		// The wall path remains the historical quad. Both hand styles use a single
		// continuous covering triangle and an exact shader aperture mask selected by
		// authored-surface identity; neither relies on depth-hidden overscan.
		Microsoft::WRL::ComPtr<ID3D11Buffer> handVertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> handIndexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> filigreeHandVertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> filigreeHandIndexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> customPaneVertexBuffer;
		std::uint32_t customPaneVertexCapacity{ 0 };
		Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState;
		SamplingMode activeSamplingMode{ SamplingMode::kFullMipChain };
		bool coplanarFallbackReplacement{ false };
		/** NDC clamp the cached rasterizer state was built with (0 = unbiased). */
		float coplanarBiasClamp{ 0.0f };

		/**
		 * Rebuild the rasterizer state when the pane's viewing distance changes
		 * enough that a fixed NDC bias would no longer be the intended world
		 * offset. Returns false only if the state could not be created, in which
		 * case the previous one is kept.
		 */
		bool EnsureCoplanarBias(float viewDistance) noexcept;
	};

#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
	namespace TestHooks
	{
		enum class CleanupFaultSite : std::uint8_t
		{
			kNone,
			kPublishedFrameAfterResourceQuery,
			kRestoreInputLayout,
			kShutdownFirstRelease
		};

		struct CleanupTelemetry
		{
			std::uint32_t restoreStepsAttempted{ 0 };
			std::uint32_t restoreStepsCompleted{ 0 };
			std::uint32_t queryReleasesAttempted{ 0 };
			std::uint32_t queryReleasesCompleted{ 0 };
			std::uint32_t snapshotReleasesAttempted{ 0 };
			std::uint32_t snapshotReleasesCompleted{ 0 };
			std::uint32_t shutdownReleasesAttempted{ 0 };
			std::uint32_t shutdownReleasesCompleted{ 0 };
		};

		inline constexpr std::uint32_t kRestoreStepCount = 20;
		inline constexpr std::uint32_t kRendererResourceCount = 16;

		void ResetCleanupFaultInjection() noexcept;
		void SetCleanupFaultSite(CleanupFaultSite site) noexcept;
		[[nodiscard]] CleanupTelemetry GetCleanupTelemetry() noexcept;
	}
#endif
}
