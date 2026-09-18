#if !defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) && !defined(MIRRORS_OF_SKYRIM_FLEET_TEST)
#	include "PCH.h"
#endif

#include "D3D11OutputMergerState.h"
#include "MirrorSecondViewRenderTarget.h"
#include "EngineDeviceIdentity.h"

#include <atomic>

#include <array>
#include <cstring>
#include <utility>

namespace SecondView
{
	namespace
	{
		constexpr std::uint32_t kMaximumTargetDimension = 16384;
		// Viewport slots bound for the mono private capture: one per eye the
		// engine's instanced-stereo draw can select (2 on VR), 1 on flat.
		constexpr UINT kStereoViewportSlots = 2;
		static_assert(kStereoViewportSlots <=
			D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);

		[[nodiscard]] UINT PrivateViewportSlotCount() noexcept
		{
#if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) || defined(MIRRORS_OF_SKYRIM_FLEET_TEST)
			// The engine-independent warp test has no runtime module to query.
			return 1;
#else
			return SupportedRuntimePolicy::IsExactVRRuntime() ?
				kStereoViewportSlots : 1u;
#endif
		}

		void BindFullTargetViewports(
			ID3D11DeviceContext* a_context,
			const std::uint32_t a_width,
			const std::uint32_t a_height) noexcept
		{
			// Skyrim VR draws every engine batch for both eyes in one instanced
			// pass and the right-eye instance selects viewport index 1.  The
			// private capture is mono, so every viewport slot the engine can select
			// must be the same full-target rectangle; a single bound viewport would
			// leave the right-eye instance on a default (empty) viewport and drop
			// its half of the geometry from the capture.  Flat runtimes keep the
			// historical single viewport.  End() restores every saved viewport slot
			// (savedViewportCount of them), so the extra slot never leaks into the
			// main pass.
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(a_width);
			viewport.Height = static_cast<float>(a_height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			std::array<D3D11_VIEWPORT, kStereoViewportSlots> viewports{};
			viewports.fill(viewport);
			a_context->RSSetViewports(PrivateViewportSlotCount(), viewports.data());
		}
		constexpr GUID kD3DDebugObjectName{
			0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 }
		};

		void SetDebugName(ID3D11DeviceChild* a_object, const char* a_name) noexcept
		{
			if (a_object && a_name) {
				a_object->SetPrivateData(
					kD3DDebugObjectName,
					static_cast<UINT>(std::strlen(a_name)),
					a_name);
			}
		}

		enum class NativeCallPoint : std::uint8_t
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

		enum class InterfaceOwnership : std::uint8_t
		{
			kQueried,
			kSaved,
			kOwned
		};

#if (defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) || defined(MIRRORS_OF_SKYRIM_FLEET_TEST)) && defined(_MSC_VER)
		thread_local RenderTarget::TestFaultPoint g_testFaultPoint{
			RenderTarget::TestFaultPoint::kNone
		};
		thread_local RenderTarget::TestCleanupCounters g_testCleanupCounters{};

		void MaybeRaiseTestFault(const NativeCallPoint a_point) noexcept
		{
			if (static_cast<std::uint8_t>(g_testFaultPoint) !=
				static_cast<std::uint8_t>(a_point))
				return;
			g_testFaultPoint = RenderTarget::TestFaultPoint::kNone;
			::RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		}

		void RecordReleaseAttempt(const InterfaceOwnership a_ownership) noexcept
		{
			switch (a_ownership) {
			case InterfaceOwnership::kQueried:
				++g_testCleanupCounters.queriedInterfaceReleases;
				break;
			case InterfaceOwnership::kSaved:
				++g_testCleanupCounters.savedInterfaceReleases;
				break;
			case InterfaceOwnership::kOwned:
				++g_testCleanupCounters.ownedInterfaceReleases;
				break;
			}
		}
#else
		void MaybeRaiseTestFault(const NativeCallPoint) noexcept {}
		void RecordReleaseAttempt(const InterfaceOwnership) noexcept {}
#endif

		[[nodiscard]] NativeCallPoint ReleaseFaultPoint(
			const InterfaceOwnership a_ownership) noexcept
		{
			switch (a_ownership) {
			case InterfaceOwnership::kQueried:
				return NativeCallPoint::kAfterQueriedInterfaceRelease;
			case InterfaceOwnership::kSaved:
				return NativeCallPoint::kAfterSavedInterfaceRelease;
			case InterfaceOwnership::kOwned:
				return NativeCallPoint::kAfterOwnedInterfaceRelease;
			}
			return NativeCallPoint::kNone;
		}

		template <class Interface>
		[[nodiscard]] bool ReleaseInterfaceGuarded(
			Interface*& a_value,
			const InterfaceOwnership a_ownership) noexcept
		{
			Interface* const owned = a_value;
			a_value = nullptr;
			if (!owned)
				return true;
			RecordReleaseAttempt(a_ownership);

#if defined(_MSC_VER)
			__try {
				owned->Release();
				MaybeRaiseTestFault(ReleaseFaultPoint(a_ownership));
				return true;
			} __except (1) {
				return false;
			}
#else
			owned->Release();
			return true;
#endif
		}

