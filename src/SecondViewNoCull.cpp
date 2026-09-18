#include "SecondViewNoCull.h"

#include "NoDestructor.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace SecondViewNoCull
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr std::size_t kCacheSize = 16;
		constexpr GUID kD3DDebugObjectName{
			0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 }
		};

		struct CacheEntry
		{
			D3D11_RASTERIZER_DESC description{};
			ComPtr<ID3D11RasterizerState> state;
		};

		struct CacheState
		{
			ComPtr<ID3D11Device> device;
			std::array<CacheEntry, kCacheSize> entries{};
			std::size_t nextReplacement{ 0 };
		};

		stl::no_destructor<CacheState> g_cache{};

		[[nodiscard]] D3D11_RASTERIZER_DESC DefaultDescription() noexcept
		{
			D3D11_RASTERIZER_DESC description{};
			description.FillMode = D3D11_FILL_SOLID;
			description.CullMode = D3D11_CULL_BACK;
			description.FrontCounterClockwise = FALSE;
			description.DepthClipEnable = TRUE;
			return description;
		}

		[[nodiscard]] bool EqualDescriptions(
			const D3D11_RASTERIZER_DESC& left,
			const D3D11_RASTERIZER_DESC& right) noexcept
		{
			return left.FillMode == right.FillMode && left.CullMode == right.CullMode &&
			       left.FrontCounterClockwise == right.FrontCounterClockwise &&
			       left.DepthBias == right.DepthBias &&
			       left.DepthBiasClamp == right.DepthBiasClamp &&
			       left.SlopeScaledDepthBias == right.SlopeScaledDepthBias &&
			       left.DepthClipEnable == right.DepthClipEnable &&
			       left.ScissorEnable == right.ScissorEnable &&
			       left.MultisampleEnable == right.MultisampleEnable &&
			       left.AntialiasedLineEnable == right.AntialiasedLineEnable;
		}

		void ClearCache() noexcept
		{
			auto& cache = g_cache.get();
			for (auto& entry : cache.entries) {
				entry.state.Reset();
				entry.description = {};
			}
			cache.device.Reset();
			cache.nextReplacement = 0;
		}

		[[nodiscard]] bool ContextUsesDevice(
			ID3D11Device* device,
			ID3D11DeviceContext* context) noexcept
		{
			if (!device || !context)
				return false;
			ComPtr<ID3D11Device> contextDevice;
			context->GetDevice(contextDevice.GetAddressOf());
			return contextDevice.Get() == device;
		}

		[[nodiscard]] bool AcquireNoCullState(
			ID3D11Device* device,
			const D3D11_RASTERIZER_DESC& description,
			ComPtr<ID3D11RasterizerState>& output) noexcept
		{
			output.Reset();
			if (!device)
				return false;
			auto& cache = g_cache.get();
			if (cache.device.Get() != device) {
				ClearCache();
				cache.device = device;
			}

			for (const auto& entry : cache.entries) {
				if (entry.state && EqualDescriptions(entry.description, description)) {
					output = entry.state;
					return true;
				}
			}

			ComPtr<ID3D11RasterizerState> created;
			if (FAILED(device->CreateRasterizerState(&description, created.GetAddressOf())))
				return false;
			created->SetPrivateData(
				kD3DDebugObjectName,
				static_cast<UINT>(std::strlen("RealisticReflections.M4.NoCullRS")),
				"RealisticReflections.M4.NoCullRS");

			std::size_t slot = cache.entries.size();
			for (std::size_t index = 0; index < cache.entries.size(); ++index) {
				if (!cache.entries[index].state) {
					slot = index;
					break;
				}
			}
			if (slot == cache.entries.size()) {
				slot = cache.nextReplacement;
				cache.nextReplacement =
					(cache.nextReplacement + 1) % cache.entries.size();
			}
			cache.entries[slot].description = description;
			cache.entries[slot].state = created;
			output = std::move(created);
			return true;
		}

		[[nodiscard]] bool CurrentStateIs(
			ID3D11DeviceContext* context,
			ID3D11RasterizerState* expected) noexcept
		{
			if (!context)
				return false;
			ComPtr<ID3D11RasterizerState> current;
			context->RSGetState(current.GetAddressOf());
			return current.Get() == expected;
		}

		[[nodiscard]] bool BuildNoCullForCurrentState(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			ComPtr<ID3D11RasterizerState>& output) noexcept
		{
			output.Reset();
			if (!ContextUsesDevice(device, context))
				return false;
			ComPtr<ID3D11RasterizerState> current;
			context->RSGetState(current.GetAddressOf());
			D3D11_RASTERIZER_DESC description = DefaultDescription();
			if (current)
				current->GetDesc(&description);
			if (current && description.CullMode == D3D11_CULL_NONE) {
				output = current;
				return true;
			}
			description.CullMode = D3D11_CULL_NONE;
			return AcquireNoCullState(device, description, output);
		}
	}

	bool Scope::Begin(ID3D11Device* newDevice, ID3D11DeviceContext* newContext) noexcept
	{
		if (active || restorePending || device || context || savedState || noCullState ||
			!ContextUsesDevice(newDevice, newContext))
			return false;

		ComPtr<ID3D11RasterizerState> current;
		newContext->RSGetState(current.GetAddressOf());
		ComPtr<ID3D11RasterizerState> replacement;
		if (!BuildNoCullForCurrentState(newDevice, newContext, replacement))
			return false;

		device = newDevice;
		context = newContext;
		savedState = current;
		noCullState = replacement;
		active = true;
		restorePending = true;
		failed = false;
		context->RSSetState(noCullState.Get());
		if (CurrentStateIs(context.Get(), noCullState.Get()))
			return true;

		failed = true;
		(void) Restore();
		return false;
	}

	bool Scope::Reapply() noexcept
	{
		if (!active || !restorePending || !device || !context) {
			failed = true;
			return false;
		}
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> replacement;
		if (!BuildNoCullForCurrentState(device.Get(), context.Get(), replacement)) {
			failed = true;
			return false;
		}
		noCullState = std::move(replacement);
		context->RSSetState(noCullState.Get());
		const bool applied = CurrentStateIs(context.Get(), noCullState.Get());
		failed = failed || !applied;
		return applied;
	}

	bool Scope::Restore() noexcept
	{
		active = false;
		if (!restorePending)
			return true;
		if (!context) {
			failed = true;
			return false;
		}

		context->RSSetState(savedState.Get());
		if (!CurrentStateIs(context.Get(), savedState.Get())) {
			failed = true;
			return false;
		}

		restorePending = false;
		noCullState.Reset();
		savedState.Reset();
		context.Reset();
		device.Reset();
		return true;
	}

	void ResetCache() noexcept
	{
		ClearCache();
	}
}
