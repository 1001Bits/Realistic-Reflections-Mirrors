#include "PCH.h"

#include "HandMirrorReadOnlyObserver.h"

#include "FirstPersonViewCallSiteSignature.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorFirstPersonViewHookChainPolicy.h"
#include "HandMirrorReflectionRuntime.h"
#include "HandMirrorReadOnlyProbePolicy.h"
#include "MirrorPaneDeliverySignature.h"
#include "MirrorsOfSkyrimGeometryDrawObserverOwner.h"
#include "SecondView.h"
namespace GeometryDrawObserver = MirrorsOfSkyrimGeometryDrawObserver;

#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <type_traits>

namespace HandMirrorReadOnlyObserver
{
	namespace
	{
		namespace Policy = HandMirrorReadOnlyProbePolicy;
		namespace Signature = FirstPersonViewCallSiteSignature;
		namespace FirstPersonChain = HandMirrorFirstPersonViewHookChainPolicy;
		namespace fs = std::filesystem;

		constexpr std::wstring_view kEnableMarker =
			L"RealisticReflections_HandMirrorReadOnlyProbe.enable";
		constexpr REL::Version kSupportedSE{ 1, 5, 97, 0 };
		constexpr REL::Version kSupportedAE{ 1, 6, 1170, 0 };
		constexpr REL::RelocationID kRenderPlayerView{ 35560, 36559 };
		// The VR address library maps 100411 to 0x13218C0, a dead never-called
		// copy; the live VR 1.4.15 Main::RenderFirstPersonView is 0x13244E0
		// (Ghidra 2026-09-04: sole caller of InitializeDescriptor/CullFromDescriptor/
		// RenderPreAndPostResolve, called from RenderPlayerView+0x7FA).
		constexpr REL::VariantID kRenderFirstPersonView{ 100411, 107129, 0x13244E0 };
		constexpr std::size_t kMaximumParentDepth = 256;
		constexpr std::size_t kPendingDrawRecordCapacity = 4;
		constexpr auto kDiagnosticPeriod = std::chrono::seconds{ 5 };
		constexpr std::size_t kDoubleReadStatusCount =
			static_cast<std::size_t>(
				Policy::RawDoubleReadStatus::kThirdPersonTransformChanged) + 1u;
		constexpr std::size_t kDrawJoinStatusCount =
			static_cast<std::size_t>(
				Policy::RawDrawJoinStatus::kTransformBitsChanged) + 1u;

		struct RawReadValues
		{
			Policy::RawShieldValueIdentity identity{};
			Policy::RawBipedTransformValues firstPersonTransforms{};
			Policy::RawBipedTransformValues thirdPersonTransforms{};
			bool actorIsExactPlayer{ false };
			bool bothBipedObjectsRead{ false };
			bool bothShieldTuplesRead{ false };
		};

		struct PendingDrawValueRecord
		{
			std::uint64_t mainViewEpoch{ 0 };
			std::uint64_t entrySequence{ 0 };
			Policy::RawShieldValueIdentity identity{};
			Policy::ProbeEpochs epochs{};
			Policy::RawBipedTransformValues transforms{};
			std::uint8_t phaseOrdinal{ 0 };
			Policy::BipedPath path{ Policy::BipedPath::kUnknown };
			bool valid{ false };
		};

		struct ThreadState
		{
			std::uint64_t lifecycleSerialSeen{ 0 };
			std::uint64_t equipWakeSerialSeen{ 0 };
			std::uint64_t mainViewEpoch{ 0 };
			std::uint64_t readSequence{ 0 };
			std::uint64_t drawSequence{ 0 };
			std::uint64_t genericDrawEntrySequence{ 0 };
			std::uint32_t firstPersonExactMatchesThisCall{ 0 };
			Policy::ProbeEpochs epochs{};
			Policy::RawShieldValueIdentity previousIdentity{};
			Policy::RawBipedTransformValues previousFirstPersonTransforms{};
			Policy::RawBipedTransformValues previousThirdPersonTransforms{};
			Policy::PhaseStamp currentStamp{};
			std::array<Policy::CoherentRawPhaseSample,
				static_cast<std::size_t>(Policy::MainViewPhase::kCount)> samples{};
			std::array<PendingDrawValueRecord, kPendingDrawRecordCapacity>
				pendingDrawValues{};
			Policy::EvidenceLedgerState ledger{};
			bool epochsInitialized{ false };
			bool previousRawValuesValid{ false };
			bool currentStampValid{ false };
			bool worldCallActive{ false };
			bool worldReturnedNormally{ false };
			bool firstPersonCallActive{ false };
			bool firstPersonReturnedNormally{ false };
		};

		static_assert(std::is_trivially_copyable_v<RawReadValues>);
		static_assert(std::is_trivially_copyable_v<PendingDrawValueRecord>);
		static_assert(std::is_trivially_copyable_v<ThreadState>);
		static_assert(std::is_trivially_copyable_v<GenericDrawToken>);
		static_assert(sizeof(GenericDrawToken) <= 24);

		struct Counters
		{
			std::atomic<std::uint64_t> prepareCalls{ 0 };
			std::atomic<std::uint64_t> completeCalls{ 0 };
			std::atomic<std::uint64_t> renderWorldOwnerRejects{ 0 };
			std::atomic<std::uint64_t> genericDrawOwnerRejects{ 0 };
			std::atomic<std::uint64_t> runtimeRejects{ 0 };
			std::atomic<std::uint64_t> signatureRejects{ 0 };
			std::atomic<std::uint64_t> nativeTargetRejects{ 0 };
			std::atomic<std::uint64_t> patchFailures{ 0 };
			std::atomic<std::uint64_t> postPatchRejects{ 0 };
			std::atomic<std::uint64_t> dataLoadedCalls{ 0 };
			std::atomic<std::uint64_t> equipWakeEvents{ 0 };
			std::atomic<std::uint64_t> gameLoadInvalidations{ 0 };
			std::atomic<std::uint64_t> mainViews{ 0 };
			std::atomic<std::uint64_t> phaseAttempts{ 0 };
			std::atomic<std::uint64_t> phaseSamples{ 0 };
			std::atomic<std::uint64_t> guardedReadFaults{ 0 };
			std::atomic<std::uint64_t> unavailableReads{ 0 };
			std::atomic<std::uint64_t> abnormalWorldReturns{ 0 };
			std::atomic<std::uint64_t> abnormalFirstPersonReturns{ 0 };
			std::atomic<std::uint64_t> exactFirstPersonDraws{ 0 };
			std::atomic<std::uint64_t> exactThirdPersonCandidates{ 0 };
			std::atomic<std::uint64_t> thirdPersonMainViewUnproven{ 0 };
			std::atomic<std::uint64_t> firstPersonBitExactJoins{ 0 };
			std::atomic<std::uint64_t> firstPersonTransformDrift{ 0 };
			std::atomic<std::uint64_t> firstPersonCallsWithZeroMatches{ 0 };
			std::atomic<std::uint64_t> firstPersonCallsWithOneMatch{ 0 };
			std::atomic<std::uint64_t> firstPersonCallsWithMultipleMatches{ 0 };
			std::atomic<std::uint64_t> ancestryFaults{ 0 };
			std::atomic<std::uint64_t> ledgerOverflows{ 0 };
			std::atomic<std::uint64_t> pendingDrawOverflows{ 0 };
			std::atomic<std::uint64_t> firstPersonPoseEpochDrift{ 0 };
			std::array<std::atomic<std::uint64_t>, kDoubleReadStatusCount>
				doubleReadStatuses{};
			std::array<std::atomic<std::uint64_t>, kDrawJoinStatusCount>
				drawJoinStatuses{};
		};

		Counters g_counters{};
		std::atomic_bool g_prepareAttempted{ false };
		std::atomic_bool g_completeAttempted{ false };
		std::atomic_bool g_requested{ false };
		std::atomic_bool g_renderWorldOwnerReady{ false };
		std::atomic_bool g_genericDrawOwnerReady{ false };
		std::atomic_bool g_hookInstalled{ false };
		std::atomic_bool g_hookValidated{ false };
		std::atomic_bool g_eventSinkInstalled{ false };
		std::atomic_bool g_menuEventSinkInstalled{ false };
		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_faultDiagnosticPending{ false };
		std::atomic<std::uint8_t> g_firstFault{
			static_cast<std::uint8_t>(Policy::ProbeFault::kNone) };
		std::atomic<std::uint64_t> g_lifecycleSerial{ 1 };
		std::atomic<std::uint64_t> g_equipWakeSerial{ 1 };
		std::atomic<std::uintptr_t> g_callSite{ 0 };
		std::atomic<std::uintptr_t> g_nativeTarget{ 0 };
		std::atomic<std::uintptr_t> g_preservedTarget{ 0 };
		std::atomic<std::uintptr_t> g_installedBranchTarget{ 0 };
		std::atomic<std::uint8_t> g_preservedChainKind{
			static_cast<std::uint8_t>(FirstPersonChain::ChainKind::kRejected) };
		std::atomic<std::int64_t> g_lastPeriodicLogMilliseconds{ 0 };
		std::atomic<DWORD> g_renderThreadID{ 0 };
		thread_local ThreadState g_thread{};