		[[nodiscard]] bool GetOutputMergerStateGuarded(
			ID3D11DeviceContext* a_context,
			ID3D11RenderTargetView** a_renderTargets,
			ID3D11DepthStencilView** a_depthStencil,
			const NativeCallPoint a_afterCall) noexcept
		{
#if defined(_MSC_VER)
			__try {
				a_context->OMGetRenderTargets(
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
					a_renderTargets,
					a_depthStencil);
				MaybeRaiseTestFault(a_afterCall);
				return true;
			} __except (1) {
				return false;
			}
#else
			a_context->OMGetRenderTargets(
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
				a_renderTargets,
				a_depthStencil);
			return true;
#endif
		}

		[[nodiscard]] bool GetViewportStateGuarded(
			ID3D11DeviceContext* a_context,
			UINT* a_count,
			D3D11_VIEWPORT* a_viewports,
			const NativeCallPoint a_afterCall) noexcept
		{
#if defined(_MSC_VER)
			__try {
				a_context->RSGetViewports(a_count, a_viewports);
				MaybeRaiseTestFault(a_afterCall);
				return true;
			} __except (1) {
				return false;
			}
#else
			a_context->RSGetViewports(a_count, a_viewports);
			return true;
#endif
		}

		[[nodiscard]] bool SetOutputMergerStateGuarded(
			ID3D11DeviceContext* a_context,
			ID3D11RenderTargetView* const* a_renderTargets,
			ID3D11DepthStencilView* a_depthStencil) noexcept
		{
#if defined(_MSC_VER)
			__try {
				MaybeRaiseTestFault(NativeCallPoint::kBeforeRestoreOMSet);
				a_context->OMSetRenderTargets(
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
					a_renderTargets,
					a_depthStencil);
				MaybeRaiseTestFault(NativeCallPoint::kAfterRestoreOMSet);
				return true;
			} __except (1) {
				return false;
			}
#else
			a_context->OMSetRenderTargets(
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
				a_renderTargets,
				a_depthStencil);
			return true;
#endif
		}

		[[nodiscard]] bool SetViewportStateGuarded(
			ID3D11DeviceContext* a_context,
			const UINT a_count,
			const D3D11_VIEWPORT* a_viewports) noexcept
		{
#if defined(_MSC_VER)
			__try {
				MaybeRaiseTestFault(NativeCallPoint::kBeforeRestoreViewportSet);
				a_context->RSSetViewports(a_count, a_viewports);
				MaybeRaiseTestFault(NativeCallPoint::kAfterRestoreViewportSet);
				return true;
			} __except (1) {
				return false;
			}
#else
			a_context->RSSetViewports(a_count, a_viewports);
			return true;
#endif
		}

		[[nodiscard]] bool SameFloatBits(const float a_left, const float a_right) noexcept
		{
			return std::memcmp(&a_left, &a_right, sizeof(float)) == 0;
		}

		[[nodiscard]] bool SameViewport(
			const D3D11_VIEWPORT& a_left,
			const D3D11_VIEWPORT& a_right) noexcept
		{
			return SameFloatBits(a_left.TopLeftX, a_right.TopLeftX) &&
			       SameFloatBits(a_left.TopLeftY, a_right.TopLeftY) &&
			       SameFloatBits(a_left.Width, a_right.Width) &&
			       SameFloatBits(a_left.Height, a_right.Height) &&
			       SameFloatBits(a_left.MinDepth, a_right.MinDepth) &&
			       SameFloatBits(a_left.MaxDepth, a_right.MaxDepth);
		}

		[[nodiscard]] bool ReleaseQueriedOutputState(
			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>& a_renderTargets,
			ID3D11DepthStencilView*& a_depthStencil) noexcept
		{
			auto ownedRenderTargets = a_renderTargets;
			ID3D11DepthStencilView* ownedDepthStencil = a_depthStencil;
			a_renderTargets = {};
			a_depthStencil = nullptr;

			bool succeeded = true;
			for (auto& target : ownedRenderTargets) {
				if (!ReleaseInterfaceGuarded(target, InterfaceOwnership::kQueried))
					succeeded = false;
			}
			if (!ReleaseInterfaceGuarded(
					ownedDepthStencil, InterfaceOwnership::kQueried))
				succeeded = false;
			return succeeded;
		}

	}

