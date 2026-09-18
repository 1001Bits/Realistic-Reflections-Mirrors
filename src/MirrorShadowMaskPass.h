#pragma once

#include <cstdint>

#include <d3d11.h>
#include <wrl/client.h>

/**
 * Private screen-space sun-shadow mask for the reflected view.
 *
 * Community Shaders' lighting programs cannot be rebuilt, and the shipped
 * CS 1.8.4 `Lighting.hlsl` takes its directional shadow from the engine's
 * screen-space mask (t14) — sampled at the pixel's own raster position — plus,
 * with Deferred + Screen Space Shadows, a contact texture at t45. A reflected
 * pixel has no entry in either, which is why the reflection is flat under CS
 * (owner, 2026-09-14 run 24) and why the rebuilt vanilla-bytecode variant, the
 * only path that ever carried private cascades, darkened exactly the draws it
 * covered.
 *
 * This pass renders the same private sun cascades the variant samples into a
 * capture-sized mask, so the *engine's own* shadow mechanism carries reflected
 * shadows for every lighting program, CS's included, in every CS configuration.
 * The mask is a fullscreen evaluation over the capture's depth, so the capture
 * must have a completed depth pre-pass before it runs.
 *
 * The sampling body is the private-depth variant's `sampleDepth`/`main` from
 * `MirrorSunShadowShader.cpp`, verbatim except for the depth source: the
 * variant reads its own `SV_Position.z`, this pass loads the pre-pass depth at
 * the same pixel. Receiver-plane derivatives are therefore taken across the
 * depth buffer rather than inside one triangle, which differs only on silhouette
 * quads, where the existing slope bias already dominates.
 */
namespace MirrorShadowMaskPass
{
	/** Private slots, identical to the rebuilt variant so the HLSL body matches. */
	inline constexpr unsigned kShadowDepth = 126;
	inline constexpr unsigned kMatrices = 127;
	inline constexpr unsigned kDetailDepth = 124;
	inline constexpr unsigned kDetailMatrices = 125;
	inline constexpr unsigned kCaptureDepth = 123;
	inline constexpr unsigned kProjectionConstants = 13;
	/** The engine's screen-space shadow mask slot the result is bound to. */
	inline constexpr unsigned kMaskSlot = 14;

	struct Inputs
	{
		/** Depth of the completed private pre-pass, read-only for this draw. */
		ID3D11ShaderResourceView* captureDepth{ nullptr };
		/** Wide private sun map and its cascade matrices. */
		ID3D11ShaderResourceView* shadowDepth{ nullptr };
		ID3D11ShaderResourceView* matrices{ nullptr };
		/** Player-detail map; when absent the wide map covers every receiver. */
		ID3D11ShaderResourceView* detailDepth{ nullptr };
		ID3D11ShaderResourceView* detailMatrices{ nullptr };
		/** `MirrorProjection` (b13) already uploaded for this capture. */
		ID3D11Buffer* projection{ nullptr };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};

	/**
	 * Render the mask for one capture. Every pipeline object this touches is
	 * restored before returning; a failure leaves no private state bound and
	 * reports false so the caller keeps the neutral (unshadowed) route.
	 */
	[[nodiscard]] bool Render(ID3D11DeviceContext* context, const Inputs& inputs) noexcept;

	/** Mask of the most recent successful render, or null. */
	[[nodiscard]] ID3D11ShaderResourceView* Mask() noexcept;

	/** Capture identity of the current mask, so a stale mask is never bound. */
	[[nodiscard]] std::uint64_t MaskSequence() noexcept;
	void Publish(std::uint64_t sequence) noexcept;
	[[nodiscard]] bool Current(std::uint64_t sequence) noexcept;

	void Invalidate() noexcept;
	void Release() noexcept;

	struct Diagnostics
	{
		std::uint64_t renders{}, rejects{}, faults{}, allocations{};
	};
	[[nodiscard]] Diagnostics Counters() noexcept;
}
