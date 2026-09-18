#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace HandMirrorUnifiedCullRuntimePolicy
{
	enum class Runtime : std::uint8_t
	{
		kUnsupported,
		kSkyrimSE1597,
		kSkyrimAE161170,
		// Skyrim 1.7.104: same Address Library IDs and the same byte windows as
		// AE 1.6.1170; only the six function RVAs move.
		kSkyrimAE17104,
		// GOG 1.6.1179: same IDs and byte windows as AE 1.6.1170; only the RVAs
		// move (versionlib-1-6-1179-0.bin, Ghidra Combined SkyrimAE_GOG Edition).
		kSkyrimAE161179,
		kSkyrimVR1415
	};

	struct Contract
	{
		std::uint64_t initializeDescriptorID{ 0 };
		std::uintptr_t initializeDescriptorRVA{ 0 };
		std::uint64_t cullFromDescriptorID{ 0 };
		std::uintptr_t cullFromDescriptorRVA{ 0 };
		std::ptrdiff_t routeZeroWindowOffset{ 0 };
		std::uint64_t niAVObjectCullID{ 0 };
		std::uintptr_t niAVObjectCullRVA{ 0 };
		std::uint64_t niNodeOnVisibleID{ 0 };
		std::uintptr_t niNodeOnVisibleRVA{ 0 };
		std::ptrdiff_t childCullWindowOffset{ 0 };
		std::uint64_t finalizeCollectionID{ 0 };
		std::uintptr_t finalizeCollectionRVA{ 0 };
		std::uint64_t clearCollectionID{ 0 };
		std::uintptr_t clearCollectionRVA{ 0 };
		// Skyrim VR routes BSCullingProcess::ClearCollectedGeometry through the
		// vtable (slot 0x1A, +0xD0) right after the direct finalize call, so the
		// VR route-zero window has one masked call and no direct clear target.
		bool clearIsVirtual{ false };
		std::uint8_t clearVirtualSlot{ 0 };
		// None of the six VR functions are in the VR Address Library; on VR the
		// RVAs are authoritative and the IDs are informational.
		bool rvaAuthoritative{ false };
	};

	// Skyrim VR 1.4.15 native cull job descriptor (0x78 bytes; the flat layout
	// is 0x68).  VR inserts a second camera after the primary camera and one
	// extra pointer after the culling object list:
	//   00 shaderAccumulator, 08 secondaryAccumulator, 10 camera,
	//   18 secondaryCamera (VR only), 20 compoundFrustum, 28 frustum,
	//   30 portalGraphEntry, 38 cullingProcess, 40 customCullPlanes, 48 scene,
	//   50 cullingObjects, 58 unk58 (VR only), 60 cullMode (u32),
	//   64 lightRadius, 68 sentinel (-1), 6C isParabolic, 6D isDirectionalLight,
	//   6E ignorePreprocess, 6F doCustomCullPlanes, 70 cameraRelatedUpdates,
	//   71 unk71, 72 updateAccumulateFlag, 73..77 pad.
	inline constexpr std::size_t kFlatDescriptorSize = 0x68;
	inline constexpr std::size_t kVRDescriptorSize = 0x78;

	// IDs and RVAs are exact Address Library/executable pairs.  The initializer,
	// app-cull helper, and masked call windows below are byte-identical across the
	// two flat runtimes; only the containing function offsets and rel32 values move.
	inline constexpr Contract kSE1597{
		.initializeDescriptorID = 100211,
		.initializeDescriptorRVA = 0x12D6B40,
		.cullFromDescriptorID = 100213,
		.cullFromDescriptorRVA = 0x12D6BB0,
		.routeZeroWindowOffset = 0x16B,
		.niAVObjectCullID = 68916,
		.niAVObjectCullRVA = 0xC57140,
		.niNodeOnVisibleID = 68956,
		.niNodeOnVisibleRVA = 0xC58C70,
		.childCullWindowOffset = 0x60,
		.finalizeCollectionID = 74809,
		.finalizeCollectionRVA = 0xD51280,
		.clearCollectionID = 74808,
		.clearCollectionRVA = 0xD51100
	};

	inline constexpr Contract kAE161170{
		.initializeDescriptorID = 106919,
		.initializeDescriptorRVA = 0x14BF2B0,
		.cullFromDescriptorID = 106921,
		.cullFromDescriptorRVA = 0x14BF320,
		.routeZeroWindowOffset = 0x103,
		.niAVObjectCullID = 70267,
		.niAVObjectCullRVA = 0xD1C570,
		.niNodeOnVisibleID = 70307,
		.niNodeOnVisibleRVA = 0xD1E2C0,
		.childCullWindowOffset = 0x60,
		.finalizeCollectionID = 76558,
		.finalizeCollectionRVA = 0xE28AF0,
		.clearCollectionID = 76557,
		.clearCollectionRVA = 0xE28970
	};

	// Skyrim 1.7.104 (versionlib-1-7-104-0.bin, 2026-09-05).  Every Address
	// Library ID is the AE one and every verified byte window -- the descriptor
	// initializer, the CullFromDescriptor prologue, NiAVObject::Cull, the child
	// cull window at +0x60 and the route-zero window at +0x103 -- matches the flat
	// arrays unchanged, so this contract only re-pins the RVAs.
	inline constexpr Contract kAE17104{
		.initializeDescriptorID = 106919,
		.initializeDescriptorRVA = 0x152B6D0,
		.cullFromDescriptorID = 106921,
		.cullFromDescriptorRVA = 0x152B740,
		.routeZeroWindowOffset = 0x103,
		.niAVObjectCullID = 70267,
		.niAVObjectCullRVA = 0xEE0F30,
		.niNodeOnVisibleID = 70307,
		.niNodeOnVisibleRVA = 0xEE2CC0,
		.childCullWindowOffset = 0x60,
		.finalizeCollectionID = 76558,
		.finalizeCollectionRVA = 0xFEE1D0,
		.clearCollectionID = 76557,
		.clearCollectionRVA = 0xFEE050,
		.clearIsVirtual = false
	};

	// GOG 1.6.1179 (versionlib-1-6-1179-0.bin).  Every Address Library ID is the
	// AE one and Ghidra Combined confirmed the descriptor initializer, cull
	// prologue, and OnVisible rel32 match AE 1.6.1170; only the RVAs move.
	inline constexpr Contract kAE161179{
		.initializeDescriptorID = 106919,
		.initializeDescriptorRVA = 0x14C0350,
		.cullFromDescriptorID = 106921,
		.cullFromDescriptorRVA = 0x14C03C0,
		.routeZeroWindowOffset = 0x103,
		.niAVObjectCullID = 70267,
		.niAVObjectCullRVA = 0xD1DF90,
		.niNodeOnVisibleID = 70307,
		.niNodeOnVisibleRVA = 0xD1FCE0,
		.childCullWindowOffset = 0x60,
		.finalizeCollectionID = 76558,
		.finalizeCollectionRVA = 0xE2A510,
		.clearCollectionID = 76557,
		.clearCollectionRVA = 0xE2A390,
		.clearIsVirtual = false
	};

	// VR 1.4.15 (Ghidra Combined /Skyrim/SkyrimVR_1_4_15.exe, 2026-09-04):
	// InitializeDescriptor 0x1315AC0, CullFromDescriptor 0x1315B40 (both proven
	// by the RenderFirstPersonView call order), NiAVObject::Cull 0xC9C330 and
	// NiNode::OnVisible 0xC9E0D0 (ported names, child window verified),
	// finalize 0xD9A190 (direct call in the VR route-zero window).
	inline constexpr Contract kVR1415{
		.initializeDescriptorID = 100211,
		.initializeDescriptorRVA = 0x1315AC0,
		.cullFromDescriptorID = 100213,
		.cullFromDescriptorRVA = 0x1315B40,
		.routeZeroWindowOffset = 0x1B0,
		.niAVObjectCullID = 68916,
		.niAVObjectCullRVA = 0xC9C330,
		.niNodeOnVisibleID = 68956,
		.niNodeOnVisibleRVA = 0xC9E0D0,
		.childCullWindowOffset = 0x60,
		.finalizeCollectionID = 74809,
		.finalizeCollectionRVA = 0xD9A190,
		.clearCollectionID = 74808,
		.clearCollectionRVA = 0,
		.clearIsVirtual = true,
		.clearVirtualSlot = 0x1A,
		.rvaAuthoritative = true
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

	inline constexpr std::array<std::uint8_t, 68> kDescriptorInitializer{
		0x33, 0xC0, 0x48, 0x89, 0x01, 0x48, 0x89, 0x41,
		0x08, 0x48, 0x89, 0x41, 0x10, 0x48, 0x89, 0x41,
		0x18, 0x48, 0x89, 0x41, 0x20, 0x48, 0x89, 0x41,
		0x28, 0x48, 0x89, 0x41, 0x30, 0x48, 0x89, 0x41,
		0x38, 0x48, 0x89, 0x41, 0x40, 0x48, 0x89, 0x41,
		0x48, 0x48, 0x89, 0x41, 0x50, 0x89, 0x41, 0x5C,
		0x88, 0x41, 0x62, 0x48, 0x8B, 0xC1, 0xC7, 0x41,
		0x58, 0xFF, 0xFF, 0xFF, 0xFF, 0x66, 0xC7, 0x41,
		0x60, 0x01, 0x00, 0xC3
	};

	inline constexpr std::array<std::uint8_t, 24> kCullFromDescriptorPrologue{
		0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54, 0x41, 0x55,
		0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
		0x48, 0xC7, 0x40, 0xC8, 0xFE, 0xFF, 0xFF, 0xFF
	};

	inline constexpr std::array<std::uint8_t, 29> kNiAVObjectCull{
		0xF6, 0x81, 0xF4, 0x00, 0x00, 0x00, 0x01, 0x4C,
		0x8B, 0xCA, 0x75, 0x10, 0x48, 0x8B, 0x02, 0x48,
		0x8B, 0xD1, 0x49, 0x8B, 0xC9, 0x48, 0xFF, 0xA0,
		0xB0, 0x00, 0x00, 0x00, 0xC3
	};

	inline constexpr std::array<std::uint8_t, 15> kChildCullWindow{
		0x44, 0x8B, 0xC5, 0x49, 0x8B, 0xD6, 0xE8, 0x00,
		0x00, 0x00, 0x00, 0x48, 0x83, 0xC3, 0x08
	};
	inline constexpr std::size_t kChildCullCallOffset = 6;

	inline constexpr std::array<std::uint8_t, 24> kRouteZeroWindow{
		0x45, 0x85, 0xFF, 0x75, 0x15, 0x48, 0x8B, 0x16,
		0x48, 0x8B, 0xCF, 0xE8, 0x00, 0x00, 0x00, 0x00,
		0x48, 0x8B, 0xCF, 0xE8, 0x00, 0x00, 0x00, 0x00
	};
	inline constexpr std::size_t kFinalizeCallOffset = 11;
	inline constexpr std::size_t kClearCallOffset = 19;

	// VR 1.4.15 byte windows.  The descriptor initializer grew by two pointer
	// stores (0x58/0x60) and its scalar tail moved by +0x10; the cull prologue
	// spills rbx/rbp/rsi/rdi; NiAVObject::Cull tests the flag dword at +0x10C.
	inline constexpr std::array<std::uint8_t, 76> kVRDescriptorInitializer{
		0x33, 0xC0, 0x48, 0x89, 0x01, 0x48, 0x89, 0x41,
		0x08, 0x48, 0x89, 0x41, 0x10, 0x48, 0x89, 0x41,
		0x18, 0x48, 0x89, 0x41, 0x20, 0x48, 0x89, 0x41,
		0x28, 0x48, 0x89, 0x41, 0x30, 0x48, 0x89, 0x41,
		0x38, 0x48, 0x89, 0x41, 0x40, 0x48, 0x89, 0x41,
		0x48, 0x48, 0x89, 0x41, 0x50, 0x48, 0x89, 0x41,
		0x58, 0x48, 0x89, 0x41, 0x60, 0x89, 0x41, 0x6C,
		0x88, 0x41, 0x72, 0x48, 0x8B, 0xC1, 0xC7, 0x41,
		0x68, 0xFF, 0xFF, 0xFF, 0xFF, 0x66, 0xC7, 0x41,
		0x70, 0x01, 0x00, 0xC3
	};

	inline constexpr std::array<std::uint8_t, 24> kVRCullFromDescriptorPrologue{
		0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x56, 0x57, 0x41,
		0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
		0x40, 0x49, 0xC7, 0x43, 0xC0, 0xFE, 0xFF, 0xFF
	};

	inline constexpr std::array<std::uint8_t, 29> kVRNiAVObjectCull{
		0xF6, 0x81, 0x0C, 0x01, 0x00, 0x00, 0x01, 0x4C,
		0x8B, 0xCA, 0x75, 0x10, 0x48, 0x8B, 0x02, 0x48,
		0x8B, 0xD1, 0x49, 0x8B, 0xC9, 0x48, 0xFF, 0xA0,
		0xB0, 0x00, 0x00, 0x00, 0xC3
	};

	// CullFromDescriptor+0x1B0: `test ebp,ebp; jnz +0x1C; mov r8,[rax];
	// mov rdx,[r14]; mov rcx,rsi; call finalize; mov rax,[rsi]; mov rcx,rsi;
	// call qword [rax+0xD0]`.  Only the finalize rel32 is masked.
	inline constexpr std::array<std::uint8_t, 24> kVRRouteZeroWindow{
		0x85, 0xED, 0x75, 0x1C, 0x4C, 0x8B, 0x00, 0x49,
		0x8B, 0x16, 0x48, 0x8B, 0xCE, 0xE8, 0x00, 0x00,
		0x00, 0x00, 0x48, 0x8B, 0x06, 0x48, 0x8B, 0xCE
	};
	inline constexpr std::size_t kVRFinalizeCallOffset = 13;
	inline constexpr std::array<std::uint8_t, 6> kVRRouteZeroVirtualClear{
		0xFF, 0x90, 0xD0, 0x00, 0x00, 0x00
	};
	inline constexpr std::size_t kVRRouteZeroVirtualClearOffset = 24;
	inline constexpr std::size_t kVRRouteZeroVerifiedSize =
		kVRRouteZeroWindow.size() + kVRRouteZeroVirtualClear.size();

	[[nodiscard]] constexpr bool MatchesExact(
		const std::span<const std::uint8_t> observed,
		const std::span<const std::uint8_t> expected) noexcept
	{
		return observed.size() == expected.size() &&
			std::equal(expected.begin(), expected.end(), observed.begin());
	}

	[[nodiscard]] constexpr bool MatchesMaskedCallWindow(
		const std::span<const std::uint8_t> observed,
		const std::span<const std::uint8_t> expected,
		const std::span<const std::size_t> callOffsets) noexcept
	{
		if (observed.size() != expected.size())
			return false;
		for (std::size_t index = 0; index < observed.size(); ++index) {
			bool displacement = false;
			for (const auto callOffset : callOffsets) {
				displacement = displacement ||
					(index > callOffset && index < callOffset + 5);
			}
			if (!displacement && observed[index] != expected[index])
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool MatchesChildCullWindow(
		const std::span<const std::uint8_t> observed) noexcept
	{
		constexpr std::array offsets{ kChildCullCallOffset };
		return MatchesMaskedCallWindow(observed, kChildCullWindow, offsets);
	}

	[[nodiscard]] constexpr bool MatchesRouteZeroWindow(
		const std::span<const std::uint8_t> observed) noexcept
	{
		constexpr std::array offsets{ kFinalizeCallOffset, kClearCallOffset };
		return MatchesMaskedCallWindow(observed, kRouteZeroWindow, offsets);
	}

	/**
	 * VR: `observed` must hold kVRRouteZeroVerifiedSize bytes: the 24-byte
	 * masked window followed by the exact virtual clear call.
	 */
	[[nodiscard]] constexpr bool MatchesVRRouteZeroWindow(
		const std::span<const std::uint8_t> observed) noexcept
	{
		if (observed.size() != kVRRouteZeroVerifiedSize)
			return false;
		constexpr std::array offsets{ kVRFinalizeCallOffset };
		return MatchesMaskedCallWindow(
				   observed.first(kVRRouteZeroWindow.size()), kVRRouteZeroWindow,
				   offsets) &&
			std::equal(
				kVRRouteZeroVirtualClear.begin(), kVRRouteZeroVirtualClear.end(),
				observed.begin() + kVRRouteZeroVirtualClearOffset);
	}

	[[nodiscard]] constexpr std::span<const std::uint8_t> DescriptorInitializerBytes(
		const Runtime runtime) noexcept
	{
		return runtime == Runtime::kSkyrimVR1415 ?
			std::span<const std::uint8_t>{ kVRDescriptorInitializer } :
			std::span<const std::uint8_t>{ kDescriptorInitializer };
	}

	[[nodiscard]] constexpr std::span<const std::uint8_t> CullPrologueBytes(
		const Runtime runtime) noexcept
	{
		return runtime == Runtime::kSkyrimVR1415 ?
			std::span<const std::uint8_t>{ kVRCullFromDescriptorPrologue } :
			std::span<const std::uint8_t>{ kCullFromDescriptorPrologue };
	}

	[[nodiscard]] constexpr std::span<const std::uint8_t> NiAVObjectCullBytes(
		const Runtime runtime) noexcept
	{
		return runtime == Runtime::kSkyrimVR1415 ?
			std::span<const std::uint8_t>{ kVRNiAVObjectCull } :
			std::span<const std::uint8_t>{ kNiAVObjectCull };
	}

	[[nodiscard]] constexpr std::size_t RouteZeroWindowSize(
		const Runtime runtime) noexcept
	{
		return runtime == Runtime::kSkyrimVR1415 ? kVRRouteZeroVerifiedSize :
		                                            kRouteZeroWindow.size();
	}

	[[nodiscard]] constexpr bool MatchesRouteZeroWindowFor(
		const Runtime runtime,
		const std::span<const std::uint8_t> observed) noexcept
	{
		return runtime == Runtime::kSkyrimVR1415 ?
			MatchesVRRouteZeroWindow(observed) : MatchesRouteZeroWindow(observed);
	}
}