	RenderTarget::~RenderTarget()
	{
		Release();
		if (!bound)
			return;

		// Destruction supplies no trustworthy context with which to prove exact
		// pipeline restoration. If the owner violated that lifecycle contract,
		// detach the bounded set rather than letting WRL issue unguarded virtual
		// Releases after this body. The normal context-backed Release path above
		// performs exhaustive guarded cleanup; this terminal path intentionally
		// leaks its bounded identities at process teardown.
		for (auto& target : savedRTVs)
			(void)target.Detach();
		(void)savedDSV.Detach();
		(void)depthSRV.Detach();
		(void)readOnlyDepthDSV.Detach();
		(void)depthDSV.Detach();
		(void)depthTexture.Detach();
		(void)colorSRV.Detach();
		(void)colorRTV.Detach();
		(void)colorTexture.Detach();
		for (auto& views : reducedViews) {
			(void)views.colorRTV.Detach();
			(void)views.colorSRV.Detach();
			(void)views.depthDSV.Detach();
			(void)views.readOnlyDepthDSV.Detach();
			(void)views.depthSRV.Detach();
		}
		reducedViewCount = 0;
		(void)reducedMipTailTexture.Detach();
		(void)reducedMipTailSRV.Detach();
		renderMip = 0;
		savedViewportCount = 0;
		savedViewports = {};
		width = 0;
		height = 0;
		colorMipLevels = 0;
		colorFormat = DXGI_FORMAT_UNKNOWN;
		colorMipGenerationSupported = false;
	}

#if (defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) || defined(MIRRORS_OF_SKYRIM_FLEET_TEST)) && defined(_MSC_VER)
	void RenderTarget::ResetTestInstrumentation() noexcept
	{
		g_testFaultPoint = TestFaultPoint::kNone;
		g_testCleanupCounters = {};
	}

	void RenderTarget::InjectTestFaultOnce(const TestFaultPoint a_point) noexcept
	{
		g_testCleanupCounters = {};
		g_testFaultPoint = a_point;
	}

	RenderTarget::TestCleanupCounters RenderTarget::ReadTestCleanupCounters() noexcept
	{
		return g_testCleanupCounters;
	}

	bool RenderTarget::SavedStateEmptyForTesting() const noexcept
	{
		if (savedDSV || savedViewportCount != 0)
			return false;
		for (const auto& target : savedRTVs) {
			if (target)
				return false;
		}
		for (const auto& viewport : savedViewports) {
			const D3D11_VIEWPORT zero{};
			if (!SameViewport(viewport, zero))
				return false;
		}
		return true;
	}
#endif

	namespace
	{
		std::atomic<std::uint32_t> g_sharedDepthForwardedAccepts{ 0 };
	}

	std::uint32_t RenderTarget::SharedDepthForwardedAccepts() noexcept
	{
		return g_sharedDepthForwardedAccepts.load(std::memory_order_relaxed);
	}

