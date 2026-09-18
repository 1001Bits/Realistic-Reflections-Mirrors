#pragma once

#include <cstdint>

namespace HandMirrorSoftEffectPolicy
{
	// BSEffectShader::SetupTechnique subtracts this value before decoding the
	// descriptor. Bit 18 enables SoftEffect. The hand pass must preserve that
	// permutation and replace its live depth input with the private target after
	// native SetDirtyStates commits it.
	inline constexpr std::uint32_t kEffectTechniqueBase = 0x4000002Cu;
	inline constexpr std::uint32_t kSoftEffectDescriptorBit = 1u << 18;
	inline constexpr std::uint32_t kDepthResourceSlot = 3;

	struct Inputs
	{
		bool exactHandPrivatePass{ false };
		bool effectShader{ false };
		std::uint32_t technique{ 0 };
	};

	struct Decision
	{
		std::uint32_t technique{ 0 };
		bool privateDepthRequired{ false };
	};

	[[nodiscard]] constexpr Decision SelectTechnique(
		const Inputs& inputs) noexcept
	{
		Decision output{ inputs.technique, false };
		if (!inputs.exactHandPrivatePass || !inputs.effectShader ||
			inputs.technique < kEffectTechniqueBase) {
			return output;
		}

		const auto descriptor = inputs.technique - kEffectTechniqueBase;
		if ((descriptor & kSoftEffectDescriptorBit) == 0)
			return output;

		// Preserve every native descriptor bit. Changing the technique avoids the
		// bad main-depth sample, but also removes the authored intersection fade and
		// makes fireplace flames appear detached from their emitter. The scoped D3D
		// transaction supplies the correct private t3 instead.
		output.privateDepthRequired = true;
		return output;
	}

	[[nodiscard]] constexpr bool VerifiedPrivateDepthCommit(
		const bool nativeT3Captured,
		const bool mutationMayHaveOccurred,
		const bool privateBindingVerified) noexcept
	{
		return nativeT3Captured && mutationMayHaveOccurred &&
			privateBindingVerified;
	}
}
