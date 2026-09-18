#pragma once
#include "MirrorSunShadowShader.h"
#include <d3d11.h>
#include <wrl/client.h>

// One draw's D3D-only lease. No engine or Community Shaders object is retained.
// The caller owns thread/target admission and its outer SEH/fail-stop boundary.
struct MirrorSunShadowD3DState
{
	template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
	bool bound{};
	bool usesPrivateSampler{};
	bool neutralizesBorrowedMoments{};
	bool usesDetailMap{};
	// Community Shaders only: replace the borrowed main-camera sun moments (t18)
	// for one draw whose program is left unpatched. Shader, constants, private
	// map slots and samplers stay exactly as the engine bound them.
	bool neutralOnly{};
	// Slot neutralized by BindNeutralOnly: 18 (CS moments) or 14 (vanilla shadow mask).
	UINT neutralSlot{ 18 };
	Ptr<ID3D11DeviceContext> context;
	Ptr<ID3D11PixelShader> original, replacement;
	Ptr<ID3D11Buffer> cb;
	Ptr<ID3D11ShaderResourceView> maps[2], originalMoments;
	Ptr<ID3D11ShaderResourceView> detailMaps[2];
	Ptr<ID3D11SamplerState> sampler;
	// Community Shaders Light Limit Fix: its cluster light grid (t37) is built
	// for the main camera's screen, so a mirror pixel indexes a foreign cell.
	// For one private draw the grid is replaced by an all-zero grid (no cluster
	// lights); the strict per-draw list carries the lights instead.
	bool neutralizesClusterGrid{};
	static constexpr UINT kClusterGridSlot = 37;
	Ptr<ID3D11ShaderResourceView> originalClusterGrid;
	// Community Shaders Screen Space Shadows (deferred): the lighting shader
	// multiplies the sun by a contact-shadow texture (t45) loaded at the pixel's
	// raster position on the main screen; a private capture pixel has no entry
	// there (run 24: the main-screen player stamped dark onto the reflection).
	bool neutralizesScreenSpaceShadow{};
	static constexpr UINT kScreenSpaceShadowSlot = 45;
	Ptr<ID3D11ShaderResourceView> originalScreenSpaceShadow;
	// The engine's screen-space sun mask. A private capture supplies its own
	// (MirrorShadowMaskPass) so every lighting program, including the ones
	// Community Shaders replaced, reads the reflected view's sun visibility.
	bool bindsShadowMask{};
	static constexpr UINT kShadowMaskSlot = 14;
	Ptr<ID3D11ShaderResourceView> originalShadowMask;

	bool BindShadowMask(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *mask)
	{
		if (!bound || !ctx || !mask || bindsShadowMask || ctx != context.Get())
			return false;
		if (neutralOnly && neutralSlot == kShadowMaskSlot)
			return false;
		ctx->PSGetShaderResources(kShadowMaskSlot, 1, &originalShadowMask);
		bindsShadowMask = true;
		ctx->PSSetShaderResources(kShadowMaskSlot, 1, &mask);
		return true;
	}

	bool BindScreenSpaceShadowNeutral(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *white)
	{
		if (!bound || !ctx || !white || neutralizesScreenSpaceShadow || ctx != context.Get())
			return false;
		ctx->PSGetShaderResources(kScreenSpaceShadowSlot, 1, &originalScreenSpaceShadow);
		neutralizesScreenSpaceShadow = true;
		ctx->PSSetShaderResources(kScreenSpaceShadowSlot, 1, &white);
		return true;
	}

	bool BindClusterGridNeutral(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *grid)
	{
		if (!bound || !ctx || !grid || neutralizesClusterGrid || ctx != context.Get())
			return false;
		ctx->PSGetShaderResources(kClusterGridSlot, 1, &originalClusterGrid);
		neutralizesClusterGrid = true;
		ctx->PSSetShaderResources(kClusterGridSlot, 1, &grid);
		return true;
	}