	bool RenderTarget::Create(
		ID3D11Device* a_device,
		std::uint32_t a_width,
		std::uint32_t a_height,
		DXGI_FORMAT a_colorFormat,
		DebugChannel a_debugChannel,
		std::uint32_t a_debugInstance,
		const SharedDepth* a_sharedDepth,
		std::uint32_t a_reducedViews) noexcept
	{
		if (!a_device || bound || restoreStatus == RestoreStatus::kFault ||
			a_width == 0 || a_height == 0 ||
			a_colorFormat == DXGI_FORMAT_UNKNOWN ||
			a_width > kMaximumTargetDimension || a_height > kMaximumTargetDimension ||
			(a_debugChannel != DebugChannel::kMirror &&
				a_debugChannel != DebugChannel::kMirrorAlternate))
			return false;
		(void)a_debugInstance;
		if (a_reducedViews > kMaximumReducedViews ||
			(a_reducedViews != 0 && (a_sharedDepth || a_width < (1u << a_reducedViews) ||
				a_height < (1u << a_reducedViews))))
			return false;

		// The mirror is usually displayed much smaller than its capture. Require
		// the complete generated mip chain so movement does not shimmer at that
		// minification; unsupported formats fail closed.
		UINT formatSupport = 0;
		constexpr UINT requiredSupport = D3D11_FORMAT_SUPPORT_TEXTURE2D |
			D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
			D3D11_FORMAT_SUPPORT_RENDER_TARGET |
			D3D11_FORMAT_SUPPORT_MIP |
			D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
		if (FAILED(a_device->CheckFormatSupport(a_colorFormat, &formatSupport)) ||
			(formatSupport & requiredSupport) != requiredSupport)
			return false;
		D3D11_TEXTURE2D_DESC colorDescription{};
		colorDescription.Width = a_width;
		colorDescription.Height = a_height;
		colorDescription.MipLevels = 0;
		colorDescription.ArraySize = 1;
		colorDescription.Format = a_colorFormat;
		colorDescription.SampleDesc.Count = 1;
		colorDescription.Usage = D3D11_USAGE_DEFAULT;
		colorDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		colorDescription.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> newColorTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newColorRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newColorSRV;
		// Never silently substitute a single-mip target. Product behavior is
		// fail-closed if the device cannot create the required full chain.
		if (FAILED(a_device->CreateTexture2D(
				&colorDescription, nullptr, newColorTexture.GetAddressOf())))
			return false;
		D3D11_TEXTURE2D_DESC realizedColorDescription{};
		newColorTexture->GetDesc(&realizedColorDescription);
		if (realizedColorDescription.MipLevels <= 1 ||
			(realizedColorDescription.MiscFlags &
				D3D11_RESOURCE_MISC_GENERATE_MIPS) == 0)
			return false;
		if (FAILED(a_device->CreateRenderTargetView(
				newColorTexture.Get(), nullptr, newColorRTV.GetAddressOf())) ||
			FAILED(a_device->CreateShaderResourceView(
				newColorTexture.Get(), nullptr, newColorSRV.GetAddressOf())))
			return false;

		D3D11_TEXTURE2D_DESC depthDescription = colorDescription;
		depthDescription.MipLevels = 1u + a_reducedViews;
		depthDescription.MiscFlags = 0;
		depthDescription.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newDepthTexture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> newDepthDSV;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> newReadOnlyDepthDSV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newDepthSRV;
		if (a_sharedDepth) {
			if (!a_sharedDepth->texture || !a_sharedDepth->writable || !a_sharedDepth->readOnly || !a_sharedDepth->srv)
				return false;
			D3D11_TEXTURE2D_DESC shared{};
			a_sharedDepth->texture->GetDesc(&shared);
			Microsoft::WRL::ComPtr<ID3D11Device> sharedDevice;
			a_sharedDepth->texture->GetDevice(&sharedDevice);
			// Through a forwarding (wrapper) device every texture created via
			// a_device reports the real device instead, so the shared depth must
			// then agree with the colour texture just created through a_device.
			// 2026-09-10 AE + ENB: this strict compare refused every reuse of the
			// pool's depth after the first publication ("render-target-allocation").
			bool sameDevice = sharedDevice.Get() == a_device;
			if (!sameDevice && sharedDevice &&
				EngineDeviceIdentity::ForwardingDeviceAccepted()) {
				Microsoft::WRL::ComPtr<ID3D11Device> colourDevice;
				newColorTexture->GetDevice(&colourDevice);
				sameDevice = colourDevice && colourDevice.Get() != a_device &&
					colourDevice.Get() == sharedDevice.Get();
				if (sameDevice)
					g_sharedDepthForwardedAccepts.fetch_add(1, std::memory_order_relaxed);
			}
			if (!sameDevice || shared.Width != a_width || shared.Height != a_height ||
				shared.Format != DXGI_FORMAT_R24G8_TYPELESS || shared.ArraySize != 1 || shared.MipLevels != 1 ||
				shared.SampleDesc.Count != 1 || shared.SampleDesc.Quality != 0) return false;
			// Verify every view names this exact allocation; a same-sized alien
			// SRV/DSV must never grant authority to overwrite another receiver.
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			a_sharedDepth->writable->GetResource(&resource);
			if (resource.Get() != a_sharedDepth->texture.Get()) return false;
			a_sharedDepth->readOnly->GetResource(resource.ReleaseAndGetAddressOf());
			if (resource.Get() != a_sharedDepth->texture.Get()) return false;
			a_sharedDepth->srv->GetResource(resource.ReleaseAndGetAddressOf());
			if (resource.Get() != a_sharedDepth->texture.Get()) return false;
			newDepthTexture = a_sharedDepth->texture;
			D3D11_DEPTH_STENCIL_VIEW_DESC writable{}, readOnly{};
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			a_sharedDepth->writable->GetDesc(&writable); a_sharedDepth->readOnly->GetDesc(&readOnly);
			a_sharedDepth->srv->GetDesc(&srv);
			if (writable.Format != DXGI_FORMAT_D24_UNORM_S8_UINT || writable.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
				writable.Flags != 0 || writable.Texture2D.MipSlice != 0 ||
				readOnly.Format != writable.Format || readOnly.ViewDimension != writable.ViewDimension ||
				readOnly.Flags != D3D11_DSV_READ_ONLY_DEPTH || readOnly.Texture2D.MipSlice != 0 ||
				srv.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS || srv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				srv.Texture2D.MostDetailedMip != 0 || srv.Texture2D.MipLevels != 1) return false;
			newDepthDSV = a_sharedDepth->writable; newReadOnlyDepthDSV = a_sharedDepth->readOnly;
			newDepthSRV = a_sharedDepth->srv;
		} else {
		if (FAILED(a_device->CreateTexture2D(
				&depthDescription, nullptr, newDepthTexture.GetAddressOf())))
			return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDescription{};
		depthViewDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(a_device->CreateDepthStencilView(
				newDepthTexture.Get(), &depthViewDescription, newDepthDSV.GetAddressOf())))
			return false;
		// The SRV exposes only the depth plane (R24_UNORM_X8_TYPELESS), so depth
		// must be read-only while stencil may retain its native write semantics.
		// D3D11_DSV_READ_ONLY_STENCIL is unnecessary for this exact alias.
		depthViewDescription.Flags = D3D11_DSV_READ_ONLY_DEPTH;
		if (FAILED(a_device->CreateDepthStencilView(
				newDepthTexture.Get(), &depthViewDescription,
				newReadOnlyDepthDSV.GetAddressOf())))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDescription{};
		depthResourceDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		depthResourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		depthResourceDescription.Texture2D.MipLevels = 1;
		if (FAILED(a_device->CreateShaderResourceView(
				newDepthTexture.Get(), &depthResourceDescription, newDepthSRV.GetAddressOf())))
			return false;
		}