		[[nodiscard]] fs::path MarkerPath()
		{
			std::array<wchar_t, 32768> executablePath{};
			const DWORD length = GetModuleFileNameW(
				nullptr, executablePath.data(),
				static_cast<DWORD>(executablePath.size()));
			if (length == 0 || length >= executablePath.size())
				return fs::path{ L"Data" } / std::wstring{ kEnableMarker };
			return fs::path{
				std::wstring_view{ executablePath.data(), length } }.parent_path() /
				L"Data" / std::wstring{ kEnableMarker };
		}

		void LatchFault(const Policy::ProbeFault fault) noexcept
		{
			if (fault == Policy::ProbeFault::kNone)
				return;
			std::uint8_t expected =
				static_cast<std::uint8_t>(Policy::ProbeFault::kNone);
			const bool firstFault = g_firstFault.compare_exchange_strong(
				expected, static_cast<std::uint8_t>(fault),
				std::memory_order_acq_rel);
			g_faulted.store(true, std::memory_order_release);
			g_enabled.store(false, std::memory_order_release);
			if (firstFault) {
				// Render callbacks remain log-free.  Publish the urgent diagnostic
				// only after the fail-stop state above is globally visible.
				g_faultDiagnosticPending.store(true, std::memory_order_release);
			}
		}

		[[nodiscard]] bool RequireRenderThread(
			const bool captureIfUnset) noexcept
		{
			const DWORD current = GetCurrentThreadId();
			auto expected = g_renderThreadID.load(std::memory_order_acquire);
			auto status = Policy::ClassifyRenderThreadGate(
				expected, current, captureIfUnset);
			if (status == Policy::RenderThreadGateStatus::kCaptureUnset) {
				DWORD unset = 0;
				(void)g_renderThreadID.compare_exchange_strong(
					unset, current, std::memory_order_acq_rel);
				expected = g_renderThreadID.load(std::memory_order_acquire);
				status = Policy::ClassifyRenderThreadGate(
					expected, current, false);
			}
			if (status == Policy::RenderThreadGateStatus::kUnavailableUnset)
				return false;
			if (status == Policy::RenderThreadGateStatus::kAccept)
				return true;
			LatchFault(Policy::ProbeFault::kPhaseOrderFault);
			return false;
		}

		[[nodiscard]] bool AdvanceNonWrapping(
			std::atomic<std::uint64_t>& value) noexcept
		{
			auto current = value.load(std::memory_order_acquire);
			for (;;) {
				if (current == (std::numeric_limits<std::uint64_t>::max)()) {
					LatchFault(Policy::ProbeFault::kEpochExhausted);
					return false;
				}
				if (value.compare_exchange_weak(
						current, current + 1u, std::memory_order_acq_rel)) {
					return true;
				}
			}
		}

		[[nodiscard]] bool RecordLedger(
			const Policy::EvidenceRecordKind kind) noexcept
		{
			const auto transition = Policy::RecordEvidence(g_thread.ledger, kind);
			g_thread.ledger = transition.next;
			if (transition.status ==
				Policy::LedgerTransitionStatus::kOverflowFaultLatched) {
				g_counters.ledgerOverflows.fetch_add(1, std::memory_order_relaxed);
				LatchFault(g_thread.ledger.firstFault);
				return false;
			}
			return transition.status == Policy::LedgerTransitionStatus::kRecorded;
		}