	bool Bind(ID3D11DeviceContext *ctx, ID3D11PixelShader *next, ID3D11Buffer *constants,
			  ID3D11ShaderResourceView *moments, ID3D11ShaderResourceView *matrices,
			  ID3D11SamplerState *filtering, ID3D11ShaderResourceView *neutral, bool changeSampler = true,
              bool changeDetail = false, ID3D11ShaderResourceView *detailDepth = nullptr,
              ID3D11ShaderResourceView *detailMatrices = nullptr)
	{
		if (bound || !ctx || !next || !constants || !moments || !matrices || !filtering)
			return false;
		UINT classes = 0;
		ctx->PSGetShader(&original, nullptr, &classes);
		if (!original || classes)
		{
			original.Reset();
			return false;
		}
		context = ctx;
		replacement = next;
		usesPrivateSampler = changeSampler;
		neutralizesBorrowedMoments = neutral != nullptr;
		usesDetailMap = changeDetail;
		ctx->PSGetConstantBuffers(MirrorSunShadowShader::kConstants, 1, &cb);
		ctx->PSGetShaderResources(MirrorSunShadowShader::kMoments, 1, &maps[0]);
		ctx->PSGetShaderResources(MirrorSunShadowShader::kMatrices, 1, &maps[1]);
        if (usesDetailMap) {
            ctx->PSGetShaderResources(MirrorSunShadowShader::kDetailDepth, 1, &detailMaps[0]);
            ctx->PSGetShaderResources(MirrorSunShadowShader::kDetailMatrices, 1, &detailMaps[1]);
        }
		if (neutralizesBorrowedMoments) ctx->PSGetShaderResources(18, 1, &originalMoments);
		if (usesPrivateSampler)
			ctx->PSGetSamplers(MirrorSunShadowShader::kSampler, 1, &sampler);
		bound = true;
		ctx->PSSetConstantBuffers(MirrorSunShadowShader::kConstants, 1, &constants);
		ctx->PSSetShaderResources(MirrorSunShadowShader::kMoments, 1, &moments);
		ctx->PSSetShaderResources(MirrorSunShadowShader::kMatrices, 1, &matrices);
        if (usesDetailMap) {
            ID3D11ShaderResourceView* views[]{detailDepth, detailMatrices};
            ctx->PSSetShaderResources(MirrorSunShadowShader::kDetailDepth, 2, views);
        }
		if (usesPrivateSampler)
			ctx->PSSetSamplers(MirrorSunShadowShader::kSampler, 1, &filtering);
		if (neutralizesBorrowedMoments) ctx->PSSetShaderResources(18, 1, &neutral);
		ctx->PSSetShader(next, nullptr, 0);
		return true;
	}

	bool BindNeutralOnly(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *neutral, UINT slot = 18)
	{
		if (bound || !ctx || !neutral)
			return false;
		context = ctx;
		neutralOnly = true;
		neutralizesBorrowedMoments = true;
		neutralSlot = slot;
		ctx->PSGetShaderResources(neutralSlot, 1, &originalMoments);
		bound = true;
		ctx->PSSetShaderResources(neutralSlot, 1, &neutral);
		return true;
	}

	bool Restore() noexcept
	{
		if (!bound)
			return true;
		if (!RestoreBindings() || !VerifyBindings())
			return false;
		*this = {};
		return true;
	}