		std::array<ReducedViews, kMaximumReducedViews> newReducedViews;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newReducedMipTailTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newReducedMipTailSRV;
		if (a_reducedViews != 0 && realizedColorDescription.MipLevels <= a_reducedViews)
			return false;
		for (std::uint32_t mip = 1; mip <= a_reducedViews; ++mip) {
			auto& views = newReducedViews[mip - 1];
			D3D11_RENDER_TARGET_VIEW_DESC colorView{};
			colorView.Format = a_colorFormat;
			colorView.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			colorView.Texture2D.MipSlice = mip;
			D3D11_SHADER_RESOURCE_VIEW_DESC colorResourceView{};
			colorResourceView.Format = a_colorFormat;
			colorResourceView.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			colorResourceView.Texture2D.MostDetailedMip = mip;
			colorResourceView.Texture2D.MipLevels = realizedColorDescription.MipLevels - mip;
			D3D11_DEPTH_STENCIL_VIEW_DESC reducedDepthView{};
			reducedDepthView.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			reducedDepthView.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			reducedDepthView.Texture2D.MipSlice = mip;
			D3D11_SHADER_RESOURCE_VIEW_DESC reducedDepthResourceView{};
			reducedDepthResourceView.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			reducedDepthResourceView.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			reducedDepthResourceView.Texture2D.MostDetailedMip = mip;
			reducedDepthResourceView.Texture2D.MipLevels = 1;
			if (FAILED(a_device->CreateRenderTargetView(newColorTexture.Get(),
					&colorView, views.colorRTV.GetAddressOf())) ||
				FAILED(a_device->CreateShaderResourceView(newColorTexture.Get(),
					&colorResourceView, views.colorSRV.GetAddressOf())) ||
				FAILED(a_device->CreateDepthStencilView(newDepthTexture.Get(),
					&reducedDepthView, views.depthDSV.GetAddressOf())) ||
				FAILED(a_device->CreateShaderResourceView(newDepthTexture.Get(),
					&reducedDepthResourceView, views.depthSRV.GetAddressOf())))
				return false;
			reducedDepthView.Flags = D3D11_DSV_READ_ONLY_DEPTH;
			if (FAILED(a_device->CreateDepthStencilView(newDepthTexture.Get(),
					&reducedDepthView, views.readOnlyDepthDSV.GetAddressOf())))
				return false;
		}
		if (a_reducedViews != 0) {
			// WARP leaves the final 1x1 mip stale when GenerateMips starts below
			// mip 0. Finalize that tail through a tiny mip-0 resource, preserving
			// filtering and the cached base level without touching pipeline state.
			auto tailDescription = colorDescription;
			tailDescription.Width = a_width >> (realizedColorDescription.MipLevels - 2);
			tailDescription.Height = a_height >> (realizedColorDescription.MipLevels - 2);
			if (!tailDescription.Width) tailDescription.Width = 1;
			if (!tailDescription.Height) tailDescription.Height = 1;
			tailDescription.MipLevels = 2;
			if (FAILED(a_device->CreateTexture2D(&tailDescription, nullptr,
					newReducedMipTailTexture.GetAddressOf())) ||
				FAILED(a_device->CreateShaderResourceView(newReducedMipTailTexture.Get(), nullptr,
					newReducedMipTailSRV.GetAddressOf())))
				return false;
		}

		Release();
		if (bound || restoreStatus == RestoreStatus::kFault)
			return false;
		colorTexture = std::move(newColorTexture);
		colorRTV = std::move(newColorRTV);
		colorSRV = std::move(newColorSRV);
		depthTexture = std::move(newDepthTexture);
		depthDSV = std::move(newDepthDSV);
		readOnlyDepthDSV = std::move(newReadOnlyDepthDSV);
		depthSRV = std::move(newDepthSRV);
		reducedViews = std::move(newReducedViews);
		reducedViewCount = a_reducedViews;
		reducedMipTailTexture = std::move(newReducedMipTailTexture);
		reducedMipTailSRV = std::move(newReducedMipTailSRV);
		width = a_width;
		height = a_height;
		colorMipLevels = realizedColorDescription.MipLevels;
		colorFormat = a_colorFormat;
		colorMipGenerationSupported = colorMipLevels > 1 &&
			(realizedColorDescription.MiscFlags &
				D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0;
		restoreStatus = RestoreStatus::kNotRequired;
		lastBeginRejectedOutputMergerUAV = false;
		lastBeginNativeFaulted = false;

		if (a_debugChannel == DebugChannel::kMirrorAlternate) {
			SetDebugName(colorTexture.Get(), "MirrorsOfSkyrim.Mirror.ColorB");
			SetDebugName(colorRTV.Get(), "MirrorsOfSkyrim.Mirror.ColorB.RTV");
			SetDebugName(colorSRV.Get(), "MirrorsOfSkyrim.Mirror.ColorB.SRV");
			SetDebugName(depthTexture.Get(), "MirrorsOfSkyrim.Mirror.DepthB");
			SetDebugName(depthDSV.Get(), "MirrorsOfSkyrim.Mirror.DepthB.DSV");
			SetDebugName(readOnlyDepthDSV.Get(),
				"MirrorsOfSkyrim.Mirror.DepthB.ReadOnlyDSV");
			SetDebugName(depthSRV.Get(), "MirrorsOfSkyrim.Mirror.DepthB.SRV");
		} else {
			SetDebugName(colorTexture.Get(), "MirrorsOfSkyrim.Mirror.Color");
			SetDebugName(colorRTV.Get(), "MirrorsOfSkyrim.Mirror.ColorRTV");
			SetDebugName(colorSRV.Get(), "MirrorsOfSkyrim.Mirror.ColorSRV");
			SetDebugName(depthTexture.Get(), "MirrorsOfSkyrim.Mirror.Depth");
			SetDebugName(depthDSV.Get(), "MirrorsOfSkyrim.Mirror.DepthDSV");
			SetDebugName(readOnlyDepthDSV.Get(),
				"MirrorsOfSkyrim.Mirror.DepthReadOnlyDSV");
			SetDebugName(depthSRV.Get(), "MirrorsOfSkyrim.Mirror.DepthSRV");
		}
		return true;
	}

