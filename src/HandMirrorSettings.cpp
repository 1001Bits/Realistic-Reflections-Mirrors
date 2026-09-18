#include "PCH.h"
#include "MirrorPerformance.h"
#include "MirrorBenchmark.h"
#include "MirrorContentProfile.h"
#include "MirrorShadowSettings.h"
#include "MirrorPrivateShadow.h"
#include "MirrorQualityPreset.h"
#include "MirrorAdaptiveQuality.h"
#include "MirrorCaptureSizing.h"
#include "MirrorCaptureOptimizations.h"
#include "SecondView.h"
#include "HandMirrorSafetySettings.h"
#include "SupportedRuntimePolicy.h"

#include "HandMirrorSettings.h"
#include "MirrorAcquisition.h"
#include "MirrorAcquisitionSettingsIni.h"
#include "HandMirrorSettingsIni.h"
#include "MirrorFleetSettingsIni.h"
#include "MirrorSettingsFile.h"
#include "HandMirrorWheelZoomPolicy.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorReflectionRuntime.h"
#include "HandMirrorLoweredPresentationPolicy.h"


namespace HandMirrorSettings
{
	namespace
	{
		constexpr const char* kIniRelativePath =
			"Data\\SKSE\\Plugins\\RealisticReflectionsMirrors.ini";

		std::mutex g_fileMutex;
		std::atomic_bool g_inputRegistered{ false };
		std::atomic_bool g_drawEventsRegistered{ false };
		std::atomic_bool g_saveQueued{ false };
		[[nodiscard]] fs::path IniPath()
		{
			return fs::path(kIniRelativePath);
		}

		void QueueSave()
		{
			if (g_saveQueued.exchange(true, std::memory_order_acq_rel))
				return;
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([] {
					g_saveQueued.store(false, std::memory_order_release);
					(void)Save();
				});
			} else {
				g_saveQueued.store(false, std::memory_order_release);
				(void)Save();
			}
		}

		[[nodiscard]] RE::TESObjectARMO* HandMirrorArmor()
		{
			namespace Content = HandMirrorApprovedContentReadOnlyObserver;
			auto* data = RE::TESDataHandler::GetSingleton();
			return data ? data->LookupForm<RE::TESObjectARMO>(
				Content::kFiligreeV1ArmorLocalFormID, Content::kFiligreeV1PluginBasename) : nullptr;
		}

		class DrawZoomSink final :
			public RE::BSTEventSink<SKSE::ActionEvent>,
			public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			static DrawZoomSink& GetSingleton()
			{
				static DrawZoomSink sink;
				return sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* event,
				RE::BSTEventSource<SKSE::ActionEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!event || !player || event->actor != player ||
					!event->type.any(SKSE::ActionEvent::Type::kBeginDraw))
					return RE::BSEventNotifyControl::kContinue;
				auto* armor = HandMirrorArmor();
				for (bool firstPerson : {true, false}) {
					const auto biped = player->GetBiped(firstPerson);
					const auto* shield = biped ? biped->GetShieldObject() : nullptr;
					if (armor && shield && shield->item == armor) {
						ResetForNewDraw();
						break;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,
				RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (event && player && event->equipped && event->actor.get() == player) {
					auto* armor = HandMirrorArmor();
					if (armor && event->baseObject == armor->GetFormID())
						ResetForNewDraw();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		[[nodiscard]] HandMirrorWheelZoomPolicy::Context WheelContext()
		{
			HandMirrorWheelZoomPolicy::Context context;
			context.enabled = WheelZoomEnabled();
			if (REL::Module::IsVR() || !HandMirrorReflectionRuntime::IsEnabled() ||
				HandMirrorReflectionRuntime::LifecycleTransitionSuspended())
				return context;
			auto* ui = RE::UI::GetSingleton();
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsItemMenuOpen() ||
				ui->IsModalMenuOpen() || ui->IsApplicationMenuOpen() ||
				ui->IsMenuOpen(RE::Console::MENU_NAME) ||
				ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ||
				ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
				ui->IsMenuOpen(RE::JournalMenu::MENU_NAME))
				return context;
			context.gameplay = true;
			context.firstPerson = camera && camera->IsInFirstPerson();
			context.raised = player && player->IsBlocking() &&
				player->AsActorState()->IsWeaponDrawn();
			if (!context.firstPerson || !context.raised)
				return context;
			// Reuse the shipping plugin/local identity and the native shield slot.
			// No render-thread receipt or previous-frame raised state is retained.
			namespace Content = HandMirrorApprovedContentReadOnlyObserver;
			auto* data = RE::TESDataHandler::GetSingleton();
			const auto biped = player->GetBiped(true);
			auto* shield = biped ? biped->GetShieldObject() : nullptr;
			auto* armor = data ? data->LookupForm<RE::TESObjectARMO>(
				Content::kFiligreeV1ArmorLocalFormID, Content::kFiligreeV1PluginBasename) : nullptr;
			context.mirrorEquipped = armor && shield && shield->item == armor && shield->partClone;
			return context;
		}

		class WheelInputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static WheelInputSink& GetSingleton()
			{
				static WheelInputSink sink;
				return sink;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!events || !MirrorContentProfile::AddonLoaded() || !WheelZoomEnabled())
					return RE::BSEventNotifyControl::kContinue;
				bool checked = false;
				bool changed = false;
				HandMirrorWheelZoomPolicy::Context context;
				for (auto* event = *events; event; event = event->next) {
					auto* button = event->AsButtonEvent();
					if (!button || !HandMirrorWheelZoomPolicy::IsWheel(
						button->GetDevice() == RE::INPUT_DEVICE::kMouse, button->GetIDCode()))
						continue;
					if (!checked) {
						context = WheelContext();
						checked = true;
					}
					const float before = PortraitZoom();
					const auto result = HandMirrorWheelZoomPolicy::Evaluate(
						context, true, button->GetIDCode(),
						button->IsDown(), before);
					if (result.consume) {
						if (result.zoom != before) {
							(void)SetPortraitZoom(result.zoom);
							changed = true;
						}
						// Neutralize only this wheel event, even at a zoom limit. Keep
						// the engine-owned list intact and let RMB releases, movement
						// and menu keys in this batch continue to their normal sinks.
						button->SetUserEvent(RE::BSFixedString{});
						button->SetIDCode(RE::ControlMap::kInvalid);
						button->GetRuntimeData().value = 0.0F;
						button->GetRuntimeData().heldDownSecs = 0.0F;
					}
				}
				if (changed)
					QueueSave();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		bool GetMirrorDistanceControlsNative(RE::StaticFunctionTag*) { return MirrorRenderDistance::controls.load(); }
		int GetHandMirrorRenderDistanceNative(RE::StaticFunctionTag*) { return MirrorRenderDistance::Get(true); }
		int GetPlacedMirrorRenderDistanceNative(RE::StaticFunctionTag*) { return MirrorRenderDistance::Get(false); }
		int SetHandMirrorRenderDistanceNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = MirrorRenderDistance::Set(true, value); (void)Save();
			logger::info("[MOS][Settings] hand geometry/shadow distance={}", stored); return stored;
		}
		int SetPlacedMirrorRenderDistanceNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = MirrorRenderDistance::Set(false, value); (void)Save();
			logger::info("[MOS][Settings] placed geometry/shadow distance={}", stored); return stored;
		}
		// Papyrus bridge: MirrorsOfSkyrimNative (Global Native).
		float GetHandMirrorZoomNative(RE::StaticFunctionTag*)
		{
			return PortraitZoom();
		}

