#pragma once

#include "CommunityShadersMainCullingFinalizeChainPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace HandMirrorCommunityShadersLightBridgePolicy
{
	/**
	 * Exact flat-runtime BSLightingShader::SetupGeometry call which stock
	 * Community Shaders v1.8.4 replaces with its Light Limit Fix strict-light
	 * packer.  The four rel32 displacement bytes are the only mutable bytes in
	 * the signed Skyrim window.
	 */
	inline constexpr std::uintptr_t kSE1597CallSiteRVA = 0x012F30D3;
	inline constexpr std::uintptr_t kSE1597NativeTargetRVA = 0x012F4700;
	inline constexpr std::uintptr_t kAE161170CallSiteRVA = 0x014DDB4E;
	inline constexpr std::uintptr_t kAE161170NativeTargetRVA = 0x014DF650;
	// GOG 1.6.1179: Ghidra Combined confirmed the AE prefix/suffix around the
	// SetupGeometry call; only the site and native target RVAs move.
	inline constexpr std::uintptr_t kAE161179CallSiteRVA = 0x014DEBBE;
	inline constexpr std::uintptr_t kAE161179NativeTargetRVA = 0x014E06C0;
	inline constexpr std::size_t kFiveByteCallSize = 5;
	inline constexpr std::size_t kBranchStubSize = 14;

	inline constexpr std::array<std::uint8_t, 34> kSE1597Prefix{
		0x44, 0x8B, 0x64, 0x24, 0x40,
		0x4C, 0x8D, 0x45, 0xC0,
		0x44, 0x89, 0x64, 0x24, 0x30,
		0x44, 0x8B, 0xCF,
		0xF3, 0x0F, 0x11, 0x44, 0x24, 0x28,
		0x49, 0x8B, 0xD2,
		0x49, 0x8B, 0xCF,
		0x44, 0x89, 0x6C, 0x24, 0x20
	};
	inline constexpr std::array<std::uint8_t, 14> kSE1597Suffix{
		0xEB, 0x05,
		0x44, 0x8B, 0x64, 0x24, 0x40,
		0x4C, 0x8B, 0xAD, 0x80, 0x00, 0x00, 0x00
	};
	inline constexpr std::array<std::uint8_t, 36> kAE161170Prefix{
		0x44, 0x89, 0x7C, 0x24, 0x30,
		0x4C, 0x8B, 0xA5, 0xE8, 0x01, 0x00, 0x00,
		0x4C, 0x8D, 0x85, 0xB0, 0x00, 0x00, 0x00,
		0xF3, 0x0F, 0x11, 0x44, 0x24, 0x28,
		0x49, 0x8B, 0xD4,
		0x48, 0x8B, 0xCF,
		0x44, 0x89, 0x6C, 0x24, 0x20
	};
	inline constexpr std::array<std::uint8_t, 16> kAE161170Suffix{
		0xEB, 0x07,
		0x4C, 0x8B, 0xA5, 0xE8, 0x01, 0x00, 0x00,
		0x4C, 0x8B, 0xAD, 0xE0, 0x01, 0x00, 0x00
	};

	struct RuntimeContract
	{
		std::uint64_t nativeTargetID{ 0 };
		std::uintptr_t callSiteRVA{ 0 };
		std::uintptr_t nativeTargetRVA{ 0 };
		std::span<const std::uint8_t> prefix{};
		std::span<const std::uint8_t> suffix{};
	};

	inline constexpr RuntimeContract kSE1597Contract{
		.nativeTargetID = 100578,
		.callSiteRVA = kSE1597CallSiteRVA,
		.nativeTargetRVA = kSE1597NativeTargetRVA,
		.prefix = kSE1597Prefix,
		.suffix = kSE1597Suffix
	};
	inline constexpr RuntimeContract kAE161170Contract{
		.nativeTargetID = 107313,
		.callSiteRVA = kAE161170CallSiteRVA,
		.nativeTargetRVA = kAE161170NativeTargetRVA,
		.prefix = kAE161170Prefix,
		.suffix = kAE161170Suffix
	};
	inline constexpr RuntimeContract kAE161179Contract{
		.nativeTargetID = 107313,
		.callSiteRVA = kAE161179CallSiteRVA,
		.nativeTargetRVA = kAE161179NativeTargetRVA,
		.prefix = kAE161170Prefix,
		.suffix = kAE161170Suffix
	};

	// Pinned by the official v1.8.4 PDB and verified against the release image:
	// Hooks::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights::thunk.
	inline constexpr std::uintptr_t kCommunityShadersLightPackerThunkRVA =
		0x001898F0;

	using ModuleIdentity =
		CommunityShadersMainCullingFinalizeChainPolicy::ModuleIdentity;

	enum class ChainKind : std::uint8_t
	{
		kRejected,
		kCommunityShaders184Direct,
		kCommunityShaders184BranchStub
	};

	struct ChainEvidence
	{
		bool exactSE1597{ false };
		bool exactAE161170{ false };
		bool exactAE161179{ false };
		bool exactFileIdentity{ false };
		std::uintptr_t skyrimBase{ 0 };
		std::uintptr_t callSite{ 0 };
		std::uintptr_t inspectedCallTarget{ 0 };
		std::uintptr_t communityShadersBase{ 0 };
		ModuleIdentity communityShadersModule{};
		std::span<const std::uint8_t> prefix{};
		std::span<const std::uint8_t> suffix{};
		std::span<const std::uint8_t> inspectedBranchStub{};
	};

	[[nodiscard]] constexpr bool EqualBytes(
		const std::span<const std::uint8_t> actual,
		const std::span<const std::uint8_t> expected) noexcept
	{
		if (actual.size() != expected.size())
			return false;
		for (std::size_t index = 0; index < actual.size(); ++index) {
			if (actual[index] != expected[index])
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool DecodeBranchStub(
		const std::span<const std::uint8_t> stub,
		std::uintptr_t& destination) noexcept
	{
		destination = 0;
		if (stub.size() != kBranchStubSize || stub[0] != 0xFF ||
			stub[1] != 0x25 || stub[2] != 0 || stub[3] != 0 ||
			stub[4] != 0 || stub[5] != 0) {
			return false;
		}
		std::uint64_t decoded = 0;
		for (std::size_t index = 0; index < sizeof(decoded); ++index) {
			decoded |= static_cast<std::uint64_t>(stub[6 + index]) <<
				(index * 8);
		}
		if constexpr (sizeof(std::uintptr_t) < sizeof(decoded)) {
			if (decoded > (std::numeric_limits<std::uintptr_t>::max)())
				return false;
		}
		destination = static_cast<std::uintptr_t>(decoded);
		return destination != 0;
	}

	[[nodiscard]] constexpr bool IsPinnedCommunityShadersLightPacker(
		const std::uintptr_t moduleBase,
		const std::uintptr_t address) noexcept
	{
		if (!moduleBase ||
			moduleBase > (std::numeric_limits<std::uintptr_t>::max)() -
				kCommunityShadersLightPackerThunkRVA) {
			return false;
		}
		return address ==
			moduleBase + kCommunityShadersLightPackerThunkRVA;
	}

	[[nodiscard]] constexpr ChainKind Classify(
		const ChainEvidence& evidence) noexcept
	{
		using CommunityShadersMainCullingFinalizeChainPolicy::
			MatchesModuleIdentity;
		const RuntimeContract* contract = nullptr;
		if (evidence.exactSE1597 && !evidence.exactAE161170 &&
			!evidence.exactAE161179) {
			contract = &kSE1597Contract;
		} else if (evidence.exactAE161179 && !evidence.exactSE1597 &&
			!evidence.exactAE161170) {
			contract = &kAE161179Contract;
		} else if (evidence.exactAE161170 && !evidence.exactSE1597 &&
			!evidence.exactAE161179) {
			contract = &kAE161170Contract;
		}
		if (!contract || !evidence.exactFileIdentity ||
			!evidence.skyrimBase ||
			evidence.skyrimBase >
				(std::numeric_limits<std::uintptr_t>::max)() -
					contract->callSiteRVA ||
			evidence.callSite != evidence.skyrimBase + contract->callSiteRVA ||
			!evidence.inspectedCallTarget ||
			!MatchesModuleIdentity(evidence.communityShadersModule) ||
			!EqualBytes(evidence.prefix, contract->prefix) ||
			!EqualBytes(evidence.suffix, contract->suffix)) {
			return ChainKind::kRejected;
		}

		if (IsPinnedCommunityShadersLightPacker(
				evidence.communityShadersBase,
				evidence.inspectedCallTarget)) {
			return ChainKind::kCommunityShaders184Direct;
		}

		std::uintptr_t decoded = 0;
		if (!DecodeBranchStub(evidence.inspectedBranchStub, decoded) ||
			!IsPinnedCommunityShadersLightPacker(
				evidence.communityShadersBase, decoded)) {
			return ChainKind::kRejected;
		}
		return ChainKind::kCommunityShaders184BranchStub;
	}

	struct Admission
	{
		bool installed{ false };
		bool faulted{ false };
		bool exactHandPrivateTarget{ false };
		bool exactStandingPrivateTarget{ false };
		bool exactShadowPrivateTarget{ false };
		bool targetBound{ false };
		bool nested{ false };
		std::uintptr_t currentAccumulator{ 0 };
		std::uintptr_t savedActiveShadowScene{ 0 };
		std::uintptr_t mainShadowScene{ 0 };
	};

	[[nodiscard]] constexpr bool ShouldBridge(
		const Admission& admission) noexcept
	{
		const unsigned targets = unsigned(admission.exactHandPrivateTarget) +
			unsigned(admission.exactStandingPrivateTarget) +
			unsigned(admission.exactShadowPrivateTarget);
		return admission.installed && !admission.faulted &&
			targets == 1 && admission.targetBound &&
			!admission.nested && admission.currentAccumulator != 0 &&
			admission.mainShadowScene != 0 &&
			admission.savedActiveShadowScene == admission.mainShadowScene;
	}
}