	void RenderTarget::Release(ID3D11DeviceContext* a_context) noexcept
	{
		bool faulted = restoreStatus == RestoreStatus::kFault;
		if (bound && End(a_context) == RestoreStatus::kFault)
			faulted = true;
		if (bound) {
			restoreStatus = RestoreStatus::kFault;
			return;
		}

		if (!ResetSavedState())
			faulted = true;
		if (!ReleaseOwnedResources())
			faulted = true;
		if (faulted)
			restoreStatus = RestoreStatus::kFault;
	}

	bool RenderTarget::SelectRenderMip(std::uint32_t a_mip) noexcept
	{
		if (bound || !Ready() || a_mip > reducedViewCount)
			return false;
		if (a_mip != 0) {
			const auto& views = reducedViews[a_mip - 1];
			if (!views.colorRTV || !views.colorSRV || !views.depthDSV ||
				!views.readOnlyDepthDSV || !views.depthSRV ||
				!reducedMipTailTexture || !reducedMipTailSRV)
				return false;
		}
		renderMip = a_mip;
		return true;
	}

	bool RenderTarget::Begin(ID3D11DeviceContext* a_context, const float a_clearColor[4]) noexcept
	{
		lastBeginRejectedOutputMergerUAV = false;
		lastBeginNativeFaulted = false;
		if (!a_context || !a_clearColor || bound ||
			restoreStatus == RestoreStatus::kFault || !Ready())
			return false;

		// A stale read-only snapshot owns no D3D mutation, but its AddRefs still
		// require guarded, exhaustive cleanup before a fresh transaction.
		if (!ResetSavedState()) {
			lastBeginNativeFaulted = true;
			restoreStatus = RestoreStatus::kFault;
			return false;
		}

		// OM RTVs and pixel UAVs share absolute output slots. UAV append/counter
		// positions cannot be queried and exactly restored. A bound UAV is an
		// ordinary fail-closed rejection; a query/cleanup fault is terminal.
		const auto uavState = D3D11OutputMergerState::QueryUAVState(a_context);
		if (uavState == D3D11OutputMergerState::UAVState::kBound) {
			lastBeginRejectedOutputMergerUAV = true;
			return false;
		}
		if (uavState == D3D11OutputMergerState::UAVState::kFault) {
			lastBeginNativeFaulted = true;
			restoreStatus = RestoreStatus::kFault;
			return false;
		}

		std::array<ID3D11RenderTargetView*,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawRTVs{};
		ID3D11DepthStencilView* rawDSV = nullptr;
		const bool outputStateCaptured = GetOutputMergerStateGuarded(
			a_context,
			rawRTVs.data(),
			&rawDSV,
			NativeCallPoint::kAfterCaptureOMQuery);
		if (!outputStateCaptured) {
			(void)ReleaseQueriedOutputState(rawRTVs, rawDSV);
			lastBeginNativeFaulted = true;
			restoreStatus = RestoreStatus::kFault;
			return false;
		}

		std::array<D3D11_VIEWPORT,
			D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> rawViewports{};
		UINT rawViewportCount = static_cast<UINT>(rawViewports.size());
		const bool viewportStateCaptured = GetViewportStateGuarded(
			a_context,
			&rawViewportCount,
			rawViewports.data(),
			NativeCallPoint::kAfterCaptureViewportQuery);
		if (!viewportStateCaptured ||
			rawViewportCount > static_cast<UINT>(rawViewports.size())) {
			(void)ReleaseQueriedOutputState(rawRTVs, rawDSV);
			lastBeginNativeFaulted = true;
			restoreStatus = RestoreStatus::kFault;
			return false;
		}

		for (std::size_t index = 0; index < savedRTVs.size(); ++index) {
			ID3D11RenderTargetView* const retained = rawRTVs[index];
			rawRTVs[index] = nullptr;
			savedRTVs[index].Attach(retained);
		}
		ID3D11DepthStencilView* const retainedDSV = rawDSV;
		rawDSV = nullptr;
		savedDSV.Attach(retainedDSV);
		savedViewports = rawViewports;
		savedViewportCount = rawViewportCount;
		bound = true;
		restoreStatus = RestoreStatus::kPending;
		Rebind(a_context);
		a_context->ClearRenderTargetView(ColorRTV(), a_clearColor);
		a_context->ClearDepthStencilView(
			DepthDSV(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		return true;
	}

	bool RenderTarget::ClearDepthStencil(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || !bound || restoreStatus != RestoreStatus::kPending ||
			!Ready())
			return false;

		a_context->ClearDepthStencilView(
			DepthDSV(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		return true;
	}

	bool RenderTarget::GenerateColorMips(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || bound || restoreStatus == RestoreStatus::kFault ||
			!colorSRV)
			return false;
		if (!colorMipGenerationSupported || colorMipLevels <= 1)
			return false;
		a_context->GenerateMips(ColorSRV());
		if (renderMip != 0 && colorMipLevels > renderMip + 1) {
			a_context->CopySubresourceRegion(reducedMipTailTexture.Get(), 0, 0, 0, 0,
				colorTexture.Get(), colorMipLevels - 2, nullptr);
			a_context->GenerateMips(reducedMipTailSRV.Get());
			a_context->CopySubresourceRegion(colorTexture.Get(), colorMipLevels - 1, 0, 0, 0,
				reducedMipTailTexture.Get(), 1, nullptr);
		}
		return true;
	}

	void RenderTarget::Rebind(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || !bound || restoreStatus != RestoreStatus::kPending ||
			!Ready())
			return;

		ID3D11RenderTargetView* target = ColorRTV();
		a_context->OMSetRenderTargets(1, &target, DepthDSV());
		BindFullTargetViewports(a_context, Width(), Height());
	}

	void RenderTarget::RebindReadOnlyDepth(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context || !bound || restoreStatus != RestoreStatus::kPending ||
			!Ready())
			return;

		ID3D11RenderTargetView* target = ColorRTV();
		a_context->OMSetRenderTargets(1, &target, ReadOnlyDepthDSV());
		BindFullTargetViewports(a_context, Width(), Height());
	}


