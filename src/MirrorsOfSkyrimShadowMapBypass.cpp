#include "PCH.h"

#include <cstring>
#include <wrl/client.h>

#include "MirrorsOfSkyrimShadowMapBypass.h"

#include "MirrorsOfSkyrimRecognition.h"
#include "MirrorShadowMapBypassGate.h"
#include "MirrorShadowMaskAE17104Contract.h"
#include "MirrorPlayerDrawPassProbe.h"
#include "SecondView.h"
#include "MirrorSunShadows.h"

namespace MirrorShadowMapBypass
{
	namespace
	{
		constexpr UINT kShadowMaskSlot = 14;

		struct Resources
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device{};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> neutralSRV{};
		};

		struct PassState
		{
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context{};
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> originalSRV{};
			DWORD threadID{ 0 };
			bool active{ false };
			bool neutralBound{ false };
			std::uint32_t technique{ 0 };
			std::uint8_t lightCount{ 0 };
			std::uint8_t shadowLightCount{ 0 };
			std::uintptr_t geometry{ 0 };
			bool passValuesAvailable{ false };
		};

		struct Counters
		{
			std::atomic_uint64_t setupCallbacks{ 0 };
			std::atomic_uint64_t armedPasses{ 0 };
			std::atomic_uint64_t dirtyCallbacks{ 0 };
			std::atomic_uint64_t neutralBinds{ 0 };
			std::atomic_uint64_t alreadyNeutral{ 0 };
			std::atomic_uint64_t missingShadowBindings{ 0 };
			std::atomic_uint64_t normalRestores{ 0 };
			std::atomic_uint64_t cleanupRestores{ 0 };
			std::atomic_uint64_t faults{ 0 };
		};

		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_prepared{ false };
		// SE/VR retain their existing activation. AE 1.7.104 requires an explicit
		// comparison request and an exact native binding-byte match; every other
		// AE configuration stands down without affecting mirror activation.
		std::atomic_bool g_runtimeUnsupported{ false };
		std::atomic_bool g_ae17104ComparisonRequested{ false };
		std::atomic_bool g_ae17104ComparisonActive{ false };
		std::array<std::atomic_uint32_t, 8> g_comparisonSamples{};
		std::atomic_bool g_wallMirrorRequested{ false };
		std::atomic_bool g_handMirrorRequested{ false };
		std::atomic_bool g_faulted{ false };
		thread_local PassState g_pass{};
		Counters g_counters{};

