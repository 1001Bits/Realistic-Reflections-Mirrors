#pragma once

#include "HandSurfaceLightCohortPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace HandSurfaceLightCallSiteSignature
{
	using Family = HandSurfaceLightCohortPolicy::CallFamily;

	enum class Runtime : std::uint8_t
	{
		kUnsupported,
		kSkyrimSE1597,
		kSkyrimAE161170,
		// Skyrim 1.7.104: the three call families, their prefixes and suffixes and
		// the main-build prologue are byte-identical to AE 1.6.1170.  Only the
		// three site RVAs and the two target RVAs move.
		kSkyrimAE17104,
		// GOG 1.6.1179: prefixes/suffixes and prologue match AE 1.6.1170; only
		// the three site RVAs and the two target RVAs move.
		kSkyrimAE161179,
		kSkyrimVR1415
	};

	struct Site
	{
		std::uintptr_t rva{ 0 };
		Family family{ Family::kMaximum16 };
		std::size_t prefixSize{ 0 };
		std::size_t suffixSize{ 0 };
		std::array<std::uint8_t, 48> prefix{};
		std::array<std::uint8_t, 5> suffix{};
	};

	struct RuntimeContract
	{
		std::uint64_t selectorTargetID{ 0 };
		std::uintptr_t selectorTargetRVA{ 0 };
		std::uint64_t mainBuildTargetID{ 0 };
		std::uintptr_t mainBuildTargetRVA{ 0 };
		std::size_t mainBuildPrologueSize{ 0 };
		std::array<std::uint8_t, 16> mainBuildPrologue{};
		// Skyrim VR's CalculateActiveLightsForSurface takes an eleventh stack
		// argument: uint32(BSShaderPropertyLightData*, BSLight**, int32 max,
		// int32* shadowCount, ShadowSceneNode*, BSLightingShaderProperty*,
		// bool addShadow, bool* useShadowSun, bool firstPerson,
		// uint32 firstPersonMask, bool extra).
		bool selectorHasEleventhArgument{ false };
	};

	// Skyrim VR 1.4.15 call sites of CalculateActiveLightsForSurface (0x1354D20):
	// 0x13045F0+0x763, 0x1314290+0x2EC and 0x1316ED0+0x381, decoded from the
	// VR image.  The kMaximum7 caller spills its extra argument to [rsp+0x38].
	inline constexpr std::array<Site, 3> kVR1415Sites{
		Site{
			.rva = 0x1304D53u,
			.family = Family::kMaximum16,
			.prefixSize = 19,
			.suffixSize = 2,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x68,
				0x41, 0xB8, 0x10, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x55, 0xD0,
				0x49, 0x8B, 0x4F, 0x70 },
			.suffix = { 0x8B, 0xF8 }
		},
		Site{
			.rva = 0x131457Cu,
			.family = Family::kMaximum5,
			.prefixSize = 20,
			.suffixSize = 5,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x64,
				0x41, 0xB8, 0x05, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x54, 0x24, 0x78,
				0x49, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x4C, 0x8D, 0x44, 0x24, 0x78 }
		},
		Site{
			.rva = 0x1317251u,
			.family = Family::kMaximum7,
			.prefixSize = 39,
			.suffixSize = 5,
			.prefix = {
				0x48, 0x8B, 0x80, 0x70, 0x01, 0x00, 0x00,
				0x48, 0x89, 0x54, 0x24, 0x38,
				0x48, 0x8D, 0x54, 0x24, 0x70,
				0x88, 0x4C, 0x24, 0x30,
				0x88, 0x4C, 0x24, 0x60,
				0x48, 0x8B, 0x4F, 0x70,
				0x48, 0x89, 0x7C, 0x24, 0x28,
				0x48, 0x89, 0x44, 0x24, 0x20 },
			.suffix = { 0x8B, 0xD8, 0x83, 0xF8, 0x01 }
		}
	};

	inline constexpr std::array<Site, 3> kSE1597Sites{
		Site{
			.rva = 0x12C693Fu,
			.family = Family::kMaximum16,
			.prefixSize = 19,
			.suffixSize = 2,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xB8, 0x10, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x55, 0xC0,
				0x49, 0x8B, 0x4F, 0x70 },
			.suffix = { 0x8B, 0xF8 }
		},
		Site{
			.rva = 0x12D562Au,
			.family = Family::kMaximum5,
			.prefixSize = 20,
			.suffixSize = 5,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x54,
				0x41, 0xB8, 0x05, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x54, 0x24, 0x68,
				0x49, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x4C, 0x8D, 0x44, 0x24, 0x68 }
		},
		Site{
			.rva = 0x12D7DC9u,
			.family = Family::kMaximum7,
			.prefixSize = 40,
			.suffixSize = 5,
			.prefix = {
				0x41, 0xBC, 0x07, 0x00, 0x00, 0x00,
				0x48, 0x8B, 0x80, 0x48, 0x01, 0x00, 0x00,
				0x45, 0x8B, 0xC4,
				0x48, 0x8B, 0x4F, 0x70,
				0xC6, 0x44, 0x24, 0x30, 0x01,
				0x48, 0x89, 0x7C, 0x24, 0x28,
				0x48, 0x89, 0x44, 0x24, 0x20,
				0xC6, 0x44, 0x24, 0x50, 0x01 },
			.suffix = { 0x8B, 0xD8, 0x83, 0xF8, 0x01 }
		}
	};

	// Exact AE 1.6.1170 calls to CalculateActiveLightsForSurface (ID 107784,
	// RVA 0x14FCF80).  The first two compiler families retain the SE argument
	// setup; the maximum-7 family has AE's larger stack frame and r12 maximum.
	inline constexpr std::array<Site, 3> kAE161170Sites{
		Site{
			.rva = 0x14AE6DCu,
			.family = Family::kMaximum16,
			.prefixSize = 19,
			.suffixSize = 2,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xB8, 0x10, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x55, 0xB0,
				0x49, 0x8B, 0x4F, 0x70 },
			.suffix = { 0x8B, 0xF8 }
		},
		Site{
			.rva = 0x14BDB38u,
			.family = Family::kMaximum5,
			.prefixSize = 20,
			.suffixSize = 5,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x54,
				0x41, 0xB8, 0x05, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x54, 0x24, 0x68,
				0x49, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x4C, 0x8D, 0x44, 0x24, 0x68 }
		},
		Site{
			.rva = 0x14C082Cu,
			.family = Family::kMaximum7,
			.prefixSize = 41,
			.suffixSize = 5,
			.prefix = {
				0xC6, 0x44, 0x24, 0x30, 0x01,
				0x48, 0x89, 0x74, 0x24, 0x28,
				0x48, 0x89, 0x44, 0x24, 0x20,
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xBC, 0x07, 0x00, 0x00, 0x00,
				0x45, 0x8B, 0xC4,
				0x48, 0x8D, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00,
				0x48, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x8B, 0xD8, 0x83, 0xF8, 0x01 }
		}
	};

	// Skyrim 1.7.104 calls to CalculateActiveLightsForSurface (ID 107784,
	// RVA 0x1569550).  Every prefix and suffix byte matched the AE arrays against
	// the real 1.7.104 image; only the three call-site RVAs move.
	inline constexpr std::array<Site, 3> kAE17104Sites{
		Site{
			.rva = 0x151A88Cu,
			.family = Family::kMaximum16,
			.prefixSize = 19,
			.suffixSize = 2,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xB8, 0x10, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x55, 0xB0,
				0x49, 0x8B, 0x4F, 0x70 },
			.suffix = { 0x8B, 0xF8 }
		},
		Site{
			.rva = 0x1529F58u,
			.family = Family::kMaximum5,
			.prefixSize = 20,
			.suffixSize = 5,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x54,
				0x41, 0xB8, 0x05, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x54, 0x24, 0x68,
				0x49, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x4C, 0x8D, 0x44, 0x24, 0x68 }
		},
		Site{
			.rva = 0x152CC4Cu,
			.family = Family::kMaximum7,
			.prefixSize = 41,
			.suffixSize = 5,
			.prefix = {
				0xC6, 0x44, 0x24, 0x30, 0x01,
				0x48, 0x89, 0x74, 0x24, 0x28,
				0x48, 0x89, 0x44, 0x24, 0x20,
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xBC, 0x07, 0x00, 0x00, 0x00,
				0x45, 0x8B, 0xC4,
				0x48, 0x8D, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00,
				0x48, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x8B, 0xD8, 0x83, 0xF8, 0x01 }
		}
	};

	// GOG 1.6.1179 calls to CalculateActiveLightsForSurface (ID 107784,
	// RVA 0x14FDFF0).  Ghidra Combined confirmed every prefix and suffix byte
	// against the mapped call sites; only the three RVAs move.
	inline constexpr std::array<Site, 3> kAE161179Sites{
		Site{
			.rva = 0x14AF77Cu,
			.family = Family::kMaximum16,
			.prefixSize = 19,
			.suffixSize = 2,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xB8, 0x10, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x55, 0xB0,
				0x49, 0x8B, 0x4F, 0x70 },
			.suffix = { 0x8B, 0xF8 }
		},
		Site{
			.rva = 0x14BEBD8u,
			.family = Family::kMaximum5,
			.prefixSize = 20,
			.suffixSize = 5,
			.prefix = {
				0x4C, 0x8D, 0x4C, 0x24, 0x54,
				0x41, 0xB8, 0x05, 0x00, 0x00, 0x00,
				0x48, 0x8D, 0x54, 0x24, 0x68,
				0x49, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x4C, 0x8D, 0x44, 0x24, 0x68 }
		},
		Site{
			.rva = 0x14C18CCu,
			.family = Family::kMaximum7,
			.prefixSize = 41,
			.suffixSize = 5,
			.prefix = {
				0xC6, 0x44, 0x24, 0x30, 0x01,
				0x48, 0x89, 0x74, 0x24, 0x28,
				0x48, 0x89, 0x44, 0x24, 0x20,
				0x4C, 0x8D, 0x4C, 0x24, 0x58,
				0x41, 0xBC, 0x07, 0x00, 0x00, 0x00,
				0x45, 0x8B, 0xC4,
				0x48, 0x8D, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00,
				0x48, 0x8B, 0x4E, 0x70 },
			.suffix = { 0x8B, 0xD8, 0x83, 0xF8, 0x01 }
		}
	};

	inline constexpr RuntimeContract kSE1597Contract{
		.selectorTargetID = 100997,
		.selectorTargetRVA = 0x1310E10,
		.mainBuildTargetID = 35630,
		.mainBuildTargetRVA = 0x5B7C80,
		.mainBuildPrologueSize = 5,
		.mainBuildPrologue = {
			0x40, 0x55, 0x56, 0x57, 0x41 }
	};

	inline constexpr RuntimeContract kAE161170Contract{
		.selectorTargetID = 107784,
		.selectorTargetRVA = 0x14FCF80,
		.mainBuildTargetID = 36643,
		.mainBuildTargetRVA = 0x64BC20,
		.mainBuildPrologueSize = 16,
		.mainBuildPrologue = {
			0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
			0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83 }
	};

	// Skyrim 1.7.104: selector 107784 -> 0x1569550, main build job 36643 ->
	// 0x65E610.  The 16-byte prologue is byte-identical to AE 1.6.1170 and the
	// selector keeps the ten-argument flat ABI.
	inline constexpr RuntimeContract kAE17104Contract{
		.selectorTargetID = 107784,
		.selectorTargetRVA = 0x1569550,
		.mainBuildTargetID = 36643,
		.mainBuildTargetRVA = 0x65E610,
		.mainBuildPrologueSize = 16,
		.mainBuildPrologue = {
			0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
			0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83 },
		.selectorHasEleventhArgument = false
	};

	// GOG 1.6.1179: selector 107784 -> 0x14FDFF0, main build job 36643 ->
	// 0x64DE80.  The 16-byte prologue is byte-identical to AE 1.6.1170.
	inline constexpr RuntimeContract kAE161179Contract{
		.selectorTargetID = 107784,
		.selectorTargetRVA = 0x14FDFF0,
		.mainBuildTargetID = 36643,
		.mainBuildTargetRVA = 0x64DE80,
		.mainBuildPrologueSize = 16,
		.mainBuildPrologue = {
			0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
			0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83 },
		.selectorHasEleventhArgument = false
	};

	// VR: selector 0x1354D20 (VR Address Library maps SE ID 100997 to it); the
	// main build job function queued by RenderPlayerView is 0x5BFC90 (SE ID
	// 35630 is absent from the VR CSV, so the RVA is authoritative there).  Its
	// prologue is byte-identical to AE 1.6.1170.
	inline constexpr RuntimeContract kVR1415Contract{
		.selectorTargetID = 100997,
		.selectorTargetRVA = 0x1354D20,
		.mainBuildTargetID = 35630,
		.mainBuildTargetRVA = 0x5BFC90,
		.mainBuildPrologueSize = 16,
		.mainBuildPrologue = {
			0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54,
			0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83 },
		.selectorHasEleventhArgument = true
	};

	[[nodiscard]] constexpr const RuntimeContract* SelectContract(
		const Runtime runtime) noexcept
	{
		switch (runtime) {
		case Runtime::kSkyrimSE1597:
			return &kSE1597Contract;
		case Runtime::kSkyrimAE161170:
			return &kAE161170Contract;
		case Runtime::kSkyrimAE17104:
			return &kAE17104Contract;
		case Runtime::kSkyrimAE161179:
			return &kAE161179Contract;
		case Runtime::kSkyrimVR1415:
			return &kVR1415Contract;
		default:
			return nullptr;
		}
	}

	[[nodiscard]] constexpr std::span<const Site> SelectSites(
		const Runtime runtime) noexcept
	{
		switch (runtime) {
		case Runtime::kSkyrimSE1597:
			return std::span<const Site>{ kSE1597Sites };
		case Runtime::kSkyrimAE161170:
			return std::span<const Site>{ kAE161170Sites };
		case Runtime::kSkyrimAE17104:
			return std::span<const Site>{ kAE17104Sites };
		case Runtime::kSkyrimAE161179:
			return std::span<const Site>{ kAE161179Sites };
		case Runtime::kSkyrimVR1415:
			return std::span<const Site>{ kVR1415Sites };
		default:
			return {};
		}
	}

	[[nodiscard]] constexpr bool Matches(
		const Site& site,
		const std::span<const std::uint8_t> prefix,
		const std::span<const std::uint8_t> suffix) noexcept
	{
		if (site.prefixSize > site.prefix.size() ||
			site.suffixSize > site.suffix.size() ||
			prefix.size() != site.prefixSize || suffix.size() != site.suffixSize) {
			return false;
		}
		for (std::size_t index = 0; index < prefix.size(); ++index) {
			if (prefix[index] != site.prefix[index])
				return false;
		}
		for (std::size_t index = 0; index < suffix.size(); ++index) {
			if (suffix[index] != site.suffix[index])
				return false;
		}
		return true;
	}
}
