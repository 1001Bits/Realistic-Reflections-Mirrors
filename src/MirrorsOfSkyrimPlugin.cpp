#include "PCH.h"
#include "MirrorFeatures.h"
#include "MirrorContentProfile.h"
#include "MirrorFixtureAnchors.h"
#include "MirrorPlayerAimSteady.h"
#include "MirrorSunShadows.h"
#include "MirrorENBParameters.h"
#include "MirrorENBScreenMask.h"

#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorReadOnlyObserver.h"
#include "HandMirrorReflectionRuntime.h"
#include "HandMirrorSettings.h"
#include "HandMirrorVRGripOffset.h"
#include "MirrorAuthorRegistrationRuntime.h"
#include "MirrorAcquisition.h"
#include "MirrorsOfSkyrimCameraOverride.h"
#include "MirrorMainSceneListBorrow.h"
#include "MirrorMainSceneListBorrowPolicy.h"
#include "MirrorsOfSkyrimPaneDelivery.h"
#include "MirrorPaneRenderer.h"
#include "MirrorsOfSkyrimPlayerInclusion.h"
#include "MirrorsOfSkyrimRecognition.h"
#include "MirrorsOfSkyrimShadowMapBypass.h"
#if defined(MOS_VERIFY_HARNESS)
#include "AutonomousTestControl.h"
#endif
#include "MirrorActivation.h"
#include "MirrorVRReadinessPolicy.h"
#include "ProductActivationIntegration.h"
#include "SKSELogDirectoryPolicy.h"
#include "SecondView.h"
#include "StandingMirrorPlacement.h"

// vcpkg's detours static library carries this debug-CRT import reference even
// in Release.  Keep the product DLL self-contained without pulling ucrtd.
extern "C" int __cdecl MOS_CrtDbgReportStub(
	int /*reportType*/, const char* /*filename*/, int /*lineNumber*/,
	const char* /*moduleName*/, const char* /*format*/, ...)
{
	return 0;
}
extern "C" decltype(&MOS_CrtDbgReportStub) __imp__CrtDbgReport =
	&MOS_CrtDbgReportStub;

using namespace std::literals;

SKSEPluginInfo(
	.Version = REL::Version{ 1, 0, 0, 0 },
	.Name = "RealisticReflectionsMirrors"sv,
	.Author = "Realistic Reflections Project"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::PluginDeclaration::RuntimeCompatibility(
		REL::Version{1, 5, 97, 0}, REL::Version{1, 6, 1170, 0},
		REL::Version{1, 6, 1179, 0},
		REL::Version{1, 6, 1179, 1},  // SKSE GOG: RUNTIME_TYPE_GOG in the low nibble
		REL::Version{1, 7, 104, 0},
		REL::Version{1, 4, 15, 0}))

