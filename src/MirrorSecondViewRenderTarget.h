#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <d3d11.h>
#include <wrl/client.h>

namespace SecondView
{
	/** Private HDR color/depth target used by mirror captures. */
	class RenderTarget
	{
	public:
		inline static constexpr std::uint32_t kNoDebugInstance =
			(std::numeric_limits<std::uint32_t>::max)();
		/** Reduced capture views: mips 1..3 of one allocation (4096 -> 512). */
		inline static constexpr std::uint32_t kMaximumReducedViews = 3;

		enum class DebugChannel : std::uint8_t
		{
			kMirror,
			kMirrorAlternate
		};

		/** Cleanup proof is intentionally separate from restoration ownership. */
		enum class RestoreStatus : std::uint8_t
		{
			kNotRequired,
			kPending,
			kRestored,
			kFault
		};

#if (defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) || defined(MIRRORS_OF_SKYRIM_FLEET_TEST)) && defined(_MSC_VER)
		/** Test-only native-fault seams used by the focused WARP executable. */
		enum class TestFaultPoint : std::uint8_t
		{
			kNone,
			kAfterCaptureOMQuery,
			kAfterCaptureViewportQuery,
			kBeforeRestoreOMSet,
			kAfterRestoreOMSet,
			kBeforeRestoreViewportSet,
			kAfterRestoreViewportSet,
			kAfterRestoreOMReadback,
			kAfterRestoreViewportReadback,
			kAfterQueriedInterfaceRelease,
			kAfterSavedInterfaceRelease,
			kAfterOwnedInterfaceRelease
		};

		struct TestCleanupCounters
		{
			std::uint32_t queriedInterfaceReleases{ 0 };
			std::uint32_t savedInterfaceReleases{ 0 };
			std::uint32_t ownedInterfaceReleases{ 0 };
		};
#endif

		RenderTarget() = default;
		~RenderTarget();

		RenderTarget(const RenderTarget&) = delete;
		RenderTarget& operator=(const RenderTarget&) = delete;

		/** Capture-only storage. Receivers using this must publish colour only. */
		struct SharedDepth
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> writable, readOnly;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		};
		[[nodiscard]] SharedDepth RetainDepthStorage() const noexcept
		{
			return !bound && Ready() && !HasReducedViews() ?
				SharedDepth{ depthTexture, depthDSV, readOnlyDepthDSV, depthSRV } : SharedDepth{};
		}

		[[nodiscard]] bool Create(
			ID3D11Device* a_device,
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
			DebugChannel a_debugChannel = DebugChannel::kMirror,
			std::uint32_t a_debugInstance = kNoDebugInstance,
			const SharedDepth* a_sharedDepth = nullptr,
			std::uint32_t a_reducedViews = 0) noexcept;
		void Release(ID3D11DeviceContext* a_context = nullptr) noexcept;
		/** Select a precreated view while unbound; resource identities never change. */
		[[nodiscard]] bool SelectRenderMip(std::uint32_t a_mip) noexcept;
		[[nodiscard]] std::uint32_t RenderMip() const noexcept { return renderMip; }
		/** Reduced capture views (mips 1..n) share the colour/depth allocations. */
		[[nodiscard]] bool HasReducedViews() const noexcept { return reducedViewCount != 0; }
		[[nodiscard]] std::uint32_t ReducedViewCount() const noexcept { return reducedViewCount; }

		/**
		 * Snapshot the current OM/viewports and bind the private target.  Once the
		 * snapshot is complete, Bound() becomes true before the first D3D mutation
		 * so a caller-side SEH boundary can always detect and roll back a partial
		 * bind.
		 */
		[[nodiscard]] bool Begin(ID3D11DeviceContext* a_context, const float a_clearColor[4]) noexcept;
		/** Preserve private color while starting a fresh depth/stencil composition cycle. */
		[[nodiscard]] bool ClearDepthStencil(ID3D11DeviceContext* a_context) noexcept;
		/** Generate the complete color mip chain after unbind. */
		[[nodiscard]] bool GenerateColorMips(ID3D11DeviceContext* a_context) noexcept;
		void Rebind(ID3D11DeviceContext* a_context) noexcept;
		/** Bind the private color target with depth read-only for depth-SRV sampling. */
		void RebindReadOnlyDepth(ID3D11DeviceContext* a_context) noexcept;
		RestoreStatus End(ID3D11DeviceContext* a_context) noexcept;

		/** Shared-depth reuses accepted through a forwarding device (evidence only). */
		[[nodiscard]] static std::uint32_t SharedDepthForwardedAccepts() noexcept;

