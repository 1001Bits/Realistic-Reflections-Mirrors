#include "PCH.h"

#include "HandMirrorVRGripOffset.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace HandMirrorVRGripOffset
{
	namespace
	{
		constexpr const char* kIniRelativePath =
			"Data\\SKSE\\Plugins\\RealisticReflectionsMirrors.ini";
		constexpr std::string_view kSection = "[VRHandGrip]";
		constexpr const char* kItemNodeName = "RRMirrorItem";
		constexpr float kPi = 3.14159265358979323846F;
		constexpr float kDegreesToRadians = kPi / 180.0F;
		// Frames to keep looking for the item node after an equip or a load; the
		// armor 3D attaches a little after the equip event.
		constexpr std::uint32_t kApplyAttemptBudget = 180;
		constexpr float kTranslateMatchTolerance = 0.05F;

		struct Offset
		{
			float x{ 0.0F };
			float y{ 0.0F };
			float z{ 0.0F };
			float rotateX{ 0.0F };
			float rotateY{ 0.0F };
			float rotateZ{ 0.0F };

			[[nodiscard]] bool Any() const noexcept
			{
				return x != 0.0F || y != 0.0F || z != 0.0F || rotateX != 0.0F ||
				       rotateY != 0.0F || rotateZ != 0.0F;
			}
		};

		// Mesh-authored RRMirrorItem local translation per perspective
		// (gilded_noble_round_filigree_v1, measured 2026-09-05).  A node is only
		// touched while it still carries the authored translation or the one this
		// module wrote, so a re-authored mesh is left alone (and logged once).
		constexpr std::array<RE::NiPoint3, 2> kAuthoredTranslate{
			RE::NiPoint3{ 15.0F, 0.0F, 0.5F },     // third person
			RE::NiPoint3{ 10.0F, 17.321F, 0.5F }   // first person
		};

		struct PerspectiveState
		{
			RE::NiPoint3 appliedTranslate{};
			bool appliedValid{ false };
			bool mismatchLogged{ false };
			std::uint64_t applications{ 0 };
		};

		Offset g_offset{};
		bool g_correctAuthoredGrip{ false };
		bool g_exactBipedGrip{ false };
		RE::TESObjectARMO* g_exactArmor{ nullptr };
		RE::TESObjectARMA* g_exactAddon{ nullptr };
		std::atomic_bool g_active{ false };
		std::atomic_bool g_sinkRegistered{ false };
		std::atomic<std::uint32_t> g_attemptsRemaining{ 0 };
		std::mutex g_lock{};
		std::array<PerspectiveState, 2> g_state{};

		[[nodiscard]] std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos)
				return {};
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		[[nodiscard]] bool ParseFloat(std::string_view a_text, float& a_out) noexcept
		{
			const auto text = Trim(a_text);
			if (text.empty())
				return false;
			char* end = nullptr;
			const float value = std::strtof(text.c_str(), &end);
			if (end == text.c_str() || !std::isfinite(value))
				return false;
			a_out = value;
			return true;
		}

		[[nodiscard]] bool Near(const RE::NiPoint3& a_left, const RE::NiPoint3& a_right) noexcept
		{
			return std::abs(a_left.x - a_right.x) <= kTranslateMatchTolerance &&
			       std::abs(a_left.y - a_right.y) <= kTranslateMatchTolerance &&
			       std::abs(a_left.z - a_right.z) <= kTranslateMatchTolerance;
		}

		void LoadIni()
		{
			Offset offset{};
			std::ifstream file(kIniRelativePath);
			bool inSection = false;
			std::string line;
			while (file && std::getline(file, line)) {
				const auto text = Trim(line);
				if (text.empty() || text[0] == ';' || text[0] == '#')
					continue;
				if (text[0] == '[') {
					inSection = text == kSection;
					continue;
				}
				if (!inSection)
					continue;
				const auto equals = text.find('=');
				if (equals == std::string::npos)
					continue;
				const auto key = Trim(std::string_view(text).substr(0, equals));
				const auto value = std::string_view(text).substr(equals + 1);
				float parsed = 0.0F;
				if (!ParseFloat(value, parsed))
					continue;
				if (key == "fOffsetX")
					offset.x = parsed;
				else if (key == "fOffsetY")
					offset.y = parsed;
				else if (key == "fOffsetZ")
					offset.z = parsed;
				else if (key == "fRotateX")
					offset.rotateX = parsed;
				else if (key == "fRotateY")
					offset.rotateY = parsed;
				else if (key == "fRotateZ")
					offset.rotateZ = parsed;
			}
			g_offset = offset;
		}

		/**
		 * Apply the offset to one perspective's item node.  Returns true when the
		 * node exists (found, applied or already applied), false when absent.
		 */
		[[nodiscard]] bool ApplyToPerspective(
			RE::PlayerCharacter& a_player, const bool a_firstPerson) noexcept
		{
			RE::NiPointer<RE::NiAVObject> retainedClone{};
			RE::NiAVObject* object = nullptr;
			if (g_exactBipedGrip) {
				// VRIK can put both perspective clones under Get3D1(false).
				// A name search there can return the hidden first-person item.
				// The current shield slot identifies the exact equipped clone.
				const auto biped = a_player.GetBiped(a_firstPerson);
				const auto* shield = biped ? biped->GetShieldObject() : nullptr;
				if (!g_exactArmor || !g_exactAddon || !shield ||
					shield->item != g_exactArmor || shield->addon != g_exactAddon || !shield->partClone)
					return false;
				retainedClone = shield->partClone;
				object = retainedClone->GetObjectByName(kItemNodeName);
				auto* pane = retainedClone->GetObjectByName("TrueMirror:0");
				auto* frame = retainedClone->GetObjectByName("MirrorFrame");
				if (!object || object->parent != retainedClone.get() ||
					!pane || !frame || pane->parent != object || frame->parent != object)
					return false;
			} else {
				auto* root = a_player.Get3D1(a_firstPerson);
				object = root ? root->GetObjectByName(kItemNodeName) : nullptr;
			}
			if (!object)
				return false;
			const std::size_t index = a_firstPerson ? 1 : 0;
			// VRIK displays the body clone. The authored anchor (-0.5,0,-20)
			// transforms to (-5,0,0) there, but already to (0,0,0) in the
			// hidden first-person clone. Only the body needs this correction.
			const float gripCorrectionX = g_correctAuthoredGrip && !a_firstPerson ? 5.0F : 0.0F;
			if (!g_offset.Any() && gripCorrectionX == 0.0F)
				return true;
			auto& state = g_state[index];
			const auto& current = object->local.translate;
			const bool authored = Near(current, kAuthoredTranslate[index]);
			const bool alreadyApplied = state.appliedValid &&
				Near(current, state.appliedTranslate);
			if (alreadyApplied)
				return true;
			if (!authored) {
				if (!state.mismatchLogged) {
					state.mismatchLogged = true;
					logger::warn(
						"[MOS][VRGrip] {} RRMirrorItem translation ({:.3f},{:.3f},{:.3f}) is neither the authored ({:.3f},{:.3f},{:.3f}) nor an applied value; leaving it alone",
						a_firstPerson ? "first-person" : "third-person", current.x,
						current.y, current.z, kAuthoredTranslate[index].x,
						kAuthoredTranslate[index].y, kAuthoredTranslate[index].z);
				}
				return true;
			}
			RE::NiMatrix3 rotation{};
			rotation.SetEulerAnglesXYZ(
				g_offset.rotateX * kDegreesToRadians,
				g_offset.rotateY * kDegreesToRadians,
				g_offset.rotateZ * kDegreesToRadians);
			object->local.translate = RE::NiPoint3{
				kAuthoredTranslate[index].x + gripCorrectionX + g_offset.x,
				kAuthoredTranslate[index].y + g_offset.y,
				kAuthoredTranslate[index].z + g_offset.z
			};
			object->local.rotate = object->local.rotate * rotation;
			state.appliedTranslate = object->local.translate;
			state.appliedValid = true;
			++state.applications;
			if (state.applications <= 3) {
				logger::info(
					"[MOS][VRGrip] applied offset ({:+.2f},{:+.2f},{:+.2f}) rotate ({:+.1f},{:+.1f},{:+.1f}) to the {} RRMirrorItem node",
					gripCorrectionX + g_offset.x, g_offset.y, g_offset.z, g_offset.rotateX,
					g_offset.rotateY, g_offset.rotateZ,
					a_firstPerson ? "first-person" : "third-person");
			}
			return true;
		}

		void ApplyTask();

		void ScheduleApply(const std::uint32_t a_attempts) noexcept
		{
			g_attemptsRemaining.store(a_attempts, std::memory_order_release);
			if (auto* tasks = SKSE::GetTaskInterface())
				tasks->AddTask(ApplyTask);
		}

		void ApplyTask()
		{
			if (!g_active.load(std::memory_order_acquire))
				return;
			auto* const player = RE::PlayerCharacter::GetSingleton();
			bool found = false;
			if (player) {
				std::scoped_lock lock{ g_lock };
				const bool third = ApplyToPerspective(*player, false);
				const bool first = ApplyToPerspective(*player, true);
				found = g_correctAuthoredGrip ? third : third || first;
			}
			const auto remaining = g_attemptsRemaining.load(std::memory_order_acquire);
			if (!found && remaining > 1) {
				g_attemptsRemaining.store(remaining - 1, std::memory_order_release);
				if (auto* tasks = SKSE::GetTaskInterface())
					tasks->AddTask(ApplyTask);
			}
		}

		class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			static EquipSink& GetSingleton() noexcept
			{
				static stl::no_destructor<EquipSink> sink{};
				return sink.get();
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESEquipEvent* a_event,
				RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (a_event && a_event->actor &&
					a_event->actor.get() == RE::PlayerCharacter::GetSingleton()) {
					ScheduleApply(kApplyAttemptBudget);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void OnDataLoaded() noexcept
	{
		if (!REL::Module::IsVR())
			return;
		g_correctAuthoredGrip =
			HandMirrorApprovedContentReadOnlyObserver::VRPresentationFixEnabled() &&
			GetModuleHandleW(L"VRIK.dll") != nullptr;
		g_exactBipedGrip = HandMirrorApprovedContentReadOnlyObserver::VRViewCohortFixEnabled();
		if (g_exactBipedGrip) {
			using namespace HandMirrorApprovedContentReadOnlyObserver;
			if (auto* const data = RE::TESDataHandler::GetSingleton()) {
				g_exactArmor = data->LookupForm<RE::TESObjectARMO>(kFiligreeV1ArmorLocalFormID, kFiligreeV1PluginBasename);
				g_exactAddon = data->LookupForm<RE::TESObjectARMA>(kFiligreeV1ArmorAddonLocalFormID, kFiligreeV1PluginBasename);
			}
			logger::info("[MOS][VRGrip] exact equipped biped clone lookup enabled; armor={} addon={}",
				g_exactArmor != nullptr, g_exactAddon != nullptr);
		}
		try {
			LoadIni();
		} catch (...) {
			g_offset = {};
		}
		if (!g_offset.Any() && !g_correctAuthoredGrip) {
			logger::info(
				"[MOS][VRGrip] no [VRHandGrip] offset in RealisticReflectionsMirrors.ini (keys fOffsetX/fOffsetY/fOffsetZ, fRotateX/fRotateY/fRotateZ); hand mirror grip left as authored");
			return;
		}
		g_active.store(true, std::memory_order_release);
		logger::info("[MOS][VRGrip] authored body-grip correction enabled={} offsetX={}",
			g_correctAuthoredGrip, g_correctAuthoredGrip ? 5.0F : 0.0F);
		if (!g_sinkRegistered.exchange(true, std::memory_order_acq_rel)) {
			if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton())
				holder->AddEventSink<RE::TESEquipEvent>(&EquipSink::GetSingleton());
		}
		logger::info(
			"[MOS][VRGrip] offset ({:+.2f},{:+.2f},{:+.2f}) rotate ({:+.1f},{:+.1f},{:+.1f}) armed for the equipped hand mirror",
			g_offset.x, g_offset.y, g_offset.z, g_offset.rotateX, g_offset.rotateY,
			g_offset.rotateZ);
	}

	void OnGameLoaded() noexcept
	{
		if (!g_active.load(std::memory_order_acquire))
			return;
		{
			std::scoped_lock lock{ g_lock };
			for (auto& state : g_state)
				state = {};
		}
		ScheduleApply(kApplyAttemptBudget);
	}
}
