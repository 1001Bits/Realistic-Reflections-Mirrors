#include "PCH.h"

#include "HandMirrorParticleCameraLease.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <mutex>

namespace HandMirrorParticleCameraLease
{
	namespace
	{
		using NativePackFunction = void(
			std::uint32_t, RE::NiParticles*, void*);

		struct RuntimeContract
		{
			std::uint64_t genericDrawID{ 0 };
			std::uintptr_t genericDrawRVA{ 0 };
			std::uint64_t nativePackID{ 0 };
			std::uintptr_t nativePackRVA{ 0 };
		};

		// Exact executable/Address Library evidence.  The generic-draw packer call
		// is at +0xC4 on both flat runtimes and its masked 32-byte call window is
		// byte-identical apart from the rel32 displacement.
		constexpr RuntimeContract kSE1597{
			.genericDrawID = 100847,
			.genericDrawRVA = 0x1307160,
			.nativePackID = 75657,
			.nativePackRVA = 0xD76080
		};
		constexpr RuntimeContract kAE161170{
			.genericDrawID = 107637,
			.genericDrawRVA = 0x14F2AD0,
			.nativePackID = 77463,
			.nativePackRVA = 0xE51320
		};
		// GOG 1.6.1179: same AE IDs, generic-draw 0x14F3B40 / pack 0xE52E20, and
		// the +0xC4 masked call window matches AE outside its rel32.
		constexpr RuntimeContract kAE161179{
			.genericDrawID = 107637,
			.genericDrawRVA = 0x14F3B40,
			.nativePackID = 77463,
			.nativePackRVA = 0xE52E20
		};
		// Skyrim 1.7.104: the same AE IDs resolve to 0x155F050 / 0x1016B90, and the
		// +0xC4 masked call window is byte-identical to AE outside its rel32.
		constexpr RuntimeContract kAE17104{
			.genericDrawID = 107637,
			.genericDrawRVA = 0x155F050,
			.nativePackID = 77463,
			.nativePackRVA = 0x1016B90
		};
		// Skyrim VR 1.4.15 (Ghidra 2026-09-04): GenericDraw 100847 -> VR CSV
		// 0x1348390; the +0xC4 call window is byte-identical to flat and targets
		// the native particle pack at 0xDC8F10, which has no VR address-library
		// ID (nativePackID 0 => resolved as module base + RVA).
		constexpr RuntimeContract kVR1415{
			.genericDrawID = 100847,
			.genericDrawRVA = 0x1348390,
			.nativePackID = 0,
			.nativePackRVA = 0xDC8F10
		};
		constexpr std::ptrdiff_t kPackCallOffset = 0xC4;
		constexpr std::size_t kWindowPrefixSize = 13;
		constexpr std::size_t kCallOffsetInWindow = kWindowPrefixSize;
		constexpr std::size_t kWindowSize = 32;
		constexpr std::array<std::uint8_t, kWindowSize> kCallWindow{
			0x48, 0x85, 0xC0, 0x74, 0x24, 0x4C, 0x8B, 0xC0,
			0x49, 0x8B, 0xD6, 0x8B, 0xCF, 0xE8, 0x00, 0x00,
			0x00, 0x00, 0x4C, 0x8D, 0x84, 0x24, 0x80, 0x00,
			0x00, 0x00, 0x48, 0x8B, 0xD6, 0x48, 0x8D, 0x0D
		};