		[[nodiscard]] bool RuntimeSignature(
			Signature::Runtime& runtime) noexcept
		{
			const auto version = REL::Module::get().version();
			if (REL::Module::IsSE() && version == kSupportedSE) {
				runtime = Signature::Runtime::kSE1597;
				return true;
			}
			// 1.7.104 moves the RenderFirstPersonView call to +0x971 and needs its
			// own 49-byte window, so it must not fall into the AE 1.6.1170 case.
			if (SupportedRuntimePolicy::IsExactAE17104Runtime()) {
				runtime = Signature::Runtime::kAE17104;
				return true;
			}
			if (SupportedRuntimePolicy::IsExactAE161179Runtime()) {
				runtime = Signature::Runtime::kAE161179;
				return true;
			}
			if (SupportedRuntimePolicy::IsExactAE161170Runtime()) {
				runtime = Signature::Runtime::kAE161170;
				return true;
			}
			if (SupportedRuntimePolicy::IsExactVRRuntime()) {
				runtime = Signature::Runtime::kVR1415;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool IsExecutableAddress(const void* address) noexcept
		{
			if (!address)
				return false;
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT ||
				(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}
			const DWORD protection = info.Protect & 0xFF;
			return protection == PAGE_EXECUTE ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool CopyCodeWindowSEH(
			const std::uintptr_t address,
			std::uint8_t* output,
			const std::size_t size) noexcept
		{
			if (!address || !output || size == 0)
				return false;
#if defined(_MSC_VER)
			__try {
#endif
				std::memcpy(output, reinterpret_cast<const void*>(address), size);
				return true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#endif
		}

		[[nodiscard]] bool ReadCallTargetSEH(
			const std::uintptr_t callSite,
			std::uintptr_t& target) noexcept
		{
			std::array<std::uint8_t, 5> instruction{};
			if (!CopyCodeWindowSEH(
					callSite, instruction.data(), instruction.size()) ||
				instruction[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(
				&displacement, instruction.data() + 1, sizeof(displacement));
			target = callSite + instruction.size() + displacement;
			return target != 0;
		}

		[[nodiscard]] bool ValidateFiveByteBranchStubSEH(
			const std::uintptr_t branchTarget,
			const std::uintptr_t expectedDestination) noexcept
		{
			if (!IsExecutableAddress(reinterpret_cast<const void*>(branchTarget)) ||
				!IsExecutableAddress(
					reinterpret_cast<const void*>(expectedDestination))) {
				return false;
			}
			std::array<std::uint8_t,
				MirrorPaneDeliverySignature::kFiveByteBranchStubSize> stub{};
			return CopyCodeWindowSEH(
					branchTarget, stub.data(), stub.size()) &&
				MirrorPaneDeliverySignature::MatchesFiveByteBranchStub(
					stub, static_cast<std::uint64_t>(expectedDestination));
		}

		[[nodiscard]] bool ReadCommunityShaders184FirstPersonEvidenceSEH(
			const std::uintptr_t inspectedTarget,
			const std::uintptr_t expectedNativeTarget,
			FirstPersonChain::Evidence& evidence,
			std::array<std::uint8_t,
				FirstPersonChain::kFiveByteCallBranchStubSize>& branchStub,
			std::array<std::uint8_t,
				FirstPersonChain::kThunkPrefix.size()>& thunkPrefix) noexcept
		{
			const auto module = GetModuleHandleW(L"CommunityShaders.dll");
			if (!module)
				return false;
			const auto moduleBase = reinterpret_cast<std::uintptr_t>(module);
			bool read = false;
#if defined(_MSC_VER)
			__try {
#endif
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
				if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
					dos->e_lfanew > 0x1000) {
#if defined(_MSC_VER)
					__leave;
#else
					return false;
#endif
				}
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
					moduleBase + static_cast<std::uintptr_t>(dos->e_lfanew));
				if (nt->Signature != IMAGE_NT_SIGNATURE ||
					nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
#if defined(_MSC_VER)
					__leave;
#else
					return false;
#endif
				}
				const FirstPersonChain::ModuleIdentity identity{
					.machine = nt->FileHeader.Machine,
					.sectionCount = nt->FileHeader.NumberOfSections,
					.optionalMagic = nt->OptionalHeader.Magic,
					.timeDateStamp = nt->FileHeader.TimeDateStamp,
					.sizeOfCode = nt->OptionalHeader.SizeOfCode,
					.sizeOfImage = nt->OptionalHeader.SizeOfImage
				};
				if (!FirstPersonChain::MatchesModuleIdentity(identity) ||
					moduleBase > (std::numeric_limits<std::uintptr_t>::max)() -
						FirstPersonChain::kFirstPersonThunkRVA ||
					moduleBase > (std::numeric_limits<std::uintptr_t>::max)() -
						FirstPersonChain::kOriginalFirstPersonSlotRVA) {
#if defined(_MSC_VER)
					__leave;
#else
					return false;
#endif
				}
				const auto thunk =
					moduleBase + FirstPersonChain::kFirstPersonThunkRVA;
				const auto originalSlot =
					moduleBase + FirstPersonChain::kOriginalFirstPersonSlotRVA;
				if (!IsExecutableAddress(reinterpret_cast<const void*>(thunk))) {
#if defined(_MSC_VER)
					__leave;
#else
					return false;
#endif
				}
				std::memcpy(
					thunkPrefix.data(), reinterpret_cast<const void*>(thunk),
					thunkPrefix.size());
				if (inspectedTarget != thunk) {
					if (!IsExecutableAddress(
							reinterpret_cast<const void*>(inspectedTarget))) {
#if defined(_MSC_VER)
						__leave;
#else
						return false;
#endif
					}
					std::memcpy(
						branchStub.data(),
						reinterpret_cast<const void*>(inspectedTarget),
						branchStub.size());
				}
				const auto wrapperOriginal =
					*reinterpret_cast<const std::uintptr_t*>(originalSlot);
				evidence = {
					.expectedNativeTarget = expectedNativeTarget,
					.inspectedCallTarget = inspectedTarget,
					.communityShadersBase = moduleBase,
					.module = identity,
					.inspectedBranchStub = inspectedTarget == thunk ?
						std::span<const std::uint8_t>{} :
						std::span<const std::uint8_t>{ branchStub },
					.thunkPrefix = std::span<const std::uint8_t>{ thunkPrefix },
					.wrapperOriginalTarget = wrapperOriginal
				};
				read = true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				read = false;
			}
#endif
			return read;
		}

		[[nodiscard]] FirstPersonChain::ChainKind ClassifyFirstPersonChainTarget(
			const std::uintptr_t inspectedTarget,
			const std::uintptr_t expectedNativeTarget) noexcept
		{
			if (inspectedTarget == expectedNativeTarget)
				return FirstPersonChain::ChainKind::kNative;
			std::array<std::uint8_t,
				FirstPersonChain::kFiveByteCallBranchStubSize> branchStub{};
			std::array<std::uint8_t,
				FirstPersonChain::kThunkPrefix.size()> thunkPrefix{};
			FirstPersonChain::Evidence evidence{};
			if (!ReadCommunityShaders184FirstPersonEvidenceSEH(
					inspectedTarget, expectedNativeTarget, evidence,
					branchStub, thunkPrefix)) {
				return FirstPersonChain::ChainKind::kRejected;
			}
			return FirstPersonChain::Classify(evidence);
		}

		[[nodiscard]] Policy::RawTransformSnapshot CopyTransform(
			const RE::NiAVObject* object)
		{
			Policy::RawTransformWords words{};
			static_assert(sizeof(RE::NiTransform) ==
				Policy::kRawTransformWordCount * sizeof(std::uint32_t));
			std::memcpy(words.data(), std::addressof(object->world), sizeof(object->world));
			return Policy::MakeRawTransformSnapshot(words);
		}

		[[nodiscard]] bool ReadRawValuesUnsafe(RawReadValues& output)
		{
			output = {};
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return false;
			const auto& firstBipedOwner = player->GetBiped(true);
			const auto& thirdBipedOwner = player->GetBiped(false);
			auto* const firstBiped = firstBipedOwner.get();
			auto* const thirdBiped = thirdBipedOwner.get();
			if (!firstBiped || !thirdBiped || firstBiped == thirdBiped)
				return false;

			auto* const firstShield = firstBiped->GetShieldObject();
			auto* const thirdShield = thirdBiped->GetShieldObject();
			auto* const firstRoot = firstBiped->root;
			auto* const thirdRoot = thirdBiped->root;
			auto* const firstClone = firstShield ? firstShield->partClone.get() : nullptr;
			auto* const thirdClone = thirdShield ? thirdShield->partClone.get() : nullptr;
			if (!firstShield || !thirdShield || !firstRoot || !thirdRoot ||
				!firstClone || !thirdClone) {
				return false;
			}

			const auto firstHandle = firstBiped->actorRef.native_handle();
			const auto thirdHandle = thirdBiped->actorRef.native_handle();
			if (firstHandle == 0 || firstHandle != thirdHandle)
				return false;
			output.identity = {
				.playerFormID = player->GetFormID(),
				.playerHandleToken = firstHandle,
				.playerActorAddressValue =
					reinterpret_cast<std::uintptr_t>(player),
				.firstPersonBipedAddressValue =
					reinterpret_cast<std::uintptr_t>(firstBiped),
				.thirdPersonBipedAddressValue =
					reinterpret_cast<std::uintptr_t>(thirdBiped),
				.firstPersonRootAddressValue =
					reinterpret_cast<std::uintptr_t>(firstRoot),
				.thirdPersonRootAddressValue =
					reinterpret_cast<std::uintptr_t>(thirdRoot),
				.firstPersonShield = {
					reinterpret_cast<std::uintptr_t>(firstShield->item),
					reinterpret_cast<std::uintptr_t>(firstShield->addon),
					reinterpret_cast<std::uintptr_t>(firstClone) },
				.thirdPersonShield = {
					reinterpret_cast<std::uintptr_t>(thirdShield->item),
					reinterpret_cast<std::uintptr_t>(thirdShield->addon),
					reinterpret_cast<std::uintptr_t>(thirdClone) }
			};
			output.firstPersonTransforms = {
				.rootWorld = CopyTransform(firstRoot),
				.partCloneWorld = CopyTransform(firstClone)
			};
			output.thirdPersonTransforms = {
				.rootWorld = CopyTransform(thirdRoot),
				.partCloneWorld = CopyTransform(thirdClone)
			};
			output.actorIsExactPlayer = output.identity.playerFormID == 0x14;
			output.bothBipedObjectsRead = true;
			output.bothShieldTuplesRead = true;
			return Policy::IsValidRawShieldValueIdentity(output.identity) &&
				Policy::IsSelfConsistentRawBipedTransforms(
					output.firstPersonTransforms) &&
				Policy::IsSelfConsistentRawBipedTransforms(
					output.thirdPersonTransforms);
		}

		[[nodiscard]] bool ReadRawValuesGuarded(RawReadValues& output) noexcept
		{
			output = {};
#if defined(_MSC_VER)
			__try {
#endif
				return ReadRawValuesUnsafe(output);
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				g_counters.guardedReadFaults.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(Policy::ProbeFault::kGuardedReadFault);
				return false;
			}
#endif
		}

		[[nodiscard]] bool ReadRawValuesPairGuarded(
			RawReadValues& first,
			RawReadValues& second) noexcept
		{
			first = {};
			second = {};
#if defined(_MSC_VER)
			__try {
#endif
				// These are deliberately adjacent full reads.  No epoch, hash, log,
				// allocation, or engine-pointer retention occurs between them.
				if (!ReadRawValuesUnsafe(first))
					return false;
				return ReadRawValuesUnsafe(second);
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				first = {};
				second = {};
				g_counters.guardedReadFaults.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(Policy::ProbeFault::kGuardedReadFault);
				return false;
			}
#endif
		}

		[[nodiscard]] bool SameRawReadValues(
			const RawReadValues& left,
			const RawReadValues& right) noexcept
		{
			return left.identity == right.identity &&
				Policy::SameRawBipedTransforms(
					left.firstPersonTransforms, right.firstPersonTransforms) &&
				Policy::SameRawBipedTransforms(
					left.thirdPersonTransforms, right.thirdPersonTransforms) &&
				left.actorIsExactPlayer == right.actorIsExactPlayer &&
				left.bothBipedObjectsRead == right.bothBipedObjectsRead &&
				left.bothShieldTuplesRead == right.bothShieldTuplesRead;
		}

		void RememberRawValues(const RawReadValues& values) noexcept
		{
			g_thread.previousIdentity = values.identity;
			g_thread.previousFirstPersonTransforms =
				values.firstPersonTransforms;
			g_thread.previousThirdPersonTransforms =
				values.thirdPersonTransforms;
			g_thread.previousRawValuesValid = true;
		}

		[[nodiscard]] bool SameRootIdentity(
			const Policy::RawShieldValueIdentity& left,
			const Policy::RawShieldValueIdentity& right) noexcept
		{
			return left.firstPersonBipedAddressValue ==
					right.firstPersonBipedAddressValue &&
				left.thirdPersonBipedAddressValue ==
					right.thirdPersonBipedAddressValue &&
				left.firstPersonRootAddressValue ==
					right.firstPersonRootAddressValue &&
				left.thirdPersonRootAddressValue ==
					right.thirdPersonRootAddressValue;
		}

		[[nodiscard]] bool SameCloneIdentity(
			const Policy::RawShieldValueIdentity& left,
			const Policy::RawShieldValueIdentity& right) noexcept
		{
			return left.firstPersonShield == right.firstPersonShield &&
				left.thirdPersonShield == right.thirdPersonShield;
		}

		[[nodiscard]] bool SamePlayerIdentity(
			const Policy::RawShieldValueIdentity& left,
			const Policy::RawShieldValueIdentity& right) noexcept
		{
			return left.playerFormID == right.playerFormID &&
				left.playerHandleToken == right.playerHandleToken &&
				left.playerActorAddressValue == right.playerActorAddressValue;
		}

		[[nodiscard]] bool AdvanceThreadEpoch(
			const Policy::EpochEvent event) noexcept
		{
			const auto result = Policy::AdvanceEpochs(g_thread.epochs, event);
			if (!result.Succeeded()) {
				LatchFault(Policy::ProbeFault::kEpochExhausted);
				return false;
			}
			g_thread.epochs = result.next;
			return true;
		}

		[[nodiscard]] bool SynchronizeEpochs(
			const RawReadValues& values) noexcept
		{
			const auto lifecycle = g_lifecycleSerial.load(std::memory_order_acquire);
			const auto equipWake = g_equipWakeSerial.load(std::memory_order_acquire);
			if (!g_thread.epochsInitialized) {
				g_thread.epochs = Policy::InitialEpochs();
				g_thread.lifecycleSerialSeen = lifecycle;
				g_thread.equipWakeSerialSeen = equipWake;
				g_thread.epochsInitialized = true;
			} else {
				if (lifecycle != g_thread.lifecycleSerialSeen) {
					if (!AdvanceThreadEpoch(Policy::EpochEvent::kGameLoadInvalidation))
						return false;
					g_thread.lifecycleSerialSeen = lifecycle;
					g_thread.previousRawValuesValid = false;
				}
				if (equipWake != g_thread.equipWakeSerialSeen) {
					if (!AdvanceThreadEpoch(
							Policy::EpochEvent::kEquipLifecycleChanged)) {
						return false;
					}
					g_thread.equipWakeSerialSeen = equipWake;
				}
			}

			if (!g_thread.previousRawValuesValid)
				return true;
			if (!SamePlayerIdentity(g_thread.previousIdentity, values.identity)) {
				LatchFault(Policy::ProbeFault::kMalformedEvidence);
				return false;
			}
			if (!SameRootIdentity(g_thread.previousIdentity, values.identity) &&
				!AdvanceThreadEpoch(
					Policy::EpochEvent::kPerspectiveOrBipedRootChanged)) {
				return false;
			}
			if (!SameCloneIdentity(g_thread.previousIdentity, values.identity) &&
				!AdvanceThreadEpoch(
					Policy::EpochEvent::kPartCloneOrRaceModelChanged)) {
				return false;
			}
			if ((!Policy::SameRawBipedTransforms(
					g_thread.previousFirstPersonTransforms,
					values.firstPersonTransforms) ||
				!Policy::SameRawBipedTransforms(
					g_thread.previousThirdPersonTransforms,
					values.thirdPersonTransforms)) &&
				!AdvanceThreadEpoch(Policy::EpochEvent::kPoseObserved)) {
				return false;
			}
			return true;
		}

		[[nodiscard]] Policy::PhaseStamp Stamp(
			const Policy::MainViewPhase phase) noexcept
		{
			return {
				.mainViewEpoch = g_thread.mainViewEpoch,
				.phase = phase,
				.phaseSequence = static_cast<std::uint8_t>(
					Policy::PhaseOrdinal(phase) + 1u)
			};
		}

		[[nodiscard]] bool AdvanceReadSequence(
			std::uint64_t& first,
			std::uint64_t& second) noexcept
		{
			if (g_thread.readSequence >
					(std::numeric_limits<std::uint64_t>::max)() - 2u) {
				LatchFault(Policy::ProbeFault::kEpochExhausted);
				return false;
			}
			first = ++g_thread.readSequence;
			second = ++g_thread.readSequence;
			return true;
		}

		[[nodiscard]] bool CapturePhaseSample(
			const Policy::MainViewPhase phase) noexcept
		{
			g_counters.phaseAttempts.fetch_add(1, std::memory_order_relaxed);
			if (!RecordLedger(Policy::EvidenceRecordKind::kPhaseRead) ||
				!RecordLedger(Policy::EvidenceRecordKind::kPhaseRead)) {
				return false;
			}

			RawReadValues firstValues{};
			RawReadValues secondValues{};
			const bool pairRead =
				ReadRawValuesPairGuarded(firstValues, secondValues);
			if (g_faulted.load(std::memory_order_acquire))
				return false;
			if (!pairRead || !SynchronizeEpochs(firstValues)) {
				g_counters.unavailableReads.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			std::uint64_t firstSequence = 0;
			std::uint64_t secondSequence = 0;
			if (!AdvanceReadSequence(firstSequence, secondSequence))
				return false;
			const auto stamp = Stamp(phase);
			Policy::RawPhaseReadObservation first{
				.readSequence = firstSequence,
				.stamp = stamp,
				.identity = firstValues.identity,
				.epochs = g_thread.epochs,
				.firstPersonTransforms = firstValues.firstPersonTransforms,
				.thirdPersonTransforms = firstValues.thirdPersonTransforms,
				.actorIsExactPlayer = firstValues.actorIsExactPlayer,
				.bothBipedObjectsRead = firstValues.bothBipedObjectsRead,
				.bothShieldTuplesRead = firstValues.bothShieldTuplesRead,
				.guardedReadCompleted = true,
				.guardedReadFaulted = false,
				.noEnginePointerRetained = true
			};

			Policy::RawPhaseReadObservation second{
				.readSequence = secondSequence,
				.stamp = stamp,
				.identity = secondValues.identity,
				.epochs = g_thread.epochs,
				.firstPersonTransforms = secondValues.firstPersonTransforms,
				.thirdPersonTransforms = secondValues.thirdPersonTransforms,
				.actorIsExactPlayer = secondValues.actorIsExactPlayer,
				.bothBipedObjectsRead = secondValues.bothBipedObjectsRead,
				.bothShieldTuplesRead = secondValues.bothShieldTuplesRead,
				.guardedReadCompleted = true,
				.guardedReadFaulted =
					g_faulted.load(std::memory_order_acquire),
				.noEnginePointerRetained = true
			};
			const auto coherent = Policy::AdmitCoherentRawDoubleRead(first, second);
			const auto statusIndex = static_cast<std::size_t>(coherent.status);
			if (statusIndex < g_counters.doubleReadStatuses.size()) {
				g_counters.doubleReadStatuses[statusIndex].fetch_add(
					1, std::memory_order_relaxed);
			}
			if (!coherent.AdmittedDiagnosticOnly())
				return false;

			g_thread.samples[static_cast<std::size_t>(phase)] = coherent;
			RememberRawValues(secondValues);
			g_counters.phaseSamples.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] bool TransitionAndSample(
			const Policy::MainViewPhase phase,
			const bool newMainView) noexcept
		{
			if (!IsEnabled())
				return false;
			if (newMainView) {
				if (g_thread.currentStampValid &&
					g_thread.currentStamp.phase !=
						Policy::MainViewPhase::kPostFirstPerson &&
					g_thread.currentStamp.phase !=
						Policy::MainViewPhase::kPostWorld) {
					LatchFault(Policy::ProbeFault::kPhaseOrderFault);
					return false;
				}
				if (g_thread.mainViewEpoch ==
						(std::numeric_limits<std::uint64_t>::max)()) {
					LatchFault(Policy::ProbeFault::kEpochExhausted);
					return false;
				}
				++g_thread.mainViewEpoch;
				g_thread.ledger = {};
				g_thread.samples = {};
				g_thread.pendingDrawValues = {};
				g_thread.firstPersonExactMatchesThisCall = 0;
				g_counters.mainViews.fetch_add(1, std::memory_order_relaxed);
			} else {
				if (!g_thread.currentStampValid) {
					LatchFault(Policy::ProbeFault::kPhaseOrderFault);
					return false;
				}
				const auto next = Stamp(phase);
				if (Policy::ClassifyPhaseTransition(
						g_thread.currentStamp, next) !=
					Policy::PhaseTransitionStatus::kValidSameMainView) {
					LatchFault(Policy::ProbeFault::kPhaseOrderFault);
					return false;
				}
			}
			g_thread.currentStamp = Stamp(phase);
			g_thread.currentStampValid = true;
			return CapturePhaseSample(phase);
		}

		[[nodiscard]] bool ResolvePassToCloneUnsafe(
			RE::BSRenderPass* pass,
			const std::uintptr_t cloneAddress,
			bool& malformed)
		{
			malformed = false;
			if (!pass || !pass->geometry || cloneAddress == 0)
				return false;
			auto* current = static_cast<RE::NiAVObject*>(pass->geometry);
			for (std::size_t depth = 0; depth < kMaximumParentDepth; ++depth) {
				if (reinterpret_cast<std::uintptr_t>(current) == cloneAddress)
					return true;
				auto* const parent = current->parent;
				if (!parent)
					return false;
				if (parent == current) {
					malformed = true;
					return false;
				}
				current = parent;
			}
			malformed = true;
			return false;
		}

		[[nodiscard]] bool ResolvePassToCloneGuarded(
			RE::BSRenderPass* pass,
			const std::uintptr_t cloneAddress,
			bool& malformed) noexcept
		{
			malformed = false;
#if defined(_MSC_VER)
			__try {
#endif
				return ResolvePassToCloneUnsafe(pass, cloneAddress, malformed);
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				malformed = true;
				return false;
			}
#endif
		}

		[[nodiscard]] const Policy::CoherentRawPhaseSample* DrawJoinSample(
			const Policy::BipedPath path) noexcept
		{
			if (path == Policy::BipedPath::kThirdPerson) {
				const auto& sample = g_thread.samples[static_cast<std::size_t>(
					Policy::MainViewPhase::kPreWorld)];
				return sample.AdmittedDiagnosticOnly() ? &sample : nullptr;
			}
			if (path == Policy::BipedPath::kFirstPerson) {
				const auto& postWorld = g_thread.samples[static_cast<std::size_t>(
					Policy::MainViewPhase::kPostWorld)];
				return postWorld.AdmittedDiagnosticOnly() ? &postWorld : nullptr;
			}
			return nullptr;
		}

		class EquipEventSink final : public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			EquipEventSink() = default;

			static EquipEventSink& GetSingleton() noexcept
			{
				static stl::no_destructor<EquipEventSink> sink{};
				return sink.get();
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESEquipEvent* event,
				RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (!event)
					return RE::BSEventNotifyControl::kContinue;
				auto* const actor = event->actor.get();
				if (actor && actor == RE::PlayerCharacter::GetSingleton()) {
					HandMirrorApprovedContentReadOnlyObserver::OnPlayerEquipWake();
					HandMirrorReflectionRuntime::OnPlayerEquipWake();
					if (g_enabled.load(std::memory_order_acquire)) {
						g_counters.equipWakeEvents.fetch_add(
							1, std::memory_order_relaxed);
						(void)AdvanceNonWrapping(g_equipWakeSerial);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class InventoryMenuEventSink final :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static InventoryMenuEventSink& GetSingleton() noexcept
			{
				static stl::no_destructor<InventoryMenuEventSink> sink{};
				return sink.get();
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (event && event->menuName == RE::InventoryMenu::MENU_NAME) {
					HandMirrorReflectionRuntime::OnInventoryMenuOpenChanged(
						event->opening);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		struct FirstPersonCallerHook
		{
			using Function = void(std::uint8_t, std::uint8_t);

			__declspec(noinline) static void RunBeforeNativeGuarded() noexcept
			{
				__try {
					OnPreFirstPerson();
					OnEnterFirstPerson();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					// A diagnostic callback may never suppress the native call.
					LatchFault(Policy::ProbeFault::kDrawCallbackFault);
				}
				__try {
					HandMirrorApprovedContentReadOnlyObserver::OnFirstPersonEnter();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorApprovedContentReadOnlyObserver::
						FailStopCallbackFault();
				}
				__try {
					HandMirrorReflectionRuntime::OnFirstPersonEnter();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorReflectionRuntime::FailStopCallbackFault();
				}
			}

			__declspec(noinline) static void RunReturnedGuarded() noexcept
			{
				__try {
					OnFirstPersonReturnedNormally();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					LatchFault(Policy::ProbeFault::kDrawCallbackFault);
				}
				__try {
					HandMirrorApprovedContentReadOnlyObserver::
						OnFirstPersonReturnedNormally();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorApprovedContentReadOnlyObserver::
						FailStopCallbackFault();
				}
				__try {
					HandMirrorReflectionRuntime::OnFirstPersonReturnedNormally();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorReflectionRuntime::FailStopCallbackFault();
				}
			}

			__declspec(noinline) static void RunFinallyGuarded() noexcept
			{
				__try {
					OnFirstPersonFinally();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					// Clear the POD TLS even if secondary fault accounting failed.
					g_thread.firstPersonCallActive = false;
					g_thread.firstPersonReturnedNormally = false;
					LatchFault(Policy::ProbeFault::kDrawCallbackFault);
				}
				__try {
					HandMirrorApprovedContentReadOnlyObserver::
						OnFirstPersonFinally();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorApprovedContentReadOnlyObserver::
						FailStopCallbackFault();
				}
				__try {
					HandMirrorReflectionRuntime::OnFirstPersonFinally();
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					HandMirrorReflectionRuntime::FailStopCallbackFault();
				}
			}

			__declspec(noinline) static void CallNativeWithObserverFinally(
				const std::uint8_t paused,
				const std::uint8_t controlsDriven)
			{
				if (REL::Module::IsVR()) {
					// VR: the exact main-view scope runs around RenderWorld
					// (VRWorldScope*); this call site only forwards.
					func(paused, controlsDriven);
					return;
				}
				RunBeforeNativeGuarded();
#if defined(_MSC_VER)
				__try {
					func(paused, controlsDriven);
					RunReturnedGuarded();
				} __finally {
					// Clear the POD active-call state before any secondary work.
					RunFinallyGuarded();
				}
#else
				func(paused, controlsDriven);
				RunReturnedGuarded();
				RunFinallyGuarded();
#endif
			}

			static void thunk(
				const std::uint8_t paused,
				const std::uint8_t controlsDriven)
			{
				CallNativeWithObserverFinally(paused, controlsDriven);
			}

			static inline REL::Relocation<Function> func;
		};

		[[nodiscard]] bool InstallFirstPersonHook(
			const Signature::Runtime runtime) noexcept
		{
			// Runtime was proven before either relocation is resolved.
			const auto caller =
				REL::Relocation<std::uintptr_t>{ kRenderPlayerView }.address();
			const auto nativeTarget =
				REL::Relocation<std::uintptr_t>{ kRenderFirstPersonView }.address();
			const auto windowAddress =
				caller + Signature::WindowOffsetFromCaller(runtime);
			const auto callSite =
				windowAddress + Signature::kCallOffsetInWindow;
			std::array<std::uint8_t, Signature::kWindowSize> window{};
			if (!CopyCodeWindowSEH(
					windowAddress, window.data(), window.size()) ||
				!Signature::MatchesMasked(window, runtime)) {
				g_counters.signatureRejects.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			std::uintptr_t decodedTarget = 0;
			if (!Signature::DecodeRel32Target(
					windowAddress, window, decodedTarget) ||
				!IsExecutableAddress(reinterpret_cast<const void*>(nativeTarget))) {
				g_counters.nativeTargetRejects.fetch_add(
					1, std::memory_order_relaxed);
				return false;
			}
			const auto chainKind = ClassifyFirstPersonChainTarget(
				decodedTarget, nativeTarget);
			if (chainKind == FirstPersonChain::ChainKind::kRejected) {
				g_counters.nativeTargetRejects.fetch_add(
					1, std::memory_order_relaxed);
				try {
					logger::critical(
						"[RR][HandMirrorRawObserver] refused first-person hook: call target is not native or the exact verified Community Shaders v1.8.4 chain at 0x{:X} target=0x{:X} native=0x{:X}",
						callSite, decodedTarget, nativeTarget);
				} catch (...) {
				}
				return false;
			}

			// Seed the complete inspected chain before the call site becomes live.
			// For stock CS this is its exact wrapper/stub, whose own preserved slot
			// was independently proven to contain the native target.
			FirstPersonCallerHook::func = decodedTarget;
			std::uintptr_t chainedTarget = 0;
			try {
				chainedTarget = SKSE::GetTrampoline().write_call<5>(
					callSite, FirstPersonCallerHook::thunk);
			} catch (...) {
				g_counters.patchFailures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			// The patch is live. Record ownership before every subsequent check.
			g_callSite.store(callSite, std::memory_order_release);
			g_nativeTarget.store(nativeTarget, std::memory_order_release);
			g_preservedTarget.store(decodedTarget, std::memory_order_release);
			g_preservedChainKind.store(
				static_cast<std::uint8_t>(chainKind), std::memory_order_release);
			g_hookInstalled.store(true, std::memory_order_release);
			// If a peer raced inspection, preserve the live returned chain even
			// though exact ownership validation below will fail closed.
			if (chainedTarget != 0 &&
				IsExecutableAddress(reinterpret_cast<const void*>(chainedTarget))) {
				FirstPersonCallerHook::func = chainedTarget;
			}

			std::uintptr_t installedTarget = 0;
			const auto thunkAddress =
				std::bit_cast<std::uintptr_t>(&FirstPersonCallerHook::thunk);
			const bool valid = chainedTarget == decodedTarget &&
				ReadCallTargetSEH(callSite, installedTarget) &&
				installedTarget != decodedTarget &&
				ValidateFiveByteBranchStubSEH(installedTarget, thunkAddress) &&
				FirstPersonCallerHook::func.address() == decodedTarget;
			if (!valid) {
				g_counters.postPatchRejects.fetch_add(1, std::memory_order_relaxed);
				LatchFault(Policy::ProbeFault::kMalformedEvidence);
				return false;
			}
			g_installedBranchTarget.store(installedTarget, std::memory_order_release);
			g_hookValidated.store(true, std::memory_order_release);
			try {
				logger::info(
					"[RR][HandMirrorRawObserver] first-person caller preservedChain={}",
					FirstPersonChain::ToString(chainKind));
			} catch (...) {
			}
			return true;
		}

		[[nodiscard]] bool FirstPersonHookStillOutermost() noexcept
		{
			Signature::Runtime runtime{};
			if (!RuntimeSignature(runtime))
				return false;

			// Exact runtime was proven before resolving either relocation.
			const auto caller =
				REL::Relocation<std::uintptr_t>{ kRenderPlayerView }.address();
			const auto exactNative =
				REL::Relocation<std::uintptr_t>{ kRenderFirstPersonView }.address();
			const auto windowAddress =
				caller + Signature::WindowOffsetFromCaller(runtime);
			const auto exactCallSite =
				windowAddress + Signature::kCallOffsetInWindow;
			std::array<std::uint8_t, Signature::kWindowSize> window{};
			std::uintptr_t currentBranchTarget = 0;
			const auto storedCallSite =
				g_callSite.load(std::memory_order_acquire);
			const auto storedNative =
				g_nativeTarget.load(std::memory_order_acquire);
			const auto storedPreserved =
				g_preservedTarget.load(std::memory_order_acquire);
			const auto storedBranch =
				g_installedBranchTarget.load(std::memory_order_acquire);
			const auto storedChainKind = static_cast<FirstPersonChain::ChainKind>(
				g_preservedChainKind.load(std::memory_order_acquire));
			const auto thunkAddress =
				std::bit_cast<std::uintptr_t>(&FirstPersonCallerHook::thunk);
			const auto currentChainKind = ClassifyFirstPersonChainTarget(
				storedPreserved, exactNative);
			return storedCallSite != 0 && storedCallSite == exactCallSite &&
				storedNative != 0 && storedNative == exactNative &&
				storedPreserved != 0 &&
				currentChainKind != FirstPersonChain::ChainKind::kRejected &&
				currentChainKind == storedChainKind &&
				storedBranch != 0 &&
				CopyCodeWindowSEH(
					windowAddress, window.data(), window.size()) &&
				Signature::MatchesMasked(window, runtime) &&
				ReadCallTargetSEH(storedCallSite, currentBranchTarget) &&
				currentBranchTarget == storedBranch &&
				ValidateFiveByteBranchStubSEH(storedBranch, thunkAddress) &&
				FirstPersonCallerHook::func.address() == storedPreserved;
		}
	}

	void PrepareAtInputLoaded() noexcept
	{
		g_counters.prepareCalls.fetch_add(1, std::memory_order_relaxed);
		if (g_prepareAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		std::error_code error{};
		const bool legacyRequested = fs::exists(MarkerPath(), error) && !error;
		const bool approvedRequested =
			HandMirrorApprovedContentReadOnlyObserver::Requested();
		const bool requested =
			HandMirrorApprovedContentReadOnlyObserver::SharedRawHookRequested(
				legacyRequested, approvedRequested);
		g_requested.store(requested, std::memory_order_release);
		try {
			if (error) {
				logger::warn(
					"[RR][HandMirrorRawObserver] marker inspection failed; observer remains off: {}",
					error.message());
			} else if (requested) {
				logger::info(
					"[RR][HandMirrorRawObserver] read-only vanilla-shield observer requested (legacyMarker={} approvedContentDependency={}); the one existing first-person hook remains non-authorizing and no reflection path is enabled",
					legacyRequested, approvedRequested);
			}
		} catch (...) {
		}
	}

	bool Requested() noexcept
	{
		return g_requested.load(std::memory_order_acquire);
	}

	void CompleteInputLoaded(const bool renderWorldOwnerReady) noexcept
	{
		if (!Requested())
			return;
		g_counters.completeCalls.fetch_add(1, std::memory_order_relaxed);
		if (g_completeAttempted.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		g_renderWorldOwnerReady.store(
			renderWorldOwnerReady, std::memory_order_release);
		if (!renderWorldOwnerReady) {
			g_counters.renderWorldOwnerRejects.fetch_add(
				1, std::memory_order_relaxed);
			try {
				logger::critical(
					"[RR][HandMirrorRawObserver] refused: the existing RenderWorld owner is not installed/ready; no hand-only second-view hooks were installed");
			} catch (...) {
			}
			return;
		}

		Signature::Runtime runtime{};
		if (!RuntimeSignature(runtime)) {
			g_counters.runtimeRejects.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const bool genericReady = GeometryDrawObserver::EnsureInstalled();
		g_genericDrawOwnerReady.store(genericReady, std::memory_order_release);
		if (!genericReady) {
			g_counters.genericDrawOwnerRejects.fetch_add(
				1, std::memory_order_relaxed);
			return;
		}

		if (!InstallFirstPersonHook(runtime))
			return;

		try {
			logger::info(
				"[RR][HandMirrorRawObserver] dormant exact SE/AE first-person caller patch installed; activation waits for DataLoaded");
		} catch (...) {
		}
	}

	void OnDataLoaded() noexcept
	{
		if (!Requested())
			return;
		g_counters.dataLoadedCalls.fetch_add(1, std::memory_order_relaxed);
		g_enabled.store(false, std::memory_order_release);
		if (g_faulted.load(std::memory_order_acquire))
			return;

		const bool renderWorldReady =
			SecondView::RenderWorldDriverReadyForReadOnlyObserver();
		const bool genericReady = GeometryDrawObserver::IsInstalled();
		const bool firstPersonReady = FirstPersonHookStillOutermost();
		g_renderWorldOwnerReady.store(
			renderWorldReady, std::memory_order_release);
		g_genericDrawOwnerReady.store(genericReady, std::memory_order_release);
		g_hookValidated.store(firstPersonReady, std::memory_order_release);
		if (!renderWorldReady || !genericReady || !firstPersonReady ||
			!g_hookInstalled.load(std::memory_order_acquire) ||
			!g_hookValidated.load(std::memory_order_acquire)) {
			LatchFault(Policy::ProbeFault::kMalformedEvidence);
			try {
				logger::critical(
					"[RR][HandMirrorRawObserver] refused at DataLoaded: existing RenderWorld owner, generic-draw owner, or exact first-person branch was not still validated/outermost");
			} catch (...) {
			}
			return;
		}

		if (!g_eventSinkInstalled.load(std::memory_order_acquire)) {
			auto* const source = RE::ScriptEventSourceHolder::GetSingleton();
			if (!source)
				return;
			source->AddEventSink<RE::TESEquipEvent>(&EquipEventSink::GetSingleton());
			g_eventSinkInstalled.store(true, std::memory_order_release);
		}
		if (!g_menuEventSinkInstalled.load(std::memory_order_acquire)) {
			auto* const ui = RE::UI::GetSingleton();
			if (!ui)
				return;
			ui->AddEventSink<RE::MenuOpenCloseEvent>(
				&InventoryMenuEventSink::GetSingleton());
			g_menuEventSinkInstalled.store(true, std::memory_order_release);
		}

		const Policy::FutureRuntimeActivationEvidence evidence{
			.explicitOptInMarkerPresent = true,
			.exactRuntimePinned = true,
			.signaturesVerified = true,
			.firstPersonCallbackABIClosed = true,
			.reusesExistingHookOwners = true,
			.ownsOnlyValidatedFirstPersonCallPatch = true,
			.readOnlyValueCopiesOnly = true,
			.noEnginePointerRetention = true,
			.faultAndOverflowFailStopInstalled = true
		};
		g_enabled.store(
			Policy::CanActivateRuntimeObserver(evidence),
			std::memory_order_release);
		try {
			logger::info(
				"[RR][HandMirrorRawObserver] read-only observer armed; raw shield timing evidence remains non-authorizing and no reflection/content path was enabled");
		} catch (...) {
		}
	}

	void OnGameLoaded() noexcept
	{
		if (!Requested())
			return;
		g_counters.gameLoadInvalidations.fetch_add(1, std::memory_order_relaxed);
		(void)AdvanceNonWrapping(g_lifecycleSerial);
	}

	void OnPreWorld() noexcept
	{
		if (!IsEnabled() || !RequireRenderThread(true))
			return;
		if (g_thread.epochsInitialized &&
			g_thread.lifecycleSerialSeen !=
				g_lifecycleSerial.load(std::memory_order_acquire)) {
			// A save/new-game transition invalidates phase-local evidence even
			// when the optional first-person route was skipped in the old frame.
			g_thread.currentStampValid = false;
			g_thread.previousRawValuesValid = false;
			g_thread.samples = {};
			g_thread.pendingDrawValues = {};
			g_thread.ledger = {};
		}
		if (g_thread.worldCallActive || g_thread.firstPersonCallActive) {
			LatchFault(Policy::ProbeFault::kPhaseOrderFault);
			return;
		}
		g_thread.worldCallActive = true;
		g_thread.worldReturnedNormally = false;
		(void)TransitionAndSample(Policy::MainViewPhase::kPreWorld, true);
	}

	void OnEnterWorld() noexcept
	{
		if (!IsEnabled() || !RequireRenderThread(false) ||
			!g_thread.worldCallActive)
			return;
		(void)TransitionAndSample(Policy::MainViewPhase::kInWorld, false);
	}

	void OnWorldReturnedNormally() noexcept
	{
		if (!IsEnabled() || !RequireRenderThread(false) ||
			!g_thread.worldCallActive)
			return;
		(void)TransitionAndSample(Policy::MainViewPhase::kPostWorld, false);
		g_thread.worldReturnedNormally = true;
	}

	void OnWorldFinally() noexcept
	{
		const bool active = g_thread.worldCallActive;
		const bool returned = g_thread.worldReturnedNormally;
		g_thread.worldCallActive = false;
		g_thread.worldReturnedNormally = false;
		if (active && !RequireRenderThread(false))
			return;
		if (active && !returned && IsEnabled()) {
			g_counters.abnormalWorldReturns.fetch_add(1, std::memory_order_relaxed);
			LatchFault(Policy::ProbeFault::kDrawCallbackFault);
		}
	}

	void VRWorldScopeEnter() noexcept
	{
		FirstPersonCallerHook::RunBeforeNativeGuarded();
	}

	void VRWorldScopeReturned() noexcept
	{
		FirstPersonCallerHook::RunReturnedGuarded();
	}

	void VRWorldScopeFinally() noexcept
	{
		FirstPersonCallerHook::RunFinallyGuarded();
	}

	void OnPreFirstPerson() noexcept
	{
		// The hook may already be live while DataLoaded activation occurs in the
		// middle of a menu/loading frame.  Without a preceding tracked world phase
		// there is no post-world sample to join, so ignore this orphan callback.
		if (!IsEnabled() || !g_thread.currentStampValid ||
			!RequireRenderThread(false))
			return;
		if (g_thread.worldCallActive || g_thread.firstPersonCallActive) {
			LatchFault(Policy::ProbeFault::kPhaseOrderFault);
			return;
		}
		g_thread.firstPersonCallActive = true;
		g_thread.firstPersonReturnedNormally = false;
		g_thread.firstPersonExactMatchesThisCall = 0;
		(void)TransitionAndSample(
			Policy::MainViewPhase::kPreFirstPerson, false);
	}

	void OnEnterFirstPerson() noexcept
	{
		if (!IsEnabled() || !RequireRenderThread(false) ||
			!g_thread.firstPersonCallActive)
			return;
		(void)TransitionAndSample(
			Policy::MainViewPhase::kInFirstPerson, false);
	}

	void OnFirstPersonReturnedNormally() noexcept
	{
		if (!IsEnabled() || !RequireRenderThread(false) ||
			!g_thread.firstPersonCallActive)
			return;
		if (g_thread.firstPersonExactMatchesThisCall == 0) {
			g_counters.firstPersonCallsWithZeroMatches.fetch_add(
				1, std::memory_order_relaxed);
		} else if (g_thread.firstPersonExactMatchesThisCall == 1) {
			g_counters.firstPersonCallsWithOneMatch.fetch_add(
				1, std::memory_order_relaxed);
		} else {
			g_counters.firstPersonCallsWithMultipleMatches.fetch_add(
				1, std::memory_order_relaxed);
		}
		(void)TransitionAndSample(
			Policy::MainViewPhase::kPostFirstPerson, false);
		g_thread.firstPersonReturnedNormally = true;
	}

	void OnFirstPersonFinally() noexcept
	{
		// Clear POD TLS first.  No observer helper may delay native unwind.
		const bool active = g_thread.firstPersonCallActive;
		const bool returned = g_thread.firstPersonReturnedNormally;
		g_thread.firstPersonCallActive = false;
		g_thread.firstPersonReturnedNormally = false;
		g_thread.firstPersonExactMatchesThisCall = 0;
		if (active && !RequireRenderThread(false))
			return;
		if (active && !returned && IsEnabled()) {
			g_counters.abnormalFirstPersonReturns.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(Policy::ProbeFault::kDrawCallbackFault);
		}
	}

	void FailStopCallbackFault() noexcept
	{
		LatchFault(Policy::ProbeFault::kDrawCallbackFault);
	}

	GenericDrawToken OnGenericDrawEntry(RE::BSRenderPass* pass) noexcept
	{
		GenericDrawToken token{};
		if (!IsEnabled() || !g_thread.currentStampValid ||
			(!g_thread.worldCallActive && !g_thread.firstPersonCallActive)) {
			return token;
		}
		if (!RequireRenderThread(false))
			return token;
		const auto phase = g_thread.currentStamp.phase;
		if (phase != Policy::MainViewPhase::kInWorld &&
			phase != Policy::MainViewPhase::kInFirstPerson) {
			return token;
		}

		// Skyrim VR (VRIK): the visible body is the third-person biped, so the
		// exact main-view pane comes from that biped's shield clone during the
		// world pass; the phase still says "first person" for the join.
		const auto path = phase == Policy::MainViewPhase::kInFirstPerson &&
				!REL::Module::IsVR() ?
			Policy::BipedPath::kFirstPerson : Policy::BipedPath::kThirdPerson;
		const auto* const sample = DrawJoinSample(path);
		if (!sample)
			return token;

		const bool firstPerson = path == Policy::BipedPath::kFirstPerson;
		const auto sampledCloneAddress = firstPerson ?
			sample->identity.firstPersonShield.partCloneAddressValue :
			sample->identity.thirdPersonShield.partCloneAddressValue;
		bool malformed = false;
		if (!ResolvePassToCloneGuarded(
				pass, sampledCloneAddress, malformed)) {
			if (malformed) {
				g_counters.ancestryFaults.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(Policy::ProbeFault::kDrawCallbackFault);
			}
			return token;
		}

		// Only an exact sampled-clone candidate pays for a fresh biped read.
		// The fresh identity must still name the clone just resolved from pass.
		RawReadValues entryValues{};
		if (!ReadRawValuesGuarded(entryValues))
			return token;
		if (!SynchronizeEpochs(entryValues))
			return token;
		RememberRawValues(entryValues);
		const auto freshCloneAddress = firstPerson ?
			entryValues.identity.firstPersonShield.partCloneAddressValue :
			entryValues.identity.thirdPersonShield.partCloneAddressValue;
		if (freshCloneAddress != sampledCloneAddress)
			return token;
		if (g_thread.genericDrawEntrySequence ==
				(std::numeric_limits<std::uint64_t>::max)()) {
			LatchFault(Policy::ProbeFault::kEpochExhausted);
			return token;
		}
		const auto entrySequence = ++g_thread.genericDrawEntrySequence;
		auto& pending = g_thread.pendingDrawValues[
			(entrySequence - 1u) % g_thread.pendingDrawValues.size()];
		if (pending.valid) {
			g_counters.pendingDrawOverflows.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(Policy::ProbeFault::kPendingDrawOverflow);
			return token;
		}
		pending = {
			.mainViewEpoch = g_thread.mainViewEpoch,
			.entrySequence = entrySequence,
			.identity = entryValues.identity,
			.epochs = g_thread.epochs,
			.transforms = firstPerson ? entryValues.firstPersonTransforms :
				entryValues.thirdPersonTransforms,
			.phaseOrdinal = Policy::PhaseOrdinal(phase),
			.path = path,
			.valid = true
		};
		token.mainViewEpoch = g_thread.mainViewEpoch;
		token.entrySequence = entrySequence;
		token.phaseOrdinal = Policy::PhaseOrdinal(phase);
		token.path = static_cast<std::uint8_t>(path);
		token.active = true;
		return token;
	}

	void OnGenericDrawReturnedNormally(const GenericDrawToken& token) noexcept
	{
		if (!token.active || !IsEnabled() || !RequireRenderThread(false) ||
			!g_thread.currentStampValid ||
			token.mainViewEpoch != g_thread.mainViewEpoch ||
			token.phaseOrdinal !=
				Policy::PhaseOrdinal(g_thread.currentStamp.phase)) {
			return;
		}

		const auto path = static_cast<Policy::BipedPath>(token.path);
		const auto expectedPath = g_thread.currentStamp.phase ==
				Policy::MainViewPhase::kInFirstPerson ?
			Policy::BipedPath::kFirstPerson : Policy::BipedPath::kThirdPerson;
		if (path != expectedPath)
			return;
		auto& pendingSlot = g_thread.pendingDrawValues[
			(token.entrySequence - 1u) % g_thread.pendingDrawValues.size()];
		if (!pendingSlot.valid ||
			pendingSlot.mainViewEpoch != token.mainViewEpoch ||
			pendingSlot.entrySequence != token.entrySequence ||
			pendingSlot.phaseOrdinal != token.phaseOrdinal ||
			pendingSlot.path != path) {
			return;
		}
		const PendingDrawValueRecord pending = pendingSlot;
		pendingSlot.valid = false;
		const auto* const sample = DrawJoinSample(path);
		if (!sample)
			return;

		RawReadValues firstValues{};
		RawReadValues secondValues{};
		if (!ReadRawValuesPairGuarded(firstValues, secondValues)) {
			if (!g_faulted.load(std::memory_order_acquire)) {
				g_counters.unavailableReads.fetch_add(
					1, std::memory_order_relaxed);
			}
			return;
		}
		if (!SynchronizeEpochs(firstValues) ||
			!SameRawReadValues(firstValues, secondValues)) {
			g_counters.unavailableReads.fetch_add(
				1, std::memory_order_relaxed);
			return;
		}
		RememberRawValues(secondValues);

		if (!RecordLedger(Policy::EvidenceRecordKind::kDrawObservation))
			return;
		if (g_thread.drawSequence ==
				(std::numeric_limits<std::uint64_t>::max)()) {
			LatchFault(Policy::ProbeFault::kEpochExhausted);
			return;
		}
		const bool firstPerson = path == Policy::BipedPath::kFirstPerson;
		// VR: a third-person draw inside the world scope, on the main colour
		// target with the main viewport, is the exact main-view submission
		// (shadow, private and menu passes fail the target probe).
		const bool exactMainView = firstPerson ||
			(REL::Module::IsVR() && g_thread.firstPersonCallActive &&
			 HandMirrorApprovedContentReadOnlyObserver::CurrentDrawTargetsMainView());
		const bool sameEntryIdentity =
			pending.identity == secondValues.identity;
		const auto freshCloneAddress = firstPerson ?
			secondValues.identity.firstPersonShield.partCloneAddressValue :
			secondValues.identity.thirdPersonShield.partCloneAddressValue;
		Policy::RawNormalReturnDrawObservation draw{
			.drawSequence = ++g_thread.drawSequence,
			.stamp = g_thread.currentStamp,
			.path = path,
			.identity = secondValues.identity,
			.epochs = g_thread.epochs,
			.transforms = firstPerson ? secondValues.firstPersonTransforms :
				secondValues.thirdPersonTransforms,
			.observedBipedAddressValue = firstPerson ?
				secondValues.identity.firstPersonBipedAddressValue :
				secondValues.identity.thirdPersonBipedAddressValue,
			.observedRootAddressValue = firstPerson ?
				secondValues.identity.firstPersonRootAddressValue :
				secondValues.identity.thirdPersonRootAddressValue,
			.observedPartCloneAddressValue = freshCloneAddress,
			.drawResolvedToExactPartClone = sameEntryIdentity,
			// Exact RenderFirstPersonView scope is sufficient.  RenderWorld scope
			// alone is not sufficient to exclude shadow-family submissions; on VR
			// the main-target probe supplies that exclusion.
			.mainPlayerViewSubmission = exactMainView,
			.privateShadowMenuAndInventoryPassExcluded = exactMainView,
			.nativeDrawEntered = true,
			.nativeDrawReturnedNormally = true,
			.transformCopyCompletedAfterNormalReturn = true,
			.callbackFaulted = false,
			.noEnginePointerRetained = true
		};

		if (exactMainView) {
			g_counters.exactFirstPersonDraws.fetch_add(
				1, std::memory_order_relaxed);
			if (g_thread.firstPersonExactMatchesThisCall ==
					(std::numeric_limits<std::uint32_t>::max)()) {
				LatchFault(Policy::ProbeFault::kEpochExhausted);
				return;
			}
			++g_thread.firstPersonExactMatchesThisCall;
		} else {
			g_counters.exactThirdPersonCandidates.fetch_add(
				1, std::memory_order_relaxed);
			g_counters.thirdPersonMainViewUnproven.fetch_add(
				1, std::memory_order_relaxed);
			// RenderWorld TLS is deliberately insufficient to classify a 3P
			// submission.  Keep this as a raw candidate and consume no join slot.
			return;
		}

		if (!RecordLedger(Policy::EvidenceRecordKind::kDrawJoin))
			return;
		const auto joined = Policy::JoinRawNormalReturnDraw(*sample, draw);
		const auto statusIndex = static_cast<std::size_t>(joined.status);
		if (statusIndex < g_counters.drawJoinStatuses.size()) {
			g_counters.drawJoinStatuses[statusIndex].fetch_add(
				1, std::memory_order_relaxed);
		}
		if (firstPerson && joined.JoinedDiagnosticOnly()) {
			g_counters.firstPersonBitExactJoins.fetch_add(
				1, std::memory_order_relaxed);
		} else if (firstPerson &&
			joined.status == Policy::RawDrawJoinStatus::kPoseEpochChanged) {
			g_counters.firstPersonPoseEpochDrift.fetch_add(
				1, std::memory_order_relaxed);
		} else if (firstPerson &&
			joined.status == Policy::RawDrawJoinStatus::kTransformBitsChanged) {
			g_counters.firstPersonTransformDrift.fetch_add(
				1, std::memory_order_relaxed);
		}
	}

	bool IsHookInstalled() noexcept
	{
		return g_hookInstalled.load(std::memory_order_acquire);
	}

	bool IsEnabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire) &&
			!g_faulted.load(std::memory_order_acquire);
	}

	bool IsFaultStopped() noexcept
	{
		return g_faulted.load(std::memory_order_acquire);
	}

	void MaybeLogPeriodic() noexcept
	{
		if (!Requested())
			return;
		const bool faultPending = g_faultDiagnosticPending.exchange(
			false, std::memory_order_acq_rel);
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		auto previous = g_lastPeriodicLogMilliseconds.load(
			std::memory_order_relaxed);
		if (faultPending) {
			g_lastPeriodicLogMilliseconds.store(now, std::memory_order_relaxed);
		} else {
			if (now - previous <
					std::chrono::duration_cast<std::chrono::milliseconds>(
						kDiagnosticPeriod).count() ||
				!g_lastPeriodicLogMilliseconds.compare_exchange_strong(
					previous, now, std::memory_order_relaxed)) {
				return;
			}
		}
		try {
			LogDiagnostics(faultPending ? "fault-latched" : "periodic");
		} catch (...) {
		}
	}

	void LogDiagnostics(const char* reason)
	{
		if (!Requested())
			return;
		logger::info(
			"[RR][HandMirrorRawObserver][{}] requested={} renderWorldOwner={} genericOwner={} firstPersonHook(installed/validated/call/native/branch)={}/{}/0x{:X}/0x{:X}/0x{:X} enabled={} faulted={} firstFault={} "
			"install(prepare/complete/worldReject/genericReject/runtimeReject/signatureReject/nativeReject/patchFail/postPatchReject)={}/{}/{}/{}/{}/{}/{}/{}/{}",
			reason ? reason : "status", Requested(),
			g_renderWorldOwnerReady.load(std::memory_order_relaxed),
			g_genericDrawOwnerReady.load(std::memory_order_relaxed),
			IsHookInstalled(),
			g_hookValidated.load(std::memory_order_relaxed),
			g_callSite.load(std::memory_order_relaxed),
			g_nativeTarget.load(std::memory_order_relaxed),
			g_installedBranchTarget.load(std::memory_order_relaxed),
			IsEnabled(), IsFaultStopped(),
			g_firstFault.load(std::memory_order_relaxed),
			g_counters.prepareCalls.load(std::memory_order_relaxed),
			g_counters.completeCalls.load(std::memory_order_relaxed),
			g_counters.renderWorldOwnerRejects.load(std::memory_order_relaxed),
			g_counters.genericDrawOwnerRejects.load(std::memory_order_relaxed),
			g_counters.runtimeRejects.load(std::memory_order_relaxed),
			g_counters.signatureRejects.load(std::memory_order_relaxed),
			g_counters.nativeTargetRejects.load(std::memory_order_relaxed),
			g_counters.patchFailures.load(std::memory_order_relaxed),
			g_counters.postPatchRejects.load(std::memory_order_relaxed));
		logger::info(
			"[RR][HandMirrorRawObserver][{}] lifecycle(dataLoaded/equipWake/gameLoad/mainViews)={}/{}/{}/{} phase(attempts/admitted/readFault/unavailable/overflow)={}/{}/{}/{}/{} abnormal(world/firstPerson)={}/{} "
			"draws(firstPerson/thirdPersonCandidate/thirdPersonUnproven)={}/{}/{} firstPerson(joined/poseEpochDrift/transformBitsDrift/calls0/calls1/callsMany)={}/{}/{}/{}/{}/{} pendingOverflow={} rawCanAuthorize=false strict/content/capture/publication=false finalPixelClaim=false",
			reason ? reason : "status",
			g_counters.dataLoadedCalls.load(std::memory_order_relaxed),
			g_counters.equipWakeEvents.load(std::memory_order_relaxed),
			g_counters.gameLoadInvalidations.load(std::memory_order_relaxed),
			g_counters.mainViews.load(std::memory_order_relaxed),
			g_counters.phaseAttempts.load(std::memory_order_relaxed),
			g_counters.phaseSamples.load(std::memory_order_relaxed),
			g_counters.guardedReadFaults.load(std::memory_order_relaxed),
			g_counters.unavailableReads.load(std::memory_order_relaxed),
			g_counters.ledgerOverflows.load(std::memory_order_relaxed),
			g_counters.abnormalWorldReturns.load(std::memory_order_relaxed),
			g_counters.abnormalFirstPersonReturns.load(std::memory_order_relaxed),
			g_counters.exactFirstPersonDraws.load(std::memory_order_relaxed),
			g_counters.exactThirdPersonCandidates.load(
				std::memory_order_relaxed),
			g_counters.thirdPersonMainViewUnproven.load(
				std::memory_order_relaxed),
			g_counters.firstPersonBitExactJoins.load(
				std::memory_order_relaxed),
			g_counters.firstPersonPoseEpochDrift.load(
				std::memory_order_relaxed),
			g_counters.firstPersonTransformDrift.load(
				std::memory_order_relaxed),
			g_counters.firstPersonCallsWithZeroMatches.load(
				std::memory_order_relaxed),
			g_counters.firstPersonCallsWithOneMatch.load(
				std::memory_order_relaxed),
			g_counters.firstPersonCallsWithMultipleMatches.load(
				std::memory_order_relaxed),
			g_counters.pendingDrawOverflows.load(
				std::memory_order_relaxed));
	}
}
