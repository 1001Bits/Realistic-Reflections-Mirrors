#include "PCH.h"
#include "MirrorFeatures.h"

#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "EngineDeviceIdentity.h"

#include "HandMirrorLastGoodPresentationPolicy.h"
#include "HandMirrorLoweredPresentationPolicy.h"

#include "GraphicsFrameCounterPolicy.h"
#include "HandMirrorVRRuntimePolicy.h"
#include "HandMirrorApprovedContentReadOnlyGatePolicy.h"
#include "HandMirrorReadOnlyObserver.h"
#include "MirrorsOfSkyrimGeometryDrawObserverOwner.h"
#include "SecondView.h"
namespace GeometryDrawObserver = MirrorsOfSkyrimGeometryDrawObserver;

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <d3d11.h>

#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <type_traits>

#pragma comment(lib, "bcrypt.lib")

namespace HandMirrorApprovedContentReadOnlyObserver
{
	namespace
	{
		namespace Policy = HandMirrorApprovedContentReadOnlyGatePolicy;
		namespace Freshness = HandMirrorFrameReceiptPolicy;
		namespace fs = std::filesystem;

		constexpr REL::Version kSupportedSE{ 1, 5, 97, 0 };
		constexpr REL::Version kSupportedAE{ 1, 6, 1170, 0 };
		constexpr std::size_t kMaximumParentDepth = 256;
		constexpr std::size_t kPendingDrawCapacity = 4;
		constexpr auto kDiagnosticPeriod = std::chrono::seconds{ 5 };

		constexpr std::uint64_t kFNVOffset = UINT64_C(14695981039346656037);
		constexpr std::uint64_t kFNVPrime = UINT64_C(1099511628211);

		[[nodiscard]] constexpr std::uint64_t HashText(
			const std::string_view text) noexcept
		{
			std::uint64_t value = kFNVOffset;
			for (const auto character : text) {
				value ^= static_cast<std::uint8_t>(character);
				value *= kFNVPrime;
			}
			return value == 0 ? 1 : value;
		}

		[[nodiscard]] constexpr std::uint64_t Mix64(std::uint64_t value) noexcept
		{
			value += UINT64_C(0x9E3779B97F4A7C15);
			value = (value ^ (value >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
			value = (value ^ (value >> 27)) * UINT64_C(0x94D049BB133111EB);
			value ^= value >> 31;
			return value == 0 ? 1 : value;
		}

		template <class T>
		[[nodiscard]] Policy::IdentityToken PointerToken(
			const T* pointer,
			const std::uint64_t identityDomain) noexcept
		{
			if (!pointer)
				return 0;
			// Domains document the observation role at each call site, but must not
			// perturb the token: one live object seen in two roles must retain one
			// value identity so the policy's alias checks fail closed.
			(void)identityDomain;
			return Mix64(
				static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)) ^
				UINT64_C(0xA8329D4CB70F51E3));
		}

		enum class FrozenStyleIndex : std::uint8_t
		{
			kFiligreeV1 = 0,
			kCount = 1,
			kNone = 0xFF
		};

		constexpr std::size_t kFrozenStyleCount =
			static_cast<std::size_t>(FrozenStyleIndex::kCount);

		struct FrozenStyleRuntimeContract
		{
			FrozenStyleIndex index{ FrozenStyleIndex::kNone };
			std::string_view diagnosticName{};
			std::string_view pluginBasename{};
			std::uint32_t armorLocalFormID{ 0 };
			std::uint32_t armorAddonLocalFormID{ 0 };
			std::uint64_t pluginByteLength{ 0 };
			const std::array<std::uint8_t, 32>* pluginSHA256{ nullptr };
			// Same plugin, ESL flag cleared, for runtimes without light-plugin
			// support (Skyrim VR 1.4.15).
			const std::array<std::uint8_t, 32>* pluginSHA256VR{ nullptr };
			const std::array<FrozenAssetContract, 4>* equippedAssets{ nullptr };
			std::uint32_t allFrozenFilesVerifiedMask{ 0 };
			std::uint64_t pluginIdentity{ 0 };
			std::uint64_t frozenContentLockIdentity{ 0 };
			std::uint64_t styleIdentity{ 0 };
			std::uint64_t mirrorItemContractIdentity{ 0 };
			std::uint64_t paneContractIdentity{ 0 };
			std::uint64_t apertureContractIdentity{ 0 };
		};

		constexpr std::array<FrozenStyleRuntimeContract, kFrozenStyleCount>
			kFrozenStyleContracts{
				FrozenStyleRuntimeContract{
					.index = FrozenStyleIndex::kFiligreeV1,
					.diagnosticName = "filigree-v1",
					.pluginBasename = kFiligreeV1PluginBasename,
					.armorLocalFormID =
						kFiligreeV1ArmorLocalFormID,
					.armorAddonLocalFormID =
						kFiligreeV1ArmorAddonLocalFormID,
					.pluginByteLength = kFiligreeV1PluginByteLength,
					.pluginSHA256 = &kFiligreeV1PluginSHA256,
					.pluginSHA256VR = &kFiligreeV1PluginSHA256VR,
					.equippedAssets = &kFiligreeV1EquippedAssets,
					.allFrozenFilesVerifiedMask =
						kFiligreeV1AllFrozenFilesVerifiedMask,
					.pluginIdentity = HashText(
						kFiligreeV1PluginBasename),
					.frozenContentLockIdentity = HashText(
						"filigree-v1:94E4E91361FBEEB6EFFB7E7099BEDDAA870FDB60EABB67857D474A9D885D958C:"
						"73E100C99D44872C119F18D0064084B0E798E2656EDDD878FC294D0F579D3106"),
					.styleIdentity = HashText(
						"gilded-noble-round-filigree-v1"),
					.mirrorItemContractIdentity = HashText(
						"filigree-v1:RRMirrorItem:direct:MirrorFrame,TrueMirror:0"),
					.paneContractIdentity = HashText(
						"filigree-v1:TrueMirror:0:TrueMirror=2:RRMirrorAperture=7,7,1,2"),
					.apertureContractIdentity = HashText(
						"filigree-v1:frame-opening-24gon-aperture:7,7,1,2")
				}
			};

		[[nodiscard]] constexpr std::uint32_t StyleBit(
			const FrozenStyleIndex index) noexcept
		{
			return index == FrozenStyleIndex::kNone ? 0u :
				1u << static_cast<std::uint8_t>(index);
		}

		[[nodiscard]] constexpr const FrozenStyleRuntimeContract* StyleContract(
			const FrozenStyleIndex index) noexcept
		{
			const auto value = static_cast<std::size_t>(index);
			return value < kFrozenStyleContracts.size() ?
				&kFrozenStyleContracts[value] : nullptr;
		}

		constexpr std::uint64_t kDomainPlayer = UINT64_C(0x1016B15B37A23907);
		constexpr std::uint64_t kDomainBipedFirst = UINT64_C(0x20C134C90A1B5F11);
		constexpr std::uint64_t kDomainBipedThird = UINT64_C(0x30C134C90A1B5F11);
		constexpr std::uint64_t kDomainRootFirst = UINT64_C(0x4019D5B3483B3921);
		constexpr std::uint64_t kDomainRootThird = UINT64_C(0x5019D5B3483B3921);
		constexpr std::uint64_t kDomainCloneFirst = UINT64_C(0x602A71A8612D0373);
		constexpr std::uint64_t kDomainCloneThird = UINT64_C(0x702A71A8612D0373);
		constexpr std::uint64_t kDomainItemFirst = UINT64_C(0x807D156991B1AB11);
		constexpr std::uint64_t kDomainItemThird = UINT64_C(0x907D156991B1AB11);
		constexpr std::uint64_t kDomainPaneFirst = UINT64_C(0xA018A8DDC44E76F3);
		constexpr std::uint64_t kDomainPaneThird = UINT64_C(0xB018A8DDC44E76F3);
		constexpr std::uint64_t kDomainDevice = UINT64_C(0xC023A1AC7724491D);
		constexpr std::uint64_t kDomainColorView = UINT64_C(0xD0D4716C944612A9);
		constexpr std::uint64_t kDomainColorResource = UINT64_C(0xE04A624DBF450859);
		constexpr std::uint64_t kDomainDepthView = UINT64_C(0xF06D4BD02F96C4E7);
		constexpr std::uint64_t kDomainDepthResource = UINT64_C(0x1194EA80C76711C3);

		struct RawClone
		{
			Policy::IdentityToken bipedIdentity{ 0 };
			Policy::IdentityToken rootIdentity{ 0 };
			Policy::IdentityToken partCloneIdentity{ 0 };
			Policy::IdentityToken itemIdentity{ 0 };
			Policy::IdentityToken paneIdentity{ 0 };
			Policy::PoseWords paneWorld{};
			Policy::PoseWords partCloneWorld{};
			Policy::PoseWords rootWorld{};
			std::uint32_t rootLastUpdated{ 0 };
			std::uint32_t partCloneLastUpdated{ 0 };
			std::uint32_t itemLastUpdated{ 0 };
			std::uint32_t paneLastUpdated{ 0 };
			bool itemMatches{ false };
			bool addonMatches{ false };
			bool modelMatches{ false };
			bool partCloneDescendsRoot{ false };
			bool itemDirectChildOfPartClone{ false };
			bool paneDirectChildOfItem{ false };
			bool exactDirectSchema{ false };
			bool visibleAndNotAppCulled{ false };

			bool operator==(const RawClone&) const noexcept = default;
		};

		struct RawCandidate
		{
			Policy::ApprovedContentIdentity approved{};
			RawClone first{};
			RawClone third{};
			FrozenStyleIndex styleIndex{ FrozenStyleIndex::kNone };
			Policy::Perspective perspective{ Policy::Perspective::kUnknown };
			std::uint32_t graphicsFrame{ 0 };
			std::uint32_t playerFormID{ 0 };
			std::uint32_t playerHandle{ 0 };
			bool formsExact{ false };
			bool exactPlayer{ false };
			bool equipmentSlotsConclusive{ false };
			bool approvedArmorInFirstShieldSlot{ false };
			bool approvedArmorInThirdShieldSlot{ false };
			bool exactTupleEquipped{ false };
			bool currentRaceAndSexExact{ false };
			bool graphIsBlocking{ false };
			bool actorWantsBlocking{ false };
			// Weapon drawn (actor state), read with the blocking pair: the block
			// input only raises the mirror while a weapon is out.
			bool weaponDrawn{ false };
			bool passIsExactFirstPersonPane{ false };
			bool contentFilesVerified{ false };
			bool styleAmbiguous{ false };
		};

		// The biped whose pane is the exact main-view pane: first person on flat,
		// third person on Skyrim VR (VRIK shows the third-person body).
		[[nodiscard]] inline const RawClone& Exact(const RawCandidate& a_candidate) noexcept
		{
			return REL::Module::IsVR() ? a_candidate.third : a_candidate.first;
		}

		struct TargetRead
		{
			Policy::MainTargetIdentity identity{};
			bool colorWritable{ false };
			bool depthWritable{ false };
			bool viewportMatchesShadowState{ false };
			bool contextAndTargetsExactDevice{ false };
			bool noOutputConflict{ false };
			bool noMainResourceAlias{ false };
			bool completed{ false };
			bool faulted{ false };
		};

		struct TargetCOMScratch
		{
			ID3D11RenderTargetView* color{ nullptr };
			ID3D11DepthStencilView* depth{ nullptr };
			ID3D11Resource* colorResource{ nullptr };
			ID3D11Resource* depthResource{ nullptr };
			ID3D11Device* contextDevice{ nullptr };
			ID3D11Device* colorDevice{ nullptr };
			ID3D11Device* depthDevice{ nullptr };
			ID3D11Device* uavInspectionDevice{ nullptr };
			ID3D11Predicate* predicate{ nullptr };
			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> uavInspectionRTVs{};
			std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT>
				uavInspectionViews{};
			std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> soTargets{};
		};

		struct PendingDraw
		{
			std::uint64_t sourceFrame{ 0 };
			std::uint64_t entrySequence{ 0 };
			Policy::CandidateBinding binding{};
			Policy::IdentityToken paneIdentity{ 0 };
			TargetRead targetAtEntry{};
			bool visibleAndNotAppCulled{ false };
			bool valid{ false };
		};

		struct ThreadState
		{
			std::uint64_t sourceFrame{ 0 };
			std::uint64_t entrySequence{ 0 };
			std::uint64_t globalLifecycleSeen{ 1 };
			std::uint64_t equipWakeSeen{ 1 };
			Policy::TokenIssuerState snapshotIssuer{};
			Policy::TokenIssuerState observationIssuer{};
			Policy::LifecycleState lifecycle{};
			Freshness::FreshnessState hiddenFreshness{};
			Policy::TelemetryLedger telemetry{};
			std::uint64_t seenGateStatuses{ 0 };
			RawCandidate previousRaw{};
			Policy::ApprovedContentReadOnlyGateEvidence evidence{};
			std::array<PendingDraw, kPendingDrawCapacity> pending{};
			bool lifecycleInitialized{ false };
			bool previousRawValid{ false };
			bool worldActive{ false };
			bool worldReturnedNormally{ false };
			bool firstPersonActive{ false };
			bool firstPersonReturnedNormally{ false };
			ExactPaneReturnCohort exactPaneReturnCohort{
				ExactPaneReturnCohort::kZero };
		};

		static_assert(std::is_trivially_copyable_v<RawClone>);
		static_assert(std::is_trivially_copyable_v<RawCandidate>);
		static_assert(std::is_trivially_copyable_v<TargetRead>);
		static_assert(std::is_trivially_copyable_v<TargetCOMScratch>);
		static_assert(std::is_trivially_destructible_v<TargetCOMScratch>);
		static_assert(D3D11_PS_CS_UAV_REGISTER_COUNT == 8);
		static_assert(D3D11_1_UAV_SLOT_COUNT == 64);
		static_assert(std::is_trivially_copyable_v<PendingDraw>);
		static_assert(std::is_trivially_copyable_v<ThreadState>);
		static_assert(std::is_trivially_copyable_v<GenericDrawToken>);

		struct Counters
		{
			std::atomic<std::uint64_t> dataLoadedCalls{ 0 };
			std::atomic<std::uint64_t> gameLoadInvalidations{ 0 };
			std::atomic<std::uint64_t> equipWakeups{ 0 };
			std::atomic<std::uint64_t> frames{ 0 };
			std::atomic<std::uint64_t> guardedReads{ 0 };
			std::atomic<std::uint64_t> guardedReadFaults{ 0 };
			std::atomic<std::uint64_t> postWorldSnapshots{ 0 };
			std::atomic<std::uint64_t> exactCandidates{ 0 };
			std::atomic<std::uint64_t> exactPaneDrawReturns{ 0 };
			std::atomic<std::uint64_t> mainTargetReads{ 0 };
			std::atomic<std::uint64_t> mainTargetReadFaults{ 0 };
			std::atomic<std::uint64_t> gateEvaluations{ 0 };
			std::atomic<std::uint64_t> ordinaryRejects{ 0 };
			std::atomic<std::uint64_t> admissions{ 0 };
			std::atomic<std::uint64_t> abnormalWorldReturns{ 0 };
			std::atomic<std::uint64_t> abnormalFirstPersonReturns{ 0 };
			std::atomic<std::uint64_t> pendingOverflows{ 0 };
			std::atomic<std::uint64_t> runtimeBorrows{ 0 };
			std::atomic<std::uint64_t> runtimeBorrowRejects{ 0 };
			std::atomic<std::uint64_t> runtimeBorrowFaults{ 0 };
			std::atomic<std::uint64_t> blockingGraphTrue{ 0 };
			std::atomic<std::uint64_t> blockingWantTrue{ 0 };
			std::atomic<std::uint64_t> blockingSignalDisagreements{ 0 };
			std::atomic<std::uint64_t> preFreshnessChecks{ 0 };
			std::atomic<std::uint64_t> preFreshnessPasses{ 0 };
			std::atomic<std::uint64_t> preReceiptInvalidRejects{ 0 };
			std::atomic<std::uint64_t> preGraphicsFrameMismatchRejects{ 0 };
			std::atomic<std::uint64_t> preExactTupleRejects{ 0 };
			std::atomic<std::uint64_t> preRaceSexRejects{ 0 };
			std::atomic<std::uint64_t> prePerspectiveRejects{ 0 };
			std::atomic<std::uint64_t> preFirstSchemaRejects{ 0 };
			std::atomic<std::uint64_t> preThirdSchemaRejects{ 0 };
			std::atomic<std::uint64_t> preVisiblePaneRejects{ 0 };
			std::atomic<std::uint64_t> preExactPassRejects{ 0 };
			std::atomic<std::uint64_t> preOwnerIdentityRejects{ 0 };
			std::atomic<std::uint64_t> preCandidateReadRejects{ 0 };
			std::atomic<std::uint64_t> preCandidatePairMismatchRejects{ 0 };
			std::atomic<std::uint64_t> preLifecycleRefreshRejects{ 0 };
			std::atomic<std::uint64_t> freshnessDirectCurrent{ 0 };
			std::atomic<std::uint64_t> freshnessSeedsDark{ 0 };
			std::atomic<std::uint64_t> freshnessCalibrations{ 0 };
			std::atomic<std::uint64_t> freshnessCalibratedRereads{ 0 };
			std::atomic<std::uint64_t> freshnessResetsDark{ 0 };
			std::atomic<std::uint64_t> freshnessRejects{ 0 };
		};

		Counters g_counters{};
		std::atomic_bool g_prepareAttempted{ false };
		std::atomic_bool g_requested{ false };
		std::atomic_bool g_dataLoaded{ false };
		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_faultDiagnosticPending{ false };
		std::atomic<std::uint8_t> g_firstFault{
			static_cast<std::uint8_t>(Policy::ObserverFault::kNone) };
		std::atomic<std::uint8_t> g_lastGateStatus{
			static_cast<std::uint8_t>(Policy::ReadOnlyGateStatus::kNoCandidateDark) };
		std::array<std::atomic<std::uint32_t>, kFrozenStyleCount>
			g_verifiedFileMasks{};
		std::atomic<std::uint32_t> g_formsPinnedStyleMask{ 0 };
		std::atomic<std::uint32_t> g_readyStyleMask{ 0 };
		std::atomic<std::uint64_t> g_globalLifecycleSerial{ 1 };
		std::atomic<std::uint64_t> g_equipWakeSerial{ 1 };
		std::atomic<std::int64_t> g_lastPeriodicLogMilliseconds{ 0 };
		std::atomic<std::uint32_t> g_lastGraphicsFrame{ 0 };
		std::atomic<std::uint32_t> g_lastThirdRootUpdate{ 0 };
		std::atomic<std::uint32_t> g_lastThirdCloneUpdate{ 0 };
		std::atomic<std::uint32_t> g_lastThirdItemUpdate{ 0 };
		std::atomic<std::uint32_t> g_lastThirdPaneUpdate{ 0 };
		std::atomic<std::uint64_t> g_lastReceiptSource{ 0 };
		std::atomic<std::uint64_t> g_lastReceiptMainView{ 0 };
		std::atomic<std::uint32_t> g_lastReceiptGraphics{ 0 };
		std::atomic_bool g_lastGraphIsBlocking{ false };
		std::atomic_bool g_livePosePresentationEnabled{ false };
		std::atomic_bool g_vrRuntimeFixEnabled{ false };
		std::atomic_bool g_vrPresentationFixEnabled{ false };
		std::atomic_bool g_vrViewCohortFixEnabled{ false };
		std::atomic_bool g_vrSceneFixEnabled{ false };
		std::atomic_bool g_lastActorWantsBlocking{ false };
		std::atomic<RuntimePreFreshnessRejectMask>
			g_lastPreFreshnessRejectMask{ 0 };
		std::atomic<std::uint8_t> g_lastFreshnessStatus{
			static_cast<std::uint8_t>(Freshness::FreshnessStatus::kRejectedInvalid) };
		thread_local ThreadState g_thread{};