	RenderTarget::RestoreStatus RenderTarget::End(ID3D11DeviceContext* a_context) noexcept
	{
		if (!bound) {
			// A pre-mutation capture fault may still have retained query AddRefs.
			if (!ResetSavedState())
				restoreStatus = RestoreStatus::kFault;
			return restoreStatus;
		}

		bool faulted = restoreStatus == RestoreStatus::kFault;
		if (!a_context) {
			restoreStatus = RestoreStatus::kFault;
			return restoreStatus;
		}

		std::array<ID3D11RenderTargetView*,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> restoreRTVs{};
		for (std::size_t index = 0; index < savedRTVs.size(); ++index)
			restoreRTVs[index] = savedRTVs[index].Get();

		const bool outputSetSucceeded = SetOutputMergerStateGuarded(
			a_context, restoreRTVs.data(), savedDSV.Get());
		const D3D11_VIEWPORT* const restoreViewports =
			savedViewportCount > 0 ? savedViewports.data() : nullptr;
		const bool viewportSetSucceeded = SetViewportStateGuarded(
			a_context, savedViewportCount, restoreViewports);
		if (!outputSetSucceeded || !viewportSetSucceeded)
			faulted = true;

		std::array<ID3D11RenderTargetView*,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> readbackRTVs{};
		ID3D11DepthStencilView* readbackDSV = nullptr;
		const bool outputReadbackSucceeded = GetOutputMergerStateGuarded(
			a_context,
			readbackRTVs.data(),
			&readbackDSV,
			NativeCallPoint::kAfterRestoreOMReadback);
		bool outputRestoredExactly = outputReadbackSucceeded;
		if (outputReadbackSucceeded) {
			for (std::size_t index = 0; index < savedRTVs.size(); ++index) {
				if (readbackRTVs[index] != savedRTVs[index].Get())
					outputRestoredExactly = false;
			}
			if (readbackDSV != savedDSV.Get())
				outputRestoredExactly = false;
		}
		const bool outputReadbackReleased =
			ReleaseQueriedOutputState(readbackRTVs, readbackDSV);
		if (!outputReadbackSucceeded || !outputReadbackReleased)
			faulted = true;

		std::array<D3D11_VIEWPORT,
			D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
			readbackViewports{};
		UINT readbackViewportCount = static_cast<UINT>(readbackViewports.size());
		const bool viewportReadbackSucceeded = GetViewportStateGuarded(
			a_context,
			&readbackViewportCount,
			readbackViewports.data(),
			NativeCallPoint::kAfterRestoreViewportReadback);
		bool viewportsRestoredExactly = viewportReadbackSucceeded &&
			readbackViewportCount <= static_cast<UINT>(readbackViewports.size()) &&
			readbackViewportCount == savedViewportCount;
		if (viewportsRestoredExactly) {
			for (UINT index = 0; index < savedViewportCount; ++index) {
				if (!SameViewport(readbackViewports[index], savedViewports[index]))
					viewportsRestoredExactly = false;
			}
		}
		if (!viewportReadbackSucceeded)
			faulted = true;

		if (!outputRestoredExactly || !viewportsRestoredExactly) {
			// Keep the exact saved identities and scalars for a later best-effort
			// retry. The transaction remains terminally faulted and cannot render.
			restoreStatus = RestoreStatus::kFault;
			return restoreStatus;
		}

		// Exact D3D readback ends restoration ownership. Tombstone every saved
		// identity/scalar before making any potentially-faulting Release call.
		bound = false;
		if (!ResetSavedState())
			faulted = true;
		restoreStatus = faulted ? RestoreStatus::kFault : RestoreStatus::kRestored;
		return restoreStatus;
	}