		[[nodiscard]] bool AE17104ComparisonContractReady() noexcept
		{
			const bool requested = g_ae17104ComparisonRequested.load(std::memory_order_acquire);
			const bool exactRuntime = SupportedRuntimePolicy::IsExactAE17104Runtime();
			if (!requested || !exactRuntime)
				return false;
			std::array<std::uint8_t, MirrorShadowMaskAE17104Contract::kBindingBytes.size()> bytes{};
#if defined(_MSC_VER)
			__try {
#endif
				std::memcpy(bytes.data(), reinterpret_cast<const void*>(
					REL::Module::get().base() + MirrorShadowMaskAE17104Contract::kBindingRVA), bytes.size());
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#endif
			return MirrorShadowMaskAE17104Contract::Accept(requested, exactRuntime, bytes);
		}

		void CopyComparisonPassValues(const RE::BSRenderPass* pass) noexcept
		{
			if (!pass)
				return;
#if defined(_MSC_VER)
			__try {
#endif
				g_pass.technique = pass->passEnum;
				g_pass.lightCount = pass->numLights;
				g_pass.shadowLightCount = pass->numShadowLights;
				g_pass.geometry = reinterpret_cast<std::uintptr_t>(pass->geometry);
				g_pass.passValuesAvailable = true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_pass.passValuesAvailable = false;
			}
#endif
		}

		// Bounded CPU-only description reads; no GPU copy, Map, shader getter,
		// symbol resolution or retained game-object pointer is involved.
		[[nodiscard]] bool DescribeComparisonTexture(
			ID3D11ShaderResourceView* view, D3D11_TEXTURE2D_DESC& description) noexcept
		{
			ID3D11Resource* resource = nullptr;
			ID3D11Texture2D* texture = nullptr;
			bool described = false;
#if defined(_MSC_VER)
			__try {
#endif
				view->GetResource(&resource);
				if (resource && SUCCEEDED(resource->QueryInterface(
						__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
					texture->GetDesc(&description);
					described = true;
				}
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				described = false;
			}
#endif
			// Release each getter-owned reference, including either partial result.
			for (IUnknown* object : { static_cast<IUnknown*>(texture), static_cast<IUnknown*>(resource) }) {
				if (!object)
					continue;
#if defined(_MSC_VER)
				__try {
#endif
					object->Release();
#if defined(_MSC_VER)
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					described = false;
				}
#endif
			}
			return described;
		}

		void LogComparisonBinding(ID3D11ShaderResourceView* original) noexcept
		{
			if (!g_ae17104ComparisonActive.load(std::memory_order_acquire))
				return;
			const bool raisedPhysicalHand = SecondView::IsInsideExactHandPhysicalRasterClip();
			// Bit 14 belongs to the descriptor after subtracting the lighting base.
			const auto descriptor = g_pass.technique - 0x4800002Du;
			const auto sampleClass = (raisedPhysicalHand ? 4u : 0u) |
				((descriptor & 0x4000u) != 0 ? 2u : 0u) | (g_pass.shadowLightCount != 0 ? 1u : 0u);
			auto& count = g_comparisonSamples[sampleClass];
			if (count.load(std::memory_order_relaxed) >= 2 ||
				count.fetch_add(1, std::memory_order_relaxed) >= 2)
				return;
			D3D11_TEXTURE2D_DESC description{};
			const bool described = DescribeComparisonTexture(original, description);
			try {
				logger::info(
					"[MirrorsOfSkyrim][ShadowStability][AE-comparison] raisedPhysicalHand={} "
					"passAvailable={} technique=0x{:08X} geometry=0x{:X} lights/shadowLights={}/{} "
					"originalT14=0x{:X} described={} texture={}x{} array={} format={} neutralBindVerified=true",
					raisedPhysicalHand, g_pass.passValuesAvailable, g_pass.technique, g_pass.geometry,
					g_pass.lightCount, g_pass.shadowLightCount, reinterpret_cast<std::uintptr_t>(original),
					described, description.Width, description.Height, description.ArraySize,
					static_cast<unsigned>(description.Format));
			} catch (...) {
			}
		}

		[[nodiscard]] Resources* GetResources() noexcept
		{
			static Resources* resources = new (std::nothrow) Resources{};
			return resources;
		}

		[[nodiscard]] bool GetShaderResourceSEH(
			ID3D11DeviceContext* context,
			ID3D11ShaderResourceView** output) noexcept
		{
			if (!context || !output)
				return false;
			*output = nullptr;
#if defined(_MSC_VER)
			__try {
				context->PSGetShaderResources(kShadowMaskSlot, 1, output);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#else
			context->PSGetShaderResources(kShadowMaskSlot, 1, output);
			return true;
#endif
		}

		[[nodiscard]] bool SetShaderResourceSEH(
			ID3D11DeviceContext* context,
			ID3D11ShaderResourceView* view) noexcept
		{
			if (!context)
				return false;
#if defined(_MSC_VER)
			__try {
				context->PSSetShaderResources(kShadowMaskSlot, 1, &view);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#else
			context->PSSetShaderResources(kShadowMaskSlot, 1, &view);
			return true;
#endif
		}

		[[nodiscard]] bool VerifyShaderResource(
			ID3D11DeviceContext* context,
			ID3D11ShaderResourceView* expected) noexcept
		{
			ID3D11ShaderResourceView* observed = nullptr;
			if (!GetShaderResourceSEH(context, &observed))
				return false;
			const bool matched = observed == expected;
			if (observed)
				observed->Release();
			return matched;
		}

		[[nodiscard]] bool EnsureNeutralResource(
			ID3D11DeviceContext* context,
			Resources& resources) noexcept
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device{};
			context->GetDevice(device.GetAddressOf());
			if (!device)
				return false;
			if (resources.device && resources.device.Get() != device.Get())
				return false;
			if (resources.neutralSRV)
				return true;

			D3D11_TEXTURE2D_DESC description{};
			description.Width = 1;
			description.Height = 1;
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			constexpr std::uint32_t white = 0xFFFFFFFFu;
			D3D11_SUBRESOURCE_DATA initialData{};
			initialData.pSysMem = &white;
			initialData.SysMemPitch = sizeof(white);

			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
			if (FAILED(device->CreateTexture2D(
					&description, &initialData, texture.GetAddressOf())))
				return false;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view{};
			if (FAILED(device->CreateShaderResourceView(
					texture.Get(), nullptr, view.GetAddressOf())))
				return false;

			resources.device = std::move(device);
			resources.texture = std::move(texture);
			resources.neutralSRV = std::move(view);
			return true;
		}

		void ClearPass() noexcept
		{
			g_pass.originalSRV.Reset();
			g_pass.context.Reset();
			g_pass.threadID = 0;
			g_pass.active = false;
			g_pass.neutralBound = false;
			g_pass.passValuesAvailable = false;
			g_pass.technique = 0;
			g_pass.lightCount = 0;
			g_pass.shadowLightCount = 0;
			g_pass.geometry = 0;
		}

		[[nodiscard]] bool RestoreAndClear(bool cleanup) noexcept
		{
			bool restored = true;
			if (g_pass.neutralBound) {
				auto* const original = g_pass.originalSRV.Get();
				restored = SetShaderResourceSEH(g_pass.context.Get(), original) &&
					VerifyShaderResource(g_pass.context.Get(), original);
				if (restored) {
					if (cleanup)
						g_counters.cleanupRestores.fetch_add(1, std::memory_order_relaxed);
					else
						g_counters.normalRestores.fetch_add(1, std::memory_order_relaxed);
				}
			}
			const auto disposition = MirrorShadowMapBypassGate::ClassifyRestore(
				g_pass.neutralBound, restored);
			if (disposition == MirrorShadowMapBypassGate::RestoreDisposition::kClearPass)
				ClearPass();
			return restored;
		}

		void FailStop(const char* reason) noexcept
		{
			const bool first = !g_faulted.exchange(true, std::memory_order_acq_rel);
			g_counters.faults.fetch_add(1, std::memory_order_relaxed);
			if (first) {
				try {
					logger::critical(
						"[MirrorsOfSkyrim][ShadowStability] fail-stop: {}",
						reason ? reason : "unknown");
				} catch (...) {
					// Diagnostics must never violate this render-path fail-stop's
					// noexcept boundary.
				}
			}
		}
	}

	void ConfigureAE17104Comparison(bool requested) noexcept
	{
		// Configuration belongs to pre-activation; never change a live transaction.
		if (!g_prepared.load(std::memory_order_acquire) &&
			!g_enabled.load(std::memory_order_acquire))
			g_ae17104ComparisonRequested.store(requested, std::memory_order_release);
	}

	void OnDataLoaded(bool wallMirrorRequested, bool handMirrorRequested)
	{
		g_enabled.store(false, std::memory_order_release);
		g_prepared.store(false, std::memory_order_release);
		g_runtimeUnsupported.store(false, std::memory_order_release);
		g_ae17104ComparisonActive.store(false, std::memory_order_release);
		g_wallMirrorRequested.store(wallMirrorRequested, std::memory_order_release);
		g_handMirrorRequested.store(handMirrorRequested, std::memory_order_release);
		g_faulted.store(false, std::memory_order_release);
		ClearPass();

		if (!wallMirrorRequested && !handMirrorRequested) {
			logger::info(
				"[MirrorsOfSkyrim][ShadowStability] no mirror role requested preparation");
			return;
		}
		const bool exactSE1597 = REL::Module::IsSE() &&
			REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 };
		const bool exactVR1415 = SupportedRuntimePolicy::IsExactVRRuntime();
		const bool aeComparisonReady = AE17104ComparisonContractReady();
		if (!exactSE1597 && !exactVR1415 && !aeComparisonReady) {
			// Stand down instead of leaving the module half-armed: the caller
			// commits the mirror channel regardless, and an optional stabilizer
			// must never terminate an otherwise valid activation.
			g_runtimeUnsupported.store(true, std::memory_order_release);
			logger::warn(
				"[MirrorsOfSkyrim][ShadowStability] no admitted t14 contract: SE 1.5.97 / VR 1.4.15 or explicit byte-verified AE 1.7.104 comparison required; stabilizer stands down and mirror activation continues (AE comparison requested={})",
				g_ae17104ComparisonRequested.load(std::memory_order_acquire));
			return;
		}
		g_ae17104ComparisonActive.store(aeComparisonReady, std::memory_order_release);
		if (aeComparisonReady)
			logger::warn("[MirrorsOfSkyrim][ShadowStability] AE 1.7.104 comparison prepared: only reflected lighting t14 is neutralized; visual correctness is unverified");

		const bool lightingObserverInstalled =
			MirrorRecognition::EnsureLightingSetupObserverInstalled();
		const bool playerProbeInstalled = PlayerDrawPassProbe::EnsureInstalled();
		if (!lightingObserverInstalled || !playerProbeInstalled) {
			FailStop(!lightingObserverInstalled ?
				"lighting setup observer installation failed" :
				"player draw probe installation failed");
			return;
		}
		g_prepared.store(true, std::memory_order_release);
		logger::info(
			"[MirrorsOfSkyrim][ShadowStability] prepared (wallRequested={} handRequested={})",
			wallMirrorRequested, handMirrorRequested);
	}

	void OnActivationCommitted(
		bool wallMirrorEnabled,
		bool handMirrorEnabled) noexcept
	{
		if (g_runtimeUnsupported.load(std::memory_order_acquire)) {
			// Reported once at DataLoaded; the mirror channel owns its own
			// correctness and simply runs without the shadow-mask stabilizer.
			return;
		}
		const bool wallRequested =
			g_wallMirrorRequested.load(std::memory_order_acquire);
		const bool handRequested =
			g_handMirrorRequested.load(std::memory_order_acquire);
		if ((wallMirrorEnabled && !wallRequested) ||
			(handMirrorEnabled && !handRequested)) {
			FailStop("mirror activation committed for an unprepared role");
			return;
		}
		const bool wallAdmitted = wallMirrorEnabled && wallRequested;
		const bool handAdmitted = handMirrorEnabled && handRequested;
		if (!wallAdmitted && !handAdmitted)
			return;
		if (!g_prepared.load(std::memory_order_acquire) ||
			g_faulted.load(std::memory_order_acquire)) {
			FailStop("mirror activation committed without prepared lighting hooks");
			return;
		}
		if (!g_enabled.exchange(true, std::memory_order_acq_rel)) {
			try {
				logger::info(
					"[MirrorsOfSkyrim][ShadowStability] activated (wall={} hand={})",
					wallAdmitted, handAdmitted);
			} catch (...) {
				FailStop("activation logging failed");
			}
		}
	}

	void OnGameLoaded() noexcept
	{
		(void)OnPrivateCaptureCleanup();
	}

	void OnSetupGeometryReturned(const RE::BSRenderPass* pass) noexcept
	{
		SecondView::ObserveLightingEvidence(pass);
		MirrorSunShadows::OnSetupGeometry(pass);
		g_counters.setupCallbacks.fetch_add(1, std::memory_order_relaxed);
		if (!IsEnabled() || !SecondView::IsInsideMirrorPrimaryCapture())
			return;
		if (g_pass.active) {
			const bool restored = RestoreAndClear(true);
			FailStop(restored ? "nested lighting setup" : "nested lighting setup restore failed");
			return;
		}
		g_pass.active = true;
		g_pass.threadID = GetCurrentThreadId();
		if (g_ae17104ComparisonActive.load(std::memory_order_acquire))
			CopyComparisonPassValues(pass);
		g_counters.armedPasses.fetch_add(1, std::memory_order_relaxed);
	}

	void OnSetDirtyStatesCommitted(ID3D11DeviceContext* context) noexcept
	{
		g_counters.dirtyCallbacks.fetch_add(1, std::memory_order_relaxed);
		if (!IsEnabled() || !g_pass.active)
			return;

		auto* resources = GetResources();
		const bool sameContext = context &&
			(!g_pass.context || g_pass.context.Get() == context);
		ID3D11ShaderResourceView* observedRaw = nullptr;
		if (!sameContext || !GetShaderResourceSEH(context, &observedRaw)) {
			const bool restored = RestoreAndClear(true);
			FailStop(restored ? "invalid dirty-state context" :
				"invalid dirty-state context and restore failed");
			return;
		}
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> observed{};
		observed.Attach(observedRaw);
		const bool neutralAlreadyBound = resources && resources->neutralSRV &&
			observed.Get() == resources->neutralSRV.Get();
		const auto decision = MirrorShadowMapBypassGate::Decide({
			.enabled = g_enabled.load(std::memory_order_acquire),
			.faulted = g_faulted.load(std::memory_order_acquire),
			.passActive = g_pass.active,
			.mirrorPrimary = SecondView::IsInsideMirrorPrimaryCapture(),
			.graphicsCommit = true,
			.sameThread = g_pass.threadID == GetCurrentThreadId(),
			.sameContext = sameContext,
			.hasShadowBinding = observed != nullptr,
			.neutralAlreadyBound = neutralAlreadyBound,
			.neutralWasBoundByUs = g_pass.neutralBound
		});
		if (decision == MirrorShadowMapBypassGate::Decision::kIgnore)
			return;
		if (decision == MirrorShadowMapBypassGate::Decision::kFailStop) {
			const bool restored = RestoreAndClear(true);
			FailStop(restored ? "mirror-primary commit identity changed" :
				"mirror-primary commit identity changed and restore failed");
			return;
		}
		if (decision == MirrorShadowMapBypassGate::Decision::kAlreadyNeutral) {
			g_counters.alreadyNeutral.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (decision == MirrorShadowMapBypassGate::Decision::kNoShadowBinding) {
			g_counters.missingShadowBindings.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!resources || !EnsureNeutralResource(context, *resources)) {
			const bool restored = RestoreAndClear(true);
			FailStop(restored ? "neutral shadow resource creation failed" :
				"neutral shadow resource creation and restore failed");
			return;
		}

		g_pass.context = context;
		g_pass.originalSRV = observed;
		// From this point onward restoration is mandatory even if the neutral
		// bind itself faults or cannot be verified: PSSetShaderResources may
		// have committed before reporting failure.
		g_pass.neutralBound = true;
		auto* const neutral = resources->neutralSRV.Get();
		if (!SetShaderResourceSEH(context, neutral) ||
			!VerifyShaderResource(context, neutral)) {
			const bool restored = RestoreAndClear(true);
			FailStop(restored ? "neutral shadow binding failed verification" :
				"neutral shadow binding failed verification and t14 restore failed");
			return;
		}
		g_counters.neutralBinds.fetch_add(1, std::memory_order_relaxed);
		LogComparisonBinding(observed.Get());
	}

	void OnBeforeRestoreGeometry() noexcept
	{
		if (!MirrorSunShadows::Restore()) FailStop("sun-shadow shader restore failed");
		if (!g_pass.active)
			return;
		if (!IsEnabled() || !SecondView::IsInsideMirrorPrimaryCapture() ||
			g_pass.threadID != GetCurrentThreadId()) {
			const bool restored = RestoreAndClear(true);
			FailStop(restored ? "restore identity changed" :
				"restore identity changed and t14 restore failed");
			return;
		}
		if (!RestoreAndClear(false))
			FailStop("exact t14 restore failed before native RestoreGeometry");
	}

	void OnAfterRestoreGeometry() noexcept
	{
		if (!g_pass.active)
			return;
		const bool restored = RestoreAndClear(true);
		FailStop(restored ? "pass remained active after native RestoreGeometry" :
			"post-RestoreGeometry cleanup failed");
	}

	bool OnPrivateCaptureCleanup() noexcept
	{
		const bool sunRestored = MirrorSunShadows::Restore();
		if (!g_pass.active && !g_pass.neutralBound)
			return sunRestored;
		if (RestoreAndClear(true))
			return sunRestored;

		FailStop("private-pass fallback could not restore t14");
		// One final best-effort retry reduces the chance of leaking the neutral
		// mask into the main view.  RestoreAndClear deliberately retains the
		// original state again if this retry also fails.  The first failure is
		// still reported to SecondView regardless of the retry result.
		(void)RestoreAndClear(true);
		return false;
	}

	bool IsEnabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire) &&
			!g_faulted.load(std::memory_order_acquire);
	}

	void LogDiagnostics(const char* reason)
	{
		MirrorSunShadows::LogDiagnostics();
		logger::info(
			"[MirrorsOfSkyrim][ShadowStability] diagnostics ({}) enabled={} faulted={} setup/armed={}/{} dirty={} binds/already/null={}/{}/{} restores(normal/cleanup)={}/{} faults={}",
			reason ? reason : "unspecified", IsEnabled(),
			g_faulted.load(std::memory_order_acquire),
			g_counters.setupCallbacks.load(std::memory_order_relaxed),
			g_counters.armedPasses.load(std::memory_order_relaxed),
			g_counters.dirtyCallbacks.load(std::memory_order_relaxed),
			g_counters.neutralBinds.load(std::memory_order_relaxed),
			g_counters.alreadyNeutral.load(std::memory_order_relaxed),
			g_counters.missingShadowBindings.load(std::memory_order_relaxed),
			g_counters.normalRestores.load(std::memory_order_relaxed),
			g_counters.cleanupRestores.load(std::memory_order_relaxed),
			g_counters.faults.load(std::memory_order_relaxed));
	}
}