		float SetHandMirrorZoomNative(RE::StaticFunctionTag*, const float a_zoom)
		{
			const float stored = SetPortraitZoom(a_zoom);
			(void)Save();
			return stored;
		}

		float GetHandMirrorZoomDefaultNative(RE::StaticFunctionTag*)
		{
			return kDefaultPortraitZoom;
		}

		float GetHandMirrorZoomMinimumNative(RE::StaticFunctionTag*)
		{
			return kMinimumPortraitZoom;
		}

		float GetHandMirrorZoomMaximumNative(RE::StaticFunctionTag*)
		{
			return kMaximumPortraitZoom;
		}

		bool GetHandMirrorWheelZoomNative(RE::StaticFunctionTag*)
		{
			return WheelZoomEnabled();
		}
		bool GetPlacedMirrorControlsNative(RE::StaticFunctionTag*) { return MirrorFleetPolicy::enabled.load(); }
		bool GetHandMirrorResolutionControlsNative(RE::StaticFunctionTag*)
		{
			return HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load();
		}
		int GetHandMirrorRaisedResolutionNative(RE::StaticFunctionTag*) { return RaisedResolution(); }
		int GetHandMirrorLoweredResolutionNative(RE::StaticFunctionTag*) { return LoweredResolution(); }
		int SetHandMirrorRaisedResolutionNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = SetRaisedResolution(value); (void)Save(); return stored;
		}
		int SetHandMirrorLoweredResolutionNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = SetLoweredResolution(value); (void)Save(); return stored;
		}
		bool GetHandMirrorReflectWhenLoweredNative(RE::StaticFunctionTag*) { return ReflectWhenLowered(); }
		bool SetHandMirrorReflectWhenLoweredNative(RE::StaticFunctionTag*, bool value)
		{
			(void)SetReflectWhenLowered(value); (void)Save(); return value;
		}
		bool GetMirrorMerchantsEnabledNative(RE::StaticFunctionTag*) { return MirrorAcquisition::merchantsEnabled.load(); }
		bool GetMirrorCraftingEnabledNative(RE::StaticFunctionTag*) { return MirrorAcquisition::craftingEnabled.load(); }
		void RefreshMirrorFurnishingsNative(RE::StaticFunctionTag*) { MirrorAcquisition::QueueFurnishings(); }
		bool GetMirrorHomesEnabledNative(RE::StaticFunctionTag*) { return MirrorAcquisition::homesEnabled.load(); }
		bool GetMirrorInnsEnabledNative(RE::StaticFunctionTag*) { return MirrorAcquisition::innsEnabled.load(); }
		bool SetMirrorHomesEnabledNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorAcquisition::homesEnabled.store(value); MirrorAcquisition::QueueFurnishings(); (void)Save(); return value;
		}
		bool SetMirrorInnsEnabledNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorAcquisition::innsEnabled.store(value); MirrorAcquisition::QueueFurnishings(); (void)Save(); return value;
		}
		bool SetMirrorMerchantsEnabledNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorAcquisition::merchantsEnabled.store(value); MirrorAcquisition::QueueApply(); (void)Save(); return value;
		}
		bool SetMirrorCraftingEnabledNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorAcquisition::craftingEnabled.store(value); MirrorAcquisition::QueueApply(); (void)Save(); return value;
		}
		bool GetPlacedMirrorDynamicResolutionNative(RE::StaticFunctionTag*) { return MirrorFleetPolicy::dynamicResolution.load(); }
		// One control instead of five. Applying a level writes every value it
		// owns, on both mirrors, and saves once -- so the INI and the MCM agree
		// immediately rather than after the next reload.
		bool GetMirrorAutomaticQualityNative(RE::StaticFunctionTag*)
		{
			return MirrorQualityPreset::Automatic();
		}
		bool SetMirrorAutomaticQualityNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorQualityPreset::SetAutomatic(value);
			MirrorQualityPreset::Apply();
			(void)Save();
			logger::info("[MOS][Settings] automatic quality={}", value);
			return value;
		}
		std::int32_t GetMirrorQualityLevelNative(RE::StaticFunctionTag*)
		{
			return static_cast<std::int32_t>(MirrorQualityPreset::CurrentLevel());
		}
		std::int32_t SetMirrorQualityLevelNative(RE::StaticFunctionTag*, std::int32_t value)
		{
			const auto chosen = MirrorQualityPreset::Clamp(
				static_cast<std::uint32_t>(value < 0 ? 1 : value));
			MirrorQualityPreset::level.store(static_cast<std::uint32_t>(chosen));
			MirrorQualityPreset::Apply();
			(void)Save();
			logger::info("[MOS][Settings] quality level={}", static_cast<unsigned>(chosen));
			return static_cast<std::int32_t>(chosen);
		}
		// Owner 2026-09-16 (core MCM): Manual / Preset / Automatic as one selector.
		// Automatic adapts placed-mirror resolution and cadence together;
		// the stored manual values are untouched while it runs.
		std::int32_t GetMirrorQualityModeNative(RE::StaticFunctionTag*)
		{
			return static_cast<std::int32_t>(MirrorQualityPreset::CurrentMode());
		}
		std::int32_t SetMirrorQualityModeNative(RE::StaticFunctionTag*, std::int32_t value)
		{
			const auto chosen = MirrorQualityPreset::ClampMode(value);
			MirrorQualityPreset::mode.store(static_cast<std::uint32_t>(chosen));
			MirrorQualityPreset::Apply();
			(void)Save();
			logger::info("[MOS][Settings] quality mode={}", static_cast<unsigned>(chosen));
			return static_cast<std::int32_t>(chosen);
		}
		std::int32_t GetMirrorTargetFPSNative(RE::StaticFunctionTag*)
		{
			return MirrorQualityPreset::targetFPS.load();
		}
		std::int32_t SetMirrorTargetFPSNative(RE::StaticFunctionTag*, std::int32_t value)
		{
			const auto stored = static_cast<int>(MirrorAdaptiveQuality::TargetFPS(value));
			MirrorQualityPreset::targetFPS.store(stored);
			(void)Save();
			logger::info("[MOS][Settings] automatic quality target={} FPS", stored);
			return stored;
		}
		// The size the last standing-mirror capture used (on-screen sizing),
		// never above the current limit; the limit itself before any capture.
		std::int32_t GetMirrorEffectiveResolutionNative(RE::StaticFunctionTag*)
		{
			const int limit = MirrorFleetPolicy::EffectiveResolution();
			const int last = SecondView::LastPlacedCaptureResolution();
			return MirrorCaptureOptimizations::screenSizedCapture.load() && last > 0 ? (std::min)(last, limit) : limit;
		}
		// Owner 2026-09-17: "can we have a reflected face light slider? Don't change
		// the room light". Percent of the key light the reflected face receives.
		std::int32_t GetMirrorFaceLightNative(RE::StaticFunctionTag*)
		{
			return MirrorSurfaceLightPolicy::ClampFaceLight(MirrorSurfaceLightPolicy::faceLightPercent.load());
		}
		std::int32_t SetMirrorFaceLightNative(RE::StaticFunctionTag*, std::int32_t value)
		{
			const int stored = MirrorSurfaceLightPolicy::ClampFaceLight(value);
			MirrorSurfaceLightPolicy::faceLightPercent.store(stored, std::memory_order_relaxed);
			(void)Save();
			logger::info("[MOS][Settings] reflected face light={}%", stored);
			return stored;
		}
		// Captures follow the pane's shape (core run 9): the last capture's height.
		std::int32_t GetMirrorEffectiveHeightNative(RE::StaticFunctionTag*)
		{
			const int limit = MirrorFleetPolicy::EffectiveResolution();
			const int last = SecondView::LastPlacedCaptureHeight();
			return MirrorCaptureOptimizations::screenSizedCapture.load() && last > 0 ? (std::min)(last, limit) : limit;
		}
		// Automatic mode's current update rate, otherwise the configured one.
		std::int32_t GetMirrorEffectiveRefreshNative(RE::StaticFunctionTag*)
		{
			return MirrorFleetPolicy::EffectiveRefresh();
		}
		bool GetMirrorResolutionFollowsFramebufferNative(RE::StaticFunctionTag*)
		{
			return MirrorCaptureSizing::UsesFramebuffer();
		}
		// Owner 2026-09-16: the F8/F11 development hotkeys stay off unless asked for.
		bool GetMirrorDebugHotkeysNative(RE::StaticFunctionTag*)
		{
			return MirrorPerformance::DebugHotkeysEnabled();
		}
		// Owner 2026-09-17: the built-in benchmark, from the Development menu.
		bool StartMirrorBenchmarkNative(RE::StaticFunctionTag*)
		{
			if (!MirrorPerformance::DebugHotkeysEnabled())
				return false;
			MirrorBenchmark::Request();
			logger::info("[MOS][Benchmark] requested from the MCM; starts when the menu closes");
			return true;
		}
		bool SetMirrorDebugHotkeysNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorPerformance::debugHotkeys.store(value, std::memory_order_release);
			(void)Save();
			logger::info("[MOS][Settings] debug hotkeys={}", value);
			return value;
		}
		// Shadow reach, separate from object reach: run 36 measured 0.89 ms a
		// frame of caster work that the object slider dragged in without moving
		// a single visible shadow.
		std::int32_t GetHandMirrorShadowDistanceNative(RE::StaticFunctionTag*)
		{
			return MirrorRenderDistance::GetShadow(true);
		}
		std::int32_t GetPlacedMirrorShadowDistanceNative(RE::StaticFunctionTag*)
		{
			return MirrorRenderDistance::GetShadow(false);
		}
		std::int32_t SetHandMirrorShadowDistanceNative(RE::StaticFunctionTag*, std::int32_t value)
		{
			const auto stored = MirrorRenderDistance::SetShadow(true, value);
			(void)Save();
			logger::info("[MOS][Settings] hand shadow distance={}", stored);
			return stored;
		}
		std::int32_t SetPlacedMirrorShadowDistanceNative(RE::StaticFunctionTag*, std::int32_t value)
		{
			const auto stored = MirrorRenderDistance::SetShadow(false, value);
			(void)Save();
			logger::info("[MOS][Settings] placed shadow distance={}", stored);
			return stored;
		}
		// The ceiling is texel density against the configured map size, so the
		// MCM slider can stop where quality would start to fail rather than
		// letting a player pick a number that silently loses thin shadows.
		std::int32_t GetMirrorShadowDistanceMaximumNative(RE::StaticFunctionTag*)
		{
			return MirrorRenderDistance::ShadowCeiling(
				MirrorPrivateShadow::Resolution(false), MirrorPrivateShadow::kGuard);
		}
		bool GetMirrorShadowControlsNative(RE::StaticFunctionTag*) { return MirrorShadowSettings::controls.load(); }
		bool GetHandMirrorShadowsNative(RE::StaticFunctionTag*) { return MirrorShadowSettings::hand.load(); }
		bool GetPlacedMirrorShadowsNative(RE::StaticFunctionTag*) { return MirrorShadowSettings::placed.load(); }
		bool SetHandMirrorShadowsNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorShadowSettings::hand.store(value); (void)Save();
			logger::info("[MOS][Settings] hand mirror shadows={}",value); return value;
		}
		bool SetPlacedMirrorShadowsNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorShadowSettings::placed.store(value); (void)Save();
			logger::info("[MOS][Settings] placed mirror shadows={}",value); return value;
		}
		bool SetPlacedMirrorDynamicResolutionNative(RE::StaticFunctionTag*, bool value)
		{
			MirrorFleetPolicy::dynamicResolution.store(value); (void)Save(); return value;
		}
		int GetPlacedMirrorResolutionNative(RE::StaticFunctionTag*) { return MirrorFleetPolicy::resolution.load(); }
		int SetPlacedMirrorResolutionNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = MirrorFleetPolicy::Resolution(value);
			MirrorFleetPolicy::resolution.store(stored); (void)Save(); return stored;
		}
		int GetPlacedMirrorRefreshNative(RE::StaticFunctionTag*) { return MirrorFleetPolicy::refreshHz.load(); }
		int SetPlacedMirrorRefreshNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = MirrorFleetPolicy::Refresh(value);
			MirrorFleetPolicy::refreshHz.store(stored); (void)Save(); return stored;
		}
		int GetHandMirrorRefreshNative(RE::StaticFunctionTag*) { return MirrorFleetPolicy::handRefreshHz.load(); }
		int SetHandMirrorRefreshNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = MirrorFleetPolicy::Refresh(value);
			MirrorFleetPolicy::handRefreshHz.store(stored); (void)Save(); return stored;
		}
		int GetHandMirrorLoweredRefreshNative(RE::StaticFunctionTag*) { return MirrorFleetPolicy::handLoweredRefreshHz.load(); }
		int SetHandMirrorLoweredRefreshNative(RE::StaticFunctionTag*, int value)
		{
			const int stored = MirrorFleetPolicy::Refresh(value);
			MirrorFleetPolicy::handLoweredRefreshHz.store(stored); (void)Save(); return stored;
		}

		bool SetHandMirrorWheelZoomNative(RE::StaticFunctionTag*, const bool a_enabled)
		{
			const bool stored = SetWheelZoomEnabled(a_enabled);
			(void)Save();
			return stored;
		}
	}

	void Load()
	{
		std::scoped_lock lock(g_fileMutex);
		std::ifstream file(IniPath());
		const auto values = HandMirrorSettingsIni::Read(file);
		// A previous wheel position is not the starting composition for a new
		// session. SE/AE always start at the owner-selected face portrait.
		(void)SetPortraitZoom(REL::Module::IsVR() ? values.zoom : kDefaultPortraitZoom);
		(void)SetWheelZoomEnabled(values.wheel);
		(void)SetReflectWhenLowered(values.lowered);
		(void)SetRaisedResolution(values.raisedResolution);
		(void)SetLoweredResolution(values.loweredResolution);
		MirrorShadowSettings::hand.store(values.shadows);
		MirrorShadowSettings::controls.store(!REL::Module::IsVR() &&
			((REL::Module::IsSE() && REL::Module::get().version()==REL::Version{1,5,97,0}) ||
			 (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(REL::Module::get().version()))) &&
			HandMirrorSafetySettings::EmptyMarker(L"Data\\MirrorsOfSkyrim_ShadowControls.enable"));
		logger::info("[MOS][Settings] hand reflectWhenLowered={} raisedResolution={} loweredResolution={}",
			values.lowered, RaisedResolution(), LoweredResolution());
		file.clear(); file.seekg(0);
		const auto acquisition = MirrorAcquisitionSettingsIni::Read(file);
		MirrorAcquisition::merchantsEnabled.store(acquisition.merchants);
		MirrorAcquisition::craftingEnabled.store(acquisition.crafting);
		MirrorAcquisition::homesEnabled.store(acquisition.homes);
		MirrorAcquisition::innsEnabled.store(acquisition.inns);
		file.clear(); file.seekg(0);
		const auto placed = MirrorFleetSettingsIni::Read(file);
		MirrorFleetPolicy::resolution.store(placed.resolution);
		MirrorFleetPolicy::refreshHz.store(placed.refreshHz);
		MirrorFleetPolicy::handRefreshHz.store(placed.handRefreshHz);
		MirrorFleetPolicy::handLoweredRefreshHz.store(placed.handLoweredRefreshHz);
		MirrorFleetPolicy::dynamicResolution.store(placed.dynamicResolution);
		MirrorShadowSettings::placed.store(placed.shadows);
		MirrorShadowSettings::playerShadow.store(placed.playerShadow);
		MirrorShadowSettings::minimumSunLuminance.store(placed.minimumSunLight / 100.0f);
		(void)MirrorRenderDistance::SetShadow(true, values.shadowDistance);
		(void)MirrorRenderDistance::SetShadow(false, placed.shadowDistance);
		MirrorQualityPreset::mode.store(static_cast<std::uint32_t>(MirrorQualityPreset::ClampMode(placed.qualityMode)));
		MirrorQualityPreset::targetFPS.store(static_cast<int>(MirrorAdaptiveQuality::TargetFPS(placed.targetFPS)));
		MirrorPerformance::debugHotkeys.store(placed.debugHotkeys, std::memory_order_release);
		MirrorSurfaceLightPolicy::faceLightPercent.store(placed.faceLight, std::memory_order_relaxed);
		MirrorQualityPreset::level.store(static_cast<std::uint32_t>(placed.qualityLevel));
		MirrorPrivateShadow::resolution.store(static_cast<std::uint32_t>(placed.shadowResolution));
		MirrorPrivateShadow::detailResolution.store(static_cast<std::uint32_t>(placed.shadowDetailResolution));
		(void)MirrorRenderDistance::Set(true, values.renderDistance);
		(void)MirrorRenderDistance::Set(false, placed.renderDistance);
		MirrorRenderDistance::controls.store(MirrorShadowSettings::controls.load() &&
			HandMirrorSafetySettings::EmptyMarker(L"Data\\MirrorsOfSkyrim_RenderDistance.enable"));
		logger::info("[MOS][Settings] geometry/shadow distance controls={} hand={} placed={}",
			MirrorRenderDistance::controls.load(), MirrorRenderDistance::Get(true), MirrorRenderDistance::Get(false));
		// Applied after every individual value is loaded, because with automatic
		// quality on the level owns them and must win over whatever the INI
		// happened to hold; with it off this is a no-op and the manual values
		// stand exactly as read.
		MirrorQualityPreset::Apply();
		logger::info("[MOS][Settings] automaticQuality={} qualityMode={} targetFPS={} level={} objectDistance(hand/placed)={}/{} shadowDistance(hand/placed)={}/{} minimumSunLight={:.2f}",
			MirrorQualityPreset::Automatic(),
			static_cast<unsigned>(MirrorQualityPreset::CurrentMode()),
			MirrorQualityPreset::targetFPS.load(),
			static_cast<unsigned>(MirrorQualityPreset::CurrentLevel()),
			MirrorRenderDistance::Get(true), MirrorRenderDistance::Get(false),
			MirrorRenderDistance::GetShadow(true), MirrorRenderDistance::GetShadow(false),
			MirrorShadowSettings::minimumSunLuminance.load());
		logger::info("[MOS][Settings] shadow controls={} hand={} placed={} playerShadow={} shadowMap={}x{} detailMap={}x{}",
			MirrorShadowSettings::controls.load(),values.shadows,placed.shadows,
			placed.playerShadow, MirrorPrivateShadow::Resolution(false), MirrorPrivateShadow::Resolution(false),
			MirrorPrivateShadow::Resolution(true), MirrorPrivateShadow::Resolution(true));
		logger::info("[MOS][Settings] placed mirrors dynamicResolution={} manualResolution={} refreshHz={} handRefreshHz={} handLoweredRefreshHz={}",
			placed.dynamicResolution, placed.resolution, placed.refreshHz, placed.handRefreshHz, placed.handLoweredRefreshHz);
		logger::info("[MOS][Settings] hand portrait zoom {:.3f}, mouse wheel zoom {} loaded (missing keys use defaults) from {}",
			PortraitZoom(), WheelZoomEnabled(), kIniRelativePath);
		if (!REL::Module::IsVR()) {
			auto* actions = SKSE::GetActionEventSource();
			auto* events = RE::ScriptEventSourceHolder::GetSingleton();
			if (actions && events && !g_drawEventsRegistered.exchange(true)) {
				actions->AddEventSink(&DrawZoomSink::GetSingleton());
				events->AddEventSink<RE::TESEquipEvent>(&DrawZoomSink::GetSingleton());
				logger::info("[MOS][Settings] fresh hand-mirror equip/draw starts at face zoom");
			}
		}
	}

	bool Save()
	{
		std::scoped_lock lock(g_fileMutex);
		const HandMirrorSettingsIni::Values values{ PortraitZoom(), WheelZoomEnabled(), ReflectWhenLowered(),
			RaisedResolution(), LoweredResolution(), MirrorShadowSettings::hand.load(), MirrorRenderDistance::Get(true),
			MirrorRenderDistance::GetShadow(true) };
		std::vector<std::string> lines;
		{
			std::ifstream file(IniPath());
			std::error_code readError;
			const bool exists = fs::exists(IniPath(), readError);
			if (readError || (!file.is_open() && exists)) return false;
			std::string line;
			while (file.is_open() && std::getline(file, line))
				lines.push_back(line);
			if (file.bad()) return false; // Never replace an INI that was only partly read.
		}
		const auto output = MirrorAcquisitionSettingsIni::Update(
			MirrorFleetSettingsIni::Update(HandMirrorSettingsIni::Update(lines, values),
			MirrorFleetSettingsIni::CurrentValues()),
			{ MirrorAcquisition::merchantsEnabled.load(), MirrorAcquisition::craftingEnabled.load(),
				MirrorAcquisition::homesEnabled.load(), MirrorAcquisition::innsEnabled.load() });
		if (!MirrorSettingsFile::Write(IniPath(), output)) {
			logger::warn("[MOS][Settings] atomic save failed; previous {} preserved", kIniRelativePath);
			return false;
		}
		logger::info(
			"[MOS][Settings] hand portrait zoom {:.4f}, mouse wheel zoom {} saved to {}",
			values.zoom, values.wheel, kIniRelativePath);
		return true;
	}

	void OnInputLoaded()
	{
		MirrorPerformance::OnInputLoaded();
		if (REL::Module::IsVR())
			return;
		if (auto* manager = RE::BSInputDeviceManager::GetSingleton(); manager &&
			!g_inputRegistered.exchange(true, std::memory_order_acq_rel)) {
			// StandingMirrorPlacement prepends its sink after this registration,
			// retaining first refusal of wheel input while placing a mirror.
			manager->PrependEventSink(&WheelInputSink::GetSingleton());
			logger::info("[MOS][Settings] optional raised-mirror mouse wheel input sink registered");
		}
	}

	bool RegisterCorePapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm)
			return false;
		constexpr auto kScript = "RealisticReflectionsMirrorsNative";
		a_vm->RegisterFunction("GetMirrorDistanceControls", kScript, GetMirrorDistanceControlsNative);
		a_vm->RegisterFunction("GetPlacedMirrorRenderDistance", kScript, GetPlacedMirrorRenderDistanceNative);
		a_vm->RegisterFunction("SetPlacedMirrorRenderDistance", kScript, SetPlacedMirrorRenderDistanceNative);
		a_vm->RegisterFunction("GetMirrorQualityLevel", kScript, GetMirrorQualityLevelNative);
		a_vm->RegisterFunction("SetMirrorQualityLevel", kScript, SetMirrorQualityLevelNative);
		a_vm->RegisterFunction("GetMirrorQualityMode", kScript, GetMirrorQualityModeNative);
		a_vm->RegisterFunction("SetMirrorQualityMode", kScript, SetMirrorQualityModeNative);
		a_vm->RegisterFunction("GetMirrorTargetFPS", kScript, GetMirrorTargetFPSNative);
		a_vm->RegisterFunction("SetMirrorTargetFPS", kScript, SetMirrorTargetFPSNative);
		a_vm->RegisterFunction("GetMirrorEffectiveResolution", kScript, GetMirrorEffectiveResolutionNative);
		a_vm->RegisterFunction("GetMirrorEffectiveRefresh", kScript, GetMirrorEffectiveRefreshNative);
		a_vm->RegisterFunction("GetMirrorEffectiveHeight", kScript, GetMirrorEffectiveHeightNative);
		a_vm->RegisterFunction("GetMirrorResolutionFollowsFramebuffer", kScript, GetMirrorResolutionFollowsFramebufferNative);
		a_vm->RegisterFunction("GetMirrorDebugHotkeys", kScript, GetMirrorDebugHotkeysNative);
		a_vm->RegisterFunction("SetMirrorDebugHotkeys", kScript, SetMirrorDebugHotkeysNative);
		a_vm->RegisterFunction("GetPlacedMirrorResolution", kScript, GetPlacedMirrorResolutionNative);
		a_vm->RegisterFunction("SetPlacedMirrorResolution", kScript, SetPlacedMirrorResolutionNative);
		a_vm->RegisterFunction("GetPlacedMirrorRefresh", kScript, GetPlacedMirrorRefreshNative);
		a_vm->RegisterFunction("SetPlacedMirrorRefresh", kScript, SetPlacedMirrorRefreshNative);
		a_vm->RegisterFunction("GetPlacedMirrorShadows", kScript, GetPlacedMirrorShadowsNative);
		a_vm->RegisterFunction("SetPlacedMirrorShadows", kScript, SetPlacedMirrorShadowsNative);
		a_vm->RegisterFunction("RefreshMirrorFurnishings", kScript, RefreshMirrorFurnishingsNative);
		a_vm->RegisterFunction("GetMirrorHomesEnabled", kScript, GetMirrorHomesEnabledNative);
		a_vm->RegisterFunction("SetMirrorHomesEnabled", kScript, SetMirrorHomesEnabledNative);
		a_vm->RegisterFunction("GetMirrorInnsEnabled", kScript, GetMirrorInnsEnabledNative);
		a_vm->RegisterFunction("SetMirrorInnsEnabled", kScript, SetMirrorInnsEnabledNative);
		return true;
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm)
			return false;
		if (!RegisterCorePapyrus(a_vm))
			return false;
		constexpr auto kScript = "MirrorsOfSkyrimNative";
		a_vm->RegisterFunction("GetMirrorDistanceControls", kScript, GetMirrorDistanceControlsNative);
		a_vm->RegisterFunction("GetHandMirrorRenderDistance", kScript, GetHandMirrorRenderDistanceNative);
		a_vm->RegisterFunction("SetHandMirrorRenderDistance", kScript, SetHandMirrorRenderDistanceNative);
		a_vm->RegisterFunction("GetPlacedMirrorRenderDistance", kScript, GetPlacedMirrorRenderDistanceNative);
		a_vm->RegisterFunction("SetPlacedMirrorRenderDistance", kScript, SetPlacedMirrorRenderDistanceNative);
		a_vm->RegisterFunction("GetMirrorShadowControls", kScript, GetMirrorShadowControlsNative);
		a_vm->RegisterFunction("GetMirrorAutomaticQuality", kScript, GetMirrorAutomaticQualityNative);
		a_vm->RegisterFunction("SetMirrorAutomaticQuality", kScript, SetMirrorAutomaticQualityNative);
		a_vm->RegisterFunction("GetMirrorQualityLevel", kScript, GetMirrorQualityLevelNative);
		a_vm->RegisterFunction("SetMirrorQualityLevel", kScript, SetMirrorQualityLevelNative);
		a_vm->RegisterFunction("GetMirrorQualityMode", kScript, GetMirrorQualityModeNative);
		a_vm->RegisterFunction("SetMirrorQualityMode", kScript, SetMirrorQualityModeNative);
		a_vm->RegisterFunction("GetMirrorTargetFPS", kScript, GetMirrorTargetFPSNative);
		a_vm->RegisterFunction("SetMirrorTargetFPS", kScript, SetMirrorTargetFPSNative);
		a_vm->RegisterFunction("GetMirrorEffectiveResolution", kScript, GetMirrorEffectiveResolutionNative);
		a_vm->RegisterFunction("GetMirrorEffectiveRefresh", kScript, GetMirrorEffectiveRefreshNative);
		a_vm->RegisterFunction("GetMirrorEffectiveHeight", kScript, GetMirrorEffectiveHeightNative);
		a_vm->RegisterFunction("GetMirrorFaceLight", kScript, GetMirrorFaceLightNative);
		a_vm->RegisterFunction("SetMirrorFaceLight", kScript, SetMirrorFaceLightNative);
		a_vm->RegisterFunction("GetMirrorResolutionFollowsFramebuffer", kScript, GetMirrorResolutionFollowsFramebufferNative);
		a_vm->RegisterFunction("GetMirrorDebugHotkeys", kScript, GetMirrorDebugHotkeysNative);
		a_vm->RegisterFunction("SetMirrorDebugHotkeys", kScript, SetMirrorDebugHotkeysNative);
		a_vm->RegisterFunction("StartMirrorBenchmark", kScript, StartMirrorBenchmarkNative);
		a_vm->RegisterFunction("GetHandMirrorShadowDistance", kScript, GetHandMirrorShadowDistanceNative);
		a_vm->RegisterFunction("SetHandMirrorShadowDistance", kScript, SetHandMirrorShadowDistanceNative);
		a_vm->RegisterFunction("GetPlacedMirrorShadowDistance", kScript, GetPlacedMirrorShadowDistanceNative);
		a_vm->RegisterFunction("SetPlacedMirrorShadowDistance", kScript, SetPlacedMirrorShadowDistanceNative);
		a_vm->RegisterFunction("GetMirrorShadowDistanceMaximum", kScript, GetMirrorShadowDistanceMaximumNative);
		a_vm->RegisterFunction("GetHandMirrorShadows", kScript, GetHandMirrorShadowsNative);
		a_vm->RegisterFunction("SetHandMirrorShadows", kScript, SetHandMirrorShadowsNative);
		a_vm->RegisterFunction("GetPlacedMirrorShadows", kScript, GetPlacedMirrorShadowsNative);
		a_vm->RegisterFunction("SetPlacedMirrorShadows", kScript, SetPlacedMirrorShadowsNative);
		a_vm->RegisterFunction("RefreshMirrorFurnishings", kScript, RefreshMirrorFurnishingsNative);
		a_vm->RegisterFunction("GetMirrorHomesEnabled", kScript, GetMirrorHomesEnabledNative);
		a_vm->RegisterFunction("SetMirrorHomesEnabled", kScript, SetMirrorHomesEnabledNative);
		a_vm->RegisterFunction("GetMirrorInnsEnabled", kScript, GetMirrorInnsEnabledNative);
		a_vm->RegisterFunction("SetMirrorInnsEnabled", kScript, SetMirrorInnsEnabledNative);
		a_vm->RegisterFunction("GetHandMirrorReflectWhenLowered", kScript, GetHandMirrorReflectWhenLoweredNative);
		a_vm->RegisterFunction("SetHandMirrorReflectWhenLowered", kScript, SetHandMirrorReflectWhenLoweredNative);
		a_vm->RegisterFunction("GetMirrorMerchantsEnabled", kScript, GetMirrorMerchantsEnabledNative);
		a_vm->RegisterFunction("SetMirrorMerchantsEnabled", kScript, SetMirrorMerchantsEnabledNative);
		a_vm->RegisterFunction("GetMirrorCraftingEnabled", kScript, GetMirrorCraftingEnabledNative);
		a_vm->RegisterFunction("SetMirrorCraftingEnabled", kScript, SetMirrorCraftingEnabledNative);
		a_vm->RegisterFunction("GetPlacedMirrorControls", kScript, GetPlacedMirrorControlsNative);
		a_vm->RegisterFunction("GetHandMirrorResolutionControls", kScript, GetHandMirrorResolutionControlsNative);
		a_vm->RegisterFunction("GetHandMirrorRaisedResolution", kScript, GetHandMirrorRaisedResolutionNative);
		a_vm->RegisterFunction("SetHandMirrorRaisedResolution", kScript, SetHandMirrorRaisedResolutionNative);
		a_vm->RegisterFunction("GetHandMirrorLoweredResolution", kScript, GetHandMirrorLoweredResolutionNative);
		a_vm->RegisterFunction("SetHandMirrorLoweredResolution", kScript, SetHandMirrorLoweredResolutionNative);
		a_vm->RegisterFunction("GetPlacedMirrorDynamicResolution", kScript, GetPlacedMirrorDynamicResolutionNative);
		a_vm->RegisterFunction("SetPlacedMirrorDynamicResolution", kScript, SetPlacedMirrorDynamicResolutionNative);
		a_vm->RegisterFunction("GetPlacedMirrorResolution", kScript, GetPlacedMirrorResolutionNative);
		a_vm->RegisterFunction("SetPlacedMirrorResolution", kScript, SetPlacedMirrorResolutionNative);
		a_vm->RegisterFunction("GetPlacedMirrorRefresh", kScript, GetPlacedMirrorRefreshNative);
		a_vm->RegisterFunction("SetPlacedMirrorRefresh", kScript, SetPlacedMirrorRefreshNative);
		a_vm->RegisterFunction("GetHandMirrorRefresh", kScript, GetHandMirrorRefreshNative);
		a_vm->RegisterFunction("SetHandMirrorRefresh", kScript, SetHandMirrorRefreshNative);
		a_vm->RegisterFunction("GetHandMirrorLoweredRefresh", kScript, GetHandMirrorLoweredRefreshNative);
		a_vm->RegisterFunction("SetHandMirrorLoweredRefresh", kScript, SetHandMirrorLoweredRefreshNative);
		a_vm->RegisterFunction("GetHandMirrorZoom", kScript, GetHandMirrorZoomNative);
		a_vm->RegisterFunction("SetHandMirrorZoom", kScript, SetHandMirrorZoomNative);
		a_vm->RegisterFunction(
			"GetHandMirrorZoomDefault", kScript, GetHandMirrorZoomDefaultNative);
		a_vm->RegisterFunction(
			"GetHandMirrorZoomMinimum", kScript, GetHandMirrorZoomMinimumNative);
		a_vm->RegisterFunction(
			"GetHandMirrorZoomMaximum", kScript, GetHandMirrorZoomMaximumNative);
		a_vm->RegisterFunction(
			"GetHandMirrorWheelZoom", kScript, GetHandMirrorWheelZoomNative);
		a_vm->RegisterFunction(
			"SetHandMirrorWheelZoom", kScript, SetHandMirrorWheelZoomNative);
		return true;
	}

}
