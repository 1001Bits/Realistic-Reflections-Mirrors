#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace HandMirrorFirstPersonViewHookChainPolicy
{
	/**
	 * Exact official Community Shaders v1.8.4 binary identity used by the
	 * compatibility run. Source tag commit
	 * 02646c3008dd7cae91fc790c67c0f342a29aa938 installs
	 * Deferred::Hooks::Main_RenderFirstPersonView at RenderPlayerView + 0x944
	 * on SE 1.5.97 (+0x954 on AE 1.6.1170). These values come from the release
	 * DLL whose SHA-256 is
	 * BDF655FB2CCA157C3BD8DE9306C9B70F25120BA1E2777F60843C6389BF2BB88F.
	 */
	inline constexpr std::uint16_t kMachineAMD64 = 0x8664;
	inline constexpr std::uint16_t kPE32PlusMagic = 0x020B;
	inline constexpr std::uint16_t kSectionCount = 9;
	inline constexpr std::uint32_t kTimeDateStamp = 0x6A8F7CE2;
	inline constexpr std::uint32_t kSizeOfCode = 0x0065A400;
	inline constexpr std::uint32_t kSizeOfImage = 0x01408000;
	inline constexpr std::uintptr_t kFirstPersonThunkRVA = 0x003A7CD0;
	inline constexpr std::size_t kThunkPrefixSize = 26;
	inline constexpr std::uint32_t kOriginalFirstPersonRIPDisplacement =
		0x01012FEE;
	// The preserved-function call is the final six bytes of kThunkPrefix, so
	// its RIP base is exactly thunk RVA + prefix size.
	inline constexpr std::uintptr_t kOriginalFirstPersonSlotRVA =
		kFirstPersonThunkRVA + kThunkPrefixSize +
		kOriginalFirstPersonRIPDisplacement;
	inline constexpr std::size_t kFiveByteCallBranchStubSize = 14;

	// Deferred::Hooks::Main_RenderFirstPersonView::thunk, through and including
	// its indirect call of the preserved native target.
	inline constexpr std::array<std::uint8_t, kThunkPrefixSize> kThunkPrefix{
		0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
		0x48, 0x8B, 0x1D, 0x2B, 0xA0, 0x01, 0x01,
		0x83, 0x8B, 0xD8, 0x01, 0x00, 0x00, 0x01,
		0xFF, 0x15, 0xEE, 0x2F, 0x01, 0x01
	};
	static_assert(kOriginalFirstPersonRIPDisplacement ==
		(static_cast<std::uint32_t>(kThunkPrefix[22]) |
		 (static_cast<std::uint32_t>(kThunkPrefix[23]) << 8) |
		 (static_cast<std::uint32_t>(kThunkPrefix[24]) << 16) |
		 (static_cast<std::uint32_t>(kThunkPrefix[25]) << 24)));
	static_assert(kOriginalFirstPersonSlotRVA == 0x013BACD8);
	static_assert(kFirstPersonThunkRVA + kThunkPrefix.size() <= kSizeOfImage);
	static_assert(kOriginalFirstPersonSlotRVA + sizeof(std::uintptr_t) <=
		kSizeOfImage);

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
		const ModuleIdentity& identity) noexcept
	{
		return identity.machine == kMachineAMD64 &&
		       identity.sectionCount == kSectionCount &&
		       identity.optionalMagic == kPE32PlusMagic &&
		       identity.timeDateStamp == kTimeDateStamp &&
		       identity.sizeOfCode == kSizeOfCode &&
		       identity.sizeOfImage == kSizeOfImage;
	}

	[[nodiscard]] constexpr bool MatchesThunkPrefix(
		const std::span<const std::uint8_t> prefix) noexcept
	{
		if (prefix.size() != kThunkPrefix.size())
			return false;
		for (std::size_t index = 0; index < kThunkPrefix.size(); ++index) {
			if (prefix[index] != kThunkPrefix[index])
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool DecodeFiveByteCallBranchStub(
		const std::span<const std::uint8_t> stub,
		std::uintptr_t& destination) noexcept
	{
		destination = 0;
		if (stub.size() != kFiveByteCallBranchStubSize ||
			stub[0] != 0xFF || stub[1] != 0x25 || stub[2] != 0 ||
			stub[3] != 0 || stub[4] != 0 || stub[5] != 0) {
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

	[[nodiscard]] constexpr ChainKind Classify(
		const Evidence& evidence) noexcept
	{
		if (!evidence.expectedNativeTarget || !evidence.inspectedCallTarget)
			return ChainKind::kRejected;
		if (evidence.inspectedCallTarget == evidence.expectedNativeTarget)
			return ChainKind::kNative;

		if (!evidence.communityShadersBase ||
			!MatchesModuleIdentity(evidence.module) ||
			!MatchesThunkPrefix(evidence.thunkPrefix) ||
			evidence.wrapperOriginalTarget != evidence.expectedNativeTarget ||
			evidence.communityShadersBase >
				(std::numeric_limits<std::uintptr_t>::max)() -
				kFirstPersonThunkRVA) {
			return ChainKind::kRejected;
		}

		const auto expectedThunk =
			evidence.communityShadersBase + kFirstPersonThunkRVA;
		if (evidence.inspectedCallTarget == expectedThunk)
			return ChainKind::kCommunityShaders184Direct;

		std::uintptr_t decodedStubTarget = 0;
		if (!DecodeFiveByteCallBranchStub(
				evidence.inspectedBranchStub, decodedStubTarget) ||
			decodedStubTarget != expectedThunk) {
			return ChainKind::kRejected;
		}
		return ChainKind::kCommunityShaders184BranchStub;
	}

	[[nodiscard]] constexpr const char* ToString(const ChainKind kind) noexcept
	{
		switch (kind) {
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
