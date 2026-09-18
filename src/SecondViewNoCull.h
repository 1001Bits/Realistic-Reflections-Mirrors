#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace SecondViewNoCull
{
	/**
	 * Exact D3D11 rasterizer-state scope for a reflected private pass. Begin clones
	 * the current state and changes only CullMode. Call Restore explicitly from an
	 * SEH finally region; destruction deliberately does not mutate D3D state.
	 */
	class Scope
	{
	public:
		Scope() = default;
		~Scope() = default;

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

		[[nodiscard]] bool Begin(
			ID3D11Device* device,
			ID3D11DeviceContext* context) noexcept;
		[[nodiscard]] bool Reapply() noexcept;
		[[nodiscard]] bool Restore() noexcept;
		void MarkFailed() noexcept { failed = true; }

		[[nodiscard]] bool Active() const noexcept { return active; }
		[[nodiscard]] bool RestorePending() const noexcept { return restorePending; }
		[[nodiscard]] bool Failed() const noexcept { return failed; }

	private:
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> savedState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> noCullState;
		bool active{ false };
		bool restorePending{ false };
		bool failed{ false };
	};

	/** Release the process-local rasterizer cache. Used by offline tests and renderer teardown. */
	void ResetCache() noexcept;
}