		[[nodiscard]] const RE::BSFixedString& MirrorItemName()
		{
			static stl::no_destructor<RE::BSFixedString> value{ "RRMirrorItem" };
			return value.get();
		}

		[[nodiscard]] const RE::BSFixedString& FrameName()
		{
			static stl::no_destructor<RE::BSFixedString> value{ "MirrorFrame" };
			return value.get();
		}

		[[nodiscard]] const RE::BSFixedString& PaneName()
		{
			static stl::no_destructor<RE::BSFixedString> value{ "TrueMirror:0" };
			return value.get();
		}

		[[nodiscard]] const RE::BSFixedString& SchemaName()
		{
			static stl::no_destructor<RE::BSFixedString> value{ "TrueMirror" };
			return value.get();
		}

		[[nodiscard]] const RE::BSFixedString& ApertureName()
		{
			static stl::no_destructor<RE::BSFixedString> value{ "RRMirrorAperture" };
			return value.get();
		}

		void LatchFault(const Policy::ObserverFault fault) noexcept
		{
			if (fault == Policy::ObserverFault::kNone)
				return;
			std::uint8_t expected = static_cast<std::uint8_t>(
				Policy::ObserverFault::kNone);
			const bool first = g_firstFault.compare_exchange_strong(
				expected, static_cast<std::uint8_t>(fault),
				std::memory_order_acq_rel);
			g_faulted.store(true, std::memory_order_release);
			g_enabled.store(false, std::memory_order_release);
			if (first)
				g_faultDiagnosticPending.store(true, std::memory_order_release);
		}

		[[nodiscard]] bool AdvanceAtomic(
			std::atomic<std::uint64_t>& value) noexcept
		{
			auto current = value.load(std::memory_order_acquire);
			for (;;) {
				if (current == (std::numeric_limits<std::uint64_t>::max)()) {
					LatchFault(Policy::ObserverFault::kTokenExhausted);
					return false;
				}
				if (value.compare_exchange_weak(
						current, current + 1u, std::memory_order_acq_rel)) {
					return true;
				}
			}
		}

		[[nodiscard]] fs::path DataRoot()
		{
			std::array<wchar_t, 32768> executable{};
			const DWORD length = GetModuleFileNameW(
				nullptr, executable.data(), static_cast<DWORD>(executable.size()));
			if (length == 0 || length >= executable.size())
				return {};
			return fs::path{
				std::wstring_view{ executable.data(), length } }.parent_path() / L"Data";
		}

		[[nodiscard]] fs::path MarkerPath(const std::wstring_view marker)
		{
			const auto root = DataRoot();
			return root.empty() ? fs::path{} :
				root / std::wstring{ marker };
		}