		// RendererShadowState fields read directly by the native particle pack
		// (flat 0x140D76080).  VR keeps two eyes of each field; the pack reads eye
		// 0 (posAdjust[0] 0x3A4, cameraData[0] 0x3E0).
		struct ShadowStateLayout
		{
			std::ptrdiff_t posAdjust{ 0 };
			std::ptrdiff_t cameraData{ 0 };
		};
		constexpr ShadowStateLayout kFlatShadowStateLayout{
			.posAdjust = 0x35C,
			.cameraData = 0x380
		};
		constexpr ShadowStateLayout kVRShadowStateLayout{
			.posAdjust = 0x3A4,
			.cameraData = 0x3E0
		};
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, posAdjust) ==
			kFlatShadowStateLayout.posAdjust);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, cameraData) ==
			kFlatShadowStateLayout.cameraData);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, posAdjust) ==
			kVRShadowStateLayout.posAdjust);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, cameraData) ==
			kVRShadowStateLayout.cameraData);

		[[nodiscard]] const ShadowStateLayout& CurrentShadowStateLayout() noexcept
		{
			return SupportedRuntimePolicy::IsExactVRRuntime() ?
				kVRShadowStateLayout : kFlatShadowStateLayout;
		}

		constexpr std::size_t kOriginSize = 0x0C;
		constexpr std::size_t kFacingBasisSize = 0x30;
		constexpr std::size_t kFiveByteBranchStubSize = 14;

		struct CameraInputs
		{
			alignas(float) std::array<std::byte, kOriginSize> origin{};
			alignas(float) std::array<std::byte, kFacingBasisSize> basis{};

			friend bool operator==(const CameraInputs&, const CameraInputs&) noexcept =
				default;
		};

		struct Scope
		{
			CameraInputs expected{};
			std::uint64_t packCalls{ 0 };
			std::uint64_t correctedCalls{ 0 };
			std::uint64_t alreadyPrivateCalls{ 0 };
			std::uint64_t nativeCalls{ 0 };
			std::uint64_t nativeReturns{ 0 };
			std::uint64_t restoreAttempts{ 0 };
			std::uint64_t restoreSuccesses{ 0 };
			std::uint32_t generation{ 0 };
			std::uint32_t threadID{ 0 };
			bool active{ false };
			bool transactionActive{ false };
			bool faulted{ false };
		};

		struct AtomicDiagnostics
		{
			std::atomic<std::uint64_t> installAttempts{ 0 };
			std::atomic<std::uint64_t> installSuccesses{ 0 };
			std::atomic<std::uint64_t> beginAttempts{ 0 };
			std::atomic<std::uint64_t> begins{ 0 };
			std::atomic<std::uint64_t> beginRejects{ 0 };
			std::atomic<std::uint64_t> hookCalls{ 0 };
			std::atomic<std::uint64_t> inactiveNativeCalls{ 0 };
			std::atomic<std::uint64_t> activePackCalls{ 0 };
			std::atomic<std::uint64_t> alreadyPrivateCalls{ 0 };
			std::atomic<std::uint64_t> correctedCalls{ 0 };
			std::atomic<std::uint64_t> nativeCalls{ 0 };
			std::atomic<std::uint64_t> nativeReturns{ 0 };
			std::atomic<std::uint64_t> restoreAttempts{ 0 };
			std::atomic<std::uint64_t> restoreSuccesses{ 0 };
			std::atomic<std::uint64_t> scopeCompletions{ 0 };
			std::atomic<std::uint64_t> scopeRejects{ 0 };
			std::atomic<std::uint64_t> crossThreadCalls{ 0 };
			std::atomic<std::uint64_t> tokenMismatches{ 0 };
			std::atomic<std::uint32_t> maximumOriginDriftBits{ 0 };
			std::atomic<std::uint32_t> maximumBasisDriftBits{ 0 };
		};

		AtomicDiagnostics g_diag{};
		std::atomic<NativePackFunction*> g_native{ nullptr };
		std::atomic_bool g_installed{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_installAttempted{ false };
		std::atomic<InstallStatus> g_installStatus{ InstallStatus::kNotRequested };
		std::atomic<std::uintptr_t> g_callSite{ 0 };
		std::atomic<std::uintptr_t> g_installedBranchTarget{ 0 };
		std::atomic<std::uint32_t> g_nextGeneration{ 0 };
		std::atomic<std::uint64_t> g_activeOwner{ 0 };
		std::atomic<std::uint32_t> g_lastExceptionCode{ 0 };
		std::mutex g_installLock;
		thread_local Scope g_scope{};

		[[nodiscard]] std::uint64_t PackOwner(
			const std::uint32_t threadID,
			const std::uint32_t generation) noexcept
		{
			return (static_cast<std::uint64_t>(threadID) << 32) | generation;
		}

		[[nodiscard]] int ExceptionFilter(const unsigned long code) noexcept
		{
			g_lastExceptionCode.store(code, std::memory_order_relaxed);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void LatchFault() noexcept
		{
			g_faulted.store(true, std::memory_order_release);
			if (g_scope.active) {
				g_scope.faulted = true;
			}
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
					sizeof(information) ||
				information.State != MEM_COMMIT ||
				(information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}
			const auto protection = information.Protect & 0xFF;
			return protection == PAGE_EXECUTE ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool CopySEH(
			const void* source, void* destination, const std::size_t size) noexcept
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
			const std::uintptr_t callSite, std::uintptr_t& target) noexcept
		{
			target = 0;
			std::array<std::uint8_t, 5> instruction{};
			if (!CopySEH(reinterpret_cast<const void*>(callSite),
					instruction.data(), instruction.size()) || instruction[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, instruction.data() + 1, sizeof(displacement));
			target = callSite + instruction.size() + displacement;
			return target != 0;
		}

		[[nodiscard]] bool MatchCallWindow(
			const std::uintptr_t windowAddress,
			std::uintptr_t& decodedTarget) noexcept
		{
			decodedTarget = 0;
			std::array<std::uint8_t, kWindowSize> observed{};
			if (!CopySEH(reinterpret_cast<const void*>(windowAddress),
					observed.data(), observed.size())) {
				return false;
			}
			for (std::size_t index = 0; index < observed.size(); ++index) {
				if (index > kCallOffsetInWindow &&
					index < kCallOffsetInWindow + 5) {
					continue;
				}
				if (observed[index] != kCallWindow[index])
					return false;
			}
			return ReadRel32Target(
				windowAddress + kCallOffsetInWindow, decodedTarget);
		}

		[[nodiscard]] bool MatchBranchStub(
			const std::uintptr_t stubAddress,
			const std::uintptr_t expectedDestination) noexcept
		{
			if (!Executable(reinterpret_cast<const void*>(stubAddress)) ||
				!Executable(reinterpret_cast<const void*>(expectedDestination))) {
				return false;
			}
			std::array<std::uint8_t, kFiveByteBranchStubSize> stub{};
			if (!CopySEH(reinterpret_cast<const void*>(stubAddress),
					stub.data(), stub.size()) || stub[0] != 0xFF || stub[1] != 0x25 ||
				stub[2] != 0 || stub[3] != 0 || stub[4] != 0 || stub[5] != 0) {
				return false;
			}
			std::uintptr_t destination = 0;
			std::memcpy(&destination, stub.data() + 6, sizeof(destination));
			return destination == expectedDestination;
		}

		[[nodiscard]] std::uintptr_t ThunkAddress() noexcept;

		[[nodiscard]] bool HookOwned() noexcept
		{
			if (!g_installed.load(std::memory_order_acquire))
				return false;
			const auto callSite = g_callSite.load(std::memory_order_acquire);
			std::uintptr_t branchTarget = 0;
			return callSite && ReadRel32Target(callSite, branchTarget) &&
				branchTarget == g_installedBranchTarget.load(std::memory_order_acquire) &&
				MatchBranchStub(branchTarget, ThunkAddress());
		}

		[[nodiscard]] bool FiniteCameraInputs(const CameraInputs& inputs) noexcept
		{
			constexpr std::uint32_t exponentMask = 0x7F800000U;
			const auto finiteSpan = [exponentMask](
					const auto& bytes) noexcept {
				for (std::size_t offset = 0; offset < bytes.size();
					offset += sizeof(std::uint32_t)) {
					std::uint32_t bits = 0;
					std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
					if ((bits & exponentMask) == exponentMask)
						return false;
				}
				return true;
			};
			if (!finiteSpan(inputs.origin) || !finiteSpan(inputs.basis))
				return false;
			return true;
		}

		[[nodiscard]] float ReadFloatBits(
			const std::byte* bytes, const std::size_t index) noexcept
		{
			std::uint32_t bits = 0;
			std::memcpy(&bits, bytes + index * sizeof(bits), sizeof(bits));
			return std::bit_cast<float>(bits);
		}

		[[nodiscard]] bool FiniteFloatBits(const float value) noexcept
		{
			return (std::bit_cast<std::uint32_t>(value) & 0x7F800000U) !=
				0x7F800000U;
		}

		[[nodiscard]] bool BuildExpectedCameraInputs(
			const RE::NiCamera* camera, CameraInputs& output) noexcept
		{
			output = {};
			if (!camera)
				return false;
			__try {
				const auto& world = camera->world;
				std::array<float, 3> origin{
					world.translate.x, world.translate.y, world.translate.z };
				std::array<float, 12> basis{
					world.rotate.entry[0][1], world.rotate.entry[1][1],
					world.rotate.entry[2][1], 0.0F,
					world.rotate.entry[0][2], world.rotate.entry[1][2],
					world.rotate.entry[2][2], 0.0F,
					world.rotate.entry[0][0], world.rotate.entry[1][0],
					world.rotate.entry[2][0], 0.0F
				};
				std::memcpy(output.origin.data(), origin.data(), output.origin.size());
				std::memcpy(output.basis.data(), basis.data(), output.basis.size());
				return FiniteCameraInputs(output);
			} __except (ExceptionFilter(GetExceptionCode())) {
				output = {};
				return false;
			}
		}

		[[nodiscard]] bool ReadLiveCameraInputs(CameraInputs& output) noexcept
		{
			output = {};
			auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			if (!shadowState)
				return false;
			const auto* bytes = reinterpret_cast<const std::byte*>(shadowState);
			const auto& layout = CurrentShadowStateLayout();
			return CopySEH(bytes + layout.posAdjust, output.origin.data(),
					output.origin.size()) &&
				CopySEH(bytes + layout.cameraData, output.basis.data(),
					output.basis.size()) && FiniteCameraInputs(output);
		}

		[[nodiscard]] bool WriteLiveCameraInputs(
			const CameraInputs& inputs) noexcept
		{
			auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			if (!shadowState || !FiniteCameraInputs(inputs))
				return false;
			auto* bytes = reinterpret_cast<std::byte*>(shadowState);
			const auto& layout = CurrentShadowStateLayout();
			__try {
				std::memcpy(bytes + layout.posAdjust, inputs.origin.data(),
					inputs.origin.size());
				std::memcpy(bytes + layout.cameraData, inputs.basis.data(),
					inputs.basis.size());
				CameraInputs readback{};
				return ReadLiveCameraInputs(readback) && readback == inputs;
			} __except (ExceptionFilter(GetExceptionCode())) {
				return false;
			}
		}

		void UpdateMaximum(
			std::atomic<std::uint32_t>& destination,
			const float value) noexcept
		{
			if (!FiniteFloatBits(value) ||
				(std::bit_cast<std::uint32_t>(value) & 0x80000000U) != 0)
				return;
			auto current = destination.load(std::memory_order_relaxed);
			while (value > std::bit_cast<float>(current) &&
				!destination.compare_exchange_weak(
					current, std::bit_cast<std::uint32_t>(value),
					std::memory_order_relaxed)) {
			}
		}

		void RecordDrift(
			const CameraInputs& live, const CameraInputs& expected) noexcept
		{
			float originDrift = 0.0F;
			float basisDrift = 0.0F;
			for (std::size_t index = 0; index < kOriginSize / sizeof(float); ++index) {
				originDrift = std::max(originDrift,
					std::abs(ReadFloatBits(live.origin.data(), index) -
						ReadFloatBits(expected.origin.data(), index)));
			}
			for (std::size_t index = 0; index < kFacingBasisSize / sizeof(float); ++index) {
				basisDrift = std::max(basisDrift,
					std::abs(ReadFloatBits(live.basis.data(), index) -
						ReadFloatBits(expected.basis.data(), index)));
			}
			UpdateMaximum(g_diag.maximumOriginDriftBits, originDrift);
			UpdateMaximum(g_diag.maximumBasisDriftBits, basisDrift);
		}

		void NativePackThunk(
			const std::uint32_t count,
			RE::NiParticles* particles,
			void* output)
		{
			g_diag.hookCalls.fetch_add(1, std::memory_order_relaxed);
			auto* native = g_native.load(std::memory_order_acquire);
			if (!native) {
				LatchFault();
				return;
			}

			const auto owner = g_activeOwner.load(std::memory_order_acquire);
			if (!owner && !g_scope.active) {
				g_diag.inactiveNativeCalls.fetch_add(1, std::memory_order_relaxed);
				native(count, particles, output);
				return;
			}

			const auto expectedOwner = PackOwner(g_scope.threadID, g_scope.generation);
			if (!g_scope.active || owner != expectedOwner ||
				GetCurrentThreadId() != g_scope.threadID || g_scope.transactionActive) {
				g_diag.crossThreadCalls.fetch_add(1, std::memory_order_relaxed);
				LatchFault();
				// Preserve Skyrim's native draw even when this capture becomes ineligible.
				native(count, particles, output);
				return;
			}

			++g_scope.packCalls;
			g_diag.activePackCalls.fetch_add(1, std::memory_order_relaxed);
			CameraInputs saved{};
			if (!ReadLiveCameraInputs(saved)) {
				LatchFault();
				native(count, particles, output);
				return;
			}
			RecordDrift(saved, g_scope.expected);
			const bool correctionRequired = saved != g_scope.expected;
			bool corrected = !correctionRequired;
			if (correctionRequired) {
				g_scope.transactionActive = true;
				corrected = WriteLiveCameraInputs(g_scope.expected);
				if (corrected) {
					++g_scope.correctedCalls;
					g_diag.correctedCalls.fetch_add(1, std::memory_order_relaxed);
				} else {
					LatchFault();
				}
			} else {
				++g_scope.alreadyPrivateCalls;
				g_diag.alreadyPrivateCalls.fetch_add(1, std::memory_order_relaxed);
			}

			if (corrected) {
				++g_scope.nativeCalls;
				g_diag.nativeCalls.fetch_add(1, std::memory_order_relaxed);
				__try {
					native(count, particles, output);
					++g_scope.nativeReturns;
					g_diag.nativeReturns.fetch_add(1, std::memory_order_relaxed);
				} __finally {
					if (correctionRequired) {
						++g_scope.restoreAttempts;
						g_diag.restoreAttempts.fetch_add(1, std::memory_order_relaxed);
						if (WriteLiveCameraInputs(saved)) {
							++g_scope.restoreSuccesses;
							g_diag.restoreSuccesses.fetch_add(
								1, std::memory_order_relaxed);
						} else {
							LatchFault();
						}
						g_scope.transactionActive = false;
					}
				}
			} else {
				// The correction failed before native entry. Restore the first saved
				// state, then allow the native pack once while rejecting publication.
				if (correctionRequired) {
					++g_scope.restoreAttempts;
					g_diag.restoreAttempts.fetch_add(1, std::memory_order_relaxed);
					if (WriteLiveCameraInputs(saved)) {
						++g_scope.restoreSuccesses;
						g_diag.restoreSuccesses.fetch_add(
							1, std::memory_order_relaxed);
					}
					g_scope.transactionActive = false;
				}
				native(count, particles, output);
			}
		}

		[[nodiscard]] std::uintptr_t ThunkAddress() noexcept
		{
			return std::bit_cast<std::uintptr_t>(&NativePackThunk);
		}

		void ClearScope() noexcept
		{
			g_scope = {};
		}
	}

	InstallStatus Install(const bool requested) noexcept
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
		const auto genericDraw = ResolveContractAddress(
			contract->genericDrawID, contract->genericDrawRVA);
		const auto nativePack = ResolveContractAddress(
			contract->nativePackID, contract->nativePackRVA);
		const auto callSite = genericDraw + kPackCallOffset;
		const auto windowAddress = callSite - kWindowPrefixSize;
		std::uintptr_t decodedTarget = 0;
		if (genericDraw != base + contract->genericDrawRVA ||
			nativePack != base + contract->nativePackRVA ||
			!Executable(reinterpret_cast<const void*>(genericDraw)) ||
			!Executable(reinterpret_cast<const void*>(nativePack)) ||
			!MatchCallWindow(windowAddress, decodedTarget) ||
			decodedTarget != nativePack) {
			g_installStatus.store(
				InstallStatus::kSignatureMismatch, std::memory_order_release);
			return InstallStatus::kSignatureMismatch;
		}

		g_native.store(std::bit_cast<NativePackFunction*>(nativePack),
			std::memory_order_release);
		std::uintptr_t chainedTarget = 0;
		try {
			chainedTarget = SKSE::GetTrampoline().write_call<5>(
				callSite, NativePackThunk);
		} catch (...) {
			g_installStatus.store(
				InstallStatus::kPatchFailed, std::memory_order_release);
			return InstallStatus::kPatchFailed;
		}
		g_callSite.store(callSite, std::memory_order_release);
		g_installed.store(true, std::memory_order_release);
		std::uintptr_t branchTarget = 0;
		if (chainedTarget != nativePack ||
			!ReadRel32Target(callSite, branchTarget) ||
			!MatchBranchStub(branchTarget, ThunkAddress())) {
			g_installedBranchTarget.store(branchTarget, std::memory_order_release);
			LatchFault();
			g_installStatus.store(
				InstallStatus::kPatchFailed, std::memory_order_release);
			return InstallStatus::kPatchFailed;
		}
		g_installedBranchTarget.store(branchTarget, std::memory_order_release);
		g_installStatus.store(InstallStatus::kInstalled, std::memory_order_release);
		g_diag.installSuccesses.fetch_add(1, std::memory_order_relaxed);
		return InstallStatus::kInstalled;
	}

	CaptureToken BeginCapture(const RE::NiCamera* camera) noexcept
	{
		g_diag.beginAttempts.fetch_add(1, std::memory_order_relaxed);
		CaptureToken token{};
		token.threadID = GetCurrentThreadId();
		if (!Installed()) {
			token.status = BeginStatus::kNotInstalled;
		} else if (Faulted()) {
			token.status = BeginStatus::kFaulted;
		} else if (g_scope.active || g_scope.transactionActive) {
			token.status = BeginStatus::kNested;
			LatchFault();
		} else if (!OwnsHook()) {
			token.status = BeginStatus::kHookOwnershipLost;
			LatchFault();
		} else {
			CameraInputs expected{};
			if (!BuildExpectedCameraInputs(camera, expected)) {
				token.status = BeginStatus::kInvalidCamera;
			} else {
				ClearScope();
				auto generation =
					g_nextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
				if (!generation) {
					generation = g_nextGeneration.fetch_add(
						1, std::memory_order_relaxed) + 1;
				}
				g_scope.expected = expected;
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
		}
		g_diag.beginRejects.fetch_add(1, std::memory_order_relaxed);
		return token;
	}

	CaptureResult EndCapture(CaptureToken& token) noexcept
	{
		CaptureResult result{};
		result.faulted = Faulted();
		if (!token.active)
			return result;
		const auto threadID = GetCurrentThreadId();
		const auto expectedOwner = PackOwner(token.threadID, token.generation);
		const auto scopeOwner = PackOwner(g_scope.threadID, g_scope.generation);
		const auto activeOwner = g_activeOwner.load(std::memory_order_acquire);
		result.tokenMatched = g_scope.active && threadID == token.threadID &&
			g_scope.threadID == token.threadID &&
			g_scope.generation == token.generation &&
			activeOwner == expectedOwner && !g_scope.transactionActive;
		if (!result.tokenMatched) {
			g_diag.tokenMismatches.fetch_add(1, std::memory_order_relaxed);
			LatchFault();
		}
		if (!g_scope.active || threadID != g_scope.threadID) {
			// A non-owner caller cannot clear the owning thread's TLS ledger. Keep
			// the token live while the process-wide owner is still armed so the
			// owning finally retains cleanup authority.
			if (!g_activeOwner.load(std::memory_order_acquire))
				token.active = false;
			result.faulted = true;
			return result;
		}

		g_scope.active = false;
		if (activeOwner == scopeOwner) {
			std::uint64_t expected = scopeOwner;
			if (!g_activeOwner.compare_exchange_strong(
					expected, 0, std::memory_order_acq_rel,
					std::memory_order_acquire)) {
				result.tokenMatched = false;
			}
		} else {
			result.tokenMatched = false;
		}
		result.packCalls = g_scope.packCalls;
		result.correctedCalls = g_scope.correctedCalls;
		result.alreadyPrivateCalls = g_scope.alreadyPrivateCalls;
		result.nativeCalls = g_scope.nativeCalls;
		result.nativeReturns = g_scope.nativeReturns;
		result.restoreAttempts = g_scope.restoreAttempts;
		result.restoreSuccesses = g_scope.restoreSuccesses;
		result.clean = result.tokenMatched && !g_scope.faulted && !Faulted() &&
			!g_scope.transactionActive && result.nativeCalls == result.nativeReturns &&
			result.restoreAttempts == result.restoreSuccesses &&
			result.correctedCalls == result.restoreAttempts;
		if (result.clean) {
			g_diag.scopeCompletions.fetch_add(1, std::memory_order_relaxed);
		} else {
			g_diag.scopeRejects.fetch_add(1, std::memory_order_relaxed);
			LatchFault();
		}
		ClearScope();
		token.active = false;
		result.faulted = Faulted();
		return result;
	}

	bool TryGetExpectedPrivateOrigin(PrivateOrigin& output) noexcept
	{
		output = {};
		if (!CurrentCaptureHealthy() || GetCurrentThreadId() != g_scope.threadID)
			return false;
		PrivateOrigin copied{};
		static_assert(sizeof(copied) == kOriginSize);
		if (!CopySEH(g_scope.expected.origin.data(), &copied, sizeof(copied)) ||
			!FiniteFloatBits(copied.x) || !FiniteFloatBits(copied.y) ||
			!FiniteFloatBits(copied.z)) {
			return false;
		}
		output = copied;
		return true;
	}

	bool CurrentCaptureHealthy() noexcept
	{
		return g_scope.active && !g_scope.faulted && !g_scope.transactionActive &&
			!Faulted() && g_activeOwner.load(std::memory_order_acquire) ==
				PackOwner(g_scope.threadID, g_scope.generation);
	}

	bool Installed() noexcept
	{
		return g_installed.load(std::memory_order_acquire);
	}

	bool OwnsHook() noexcept
	{
		return HookOwned();
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
			.inactiveNativeCalls =
				g_diag.inactiveNativeCalls.load(std::memory_order_relaxed),
			.activePackCalls = g_diag.activePackCalls.load(std::memory_order_relaxed),
			.alreadyPrivateCalls =
				g_diag.alreadyPrivateCalls.load(std::memory_order_relaxed),
			.correctedCalls = g_diag.correctedCalls.load(std::memory_order_relaxed),
			.nativeCalls = g_diag.nativeCalls.load(std::memory_order_relaxed),
			.nativeReturns = g_diag.nativeReturns.load(std::memory_order_relaxed),
			.restoreAttempts = g_diag.restoreAttempts.load(std::memory_order_relaxed),
			.restoreSuccesses = g_diag.restoreSuccesses.load(std::memory_order_relaxed),
			.scopeCompletions =
				g_diag.scopeCompletions.load(std::memory_order_relaxed),
			.scopeRejects = g_diag.scopeRejects.load(std::memory_order_relaxed),
			.crossThreadCalls = g_diag.crossThreadCalls.load(std::memory_order_relaxed),
			.tokenMismatches = g_diag.tokenMismatches.load(std::memory_order_relaxed),
			.lastExceptionCode = g_lastExceptionCode.load(std::memory_order_relaxed),
			.maximumOriginDrift =
				std::bit_cast<float>(g_diag.maximumOriginDriftBits.load(
					std::memory_order_relaxed)),
			.maximumBasisDrift =
				std::bit_cast<float>(g_diag.maximumBasisDriftBits.load(
					std::memory_order_relaxed)),
			.installed = Installed(),
			.hookOwned = OwnsHook(),
			.faulted = Faulted()
		};
	}
}
