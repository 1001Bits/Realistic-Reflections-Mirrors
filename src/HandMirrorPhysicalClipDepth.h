#pragma once

#include <cstdint>

#include <DirectXMath.h>
#include <d3d11.h>

namespace HandMirrorPhysicalClipDepth
{
#if defined(MIRRORS_OF_SKYRIM_PHYSICAL_CLIP_DEPTH_TEST)
	namespace Testing
	{
		// Exercise a forwarding getter's undefined output slots with real WARP
		// bindings. The callback cannot change the reported ownership count.
		inline void (*afterComputeShaderQuery)(
			ID3D11ClassInstance** instances, UINT capacity, UINT count) noexcept = nullptr;
	}
#endif

	struct ProjectionPair
	{
		DirectX::XMFLOAT4X4 conventional{};
		DirectX::XMFLOAT4X4 oblique{};
		std::uint64_t generation{ 0 };
		bool valid{ false };
	};

	struct Bindings
	{
		ID3D11DeviceContext* context{ nullptr };
		ID3D11RenderTargetView* colorRTV{ nullptr };
		ID3D11DepthStencilView* writableDepthDSV{ nullptr };
		ID3D11DepthStencilView* readOnlyDepthDSV{ nullptr };
		ID3D11ShaderResourceView* obliqueDepthSRV{ nullptr };
		std::uintptr_t targetIdentity{ 0 };
		std::uint64_t depthSnapshotEpoch{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		ProjectionPair projection{};
		// Placed-mirror targets carry their own reduced depth views (dynamic
		// resolution); the hand's reduced-resolution setting does not govern them.
		bool reducedViewsAllowed{ false };
	};

	enum class ResolveStatus : std::uint8_t
	{
		kApplied,
		kCached,
		kRejected,
		kFaulted
	};

	struct ResolveResult
	{
		ID3D11ShaderResourceView* conventionalDepthSRV{ nullptr };
		ResolveStatus status{ ResolveStatus::kRejected };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return conventionalDepthSRV != nullptr &&
				(status == ResolveStatus::kApplied ||
				 status == ResolveStatus::kCached);
		}
	};

	struct Diagnostics
	{
		std::uint64_t deviceInitializations{ 0 };
		std::uint64_t resourceCreations{ 0 };
		std::uint64_t storageReuses{ 0 };
		std::uint64_t storageEvictions{ 0 };
		std::uint64_t storageBytes{ 0 };
		std::uint64_t resolveAttempts{ 0 };
		std::uint64_t dispatches{ 0 };
		std::uint64_t cacheHits{ 0 };
		std::uint64_t restores{ 0 };
		std::uint64_t rejects{ 0 };
		std::uint64_t faults{ 0 };
		bool faulted{ false };
	};

	/** Compile-free creation of the embedded compute shader for the active device. */
	[[nodiscard]] bool EnsureDevice(ID3D11Device* device) noexcept;

	/**
	 * Convert the bound hand target's oblique D24 depth to the conventional
	 * frozen projection expected by Skyrim's native SoftEffect shader. The
	 * returned SRV is borrowed from one of two active result slots. Retired
	 * storage can be reused by extent; its old identity and contents never confer
	 * validity on a new resolve. Callers consume the result before the next resolve.
	 */
	[[nodiscard]] ResolveResult Resolve(const Bindings& bindings) noexcept;

	[[nodiscard]] Diagnostics GetDiagnostics() noexcept;
	[[nodiscard]] bool Faulted() noexcept;
}