		[[nodiscard]] bool HasNoReparseComponent(const fs::path& path) noexcept
		{
			try {
				const auto absolute = fs::absolute(path).lexically_normal();
				fs::path current = absolute.root_path();
				if (!current.empty()) {
					const DWORD attributes = GetFileAttributesW(current.c_str());
					if (attributes == INVALID_FILE_ATTRIBUTES ||
						(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
						return false;
					}
				}
				for (const auto& component : absolute.relative_path()) {
					current /= component;
					const DWORD attributes = GetFileAttributesW(current.c_str());
					if (attributes == INVALID_FILE_ATTRIBUTES ||
						(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
						return false;
					}
				}
				return true;
			} catch (...) {
				return false;
			}
		}

		struct PinnedFileProof
		{
			DWORD volumeSerialNumber{ 0 };
			DWORD fileIndexHigh{ 0 };
			DWORD fileIndexLow{ 0 };

			bool operator==(const PinnedFileProof&) const noexcept = default;
		};

		[[nodiscard]] bool InspectPinnedRegularFile(
			const HANDLE file,
			const std::uint64_t expectedLength,
			PinnedFileProof& proof) noexcept
		{
			proof = {};
			FILE_ATTRIBUTE_TAG_INFO tag{};
			FILE_STANDARD_INFO standard{};
			BY_HANDLE_FILE_INFORMATION basic{};
			if (GetFileType(file) != FILE_TYPE_DISK ||
				!GetFileInformationByHandleEx(
					file, FileAttributeTagInfo, &tag, sizeof(tag)) ||
				!GetFileInformationByHandleEx(
					file, FileStandardInfo, &standard, sizeof(standard)) ||
				!GetFileInformationByHandle(file, &basic) ||
				(tag.FileAttributes &
					(FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
						FILE_ATTRIBUTE_DEVICE)) != 0 ||
				standard.Directory || standard.EndOfFile.QuadPart < 0 ||
				static_cast<std::uint64_t>(standard.EndOfFile.QuadPart) !=
					expectedLength ||
				basic.dwVolumeSerialNumber == 0 ||
				(basic.nFileIndexHigh == 0 && basic.nFileIndexLow == 0)) {
				proof = {};
				return false;
			}
			proof.volumeSerialNumber = basic.dwVolumeSerialNumber;
			proof.fileIndexHigh = basic.nFileIndexHigh;
			proof.fileIndexLow = basic.nFileIndexLow;
			return true;
		}

		[[nodiscard]] bool RevalidatePinnedRegularFile(
			const HANDLE file,
			const std::uint64_t expectedLength,
			const PinnedFileProof& original) noexcept
		{
			PinnedFileProof current{};
			return InspectPinnedRegularFile(
					file, expectedLength, current) &&
				current == original;
		}

		[[nodiscard]] HANDLE OpenPinnedRegularFile(
			const fs::path& path,
			const std::uint64_t expectedLength,
			PinnedFileProof& proof) noexcept
		{
			proof = {};
			if (path.empty() || !HasNoReparseComponent(path))
				return INVALID_HANDLE_VALUE;
			const HANDLE file = CreateFileW(
				path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
					FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return INVALID_HANDLE_VALUE;
			// MO2 can redirect the Data path to a physical file in its mod folder.
			// Validate the opened file's contents and identity, not its location.
			if (!InspectPinnedRegularFile(file, expectedLength, proof)) {
				(void)CloseHandle(file);
				return INVALID_HANDLE_VALUE;
			}
			return file;
		}

		[[nodiscard]] bool ValidateEmptyMarker(const fs::path& path) noexcept
		{
			if (MirrorFeatures::Enabled(path.native())) return true;
			PinnedFileProof proof{};
			const HANDLE file = OpenPinnedRegularFile(path, 0, proof);
			if (file == INVALID_HANDLE_VALUE)
				return false;
			const bool valid = RevalidatePinnedRegularFile(file, 0, proof);
			(void)CloseHandle(file);
			return valid;
		}

		[[nodiscard]] bool HashExactRegularFile(
			const fs::path& path,
			const std::uint64_t expectedLength,
			const std::array<std::uint8_t, 32>& expectedDigest) noexcept
		{
			PinnedFileProof proof{};
			HANDLE file = OpenPinnedRegularFile(path, expectedLength, proof);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			bool accepted = false;
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			PUCHAR hashObject = nullptr;
			DWORD objectBytes = 0;
			std::array<std::uint8_t, 16384> buffer{};
			std::array<std::uint8_t, 32> digest{};
			do {
				if (BCryptOpenAlgorithmProvider(
						&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
					break;
				}
				DWORD copied = 0;
				DWORD digestBytes = 0;
				if (BCryptGetProperty(
						algorithm, BCRYPT_OBJECT_LENGTH,
						reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
						&copied, 0) < 0 ||
					copied != sizeof(objectBytes) || objectBytes == 0 ||
					BCryptGetProperty(
						algorithm, BCRYPT_HASH_LENGTH,
						reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes),
						&copied, 0) < 0 ||
					copied != sizeof(digestBytes) ||
					digestBytes != expectedDigest.size()) {
					break;
				}
				hashObject = static_cast<PUCHAR>(
					HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, objectBytes));
				if (!hashObject || BCryptCreateHash(
						algorithm, &hash, hashObject, objectBytes, nullptr, 0, 0) < 0) {
					break;
				}

				std::uint64_t total = 0;
				bool readComplete = false;
				for (;;) {
					DWORD bytesRead = 0;
					if (!ReadFile(
							file, buffer.data(), static_cast<DWORD>(buffer.size()),
							&bytesRead, nullptr)) {
						break;
					}
					if (bytesRead == 0) {
						readComplete = true;
						break;
					}
					total += bytesRead;
					if (total > expectedLength || BCryptHashData(
							hash, buffer.data(), bytesRead, 0) < 0) {
						break;
					}
				}
				if (!readComplete || total != expectedLength ||
					BCryptFinishHash(
						hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
					break;
				}
				accepted = digest == expectedDigest &&
					RevalidatePinnedRegularFile(
						file, expectedLength, proof);
			} while (false);

			if (hash)
				(void)BCryptDestroyHash(hash);
			if (hashObject) {
				SecureZeroMemory(hashObject, objectBytes);
				(void)HeapFree(GetProcessHeap(), 0, hashObject);
			}
			if (algorithm)
				(void)BCryptCloseAlgorithmProvider(algorithm, 0);
			(void)CloseHandle(file);
			SecureZeroMemory(buffer.data(), buffer.size());
			SecureZeroMemory(digest.data(), digest.size());
			return accepted;
		}

		[[nodiscard]] const std::array<FrozenAssetContract, 4>* EquippedAssets(
			const FrozenStyleRuntimeContract& contract) noexcept
		{
			return REL::Module::IsVR() && contract.equippedAssets == &kFiligreeV1EquippedAssets ?
				&kFiligreeV1EquippedAssetsVR : contract.equippedAssets;
		}

		[[nodiscard]] std::uint32_t VerifyFrozenFiles(
			const FrozenStyleRuntimeContract& contract) noexcept
		{
			try {
				if (!contract.pluginSHA256 || !EquippedAssets(contract)) {
					return 0;
				}
				const auto root = DataRoot();
				if (root.empty())
					return 0;
				// VR ships the same plugin with the ESL flag cleared, so its frozen
				// digest differs by exactly that one header bit.
				const auto* expectedPlugin = contract.pluginSHA256;
				if (REL::Module::IsVR() && contract.pluginSHA256VR)
					expectedPlugin = contract.pluginSHA256VR;
				std::uint32_t mask = 0;
				if (HashExactRegularFile(
						root / std::string{ contract.pluginBasename },
						contract.pluginByteLength, *expectedPlugin)) {
					mask |= 1u;
				}
				for (std::size_t index = 0;
					index < EquippedAssets(contract)->size(); ++index) {
					const auto& asset = (*EquippedAssets(contract))[index];
					if (HashExactRegularFile(
							root / L"meshes" / fs::path{ asset.modelPath },
							asset.byteLength, asset.sha256)) {
						mask |= 1u << (index + 1u);
					}
				}
				return mask;
			} catch (...) {
				return 0;
			}
		}

		[[nodiscard]] bool EqualCStringExact(
			const char* observed,
			const std::string_view expected) noexcept
		{
			if (!observed)
				return false;
			for (std::size_t index = 0; index < expected.size(); ++index) {
				if (observed[index] != expected[index])
					return false;
			}
			return observed[expected.size()] == '\0';
		}

		[[nodiscard]] Policy::AssetIdentity AssetIdentityFor(
			const FrozenStyleRuntimeContract& contract,
			const std::size_t index) noexcept
		{
			if (!EquippedAssets(contract) ||
				index >= EquippedAssets(contract)->size()) {
				return {};
			}
			const auto& asset = (*EquippedAssets(contract))[index];
			Policy::AssetIdentity identity{};
			identity.approvalIdentity = Mix64(
				HashText(asset.role) ^ std::rotl(HashText(asset.modelPath), 17) ^
				asset.byteLength);
			identity.byteLength = asset.byteLength;
			static_assert(sizeof(identity.sha256Words) == sizeof(asset.sha256));
			std::memcpy(
				identity.sha256Words.data(), asset.sha256.data(), asset.sha256.size());
			return identity;
		}

		[[nodiscard]] Policy::ApertureIdentity ApprovedAperture(
			const FrozenStyleRuntimeContract& contract) noexcept
		{
			return {
				.contractIdentity = contract.apertureContractIdentity,
				.halfWidthBits = std::bit_cast<std::uint32_t>(7.0F),
				.halfHeightBits = std::bit_cast<std::uint32_t>(7.0F),
				.frontClearanceBits = std::bit_cast<std::uint32_t>(1.0F),
				.backClearanceBits = std::bit_cast<std::uint32_t>(2.0F)
			};
		}

		[[nodiscard]] bool IsExactSourceFile(
			const RE::TESForm* form,
			const FrozenStyleRuntimeContract& contract,
			RE::TESFile*& source) noexcept
		{
			source = form ? form->GetFile(0) : nullptr;
			// The consolidated master uses full FormIDs on every runtime.
			const bool expectLight = contract.pluginBasename == kFiligreeV1PluginBasename ? false : !REL::Module::IsVR();
			return source && source->IsLight() == expectLight &&
				source->GetFilename() == contract.pluginBasename &&
				source->IsFormInMod(form->GetFormID());
		}

		[[nodiscard]] Policy::ExactFormIdentity FormIdentity(
			const RE::TESForm* form,
			const FrozenStyleRuntimeContract& contract,
			const std::uint32_t localFormID) noexcept
		{
			if (!form)
				return {};
			const auto runtimeFormID = form->GetFormID();
			return {
				.pluginIdentity = contract.pluginIdentity,
				.localFormID = localFormID,
				.runtimeFormID = runtimeFormID,
				.loadedRecordIdentity = Mix64(
					contract.pluginIdentity ^ runtimeFormID ^
					(static_cast<std::uint64_t>(localFormID) << 32))
			};
		}

		struct ExactForms
		{
			RE::TESObjectARMO* armor{ nullptr };
			RE::TESObjectARMA* armorAddon{ nullptr };
			Policy::ExactFormIdentity armorIdentity{};
			Policy::ExactFormIdentity armorAddonIdentity{};
			bool exact{ false };
		};

		[[nodiscard]] ExactForms ResolveExactFormsUnsafe(
			const FrozenStyleRuntimeContract& contract) noexcept
		{
			ExactForms output{};
			if (!contract.equippedAssets)
				return output;
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data)
				return output;
			auto* armor = data->LookupForm<RE::TESObjectARMO>(
				contract.armorLocalFormID, contract.pluginBasename);
			auto* armorAddon = data->LookupForm<RE::TESObjectARMA>(
				contract.armorAddonLocalFormID, contract.pluginBasename);
			RE::TESFile* armorSource = nullptr;
			RE::TESFile* addonSource = nullptr;
			if (!armor || !armorAddon ||
				!IsExactSourceFile(armor, contract, armorSource) ||
				!IsExactSourceFile(armorAddon, contract, addonSource) ||
				armorSource != addonSource ||
				!TESFileMasterCountUsable(armorSource->masterCount) ||
				armorSource->GetRuntimeFormID(
					TESFileRawSelfFormID(
						armorSource->masterCount, contract.armorLocalFormID)) !=
					armor->GetFormID() ||
				armorSource->GetRuntimeFormID(
					TESFileRawSelfFormID(
						armorSource->masterCount,
						contract.armorAddonLocalFormID)) !=
					armorAddon->GetFormID()) {
				return output;
			}

			const bool modelsExact =
				EqualCStringExact(
					armorAddon->bipedModel1stPersons[RE::SEX::kMale].GetModel(),
					(*contract.equippedAssets)[0].modelPath) &&
				EqualCStringExact(
					armorAddon->bipedModel1stPersons[RE::SEX::kFemale].GetModel(),
					(*contract.equippedAssets)[1].modelPath) &&
				EqualCStringExact(
					armorAddon->bipedModels[RE::SEX::kMale].GetModel(),
					(*contract.equippedAssets)[2].modelPath) &&
				EqualCStringExact(
					armorAddon->bipedModels[RE::SEX::kFemale].GetModel(),
					(*contract.equippedAssets)[3].modelPath);
			if (!modelsExact)
				return output;
			output.armor = armor;
			output.armorAddon = armorAddon;
			output.armorIdentity = FormIdentity(
				armor, contract, contract.armorLocalFormID);
			output.armorAddonIdentity = FormIdentity(
				armorAddon, contract, contract.armorAddonLocalFormID);
			output.exact = Policy::IsValidExactFormIdentity(output.armorIdentity) &&
				Policy::IsValidExactFormIdentity(output.armorAddonIdentity);
			return output;
		}

		[[nodiscard]] bool ValidateFrozenFormsGuarded(
			const FrozenStyleRuntimeContract& contract) noexcept
		{
#if defined(_MSC_VER)
			__try {
#endif
				return ResolveExactFormsUnsafe(contract).exact;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#endif
		}

		[[nodiscard]] bool DescendsFrom(
			const RE::NiAVObject* object,
			const RE::NiAVObject* root) noexcept
		{
			if (!object || !root)
				return false;
			auto* current = object;
			for (std::size_t depth = 0; depth < kMaximumParentDepth; ++depth) {
				if (current == root)
					return true;
				auto* parent = current->parent;
				if (!parent || parent == current)
					return false;
				current = parent;
			}
			return false;
		}

		[[nodiscard]] bool VisibleThrough(
			const RE::NiAVObject* object,
			const RE::NiAVObject* root) noexcept
		{
			if (!object || !root)
				return false;
			auto* current = object;
			for (std::size_t depth = 0; depth < kMaximumParentDepth; ++depth) {
				if (current->GetAppCulled())
					return false;
				if (current == root)
					return true;
				auto* parent = current->parent;
				if (!parent || parent == current)
					return false;
				current = parent;
			}
			return false;
		}

		[[nodiscard]] Policy::PoseWords CopyWorldTransform(
			const RE::NiAVObject* object) noexcept
		{
			Policy::PoseWords words{};
			static_assert(sizeof(RE::NiTransform) == sizeof(words));
			std::memcpy(words.data(), std::addressof(object->world), sizeof(object->world));
			return words;
		}

		[[nodiscard]] bool ExactPaneSchema(
			RE::NiAVObject* partClone,
			RE::NiAVObject*& item,
			RE::NiAVObject*& pane) noexcept
		{
			item = nullptr;
			pane = nullptr;
			if (!partClone)
				return false;
			auto* itemObject = partClone->GetObjectByName(MirrorItemName());
			auto* paneObject = partClone->GetObjectByName(PaneName());
			auto* frameObject = partClone->GetObjectByName(FrameName());
			if (!itemObject || !paneObject || !frameObject ||
				itemObject->parent != partClone || paneObject->parent != itemObject ||
				frameObject->parent != itemObject ||
				!EqualCStringExact(itemObject->name.c_str(), "RRMirrorItem") ||
				!EqualCStringExact(frameObject->name.c_str(), "MirrorFrame") ||
				!EqualCStringExact(paneObject->name.c_str(), "TrueMirror:0")) {
				return false;
			}
			// Preserve the proven direct-child identities even when a later schema
			// or metadata check rejects the candidate.
			item = itemObject;
			pane = paneObject;

			auto* itemNode = itemObject->AsNode();
			auto* paneShape = netimmerse_cast<RE::BSTriShape*>(paneObject);
			auto* frameShape = netimmerse_cast<RE::BSTriShape*>(frameObject);
			if (!itemNode || !paneShape || !frameShape ||
				itemNode->GetExtraDataSize() != 0 ||
				frameShape->GetExtraDataSize() != 0 ||
				paneShape->GetExtraDataSize() != 2 ||
				itemNode->GetControllers() || frameShape->GetControllers() ||
				paneShape->GetControllers() || itemNode->collisionObject ||
				frameShape->collisionObject || paneShape->collisionObject) {
				return false;
			}

			auto& children = itemNode->GetChildren();
			if (children.size() != 2 && children.size() != 3)
				return false;
			RE::NiAVObject* ordered[3]{ nullptr, nullptr, nullptr };
			std::size_t count = 0;
			for (const auto& child : children) {
				if (child && count < 3)
					ordered[count++] = child.get();
			}
			if (count != children.size() || ordered[0] != frameObject || ordered[1] != paneObject)
				return false;
			if (count == 3) {
				// The pinned third-person asset has one ordinary black insert.
				// It belongs to the same item/suppression scope and carries no
				// reflective tag, controller, collision, or independent ownership.
				auto* backing = netimmerse_cast<RE::BSTriShape*>(ordered[2]);
				if (!backing || backing->parent != itemObject ||
					!EqualCStringExact(backing->name.c_str(), "RRMirrorBacking") ||
					backing->GetExtraDataSize() != 0 || backing->GetControllers() ||
					backing->collisionObject) {
					return false;
				}
			}

			auto* schema = netimmerse_cast<RE::NiIntegerExtraData*>(
				paneShape->GetExtraData(SchemaName()));
			auto* aperture = netimmerse_cast<RE::NiFloatsExtraData*>(
				paneShape->GetExtraData(ApertureName()));
			if (!schema || schema->value != 2 || !aperture ||
				aperture->size != 4 || !aperture->value)
				return false;
			const std::uint32_t expected[]{
				std::bit_cast<std::uint32_t>(7.0F),
				std::bit_cast<std::uint32_t>(7.0F),
				std::bit_cast<std::uint32_t>(1.0F),
				std::bit_cast<std::uint32_t>(2.0F)
			};
			for (std::size_t index = 0; index < 4; ++index) {
				if (std::bit_cast<std::uint32_t>(aperture->value[index]) !=
					expected[index]) {
					return false;
				}
			}
			return true;
		}

		void ReadCloneUnsafe(
			RawClone& output,
			RE::BipedAnim* biped,
			RE::BIPOBJECT* shield,
			RE::TESObjectARMO* expectedArmor,
			RE::TESObjectARMA* expectedAddon,
			const FrozenAssetContract& expectedAsset,
			const bool firstPerson) noexcept
		{
			output = {};
			if (!biped || !shield || !biped->root || !shield->partClone)
				return;
			auto* root = biped->root;
			auto* clone = shield->partClone.get();
			output.bipedIdentity = PointerToken(
				biped, firstPerson ? kDomainBipedFirst : kDomainBipedThird);
			output.rootIdentity = PointerToken(
				root, firstPerson ? kDomainRootFirst : kDomainRootThird);
			output.partCloneIdentity = PointerToken(
				clone, firstPerson ? kDomainCloneFirst : kDomainCloneThird);
			output.itemMatches = shield->item == expectedArmor;
			output.addonMatches = shield->addon == expectedAddon;
			output.modelMatches = shield->part &&
				EqualCStringExact(shield->part->GetModel(), expectedAsset.modelPath);
			output.partCloneDescendsRoot = DescendsFrom(clone, root);
			output.partCloneWorld = CopyWorldTransform(clone);
			output.rootWorld = CopyWorldTransform(root);
			output.rootLastUpdated = ReadNodeUpdateStamp(root);
			output.partCloneLastUpdated = ReadNodeUpdateStamp(clone);

			RE::NiAVObject* item = nullptr;
			RE::NiAVObject* pane = nullptr;
			output.exactDirectSchema = ExactPaneSchema(clone, item, pane);
			if (!item || !pane)
				return;
			output.itemDirectChildOfPartClone = item->parent == clone;
			output.paneDirectChildOfItem = pane->parent == item;
			output.itemIdentity = PointerToken(
				item, firstPerson ? kDomainItemFirst : kDomainItemThird);
			output.paneIdentity = PointerToken(
				pane, firstPerson ? kDomainPaneFirst : kDomainPaneThird);
			output.paneWorld = CopyWorldTransform(pane);
			output.itemLastUpdated = ReadNodeUpdateStamp(item);
			output.paneLastUpdated = ReadNodeUpdateStamp(pane);
			output.visibleAndNotAppCulled = VisibleThrough(pane, root);
		}

		/**
		 * One-shot report of both candidate offsets so a future runtime shift is
		 * visible in the log instead of silently starving the content gate.
		 */
		[[nodiscard]] std::uint32_t CurrentGraphicsFrame(
			const RE::BSGraphics::State* a_state) noexcept
		{
			namespace Frame = GraphicsFrameCounterPolicy;
			// Only Anniversary Edition shifts the counter; SE and VR keep 0x4C.
			const bool shifted = REL::Module::IsAE();
			const auto value = Frame::Read(a_state, shifted);
			static std::atomic_bool reported{ false };
			if (!reported.exchange(true, std::memory_order_acq_rel)) {
				try {
					logger::info(
						"[RR][HandMirrorApprovedReadOnly] graphics frame counter offset=0x{:X} value={} plausible={} (other candidate 0x{:X}={})",
						Frame::Offset(shifted), value,
						Frame::IsPlausibleFrameCounter(value),
						Frame::Offset(!shifted), Frame::Read(a_state, !shifted));
				} catch (...) {
				}
			}
			return value;
		}

		void ReadCandidateUnsafe(
			RawCandidate& output,
			RE::BSRenderPass* pass) noexcept
		{
			output = {};
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return;
			output.playerFormID = player->GetFormID();
			output.exactPlayer = output.playerFormID == 0x14;
			output.graphIsBlocking = player->IsBlocking();
			output.actorWantsBlocking =
				static_cast<bool>(player->actorState2.wantBlocking);
			output.weaponDrawn = player->AsActorState()->IsWeaponDrawn();
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				const bool correctedVR = VRRuntimeFixEnabled();
				const bool exactVRState = correctedVR && camera->currentState &&
					camera->currentState == camera->GetVRRuntimeData().cameraStates[
						RE::CameraState::kVR];
				output.perspective = HandMirrorVRRuntimePolicy::AcceptPerspective(
					correctedVR, camera->IsInFirstPerson(), exactVRState) ?
					Policy::Perspective::kFirstPerson :
					Policy::Perspective::kThirdPerson;
			}
			if (auto* graphics = RE::BSGraphics::State::GetSingleton())
				output.graphicsFrame = CurrentGraphicsFrame(graphics);

			const auto& firstOwner = player->GetBiped(true);
			const auto& thirdOwner = player->GetBiped(false);
			auto* firstBiped = firstOwner.get();
			auto* thirdBiped = thirdOwner.get();
			if (!firstBiped || !thirdBiped || firstBiped == thirdBiped)
				return;
			const auto firstHandle = firstBiped->actorRef.native_handle();
			const auto thirdHandle = thirdBiped->actorRef.native_handle();
			if (firstHandle == 0 || firstHandle != thirdHandle)
				return;
			output.playerHandle = firstHandle;

			auto* actorBase = player->GetActorBase();
			const auto sex = actorBase ? actorBase->GetSex() : RE::SEX::kNone;
			const bool female = sex == RE::SEX::kFemale;
			const bool knownSex = sex == RE::SEX::kMale || female;
			const auto firstAssetIndex = female ? 1u : 0u;
			const auto thirdAssetIndex = female ? 3u : 2u;
			auto* firstShield = firstBiped->GetShieldObject();
			auto* thirdShield = thirdBiped->GetShieldObject();

			struct StyleProbe
			{
				const FrozenStyleRuntimeContract* contract{ nullptr };
				ExactForms forms{};
				RawClone first{};
				RawClone third{};
				bool exactPluginFormModelTuple{ false };
			};
			StyleProbe fallback{};
			StyleProbe selected{};
			std::uint32_t exactStyleMatches = 0;
			std::uint32_t exactReadyStyleForms = 0;
			bool approvedArmorInFirstShieldSlot = false;
			bool approvedArmorInThirdShieldSlot = false;
			const auto readyStyleMask =
				g_readyStyleMask.load(std::memory_order_acquire);
			for (const auto& contract : kFrozenStyleContracts) {
				if ((readyStyleMask & StyleBit(contract.index)) == 0 ||
					!contract.equippedAssets) {
					continue;
				}
				StyleProbe probe{};
				probe.contract = &contract;
				probe.forms = ResolveExactFormsUnsafe(contract);
				if (!probe.forms.exact)
					continue;
				++exactReadyStyleForms;
				approvedArmorInFirstShieldSlot =
					approvedArmorInFirstShieldSlot ||
					(firstShield && firstShield->item == probe.forms.armor);
				approvedArmorInThirdShieldSlot =
					approvedArmorInThirdShieldSlot ||
					(thirdShield && thirdShield->item == probe.forms.armor);
				ReadCloneUnsafe(
					probe.first, firstBiped, firstShield, probe.forms.armor,
					probe.forms.armorAddon,
					(*contract.equippedAssets)[firstAssetIndex], true);
				ReadCloneUnsafe(
					probe.third, thirdBiped, thirdShield, probe.forms.armor,
					probe.forms.armorAddon,
					(*contract.equippedAssets)[thirdAssetIndex], false);
				if (!fallback.contract)
					fallback = probe;
				probe.exactPluginFormModelTuple = knownSex &&
					probe.first.itemMatches && probe.third.itemMatches &&
					probe.first.addonMatches && probe.third.addonMatches &&
					probe.first.modelMatches && probe.third.modelMatches;
				if (probe.exactPluginFormModelTuple) {
					++exactStyleMatches;
					selected = probe;
				}
			}

			if (exactStyleMatches > 1) {
				output.styleAmbiguous = true;
				return;
			}
			const StyleProbe& probe = exactStyleMatches == 1 ? selected : fallback;
			if (!probe.contract || !probe.forms.exact)
				return;
			const auto& contract = *probe.contract;
			output.styleIndex = contract.index;
			output.formsExact = true;
			const auto styleValue = static_cast<std::size_t>(contract.index);
			output.contentFilesVerified = styleValue < g_verifiedFileMasks.size() &&
				g_verifiedFileMasks[styleValue].load(std::memory_order_acquire) ==
					contract.allFrozenFilesVerifiedMask;
			output.approved = {
				.frozenContentLockIdentity = contract.frozenContentLockIdentity,
				.styleIdentity = contract.styleIdentity,
				.armor = probe.forms.armorIdentity,
				.armorAddon = probe.forms.armorAddonIdentity,
				.firstPersonModel = AssetIdentityFor(contract, firstAssetIndex),
				.thirdPersonModel = AssetIdentityFor(contract, thirdAssetIndex),
				.mirrorItemSubtreeContractIdentity =
					contract.mirrorItemContractIdentity,
				.paneContractIdentity = contract.paneContractIdentity,
				.aperture = ApprovedAperture(contract)
			};
			output.first = probe.first;
			output.third = probe.third;

			const bool handlesExact = firstBiped->actorRef.native_handle() ==
				player->GetHandle().native_handle() &&
				thirdBiped->actorRef.native_handle() ==
					player->GetHandle().native_handle();
			output.equipmentSlotsConclusive = output.exactPlayer && handlesExact &&
				exactReadyStyleForms != 0;
			output.approvedArmorInFirstShieldSlot =
				approvedArmorInFirstShieldSlot;
			output.approvedArmorInThirdShieldSlot =
				approvedArmorInThirdShieldSlot;
			output.exactTupleEquipped = output.exactPlayer &&
				probe.exactPluginFormModelTuple &&
				handlesExact && output.first.itemMatches &&
				output.third.itemMatches && output.first.addonMatches &&
				output.third.addonMatches;
			output.currentRaceAndSexExact = output.exactTupleEquipped && knownSex &&
				probe.forms.armorAddon->IsValidRace(player->GetRace()) &&
				output.first.modelMatches && output.third.modelMatches;
			if (REL::Module::IsVR()) {
				// VR (VRIK): the body's third-person pane is the exact pane.
				if (pass && pass->geometry && output.third.paneIdentity != 0) {
					output.passIsExactFirstPersonPane = PointerToken(
						pass->geometry, kDomainPaneThird) == output.third.paneIdentity;
				}
			} else if (pass && pass->geometry && output.first.paneIdentity != 0) {
				output.passIsExactFirstPersonPane = PointerToken(
					pass->geometry, kDomainPaneFirst) == output.first.paneIdentity;
			}
		}

		[[nodiscard]] bool ReadCandidatePairGuarded(
			RawCandidate& first,
			RawCandidate& second,
			RE::BSRenderPass* pass = nullptr) noexcept
		{
			first = {};
			second = {};
#if defined(_MSC_VER)
			__try {
#endif
				ReadCandidateUnsafe(first, pass);
				ReadCandidateUnsafe(second, pass);
				g_counters.guardedReads.fetch_add(2, std::memory_order_relaxed);
				return true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				first = {};
				second = {};
				g_counters.guardedReadFaults.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(Policy::ObserverFault::kGuardedReadFault);
				return false;
			}
#endif
		}

		[[nodiscard]] bool SameRawCandidate(
			const RawCandidate& left,
			const RawCandidate& right) noexcept
		{
			// IsBlocking/wantBlocking are sampled only for telemetry.  They are
			// asynchronous gameplay state and cannot invalidate a bit-exact clone,
			// pane, pass, receipt, or content identity pair.
			return left.approved == right.approved && left.first == right.first &&
				left.third == right.third && left.styleIndex == right.styleIndex &&
				left.perspective == right.perspective &&
				left.graphicsFrame == right.graphicsFrame &&
				left.playerFormID == right.playerFormID &&
				left.playerHandle == right.playerHandle &&
				left.formsExact == right.formsExact &&
				left.exactPlayer == right.exactPlayer &&
				left.equipmentSlotsConclusive ==
					right.equipmentSlotsConclusive &&
				left.approvedArmorInFirstShieldSlot ==
					right.approvedArmorInFirstShieldSlot &&
				left.approvedArmorInThirdShieldSlot ==
					right.approvedArmorInThirdShieldSlot &&
				left.exactTupleEquipped == right.exactTupleEquipped &&
				left.currentRaceAndSexExact == right.currentRaceAndSexExact &&
				left.passIsExactFirstPersonPane ==
					right.passIsExactFirstPersonPane &&
				left.contentFilesVerified == right.contentFilesVerified &&
				left.styleAmbiguous == right.styleAmbiguous;
		}

		[[nodiscard]] bool ApplyLifecycle(
			const Policy::LifecycleEvent event) noexcept
		{
			const auto result = Policy::ApplyLifecycleEvent(g_thread.lifecycle, event);
			g_thread.lifecycle = result.next;
			if (g_thread.lifecycle.terminalFailStop) {
				LatchFault(g_thread.lifecycle.firstFault);
				return false;
			}
			return result.status == Policy::LifecycleTransitionStatus::kAdvanced;
		}

		[[nodiscard]] bool RefreshLifecycle(const RawCandidate& raw) noexcept
		{
			const auto global = g_globalLifecycleSerial.load(std::memory_order_acquire);
			const auto equipWake = g_equipWakeSerial.load(std::memory_order_acquire);
			if (!g_thread.lifecycleInitialized) {
				g_thread.lifecycle = {};
				g_thread.lifecycle.generations.lifecycle = global;
				g_thread.lifecycle.generations.equip = equipWake;
				g_thread.lifecycle.perspective = raw.perspective;
				g_thread.lifecycle.exactApprovedArmorEquipped =
					raw.exactTupleEquipped;
				g_thread.globalLifecycleSeen = global;
				g_thread.equipWakeSeen = equipWake;
				g_thread.lifecycleInitialized = true;
			} else {
				if (global != g_thread.globalLifecycleSeen) {
					if (!ApplyLifecycle(Policy::LifecycleEvent::kGameLoadInvalidation))
						return false;
					g_thread.globalLifecycleSeen = global;
					g_thread.previousRawValid = false;
				}
				if (equipWake != g_thread.equipWakeSeen) {
					if (!ApplyLifecycle(raw.exactTupleEquipped ?
							Policy::LifecycleEvent::kEquipExactApprovedArmor :
							Policy::LifecycleEvent::kUnequip)) {
						return false;
					}
					g_thread.equipWakeSeen = equipWake;
				}
				if (g_thread.lifecycle.perspective != raw.perspective &&
					raw.perspective != Policy::Perspective::kUnknown) {
					if (!ApplyLifecycle(raw.perspective ==
							Policy::Perspective::kFirstPerson ?
							Policy::LifecycleEvent::kPointOfViewFirstPerson :
							Policy::LifecycleEvent::kPointOfViewThirdPerson)) {
						return false;
					}
				}
				if (g_thread.previousRawValid) {
					if (raw.first.rootIdentity !=
							g_thread.previousRaw.first.rootIdentity &&
						!ApplyLifecycle(
							Policy::LifecycleEvent::kFirstPersonRootRebuilt)) {
						return false;
					}
					if (raw.first.partCloneIdentity !=
							g_thread.previousRaw.first.partCloneIdentity &&
						!ApplyLifecycle(
							Policy::LifecycleEvent::kFirstPersonCloneReplaced)) {
						return false;
					}
					if (raw.third.rootIdentity !=
							g_thread.previousRaw.third.rootIdentity &&
						!ApplyLifecycle(
							Policy::LifecycleEvent::kThirdPersonRootRebuilt)) {
						return false;
					}
					if (raw.third.partCloneIdentity !=
							g_thread.previousRaw.third.partCloneIdentity &&
						!ApplyLifecycle(
							Policy::LifecycleEvent::kThirdPersonCloneReplaced)) {
						return false;
					}
				}
				if (raw.exactTupleEquipped !=
					g_thread.lifecycle.exactApprovedArmorEquipped &&
					!ApplyLifecycle(raw.exactTupleEquipped ?
							Policy::LifecycleEvent::kEquipExactApprovedArmor :
							Policy::LifecycleEvent::kUnequip)) {
					return false;
				}
			}
			g_thread.previousRaw = raw;
			g_thread.previousRawValid = true;
			return Policy::IsValidLifecycleState(g_thread.lifecycle);
		}

		[[nodiscard]] Policy::CloneObservation BuildCloneObservation(
			const RawCandidate& raw,
			const RawClone& clone,
			const Policy::Perspective perspective,
			const Policy::AssetIdentity& expectedAsset) noexcept
		{
			const auto* contract = StyleContract(raw.styleIndex);
			const bool exactModelVerified = clone.modelMatches &&
				raw.contentFilesVerified && contract;
			return {
				.perspective = perspective,
				.generations = g_thread.lifecycle.generations,
				.observedArmor = clone.itemMatches ? raw.approved.armor :
					Policy::ExactFormIdentity{},
				.observedArmorAddon = clone.addonMatches ? raw.approved.armorAddon :
					Policy::ExactFormIdentity{},
				.observedModel = exactModelVerified ? expectedAsset :
					Policy::AssetIdentity{},
				.perspectiveRootIdentity = clone.rootIdentity,
				.bipedIdentity = clone.bipedIdentity,
				.partCloneIdentity = clone.partCloneIdentity,
				.mirrorItemSubtreeIdentity = clone.itemIdentity,
				.paneIdentity = clone.paneIdentity,
				.mirrorItemSubtreeContractIdentity =
					clone.exactDirectSchema && contract ?
						contract->mirrorItemContractIdentity : 0,
				.paneContractIdentity = clone.exactDirectSchema && contract ?
					contract->paneContractIdentity : 0,
				.aperture = clone.exactDirectSchema && contract ?
					ApprovedAperture(*contract) :
					Policy::ApertureIdentity{},
				.exactGameplayClone = raw.exactPlayer && clone.itemMatches &&
					clone.addonMatches && exactModelVerified,
				.partCloneDescendsPerspectiveRoot =
					clone.partCloneDescendsRoot,
				.mirrorItemDescendsPartClone =
					clone.itemDirectChildOfPartClone,
				.paneDescendsMirrorItem = clone.paneDirectChildOfItem,
				.noInventoryMenuShadowOrDisplayClone = raw.exactPlayer &&
					raw.playerHandle != 0,
				.noEnginePointerRetained = true
			};
		}

		[[nodiscard]] Policy::ApprovedCandidateEvidence BuildCandidateEvidence(
			const RawCandidate& raw) noexcept
		{
			const auto* contract = StyleContract(raw.styleIndex);
			if (!contract)
				return {};
			const bool female = raw.approved.firstPersonModel ==
				AssetIdentityFor(*contract, 1);
			const auto firstAsset = AssetIdentityFor(
				*contract, female ? 1u : 0u);
			const auto thirdAsset = AssetIdentityFor(
				*contract, female ? 3u : 2u);
			return {
				.approvedContent = raw.approved,
				.generations = g_thread.lifecycle.generations,
				.shieldSlotNumber = Policy::kShieldSlotNumber,
				.firstPersonClone = BuildCloneObservation(
					raw, raw.first, Policy::Perspective::kFirstPerson, firstAsset),
				.thirdPersonClone = BuildCloneObservation(
					raw, raw.third, Policy::Perspective::kThirdPerson, thirdAsset),
				.resolvedFromFrozenFirstPartyContentLock =
					raw.formsExact && raw.contentFilesVerified,
				.exactArmorWornAndEquippedAtShieldSlot = raw.exactTupleEquipped,
				.armorAddonMatchesArmorCurrentRaceAndSex =
					raw.currentRaceAndSexExact,
				.equipStateReacquiredAfterWakeup = true,
				.publicOrThirdPartyRegistrationUsed = false,
				.noEnginePointerRetained = true
			};
		}

		[[nodiscard]] bool IssueToken(
			Policy::TokenIssuerState& issuer,
			std::uint64_t& output) noexcept
		{
			const auto result = Policy::IssueToken(issuer);
			issuer = result.next;
			if (result.status != Policy::TokenIssueStatus::kIssued) {
				output = 0;
				LatchFault(Policy::ObserverFault::kTokenExhausted);
				return false;
			}
			output = result.token;
			return true;
		}

		[[nodiscard]] Policy::PoseSnapshot BuildPoseSnapshot(
			const Policy::PosePhase phase,
			const Policy::CandidateBinding& binding,
			const Policy::PoseWords& words) noexcept
		{
			std::uint64_t token = 0;
			if (!IssueToken(g_thread.snapshotIssuer, token))
				return {};
			return {
				.snapshotToken = token,
				.sourceFrame = g_thread.sourceFrame,
				.phase = phase,
				.binding = binding,
				.paneWorldTransformBits = words,
				.copiedCoherently = true,
				.noEnginePointerRetained = true
			};
		}

		[[nodiscard]] std::uint64_t ViewportToken(
			const D3D11_VIEWPORT& viewport) noexcept
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(&viewport);
			std::uint64_t hash = kFNVOffset;
			for (std::size_t index = 0; index < sizeof(viewport); ++index) {
				hash ^= bytes[index];
				hash *= kFNVPrime;
			}
			return Mix64(hash);
		}

		template <class T>
		[[nodiscard]] bool ReleaseCOMGuarded(T*& slot) noexcept
		{
			T* const value = slot;
			// Clear first: an SEH in Release can never leave a retryable retained
			// value or permit a later double-release.
			slot = nullptr;
			if (!value)
				return true;
#if defined(_MSC_VER)
			__try {
#endif
				value->Release();
				return true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#endif
		}

		[[nodiscard]] bool ReleaseTargetScratchGuarded(
			TargetCOMScratch& scratch) noexcept
		{
			bool allReleased = true;
			for (auto*& buffer : scratch.soTargets)
				allReleased = ReleaseCOMGuarded(buffer) && allReleased;
			for (auto*& view : scratch.uavInspectionViews)
				allReleased = ReleaseCOMGuarded(view) && allReleased;
			for (auto*& view : scratch.uavInspectionRTVs)
				allReleased = ReleaseCOMGuarded(view) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.predicate) && allReleased;
			allReleased =
				ReleaseCOMGuarded(scratch.uavInspectionDevice) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.depthDevice) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.colorDevice) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.contextDevice) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.depthResource) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.colorResource) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.depth) && allReleased;
			allReleased = ReleaseCOMGuarded(scratch.color) && allReleased;
			if (!allReleased) {
				LatchFault(Policy::ObserverFault::kCallbackFault);
				return false;
			}
			return true;
		}

		[[nodiscard]] bool ProveNoGraphicsUAVBound(
			ID3D11DeviceContext* context,
			TargetCOMScratch& scratch) noexcept
		{
			if (!context)
				return false;
			context->GetDevice(&scratch.uavInspectionDevice);
			if (!scratch.uavInspectionDevice)
				return false;
			const UINT slotCount =
				scratch.uavInspectionDevice->GetFeatureLevel() >=
					D3D_FEATURE_LEVEL_11_1 ?
					D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
			context->OMGetRenderTargets(
				static_cast<UINT>(scratch.uavInspectionRTVs.size()),
				scratch.uavInspectionRTVs.data(), nullptr);
			UINT firstUAVSlot = 0;
			for (std::size_t index = 0;
				 index < scratch.uavInspectionRTVs.size(); ++index) {
				if (scratch.uavInspectionRTVs[index])
					firstUAVSlot = static_cast<UINT>(index + 1u);
			}
			if (firstUAVSlot >= slotCount)
				return true;
			const UINT viewCount = slotCount - firstUAVSlot;
			context->OMGetRenderTargetsAndUnorderedAccessViews(
				0, nullptr, nullptr, firstUAVSlot, viewCount,
				scratch.uavInspectionViews.data());
			for (UINT index = 0; index < viewCount; ++index) {
				if (scratch.uavInspectionViews[index])
					return false;
			}
			return true;
		}

		[[nodiscard]] bool ReadMainTargetUnsafe(
			TargetRead& output,
			TargetCOMScratch& scratch) noexcept
		{
			output = {};
			scratch = {};
			auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!shadowState || !renderer)
				return false;
			const auto& flat = shadowState->GetRuntimeData();
			const auto colorIndex = static_cast<std::uint32_t>(flat.renderTargets[0]);
			const auto depthIndex = flat.depthStencil;
			const auto depthSlice = flat.depthStencilSlice;
			if (colorIndex >= static_cast<std::uint32_t>(RE::RENDER_TARGETS::kTOTAL) ||
				depthIndex >= static_cast<std::uint32_t>(
					RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL) ||
				depthSlice >= 8) {
				return false;
			}
			auto& rendererData = renderer->GetRuntimeData();
			auto& depthData = renderer->GetDepthStencilData();
			auto* expectedColor = rendererData.renderTargets[colorIndex].RTV;
			auto* expectedDepth = depthData.depthStencils[depthIndex].views[depthSlice];
			auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData.context);
			auto* exactDevice = reinterpret_cast<ID3D11Device*>(
				RE::BSGraphics::Renderer::GetDevice());
			if (!expectedColor || !expectedDepth || !context || !exactDevice)
				return false;

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			BOOL predicateValue = FALSE;
			bool success = false;
			// Query the bound RTV/DSV legally, then reuse the repository's
			// fail-closed FL11.0/11.1-aware full graphics-UAV inspector.
			context->OMGetRenderTargets(1, &scratch.color, &scratch.depth);
			const bool noUAV = ProveNoGraphicsUAVBound(context, scratch);
			context->RSGetViewports(&viewportCount, &viewport);
			context->GetPredication(&scratch.predicate, &predicateValue);
			context->SOGetTargets(
				static_cast<UINT>(scratch.soTargets.size()),
				scratch.soTargets.data());
			if (scratch.color && scratch.depth && viewportCount == 1) {
				scratch.color->GetResource(&scratch.colorResource);
				scratch.depth->GetResource(&scratch.depthResource);
				context->GetDevice(&scratch.contextDevice);
				if (scratch.colorResource)
					scratch.colorResource->GetDevice(&scratch.colorDevice);
				if (scratch.depthResource)
					scratch.depthResource->GetDevice(&scratch.depthDevice);
				D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription{};
				scratch.depth->GetDesc(&depthDescription);
				const bool viewportFinite = Policy::IsFinite(viewport.Width) &&
					Policy::IsFinite(viewport.Height) &&
					Policy::IsFinite(viewport.MinDepth) &&
					Policy::IsFinite(viewport.MaxDepth) && viewport.Width > 0.0F &&
					viewport.Height > 0.0F &&
					viewport.Width <= static_cast<float>(UINT32_MAX) &&
					viewport.Height <= static_cast<float>(UINT32_MAX);
				if (scratch.colorResource && scratch.depthResource &&
					scratch.contextDevice && scratch.colorDevice &&
					scratch.depthDevice && viewportFinite) {
					output.identity = {
						.deviceIdentity = PointerToken(exactDevice, kDomainDevice),
						.colorViewIdentity = PointerToken(
							scratch.color, kDomainColorView),
						.colorResourceIdentity = PointerToken(
							scratch.colorResource, kDomainColorResource),
						.depthViewIdentity = PointerToken(
							scratch.depth, kDomainDepthView),
						.depthResourceIdentity = PointerToken(
							scratch.depthResource, kDomainDepthResource),
						.viewportIdentity = ViewportToken(viewport),
						.viewportWidth = static_cast<std::uint32_t>(viewport.Width),
						.viewportHeight = static_cast<std::uint32_t>(viewport.Height),
						.minimumDepth = viewport.MinDepth,
						.maximumDepth = viewport.MaxDepth
					};
					output.colorWritable = scratch.color == expectedColor;
					output.depthWritable = scratch.depth == expectedDepth &&
						(depthDescription.Flags & D3D11_DSV_READ_ONLY_DEPTH) == 0;
					output.viewportMatchesShadowState =
						std::memcmp(&viewport, &flat.viewPort, sizeof(viewport)) == 0;
					output.contextAndTargetsExactDevice =
						EngineDeviceIdentityPolicy::
							ClassifyMainTargetDeviceOwnership(
								{ .engineDevice = reinterpret_cast<std::uintptr_t>(
									  exactDevice),
									.contextDevice = reinterpret_cast<std::uintptr_t>(
										scratch.contextDevice),
									.targetDevices = {
										reinterpret_cast<std::uintptr_t>(
											scratch.colorDevice),
										reinterpret_cast<std::uintptr_t>(
											scratch.depthDevice) },
									.targetDeviceCount = 2 },
								EngineDeviceIdentity::ForwardingDeviceAccepted()) !=
						EngineDeviceIdentityPolicy::MainTargetDeviceDisposition::
							kReject;
					bool noSO = true;
					for (const auto* buffer : scratch.soTargets)
						noSO = noSO && buffer == nullptr;
					output.noOutputConflict =
						noUAV && noSO && scratch.predicate == nullptr;
					output.noMainResourceAlias =
						scratch.colorResource != scratch.depthResource;
					success = Policy::IsValidMainTargetIdentity(output.identity);
				}
			}

			output.completed = success;
			return success;
		}

		[[nodiscard]] bool ReadMainTargetGuarded(TargetRead& output) noexcept
		{
			output = {};
			TargetCOMScratch scratch{};
			bool completed = false;
#if defined(_MSC_VER)
			__try {
#endif
				completed = ReadMainTargetUnsafe(output, scratch);
				if (completed) {
					g_counters.mainTargetReads.fetch_add(
						1, std::memory_order_relaxed);
				}
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				output.faulted = true;
				completed = false;
				g_counters.mainTargetReadFaults.fetch_add(
					1, std::memory_order_relaxed);
				LatchFault(Policy::ObserverFault::kGuardedReadFault);
			}
#endif
			// Every OM/SO/predication query returns AddRef'd interfaces.  Cleanup
			// happens after both normal return and SEH recovery; only hashed value
			// identities survive this function.
			if (!ReleaseTargetScratchGuarded(scratch)) {
				output = {};
				output.faulted = true;
				return false;
			}
			return completed;
		}

		void RecordGateResult(const Policy::ReadOnlyGateResult& result) noexcept
		{
			g_lastGateStatus.store(
				static_cast<std::uint8_t>(result.status), std::memory_order_release);
			g_counters.gateEvaluations.fetch_add(1, std::memory_order_relaxed);
			if (result.ReadOnlyEvidenceComplete()) {
				g_counters.admissions.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_counters.ordinaryRejects.fetch_add(1, std::memory_order_relaxed);
			}
			const auto ordinal = static_cast<std::uint8_t>(result.status);
			if (ordinal < 64) {
				const std::uint64_t bit = UINT64_C(1) << ordinal;
				if ((g_thread.seenGateStatuses & bit) == 0) {
					g_thread.seenGateStatuses |= bit;
					g_thread.telemetry = Policy::ApplyGateResult(
						g_thread.telemetry, result);
				}
			}
			if (g_thread.telemetry.terminalFailStop) {
				LatchFault(g_thread.telemetry.firstFault);
			}
		}

		[[nodiscard]] const char* GateStatusName(
			const Policy::ReadOnlyGateStatus status) noexcept
		{
			switch (status) {
			case Policy::ReadOnlyGateStatus::kAdmittedReadOnlyEvidenceOnly:
				return "admitted-read-only-evidence-only";
			case Policy::ReadOnlyGateStatus::kAlreadyFailStopped:
				return "already-fail-stopped";
			case Policy::ReadOnlyGateStatus::kCandidateOverflowFault:
				return "candidate-overflow-fault";
			case Policy::ReadOnlyGateStatus::kNoCandidateDark:
				return "no-candidate-dark";
			case Policy::ReadOnlyGateStatus::kAmbiguousCandidatesDark:
				return "ambiguous-candidates-dark";
			case Policy::ReadOnlyGateStatus::kApprovedCandidateInvalid:
				return "approved-candidate-invalid";
			case Policy::ReadOnlyGateStatus::kLifecycleOrPointOfViewInvalid:
				return "lifecycle-or-pov-invalid";
			case Policy::ReadOnlyGateStatus::kPostWorldPoseInvalid:
				return "post-world-pose-invalid";
			case Policy::ReadOnlyGateStatus::kFirstPersonPaneDrawInvalid:
				return "first-person-pane-draw-invalid";
			case Policy::ReadOnlyGateStatus::kPoseNotBitExact:
				return "pose-not-bit-exact";
			case Policy::ReadOnlyGateStatus::kVisiblePaneClearanceRejected:
				return "visible-pane-clearance-rejected";
			case Policy::ReadOnlyGateStatus::kHiddenThirdPersonPoseStale:
				return "hidden-third-person-pose-stale";
			case Policy::ReadOnlyGateStatus::kMainTargetInvalid:
				return "main-target-invalid";
			case Policy::ReadOnlyGateStatus::kTargetAliasFault:
				return "target-alias-fault";
			case Policy::ReadOnlyGateStatus::kObserverFaultEvidence:
				return "observer-fault-evidence";
			case Policy::ReadOnlyGateStatus::kObservationTokenInvalid:
				return "observation-token-invalid";
			default:
				return "unknown";
			}
		}

		[[nodiscard]] const char* FreshnessStatusName(
			const Freshness::FreshnessStatus status) noexcept
		{
			switch (status) {
			case Freshness::FreshnessStatus::kRejectedInvalid:
				return "rejected-invalid";
			case Freshness::FreshnessStatus::kRejectedUnsupportedPredecessor:
				return "rejected-unsupported-predecessor";
			case Freshness::FreshnessStatus::kDirectCurrent:
				return "direct-current";
			case Freshness::FreshnessStatus::kPredecessorSeedDark:
				return "predecessor-seed-dark";
			case Freshness::FreshnessStatus::kPredecessorSeedRereadDark:
				return "predecessor-seed-reread-dark";
			case Freshness::FreshnessStatus::kPredecessorCalibrated:
				return "predecessor-calibrated";
			case Freshness::FreshnessStatus::kPredecessorCalibratedReread:
				return "predecessor-calibrated-reread";
			case Freshness::FreshnessStatus::kPredecessorResetDark:
				return "predecessor-reset-dark";
			default:
				return "unknown";
			}
		}

		void RecordBlockingSignals(const RawCandidate& raw) noexcept
		{
			g_lastGraphIsBlocking.store(
				raw.graphIsBlocking, std::memory_order_relaxed);
			g_lastActorWantsBlocking.store(
				raw.actorWantsBlocking, std::memory_order_relaxed);
			if (raw.graphIsBlocking) {
				g_counters.blockingGraphTrue.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (raw.actorWantsBlocking) {
				g_counters.blockingWantTrue.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (raw.actorWantsBlocking && raw.weaponDrawn && !raw.graphIsBlocking) {
				static std::atomic<std::uint32_t> refusalLogBudget{ 12 };
				static std::atomic<std::uint32_t> refusalLogCadence{ 0 };
				const auto tick = refusalLogCadence.fetch_add(1, std::memory_order_relaxed);
				auto budget = refusalLogBudget.load(std::memory_order_acquire);
				if ((tick % 600) == 0 && budget != 0 &&
					refusalLogBudget.compare_exchange_strong(
						budget, budget - 1, std::memory_order_acq_rel)) {
					try {
						auto* const player = RE::PlayerCharacter::GetSingleton();
						bool attacking = false;
						bool bashing = false;
						bool synced = false;
						bool blockingVar = false;
						std::int32_t state = -1;
						if (player) {
							(void)player->GetGraphVariableBool("IsAttacking", attacking);
							(void)player->GetGraphVariableBool("IsBashing", bashing);
							(void)player->GetGraphVariableBool("bIsSynced", synced);
							(void)player->GetGraphVariableBool("IsBlocking", blockingVar);
							(void)player->GetGraphVariableInt("iState", state);
						}
						logger::info(
							"[RR][HandMirrorApprovedReadOnly] block input held with weapon drawn but the graph is not blocking: weaponState={} IsBlocking={} IsAttacking={} IsBashing={} bIsSynced={} iState={} sneaking={} sprinting={} sitSleep={}",
							player ? static_cast<int>(player->AsActorState()->GetWeaponState()) : -1,
							blockingVar, attacking, bashing, synced, state,
							player ? player->AsActorState()->IsSneaking() : false,
							player ? player->AsActorState()->IsSprinting() : false,
							player ? static_cast<int>(player->AsActorState()->GetSitSleepState()) : -1);
					} catch (...) {
					}
				}
			}
			if (raw.graphIsBlocking != raw.actorWantsBlocking) {
				g_counters.blockingSignalDisagreements.fetch_add(
					1, std::memory_order_relaxed);
			}
		}

		void RecordPreFreshnessRejects(
			const RuntimePreFreshnessRejectMask rejects) noexcept
		{
			g_lastPreFreshnessRejectMask.store(
				rejects, std::memory_order_relaxed);
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kReceiptInvalid)) {
				g_counters.preReceiptInvalidRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects,
					RuntimePreFreshnessReject::kGraphicsFrameMismatch)) {
				g_counters.preGraphicsFrameMismatchRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kExactTuple)) {
				g_counters.preExactTupleRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kCurrentRaceAndSex)) {
				g_counters.preRaceSexRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects,
					RuntimePreFreshnessReject::kFirstPersonPerspective)) {
				g_counters.prePerspectiveRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kFirstPersonSchema)) {
				g_counters.preFirstSchemaRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kThirdPersonSchema)) {
				g_counters.preThirdSchemaRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects,
					RuntimePreFreshnessReject::kVisibleFirstPersonPane)) {
				g_counters.preVisiblePaneRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects,
					RuntimePreFreshnessReject::kExactFirstPersonPanePass)) {
				g_counters.preExactPassRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kOwnerIdentity)) {
				g_counters.preOwnerIdentityRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kCandidateRead)) {
				g_counters.preCandidateReadRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects,
					RuntimePreFreshnessReject::kCandidatePairMismatch)) {
				g_counters.preCandidatePairMismatchRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			if (HasRuntimePreFreshnessReject(
					rejects, RuntimePreFreshnessReject::kLifecycleRefresh)) {
				g_counters.preLifecycleRefreshRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
		}

		void RecordFreshnessDecision(
			const Freshness::FrameReceipt& receipt,
			const Freshness::FreshnessDecision& decision) noexcept
		{
			g_lastReceiptSource.store(
				receipt.sourceSequence, std::memory_order_relaxed);
			g_lastReceiptMainView.store(
				receipt.mainViewFrame, std::memory_order_relaxed);
			g_lastReceiptGraphics.store(
				receipt.graphicsFrame, std::memory_order_relaxed);
			g_lastFreshnessStatus.store(
				static_cast<std::uint8_t>(decision.status),
				std::memory_order_relaxed);
			switch (decision.status) {
			case Freshness::FreshnessStatus::kDirectCurrent:
				g_counters.freshnessDirectCurrent.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case Freshness::FreshnessStatus::kPredecessorSeedDark:
			case Freshness::FreshnessStatus::kPredecessorSeedRereadDark:
				g_counters.freshnessSeedsDark.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case Freshness::FreshnessStatus::kPredecessorCalibrated:
				g_counters.freshnessCalibrations.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case Freshness::FreshnessStatus::kPredecessorCalibratedReread:
				g_counters.freshnessCalibratedRereads.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case Freshness::FreshnessStatus::kPredecessorResetDark:
				g_counters.freshnessResetsDark.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case Freshness::FreshnessStatus::kRejectedInvalid:
			case Freshness::FreshnessStatus::kRejectedUnsupportedPredecessor:
			default:
				g_counters.freshnessRejects.fetch_add(
					1, std::memory_order_relaxed);
				break;
			}
		}

		struct RuntimeRetainedScene
		{
			RuntimeBorrowedScene borrowed{};
			std::array<RE::NiAVObject*, 9> retained{};
			std::size_t retainedCount{ 0 };
		};

		[[nodiscard]] bool RetainRuntimeObject(
			RuntimeRetainedScene& scene, RE::NiAVObject* object) noexcept
		{
			if (!object || scene.retainedCount >= scene.retained.size())
				return false;
#if defined(_MSC_VER)
			__try {
#endif
				object->IncRefCount();
				scene.retained[scene.retainedCount++] = object;
				return true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				// IncRef completion is ambiguous after a fault. Fail-stop and never
				// retry or release the ambiguous slot.
				LatchFault(Policy::ObserverFault::kCallbackFault);
				g_counters.runtimeBorrowFaults.fetch_add(
					1, std::memory_order_relaxed);
				return false;
			}
#endif
		}

		[[nodiscard]] bool ReleaseRuntimeScene(
			RuntimeRetainedScene& scene) noexcept
		{
			bool released = true;
			while (scene.retainedCount != 0) {
				auto* object = scene.retained[--scene.retainedCount];
				scene.retained[scene.retainedCount] = nullptr;
#if defined(_MSC_VER)
				__try {
#endif
					object->DecRefCount();
#if defined(_MSC_VER)
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					released = false;
					LatchFault(Policy::ObserverFault::kCallbackFault);
					g_counters.runtimeBorrowFaults.fetch_add(
						1, std::memory_order_relaxed);
				}
#endif
			}
			scene.borrowed = {};
			return released;
		}

		[[nodiscard]] bool ResolveAndRetainRuntimeSceneUnsafe(
			const RawCandidate& raw,
			RuntimeRetainedScene& scene) noexcept
		{
			scene = {};
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* contract = StyleContract(raw.styleIndex);
			if (!player || !contract)
				return false;
			const auto forms = ResolveExactFormsUnsafe(*contract);
			if (!forms.exact || !forms.armor || !forms.armorAddon)
				return false;
			auto* firstBiped = player->GetBiped(true).get();
			auto* thirdBiped = player->GetBiped(false).get();
			auto* firstShield = firstBiped ? firstBiped->GetShieldObject() : nullptr;
			auto* thirdShield = thirdBiped ? thirdBiped->GetShieldObject() : nullptr;
			auto* firstRoot = firstBiped ? firstBiped->root : nullptr;
			auto* thirdRoot = thirdBiped ? thirdBiped->root : nullptr;
			auto* thirdRenderRoot = player->Get3D1(false);
			auto* firstClone = firstShield && firstShield->partClone ?
				firstShield->partClone.get() : nullptr;
			auto* thirdClone = thirdShield && thirdShield->partClone ?
				thirdShield->partClone.get() : nullptr;
			RE::NiAVObject* firstItem = nullptr;
			RE::NiAVObject* firstPane = nullptr;
			RE::NiAVObject* thirdItem = nullptr;
			RE::NiAVObject* thirdPane = nullptr;
			if (!firstShield || !thirdShield ||
				firstShield->item != forms.armor ||
				thirdShield->item != forms.armor ||
				firstShield->addon != forms.armorAddon ||
				thirdShield->addon != forms.armorAddon ||
				!ExactPaneSchema(firstClone, firstItem, firstPane) ||
				!ExactPaneSchema(thirdClone, thirdItem, thirdPane) ||
				PointerToken(firstRoot, kDomainRootFirst) != raw.first.rootIdentity ||
				PointerToken(thirdRoot, kDomainRootThird) != raw.third.rootIdentity ||
				PointerToken(firstClone, kDomainCloneFirst) !=
					raw.first.partCloneIdentity ||
				PointerToken(thirdClone, kDomainCloneThird) !=
					raw.third.partCloneIdentity ||
				PointerToken(firstItem, kDomainItemFirst) != raw.first.itemIdentity ||
				PointerToken(thirdItem, kDomainItemThird) != raw.third.itemIdentity ||
				PointerToken(firstPane, kDomainPaneFirst) != raw.first.paneIdentity ||
				PointerToken(thirdPane, kDomainPaneThird) != raw.third.paneIdentity) {
				return false;
			}

			RE::NiAVObject* values[]{ firstRoot, firstClone, firstItem, firstPane,
				thirdRoot, thirdClone, thirdItem, thirdPane, thirdRenderRoot };
			for (auto* value : values) {
				if (!RetainRuntimeObject(scene, value)) {
					(void)ReleaseRuntimeScene(scene);
					return false;
				}
			}
			scene.borrowed = { firstRoot, firstClone, firstItem, firstPane,
				thirdRoot, thirdClone, thirdItem, thirdPane, thirdRenderRoot,
				forms.armor, forms.armorAddon };
			return true;
		}

		[[nodiscard]] HandMirrorContentRuntimePolicy::AuthoredSurfaceIdentity
		BuildRuntimeAuthoredSurface(const RawCandidate& raw) noexcept
		{
			const auto* contract = StyleContract(raw.styleIndex);
			if (!contract)
				return {};
			return {
				.contentApprovalIdentity = contract->frozenContentLockIdentity,
				.styleIdentity = contract->styleIdentity,
				.mirrorItemSubtreeContractIdentity =
					contract->mirrorItemContractIdentity,
				.paneContractIdentity = contract->paneContractIdentity,
				.apertureContractIdentity = contract->apertureContractIdentity,
				.paneSchema = MirrorAuthoringContract::SchemaVersion::kRectangularV2
			};
		}

		[[nodiscard]] std::uint64_t StablePairGeneration(
			const std::uint64_t first, const std::uint64_t third) noexcept
		{
			return Mix64(first ^ std::rotl(third, 29));
		}

		[[nodiscard]] Freshness::HiddenNodeFrames HiddenFrames(
			const RawCandidate& raw) noexcept
		{
			return {
				raw.third.rootLastUpdated,
				raw.third.partCloneLastUpdated,
				raw.third.itemLastUpdated,
				raw.third.paneLastUpdated
			};
		}

		[[nodiscard]] Freshness::ContinuityIdentity FreshnessContinuity(
			const RawCandidate& raw,
			const HandMirrorContentRuntimePolicy::HandMirrorOwnerIdentity& owner) noexcept
		{
			const auto& lifecycle = g_thread.lifecycle.generations;
			return {
				.lifecycleGeneration = lifecycle.lifecycle,
				.equipGeneration = lifecycle.equip,
				.firstPersonRootGeneration = lifecycle.firstPersonRoot,
				.thirdPersonRootGeneration = lifecycle.thirdPersonRoot,
				.firstPersonCloneGeneration = lifecycle.firstPersonClone,
				.thirdPersonCloneGeneration = lifecycle.thirdPersonClone,
				.playerIdentity = Mix64(
					(static_cast<std::uint64_t>(raw.playerFormID) << 32) |
					raw.playerHandle),
				.armorIdentity = owner.armorBase,
				.armorAddonIdentity = owner.armorAddonBase,
				.hiddenRootIdentity = raw.third.rootIdentity,
				.hiddenCloneIdentity = raw.third.partCloneIdentity,
				.hiddenItemIdentity = raw.third.itemIdentity,
				.hiddenPaneIdentity = raw.third.paneIdentity
			};
		}

		[[nodiscard]] Freshness::RuntimeKind FreshnessRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			if (REL::Module::IsSE() && version == kSupportedSE)
				return Freshness::RuntimeKind::kSkyrimSE1597;
			if (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version))
				return Freshness::RuntimeKind::kSkyrimAE161170;
			// VR 1.4.15 keeps the SE frame structure (RenderPlayerView ->
			// RenderWorld -> RenderFirstPersonView), so the receipt policy admits
			// it like the flat runtimes.
			if (SupportedRuntimePolicy::IsExactVRRuntime())
				return Freshness::RuntimeKind::kSkyrimVR1415;
			return Freshness::RuntimeKind::kUnsupported;
		}

		// Private live-authority evidence.  The public/offline RaisedPose policy is
		// intentionally left intact for its unwired bridge foundations; the live
		// observer instead proves the exact visible pane, current pose, front-camera
		// clearance and schema without consulting gameplay blocking state.
		struct CurrentVisiblePanePoseEvidence
		{
			HandMirrorContentRuntimePolicy::PoseObservation pose{};
			std::uintptr_t exactExpectedVisiblePaneSubtree{ 0 };
			bool paneVisibleAndNotAppCulled{ false };
			bool exactPerspectiveBipedResolvedAfterAnimationUpdate{ false };
			bool paneBelongsToCurrentPerspectiveRoot{ false };
			bool poseSampledForCurrentDeliveryOpportunity{ false };
			bool paneWorldTransformFinite{ false };
			bool paneSchemaAndApertureRevalidatedAtPoseSample{ false };
			bool authoredSurfaceIdentityRevalidatedAtPoseSample{ false };
			bool renderCameraOnReflectiveFrontSide{ false };
		};

		[[nodiscard]] bool IsValidCurrentVisiblePanePose(
			const HandMirrorContentRuntimePolicy::EquippedCandidateEvidence& candidate,
			const CurrentVisiblePanePoseEvidence& evidence,
			const RuntimePaneVisibilityRequirement visibilityRequirement) noexcept
		{
			using namespace HandMirrorContentRuntimePolicy;
			return ValidateEquippedCandidate(candidate) == RejectionReason::kNone &&
				AcceptRuntimePaneVisibility(
					evidence.paneVisibleAndNotAppCulled,
					visibilityRequirement) &&
				IsValidPoseObservation(evidence.pose) &&
				evidence.pose.stableGeneration == candidate.stableGeneration &&
				evidence.pose.observedPaneSubtree ==
					candidate.visibleBipedObject.paneSubtree &&
				evidence.pose.authoredSurface ==
					candidate.ownerIdentity.authoredSurface &&
				evidence.exactExpectedVisiblePaneSubtree ==
					candidate.visibleBipedObject.paneSubtree &&
				evidence.exactPerspectiveBipedResolvedAfterAnimationUpdate &&
				evidence.paneBelongsToCurrentPerspectiveRoot &&
				evidence.poseSampledForCurrentDeliveryOpportunity &&
				evidence.paneWorldTransformFinite &&
				evidence.paneSchemaAndApertureRevalidatedAtPoseSample &&
				evidence.authoredSurfaceIdentityRevalidatedAtPoseSample &&
				evidence.renderCameraOnReflectiveFrontSide;
		}

		[[nodiscard]] bool BuildRuntimePaneGeometry(
			const RawCandidate& raw,
			const DirectX::XMFLOAT3& sourceCameraOrigin,
			HandMirrorContentRuntimePolicy::PanePoseGeometry& geometry) noexcept
		{
			using namespace HandMirrorContentRuntimePolicy;
			geometry = {};
			RE::NiTransform paneWorld{};
			static_assert(sizeof(paneWorld) == sizeof(Exact(raw).paneWorld));
			std::memcpy(&paneWorld, Exact(raw).paneWorld.data(), sizeof(paneWorld));
			// Skyrim keeps the first-person biped graph in an actor-rooted frame:
			// its root carries the actor's rotation but not the world translation
			// (owner run 2026-09-04 15:23: pane.center=(-25,-25,62) while the
			// camera stood at (22839,-44134,-22); cover-clip minimumReflectedW
			// 48,928 units, i.e. the capture reflected the camera across a plane
			// near the world origin and showed the Whiterun valley).  The
			// third-person root is the same actor in world space, so the pane
			// pose is re-rooted by the root translation difference whenever the
			// two roots disagree by more than a body length.
			RE::NiTransform firstRootWorld{};
			RE::NiTransform thirdRootWorld{};
			std::memcpy(&firstRootWorld, Exact(raw).rootWorld.data(), sizeof(firstRootWorld));
			std::memcpy(&thirdRootWorld, raw.third.rootWorld.data(), sizeof(thirdRootWorld));
			const DirectX::XMFLOAT3 rebaseDelta{
				thirdRootWorld.translate.x - firstRootWorld.translate.x,
				thirdRootWorld.translate.y - firstRootWorld.translate.y,
				thirdRootWorld.translate.z - firstRootWorld.translate.z
			};
			const float rebaseDistance = std::sqrt(
				rebaseDelta.x * rebaseDelta.x + rebaseDelta.y * rebaseDelta.y +
				rebaseDelta.z * rebaseDelta.z);
			constexpr float kFirstPersonRootRebaseThreshold = 256.0F;
			// Only a consumer whose source camera is itself in world space wants
			// the world pane: the capture fence (camera above the third-person
			// root).  The first-person raster pass presents the pane with a local
			// camera near (0,0,120); its projection, physical clip and delivery
			// need the raw local pose (V143 run 16:11: every hand pane draw
			// "projection-main-view-not-visible" after an unconditional rebase).
			constexpr float kSourceCameraWorldSpaceRadius = 512.0F;
			const float cameraToThirdRootX =
				sourceCameraOrigin.x - thirdRootWorld.translate.x;
			const float cameraToThirdRootY =
				sourceCameraOrigin.y - thirdRootWorld.translate.y;
			const float cameraToThirdRootZ =
				sourceCameraOrigin.z - thirdRootWorld.translate.z;
			const float cameraToThirdRoot = std::sqrt(
				cameraToThirdRootX * cameraToThirdRootX +
				cameraToThirdRootY * cameraToThirdRootY +
				cameraToThirdRootZ * cameraToThirdRootZ);
			// The first-person raster camera lives in the actor-rooted frame at
			// (0,0,~120): its x/y are zero while the first-person root is parked at
			// the origin.  Classify by that signature, not by distance to the
			// third-person root: in interiors the actor stands a few hundred units
			// from the cell origin, so the radius tests called the local camera
			// "world" (rebased pane against a local camera: projection miss, dark
			// or retried pane) and gave the world fence no rebase until the owner
			// had walked 256 units away (2026-09-05 13:31 and 14:03 runs, 1.7.104).
			constexpr float kOriginEpsilon = 1.0F;
			const bool firstRootAtOrigin =
				std::abs(firstRootWorld.translate.x) < kOriginEpsilon &&
				std::abs(firstRootWorld.translate.y) < kOriginEpsilon &&
				std::abs(firstRootWorld.translate.z) < kOriginEpsilon;
			const bool sourceCameraLocal = firstRootAtOrigin &&
				std::abs(sourceCameraOrigin.x) < kOriginEpsilon &&
				std::abs(sourceCameraOrigin.y) < kOriginEpsilon;
			const bool sourceCameraInWorldSpace = !sourceCameraLocal &&
				std::isfinite(cameraToThirdRoot) &&
				cameraToThirdRoot <= kSourceCameraWorldSpaceRadius;
			const bool rebased = sourceCameraInWorldSpace &&
				std::isfinite(rebaseDistance) &&
				((firstRootAtOrigin && rebaseDistance > kOriginEpsilon) ||
					rebaseDistance > kFirstPersonRootRebaseThreshold);
			const RE::NiPoint3 paneLocalTranslate = paneWorld.translate;
			if (rebased) {
				paneWorld.translate.x += rebaseDelta.x;
				paneWorld.translate.y += rebaseDelta.y;
				paneWorld.translate.z += rebaseDelta.z;
			}
			{
				static std::atomic<std::uint32_t> rebaseLogBudget{ 40 };
				static std::atomic<std::uint32_t> rebaseLogCadence{ 0 };
				const auto tick = rebaseLogCadence.fetch_add(1, std::memory_order_relaxed);
				auto budget = rebaseLogBudget.load(std::memory_order_acquire);
				if ((tick % 90) == 0 && budget != 0 &&
					rebaseLogBudget.compare_exchange_strong(
						budget, budget - 1, std::memory_order_acq_rel)) {
					try {
						logger::info(
							"[RR][HandMirror][pane-rebase] rebased={} cameraWorld={} delta={:.0f} firstRoot=({:.0f},{:.0f},{:.0f}) thirdRoot=({:.0f},{:.0f},{:.0f}) paneRaw=({:.0f},{:.0f},{:.0f}) paneWorld=({:.0f},{:.0f},{:.0f}) camera=({:.0f},{:.0f},{:.0f})",
							rebased, sourceCameraInWorldSpace, rebaseDistance,
							firstRootWorld.translate.x, firstRootWorld.translate.y,
							firstRootWorld.translate.z,
							thirdRootWorld.translate.x, thirdRootWorld.translate.y,
							thirdRootWorld.translate.z,
							paneLocalTranslate.x, paneLocalTranslate.y, paneLocalTranslate.z,
							paneWorld.translate.x, paneWorld.translate.y, paneWorld.translate.z,
							sourceCameraOrigin.x, sourceCameraOrigin.y, sourceCameraOrigin.z);
					} catch (...) {
					}
				}
			}
			const auto paneX = paneWorld.rotate.GetVectorX();
			const auto paneY = paneWorld.rotate.GetVectorY();
			const auto paneZ = paneWorld.rotate.GetVectorZ();
			const float scale = std::abs(paneWorld.scale);
			geometry = PanePoseGeometry{
				.center = { paneWorld.translate.x, paneWorld.translate.y,
					paneWorld.translate.z },
				.normal = { paneX.x, paneX.y, paneX.z },
				.tangent = { paneY.x, paneY.y, paneY.z },
				.bitangent = { paneZ.x, paneZ.y, paneZ.z },
				.halfWidth = 7.0F * scale,
				.halfHeight = 7.0F * scale,
				.frontClearance = 1.0F * scale,
				.backClearance = 2.0F * scale
			};
			if (!IsSanePoseGeometry(geometry))
				return false;
			const float cameraSide =
				(sourceCameraOrigin.x - geometry.center.x) * geometry.normal.x +
				(sourceCameraOrigin.y - geometry.center.y) * geometry.normal.y +
				(sourceCameraOrigin.z - geometry.center.z) * geometry.normal.z;
			// Pane side is not an admission gate: raised presentation owns a virtual
			// portrait plane, while lowered presentation reorients this same physical
			// plane toward the frozen source eye. An edge-on instant may skip locally,
			// but it must never retire the otherwise valid equipped mirror identity.
			return std::isfinite(cameraSide);
		}

		[[nodiscard]] bool BuildRuntimeSnapshot(
			const RawCandidate& raw,
			const Freshness::FrameReceipt& receipt,
			const DirectX::XMFLOAT3& sourceCameraOrigin,
			const bool exactPassRequired,
			const RuntimePaneVisibilityRequirement visibilityRequirement,
			RuntimeCandidateSnapshot& output) noexcept
		{
			using namespace HandMirrorContentRuntimePolicy;
			output = {};
			RecordBlockingSignals(raw);

			RuntimePreFreshnessRejectMask receiptRejects = 0;
			if (!Freshness::IsValidFrameReceipt(receipt)) {
				receiptRejects |=
					Mask(RuntimePreFreshnessReject::kReceiptInvalid);
			}
			if (raw.graphicsFrame != receipt.graphicsFrame) {
				receiptRejects |=
					Mask(RuntimePreFreshnessReject::kGraphicsFrameMismatch);
			}
			if (receiptRejects != 0) {
				g_thread.hiddenFreshness = {};
				RecordPreFreshnessRejects(receiptRejects);
				RecordFreshnessDecision(receipt, {});
				return false;
			}

			const auto candidateRejects =
				ClassifyRuntimePreFreshnessForVisibility({
				.exactTupleEquipped = raw.exactTupleEquipped,
				.currentRaceAndSexExact = raw.currentRaceAndSexExact,
				.firstPersonPerspective =
					raw.perspective == Policy::Perspective::kFirstPerson,
				.firstPersonSchemaExact = Exact(raw).exactDirectSchema,
				.thirdPersonSchemaExact = raw.third.exactDirectSchema,
				.firstPersonPaneVisibleAndNotAppCulled =
					Exact(raw).visibleAndNotAppCulled,
				.exactPassRequired = exactPassRequired,
				.exactPassIsFirstPersonPane =
					raw.passIsExactFirstPersonPane
			}, visibilityRequirement);
			if (candidateRejects != 0) {
				RecordPreFreshnessRejects(candidateRejects);
				return false;
			}
			const auto* contract = StyleContract(raw.styleIndex);
			if (!contract) {
				RecordPreFreshnessRejects(
					Mask(RuntimePreFreshnessReject::kOwnerIdentity));
				return false;
			}

			const StableCandidateGeneration generation{
				g_thread.lifecycle.generations.equip,
				StablePairGeneration(
					g_thread.lifecycle.generations.firstPersonRoot,
					g_thread.lifecycle.generations.thirdPersonRoot),
				StablePairGeneration(
					g_thread.lifecycle.generations.firstPersonClone,
					g_thread.lifecycle.generations.thirdPersonClone)
			};
			const auto authored = BuildRuntimeAuthoredSurface(raw);
			const auto visibleModel = VRRuntimeFixEnabled() ?
				raw.approved.thirdPersonModel.approvalIdentity :
				raw.approved.firstPersonModel.approvalIdentity;
			const auto captureModel = raw.approved.thirdPersonModel.approvalIdentity;
			const HandMirrorOwnerIdentity owner{
				raw.playerFormID, raw.playerHandle,
				static_cast<std::uintptr_t>(raw.approved.armor.pluginIdentity),
				contract->armorLocalFormID,
				static_cast<std::uintptr_t>(
					raw.approved.armor.loadedRecordIdentity),
				static_cast<std::uintptr_t>(raw.approved.armorAddon.pluginIdentity),
				contract->armorAddonLocalFormID,
				static_cast<std::uintptr_t>(
					raw.approved.armorAddon.loadedRecordIdentity),
				kDisplayedShieldSlotNumber, generation, authored,
				visibleModel, captureModel, VRRuntimeFixEnabled() ?
					Perspective::kVRFirstPerson : Perspective::kFirstPerson,
				static_cast<std::uintptr_t>(Exact(raw).partCloneIdentity),
				static_cast<std::uintptr_t>(raw.third.partCloneIdentity),
				static_cast<std::uintptr_t>(Exact(raw).itemIdentity),
				static_cast<std::uintptr_t>(raw.third.itemIdentity),
				static_cast<std::uintptr_t>(Exact(raw).paneIdentity),
				static_cast<std::uintptr_t>(raw.third.paneIdentity)
			};
			if (!IsValidHandMirrorOwnerIdentity(owner)) {
				RecordPreFreshnessRejects(
					Mask(RuntimePreFreshnessReject::kOwnerIdentity));
				return false;
			}
			RecordPreFreshnessRejects(0);
			g_counters.preFreshnessPasses.fetch_add(
				1, std::memory_order_relaxed);

			const auto continuity = FreshnessContinuity(raw, owner);
			const auto hiddenFrames = HiddenFrames(raw);
			const auto runtime = FreshnessRuntime();
			const auto freshness = Freshness::EvaluateHiddenFreshness(
				g_thread.hiddenFreshness, runtime, receipt, hiddenFrames,
				continuity);
			g_thread.hiddenFreshness = freshness.next;
			RecordFreshnessDecision(receipt, freshness);
			const bool hiddenFresh = freshness.fresh;
			const BipedPartCloneIdentity firstObject{
				owner.armorBase, owner.armorAddonBase, owner.visiblePartClone,
				owner.visibleMirrorItemSubtree, owner.visiblePaneSubtree,
				visibleModel, authored };
			const BipedPartCloneIdentity thirdObject{
				owner.armorBase, owner.armorAddonBase, owner.capturePartClone,
				owner.captureMirrorItemSubtree, owner.capturePaneSubtree,
				captureModel, authored };
			const auto candidate = EquippedCandidateEvidence{
				.ownerIdentity = owner,
				.authority = IdentityAuthority::kFirstPartyInternal,
				.perspective = owner.perspective,
				.stableGeneration = generation,
				.playerFormID = raw.playerFormID,
				.playerHandleToken = raw.playerHandle,
				.exactOwnedArmorPluginIdentity = owner.armorPluginIdentity,
				.exactOwnedArmorLocalFormID = contract->armorLocalFormID,
				.exactOwnedArmorAddonPluginIdentity = owner.armorAddonPluginIdentity,
				.exactOwnedArmorAddonLocalFormID =
					contract->armorAddonLocalFormID,
				.equippedBipedSlotNumber = kDisplayedShieldSlotNumber,
				.playerActor = static_cast<std::uintptr_t>(
					PointerToken(RE::PlayerCharacter::GetSingleton(), kDomainPlayer)),
				.exactOwnedArmorBase = owner.armorBase,
				.exactOwnedArmorAddon = owner.armorAddonBase,
				.visiblePerspectiveRoot = static_cast<std::uintptr_t>(
					Exact(raw).rootIdentity),
				.thirdPersonPlayerRoot = static_cast<std::uintptr_t>(
					raw.third.rootIdentity),
				.visibleBipedObject = firstObject,
				.privateCaptureBipedObject = thirdObject,
				.activeShieldSlotCandidateCount = 1,
				.actorIsExactPlayer = raw.exactPlayer,
				.exactOwnedArmorResolvedByPluginAndLocalIdentity = raw.formsExact,
				.exactOwnedArmorAddonBelongsToArmorAndCurrentRace =
					raw.currentRaceAndSexExact,
				.wornArmorAtShieldSlotMatchesExactArmor = raw.exactTupleEquipped,
				.equippedObjectAtShieldSlotMatchesExactArmor = raw.exactTupleEquipped,
				.nativeEquippedStateRevalidatedAfterWakeup = true,
				.visibleBipedObjectIsCorrectForPerspective = true,
				.privateCaptureBipedObjectIsThirdPerson = true,
				.visiblePartCloneDescendsExactPerspectiveRoot =
					Exact(raw).partCloneDescendsRoot,
				.capturePartCloneDescendsThirdPersonPlayerRoot =
					raw.third.partCloneDescendsRoot,
				.visibleMirrorItemDescendsExactVisiblePartClone =
					Exact(raw).itemDirectChildOfPartClone,
				.captureMirrorItemDescendsExactCapturePartClone =
					raw.third.itemDirectChildOfPartClone,
				.visiblePaneDescendsExactMirrorItemSubtree =
					Exact(raw).paneDirectChildOfItem,
				.capturePaneDescendsExactMirrorItemSubtree =
					raw.third.paneDirectChildOfItem,
				.visibleAndCaptureClonesMatchCurrentNativeEquipEpoch = true,
				.authoredSurfaceIdentityResolvedFromFrozenContentLock =
					raw.contentFilesVerified,
				.visibleModelAssetMatchesApprovedPerspectiveAndSex =
					Exact(raw).modelMatches,
				.captureModelAssetMatchesApprovedThirdPersonAndSex =
					raw.third.modelMatches,
				.thirdPersonCaptureClonePoseCurrentForMainFrame = hiddenFresh,
				.inventoryPreviewClone = false,
				.droppedWorldClone = false,
				.menuOrDisplayClone = false,
				.publicRegistrationOrUnownedMarkerUsed = false
			};
			if (ValidateEquippedCandidate(candidate) != RejectionReason::kNone)
				return false;

			PanePoseGeometry geometry{};
			if (!BuildRuntimePaneGeometry(raw, sourceCameraOrigin, geometry))
				return false;
			const bool cameraOnFront = true;  // Finite side proved by geometry helper.
			std::uint64_t poseSequence = 0;
			if (!IssueToken(g_thread.snapshotIssuer, poseSequence))
				return false;
			const PoseObservation pose{
				generation, poseSequence, receipt.mainViewFrame,
				owner.visiblePaneSubtree,
				authored, geometry };
			const CurrentVisiblePanePoseEvidence visiblePane{
				.pose = pose,
				.exactExpectedVisiblePaneSubtree = owner.visiblePaneSubtree,
				.paneVisibleAndNotAppCulled =
					Exact(raw).visibleAndNotAppCulled,
				.exactPerspectiveBipedResolvedAfterAnimationUpdate = true,
				.paneBelongsToCurrentPerspectiveRoot =
					Exact(raw).partCloneDescendsRoot,
				.poseSampledForCurrentDeliveryOpportunity = true,
				.paneWorldTransformFinite = true,
				.paneSchemaAndApertureRevalidatedAtPoseSample =
					Exact(raw).exactDirectSchema,
				.authoredSurfaceIdentityRevalidatedAtPoseSample = true,
				.renderCameraOnReflectiveFrontSide = cameraOnFront
			};
			if (!hiddenFresh ||
				!IsValidCurrentVisiblePanePose(
					candidate, visiblePane, visibilityRequirement)) {
				return false;
			}

			PlanarMirrorMath::Plane plane{
				{ geometry.normal.x, geometry.normal.y, geometry.normal.z },
				geometry.normal.x * geometry.center.x +
					geometry.normal.y * geometry.center.y +
					geometry.normal.z * geometry.center.z
			};
			if (!PlanarMirrorMath::NormalizePlane(plane))
				return false;
			output = {
				.candidate = candidate,
				.surface = { owner, pose, receipt.sourceSequence },
				.reflectionPlane = plane,
				.paneFit = {
					.center = { geometry.center.x, geometry.center.y, geometry.center.z },
					.tangent = { geometry.tangent.x, geometry.tangent.y,
						geometry.tangent.z },
					.bitangent = { geometry.bitangent.x, geometry.bitangent.y,
						geometry.bitangent.z },
					.halfTangent = geometry.halfWidth,
					.halfBitangent = geometry.halfHeight },
				.receipt = receipt,
				.hiddenFreshness = Freshness::MakeFreshnessEvidence(
					receipt, hiddenFrames, continuity, freshness),
				.exactPassIsFirstPersonPane = raw.passIsExactFirstPersonPane,
				.hiddenThirdPersonFreshForGraphicsFrame = hiddenFresh,
				.paneVisibleAndNotAppCulled =
					Exact(raw).visibleAndNotAppCulled,
				// V168's input-intent fallback remained true after lowering, leaving
				// portrait clipping active on a physical pane and replaying old pixels.
				.graphIndicatesRaisedPresentation =
					HandMirrorLoweredPresentationPolicy::RaisedPresentation(
						LivePosePresentationEnabled(), raw.graphIsBlocking,
						raw.actorWantsBlocking, raw.weaponDrawn)
			};
			return HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample(
				output.surface);
		}

		[[nodiscard]] bool BuildEquippedPresentationIdentity(
			const RawCandidate& raw,
			RuntimeEquippedPresentationIdentity& output) noexcept
		{
			using namespace HandMirrorContentRuntimePolicy;
			output = {};
			const auto* contract = StyleContract(raw.styleIndex);
			if (!contract || raw.styleAmbiguous || !raw.exactPlayer ||
				!raw.formsExact || !raw.exactTupleEquipped ||
				!raw.currentRaceAndSexExact || !raw.contentFilesVerified ||
				!raw.first.exactDirectSchema || !raw.third.exactDirectSchema ||
				!Exact(raw).partCloneDescendsRoot ||
				!raw.third.partCloneDescendsRoot ||
				!Exact(raw).itemDirectChildOfPartClone ||
				!raw.third.itemDirectChildOfPartClone ||
				!Exact(raw).paneDirectChildOfItem ||
				!raw.third.paneDirectChildOfItem) {
				return false;
			}

			const StableCandidateGeneration generation{
				g_thread.lifecycle.generations.equip,
				StablePairGeneration(
					g_thread.lifecycle.generations.firstPersonRoot,
					g_thread.lifecycle.generations.thirdPersonRoot),
				StablePairGeneration(
					g_thread.lifecycle.generations.firstPersonClone,
					g_thread.lifecycle.generations.thirdPersonClone)
			};
			const auto authored = BuildRuntimeAuthoredSurface(raw);
			const auto visibleModel = VRRuntimeFixEnabled() ?
				raw.approved.thirdPersonModel.approvalIdentity :
				raw.approved.firstPersonModel.approvalIdentity;
			const auto captureModel =
				raw.approved.thirdPersonModel.approvalIdentity;
			const HandMirrorOwnerIdentity owner{
				raw.playerFormID,
				raw.playerHandle,
				static_cast<std::uintptr_t>(raw.approved.armor.pluginIdentity),
				contract->armorLocalFormID,
				static_cast<std::uintptr_t>(
					raw.approved.armor.loadedRecordIdentity),
				static_cast<std::uintptr_t>(
					raw.approved.armorAddon.pluginIdentity),
				contract->armorAddonLocalFormID,
				static_cast<std::uintptr_t>(
					raw.approved.armorAddon.loadedRecordIdentity),
				kDisplayedShieldSlotNumber,
				generation,
				authored,
				visibleModel,
				captureModel,
				VRRuntimeFixEnabled() ? Perspective::kVRFirstPerson :
					Perspective::kFirstPerson,
				static_cast<std::uintptr_t>(Exact(raw).partCloneIdentity),
				static_cast<std::uintptr_t>(raw.third.partCloneIdentity),
				static_cast<std::uintptr_t>(Exact(raw).itemIdentity),
				static_cast<std::uintptr_t>(raw.third.itemIdentity),
				static_cast<std::uintptr_t>(Exact(raw).paneIdentity),
				static_cast<std::uintptr_t>(raw.third.paneIdentity)
			};
			if (!IsValidHandMirrorOwnerIdentity(owner))
				return false;
			output = { owner, true };
			return true;
		}
	}

	namespace
	{
		[[nodiscard]] bool RunWithRuntimeCandidate(
			const Freshness::FrameReceipt& receipt,
			const DirectX::XMFLOAT3& sourceCameraOrigin,
			RE::BSRenderPass* pass,
			const RuntimePaneVisibilityRequirement visibilityRequirement,
			const RuntimeCandidateOperation operation,
			void* context) noexcept
		{
		if (!IsEnabled() || !operation)
			return false;
		g_counters.preFreshnessChecks.fetch_add(
			1, std::memory_order_relaxed);
		if (!Freshness::IsValidFrameReceipt(receipt)) {
			RecordPreFreshnessRejects(
				Mask(RuntimePreFreshnessReject::kReceiptInvalid));
			return false;
		}
		RawCandidate first{};
		RawCandidate second{};
		if (!ReadCandidatePairGuarded(first, second, pass)) {
			RecordPreFreshnessRejects(
				Mask(RuntimePreFreshnessReject::kCandidateRead));
			g_counters.runtimeBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}
		if (!SameRawCandidate(first, second)) {
			RecordPreFreshnessRejects(
				Mask(RuntimePreFreshnessReject::kCandidatePairMismatch));
			g_counters.runtimeBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}
		if (!RefreshLifecycle(second)) {
			RecordPreFreshnessRejects(
				Mask(RuntimePreFreshnessReject::kLifecycleRefresh));
			g_counters.runtimeBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}
		RuntimeCandidateSnapshot snapshot{};
		if (!BuildRuntimeSnapshot(
				second, receipt, sourceCameraOrigin,
				pass != nullptr, visibilityRequirement, snapshot)) {
			g_counters.runtimeBorrowRejects.fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}
		RuntimeRetainedScene scene{};
		bool invoked = false;
		bool result = false;
#if defined(_MSC_VER)
		__try {
			__try {
#endif
				if (!ResolveAndRetainRuntimeSceneUnsafe(second, scene)) {
					g_counters.runtimeBorrowRejects.fetch_add(
						1, std::memory_order_relaxed);
				} else {
					invoked = true;
					result = operation(snapshot, scene.borrowed, context);
				}
#if defined(_MSC_VER)
			} __finally {
				if (!ReleaseRuntimeScene(scene))
					result = false;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			LatchFault(Policy::ObserverFault::kCallbackFault);
			g_counters.runtimeBorrowFaults.fetch_add(
				1, std::memory_order_relaxed);
			result = false;
		}
#endif
		if (invoked) {
			g_counters.runtimeBorrows.fetch_add(1, std::memory_order_relaxed);
		}
		return invoked && result && !IsFaultStopped();
		}
	}

	bool RunWithExactRuntimeCandidate(
		const Freshness::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		RE::BSRenderPass* pass,
		const RuntimeCandidateOperation operation,
		void* context) noexcept
	{
		return RunWithRuntimeCandidate(
			receipt, sourceCameraOrigin, pass,
			RuntimePaneVisibilityRequirement::kRequireVisibleAndNotAppCulled,
			operation, context);
	}

	bool RunWithOuterFirstPersonRuntimeCandidate(
		const Freshness::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		const RuntimeCandidateOperation operation,
		void* context) noexcept
	{
		return RunWithRuntimeCandidate(
			receipt, sourceCameraOrigin, nullptr,
			RuntimePaneVisibilityRequirement::
				kAllowAppCulledOnlyForOuterZeroCallback,
			operation, context);
	}

	bool RunWithPostCaptureRuntimeCandidate(
		const Freshness::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		const RuntimeCandidateOperation operation,
		void* context) noexcept
	{
		return RunWithRuntimeCandidate(
			receipt, sourceCameraOrigin, nullptr,
			RuntimePaneVisibilityRequirement::
				kAllowAppCulledOnlyForPostCaptureRevalidation,
			operation, context);
	}

	bool TryReadFramePresentationPose(
		RE::BSRenderPass* framePass,
		const Freshness::FrameReceipt& receipt,
		const DirectX::XMFLOAT3& sourceCameraOrigin,
		RuntimePresentationPose& output) noexcept
	{
		output = {};
		if (!IsEnabled() || !LivePosePresentationEnabled() ||
			!Freshness::IsValidFrameReceipt(receipt))
			return false;
		// Cheap rejection before the double equipment/schema read. This name is
		// only a filter: exact ownership is checked while the scene is retained.
		__try {
			if (!framePass || !framePass->geometry ||
				framePass->geometry->name != FrameName())
				return false;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			LatchFault(Policy::ObserverFault::kCallbackFault);
			return false;
		}
		RawCandidate first{};
		RawCandidate second{};
		if (!ReadCandidatePairGuarded(first, second, nullptr) ||
			!SameRawCandidate(first, second) ||
			second.graphicsFrame != receipt.graphicsFrame ||
			second.perspective != Policy::Perspective::kFirstPerson ||
			!RefreshLifecycle(second))
			return false;
		RuntimeEquippedPresentationIdentity identity{};
		HandMirrorContentRuntimePolicy::PanePoseGeometry geometry{};
		if (!BuildEquippedPresentationIdentity(second, identity) || !identity.valid ||
			!BuildRuntimePaneGeometry(second, sourceCameraOrigin, geometry))
			return false;
		std::uint64_t poseSequence = 0;
		if (!IssueToken(g_thread.snapshotIssuer, poseSequence))
			return false;
		RuntimePresentationPose snapshot{
			.surface = { identity.owner,
				{ identity.owner.stableGeneration, poseSequence, receipt.mainViewFrame,
					identity.owner.visiblePaneSubtree, identity.owner.authoredSurface, geometry },
				receipt.sourceSequence },
			.receipt = receipt,
			.graphIndicatesRaisedPresentation =
				HandMirrorLoweredPresentationPolicy::RaisedPresentation(
					true, second.graphIsBlocking, second.actorWantsBlocking, second.weaponDrawn)
		};
		if (!HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample(snapshot.surface))
			return false;
		RuntimeRetainedScene scene{};
		bool retainedAndReleased = false;
		__try {
			__try {
				retainedAndReleased = ResolveAndRetainRuntimeSceneUnsafe(second, scene);
				if (retainedAndReleased) {
					retainedAndReleased = framePass->geometry->parent ==
						scene.borrowed.firstPersonMirrorItem &&
						framePass->geometry == scene.borrowed.firstPersonMirrorItem->
							GetObjectByName(FrameName());
				}
			} __finally {
				if (!ReleaseRuntimeScene(scene))
					retainedAndReleased = false;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			LatchFault(Policy::ObserverFault::kCallbackFault);
			retainedAndReleased = false;
		}
		if (!retainedAndReleased || IsFaultStopped())
			return false;
		// Values become available only after every temporary scene retain closes.
		// Never seed/calibrate hiddenFreshness here or manufacture capture evidence.
		output = snapshot;
		return true;
	}

	RuntimeEquippedPresentationStatus RunWithEquippedPresentationIdentity(
		const RuntimeEquippedPresentationOperation operation,
		void* context) noexcept
	{
		if (!IsEnabled() || !operation)
			return IsFaultStopped() ?
				RuntimeEquippedPresentationStatus::kFaulted :
				RuntimeEquippedPresentationStatus::kIndeterminate;
		RawCandidate first{};
		RawCandidate second{};
		if (!ReadCandidatePairGuarded(first, second, nullptr) ||
			!SameRawCandidate(first, second)) {
			return IsFaultStopped() ?
				RuntimeEquippedPresentationStatus::kFaulted :
				RuntimeEquippedPresentationStatus::kIndeterminate;
		}
		RuntimeEquippedPresentationIdentity identity{};
		const bool exactIdentity =
			BuildEquippedPresentationIdentity(second, identity) && identity.valid;
		const auto equipment =
			HandMirrorLastGoodPresentationPolicy::ClassifyEquipment(
				true, second.equipmentSlotsConclusive,
				second.approvedArmorInFirstShieldSlot,
				second.approvedArmorInThirdShieldSlot, exactIdentity);
		// Absence is authoritative only when both independently sampled biped
		// shield slots are readable and neither contains any approved armor.  A
		// one-sided transition, missing clone, stale schema, or callback failure
		// must retain the established presentation lease instead of masquerading
		// as an unequip.
		if (equipment == HandMirrorLastGoodPresentationPolicy::
				EquipmentEvidence::kPositivelyUnequipped) {
			return RuntimeEquippedPresentationStatus::kPositivelyUnequipped;
		}
		if (equipment != HandMirrorLastGoodPresentationPolicy::
				EquipmentEvidence::kExactEquipped) {
			return RuntimeEquippedPresentationStatus::kIndeterminate;
		}

		RuntimeRetainedScene scene{};
		bool invoked = false;
		bool result = false;
#if defined(_MSC_VER)
		__try {
			__try {
#endif
				if (ResolveAndRetainRuntimeSceneUnsafe(second, scene)) {
					invoked = true;
					result = operation(identity, context);
				}
#if defined(_MSC_VER)
			} __finally {
				if (!ReleaseRuntimeScene(scene))
					result = false;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			LatchFault(Policy::ObserverFault::kCallbackFault);
			result = false;
		}
#endif
		if (IsFaultStopped())
			return RuntimeEquippedPresentationStatus::kFaulted;
		return invoked && result ?
			RuntimeEquippedPresentationStatus::kExactEquipped :
			RuntimeEquippedPresentationStatus::kIndeterminate;
	}

	bool RevalidateBorrowedSuppressionSubtree(
		const RuntimeCandidateSnapshot& snapshot,
		const RuntimeBorrowedScene& borrowed,
		RE::NiAVObject* exactMutationTarget,
		SuppressionSubtreeRevalidation& output) noexcept
	{
		output = {};
		if (!IsEnabled() || !exactMutationTarget ||
			!HandMirrorRuntimeBridgePolicy::IsValidMovingSurfaceSample(
				snapshot.surface)) {
			return false;
		}
		bool completed = false;
#if defined(_MSC_VER)
		__try {
#endif
			RawCandidate first{};
			RawCandidate second{};
			if (!ReadCandidatePairGuarded(first, second, nullptr) ||
				!SameRawCandidate(first, second) || !RefreshLifecycle(second)) {
				return false;
			}
			const auto& owner = snapshot.surface.owner;
			const Freshness::FrameReceipt currentReceipt{
				snapshot.receipt.sourceSequence,
				snapshot.receipt.mainViewFrame,
				second.graphicsFrame
			};
			output.receiptAndHiddenFresh =
				Freshness::IsValidFrameReceipt(snapshot.receipt) &&
				snapshot.hiddenThirdPersonFreshForGraphicsFrame &&
				snapshot.surface.sourceSequence ==
					snapshot.receipt.sourceSequence &&
				snapshot.surface.pose.mainViewFrame ==
					snapshot.receipt.mainViewFrame &&
				second.graphicsFrame == snapshot.receipt.graphicsFrame &&
				Freshness::IsExactSameReceiptFreshReread(
					FreshnessRuntime(), snapshot.hiddenFreshness,
					{ currentReceipt, HiddenFrames(second),
						FreshnessContinuity(second, owner) });
			const HandMirrorContentRuntimePolicy::StableCandidateGeneration current{
				g_thread.lifecycle.generations.equip,
				StablePairGeneration(
					g_thread.lifecycle.generations.firstPersonRoot,
					g_thread.lifecycle.generations.thirdPersonRoot),
				StablePairGeneration(
					g_thread.lifecycle.generations.firstPersonClone,
					g_thread.lifecycle.generations.thirdPersonClone)
			};
			const bool exactBorrowedPointers =
				PointerToken(borrowed.firstPersonRoot, kDomainRootFirst) ==
					second.first.rootIdentity &&
				PointerToken(borrowed.thirdPersonRoot, kDomainRootThird) ==
					second.third.rootIdentity &&
				PointerToken(borrowed.firstPersonPartClone, kDomainCloneFirst) ==
					second.first.partCloneIdentity &&
				PointerToken(borrowed.thirdPersonPartClone, kDomainCloneThird) ==
					second.third.partCloneIdentity &&
				PointerToken(borrowed.thirdPersonMirrorItem, kDomainItemThird) ==
					second.third.itemIdentity &&
				PointerToken(borrowed.thirdPersonPane, kDomainPaneThird) ==
					second.third.paneIdentity;
			output.attemptOwnerSourceAndGenerationsAreCurrent =
				output.receiptAndHiddenFresh &&
				snapshot.surface.sourceSequence != 0 && current == owner.stableGeneration &&
				second.playerFormID == owner.playerFormID &&
				second.playerHandle == owner.playerHandleToken &&
				second.exactTupleEquipped && second.currentRaceAndSexExact &&
				second.formsExact && exactBorrowedPointers;
			output.capturePartCloneIsCurrentThirdPerson =
				exactBorrowedPointers &&
				static_cast<std::uintptr_t>(second.third.partCloneIdentity) ==
					owner.capturePartClone &&
				(borrowed.thirdPersonPartClone != borrowed.firstPersonPartClone ||
                    (VRRuntimeFixEnabled() && owner.perspective ==
                        HandMirrorContentRuntimePolicy::Perspective::kVRFirstPerson &&
                        owner.visiblePartClone == owner.capturePartClone));
			output.mirrorItemIsExactApprovedRRMirrorItem =
				exactMutationTarget == borrowed.thirdPersonMirrorItem &&
				static_cast<std::uintptr_t>(second.third.itemIdentity) ==
					owner.captureMirrorItemSubtree;
			output.mirrorItemDirectlyDescendsCapturePartClone =
				borrowed.thirdPersonMirrorItem->parent ==
					borrowed.thirdPersonPartClone;
			output.paneDirectlyDescendsMirrorItem =
				borrowed.thirdPersonPane->parent ==
					borrowed.thirdPersonMirrorItem;
			RE::NiAVObject* currentItem = nullptr;
			RE::NiAVObject* currentPane = nullptr;
			output.mirrorItemContainsExactApprovedFrameAndPane =
				ExactPaneSchema(
					borrowed.thirdPersonPartClone, currentItem, currentPane) &&
				currentItem == borrowed.thirdPersonMirrorItem &&
				currentPane == borrowed.thirdPersonPane &&
				second.third.exactDirectSchema;
			auto* player = RE::PlayerCharacter::GetSingleton();
			output.targetContainsPlayer =
				player && reinterpret_cast<void*>(exactMutationTarget) ==
					reinterpret_cast<void*>(player);
			output.targetContainsFirstPersonRoot = DescendsFrom(
				borrowed.firstPersonRoot, exactMutationTarget);
			output.targetContainsThirdPersonRoot = DescendsFrom(
				borrowed.thirdPersonRoot, exactMutationTarget);
			output.targetContainsBipedRoot =
				output.targetContainsFirstPersonRoot ||
				output.targetContainsThirdPersonRoot;
			output.targetIsWholePartClone =
				exactMutationTarget == borrowed.firstPersonPartClone ||
				exactMutationTarget == borrowed.thirdPersonPartClone;
			completed = true;
#if defined(_MSC_VER)
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			completed = false;
		}
#endif
		if (!completed)
			output = {};
		return completed;
	}

	void PrepareAtInputLoaded(const bool reflectiveRuntimeDependency) noexcept
	{
		if (g_prepareAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		try {
			const auto marker = MarkerPath(kEnableMarker);
			const bool requested = reflectiveRuntimeDependency ||
				(!marker.empty() && ValidateEmptyMarker(marker));
			g_requested.store(requested, std::memory_order_release);
			const auto vrMarker = MarkerPath(kVRRuntimeFixMarker);
			const bool vrRuntimeFix = reflectiveRuntimeDependency &&
				SupportedRuntimePolicy::IsExactVRRuntime() &&
				GetModuleHandleW(L"VRIK.dll") != nullptr &&
				!vrMarker.empty() && ValidateEmptyMarker(vrMarker);
			g_vrRuntimeFixEnabled.store(vrRuntimeFix, std::memory_order_release);
			logger::info(
				"[MOS][VR][hand-runtime] enabled={} nodeFrameOffset=0x{:X} captureBeforePresentation={} marker={}",
				vrRuntimeFix, vrRuntimeFix ?
					HandMirrorVRRuntimePolicy::kVRNodeFrameOffset :
					HandMirrorVRRuntimePolicy::kFlatNodeFrameOffset,
				vrRuntimeFix, vrMarker.string());
			const auto vrPresentationMarker = MarkerPath(
				L"RealisticReflections_VRPresentationFix.enable");
			const bool vrPresentationFix = SupportedRuntimePolicy::IsExactVRRuntime() &&
				!vrPresentationMarker.empty() && ValidateEmptyMarker(vrPresentationMarker);
			g_vrPresentationFixEnabled.store(vrPresentationFix, std::memory_order_release);
			logger::info("[MOS][VR][presentation] enabled={} earlyExactHandPane={} captureSourceRebase={} marker={}",
				vrPresentationFix, vrPresentationFix && vrRuntimeFix,
				vrPresentationFix, vrPresentationMarker.string());
			const auto vrCohortMarker = MarkerPath(L"RealisticReflections_VRViewCohortFix.enable");
			const bool vrViewCohortFix = vrPresentationFix && vrRuntimeFix &&
				!vrCohortMarker.empty() && ValidateEmptyMarker(vrCohortMarker);
			g_vrViewCohortFixEnabled.store(vrViewCohortFix, std::memory_order_release);
			logger::info("[MOS][VR][view-cohort] enabled={} separateHandWorld={} exactBipedGrip={} marker={}",
				vrViewCohortFix, vrViewCohortFix, vrViewCohortFix, vrCohortMarker.string());
			const auto vrSceneMarker = MarkerPath(L"RealisticReflections_VRSceneFix.enable");
			const bool vrSceneFix = vrViewCohortFix &&
				!vrSceneMarker.empty() && ValidateEmptyMarker(vrSceneMarker);
			g_vrSceneFixEnabled.store(vrSceneFix, std::memory_order_release);
			logger::info("[MOS][VR][scene] enabled={} cachedInventoryAllowed={} poseOwner=VRIK marker={}",
				vrSceneFix, vrSceneFix, vrSceneMarker.string());
			const auto livePoseMarker = MarkerPath(kLivePosePresentationMarker);
			const bool livePoseEnabled = requested && !REL::Module::IsVR() &&
				!livePoseMarker.empty() && ValidateEmptyMarker(livePoseMarker);
			g_livePosePresentationEnabled.store(livePoseEnabled, std::memory_order_release);
			logger::info(
				"[RR][HandMirror][live-pose] enabled={} animatedBlockingOnly={} currentPanePlacementOnly={} marker={}",
				livePoseEnabled, livePoseEnabled, livePoseEnabled, livePoseMarker.string());
			if (requested) {
				logger::info(
					"[RR][HandMirrorApprovedReadOnly] approved-content observer requested (reflectiveDependency={}); validation is read-only until its caller's independent authority gate",
					reflectiveRuntimeDependency);
			} else {
				logger::info(
					"[RR][HandMirrorApprovedReadOnly] marker absent or rejected (must be an exact empty, same-handle regular non-reparse file); observer remains off");
			}
		} catch (...) {
			g_requested.store(false, std::memory_order_release);
			g_livePosePresentationEnabled.store(false, std::memory_order_release);
			g_vrRuntimeFixEnabled.store(false, std::memory_order_release);
			g_vrPresentationFixEnabled.store(false, std::memory_order_release);
			g_vrViewCohortFixEnabled.store(false, std::memory_order_release);
			g_vrSceneFixEnabled.store(false, std::memory_order_release);
		}
	}

	bool Requested() noexcept
	{
		return g_requested.load(std::memory_order_acquire);
	}

	bool VRRuntimeFixEnabled() noexcept
	{
		return g_vrRuntimeFixEnabled.load(std::memory_order_acquire);
	}

	bool VRPresentationFixEnabled() noexcept
	{
		return g_vrPresentationFixEnabled.load(std::memory_order_acquire);
	}

	bool VRViewCohortFixEnabled() noexcept
	{
		return g_vrViewCohortFixEnabled.load(std::memory_order_acquire);
	}

	bool VRSceneFixEnabled() noexcept
	{
		return g_vrSceneFixEnabled.load(std::memory_order_acquire);
	}

	bool IsExactCurrentVRPanePass(RE::BSRenderPass* pass) noexcept
	{
		if (!VRPresentationFixEnabled() || !VRRuntimeFixEnabled() || !IsEnabled() || !pass)
			return false;
		RawCandidate first{}, second{};
		RuntimeEquippedPresentationIdentity identity{};
		return ReadCandidatePairGuarded(first, second, pass) &&
			SameRawCandidate(first, second) && RefreshLifecycle(second) &&
			second.perspective == Policy::Perspective::kFirstPerson &&
			second.passIsExactFirstPersonPane && Exact(second).visibleAndNotAppCulled &&
			BuildEquippedPresentationIdentity(second, identity) && identity.valid &&
			identity.owner.perspective ==
				HandMirrorContentRuntimePolicy::Perspective::kVRFirstPerson;
	}

	std::uint32_t ReadNodeUpdateStamp(const RE::NiAVObject* object) noexcept
	{
		if (!object)
			return 0;
		const bool correctedVR = VRRuntimeFixEnabled();
		const auto offset = correctedVR ?
			HandMirrorVRRuntimePolicy::kVRNodeFrameOffset :
			HandMirrorVRRuntimePolicy::kFlatNodeFrameOffset;
		return HandMirrorVRRuntimePolicy::ReadNodeFrame(
			{ reinterpret_cast<const std::byte*>(object),
				offset + sizeof(std::uint32_t) }, correctedVR);
	}

	bool LivePosePresentationEnabled() noexcept
	{
		return g_livePosePresentationEnabled.load(std::memory_order_acquire);
	}

	void OnDataLoaded() noexcept
	{
		if (!Requested())
			return;
		g_counters.dataLoadedCalls.fetch_add(1, std::memory_order_relaxed);
		if (g_dataLoaded.exchange(true, std::memory_order_acq_rel))
			return;
		g_enabled.store(false, std::memory_order_release);
		std::uint32_t formsPinnedStyleMask = 0;
		std::uint32_t readyStyleMask = 0;
		for (const auto& contract : kFrozenStyleContracts) {
			const auto styleValue = static_cast<std::size_t>(contract.index);
			const auto verifiedMask = VerifyFrozenFiles(contract);
			g_verifiedFileMasks[styleValue].store(
				verifiedMask, std::memory_order_release);
			const bool filesExact =
				verifiedMask == contract.allFrozenFilesVerifiedMask;
			// Do not touch loaded forms until every shipping file is byte exact.
			const bool formsPinned = filesExact &&
				ValidateFrozenFormsGuarded(contract);
			if (formsPinned)
				formsPinnedStyleMask |= StyleBit(contract.index);
			if (filesExact && formsPinned)
				readyStyleMask |= StyleBit(contract.index);
			try {
				logger::info(
					"[RR][HandMirrorApprovedReadOnly] style={} filesMask=0x{:02X}/0x{:02X} formsPinned={} ready={}",
					contract.diagnosticName, verifiedMask,
					contract.allFrozenFilesVerifiedMask, formsPinned,
					(filesExact && formsPinned));
			} catch (...) {
			}
		}
		g_formsPinnedStyleMask.store(
			formsPinnedStyleMask, std::memory_order_release);
		g_readyStyleMask.store(readyStyleMask, std::memory_order_release);
		const auto version = REL::Module::get().version();
		const bool runtimePinned =
			(REL::Module::IsSE() && version == kSupportedSE) ||
			(REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			SupportedRuntimePolicy::IsExactVRRuntime();
		const bool ownersReady =
			SecondView::RenderWorldDriverReadyForReadOnlyObserver() &&
			HandMirrorReadOnlyObserver::IsHookInstalled() &&
			HandMirrorReadOnlyObserver::IsEnabled() &&
			GeometryDrawObserver::IsInstalled();
		const bool contentReady = readyStyleMask != 0;
		const bool enabled = runtimePinned && ownersReady && contentReady &&
			!g_faulted.load(std::memory_order_acquire);
		g_enabled.store(enabled, std::memory_order_release);
		try {
			if (!contentReady)
				logger::info(
					"[RR][HandMirrorApprovedReadOnly] no exact supported hand-mirror content is installed; observer remains dormant");
			logger::info(
				"[RR][HandMirrorApprovedReadOnly] DataLoaded runtimePinned={} existingOwnersReady={} readyStyleMask=0x{:02X} formsPinnedStyleMask=0x{:02X} enabled={} policyRuntimeWired={} observerAuthority=false observerCaptureAuthority=false observerPublicationAuthority=false observerDeliveryAuthority=false observerAPIExposure=false observerTargetAllocation=false observerSceneMutation=false",
				runtimePinned, ownersReady, readyStyleMask,
				formsPinnedStyleMask, enabled,
				Policy::kApprovedContentObserverRuntimeWired);
		} catch (...) {
		}
	}

	void OnGameLoaded() noexcept
	{
		if (!Requested())
			return;
		g_counters.gameLoadInvalidations.fetch_add(1, std::memory_order_relaxed);
		(void)AdvanceAtomic(g_globalLifecycleSerial);
	}

	void OnPlayerEquipWake() noexcept
	{
		if (!Requested())
			return;
		g_counters.equipWakeups.fetch_add(1, std::memory_order_relaxed);
		(void)AdvanceAtomic(g_equipWakeSerial);
	}

	bool CurrentDrawTargetsMainView() noexcept
	{
		TargetRead target{};
		if (!ReadMainTargetGuarded(target))
			return false;
		return target.colorWritable && target.depthWritable &&
		       target.viewportMatchesShadowState &&
		       target.contextAndTargetsExactDevice;
	}

	void OnPreWorld() noexcept
	{
		if (!IsEnabled())
			return;
		if (g_thread.worldActive || g_thread.firstPersonActive ||
			g_thread.sourceFrame == (std::numeric_limits<std::uint64_t>::max)()) {
			LatchFault(Policy::ObserverFault::kMalformedState);
			return;
		}
		++g_thread.sourceFrame;
		g_thread.worldActive = true;
		g_thread.worldReturnedNormally = false;
		g_thread.firstPersonReturnedNormally = false;
		g_thread.exactPaneReturnCohort = ExactPaneReturnCohort::kZero;
		g_thread.evidence = {};
		g_thread.pending = {};
		g_counters.frames.fetch_add(1, std::memory_order_relaxed);
	}

	void OnWorldReturnedNormally() noexcept
	{
		if (!IsEnabled() || !g_thread.worldActive)
			return;
		// Publish native normal-return state before any diagnostic read.  A
		// naturally incoherent adjacent read must darken this frame, not be
		// misclassified by the finally path as an abnormal engine return.
		g_thread.worldReturnedNormally = true;
		RawCandidate first{};
		RawCandidate second{};
		if (!ReadCandidatePairGuarded(first, second) ||
			!SameRawCandidate(first, second) || !RefreshLifecycle(second)) {
			return;
		}
		g_thread.evidence.lifecycle = g_thread.lifecycle;
		g_thread.evidence.candidates = second.exactTupleEquipped ?
			Policy::ObserveExactCandidate({}) : Policy::CandidateCounter{};
		g_thread.evidence.candidate = BuildCandidateEvidence(second);
		const auto binding = Policy::MakeCandidateBinding(
			g_thread.evidence.candidate);
		const auto pose = BuildPoseSnapshot(
			Policy::PosePhase::kPostWorld, binding, Exact(second).paneWorld);
		g_thread.evidence.postWorld = {
			.pose = pose,
			.renderWorldReturnedNormally = true,
			.sampledAfterWorldAndBeforeFirstPerson = true,
			.exactCurrentFirstPersonCloneReacquired =
				Exact(second).partCloneIdentity != 0,
			.guardedReadCompleted = true,
			.guardedReadFaulted = false,
			.noEnginePointerRetained = true
		};
		g_thread.evidence.visiblePaneClearance = {
			.sourceFrame = g_thread.sourceFrame,
			.postWorldSnapshotToken = pose.snapshotToken,
			.observedSourceEyeClearance = 0.0F,
			.requiredSourceEyeClearance = 1.0F,
			.reflectiveFrontFacesCurrentCamera = false,
			.paneIntersectsCurrentMainView = false,
			.paneVisibleAndNotAppCulled = Exact(second).visibleAndNotAppCulled,
			.sourceCameraAgreesWithCurrentMainView = false,
			.schemaAndApertureRevalidated = Exact(second).exactDirectSchema,
			.noEnginePointerRetained = true
		};
		std::uint64_t hiddenToken = 0;
		(void)IssueToken(g_thread.snapshotIssuer, hiddenToken);
		g_thread.evidence.hiddenThirdPerson = {
			.observationToken = hiddenToken,
			.sourceFrame = g_thread.sourceFrame,
			.animationUpdateFrame = 0,
			.binding = binding,
			.thirdPersonPartCloneWorldTransformBits = second.third.partCloneWorld,
			.exactHiddenThirdPersonRootReacquired =
				second.third.rootIdentity != 0,
			.exactThirdPersonPartCloneReacquired =
				second.third.partCloneIdentity != 0,
			.hiddenBodyPoseCurrentForSourceFrame = false,
			.boneTransformsCurrentForSourceFrame = false,
			.firstPersonRootNotUsedAsCapturePose = true,
			.copiedAfterAnimationUpdate = false,
			.guardedReadCompleted = true,
			.guardedReadFaulted = false,
			.noEnginePointerRetained = true
		};
		g_thread.evidence.safety.guardedReadsCompleted = true;
		g_thread.evidence.safety.guardedReadFaulted = false;
		g_thread.evidence.safety.noEnginePointerRetainedAnywhere = true;
		g_lastGraphicsFrame.store(second.graphicsFrame, std::memory_order_relaxed);
		g_lastThirdRootUpdate.store(
			second.third.rootLastUpdated, std::memory_order_relaxed);
		g_lastThirdCloneUpdate.store(
			second.third.partCloneLastUpdated, std::memory_order_relaxed);
		g_lastThirdItemUpdate.store(
			second.third.itemLastUpdated, std::memory_order_relaxed);
		g_lastThirdPaneUpdate.store(
			second.third.paneLastUpdated, std::memory_order_relaxed);
		g_counters.postWorldSnapshots.fetch_add(1, std::memory_order_relaxed);
		if (second.exactTupleEquipped)
			g_counters.exactCandidates.fetch_add(1, std::memory_order_relaxed);
	}

	void OnWorldFinally() noexcept
	{
		const bool active = g_thread.worldActive;
		const bool returned = g_thread.worldReturnedNormally;
		g_thread.worldActive = false;
		if (active && !returned && IsEnabled()) {
			g_counters.abnormalWorldReturns.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(Policy::ObserverFault::kAbnormalNativeReturn);
		}
	}

	void OnFirstPersonEnter() noexcept
	{
		if (!IsEnabled())
			return;
		if (g_thread.firstPersonActive) {
			LatchFault(Policy::ObserverFault::kMalformedState);
			return;
		}
		g_thread.firstPersonActive = true;
		g_thread.firstPersonReturnedNormally = false;
		g_thread.exactPaneReturnCohort = ExactPaneReturnCohort::kZero;
	}

	GenericDrawToken OnGenericDrawEntry(RE::BSRenderPass* pass) noexcept
	{
		GenericDrawToken token{};
		if (!IsEnabled() || !g_thread.firstPersonActive ||
			!g_thread.worldReturnedNormally || !pass)
			return token;
		RawCandidate first{};
		RawCandidate second{};
		if (!ReadCandidatePairGuarded(first, second, pass) ||
			!SameRawCandidate(first, second) || !second.passIsExactFirstPersonPane ||
			!RefreshLifecycle(second)) {
			return token;
		}
		const auto candidate = BuildCandidateEvidence(second);
		const auto binding = Policy::MakeCandidateBinding(candidate);
		if (binding != g_thread.evidence.postWorld.pose.binding ||
			Exact(second).paneIdentity == 0)
			return token;
		if (g_thread.entrySequence ==
			(std::numeric_limits<std::uint64_t>::max)()) {
			LatchFault(Policy::ObserverFault::kTokenExhausted);
			return token;
		}
		const auto sequence = ++g_thread.entrySequence;
		auto& pending = g_thread.pending[
			(sequence - 1u) % g_thread.pending.size()];
		if (pending.valid) {
			g_counters.pendingOverflows.fetch_add(1, std::memory_order_relaxed);
			LatchFault(Policy::ObserverFault::kCandidateOverflow);
			return token;
		}
		TargetRead target{};
		(void)ReadMainTargetGuarded(target);
		pending = {
			.sourceFrame = g_thread.sourceFrame,
			.entrySequence = sequence,
			.binding = binding,
			.paneIdentity = Exact(second).paneIdentity,
			.targetAtEntry = target,
			.visibleAndNotAppCulled = Exact(second).visibleAndNotAppCulled,
			.valid = true
		};
		token.sourceFrame = g_thread.sourceFrame;
		token.entrySequence = sequence;
		token.paneIdentity = Exact(second).paneIdentity;
		token.active = true;
		return token;
	}

	void OnGenericDrawReturnedNormally(const GenericDrawToken& token) noexcept
	{
		if (!token.active || !IsEnabled() || !g_thread.firstPersonActive ||
			token.sourceFrame != g_thread.sourceFrame)
			return;
		auto& slot = g_thread.pending[
			(token.entrySequence - 1u) % g_thread.pending.size()];
		if (!slot.valid || slot.sourceFrame != token.sourceFrame ||
			slot.entrySequence != token.entrySequence ||
			slot.paneIdentity != token.paneIdentity) {
			return;
		}
		const PendingDraw pending = slot;
		slot.valid = false;

		RawCandidate first{};
		RawCandidate second{};
		if (!ReadCandidatePairGuarded(first, second) ||
			!SameRawCandidate(first, second) || !RefreshLifecycle(second) ||
			Exact(second).paneIdentity != pending.paneIdentity) {
			return;
		}
		const auto candidate = BuildCandidateEvidence(second);
		const auto binding = Policy::MakeCandidateBinding(candidate);
		if (binding != pending.binding)
			return;

		TargetRead targetAtReturn{};
		(void)ReadMainTargetGuarded(targetAtReturn);
		const auto pose = BuildPoseSnapshot(
			Policy::PosePhase::kExactFirstPersonPaneDraw,
			binding, Exact(second).paneWorld);
		Policy::MainTargetEvidence target{
			.sourceFrame = g_thread.sourceFrame,
			.paneDrawSnapshotToken = pose.snapshotToken,
			.expectedMainTarget = pending.targetAtEntry.identity,
			.observedAtPaneDrawReturn = targetAtReturn.identity,
			.prospectivePrivateColorResourceIdentity = 0,
			.prospectivePrivateDepthResourceIdentity = 0,
			.currentMainColorWritable = targetAtReturn.colorWritable,
			.currentMainDepthWritable = targetAtReturn.depthWritable,
			.currentViewportMatchesMainView =
				targetAtReturn.viewportMatchesShadowState,
			.contextAndTargetsBelongToExactDevice =
				targetAtReturn.contextAndTargetsExactDevice,
			.targetStableAcrossNativePaneDraw =
				pending.targetAtEntry.completed && targetAtReturn.completed &&
				Policy::SameMainTargetResourceIdentity(
					pending.targetAtEntry.identity, targetAtReturn.identity),
			.privateTargetUnbound = false,
			.noOutputUAVPredicationOrStreamOutputConflict =
				targetAtReturn.noOutputConflict,
			.noResourceAliasObserved = targetAtReturn.noMainResourceAlias,
			.guardedReadCompleted = targetAtReturn.completed,
			.guardedReadFaulted = targetAtReturn.faulted,
			.noEnginePointerRetained = true
		};
		Policy::ExactFirstPersonPaneDrawEvidence paneDraw{
			.pose = pose,
			.target = target,
			.insideExactFirstPersonNativeScope = true,
			.exactCurrentFirstPersonPaneSubmitted = true,
			.paneDescendsExactCurrentMirrorItemAndPartClone =
				Exact(second).itemDirectChildOfPartClone &&
				Exact(second).paneDirectChildOfItem,
			.currentMainPlayerViewSubmission = true,
			.visibleAndNotAppCulled = pending.visibleAndNotAppCulled &&
				Exact(second).visibleAndNotAppCulled,
			.menuInventoryShadowAndPrivatePassExcluded = true,
			.nativeDrawEntered = true,
			.nativeDrawReturnedNormally = true,
			.poseCopiedAfterNormalReturn = true,
			.callbackFaulted = false,
			.noEnginePointerRetained = true
		};
		const auto priorCohort = g_thread.exactPaneReturnCohort;
		const bool firstReturn = priorCohort == ExactPaneReturnCohort::kZero;
		// This observer is a value-only candidate provider. Validate the latest
		// return completely at outer close, but leave cross-return immutable
		// identity to the reflective runtime that owns the retained resources.
		// Keeping the duplicate observer comparison here blocked every live frame
		// before the reflective runtime could evaluate its stricter cohort gate.
		const bool sameImmutableIdentity =
			priorCohort == ExactPaneReturnCohort::kOne;
		const auto nextCohort = ObserveExactPaneReturn(
			priorCohort, sameImmutableIdentity);
		if (firstReturn || nextCohort ==
				ExactPaneReturnCohort::kTwoSameImmutableIdentity) {
			// The last independently proven exact-pane normal return is
			// authoritative for pass-local pose and viewport presentation.
			g_thread.evidence.paneDraw = paneDraw;
			g_thread.evidence.visiblePaneClearance.
				paneIntersectsCurrentMainView = true;
			g_thread.evidence.visiblePaneClearance.
				paneVisibleAndNotAppCulled = paneDraw.visibleAndNotAppCulled;
		} else if (nextCohort == ExactPaneReturnCohort::kRejected) {
			g_thread.evidence.paneDraw.exactCurrentFirstPersonPaneSubmitted = false;
		}
		g_thread.exactPaneReturnCohort = nextCohort;
		g_counters.exactPaneDrawReturns.fetch_add(1, std::memory_order_relaxed);
	}

	void OnFirstPersonReturnedNormally() noexcept
	{
		if (!IsEnabled() || !g_thread.firstPersonActive)
			return;
		g_thread.firstPersonReturnedNormally = true;
		g_thread.evidence.safety.callbacksCompleted = true;
		g_thread.evidence.safety.callbackFaulted = false;
		g_thread.evidence.safety.nativeCallsReturnedNormally =
			g_thread.worldReturnedNormally;
		std::uint64_t token = 0;
		if (!IssueToken(g_thread.observationIssuer, token))
			return;
		g_thread.evidence.observationToken = token;

		Policy::ReadOnlyGateResult result{};
		if (g_thread.evidence.candidates.exactCount == 1 &&
			!Policy::IsValidApprovedCandidate(g_thread.evidence.candidate)) {
			result.status = Policy::ReadOnlyGateStatus::kApprovedCandidateInvalid;
		} else if (g_thread.evidence.candidates.exactCount == 1 &&
			!AcceptsExactPaneReturnCohort(
				g_thread.exactPaneReturnCohort)) {
			result.status =
				Policy::ReadOnlyGateStatus::kFirstPersonPaneDrawInvalid;
		} else {
			result = Policy::EvaluateReadOnlyGate(g_thread.evidence);
		}
		RecordGateResult(result);
	}

	void OnFirstPersonFinally() noexcept
	{
		const bool active = g_thread.firstPersonActive;
		const bool returned = g_thread.firstPersonReturnedNormally;
		g_thread.firstPersonActive = false;
		g_thread.firstPersonReturnedNormally = false;
		if (active && !returned && IsEnabled()) {
			g_counters.abnormalFirstPersonReturns.fetch_add(
				1, std::memory_order_relaxed);
			LatchFault(Policy::ObserverFault::kAbnormalNativeReturn);
		}
	}

	void FailStopCallbackFault() noexcept
	{
		LatchFault(Policy::ObserverFault::kCallbackFault);
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
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		auto previous = g_lastPeriodicLogMilliseconds.load(
			std::memory_order_relaxed);
		if (now - previous <
			std::chrono::duration_cast<std::chrono::milliseconds>(
				kDiagnosticPeriod).count()) {
			return;
		}
		if (!g_lastPeriodicLogMilliseconds.compare_exchange_strong(
				previous, now, std::memory_order_relaxed)) {
			return;
		}
		LogDiagnostics(
			g_faultDiagnosticPending.exchange(false, std::memory_order_acq_rel) ?
				"fault" : "periodic");
	}

	void LogDiagnostics(const char* reason)
	{
		if (!Requested())
			return;
		const auto status = static_cast<Policy::ReadOnlyGateStatus>(
			g_lastGateStatus.load(std::memory_order_acquire));
		const auto freshnessStatus = static_cast<Freshness::FreshnessStatus>(
			g_lastFreshnessStatus.load(std::memory_order_acquire));
		logger::info(
			"[RR][HandMirrorApprovedReadOnly] {} requested={} enabled={} faultStopped={} firstFault={} shippingStyle(ready/forms/files)=0x{:02X}/0x{:02X}/0x{:02X} frames={} guardedReads={} guardedFaults={} postWorld={} exactCandidates={} exactPaneNormalReturns={} mainTargetReads={} mainTargetFaults={} gateEvaluations={} rejects={} admissions={} lastStatus={} blockingTelemetry(lastGraph/lastWant;graphTrue/wantTrue/disagreement)={}/{};{}/{}/{} preFreshness(checks/passes/lastMask;receipt/frame/tuple/raceSex/perspective/firstSchema/thirdSchema/visible/exactPass/owner/candidateRead/pair/lifecycle)={}/{}/0x{:04X};{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{} receipt(S/M/G)={}/{}/{} freshness(last/direct/seeds/calibrations/calibratedRereads/resets/rejects)={}/{}/{}/{}/{}/{}/{} hidden3P(graphics/root/clone/item/pane)={}/{}/{}/{}/{} observerHiddenAnimationAuthority=false observerVisiblePaneClearanceAuthority=false observerCaptureAuthority=false observerPublicationAuthority=false observerDeliveryAuthority=false observerAPIExposure=false observerTargetAllocation=false observerSceneMutation=false observerRuntimeAcceptance=false",
			reason ? reason : "diagnostic", Requested(), IsEnabled(),
			IsFaultStopped(), g_firstFault.load(std::memory_order_acquire),
			g_readyStyleMask.load(std::memory_order_acquire),
			g_formsPinnedStyleMask.load(std::memory_order_acquire),
			g_verifiedFileMasks[0].load(std::memory_order_acquire),
			g_counters.frames.load(std::memory_order_relaxed),
			g_counters.guardedReads.load(std::memory_order_relaxed),
			g_counters.guardedReadFaults.load(std::memory_order_relaxed),
			g_counters.postWorldSnapshots.load(std::memory_order_relaxed),
			g_counters.exactCandidates.load(std::memory_order_relaxed),
			g_counters.exactPaneDrawReturns.load(std::memory_order_relaxed),
			g_counters.mainTargetReads.load(std::memory_order_relaxed),
			g_counters.mainTargetReadFaults.load(std::memory_order_relaxed),
			g_counters.gateEvaluations.load(std::memory_order_relaxed),
			g_counters.ordinaryRejects.load(std::memory_order_relaxed),
			g_counters.admissions.load(std::memory_order_relaxed),
			GateStatusName(status),
			g_lastGraphIsBlocking.load(std::memory_order_relaxed),
			g_lastActorWantsBlocking.load(std::memory_order_relaxed),
			g_counters.blockingGraphTrue.load(std::memory_order_relaxed),
			g_counters.blockingWantTrue.load(std::memory_order_relaxed),
			g_counters.blockingSignalDisagreements.load(
				std::memory_order_relaxed),
			g_counters.preFreshnessChecks.load(std::memory_order_relaxed),
			g_counters.preFreshnessPasses.load(std::memory_order_relaxed),
			g_lastPreFreshnessRejectMask.load(std::memory_order_relaxed),
			g_counters.preReceiptInvalidRejects.load(std::memory_order_relaxed),
			g_counters.preGraphicsFrameMismatchRejects.load(
				std::memory_order_relaxed),
			g_counters.preExactTupleRejects.load(std::memory_order_relaxed),
			g_counters.preRaceSexRejects.load(std::memory_order_relaxed),
			g_counters.prePerspectiveRejects.load(std::memory_order_relaxed),
			g_counters.preFirstSchemaRejects.load(std::memory_order_relaxed),
			g_counters.preThirdSchemaRejects.load(std::memory_order_relaxed),
			g_counters.preVisiblePaneRejects.load(std::memory_order_relaxed),
			g_counters.preExactPassRejects.load(std::memory_order_relaxed),
			g_counters.preOwnerIdentityRejects.load(std::memory_order_relaxed),
			g_counters.preCandidateReadRejects.load(std::memory_order_relaxed),
			g_counters.preCandidatePairMismatchRejects.load(
				std::memory_order_relaxed),
			g_counters.preLifecycleRefreshRejects.load(
				std::memory_order_relaxed),
			g_lastReceiptSource.load(std::memory_order_relaxed),
			g_lastReceiptMainView.load(std::memory_order_relaxed),
			g_lastReceiptGraphics.load(std::memory_order_relaxed),
			FreshnessStatusName(freshnessStatus),
			g_counters.freshnessDirectCurrent.load(std::memory_order_relaxed),
			g_counters.freshnessSeedsDark.load(std::memory_order_relaxed),
			g_counters.freshnessCalibrations.load(std::memory_order_relaxed),
			g_counters.freshnessCalibratedRereads.load(
				std::memory_order_relaxed),
			g_counters.freshnessResetsDark.load(std::memory_order_relaxed),
			g_counters.freshnessRejects.load(std::memory_order_relaxed),
			g_lastGraphicsFrame.load(std::memory_order_relaxed),
			g_lastThirdRootUpdate.load(std::memory_order_relaxed),
			g_lastThirdCloneUpdate.load(std::memory_order_relaxed),
			g_lastThirdItemUpdate.load(std::memory_order_relaxed),
			g_lastThirdPaneUpdate.load(std::memory_order_relaxed));
	}
}