namespace
{
	[[nodiscard]] bool AELightingMaskComparisonRequested() noexcept
	{
		if (MirrorFeatures::Enabled(L"MirrorsOfSkyrim_AELightingMaskTest.enable")) return true;
		// Legacy unsupported-runtime opt-in; inspect once, never in a draw.
		const HANDLE file = CreateFileW(
			L"Data\\MirrorsOfSkyrim_AELightingMaskTest.enable", GENERIC_READ,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		FILE_ATTRIBUTE_TAG_INFO tag{};
		FILE_STANDARD_INFO standard{};
		const bool accepted = GetFileType(file) == FILE_TYPE_DISK &&
			GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof(tag)) &&
			GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) &&
			(tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
				FILE_ATTRIBUTE_DEVICE)) == 0 &&
			!standard.Directory && standard.EndOfFile.QuadPart == 0;
		(void)CloseHandle(file);
		return accepted;
	}

	[[nodiscard]] MirrorActivation::Runtime ProductRuntime() noexcept
	{
		const auto version = REL::Module::get().version();
		if (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 })
			return MirrorActivation::Runtime::kSkyrimSE1597;
		if (REL::Module::IsAE() &&
			(SupportedRuntimePolicy::IsSupportedAEVersion(version) ||
				SupportedRuntimePolicy::IsExactAE161179Runtime()))
			return MirrorActivation::Runtime::kSkyrimAE161170;
		if (SupportedRuntimePolicy::IsExactVRRuntime())
			return MirrorActivation::Runtime::kSkyrimVR1415;
		return MirrorActivation::Runtime::kUnknown;
	}

	[[nodiscard]] MirrorActivation::DataReadiness Readiness(bool ready) noexcept
	{
		return ready ? MirrorActivation::DataReadiness::kReady :
			MirrorActivation::DataReadiness::kMissing;
	}

	[[nodiscard]] MirrorActivation::ProductDataReadiness BuildDataReadiness() noexcept
	{
		const bool sharedReady = SecondView::HooksReady() &&
			MirrorCameraOverride::HookReady() &&
			MirrorPaneDelivery::SharedSeamReady() &&
			MirrorPlayerInclusion::HookReady();
		const bool mirrorReady = sharedReady &&
			MirrorRecognition::MirrorsOfSkyrimDataReady() &&
			MirrorRecognition::LightingSetupObserverReady() &&
			MirrorMainSceneListBorrowPolicy::ProductReady(
				MirrorActivation::kMirrorCorrectnessProfile.exactMainSceneLists,
				MirrorMainSceneListBorrow::HookInstalled());

		MirrorActivation::ProductDataReadiness readiness;
		readiness.Set(
			MirrorActivation::Product::kMirrorsOfSkyrim,
			Readiness(mirrorReady));
		return readiness;
	}

	void InitializeLog()
	{
		auto path = SKSELogDirectoryPolicy::Resolve(
			logger::log_directory(),
			REL::Module::IsVR(),
			REL::Module::get().version());
		if (!path)
			return;
		*path /= "RealisticReflectionsMirrors.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
			path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] %v");
		logger::info("[MOS] SKSE log file={}", path->string());
	}

	void LogMirrorDiagnostics()
	{
		SecondView::LogDiagnostics("game-loaded");
		HandMirrorReadOnlyObserver::LogDiagnostics("game-loaded");
		HandMirrorApprovedContentReadOnlyObserver::LogDiagnostics("game-loaded");
		HandMirrorReflectionRuntime::LogDiagnostics("game-loaded");
		MirrorPaneDelivery::LogDiagnostics("game-loaded");
		MirrorShadowMapBypass::LogDiagnostics("game-loaded");
		MirrorENBScreenMask::LogDiagnostics("game-loaded");
	}

	[[nodiscard]] bool ActivationHasLoadedMaster() noexcept
	{
		const auto committed = MirrorActivation::GlobalLifecycle().Committed();
		return committed && !committed->IsConflicted() &&
			MirrorActivation::Contains(
				committed->Masks().activeProducts,
				MirrorActivation::Product::kMirrorsOfSkyrim);
	}

	std::atomic<bool> g_loggedActivationDeferral{ false };

	[[nodiscard]] bool TryCommitMirrorActivation(const char* reason) noexcept
	{
		auto& life = MirrorActivation::GlobalLifecycle();
		if (life.Stage() == MirrorActivation::LifecycleStage::kCommitted)
			return ActivationHasLoadedMaster();
		if (life.Stage() != MirrorActivation::LifecycleStage::kPrepared)
			return false;
		const auto readiness = BuildDataReadiness();
		if (readiness.Get(MirrorActivation::Product::kMirrorsOfSkyrim) !=
			MirrorActivation::DataReadiness::kReady) {
			if (!g_loggedActivationDeferral.exchange(true, std::memory_order_acq_rel)) {
				logger::warn(
					"[MOS][Activation] {} deferred: RealisticReflectionsMirrors.esm is not ready yet",
					reason);
			}
			return false;
		}
		const bool committed = life.CommitDataReadiness(readiness);
		logger::info("[MOS][Activation] {} commit={}", reason, committed);
		return committed;
	}

	void PromoteCommittedActivation() noexcept
	{
		if (!ActivationHasLoadedMaster())
			return;
		MirrorRecognition::OnMirrorActivationCommitted();
		SecondView::OnProductActivationCommitted();
		const auto committed = MirrorActivation::GlobalLifecycle().Committed();
		const bool wallMirrorLightStabilityEnabled =
			committed && !committed->IsConflicted() &&
			MirrorActivation::Contains(
				committed->Masks().RuntimeEnable(),
				MirrorActivation::Feature::kMirrorPaneDraw);
		MirrorShadowMapBypass::OnActivationCommitted(
			wallMirrorLightStabilityEnabled,
			HandMirrorReflectionRuntime::IsEnabled());
		MirrorPaneDelivery::OnMirrorActivationCommitted();
	}

	void RetryDeferredMirrorActivation(const char* reason) noexcept
	{
		if (ActivationHasLoadedMaster())
			return;
		MirrorRecognition::RecoverStandingMirrors(reason);
		if (TryCommitMirrorActivation(reason))
			PromoteCommittedActivation();
	}

	std::atomic<bool> g_gameLoadedSeen{ false };

	void HandleGameLoaded(const char* reason)
	{
		g_gameLoadedSeen.store(true, std::memory_order_release);
		// 2026-09-10 05:30 AE run: the process exited cleanly right after the
		// acquisition lines of this handler, with no crash log.  Every stage is
		// therefore marked and any C++ exception is logged instead of escaping
		// into SKSE's dispatcher, where it would terminate the game silently.
		const char* stage = "begin";
		try {
			if (!REL::Module::IsVR()) HandMirrorSettings::ResetForNewDraw();
			stage = "pane-delivery"; MirrorPaneDelivery::OnGameLoaded();
			stage = "vr-grip"; HandMirrorVRGripOffset::OnGameLoaded();
			stage = "second-view"; SecondView::OnGameLoaded();
			stage = "hand-raw-observer"; HandMirrorReadOnlyObserver::OnGameLoaded();
			stage = "hand-approved-observer"; HandMirrorApprovedContentReadOnlyObserver::OnGameLoaded();
			stage = "hand-runtime"; HandMirrorReflectionRuntime::OnGameLoaded();
			stage = "recognition"; MirrorRecognition::OnGameLoaded();
			stage = "activation"; RetryDeferredMirrorActivation("game-loaded");
			stage = "shadow-map-bypass"; MirrorShadowMapBypass::OnGameLoaded();
			stage = "placement"; StandingMirrorPlacement::OnGameLoaded();
			stage = "acquisition"; MirrorAcquisition::OnGameLoaded();
			logger::info("[MOS] game-loaded ({}) lifecycle complete; logging diagnostics", reason);
			stage = "diagnostics"; LogMirrorDiagnostics();
			stage = "placement-diagnostics"; StandingMirrorPlacement::LogDiagnostics(reason);
		} catch (const std::exception& error) {
			logger::error("[MOS] game-loaded ({}) stage '{}' threw: {}", reason, stage, error.what());
		} catch (...) {
			logger::error("[MOS] game-loaded ({}) stage '{}' threw a non-standard exception", reason, stage);
		}
	}

	/**
	 * A session started with `coc` from the main menu receives neither
	 * kNewGame nor kPostLoadGame from SKSE (AE 1.7.104, 2026-09-10 run: no
	 * game-loaded diagnostics, no merchant stocking, placement lifecycle never
	 * advanced).  The first fully loaded cell then stands in for the missing
	 * message, once, on the main thread via the SKSE task queue.
	 */
	class CellLoadFallbackSink final :
		public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
	{
	public:
		static CellLoadFallbackSink& GetSingleton()
		{
			static CellLoadFallbackSink instance;
			return instance;
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESCellFullyLoadedEvent* event,
			RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
		{
			if (!event)
				return RE::BSEventNotifyControl::kContinue;
			if (g_gameLoadedSeen.load(std::memory_order_acquire)) {
				// Inn/home frames are Initially Disabled XESP children.
				// Enable(controller) while that cell is unloaded does not
				// enable them, so the live bInns/bHomes setting is applied
				// again after every attach.
				MirrorAcquisition::QueueFurnishings();
				RetryDeferredMirrorActivation("cell-loaded-retry");
				return RE::BSEventNotifyControl::kContinue;
			}
			if (m_queued.exchange(true, std::memory_order_acq_rel)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() {
					if (g_gameLoadedSeen.load(std::memory_order_acquire))
						return;
					logger::warn(
						"[MOS] no kPostLoadGame/kNewGame before the first loaded cell (main-menu coc?); running the game-loaded lifecycle now");
					HandleGameLoaded("cell-loaded-fallback");
				});
			} else {
				m_queued.store(false, std::memory_order_release);
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		std::atomic<bool> m_queued{ false };
	};

	void MessageHandler(SKSE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			MirrorAuthorRegistrationRuntime::OnPostLoad();
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			(void)ProductActivationIntegration::RunInputLoaded(
				[] {
					return MirrorActivation::GlobalLifecycle().PrepareAtInputLoaded(
						ProductRuntime());
				},
				[] {
					const auto prepared =
						MirrorActivation::GlobalLifecycle().Prepared();
					const bool reflectiveHandRuntimeAllowed = prepared &&
						!prepared->IsConflicted() &&
						(prepared->SupportedProductMask() &
						 MirrorActivation::kMirrorProducts) != 0;
					if (reflectiveHandRuntimeAllowed && MirrorContentProfile::AddonCandidatePresent())
						HandMirrorReflectionRuntime::PrepareAtInputLoaded();
					HandMirrorApprovedContentReadOnlyObserver::PrepareAtInputLoaded(
						HandMirrorReflectionRuntime::Requested());
					HandMirrorReadOnlyObserver::PrepareAtInputLoaded();
					SecondView::OnInputLoaded();
					const bool wallMirrorCameraRequested =
						prepared && !prepared->IsConflicted() &&
						MirrorActivation::Contains(
							prepared->Masks().HookRequest(),
							MirrorActivation::Feature::kSharedCameraOverride);
					MirrorCameraOverride::OnInputLoaded(
						SecondView::HooksReady(), wallMirrorCameraRequested,
						HandMirrorReflectionRuntime::Requested());
					MirrorPaneDelivery::OnInputLoaded(SecondView::HooksReady());
					HandMirrorReadOnlyObserver::CompleteInputLoaded(
						SecondView::RenderWorldDriverReadyForReadOnlyObserver());
				});
			HandMirrorSettings::OnInputLoaded();
			MirrorENBScreenMask::OnInputLoaded();
			// Ask ENB what its water and reflection settings are, once. Owner,
			// 2026-09-16: ENB water in a reflection is flat blue and its SSR
			// shows the main view. Read-only; nothing is written, because any
			// write would change the main view too.
			MirrorENBParameters::PrepareCaptureSuppression();
			if (MirrorContentProfile::AddonCandidatePresent())
				StandingMirrorPlacement::OnInputLoaded();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("[MOS] Data loaded — mirror modules initializing");
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				const auto* addon =
					data->LookupLoadedModByName(MirrorContentProfile::kAddonPlugin);
				if (!addon)
					addon = data->LookupLoadedLightModByName(
						MirrorContentProfile::kAddonPlugin);
				MirrorContentProfile::addonLoaded.store(
					addon &&
					data->LookupForm<RE::TESGlobal>(0x826, MirrorContentProfile::kAddonPlugin) &&
					data->LookupForm<RE::TESObjectARMO>(0x900, MirrorContentProfile::kAddonPlugin) &&
					data->LookupForm<RE::TESObjectSTAT>(0x800, MirrorContentProfile::kCorePlugin),
					std::memory_order_release);
			}
			logger::info("[RR][Content] core=Realistic Reflections - Mirrors; Mirrors of Skyrim add-on loaded={}",
				MirrorContentProfile::AddonLoaded());
#if defined(MOS_VERIFY_HARNESS)
			AutonomousTestControl::OnDataLoaded(SecondView::HooksReady());
#endif
			if (REL::Module::IsVR()) {
				const bool vrikLoaded = GetModuleHandleW(L"vrik.dll") != nullptr;
				logger::info("[MOS][VR] VRIK baseline: loaded={} avatar=live-third-person equippedPane=third-person poseOwner=VRIK",
					vrikLoaded);
				if (!vrikLoaded)
					logger::warn("[MOS][VR] VRIK is required for the supported VR avatar and hand-mirror configuration");
			}
			HandMirrorSettings::Load();
			HandMirrorVRGripOffset::OnDataLoaded();
			ProductActivationIntegration::RunDataLoaded(
				[] {
					MirrorRecognition::OnDataLoaded();
					const auto prepared =
						MirrorActivation::GlobalLifecycle().Prepared();
					const bool wallMirrorLightStabilityRequested =
						prepared && !prepared->IsConflicted() &&
						MirrorActivation::Contains(
							prepared->Masks().Requested(),
							MirrorActivation::Feature::kMirrorPaneDraw);
					MirrorShadowMapBypass::ConfigureAE17104Comparison(
						AELightingMaskComparisonRequested());
					MirrorShadowMapBypass::OnDataLoaded(
						wallMirrorLightStabilityRequested,
						HandMirrorReflectionRuntime::Requested());
					HandMirrorReadOnlyObserver::OnDataLoaded();
					HandMirrorApprovedContentReadOnlyObserver::OnDataLoaded();
					HandMirrorReflectionRuntime::OnDataLoaded();
					SecondView::OnDataLoaded();
					MirrorPaneDelivery::OnDataLoaded();
					HandMirrorReflectionRuntime::CompleteDataLoadedActivation();
				},
				[] {
					return TryCommitMirrorActivation("data-loaded");
				},
				[] {
					PromoteCommittedActivation();
				},
				[] {},
				[] {});
			MirrorAuthorRegistrationRuntime::OnDataLoaded();
			if (MirrorContentProfile::AddonLoaded()) StandingMirrorPlacement::OnDataLoaded();
			MirrorFixtureAnchors::OnDataLoaded();
			MirrorPlayerAimSteady::Install();
			MirrorAcquisition::OnDataLoaded();
			if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton())
				holder->AddEventSink<RE::TESCellFullyLoadedEvent>(&CellLoadFallbackSink::GetSingleton());
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			MirrorAcquisition::OnPreLoadGame();
			// Quiesce every owner of save-specific scene pointers before Skyrim
			// tears the current world down. The standalone entry point must keep
			// the hand-capture lifecycle ordering of the proven combined runtime.
			SecondView::OnPreLoadGame();
			HandMirrorReflectionRuntime::OnPreLoadGame();
			MirrorRecognition::OnPreLoadGame();
			StandingMirrorPlacement::OnPreLoadGame();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			HandleGameLoaded("game-loaded");
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	InitializeLog();
	if (GetModuleHandleW(L"MirrorsOfSkyrim.dll")) {
		logger::critical("The old combined MirrorsOfSkyrim.dll is loaded. Install one reflection engine; refusing duplicate render hooks.");
		return false;
	}
#if defined(MOS_VERIFY_HARNESS)
	AutonomousTestControl::Initialize();
#endif
	logger::info("[MOS][Features] {} (internal defaults; user options in MCM)", MirrorFeatureDefaults::kProfileStamp);
	MirrorSunShadows::Install();
	MirrorPaneRenderer::SetDiagnosticSink([](const char* message) noexcept {
		try {
			logger::info("[RR][PaneRenderer] {}", message ? message : "");
		} catch (...) {
		}
	});
	logger::info("Realistic Reflections - Mirrors v{} loading (runtime {}{})", RR_VERSION_STR,
		REL::Module::IsVR() ? "VR " : "", skse->RuntimeVersion().string());
	if (REL::Module::IsVR()) {
		logger::info(
			"[MOS][VR] Skyrim VR 1.4.15 port active (ready={}); readinessBlockers=0x{:08X}; every VR hook still verifies its own call-site bytes before install",
			MirrorVRReadinessPolicy::kCurrentSkyrimVR1415Decision.Ready(),
			MirrorVRReadinessPolicy::kCurrentSkyrimVR1415Decision.blockers);
	}

	SKSE::AllocTrampoline(1 << 14);

	if (auto* messaging = SKSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(MessageHandler)) {
			logger::critical("[MOS] Failed to register SKSE messaging listener");
			return false;
		}
	} else {
		logger::critical("[MOS] Messaging interface is null");
		return false;
	}
	if (auto* papyrus = SKSE::GetPapyrusInterface();
		!papyrus || !papyrus->Register(StandingMirrorPlacement::RegisterPapyrus) ||
			!papyrus->Register(HandMirrorSettings::RegisterPapyrus)) {
		logger::critical("[MOS] Failed to register Papyrus bridge");
		return false;
	}

	logger::info("[MOS] Load complete");
	return true;
}
