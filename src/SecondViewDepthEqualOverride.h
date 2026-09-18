#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace SecondViewDepthEqualOverride
{
	enum class PrepareResult
	{
		kUnchanged,
		kTranslated,
		kFailed
	};

	/**
	 * Exact-SE correction scope for a planar WorldRoot/player primary pass. Begin seeds
	 * Skyrim's requested depth mode as kTestWrite; PrepareSetDirtyStates translates
	 * only later kTestEqual requests back to kTestWrite before the native commit.
	 * The owning executor disables translation before any auxiliary-root cycle.
	 * Restore returns both the renderer shadow and actual OM state to their exact
	 * entry values. The owner must call Restore from its SEH finally region.
	 */
	class Scope
	{
	public:
		Scope() = default;
		~Scope() = default;

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

		[[nodiscard]] bool Begin(
			ID3D11DeviceContext* context,
			void* flatRendererShadow) noexcept;
		/** Re-seed kTestWrite after an isolated auxiliary wrapper mutated shadow state. */
		[[nodiscard]] bool ReseedForPrimary() noexcept;
		[[nodiscard]] PrepareResult PrepareSetDirtyStates() noexcept;
		[[nodiscard]] bool Restore() noexcept;
		void MarkFailed() noexcept { failed = true; }

		[[nodiscard]] bool Active() const noexcept { return active; }
		[[nodiscard]] bool RestorePending() const noexcept { return restorePending; }
		[[nodiscard]] bool Failed() const noexcept { return failed; }

	private:
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> savedState;
		void* shadow{ nullptr };
		std::uint32_t savedStencilReference{ 0 };
		std::uint32_t savedFlags{ 0 };
		std::uint32_t savedDepthMode{ 0 };
		std::uint32_t savedPreviousDepthMode{ 0 };
		std::uint32_t savedStencilMode{ 0 };
		std::uint32_t savedShadowStencilReference{ 0 };
		bool active{ false };
		bool restorePending{ false };
		bool failed{ false };
	};
}
