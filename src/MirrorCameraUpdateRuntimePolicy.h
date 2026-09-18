#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MirrorCameraUpdateRuntimePolicy
{
	enum class Runtime : std::uint8_t
	{
		kUnsupported,
		kSkyrimSE1597,
		kSkyrimAE161170,
		// Skyrim 1.7.104: same Address Library ID (77258) and the same 36-byte
		// entry window as AE 1.6.1170; only the RVA moves.
		kSkyrimAE17104,
		// GOG 1.6.1179: ID 77258 -> 0xE47110; Ghidra Combined confirmed the
		// 36-byte AE entry window including the masked RIP-relative flag.
		kSkyrimAE161179,
		kSkyrimVR1415
	};

	struct Contract
	{
		std::uint64_t updateCameraDataID{ 0 };
		std::uintptr_t updateCameraDataRVA{ 0 };
	};

	inline constexpr Contract kSE1597{
		.updateCameraDataID = 75472,
		.updateCameraDataRVA = 0xD6B210
	};

	inline constexpr Contract kAE161170{
		.updateCameraDataID = 77258,
		.updateCameraDataRVA = 0xE45680
	};

	// Skyrim 1.7.104: BSGraphics::State::UpdateCameraData ID 77258 -> 0x100AE80.
	// The entry window is byte-identical to AE outside its masked displacements,
	// so kEntryWindow / MatchesEntry are reused unchanged.
	inline constexpr Contract kAE17104{
		.updateCameraDataID = 77258,
		.updateCameraDataRVA = 0x100AE80
	};

	// GOG 1.6.1179: BSGraphics::State::UpdateCameraData ID 77258 -> 0xE47110.
	inline constexpr Contract kAE161179{
		.updateCameraDataID = 77258,
		.updateCameraDataRVA = 0xE47110
	};

	// Skyrim VR 1.4.15: BSGraphics::State::UpdateCameraData is 0xDBCE30 (the SE
	// ID 75472 is mapped by the VR Address Library).  The VR routine uploads
	// both eye ViewData slots; its two-argument `(ctx, flags)` ABI is proven by
	// the `mov edi,edx` in the VR entry window below.
	inline constexpr Contract kVR1415{
		.updateCameraDataID = 75472,
		.updateCameraDataRVA = 0xDBCE30
	};

	[[nodiscard]] constexpr const Contract* Select(
		const Runtime runtime) noexcept
	{
		switch (runtime) {
		case Runtime::kSkyrimSE1597:
			return &kSE1597;
		case Runtime::kSkyrimAE161170:
			return &kAE161170;
		case Runtime::kSkyrimAE17104:
			return &kAE17104;
		case Runtime::kSkyrimAE161179:
			return &kAE161179;
		case Runtime::kSkyrimVR1415:
			return &kVR1415;
		default:
			return nullptr;
		}
	}

	// The first 36 bytes are instruction-identical on the two exact flat
	// runtimes.  Only the RIP-relative byte flag and conditional-branch
	// displacements move.  `mov ebx,edx` proves the two-argument hook ABI before
	// a detour may be installed.
	inline constexpr std::array<std::uint8_t, 36> kEntryWindow{
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x55,
		0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFD, 0xFF, 0xFF,
		0x48, 0x81, 0xEC, 0x20, 0x03, 0x00, 0x00,
		0x80, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x8B, 0xDA,
		0x0F, 0x84, 0x00, 0x00, 0x00, 0x00
	};

	[[nodiscard]] constexpr bool IsDisplacementByte(
		const std::size_t index) noexcept
	{
		return (index >= 23 && index <= 26) ||
			(index >= 32 && index <= 35);
	}

	[[nodiscard]] constexpr bool MatchesEntry(
		const std::span<const std::uint8_t> observed) noexcept
	{
		if (observed.size() != kEntryWindow.size())
			return false;
		for (std::size_t index = 0; index < observed.size(); ++index) {
			if (!IsDisplacementByte(index) &&
				observed[index] != kEntryWindow[index]) {
				return false;
			}
		}
		return true;
	}

	// VR 1.4.15 entry of 0xDBCE30: `mov rax,rsp; push rbp; push rdi;
	// lea rbp,[rax-0x538]; sub rsp,0x628; cmp byte [rip+disp],0; mov edi,edx;
	// je rel32; cmp dword [rip+...`.  The RIP displacement (bytes 21..24) and
	// the branch displacement (30..33) are masked.
	inline constexpr std::array<std::uint8_t, 36> kVREntryWindow{
		0x48, 0x8B, 0xC4, 0x55, 0x57,
		0x48, 0x8D, 0xA8, 0xC8, 0xFA, 0xFF, 0xFF,
		0x48, 0x81, 0xEC, 0x28, 0x06, 0x00, 0x00,
		0x80, 0x3D, 0x16, 0x9E, 0x3C, 0x02, 0x00,
		0x8B, 0xFA,
		0x0F, 0x84, 0x88, 0x07, 0x00, 0x00,
		0x83, 0x3D
	};

	[[nodiscard]] constexpr bool IsVRDisplacementByte(
		const std::size_t index) noexcept
	{
		return (index >= 21 && index <= 24) ||
			(index >= 30 && index <= 33);
	}

	[[nodiscard]] constexpr bool MatchesVREntry(
		const std::span<const std::uint8_t> observed) noexcept
	{
		if (observed.size() != kVREntryWindow.size())
			return false;
		for (std::size_t index = 0; index < observed.size(); ++index) {
			if (!IsVRDisplacementByte(index) &&
				observed[index] != kVREntryWindow[index]) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] constexpr bool MatchesEntryFor(
		const Runtime runtime,
		const std::span<const std::uint8_t> observed) noexcept
	{
		switch (runtime) {
		case Runtime::kSkyrimSE1597:
		case Runtime::kSkyrimAE161170:
		// 1.7.104 shares the flat entry window; it is listed explicitly so a new
		// runtime can never fall through to the flat matcher by accident.
		case Runtime::kSkyrimAE17104:
		case Runtime::kSkyrimAE161179:
			return MatchesEntry(observed);
		case Runtime::kSkyrimVR1415:
			return MatchesVREntry(observed);
		default:
			return false;
		}
	}
}