		[[nodiscard]] bool Ready() const noexcept
		{
			return restoreStatus != RestoreStatus::kFault && colorRTV && depthDSV &&
			       readOnlyDepthDSV && depthSRV && colorSRV &&
			       colorFormat != DXGI_FORMAT_UNKNOWN;
		}
		[[nodiscard]] bool Bound() const noexcept { return bound; }
		[[nodiscard]] RestoreStatus LastRestoreStatus() const noexcept
		{
			return restoreStatus;
		}
		[[nodiscard]] bool RestorationSucceeded() const noexcept
		{
			return !bound &&
			       (restoreStatus == RestoreStatus::kNotRequired ||
				   restoreStatus == RestoreStatus::kRestored);
		}
		/**
		 * Rebind traffic accounting. `Rebind` runs once per engine draw call
		 * inside a private pass, so the ratio of actual D3D calls to invocations
		 * is the plugin's largest per-frame contribution to driver load and a
		 * prime suspect in the repeated GPU stalls.
		 */
		[[nodiscard]] std::uint64_t RebindCalls() const noexcept { return rebindCalls; }
		[[nodiscard]] std::uint64_t Retargets() const noexcept { return retargets; }
		[[nodiscard]] std::uint64_t ViewportSets() const noexcept { return viewportSets; }
		[[nodiscard]] bool LastBeginRejectedOutputMergerUAV() const noexcept
		{
			return lastBeginRejectedOutputMergerUAV;
		}
		[[nodiscard]] bool LastBeginNativeFaulted() const noexcept
		{
			return lastBeginNativeFaulted;
		}
		[[nodiscard]] ID3D11ShaderResourceView* ColorSRV() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr :
				(renderMip == 0 ? colorSRV.Get() : reducedViews[renderMip - 1].colorSRV.Get());
		}
		[[nodiscard]] ID3D11RenderTargetView* ColorRTV() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr :
				(renderMip == 0 ? colorRTV.Get() : reducedViews[renderMip - 1].colorRTV.Get());
		}
		[[nodiscard]] ID3D11DepthStencilView* DepthDSV() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr :
				(renderMip == 0 ? depthDSV.Get() : reducedViews[renderMip - 1].depthDSV.Get());
		}
		[[nodiscard]] ID3D11DepthStencilView* ReadOnlyDepthDSV() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr :
				(renderMip == 0 ? readOnlyDepthDSV.Get() : reducedViews[renderMip - 1].readOnlyDepthDSV.Get());
		}
		[[nodiscard]] ID3D11ShaderResourceView* DepthSRV() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr :
				(renderMip == 0 ? depthSRV.Get() : reducedViews[renderMip - 1].depthSRV.Get());
		}
		/** Borrowed physical identities for alias diagnostics; no reference is added. */
		[[nodiscard]] ID3D11Resource* ColorResourceIdentity() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr : colorTexture.Get();
		}
		[[nodiscard]] ID3D11Resource* DepthResourceIdentity() const noexcept
		{
			return restoreStatus == RestoreStatus::kFault ? nullptr : depthTexture.Get();
		}
		/** Retain the color SRV only after this target has been unbound. */
		[[nodiscard]] bool TryRetainUnboundColorSRV(
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& output) const noexcept;
		/** Retain the depth SRV only after this target has been unbound. */
		[[nodiscard]] bool TryRetainUnboundDepthSRV(
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& output) const noexcept;
		[[nodiscard]] std::uint32_t Width() const noexcept { return width >> renderMip; }
		[[nodiscard]] std::uint32_t Height() const noexcept { return height >> renderMip; }
		[[nodiscard]] std::uint32_t AllocatedWidth() const noexcept { return width; }
		[[nodiscard]] std::uint32_t AllocatedHeight() const noexcept { return height; }
		[[nodiscard]] DXGI_FORMAT ColorFormat() const noexcept { return colorFormat; }
		[[nodiscard]] std::uint32_t ColorMipLevels() const noexcept
		{
			return colorMipLevels - renderMip;
		}
		[[nodiscard]] bool ColorMipGenerationSupported() const noexcept
		{
			return colorMipGenerationSupported;
		}

#if (defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) || defined(MIRRORS_OF_SKYRIM_FLEET_TEST)) && defined(_MSC_VER)
		static void ResetTestInstrumentation() noexcept;
		static void InjectTestFaultOnce(TestFaultPoint a_point) noexcept;
		[[nodiscard]] static TestCleanupCounters ReadTestCleanupCounters() noexcept;
		[[nodiscard]] bool SavedStateEmptyForTesting() const noexcept;
#endif

	private:
		[[nodiscard]] bool ResetSavedState() noexcept;
		[[nodiscard]] bool ReleaseOwnedResources() noexcept;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthDSV;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> readOnlyDepthDSV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV;
		struct ReducedViews
		{
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorRTV;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthDSV;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> readOnlyDepthDSV;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV;
		};
		std::array<ReducedViews, kMaximumReducedViews> reducedViews;
		std::uint32_t reducedViewCount{ 0 };
		Microsoft::WRL::ComPtr<ID3D11Texture2D> reducedMipTailTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> reducedMipTailSRV;

		std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> savedRTVs;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> savedDSV;
		std::array<D3D11_VIEWPORT,
			D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> savedViewports{};
		std::uint32_t savedViewportCount{ 0 };
		std::uint64_t rebindCalls{ 0 };
		std::uint64_t retargets{ 0 };
		std::uint64_t viewportSets{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		std::uint32_t colorMipLevels{ 0 };
		std::uint32_t renderMip{ 0 };
		DXGI_FORMAT colorFormat{ DXGI_FORMAT_UNKNOWN };
		bool colorMipGenerationSupported{ false };
		// This is restoration ownership, not merely proof that Rebind completed.
		// It is set before the first private OM/viewport mutation.
		bool bound{ false };
		RestoreStatus restoreStatus{ RestoreStatus::kNotRequired };
		bool lastBeginRejectedOutputMergerUAV{ false };
		bool lastBeginNativeFaulted{ false };
	};
}
