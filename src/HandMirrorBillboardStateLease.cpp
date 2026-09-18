#include "PCH.h"

#include "HandMirrorBillboardStateLease.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorVRRuntimePolicy.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace HandMirrorBillboardStateLease
{
	namespace
	{
		using OnVisibleFunction = void(
			RE::NiBillboardNode*, RE::NiCullingProcess&, std::int32_t);
		using FacingUpdateFunction = void(
			RE::NiBillboardNode*, RE::NiCamera*);

		// NiBillboardNode::OnVisible vtable slot: flat 0x34; VR NiAVObject carries
		// one extra virtual, so the same override sits at 0x35 there.
		constexpr std::size_t kFlatOnVisibleSlot = 0x34;
		constexpr std::size_t kVROnVisibleSlot = 0x35;
		constexpr std::ptrdiff_t kFacingUpdateCallOffset = 0x25;
		constexpr std::size_t kFiveByteBranchStubSize = 14;
		constexpr std::array<std::uint8_t, 48> kSE1597OnVisibleSignature{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
			0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80,
			0xBA, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x41, 0x8B,
			0xF0, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x74,
			0x09, 0x48, 0x8B, 0x52, 0x18, 0xE8, 0x16, 0xF1,
			0xFF, 0xFF, 0x44, 0x8B, 0xC6, 0x48, 0x8B, 0xD3
		};
		constexpr std::array<std::uint8_t, 48> kAE161170OnVisibleSignature{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
			0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80,
			0xBA, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x41, 0x8B,
			0xF0, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x74,
			0x09, 0x48, 0x8B, 0x52, 0x18, 0xE8, 0x06, 0xEF,
			0xFF, 0xFF, 0x44, 0x8B, 0xC6, 0x48, 0x8B, 0xD3
		};
		// Skyrim 1.7.104 OnVisible 0xF041D0: the AE 1.6.1170 instruction stream
		// byte for byte, except the +0x25 rel32 (0xFFFFEEA6 -> facing update
		// 0xF030A0) in bytes 38 and 39.
		constexpr std::array<std::uint8_t, 48> kAE17104OnVisibleSignature{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
			0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80,
			0xBA, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x41, 0x8B,
			0xF0, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x74,
			0x09, 0x48, 0x8B, 0x52, 0x18, 0xE8, 0xA6, 0xEE,
			0xFF, 0xFF, 0x44, 0x8B, 0xC6, 0x48, 0x8B, 0xD3
		};
		// VR 1.4.15 OnVisible 0xCBF5E0: SE-identical except the +0x25 rel32
		// (0xFFFFECF6 -> facing update 0xCBE300).
		constexpr std::array<std::uint8_t, 48> kVR1415OnVisibleSignature{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
			0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80,
			0xBA, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x41, 0x8B,
			0xF0, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x74,
			0x09, 0x48, 0x8B, 0x52, 0x18, 0xE8, 0xF6, 0xEC,
			0xFF, 0xFF, 0x44, 0x8B, 0xC6, 0x48, 0x8B, 0xD3
		};

		struct RuntimeContract
		{
			std::uint64_t vtableID{ 0 };
			std::uintptr_t vtableRVA{ 0 };
			std::uint64_t onVisibleID{ 0 };
			std::uintptr_t onVisibleRVA{ 0 };
			std::uint64_t facingUpdateID{ 0 };
			std::uintptr_t facingUpdateRVA{ 0 };
			const std::array<std::uint8_t, 48>* onVisibleSignature{ nullptr };
		};

		// Both vtables use flat-runtime slot 0x34.  Each helper ID/RVA and the
		// caller's exact 48-byte entry were independently decoded from its matching
		// Address Library and executable.
		constexpr RuntimeContract kSE1597{
			.vtableID = 243047,
			.vtableRVA = 0x15AB9E8,
			.onVisibleID = 69683,
			.onVisibleRVA = 0xC78D90,
			.facingUpdateID = 69681,
			.facingUpdateRVA = 0xC77ED0,
			.onVisibleSignature = &kSE1597OnVisibleSignature
		};
		constexpr RuntimeContract kAE161170{
			.vtableID = 196539,
			.vtableRVA = 0x17F0750,
			.onVisibleID = 71063,
			.onVisibleRVA = 0xD3F720,
			.facingUpdateID = 71061,
			.facingUpdateRVA = 0xD3E650,
			.onVisibleSignature = &kAE161170OnVisibleSignature
		};
		// GOG 1.6.1179: same AE IDs and the Steam 1.6.1170 OnVisible rel32
		// (E8 06 EF FF FF); only the three RVAs move.
		constexpr RuntimeContract kAE161179{
			.vtableID = 196539,
			.vtableRVA = 0x17F1A20,
			.onVisibleID = 71063,
			.onVisibleRVA = 0xD41140,
			.facingUpdateID = 71061,
			.facingUpdateRVA = 0xD40070,
			.onVisibleSignature = &kAE161170OnVisibleSignature
		};
		// Skyrim 1.7.104 (versionlib-1-7-104-0.bin, 2026-09-05).  Same AE IDs, same
		// flat vtable slot 0x34; only the three RVAs and the OnVisible rel32 move.
		constexpr RuntimeContract kAE17104{
			.vtableID = 196539,
			.vtableRVA = 0x186A6B0,
			.onVisibleID = 71063,
			.onVisibleRVA = 0xF041D0,
			.facingUpdateID = 71061,
			.facingUpdateRVA = 0xF030A0,
			.onVisibleSignature = &kAE17104OnVisibleSignature
		};
		// Skyrim VR 1.4.15 (Ghidra 2026-09-04).  The billboard OnVisible has no
		// VR address-library ID (onVisibleID 0 => module base + RVA); the vtable
		// (243047) and facing update (69681) resolve through the VR CSV.
		constexpr RuntimeContract kVR1415{
			.vtableID = 243047,
			.vtableRVA = 0x1622CC0,
			.onVisibleID = 0,
			.onVisibleRVA = 0xCBF5E0,
			.facingUpdateID = 69681,
			.facingUpdateRVA = 0xCBE300,
			.onVisibleSignature = &kVR1415OnVisibleSignature
		};

		[[nodiscard]] std::size_t OnVisibleSlot() noexcept
		{
			return SupportedRuntimePolicy::IsExactVRRuntime() ?
				kVROnVisibleSlot : kFlatOnVisibleSlot;
		}

		// 0xC77ED0 writes the billboard world transform, then invokes virtual
		// UpdateDownwardPass on every child. The latter writes these exact common
		// NiAVObject fields (including +0x109 bit 6) while recursively descending.
		constexpr std::ptrdiff_t kWorldOffset = 0x7C;
		constexpr std::size_t kTransformSize = 0x34;
		constexpr std::ptrdiff_t kParentOffset = 0x30;
		constexpr std::ptrdiff_t kLocalOffset = 0x48;
		constexpr std::ptrdiff_t kTransformTranslationOffset = 0x24;
		constexpr std::ptrdiff_t kPreviousWorldOffset = 0xB0;
		constexpr std::ptrdiff_t kWorldBoundOffset = 0xE4;
		constexpr std::size_t kWorldBoundSize = 0x10;
		constexpr std::size_t kFlagsSize = 0x4;
		constexpr std::size_t kFrameFlagsSize = 0x8;
		constexpr std::size_t kFrameBitsSize = 0x8;

		// Runtime-selected NiAVObject/NiNode/BSGeometry layout.  Flat (0x110-byte
		// NiAVObject) and VR (0x138-byte NiAVObject, CommonLib EXCLUSIVE_SKYRIM_VR
		// block) share parent/local/world/previousWorld/worldBound; VR moves
		// `flags` to 0x10C and the frame bookkeeping after lastUpdatedFrameCounter
		// differs: flat has {unk108, flags02 (bit 6 = frame flag), unk10A} at
		// 0x108..0x10B, VR has a float at 0x108 and its per-frame bitfield bytes
		// at 0x120..0x127. Native VR UpdateWorldData (0xCA7000) proves the
		// update stamp is +0x11C and the frame byte is +0x121. The gated VR fix
		// therefore retains +0x11C..0x123 as well as +0x120..0x127; the original
		// marker-off layout and flat +0x104 stamp are preserved.
		struct ObjectLayout
		{
			std::ptrdiff_t flags{ 0 };
			std::ptrdiff_t frameFlags{ 0 };
			std::ptrdiff_t frameBits{ -1 };  // -1: no separate bitfield block
			std::ptrdiff_t modelBound{ 0 };
			std::ptrdiff_t children{ 0 };
		};
		constexpr ObjectLayout kFlatObjectLayout{
			.flags = 0xF4,
			.frameFlags = 0x104,
			.frameBits = -1,
			.modelBound = 0x110,
			.children = 0x110
		};
		constexpr ObjectLayout kVRObjectLayout{
			.flags = 0x10C,
			.frameFlags = 0x104,
			.frameBits = 0x120,
			.modelBound = 0x138,
			.children = 0x138
		};

		constexpr ObjectLayout kCorrectedVRObjectLayout{
			.flags = 0x10C,
			.frameFlags = HandMirrorVRRuntimePolicy::kVRNodeFrameOffset,
			.frameBits = 0x120,
			.modelBound = 0x138,
			.children = 0x138
		};

		[[nodiscard]] const ObjectLayout& CurrentObjectLayout() noexcept
		{
			if (HandMirrorApprovedContentReadOnlyObserver::VRRuntimeFixEnabled())
				return kCorrectedVRObjectLayout;
			return SupportedRuntimePolicy::IsExactVRRuntime() ?
				kVRObjectLayout : kFlatObjectLayout;
		}

		constexpr std::size_t kMaximumObjects = 1024;
		constexpr std::size_t kIdentityCapacity = 2048;
		constexpr std::size_t kMaximumChildSlots = 4096;
		// One eighth of a game unit admits the vector round-trip error of one
		// binary32 ULP per axis even near |world|=2^19, while remaining far below
		// the multi-unit orbit this correction exists to remove.
		constexpr float kMaximumPostAnchorResidual = 0.125F;
		static_assert((kIdentityCapacity & (kIdentityCapacity - 1)) == 0);
		static_assert(kIdentityCapacity >= 2 * kMaximumObjects);
		static_assert(sizeof(RE::NiPointer<RE::NiAVObject>) == sizeof(void*));

		struct ObjectState
		{
			std::array<std::byte, kTransformSize> world{};
			std::array<std::byte, kTransformSize> previousWorld{};
			std::array<std::byte, kWorldBoundSize> worldBound{};
			std::array<std::byte, kFlagsSize> flags{};
			std::array<std::byte, kFrameFlagsSize> frameFlags{};
			// VR-only 0x120 bitfield block; stays zero on flat runtimes.
			std::array<std::byte, kFrameBitsSize> frameBits{};

			friend bool operator==(const ObjectState&, const ObjectState&) noexcept =
				default;
		};
		static_assert(std::is_trivially_copyable_v<ObjectState>);

		struct Entry
		{
			RE::NiAVObject* object{ nullptr };
			ObjectState state{};
			bool ownsReference{ false };
		};

		struct RawChildren
		{
			std::uintptr_t vtable{ 0 };
			RE::NiAVObject** values{ nullptr };
			std::uint16_t capacity{ 0 };
			std::uint16_t freeIndex{ 0 };
			std::uint16_t size{ 0 };
			std::uint16_t growth{ 0 };
		};
		static_assert(sizeof(RawChildren) == 0x18);

		struct AnchorWork
		{
			RE::NiAVObject* object{ nullptr };
			RE::NiTransform fromBillboard{};
			std::size_t branchIndex{ 0 };
		};

		struct AuthoredBranchAnchor
		{
			RE::NiAVObject* root{ nullptr };
			RE::NiPoint3 local{};
			RE::NiPoint3 world{};
			std::size_t geometryCount{ 0 };
			bool available{ false };
		};

		struct AuthoredBranchAnchors
		{
			std::array<AuthoredBranchAnchor, kMaximumObjects> branches{};
			std::size_t count{ 0 };
			std::size_t geometryCount{ 0 };
		};

		struct BranchShiftRange
		{
			std::size_t objectBegin{ 0 };
			std::size_t objectCount{ 0 };
			std::size_t geometryCount{ 0 };
		};

		struct FacingCallbackShiftPlan
		{
			std::array<RE::NiAVObject*, kMaximumObjects> objects{};
			// This one set spans every direct branch. A repeated identity therefore
			// proves a shared descendant or a second planned shift in this callback.
			std::array<std::uintptr_t, kMaximumObjects> shiftedIdentities{};
			std::size_t objectCount{ 0 };
			std::size_t shiftedIdentityCount{ 0 };
		};

		struct Scope
		{
			std::array<Entry, kMaximumObjects> entries{};
			std::array<std::uintptr_t, kIdentityCapacity> identities{};
			std::size_t count{ 0 };
			std::size_t duplicates{ 0 };
			std::size_t callbacks{ 0 };
			std::size_t nativeCalls{ 0 };
			std::size_t nativeReturns{ 0 };
			std::size_t facingUpdateCalls{ 0 };
			std::size_t facingUpdateSkips{ 0 };
			std::size_t branchAnchors{ 0 };
			std::size_t branchGeometries{ 0 };
			std::size_t branchShifts{ 0 };
			std::size_t branchReadbackFailures{ 0 };
			std::uint32_t generation{ 0 };
			std::uint32_t threadID{ 0 };
			bool active{ false };
			bool rejected{ false };
			bool rejectedBeforeMutation{ false };
			bool overflowed{ false };
			bool faulted{ false };
		};
		static_assert(std::is_trivially_destructible_v<Scope>);

		struct AtomicDiagnostics
		{
			std::atomic<std::uint64_t> installAttempts{ 0 };
			std::atomic<std::uint64_t> installSuccesses{ 0 };
			std::atomic<std::uint64_t> beginAttempts{ 0 };
			std::atomic<std::uint64_t> begins{ 0 };
			std::atomic<std::uint64_t> beginRejects{ 0 };
			std::atomic<std::uint64_t> hookCalls{ 0 };
			std::atomic<std::uint64_t> inactiveNativeCalls{ 0 };
			std::atomic<std::uint64_t> activeNativeCalls{ 0 };
			std::atomic<std::uint64_t> activeNativeReturns{ 0 };
			std::atomic<std::uint64_t> facingHookCalls{ 0 };
			std::atomic<std::uint64_t> inactiveFacingNativeCalls{ 0 };
			std::atomic<std::uint64_t> activeFacingUpdateSkips{ 0 };
			std::atomic<std::uint64_t> crossThreadFacingCalls{ 0 };
			std::atomic<std::uint64_t> recordedObjects{ 0 };
			std::atomic<std::uint64_t> duplicateObjects{ 0 };
			std::atomic<std::uint64_t> capacityOverflows{ 0 };
			std::atomic<std::uint64_t> snapshotFaults{ 0 };
			std::atomic<std::uint64_t> retainFaults{ 0 };
			std::atomic<std::uint64_t> skippedNativeMutations{ 0 };
			std::atomic<std::uint64_t> completedScopes{ 0 };
			std::atomic<std::uint64_t> rejectedScopes{ 0 };
			std::atomic<std::uint64_t> restoredObjects{ 0 };
			std::atomic<std::uint64_t> restoreFailures{ 0 };
			std::atomic<std::uint64_t> releasedObjects{ 0 };
			std::atomic<std::uint64_t> releaseFailures{ 0 };
			std::atomic<std::uint64_t> tokenMismatches{ 0 };
			std::atomic<std::uint64_t> crossThreadCallbacks{ 0 };
			std::atomic<std::uint64_t> branchAnchors{ 0 };
			std::atomic<std::uint64_t> branchGeometries{ 0 };
			std::atomic<std::uint64_t> branchShifts{ 0 };
			std::atomic<std::uint64_t> branchReadbackFailures{ 0 };
			std::atomic<std::uint32_t> maximumPreAnchorDriftBits{ 0 };
			std::atomic<std::uint32_t> maximumPostAnchorResidualBits{ 0 };
		};

		AtomicDiagnostics g_diag{};
		std::atomic<OnVisibleFunction*> g_original{ nullptr };
		std::atomic<FacingUpdateFunction*> g_facingUpdateNative{ nullptr };
		std::atomic_bool g_installed{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_installAttempted{ false };
		std::atomic<InstallStatus> g_installStatus{ InstallStatus::kNotRequested };
		std::atomic<std::uintptr_t> g_slotAddress{ 0 };
		std::atomic<std::uintptr_t> g_facingUpdateCallSite{ 0 };
		std::atomic<std::uintptr_t> g_facingUpdateBranchTarget{ 0 };
		std::atomic<std::uint32_t> g_nextGeneration{ 0 };
		// One process-wide active owner detects any unexpected worker-thread cull;
		// the actual ledger remains TLS and is touched only by its owning thread.
		std::atomic<std::uint64_t> g_activeOwner{ 0 };
		std::atomic<std::uint32_t> g_lastExceptionCode{ 0 };
		std::mutex g_installLock;
		thread_local Scope g_scope{};
		static_assert(std::atomic<OnVisibleFunction*>::is_always_lock_free);
		static_assert(std::atomic<FacingUpdateFunction*>::is_always_lock_free);
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

		[[nodiscard]] std::uint64_t PackOwner(
			std::uint32_t thread, std::uint32_t generation) noexcept
		{
			return (static_cast<std::uint64_t>(thread) << 32) | generation;
		}

		[[nodiscard]] int ExceptionFilter(unsigned long code) noexcept
		{
			g_lastExceptionCode.store(code, std::memory_order_relaxed);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void LatchFault() noexcept
		{
			g_faulted.store(true, std::memory_order_release);
			if (g_scope.active) {
				g_scope.rejected = true;
				g_scope.faulted = true;
			}
		}

		void RejectBeforeMutation(bool overflow) noexcept
		{
			g_scope.rejected = true;
			g_scope.rejectedBeforeMutation = true;
			g_scope.overflowed = g_scope.overflowed || overflow;
			g_scope.faulted = true;
			if (overflow)
				g_diag.capacityOverflows.fetch_add(1, std::memory_order_relaxed);
			LatchFault();
		}

		[[nodiscard]] const RuntimeContract* ExactRuntimeContract() noexcept
		{
			const auto version = REL::Module::get().version();
			if (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 })
				return &kSE1597;
			if (SupportedRuntimePolicy::IsExactAE17104Runtime())
				return &kAE17104;
			if (SupportedRuntimePolicy::IsExactAE161179Runtime())
				return &kAE161179;
			if (SupportedRuntimePolicy::IsExactAE161170Runtime())
				return &kAE161170;
			if (SupportedRuntimePolicy::IsExactVRRuntime())
				return &kVR1415;
			return nullptr;
		}

		// Contract entries without an address-library ID (VR-only targets) are
		// pinned by RVA alone; the caller still compares against base + RVA.
		[[nodiscard]] std::uintptr_t ResolveContractAddress(
			const std::uint64_t id,
			const std::uintptr_t rva) noexcept
		{
			if (id != 0)
				return REL::ID(id).address();
			return rva ? REL::Module::get().base() + rva : 0;
		}

		[[nodiscard]] bool Executable(const void* address) noexcept
		{
			if (!address)
				return false;
			MEMORY_BASIC_INFORMATION information{};
			if (VirtualQuery(address, &information, sizeof(information)) !=
					sizeof(information) || information.State != MEM_COMMIT ||
				(information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}
			const auto protection = information.Protect & 0xFF;
			return protection == PAGE_EXECUTE ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool ReadPointer(
			const void* address, std::uintptr_t& output) noexcept
		{
			output = 0;
			if (!address)
				return false;
			__try {
				std::memcpy(&output, address, sizeof(output));
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = 0;
				return false;
			}
		}

		[[nodiscard]] bool CopySEH(
			const void* source, void* destination, std::size_t size) noexcept
		{
			if (!source || !destination || !size)
				return false;
			__try {
				std::memcpy(destination, source, size);
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool ReadRel32Target(
			std::uintptr_t callSite, std::uintptr_t& target) noexcept
		{
			target = 0;
			std::array<std::uint8_t, 5> instruction{};
			if (!CopySEH(reinterpret_cast<const void*>(callSite),
					instruction.data(), instruction.size()) || instruction[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, instruction.data() + 1,
				sizeof(displacement));
			target = callSite + instruction.size() + displacement;
			return target != 0;
		}

		[[nodiscard]] bool MatchBranchStub(
			std::uintptr_t stubAddress,
			std::uintptr_t expectedDestination) noexcept
		{
			if (!Executable(reinterpret_cast<const void*>(stubAddress)) ||
				!Executable(reinterpret_cast<const void*>(expectedDestination))) {
				return false;
			}
			std::array<std::uint8_t, kFiveByteBranchStubSize> stub{};
			if (!CopySEH(reinterpret_cast<const void*>(stubAddress), stub.data(),
					stub.size()) || stub[0] != 0xFF || stub[1] != 0x25 ||
				stub[2] != 0 || stub[3] != 0 || stub[4] != 0 || stub[5] != 0) {
				return false;
			}
			std::uintptr_t destination = 0;
			std::memcpy(&destination, stub.data() + 6, sizeof(destination));
			return destination == expectedDestination;
		}

		[[nodiscard]] bool MatchSignature(
			const void* address, const RuntimeContract& contract) noexcept
		{
			__try {
				return address && contract.onVisibleSignature && std::memcmp(
					address, contract.onVisibleSignature->data(),
					contract.onVisibleSignature->size()) == 0;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool CaptureState(
			RE::NiAVObject* object, ObjectState& output) noexcept
		{
			if (!object)
				return false;
			const auto* bytes = reinterpret_cast<const std::byte*>(object);
			const auto& layout = CurrentObjectLayout();
			__try {
				std::memcpy(output.world.data(), bytes + kWorldOffset,
					output.world.size());
				std::memcpy(output.previousWorld.data(),
					bytes + kPreviousWorldOffset, output.previousWorld.size());
				std::memcpy(output.worldBound.data(), bytes + kWorldBoundOffset,
					output.worldBound.size());
				std::memcpy(output.flags.data(), bytes + layout.flags,
					output.flags.size());
				std::memcpy(output.frameFlags.data(), bytes + layout.frameFlags,
					output.frameFlags.size());
				if (layout.frameBits >= 0) {
					std::memcpy(output.frameBits.data(), bytes + layout.frameBits,
						output.frameBits.size());
				}
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = {};
				return false;
			}
		}

		[[nodiscard]] bool RestoreState(
			RE::NiAVObject* object, const ObjectState& state) noexcept
		{
			if (!object)
				return false;
			auto* bytes = reinterpret_cast<std::byte*>(object);
			const auto& layout = CurrentObjectLayout();
			ObjectState readback{};
			__try {
				std::memcpy(bytes + kWorldOffset, state.world.data(), state.world.size());
				std::memcpy(bytes + kPreviousWorldOffset,
					state.previousWorld.data(), state.previousWorld.size());
				std::memcpy(bytes + kWorldBoundOffset, state.worldBound.data(),
					state.worldBound.size());
				std::memcpy(bytes + layout.flags, state.flags.data(), state.flags.size());
				std::memcpy(bytes + layout.frameFlags, state.frameFlags.data(),
					state.frameFlags.size());
				if (layout.frameBits >= 0) {
					std::memcpy(bytes + layout.frameBits, state.frameBits.data(),
						state.frameBits.size());
				}
				return CaptureState(object, readback) && readback == state;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool Retain(RE::NiAVObject* object) noexcept
		{
			if (!object)
				return false;
			__try {
				object->IncRefCount();
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool Release(RE::NiAVObject* object) noexcept
		{
			if (!object)
				return false;
			__try {
				object->DecRefCount();
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool AsNode(
			RE::NiAVObject* object, RE::NiNode*& output) noexcept
		{
			output = nullptr;
			if (!object)
				return false;
			__try {
				output = object->AsNode();
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = nullptr;
				return false;
			}
		}

		[[nodiscard]] bool AsGeometry(
			RE::NiAVObject* object, RE::BSGeometry*& output) noexcept
		{
			output = nullptr;
			if (!object)
				return false;
			__try {
				output = object->AsGeometry();
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = nullptr;
				return false;
			}
		}

		[[nodiscard]] bool ReadChildren(
			RE::NiNode* node, RawChildren& output) noexcept
		{
			output = {};
			if (!node)
				return false;
			__try {
				std::memcpy(&output,
					reinterpret_cast<const std::byte*>(node) +
						CurrentObjectLayout().children,
					sizeof(output));
				return output.freeIndex <= output.capacity &&
					output.size <= output.freeIndex &&
					output.freeIndex <= kMaximumChildSlots &&
					(output.freeIndex == 0 || output.values != nullptr);
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = {};
				return false;
			}
		}

		[[nodiscard]] bool ReadChild(
			const RawChildren& children, std::uint16_t index,
			RE::NiAVObject*& output) noexcept
		{
			output = nullptr;
			if (!children.values || index >= children.freeIndex)
				return false;
			__try {
				std::memcpy(&output, children.values + index, sizeof(output));
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = nullptr;
				return false;
			}
		}

		[[nodiscard]] bool FiniteFloat(float value) noexcept
		{
			return (std::bit_cast<std::uint32_t>(value) & 0x7F800000U) !=
				0x7F800000U;
		}

		[[nodiscard]] bool FinitePoint(const RE::NiPoint3& point) noexcept
		{
			return FiniteFloat(point.x) && FiniteFloat(point.y) &&
				FiniteFloat(point.z);
		}

		[[nodiscard]] float PointDistance(
			const RE::NiPoint3& left, const RE::NiPoint3& right) noexcept
		{
			const auto dx = left.x - right.x;
			const auto dy = left.y - right.y;
			const auto dz = left.z - right.z;
			const auto squared = dx * dx + dy * dy + dz * dz;
			return FiniteFloat(squared) && squared >= 0.0F ?
				std::sqrt(squared) : std::numeric_limits<float>::infinity();
		}

		void UpdateMaximum(
			std::atomic<std::uint32_t>& destination, const float value) noexcept
		{
			if (!FiniteFloat(value) || value < 0.0F)
				return;
			auto current = destination.load(std::memory_order_relaxed);
			while (value > std::bit_cast<float>(current) &&
				!destination.compare_exchange_weak(
					current, std::bit_cast<std::uint32_t>(value),
					std::memory_order_relaxed)) {
			}
		}

		[[nodiscard]] bool FiniteTransform(
			const RE::NiTransform& transform) noexcept
		{
			if (!FinitePoint(transform.translate) ||
				!FiniteFloat(transform.scale)) {
				return false;
			}
			for (const auto& row : transform.rotate.entry) {
				for (const auto value : row) {
					if (!FiniteFloat(value))
						return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool TransformPoint(
			const RE::NiTransform& transform, const RE::NiPoint3& point,
			RE::NiPoint3& output) noexcept
		{
			if (!FiniteTransform(transform) || !FinitePoint(point))
				return false;
			const RE::NiPoint3 scaled{
				point.x * transform.scale,
				point.y * transform.scale,
				point.z * transform.scale
			};
			output = {
				transform.rotate.entry[0][0] * scaled.x +
					transform.rotate.entry[0][1] * scaled.y +
					transform.rotate.entry[0][2] * scaled.z +
					transform.translate.x,
				transform.rotate.entry[1][0] * scaled.x +
					transform.rotate.entry[1][1] * scaled.y +
					transform.rotate.entry[1][2] * scaled.z +
					transform.translate.y,
				transform.rotate.entry[2][0] * scaled.x +
					transform.rotate.entry[2][1] * scaled.y +
					transform.rotate.entry[2][2] * scaled.z +
					transform.translate.z
			};
			return FinitePoint(output);
		}

		[[nodiscard]] bool ComposeTransforms(
			const RE::NiTransform& outer, const RE::NiTransform& inner,
			RE::NiTransform& output) noexcept
		{
			if (!FiniteTransform(outer) || !FiniteTransform(inner))
				return false;
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 3; ++column) {
					output.rotate.entry[row][column] =
						outer.rotate.entry[row][0] * inner.rotate.entry[0][column] +
						outer.rotate.entry[row][1] * inner.rotate.entry[1][column] +
						outer.rotate.entry[row][2] * inner.rotate.entry[2][column];
				}
			}
			output.scale = outer.scale * inner.scale;
			if (!TransformPoint(outer, inner.translate, output.translate))
				return false;
			return FiniteTransform(output);
		}

		[[nodiscard]] bool ReadTransform(
			RE::NiAVObject* object, std::ptrdiff_t offset,
			RE::NiTransform& output) noexcept
		{
			output = {};
			return object && CopySEH(
				reinterpret_cast<const std::byte*>(object) + offset,
				&output, sizeof(output)) && FiniteTransform(output);
		}

		[[nodiscard]] bool ReadParent(
			RE::NiAVObject* object, RE::NiAVObject*& output) noexcept
		{
			output = nullptr;
			std::uintptr_t parent = 0;
			if (!object || !ReadPointer(
					reinterpret_cast<const std::byte*>(object) + kParentOffset,
					parent)) {
				return false;
			}
			output = reinterpret_cast<RE::NiAVObject*>(parent);
			return true;
		}

		[[nodiscard]] bool ReadModelBound(
			RE::BSGeometry* geometry, RE::NiBound& output) noexcept
		{
			output = {};
			return geometry && CopySEH(
				reinterpret_cast<const std::byte*>(geometry) +
					CurrentObjectLayout().modelBound,
				&output, sizeof(output)) && FinitePoint(output.center) &&
				FiniteFloat(output.radius);
		}

		[[nodiscard]] bool VisitOnce(
			std::array<std::uintptr_t, kMaximumObjects>& visited,
			std::size_t& count, RE::NiAVObject* object) noexcept
		{
			const auto identity = reinterpret_cast<std::uintptr_t>(object);
			if (!identity)
				return false;
			for (std::size_t index = 0; index < count; ++index) {
				if (visited[index] == identity)
					return false;
			}
			if (count >= visited.size())
				return false;
			visited[count++] = identity;
			return true;
		}

		[[nodiscard]] bool FindAuthoredBranchAnchors(
			RE::NiBillboardNode* billboard,
			AuthoredBranchAnchors& output) noexcept
		{
			output = {};
			if (!billboard)
				return false;

			RE::NiTransform local{};
			if (!ReadTransform(billboard, kLocalOffset, local))
				return false;
			RE::NiAVObject* parent = nullptr;
			if (!ReadParent(billboard, parent))
				return false;
			RE::NiTransform authoredWorld = local;
			if (parent) {
				RE::NiTransform parentWorld{};
				if (!ReadTransform(parent, kWorldOffset, parentWorld) ||
					!ComposeTransforms(parentWorld, local, authoredWorld)) {
					return false;
				}
			}

			RE::NiNode* billboardNode = nullptr;
			if (!AsNode(billboard, billboardNode) || !billboardNode)
				return false;
			RawChildren directChildren{};
			if (!ReadChildren(billboardNode, directChildren))
				return false;

			std::array<AnchorWork, kMaximumObjects> work{};
			std::array<std::uintptr_t, kMaximumObjects> visited{};
			std::size_t workCount = 0;
			std::size_t visitedCount = 0;
			// Seed the global traversal with the billboard itself. Every direct-child
			// branch then shares this one identity set, proving that the subtrees are
			// disjoint before any post-native translation can occur.
			if (!VisitOnce(visited, visitedCount, billboard))
				return false;
			for (std::uint32_t index = 0; index < directChildren.freeIndex; ++index) {
				RE::NiAVObject* child = nullptr;
				if (!ReadChild(directChildren,
						static_cast<std::uint16_t>(index), child)) {
					return false;
				}
				if (!child)
					continue;
				if (output.count >= output.branches.size() || workCount >= work.size())
					return false;
				RE::NiTransform childLocal{};
				if (!ReadTransform(child, kLocalOffset, childLocal))
					return false;
				const auto branchIndex = output.count++;
				output.branches[branchIndex].root = child;
				work[workCount++] = { child, childLocal, branchIndex };
			}

			while (workCount) {
				const auto current = work[--workCount];
				if (!VisitOnce(visited, visitedCount, current.object)) {
					// A repeated identity proves either a cycle or a descendant shared by
					// two direct branches. Both would make a later branch shift ambiguous.
					return false;
				}
				if (current.branchIndex >= output.count)
					return false;
				auto& branch = output.branches[current.branchIndex];

				RE::BSGeometry* geometry = nullptr;
				if (!AsGeometry(current.object, geometry))
					return false;
				if (geometry) {
					RE::NiBound modelBound{};
					RE::NiPoint3 center{};
					if (!ReadModelBound(geometry, modelBound) ||
						!TransformPoint(
							current.fromBillboard, modelBound.center, center)) {
						return false;
					}
					const auto divisor =
						static_cast<float>(branch.geometryCount + 1);
					branch.local.x += (center.x - branch.local.x) / divisor;
					branch.local.y += (center.y - branch.local.y) / divisor;
					branch.local.z += (center.z - branch.local.z) / divisor;
					++branch.geometryCount;
					++output.geometryCount;
					if (!FinitePoint(branch.local))
						return false;
				}

				RE::NiNode* node = nullptr;
				if (!AsNode(current.object, node))
					return false;
				if (!node)
					continue;
				RawChildren children{};
				if (!ReadChildren(node, children))
					return false;
				for (std::uint32_t index = 0; index < children.freeIndex; ++index) {
					RE::NiAVObject* child = nullptr;
					if (!ReadChild(children,
							static_cast<std::uint16_t>(index), child)) {
						return false;
					}
					if (!child)
						continue;
					if (workCount >= work.size())
						return false;
					RE::NiTransform childLocal{};
					RE::NiTransform fromBillboard{};
					if (!ReadTransform(child, kLocalOffset, childLocal) ||
						!ComposeTransforms(
							current.fromBillboard, childLocal, fromBillboard)) {
						return false;
					}
					work[workCount++] = {
						child, fromBillboard, current.branchIndex };
				}
			}

			for (std::size_t index = 0; index < output.count; ++index) {
				auto& branch = output.branches[index];
				if (!branch.geometryCount)
					continue;
				if (!TransformPoint(authoredWorld, branch.local, branch.world))
					return false;
				branch.available = true;
			}
			return true;
		}

		[[nodiscard]] bool ShiftObject(
			RE::NiAVObject* object, const RE::NiPoint3& delta) noexcept
		{
			if (!object || !FinitePoint(delta))
				return false;
			auto* const bytes = reinterpret_cast<std::byte*>(object);
			constexpr std::array<std::ptrdiff_t, 3> offsets{
				kWorldOffset + kTransformTranslationOffset,
				kPreviousWorldOffset + kTransformTranslationOffset,
				kWorldBoundOffset
			};
			__try {
				for (const auto offset : offsets) {
					RE::NiPoint3 point{};
					std::memcpy(&point, bytes + offset, sizeof(point));
					if (!FinitePoint(point))
						return false;
					point.x += delta.x;
					point.y += delta.y;
					point.z += delta.z;
					if (!FinitePoint(point))
						return false;
					std::memcpy(bytes + offset, &point, sizeof(point));
					RE::NiPoint3 readback{};
					std::memcpy(&readback, bytes + offset, sizeof(readback));
					if (std::memcmp(&readback, &point, sizeof(point)) != 0)
						return false;
				}
				return true;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool Contains(std::uintptr_t identity) noexcept;

		[[nodiscard]] bool PreflightShiftSubtree(
			RE::NiAVObject* root, FacingCallbackShiftPlan& plan,
			BranchShiftRange& range) noexcept
		{
			range = {};
			if (!root)
				return false;
			range.objectBegin = plan.objectCount;
			std::array<RE::NiAVObject*, kMaximumObjects> work{};
			std::size_t workCount = 1;
			work[0] = root;
			while (workCount) {
				auto* const object = work[--workCount];
				const auto identity = reinterpret_cast<std::uintptr_t>(object);
				// The first-snapshot ledger owns the only safe mutation domain. Check it
				// before any virtual call or write, then reserve this identity in the one
				// facing-callback-wide set shared by every branch.
				if (!identity || !Contains(identity) ||
					!VisitOnce(plan.shiftedIdentities,
						plan.shiftedIdentityCount, object) ||
					plan.objectCount >= plan.objects.size()) {
					return false;
				}
				plan.objects[plan.objectCount++] = object;
				++range.objectCount;

				RE::BSGeometry* geometry = nullptr;
				if (!AsGeometry(object, geometry))
					return false;
				if (geometry)
					++range.geometryCount;

				RE::NiNode* node = nullptr;
				if (!AsNode(object, node))
					return false;
				if (!node)
					continue;
				RawChildren children{};
				if (!ReadChildren(node, children))
					return false;
				for (std::uint32_t index = 0; index < children.freeIndex; ++index) {
					RE::NiAVObject* child = nullptr;
					if (!ReadChild(children,
							static_cast<std::uint16_t>(index), child)) {
						return false;
					}
					if (!child)
						continue;
					if (workCount >= work.size())
						return false;
					work[workCount++] = child;
				}
			}
			return range.objectCount != 0 && range.geometryCount != 0;
		}

		[[nodiscard]] bool MeasureSubtreeAnchor(
			const FacingCallbackShiftPlan& plan, const BranchShiftRange& range,
			RE::NiPoint3& anchor, std::size_t& geometryCount) noexcept
		{
			anchor = {};
			geometryCount = 0;
			if (!range.objectCount || !range.geometryCount ||
				range.objectBegin > plan.objectCount ||
				range.objectCount > plan.objectCount - range.objectBegin) {
				return false;
			}

			// Native billboard facing is not guaranteed to reproduce a generic
			// parent*local transform for every child. Measure the exact post-native
			// geometry centers that will be drawn instead of predicting them from the
			// billboard world transform.
			for (std::size_t offset = 0; offset < range.objectCount; ++offset) {
				auto* const object = plan.objects[range.objectBegin + offset];
				const auto identity = reinterpret_cast<std::uintptr_t>(object);
				if (!identity || !Contains(identity))
					return false;

				RE::BSGeometry* geometry = nullptr;
				if (!AsGeometry(object, geometry))
					return false;
				if (!geometry)
					continue;

				RE::NiTransform world{};
				RE::NiBound modelBound{};
				RE::NiPoint3 center{};
				if (!ReadTransform(geometry, kWorldOffset, world) ||
					!ReadModelBound(geometry, modelBound) ||
					!TransformPoint(world, modelBound.center, center)) {
					return false;
				}
				const auto divisor = static_cast<float>(geometryCount + 1);
				anchor.x += (center.x - anchor.x) / divisor;
				anchor.y += (center.y - anchor.y) / divisor;
				anchor.z += (center.z - anchor.z) / divisor;
				++geometryCount;
				if (!FinitePoint(anchor))
					return false;
			}
			return geometryCount == range.geometryCount;
		}

		[[nodiscard]] bool ShiftSubtree(
			const FacingCallbackShiftPlan& plan, const BranchShiftRange& range,
			const RE::NiPoint3& delta, RE::NiPoint3& shiftedAnchor,
			std::size_t& shiftedGeometryCount) noexcept
		{
			shiftedAnchor = {};
			shiftedGeometryCount = 0;
			if (!range.objectCount || !range.geometryCount ||
				range.objectBegin > plan.objectCount ||
				range.objectCount > plan.objectCount - range.objectBegin) {
				return false;
			}

			// Revalidate the complete range before its first write. Every identity
			// must still belong to the first snapshot and to the callback-wide preflight
			// set; no partially validated branch is ever mutated.
			for (std::size_t offset = 0; offset < range.objectCount; ++offset) {
				auto* const object = plan.objects[range.objectBegin + offset];
				const auto identity = reinterpret_cast<std::uintptr_t>(object);
				if (!identity || !Contains(identity))
					return false;
				bool planned = false;
				for (std::size_t index = 0;
						index < plan.shiftedIdentityCount; ++index) {
					planned = planned || plan.shiftedIdentities[index] == identity;
				}
				if (!planned)
					return false;
			}

			for (std::size_t offset = 0; offset < range.objectCount; ++offset) {
				auto* const object = plan.objects[range.objectBegin + offset];
				const auto identity = reinterpret_cast<std::uintptr_t>(object);
				if (!identity || !Contains(identity) ||
					!ShiftObject(object, delta)) {
					return false;
				}
				RE::BSGeometry* geometry = nullptr;
				if (!AsGeometry(object, geometry))
					return false;
				if (!geometry)
					continue;

				RE::NiTransform world{};
				RE::NiBound modelBound{};
				RE::NiPoint3 center{};
				if (!ReadTransform(geometry, kWorldOffset, world) ||
					!ReadModelBound(geometry, modelBound) ||
					!TransformPoint(world, modelBound.center, center)) {
					return false;
				}
				const auto divisor =
					static_cast<float>(shiftedGeometryCount + 1);
				shiftedAnchor.x += (center.x - shiftedAnchor.x) / divisor;
				shiftedAnchor.y += (center.y - shiftedAnchor.y) / divisor;
				shiftedAnchor.z += (center.z - shiftedAnchor.z) / divisor;
				++shiftedGeometryCount;
				if (!FinitePoint(shiftedAnchor))
					return false;
			}
			return shiftedGeometryCount == range.geometryCount;
		}

		[[nodiscard]] std::size_t Bucket(std::uintptr_t identity) noexcept
		{
			std::uint64_t value = identity >> 3;
			value ^= value >> 33;
			value *= 0xff51afd7ed558ccdULL;
			value ^= value >> 33;
			return static_cast<std::size_t>(value) & (kIdentityCapacity - 1);
		}

		[[nodiscard]] bool Contains(std::uintptr_t identity) noexcept
		{
			if (!identity)
				return false;
			auto bucket = Bucket(identity);
			for (std::size_t probe = 0; probe < kIdentityCapacity; ++probe) {
				const auto stored = g_scope.identities[bucket];
				if (stored == identity)
					return true;
				if (!stored)
					return false;
				bucket = (bucket + 1) & (kIdentityCapacity - 1);
			}
			return false;
		}

		[[nodiscard]] bool Insert(std::uintptr_t identity) noexcept
		{
			auto bucket = Bucket(identity);
			for (std::size_t probe = 0; probe < kIdentityCapacity; ++probe) {
				auto& stored = g_scope.identities[bucket];
				if (stored == identity)
					return true;
				if (!stored) {
					stored = identity;
					return true;
				}
				bucket = (bucket + 1) & (kIdentityCapacity - 1);
			}
			return false;
		}

		enum class RecordStatus : std::uint8_t
		{
			kInserted,
			kExisting,
			kFailed
		};

		[[nodiscard]] RecordStatus Record(RE::NiAVObject* object) noexcept
		{
			if (!object) {
				RejectBeforeMutation(false);
				return RecordStatus::kFailed;
			}
			const auto identity = reinterpret_cast<std::uintptr_t>(object);
			if (Contains(identity)) {
				++g_scope.duplicates;
				g_diag.duplicateObjects.fetch_add(1, std::memory_order_relaxed);
				return RecordStatus::kExisting;
			}
			if (g_scope.count >= g_scope.entries.size()) {
				RejectBeforeMutation(true);
				return RecordStatus::kFailed;
			}

			if (!Retain(object)) {
				g_diag.retainFaults.fetch_add(1, std::memory_order_relaxed);
				RejectBeforeMutation(false);
				return RecordStatus::kFailed;
			}
			ObjectState state{};
			if (!CaptureState(object, state)) {
				g_diag.snapshotFaults.fetch_add(1, std::memory_order_relaxed);
				if (!Release(object))
					g_diag.releaseFailures.fetch_add(1, std::memory_order_relaxed);
				RejectBeforeMutation(false);
				return RecordStatus::kFailed;
			}

			auto& entry = g_scope.entries[g_scope.count];
			entry = { object, state, true };
			if (!Insert(identity)) {
				// Ownership is already committed; include it in outer cleanup even
				// though the identity table itself proved malformed/full.
				++g_scope.count;
				RejectBeforeMutation(true);
				return RecordStatus::kFailed;
			}
			++g_scope.count;
			g_diag.recordedObjects.fetch_add(1, std::memory_order_relaxed);
			return RecordStatus::kInserted;
		}

		[[nodiscard]] bool SnapshotSubtree(RE::NiBillboardNode* billboard) noexcept
		{
			std::array<RE::NiAVObject*, kMaximumObjects> work{};
			std::size_t workCount = 0;
			const auto rootStatus = Record(billboard);
			if (rootStatus == RecordStatus::kFailed)
				return false;
			// A prior completed prewalk already recorded the complete reachable
			// subtree. First-snapshot semantics intentionally win across callbacks.
			if (rootStatus == RecordStatus::kExisting)
				return true;
			work[workCount++] = billboard;

			while (workCount) {
				auto* object = work[--workCount];
				RE::NiNode* node = nullptr;
				if (!AsNode(object, node)) {
					g_diag.snapshotFaults.fetch_add(1, std::memory_order_relaxed);
					RejectBeforeMutation(false);
					return false;
				}
				if (!node)
					continue;
				RawChildren children{};
				if (!ReadChildren(node, children)) {
					g_diag.snapshotFaults.fetch_add(1, std::memory_order_relaxed);
					RejectBeforeMutation(false);
					return false;
				}
				for (std::uint32_t index = 0; index < children.freeIndex; ++index) {
					RE::NiAVObject* child = nullptr;
					if (!ReadChild(children,
							static_cast<std::uint16_t>(index), child)) {
						g_diag.snapshotFaults.fetch_add(1, std::memory_order_relaxed);
						RejectBeforeMutation(false);
						return false;
					}
					if (!child)
						continue;
					const auto status = Record(child);
					if (status == RecordStatus::kFailed)
						return false;
					if (status == RecordStatus::kExisting)
						continue;
					if (workCount >= work.size()) {
						RejectBeforeMutation(true);
						return false;
					}
					work[workCount++] = child;
				}
			}
			return true;
		}

		void FacingUpdateThunk(
			RE::NiBillboardNode* billboard, RE::NiCamera* camera)
		{
			g_diag.facingHookCalls.fetch_add(1, std::memory_order_relaxed);
			auto* const native =
				g_facingUpdateNative.load(std::memory_order_acquire);
			if (!native) {
				LatchFault();
				return;
			}

			const auto globalOwner = g_activeOwner.load(std::memory_order_acquire);
			if (!globalOwner && !g_scope.active) {
				g_diag.inactiveFacingNativeCalls.fetch_add(
					1, std::memory_order_relaxed);
				native(billboard, camera);
				return;
			}


			const auto expectedOwner = PackOwner(
				g_scope.threadID, g_scope.generation);
			if (!g_scope.active || globalOwner != expectedOwner ||
				GetCurrentThreadId() != g_scope.threadID || !billboard ||
				!Contains(reinterpret_cast<std::uintptr_t>(billboard))) {
				g_diag.crossThreadFacingCalls.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault();
				// Never permit the exact unsnapshotted shared-state mutation that this
				// lease exists to contain. The owning OnVisible continues and still
				// tail-culls/submits its descendants after this skipped helper returns.
				return;
			}

			++g_scope.facingUpdateCalls;
			AuthoredBranchAnchors anchors{};
			const bool anchorsReady =
				FindAuthoredBranchAnchors(billboard, anchors);
			// Native owns the exact face mode and private-camera basis. It must run
			// exactly once even when anchor preparation fails; the outer first-state
			// ledger then restores every shared write and rejects publication.
			native(billboard, camera);
			if (!anchorsReady) {
				LatchFault();
				return;
			}
			if (!anchors.geometryCount)
				return;

			FacingCallbackShiftPlan shiftPlan{};
			std::array<BranchShiftRange, kMaximumObjects> shiftRanges{};
			std::array<RE::NiPoint3, kMaximumObjects> branchDeltas{};
			// Preflight every branch before the first scene write. The shared plan's
			// shiftedIdentities set therefore catches both cross-branch sharing and a
			// descendant attached after the first-snapshot walk without leaving a
			// partially corrected sibling branch behind.
			for (std::size_t index = 0; index < anchors.count; ++index) {
				const auto& anchor = anchors.branches[index];
				if (!anchor.available)
					continue;
				++g_scope.branchAnchors;
				g_scope.branchGeometries += anchor.geometryCount;
				g_diag.branchAnchors.fetch_add(1, std::memory_order_relaxed);
				g_diag.branchGeometries.fetch_add(
					anchor.geometryCount, std::memory_order_relaxed);

				if (!PreflightShiftSubtree(
						anchor.root, shiftPlan, shiftRanges[index]) ||
					shiftRanges[index].geometryCount != anchor.geometryCount) {
					++g_scope.branchReadbackFailures;
					g_diag.branchReadbackFailures.fetch_add(
						1, std::memory_order_relaxed);
					LatchFault();
					return;
				}
			}

			// Finish all read-only preflight and exact post-native measurements before
			// the first scene write. A failed sibling can therefore never leave an
			// earlier branch partially corrected.
			for (std::size_t index = 0; index < anchors.count; ++index) {
				const auto& anchor = anchors.branches[index];
				if (!anchor.available)
					continue;
				RE::NiPoint3 nativeAnchor{};
				std::size_t measuredGeometryCount = 0;
				if (!MeasureSubtreeAnchor(shiftPlan, shiftRanges[index],
						nativeAnchor, measuredGeometryCount) ||
					measuredGeometryCount != anchor.geometryCount) {
					++g_scope.branchReadbackFailures;
					g_diag.branchReadbackFailures.fetch_add(
						1, std::memory_order_relaxed);
					LatchFault();
					return;
				}
				const RE::NiPoint3 delta{
					anchor.world.x - nativeAnchor.x,
					anchor.world.y - nativeAnchor.y,
					anchor.world.z - nativeAnchor.z
				};
				branchDeltas[index] = delta;
				const auto preAnchorDrift =
					PointDistance(anchor.world, nativeAnchor);
				if (!FinitePoint(delta) || !FiniteFloat(preAnchorDrift)) {
					++g_scope.branchReadbackFailures;
					g_diag.branchReadbackFailures.fetch_add(
						1, std::memory_order_relaxed);
					LatchFault();
					return;
				}
				UpdateMaximum(
					g_diag.maximumPreAnchorDriftBits, preAnchorDrift);
			}

			for (std::size_t index = 0; index < anchors.count; ++index) {
				const auto& anchor = anchors.branches[index];
				if (!anchor.available)
					continue;
				RE::NiPoint3 shiftedAnchor{};
				std::size_t shiftedGeometryCount = 0;
				if (!ShiftSubtree(shiftPlan, shiftRanges[index],
						branchDeltas[index], shiftedAnchor,
						shiftedGeometryCount) ||
					shiftedGeometryCount != anchor.geometryCount) {
					++g_scope.branchReadbackFailures;
					g_diag.branchReadbackFailures.fetch_add(
						1, std::memory_order_relaxed);
					LatchFault();
					return;
				}

				const auto postAnchorResidual =
					PointDistance(anchor.world, shiftedAnchor);
				if (!FiniteFloat(postAnchorResidual)) {
					++g_scope.branchReadbackFailures;
					g_diag.branchReadbackFailures.fetch_add(
						1, std::memory_order_relaxed);
					LatchFault();
					return;
				}
				UpdateMaximum(g_diag.maximumPostAnchorResidualBits,
					postAnchorResidual);
				if (postAnchorResidual > kMaximumPostAnchorResidual) {
					++g_scope.branchReadbackFailures;
					g_diag.branchReadbackFailures.fetch_add(
						1, std::memory_order_relaxed);
					LatchFault();
					return;
				}
				++g_scope.branchShifts;
				g_diag.branchShifts.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void OnVisibleThunk(
			RE::NiBillboardNode* billboard,
			RE::NiCullingProcess& process,
			std::int32_t alphaGroup)
		{
			g_diag.hookCalls.fetch_add(1, std::memory_order_relaxed);
			auto* const native = g_original.load(std::memory_order_acquire);
			if (!native) {
				LatchFault();
				return;
			}

			const auto globalOwner = g_activeOwner.load(std::memory_order_acquire);
			if (!globalOwner && !g_scope.active) {
				g_diag.inactiveNativeCalls.fetch_add(1, std::memory_order_relaxed);
				native(billboard, process, alphaGroup);  // exactly once
				return;
			}

			const auto expectedOwner = PackOwner(
				g_scope.threadID, g_scope.generation);
			if (!g_scope.active || globalOwner != expectedOwner ||
				GetCurrentThreadId() != g_scope.threadID) {
				// A different thread has no access to the owning TLS ledger. Do not let
				// it perform an unleased shared-state mutation during this capture.
				g_diag.crossThreadCallbacks.fetch_add(1, std::memory_order_relaxed);
				g_diag.skippedNativeMutations.fetch_add(1, std::memory_order_relaxed);
				LatchFault();
				return;
			}

            // The verified native OnVisible skips its facing-update call when
            // this flag is false (including the private light-space cull).
            // Keep owner/thread admission above: native still traverses children.
            // Expecting a facing-update callback here would falsely fail cleanup.
            if (!process.cameraRelatedUpdates) {
                native(billboard, process, alphaGroup);
                return;
            }

			++g_scope.callbacks;
			if (g_scope.rejected || g_faulted.load(std::memory_order_acquire) ||
				!SnapshotSubtree(billboard)) {
				g_diag.skippedNativeMutations.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			++g_scope.nativeCalls;
			g_diag.activeNativeCalls.fetch_add(1, std::memory_order_relaxed);
			native(billboard, process, alphaGroup);
			++g_scope.nativeReturns;
			g_diag.activeNativeReturns.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] std::uintptr_t ThunkAddress() noexcept
		{
			return std::bit_cast<std::uintptr_t>(&OnVisibleThunk);
		}

		[[nodiscard]] std::uintptr_t FacingUpdateThunkAddress() noexcept
		{
			return std::bit_cast<std::uintptr_t>(&FacingUpdateThunk);
		}

		[[nodiscard]] bool FacingUpdateHookOwned() noexcept
		{
			const auto callSite =
				g_facingUpdateCallSite.load(std::memory_order_acquire);
			std::uintptr_t branchTarget = 0;
			return callSite && ReadRel32Target(callSite, branchTarget) &&
				branchTarget == g_facingUpdateBranchTarget.load(
					std::memory_order_acquire) &&
				MatchBranchStub(branchTarget, FacingUpdateThunkAddress());
		}

		[[nodiscard]] bool ExactHookOwned() noexcept
		{
			if (!g_installed.load(std::memory_order_acquire))
				return false;
			const auto slot = g_slotAddress.load(std::memory_order_acquire);
			std::uintptr_t current = 0;
			return slot && ReadPointer(reinterpret_cast<const void*>(slot), current) &&
				current == ThunkAddress() && FacingUpdateHookOwned();
		}

		void ClearScope() noexcept
		{
			for (auto& entry : g_scope.entries)
				entry = {};
			for (auto& identity : g_scope.identities)
				identity = 0;
			g_scope.count = 0;
			g_scope.duplicates = 0;
			g_scope.callbacks = 0;
			g_scope.nativeCalls = 0;
			g_scope.nativeReturns = 0;
			g_scope.facingUpdateCalls = 0;
			g_scope.facingUpdateSkips = 0;
			g_scope.branchAnchors = 0;
			g_scope.branchGeometries = 0;
			g_scope.branchShifts = 0;
			g_scope.branchReadbackFailures = 0;
			g_scope.generation = 0;
			g_scope.threadID = 0;
			g_scope.active = false;
			g_scope.rejected = false;
			g_scope.rejectedBeforeMutation = false;
			g_scope.overflowed = false;
			g_scope.faulted = false;
		}
	}

	InstallStatus Install(bool requested) noexcept
	{
		if (!requested)
			return InstallStatus::kNotRequested;
		g_diag.installAttempts.fetch_add(1, std::memory_order_relaxed);
		std::lock_guard lock{ g_installLock };
		if (g_installed.load(std::memory_order_acquire)) {
			return g_faulted.load(std::memory_order_acquire) ?
				InstallStatus::kFaulted : InstallStatus::kAlreadyInstalled;
		}
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return g_installStatus.load(std::memory_order_acquire);
		const auto* contract = ExactRuntimeContract();
		if (!contract) {
			g_installStatus.store(
				InstallStatus::kUnsupportedRuntime, std::memory_order_release);
			return InstallStatus::kUnsupportedRuntime;
		}

		const auto base = REL::Module::get().base();
		const auto vtableAddress = ResolveContractAddress(
			contract->vtableID, contract->vtableRVA);
		const auto nativeAddress = ResolveContractAddress(
			contract->onVisibleID, contract->onVisibleRVA);
		const auto facingUpdateAddress = ResolveContractAddress(
			contract->facingUpdateID, contract->facingUpdateRVA);
		const auto facingUpdateCallSite =
			nativeAddress + kFacingUpdateCallOffset;
		const auto onVisibleSlot = OnVisibleSlot();
		const auto slotAddress = vtableAddress + onVisibleSlot * sizeof(void*);
		const auto thunkAddress = ThunkAddress();
		std::uintptr_t current = 0;
		std::uintptr_t decodedFacingUpdate = 0;
		if (vtableAddress != base + contract->vtableRVA ||
			nativeAddress != base + contract->onVisibleRVA ||
			facingUpdateAddress != base + contract->facingUpdateRVA ||
			!Executable(reinterpret_cast<const void*>(nativeAddress)) ||
			!Executable(reinterpret_cast<const void*>(facingUpdateAddress)) ||
			!Executable(reinterpret_cast<const void*>(thunkAddress)) ||
			!Executable(reinterpret_cast<const void*>(
				FacingUpdateThunkAddress())) ||
			!MatchSignature(
				reinterpret_cast<const void*>(nativeAddress), *contract) ||
			!ReadRel32Target(facingUpdateCallSite, decodedFacingUpdate) ||
			decodedFacingUpdate != facingUpdateAddress ||
			!ReadPointer(reinterpret_cast<const void*>(slotAddress), current) ||
			current != nativeAddress) {
			g_installStatus.store(
				InstallStatus::kNativeSignatureMismatch, std::memory_order_release);
			return InstallStatus::kNativeSignatureMismatch;
		}

		g_original.store(std::bit_cast<OnVisibleFunction*>(nativeAddress),
			std::memory_order_release);
		g_facingUpdateNative.store(
			std::bit_cast<FacingUpdateFunction*>(facingUpdateAddress),
			std::memory_order_release);
		std::uintptr_t chainedFacingUpdate = 0;
		try {
			chainedFacingUpdate = SKSE::GetTrampoline().write_call<5>(
				facingUpdateCallSite, FacingUpdateThunk);
		} catch (...) {
			g_installStatus.store(
				InstallStatus::kPatchFailed, std::memory_order_release);
			return InstallStatus::kPatchFailed;
		}
		g_facingUpdateCallSite.store(
			facingUpdateCallSite, std::memory_order_release);
		std::uintptr_t facingUpdateBranchTarget = 0;
		if (chainedFacingUpdate != facingUpdateAddress ||
			!ReadRel32Target(facingUpdateCallSite, facingUpdateBranchTarget) ||
			!MatchBranchStub(
				facingUpdateBranchTarget, FacingUpdateThunkAddress())) {
			g_facingUpdateBranchTarget.store(
				facingUpdateBranchTarget, std::memory_order_release);
			LatchFault();
			g_installStatus.store(
				InstallStatus::kPatchFailed, std::memory_order_release);
			return InstallStatus::kPatchFailed;
		}
		g_facingUpdateBranchTarget.store(
			facingUpdateBranchTarget, std::memory_order_release);
		REL::Relocation<std::uintptr_t> vtable{ vtableAddress };
		const auto captured = vtable.write_vfunc(onVisibleSlot, thunkAddress);
		std::uintptr_t installed = 0;
		if (captured != nativeAddress ||
			!ReadPointer(reinterpret_cast<const void*>(slotAddress), installed) ||
			installed != thunkAddress) {
			// Restore only while this transaction still owns the slot.
			if (installed == thunkAddress) {
				vtable.write_vfunc(onVisibleSlot, captured);
				std::uintptr_t restored = 0;
				if (!ReadPointer(reinterpret_cast<const void*>(slotAddress), restored) ||
					restored != captured) {
					LatchFault();
				}
			}
			g_installStatus.store(
				InstallStatus::kPatchFailed, std::memory_order_release);
			return InstallStatus::kPatchFailed;
		}

		g_slotAddress.store(slotAddress, std::memory_order_release);
		g_installed.store(true, std::memory_order_release);
		g_installStatus.store(InstallStatus::kInstalled, std::memory_order_release);
		g_diag.installSuccesses.fetch_add(1, std::memory_order_relaxed);
		return InstallStatus::kInstalled;
	}

	CaptureToken BeginCapture() noexcept
	{
		g_diag.beginAttempts.fetch_add(1, std::memory_order_relaxed);
		CaptureToken token{};
		token.threadID = GetCurrentThreadId();
		if (!g_installed.load(std::memory_order_acquire)) {
			token.status = BeginStatus::kNotInstalled;
		} else if (g_faulted.load(std::memory_order_acquire)) {
			token.status = BeginStatus::kFaulted;
		} else if (g_scope.active || g_scope.count) {
			token.status = BeginStatus::kNested;
			LatchFault();
		} else if (!ExactHookOwned()) {
			token.status = BeginStatus::kHookOwnershipLost;
			LatchFault();
		} else {
			ClearScope();
			auto generation =
				g_nextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
			if (!generation)
				generation = g_nextGeneration.fetch_add(1,
					std::memory_order_relaxed) + 1;
			g_scope.generation = generation;
			g_scope.threadID = token.threadID;
			g_scope.active = true;
			const auto owner = PackOwner(token.threadID, generation);
			std::uint64_t empty = 0;
			if (g_activeOwner.compare_exchange_strong(
					empty, owner, std::memory_order_release,
					std::memory_order_acquire)) {
				token.generation = generation;
				token.status = BeginStatus::kBegan;
				token.active = true;
				g_diag.begins.fetch_add(1, std::memory_order_relaxed);
				return token;
			}
			ClearScope();
			token.status = BeginStatus::kAnotherThreadActive;
		}
		g_diag.beginRejects.fetch_add(1, std::memory_order_relaxed);
		return token;
	}

	CaptureResult EndAndRestore(CaptureToken& token) noexcept
	{
		CaptureResult result{};
		result.faulted = g_faulted.load(std::memory_order_acquire);
		if (!token.active)
			return result;

		const auto thread = GetCurrentThreadId();
		const auto scopeOwner = PackOwner(g_scope.threadID, g_scope.generation);
		const auto tokenOwner = PackOwner(token.threadID, token.generation);
		const auto globalOwner = g_activeOwner.load(std::memory_order_acquire);
		result.tokenMatched = g_scope.active && thread == token.threadID &&
			g_scope.threadID == token.threadID &&
			g_scope.generation == token.generation && globalOwner == tokenOwner;
		if (!result.tokenMatched) {
			g_diag.tokenMismatches.fetch_add(1, std::memory_order_relaxed);
			LatchFault();
		}
		// Never clear another thread's TLS. Its owning finally remains responsible.
		if (!g_scope.active || thread != g_scope.threadID) {
			// Preserve the token while the process-wide owner is still armed so a
			// later call on the owning thread can restore and release its ledger.
			if (!g_activeOwner.load(std::memory_order_acquire))
				token.active = false;
			result.faulted = true;
			return result;
		}

		// Disable interception before the first shared-state write or DecRef.
		g_scope.active = false;
		if (globalOwner == scopeOwner) {
			std::uint64_t expected = scopeOwner;
			if (!g_activeOwner.compare_exchange_strong(expected, 0,
					std::memory_order_acq_rel, std::memory_order_acquire)) {
				result.tokenMatched = false;
			}
		} else {
			result.tokenMatched = false;
		}

		result.recordedObjects = g_scope.count;
		result.duplicateObjects = g_scope.duplicates;
		result.billboardCallbacks = g_scope.callbacks;
		result.nativeCalls = g_scope.nativeCalls;
		result.nativeReturns = g_scope.nativeReturns;
		result.facingUpdateCalls = g_scope.facingUpdateCalls;
		result.facingUpdateSkips = g_scope.facingUpdateSkips;
		result.branchAnchors = g_scope.branchAnchors;
		result.branchGeometries = g_scope.branchGeometries;
		result.branchShifts = g_scope.branchShifts;
		result.branchReadbackFailures = g_scope.branchReadbackFailures;
		result.rejectedBeforeMutation = g_scope.rejectedBeforeMutation;
		result.overflowed = g_scope.overflowed;
		const auto count = std::min(g_scope.count, g_scope.entries.size());

		// Restore the deepest/latest snapshots first, while every object is still
		// strongly retained. First snapshots were recorded in parent-first order.
		for (std::size_t index = count; index > 0; --index) {
			auto& entry = g_scope.entries[index - 1];
			++result.restoreAttempts;
			if (entry.object && entry.ownsReference &&
				RestoreState(entry.object, entry.state)) {
				++result.restoreSuccesses;
				g_diag.restoredObjects.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_diag.restoreFailures.fetch_add(1, std::memory_order_relaxed);
			}
		}
		for (std::size_t index = count; index > 0; --index) {
			auto& entry = g_scope.entries[index - 1];
			auto* object = entry.object;
			const bool owned = entry.ownsReference;
			entry.object = nullptr;
			entry.ownsReference = false;
			++result.releaseAttempts;
			if (object && owned && Release(object)) {
				++result.releaseSuccesses;
				g_diag.releasedObjects.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_diag.releaseFailures.fetch_add(1, std::memory_order_relaxed);
			}
		}

		const bool clean = result.tokenMatched && !g_scope.rejected &&
			!g_scope.faulted && !g_faulted.load(std::memory_order_acquire) &&
			g_scope.count <= g_scope.entries.size() &&
			result.restoreAttempts == result.recordedObjects &&
			result.restoreSuccesses == result.restoreAttempts &&
			result.releaseAttempts == result.recordedObjects &&
			result.releaseSuccesses == result.releaseAttempts &&
			result.nativeCalls == result.nativeReturns &&
			result.facingUpdateCalls == result.nativeCalls &&
			result.facingUpdateSkips == 0 &&
			result.branchShifts == result.branchAnchors &&
			result.branchReadbackFailures == 0;
		if (clean) {
			g_diag.completedScopes.fetch_add(1, std::memory_order_relaxed);
		} else {
			g_diag.rejectedScopes.fetch_add(1, std::memory_order_relaxed);
			LatchFault();
		}
		ClearScope();
		token.active = false;
		result.clean = clean;
		result.faulted = g_faulted.load(std::memory_order_acquire);
		return result;
	}

	bool CurrentCaptureHealthy() noexcept
	{
		return g_scope.active && !g_scope.rejected && !g_scope.faulted &&
			!g_faulted.load(std::memory_order_acquire) &&
			g_activeOwner.load(std::memory_order_acquire) ==
				PackOwner(g_scope.threadID, g_scope.generation);
	}

	bool Installed() noexcept
	{
		return g_installed.load(std::memory_order_acquire);
	}

	bool OwnsHook() noexcept
	{
		return ExactHookOwned();
	}

	bool Faulted() noexcept
	{
		return g_faulted.load(std::memory_order_acquire);
	}

	Diagnostics GetDiagnostics() noexcept
	{
		return {
			.installAttempts = g_diag.installAttempts.load(std::memory_order_relaxed),
			.installSuccesses = g_diag.installSuccesses.load(std::memory_order_relaxed),
			.beginAttempts = g_diag.beginAttempts.load(std::memory_order_relaxed),
			.begins = g_diag.begins.load(std::memory_order_relaxed),
			.beginRejects = g_diag.beginRejects.load(std::memory_order_relaxed),
			.hookCalls = g_diag.hookCalls.load(std::memory_order_relaxed),
			.inactiveNativeCalls = g_diag.inactiveNativeCalls.load(std::memory_order_relaxed),
			.activeNativeCalls = g_diag.activeNativeCalls.load(std::memory_order_relaxed),
			.activeNativeReturns = g_diag.activeNativeReturns.load(std::memory_order_relaxed),
			.facingHookCalls = g_diag.facingHookCalls.load(std::memory_order_relaxed),
			.inactiveFacingNativeCalls = g_diag.inactiveFacingNativeCalls.load(std::memory_order_relaxed),
			.activeFacingUpdateSkips = g_diag.activeFacingUpdateSkips.load(std::memory_order_relaxed),
			.crossThreadFacingCalls = g_diag.crossThreadFacingCalls.load(std::memory_order_relaxed),
			.recordedObjects = g_diag.recordedObjects.load(std::memory_order_relaxed),
			.duplicateObjects = g_diag.duplicateObjects.load(std::memory_order_relaxed),
			.capacityOverflows = g_diag.capacityOverflows.load(std::memory_order_relaxed),
			.snapshotFaults = g_diag.snapshotFaults.load(std::memory_order_relaxed),
			.retainFaults = g_diag.retainFaults.load(std::memory_order_relaxed),
			.skippedNativeMutations = g_diag.skippedNativeMutations.load(std::memory_order_relaxed),
			.completedScopes = g_diag.completedScopes.load(std::memory_order_relaxed),
			.rejectedScopes = g_diag.rejectedScopes.load(std::memory_order_relaxed),
			.restoredObjects = g_diag.restoredObjects.load(std::memory_order_relaxed),
			.restoreFailures = g_diag.restoreFailures.load(std::memory_order_relaxed),
			.releasedObjects = g_diag.releasedObjects.load(std::memory_order_relaxed),
			.releaseFailures = g_diag.releaseFailures.load(std::memory_order_relaxed),
			.tokenMismatches = g_diag.tokenMismatches.load(std::memory_order_relaxed),
			.crossThreadCallbacks = g_diag.crossThreadCallbacks.load(std::memory_order_relaxed),
			.branchAnchors = g_diag.branchAnchors.load(std::memory_order_relaxed),
			.branchGeometries = g_diag.branchGeometries.load(std::memory_order_relaxed),
			.branchShifts = g_diag.branchShifts.load(std::memory_order_relaxed),
			.branchReadbackFailures = g_diag.branchReadbackFailures.load(
				std::memory_order_relaxed),
			.maximumPreAnchorDrift = std::bit_cast<float>(
				g_diag.maximumPreAnchorDriftBits.load(std::memory_order_relaxed)),
			.maximumPostAnchorResidual = std::bit_cast<float>(
				g_diag.maximumPostAnchorResidualBits.load(std::memory_order_relaxed)),
			.lastExceptionCode = g_lastExceptionCode.load(std::memory_order_relaxed),
			.installed = Installed(),
			.hookOwned = OwnsHook(),
			.faulted = Faulted()
		};
	}
}