	bool RenderTarget::TryRetainUnboundColorSRV(
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& output) const noexcept
	{
		output.Reset();
		if (bound || !Ready() || width == 0 || height == 0)
			return false;
		output = ColorSRV();
		return output != nullptr;
	}

	bool RenderTarget::TryRetainUnboundDepthSRV(
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& output) const noexcept
	{
		output.Reset();
		if (bound || !Ready() || !depthSRV || width == 0 || height == 0)
			return false;
		output = DepthSRV();
		return output != nullptr;
	}

	bool RenderTarget::ResetSavedState() noexcept
	{
		std::array<ID3D11RenderTargetView*,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> ownedRTVs{};
		for (std::size_t index = 0; index < savedRTVs.size(); ++index)
			ownedRTVs[index] = savedRTVs[index].Detach();
		ID3D11DepthStencilView* ownedDSV = savedDSV.Detach();
		savedViewportCount = 0;
		savedViewports = {};

		bool succeeded = true;
		for (auto& target : ownedRTVs) {
			if (!ReleaseInterfaceGuarded(target, InterfaceOwnership::kSaved))
				succeeded = false;
		}
		if (!ReleaseInterfaceGuarded(ownedDSV, InterfaceOwnership::kSaved))
			succeeded = false;
		return succeeded;
	}

	bool RenderTarget::ReleaseOwnedResources() noexcept
	{
		// Detach and clear all observable ownership/scalars before the first
		// virtual Release call. Every interface is then attempted independently.
		ID3D11ShaderResourceView* ownedDepthSRV = depthSRV.Detach();
		ID3D11DepthStencilView* ownedReadOnlyDepthDSV = readOnlyDepthDSV.Detach();
		ID3D11DepthStencilView* ownedDepthDSV = depthDSV.Detach();
		ID3D11Texture2D* ownedDepthTexture = depthTexture.Detach();
		ID3D11ShaderResourceView* ownedColorSRV = colorSRV.Detach();
		ID3D11RenderTargetView* ownedColorRTV = colorRTV.Detach();
		ID3D11Texture2D* ownedColorTexture = colorTexture.Detach();
		std::array<ID3D11RenderTargetView*, kMaximumReducedViews> ownedReducedColorRTVs{};
		std::array<ID3D11ShaderResourceView*, kMaximumReducedViews> ownedReducedColorSRVs{};
		std::array<ID3D11DepthStencilView*, kMaximumReducedViews> ownedReducedDepthDSVs{};
		std::array<ID3D11DepthStencilView*, kMaximumReducedViews> ownedReducedReadOnlyDepthDSVs{};
		std::array<ID3D11ShaderResourceView*, kMaximumReducedViews> ownedReducedDepthSRVs{};
		for (std::size_t index = 0; index < reducedViews.size(); ++index) {
			ownedReducedColorRTVs[index] = reducedViews[index].colorRTV.Detach();
			ownedReducedColorSRVs[index] = reducedViews[index].colorSRV.Detach();
			ownedReducedDepthDSVs[index] = reducedViews[index].depthDSV.Detach();
			ownedReducedReadOnlyDepthDSVs[index] = reducedViews[index].readOnlyDepthDSV.Detach();
			ownedReducedDepthSRVs[index] = reducedViews[index].depthSRV.Detach();
		}
		reducedViewCount = 0;
		ID3D11Texture2D* ownedReducedMipTailTexture = reducedMipTailTexture.Detach();
		ID3D11ShaderResourceView* ownedReducedMipTailSRV = reducedMipTailSRV.Detach();
		width = 0;
		height = 0;
		colorMipLevels = 0;
		colorFormat = DXGI_FORMAT_UNKNOWN;
		colorMipGenerationSupported = false;

		bool succeeded = true;
		renderMip = 0;
		for (std::size_t index = 0; index < kMaximumReducedViews; ++index) {
			if (!ReleaseInterfaceGuarded(ownedReducedColorRTVs[index], InterfaceOwnership::kOwned))
				succeeded = false;
			if (!ReleaseInterfaceGuarded(ownedReducedColorSRVs[index], InterfaceOwnership::kOwned))
				succeeded = false;
			if (!ReleaseInterfaceGuarded(ownedReducedDepthDSVs[index], InterfaceOwnership::kOwned))
				succeeded = false;
			if (!ReleaseInterfaceGuarded(ownedReducedReadOnlyDepthDSVs[index], InterfaceOwnership::kOwned))
				succeeded = false;
			if (!ReleaseInterfaceGuarded(ownedReducedDepthSRVs[index], InterfaceOwnership::kOwned))
				succeeded = false;
		}
		if (!ReleaseInterfaceGuarded(ownedReducedMipTailSRV, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedReducedMipTailTexture, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedDepthSRV, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(
				ownedReadOnlyDepthDSV, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedDepthDSV, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedDepthTexture, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedColorSRV, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedColorRTV, InterfaceOwnership::kOwned))
			succeeded = false;
		if (!ReleaseInterfaceGuarded(ownedColorTexture, InterfaceOwnership::kOwned))
			succeeded = false;
		return succeeded;
	}
}