  private:
	bool RestoreBindings() noexcept
	{
		__try
		{
			auto *ctx = context.Get();
			auto *buffer = cb.Get();
			auto *s = sampler.Get();
			auto *m = maps[0].Get();
			auto *d = maps[1].Get();
			auto *sun = originalMoments.Get();
			if (neutralizesClusterGrid)
			{
				auto *grid = originalClusterGrid.Get();
				ctx->PSSetShaderResources(kClusterGridSlot, 1, &grid);
			}
			if (neutralizesScreenSpaceShadow)
			{
				auto *contact = originalScreenSpaceShadow.Get();
				ctx->PSSetShaderResources(kScreenSpaceShadowSlot, 1, &contact);
			}
			if (bindsShadowMask)
			{
				auto *mask = originalShadowMask.Get();
				ctx->PSSetShaderResources(kShadowMaskSlot, 1, &mask);
			}
			if (neutralOnly)
			{
				ctx->PSSetShaderResources(neutralSlot, 1, &sun);
				return true;
			}
			ctx->PSSetShader(original.Get(), nullptr, 0);
			ctx->PSSetConstantBuffers(MirrorSunShadowShader::kConstants, 1, &buffer);
			if (usesPrivateSampler)
				ctx->PSSetSamplers(MirrorSunShadowShader::kSampler, 1, &s);
			ctx->PSSetShaderResources(MirrorSunShadowShader::kMoments, 1, &m);
			ctx->PSSetShaderResources(MirrorSunShadowShader::kMatrices, 1, &d);
            if (usesDetailMap) {
                ID3D11ShaderResourceView* views[]{detailMaps[0].Get(), detailMaps[1].Get()};
                ctx->PSSetShaderResources(MirrorSunShadowShader::kDetailDepth, 2, views);
            }
			if (neutralizesBorrowedMoments) ctx->PSSetShaderResources(18, 1, &sun);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	bool VerifyBindings() const noexcept
	{
		// Verification getters are confined to cleanup. A mismatch retains the
		// original references for the caller's mandatory outer cleanup retry.
		Ptr<ID3D11PixelShader> ps;
		Ptr<ID3D11Buffer> buffer;
		Ptr<ID3D11SamplerState> s;
		Ptr<ID3D11ShaderResourceView> m, d, sun, nearDepth, nearMatrices;
		if (!ReadBindings(ps.GetAddressOf(), buffer.GetAddressOf(), s.GetAddressOf(), m.GetAddressOf(),
						  d.GetAddressOf(), sun.GetAddressOf(), nearDepth.GetAddressOf(), nearMatrices.GetAddressOf()))
			return false;
		if (neutralizesClusterGrid && !VerifyClusterGridRestored())
			return false;
		if (neutralizesScreenSpaceShadow && !VerifyScreenSpaceShadowRestored())
			return false;
		if (bindsShadowMask && !VerifyShadowMaskRestored())
			return false;
		return ps.Get() == original.Get() && buffer.Get() == cb.Get() && s.Get() == sampler.Get() &&
			   m.Get() == maps[0].Get() && d.Get() == maps[1].Get() && sun.Get() == originalMoments.Get() &&
               nearDepth.Get() == detailMaps[0].Get() && nearMatrices.Get() == detailMaps[1].Get();
	}
	bool VerifyClusterGridRestored() const noexcept
	{
		Ptr<ID3D11ShaderResourceView> grid;
		if (!ReadClusterGrid(grid.GetAddressOf()))
			return false;
		return grid.Get() == originalClusterGrid.Get();
	}
	bool VerifyScreenSpaceShadowRestored() const noexcept
	{
		Ptr<ID3D11ShaderResourceView> contact;
		if (!ReadScreenSpaceShadow(contact.GetAddressOf()))
			return false;
		return contact.Get() == originalScreenSpaceShadow.Get();
	}
	bool VerifyShadowMaskRestored() const noexcept
	{
		Ptr<ID3D11ShaderResourceView> mask;
		if (!ReadShadowMask(mask.GetAddressOf()))
			return false;
		return mask.Get() == originalShadowMask.Get();
	}
	bool ReadShadowMask(ID3D11ShaderResourceView **mask) const noexcept
	{
		__try
		{
			context->PSGetShaderResources(kShadowMaskSlot, 1, mask);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	bool ReadScreenSpaceShadow(ID3D11ShaderResourceView **contact) const noexcept
	{
		__try
		{
			context->PSGetShaderResources(kScreenSpaceShadowSlot, 1, contact);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	bool ReadClusterGrid(ID3D11ShaderResourceView **grid) const noexcept
	{
		__try
		{
			context->PSGetShaderResources(kClusterGridSlot, 1, grid);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
	bool ReadBindings(ID3D11PixelShader **ps, ID3D11Buffer **buffer, ID3D11SamplerState **s,
					  ID3D11ShaderResourceView **m, ID3D11ShaderResourceView **d,
					  ID3D11ShaderResourceView **sun, ID3D11ShaderResourceView **nearDepth,
                      ID3D11ShaderResourceView **nearMatrices) const noexcept
	{
		__try
		{
			auto *ctx = context.Get();
			if (neutralOnly)
			{
				ctx->PSGetShaderResources(neutralSlot, 1, sun);
				return true;
			}
			ctx->PSGetShader(ps, nullptr, nullptr);
			ctx->PSGetConstantBuffers(MirrorSunShadowShader::kConstants, 1, buffer);
			if (usesPrivateSampler)
				ctx->PSGetSamplers(MirrorSunShadowShader::kSampler, 1, s);
			ctx->PSGetShaderResources(MirrorSunShadowShader::kMoments, 1, m);
			ctx->PSGetShaderResources(MirrorSunShadowShader::kMatrices, 1, d);
            if (usesDetailMap) {
                ctx->PSGetShaderResources(MirrorSunShadowShader::kDetailDepth, 1, nearDepth);
                ctx->PSGetShaderResources(MirrorSunShadowShader::kDetailMatrices, 1, nearMatrices);
            }
			if (neutralizesBorrowedMoments) ctx->PSGetShaderResources(18, 1, sun);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
};
