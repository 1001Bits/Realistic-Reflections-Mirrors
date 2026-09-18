#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace HandMirrorSoftEffectHookChainPolicy
{
	// This only selects the configuration. The caller must still verify the
	// Engine Fixes file, native tail, relay, owner address and owner prologue.
	[[nodiscard]] constexpr bool AcceptSEPeerConfiguration(
		bool standaloneRequested, bool communityShadersLoaded,
		bool communityShadersVerified) noexcept
	{
		return communityShadersLoaded ? communityShadersVerified : standaloneRequested;
	}

	enum class Runtime : std::uint8_t
	{
		kUnsupported,
		kSkyrimSE1597,
		kSkyrimAE161170,
		// Skyrim 1.7.104: SetupAndDrawPass (0x1560340) begins with exactly the
		// kNativeAE161170Entry bytes, so it shares the AE entry and tail arrays.
		kSkyrimAE17104,
		// GOG 1.6.1179: SetupAndDrawPass (0x14F4E30) keeps the AE 1.6.1170 entry
		// and tail; only the dispatcher RVA moves.  Engine Fixes is not pinned.
		kSkyrimAE161179,
		// Skyrim VR 1.4.15: BSBatchRenderer::SetupAndDrawPass (0x1349680) begins
		// with exactly the SE 1.5.97 entry and tail bytes.
		kSkyrimVR1415
	};

	inline constexpr std::array<std::uint8_t, 5> kNativeSE1597Entry{
		0x48, 0x89, 0x5C, 0x24, 0x10
	};
	inline constexpr std::array<std::uint8_t, 11> kNativeSE1597Tail{
		0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48
	};
	inline constexpr std::array<std::uint8_t, 16> kNativeAE161170Entry{
		0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54, 0x41, 0x55,
		0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80
	};
	// SafetyHook must relocate the first three AE instructions (3 + 1 + 2
	// bytes) before placing its five-byte E9.  Byte 5 is therefore padding owned
	// by the hook; the original continuation is provable from offset 6 onward.
	inline constexpr std::array<std::uint8_t, 10> kNativeAE161170Tail{
		0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81,
		0xEC, 0x80
	};
	inline constexpr std::array<std::uint8_t, 6> kSafetyHookAbsoluteRelay{
		0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
	};

	[[nodiscard]] inline bool IsNativeSE1597Entry(
		const std::span<const std::uint8_t> entry) noexcept
	{
		return entry.size() >=
				kNativeSE1597Entry.size() + kNativeSE1597Tail.size() &&
			std::equal(
				kNativeSE1597Entry.begin(), kNativeSE1597Entry.end(), entry.begin()) &&
			std::equal(
				kNativeSE1597Tail.begin(), kNativeSE1597Tail.end(),
				entry.begin() + kNativeSE1597Entry.size());
	}

	[[nodiscard]] inline bool HasNativeSE1597Tail(
		const std::span<const std::uint8_t> entry) noexcept
	{
		return entry.size() >= 5 + kNativeSE1597Tail.size() &&
			std::equal(
				kNativeSE1597Tail.begin(), kNativeSE1597Tail.end(),
				entry.begin() + 5);
	}

	[[nodiscard]] inline bool IsNativeAE161170Entry(
		const std::span<const std::uint8_t> entry) noexcept
	{
		return entry.size() >= kNativeAE161170Entry.size() &&
			std::equal(
				kNativeAE161170Entry.begin(), kNativeAE161170Entry.end(),
				entry.begin());
	}

	[[nodiscard]] inline bool HasNativeAE161170Tail(
		const std::span<const std::uint8_t> entry) noexcept
	{
		return entry.size() >= 6 + kNativeAE161170Tail.size() &&
			std::equal(
				kNativeAE161170Tail.begin(), kNativeAE161170Tail.end(),
				entry.begin() + 6);
	}

	[[nodiscard]] inline bool IsNativeEntry(
		const Runtime runtime,
		const std::span<const std::uint8_t> entry) noexcept
	{
		switch (runtime) {
		case Runtime::kSkyrimSE1597:
		case Runtime::kSkyrimVR1415:
			return IsNativeSE1597Entry(entry);
		case Runtime::kSkyrimAE161170:
		// 1.7.104's entry is byte-identical to AE 1.6.1170; listed explicitly so a
		// future runtime cannot silently inherit the AE shape.
		case Runtime::kSkyrimAE17104:
		case Runtime::kSkyrimAE161179:
			return IsNativeAE161170Entry(entry);
		default:
			return false;
		}
	}

	[[nodiscard]] inline bool HasNativeTail(
		const Runtime runtime,
		const std::span<const std::uint8_t> entry) noexcept
	{
		switch (runtime) {
		case Runtime::kSkyrimSE1597:
		case Runtime::kSkyrimVR1415:
			return HasNativeSE1597Tail(entry);
		case Runtime::kSkyrimAE161170:
		case Runtime::kSkyrimAE17104:
		case Runtime::kSkyrimAE161179:
			return HasNativeAE161170Tail(entry);
		default:
			return false;
		}
	}

	[[nodiscard]] inline bool DecodeRelativeJump(
		const std::uintptr_t entryAddress,
		const std::span<const std::uint8_t> entry,
		std::uintptr_t& target) noexcept
	{
		target = 0;
		if (!entryAddress || entry.size() < 5 || entry[0] != 0xE9 ||
			entryAddress > std::numeric_limits<std::uintptr_t>::max() - 5) {
			return false;
		}
		std::int32_t displacement = 0;
		std::memcpy(&displacement, entry.data() + 1, sizeof(displacement));
		const auto next = entryAddress + 5;
		if (displacement >= 0) {
			const auto delta = static_cast<std::uintptr_t>(displacement);
			if (next > std::numeric_limits<std::uintptr_t>::max() - delta)
				return false;
			target = next + delta;
		} else {
			const auto delta = static_cast<std::uintptr_t>(
				-static_cast<std::int64_t>(displacement));
			if (next < delta)
				return false;
			target = next - delta;
		}
		return target != 0;
	}

	[[nodiscard]] inline bool DecodeSafetyHookAbsoluteRelay(
		const std::span<const std::uint8_t> relay,
		std::uintptr_t& target) noexcept
	{
		target = 0;
		if (relay.size() < kSafetyHookAbsoluteRelay.size() + sizeof(target) ||
			!std::equal(
				kSafetyHookAbsoluteRelay.begin(), kSafetyHookAbsoluteRelay.end(),
				relay.begin())) {
			return false;
		}
		std::memcpy(
			&target, relay.data() + kSafetyHookAbsoluteRelay.size(), sizeof(target));
		return target != 0;
	}

	/**
	 * Accept only the exact entry shape produced by SafetyHook's x64 inline
	 * trampoline while the untouched bytes still prove the pinned Skyrim
	 * function. Module/file identity and the exact owner RVA are deliberately
	 * checked by the runtime caller, where those facts are available.
	 */
	[[nodiscard]] inline bool IsPinnedSafetyHookEntryShape(
		const std::uintptr_t entryAddress,
		const std::span<const std::uint8_t> entry,
		const std::span<const std::uint8_t> relay,
		const std::uintptr_t expectedRelayAddress,
		const std::uintptr_t expectedOwnerAddress) noexcept
	{
		std::uintptr_t decodedRelay = 0;
		std::uintptr_t decodedOwner = 0;
		return HasNativeSE1597Tail(entry) &&
			DecodeRelativeJump(entryAddress, entry, decodedRelay) &&
			decodedRelay == expectedRelayAddress &&
			DecodeSafetyHookAbsoluteRelay(relay, decodedOwner) &&
			decodedOwner == expectedOwnerAddress;
	}

	[[nodiscard]] inline bool IsPinnedSafetyHookEntryShape(
		const Runtime runtime,
		const std::uintptr_t entryAddress,
		const std::span<const std::uint8_t> entry,
		const std::span<const std::uint8_t> relay,
		const std::uintptr_t expectedRelayAddress,
		const std::uintptr_t expectedOwnerAddress) noexcept
	{
		std::uintptr_t decodedRelay = 0;
		std::uintptr_t decodedOwner = 0;
		return HasNativeTail(runtime, entry) &&
			DecodeRelativeJump(entryAddress, entry, decodedRelay) &&
			decodedRelay == expectedRelayAddress &&
			DecodeSafetyHookAbsoluteRelay(relay, decodedOwner) &&
			decodedOwner == expectedOwnerAddress;
	}
}
