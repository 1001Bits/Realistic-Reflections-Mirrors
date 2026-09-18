#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace CommunityShadersMainCullingFinalizeChainPolicy
{
	/**
	 * Exact official Community Shaders v1.8.4 binary identity used for the
	 * compatibility run. Source tag commit 02646c3008dd7cae91fc790c67c0f342a29aa938
	 * installs Main_RenderShadowMaps at RenderPlayerView + 0x2EC. These values
	 * come from the release DLL whose SHA-256 is
	 * BDF655FB2CCA157C3BD8DE9306C9B70F25120BA1E2777F60843C6389BF2BB88F.
	 */
	inline constexpr std::uint16_t kMachineAMD64 = 0x8664;
	inline constexpr std::uint16_t kPE32PlusMagic = 0x020B;
	inline constexpr std::uint16_t kSectionCount = 9;
	inline constexpr std::uint32_t kTimeDateStamp = 0x6A8F7CE2;
	inline constexpr std::uint32_t kSizeOfCode = 0x0065A400;
	inline constexpr std::uint32_t kSizeOfImage = 0x01408000;
	inline constexpr std::uintptr_t kRenderShadowMapsThunkRVA = 0x003AACF0;
	inline constexpr std::size_t kThunkPrefixSize = 23;
	inline constexpr std::uint32_t kOriginalFinalizeRIPDisplacement = 0x0100FFE9;
	// The verified indirect call is the final six bytes of kThunkPrefix, so its
	// RIP base is exactly thunk RVA + prefix size.
	inline constexpr std::uintptr_t kOriginalFinalizeSlotRVA =
		kRenderShadowMapsThunkRVA + kThunkPrefixSize +
		kOriginalFinalizeRIPDisplacement;
	inline constexpr std::size_t kFiveByteCallBranchStubSize = 14;

	// Deferred::Hooks::Main_RenderShadowMaps::thunk, through and including its
	// indirect call of the preserved native target.  The RIP displacement resolves
	// exactly to kOriginalFinalizeSlotRVA in the pinned image.
	inline constexpr std::array<std::uint8_t, kThunkPrefixSize> kThunkPrefix{
		0x40, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFE,
		0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x20, 0x02, 0x00,
		0x00, 0xFF, 0x15, 0xE9, 0xFF, 0x00, 0x01
	};
	static_assert(kOriginalFinalizeRIPDisplacement ==
		(static_cast<std::uint32_t>(kThunkPrefix[19]) |
		 (static_cast<std::uint32_t>(kThunkPrefix[20]) << 8) |
		 (static_cast<std::uint32_t>(kThunkPrefix[21]) << 16) |
		 (static_cast<std::uint32_t>(kThunkPrefix[22]) << 24)));
	static_assert(kOriginalFinalizeSlotRVA == 0x013BACF0);
	static_assert(kRenderShadowMapsThunkRVA + kThunkPrefix.size() <= kSizeOfImage);
	static_assert(kOriginalFinalizeSlotRVA + sizeof(std::uintptr_t) <= kSizeOfImage);

	struct ModuleIdentity
	{
		std::uint16_t machine{ 0 };
		std::uint16_t sectionCount{ 0 };
		std::uint16_t optionalMagic{ 0 };
		std::uint32_t timeDateStamp{ 0 };
		std::uint32_t sizeOfCode{ 0 };
		std::uint32_t sizeOfImage{ 0 };
	};

	enum class ChainKind : std::uint8_t
	{
		kRejected,
		kNative,
		kCommunityShaders184Direct,
		kCommunityShaders184BranchStub
	};

	struct Evidence
	{
		std::uintptr_t expectedNativeTarget{ 0 };
		std::uintptr_t inspectedCallTarget{ 0 };
		std::uintptr_t communityShadersBase{ 0 };
		ModuleIdentity module{};
		std::span<const std::uint8_t> inspectedBranchStub{};
		std::span<const std::uint8_t> thunkPrefix{};
		std::uintptr_t wrapperOriginalTarget{ 0 };
	};

	[[nodiscard]] constexpr bool MatchesModuleIdentity(
		const ModuleIdentity& a_identity) noexcept
	{
		return a_identity.machine == kMachineAMD64 &&
		       a_identity.sectionCount == kSectionCount &&
		       a_identity.optionalMagic == kPE32PlusMagic &&
		       a_identity.timeDateStamp == kTimeDateStamp &&
		       a_identity.sizeOfCode == kSizeOfCode &&
		       a_identity.sizeOfImage == kSizeOfImage;
	}

	[[nodiscard]] constexpr bool MatchesThunkPrefix(
		std::span<const std::uint8_t> a_prefix) noexcept
	{
		if (a_prefix.size() != kThunkPrefix.size())
			return false;
		for (std::size_t index = 0; index < kThunkPrefix.size(); ++index) {
			if (a_prefix[index] != kThunkPrefix[index])
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool DecodeFiveByteCallBranchStub(
		std::span<const std::uint8_t> a_stub,
		std::uintptr_t& a_destination) noexcept
	{
		a_destination = 0;
		if (a_stub.size() != kFiveByteCallBranchStubSize ||
			a_stub[0] != 0xFF || a_stub[1] != 0x25 || a_stub[2] != 0 ||
			a_stub[3] != 0 || a_stub[4] != 0 || a_stub[5] != 0) {
			return false;
		}
		std::uint64_t decoded = 0;
		for (std::size_t index = 0; index < sizeof(decoded); ++index) {
			decoded |= static_cast<std::uint64_t>(a_stub[6 + index]) <<
				(index * 8);
		}
		if constexpr (sizeof(std::uintptr_t) < sizeof(decoded)) {
			if (decoded > (std::numeric_limits<std::uintptr_t>::max)())
				return false;
		}
		a_destination = static_cast<std::uintptr_t>(decoded);
		return a_destination != 0;
	}

	[[nodiscard]] constexpr ChainKind Classify(const Evidence& a_evidence) noexcept
	{
		if (!a_evidence.expectedNativeTarget || !a_evidence.inspectedCallTarget)
			return ChainKind::kRejected;
		if (a_evidence.inspectedCallTarget == a_evidence.expectedNativeTarget)
			return ChainKind::kNative;

		if (!a_evidence.communityShadersBase ||
			!MatchesModuleIdentity(a_evidence.module) ||
			!MatchesThunkPrefix(a_evidence.thunkPrefix) ||
			a_evidence.wrapperOriginalTarget != a_evidence.expectedNativeTarget ||
			a_evidence.communityShadersBase >
				(std::numeric_limits<std::uintptr_t>::max)() -
				kRenderShadowMapsThunkRVA) {
			return ChainKind::kRejected;
		}

		const auto expectedThunk =
			a_evidence.communityShadersBase + kRenderShadowMapsThunkRVA;
		if (a_evidence.inspectedCallTarget == expectedThunk)
			return ChainKind::kCommunityShaders184Direct;

		std::uintptr_t decodedStubTarget = 0;
		if (!DecodeFiveByteCallBranchStub(
				a_evidence.inspectedBranchStub, decodedStubTarget) ||
			decodedStubTarget != expectedThunk) {
			return ChainKind::kRejected;
		}
		return ChainKind::kCommunityShaders184BranchStub;
	}

	[[nodiscard]] constexpr const char* ToString(const ChainKind a_kind) noexcept
	{
		switch (a_kind) {
		case ChainKind::kNative:
			return "native";
		case ChainKind::kCommunityShaders184Direct:
			return "community-shaders-v1.8.4-direct";
		case ChainKind::kCommunityShaders184BranchStub:
			return "community-shaders-v1.8.4-branch-stub";
		default:
			return "rejected";
		}
	}
}
