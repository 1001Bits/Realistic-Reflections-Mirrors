#include "PCH.h"

#include "MirrorFixtureAnchors.h"
#include "MirrorFixtureAnchorData.h"
#include "MirrorContentProfile.h"
#include "MirrorVanillaClutterPolicy.h"

namespace MirrorFixtureAnchors
{
	namespace
	{
		using namespace MirrorFixtureAnchorPolicy;
		constexpr auto kCore = MirrorContentProfile::kCorePlugin;
		constexpr std::string_view kVanilla = "Skyrim.esm";
		// Interior overhauls the 2026-09-16 inspection examined; logged when loaded.
		// Exact file names from the downloaded archives (the first list spelled the
		// HS plugins "HS Riverwood - ..." and never matched).
		constexpr std::array<std::string_view, 28> kKnownOverhauls{
			"JK's The Bannered Mare.esp", "JK's Sleeping Giant Inn.esp", "JK's Bee and Barb.esp",
			"JK's Candlehearth Hall.esp", "JK's Silver-Blood Inn.esp", "JK's The Winking Skeever.esp",
			"Ryn's Sleeping Giant Inn.esp", "HSRiverwood - Sleeping Giant Inn.esp", "HSWhiterun - The Bannered Mare.esp",
			"HSRiften - The Bee and Barb.esp", "HSWindhelm - Candlehearth Hall.esp", "HSMarkarth - Silver-Blood Inn.esp",
			"HSSolitude - The Winking Skeever.esp", "HSPlayerHomes - Breezehome.esp", "HSPlayerHomes - Honeyside.esp",
			"HSPlayerHomes - Vlindrel Hall.esp", "HSPlayerHomes - Hjerim.esp", "HSPlayerHomes - Proudspire Manor.esp",
			"Eli_Breezehome.esp", "iWill_ARavensBreezeHome.esp", "Homes All in One.esp", "Distinct Interiors.esp",
			"Candlehearth.esp", "RealisticRoomRental.esp", "Snazzy Common Cupboards Dressers and End Tables.esp",
			"Snazzy Upper Cupboards Dressers and End Tables.esp", "Lux.esp", "EnhancedLightsandFX.esp" };

		// kCompatRules resolved once at DataLoaded.
		struct ResolvedRule
		{
			bool pluginLoaded{};
			RE::FormID base{};  // kEquivalentBase: runtime id of the equivalent base
		};
		std::array<ResolvedRule, kCompatRules.size()> g_rules{};

		[[nodiscard]] bool PluginLoaded(RE::TESDataHandler* data, std::string_view name)
		{
			return data->LookupLoadedModByName(name) || data->LookupLoadedLightModByName(name);
		}

		// What the loaded overhauls say about fixture `index` right now.
		void ApplyCompat(RE::TESDataHandler* data, std::size_t index, AnchorState& state, const char*& blocker)
		{
			for (std::size_t r = 0; r < kCompatRules.size(); ++r) {
				const auto& rule = kCompatRules[r];
				if (rule.fixture != index || !g_rules[r].pluginLoaded) continue;
				switch (rule.kind) {
				case CompatKind::kBlockerPlugin:
					state.blocked = true;
					blocker = rule.plugin.data();
					break;
				case CompatKind::kBlockerReference:
					if (auto* ref = data->LookupForm<RE::TESObjectREFR>(rule.local, rule.plugin);
						ref && !ref->IsDisabled() && !ref->IsDeleted()) {
						state.blocked = true;
						blocker = rule.plugin.data();
					}
					break;
				case CompatKind::kEquivalentBase:
					if (state.base && state.base == g_rules[r].base) state.baseEquivalent = true;
					break;
				}
			}
		}

		struct Applied
		{
			Decision decision{ Decision::kKeep };
			Vec3 position{};
			float yaw{};
			bool moved{};        // we changed the reference's pose at least once
			bool hidden{};       // we disabled the mirror/light
			bool logged{};
		};
		std::array<Applied, kFixtures.size()> g_applied{};
		bool g_overhaulsLogged{};
		std::atomic<std::uint8_t> g_clutterDoneMask{ 0 };

		bool Near(const Vec3& a, const RE::NiPoint3& b) noexcept
		{
			return std::fabs(a.x - b.x) < 0.1F && std::fabs(a.y - b.y) < 0.1F && std::fabs(a.z - b.z) < 0.1F;
		}

		void Place(RE::TESObjectREFR* ref, const Vec3& position, float yaw)
		{
			if (!ref) return;
			const auto current = ref->GetPosition();
			const float currentYaw = ref->GetAngle().z;
			if (Near(position, current) && std::fabs(WrapAngle(currentYaw - yaw)) < 0.001F) return;
			ref->SetPosition(position.x, position.y, position.z);
			ref->SetAngle({ 0.0F, 0.0F, yaw });
			// A static's 3D and collision are built at enable time; cycle it so the
			// new pose is what the renderer and Havok see.
			if (!ref->IsDisabled()) {
				ref->Disable();
				ref->Enable(false);
			}
		}

		void ApplyVanillaClutter(RE::TESDataHandler* data)
		{
			// Per-record until the live ref matches. A save changeform can restore
			// the old pose when an XESP parent is enabled after load (Hjerim
			// prid / bedroom purchase), so a single GameLoaded write is not enough.
			// After every record has matched while enabled (or stayed disabled for
			// a disable override), the mask is full and later furnishings ticks
			// do not look these refs up.
			constexpr auto kAll = static_cast<std::uint8_t>((1u << MirrorVanillaClutterPolicy::kRecords.size()) - 1);
			if (g_clutterDoneMask.load() == kAll)
				return;
			std::uint8_t done = g_clutterDoneMask.load();
			for (std::size_t i = 0; i < MirrorVanillaClutterPolicy::kRecords.size(); ++i) {
				const auto bit = static_cast<std::uint8_t>(1u << i);
				if (done & bit)
					continue;
				const auto& rec = MirrorVanillaClutterPolicy::kRecords[i];
				auto* ref = data->LookupForm<RE::TESObjectREFR>(rec.formId, kVanilla);
				if (!ref || ref->IsDeleted()) {
					done |= bit;
					continue;
				}
				if (rec.action == MirrorVanillaClutterPolicy::Action::kDisable) {
					if (!ref->IsDisabled()) {
						ref->Disable();
						logger::info("[MOS][Clutter] {:08X} disabled", rec.formId);
					}
					done |= bit;
					continue;
				}
				const auto current = ref->GetPosition();
				const bool atPose = Near(Vec3{ rec.x, rec.y, rec.z }, current) &&
					std::fabs(WrapAngle(ref->GetAngle().z - rec.yaw)) < 0.001F;
				if (atPose && !ref->IsDisabled()) {
					done |= bit;
					continue;
				}
				if (!atPose) {
					Place(ref, Vec3{ rec.x, rec.y, rec.z }, rec.yaw);
					logger::info("[MOS][Clutter] {:08X} relocated to ({:.1f},{:.1f},{:.1f}) enabled={}",
						rec.formId, rec.x, rec.y, rec.z, !ref->IsDisabled());
					const auto placed = ref->GetPosition();
					if (!ref->IsDisabled() && Near(Vec3{ rec.x, rec.y, rec.z }, placed) &&
						std::fabs(WrapAngle(ref->GetAngle().z - rec.yaw)) < 0.001F)
						done |= bit;
				}
			}
			g_clutterDoneMask.store(done);
		}

		void SetHidden(RE::TESObjectREFR* ref, bool hidden)
		{
			if (!ref) return;
			if (hidden && !ref->IsDisabled()) ref->Disable();
			else if (!hidden && ref->IsDisabled()) ref->Enable(false);
		}

		AnchorState ReadAnchor(RE::TESDataHandler* data, const Fixture& f)
		{
			AnchorState state{};
			auto* ref = f.anchorReference ? data->LookupForm<RE::TESObjectREFR>(f.anchorReference, kVanilla) : nullptr;
			if (!ref) return state;
			state.present = true;
			state.deleted = ref->IsDeleted();
			state.disabled = ref->IsDisabled();
			const auto position = ref->GetPosition();
			state.position = { position.x, position.y, position.z };
			state.yaw = ref->GetAngle().z;
			state.scale = static_cast<float>(ref->GetScale());
			if (auto* base = ref->GetBaseObject()) {
				state.base = base->GetFormID();
				state.top = static_cast<float>(base->boundData.boundMax.z);
			}
			return state;
		}
	}

	bool MarkerBypassed(const RE::TESObjectREFR* marker) noexcept
	{
		if (!marker) return false;
		const auto* parent = marker->extraList.GetByType<RE::ExtraEnableStateParent>();
		if (!parent) return false;
		const auto handle = parent->parent;
		const auto owner = handle.get();
		return owner && owner->GetFormID() == 0x14;  // PlayerRef: the marker can never toggle on its own
	}

	bool CellOwnedByPlayer(const RE::TESObjectREFR* reference) noexcept
	{
		if (!reference) return false;
		auto* cell = reference->GetParentCell();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!cell || !player) return false;
		auto* owner = cell->GetActorOwner();
		return owner && owner == player->GetActorBase();
	}

	void OnPreLoadGame()
	{
		g_clutterDoneMask.store(0);
	}

	void OnDataLoaded()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data || g_overhaulsLogged) return;
		g_overhaulsLogged = true;
		std::string loaded;
		for (const auto name : kKnownOverhauls)
			if (PluginLoaded(data, name)) loaded += (loaded.empty() ? "" : ", ") + std::string(name);
		logger::info("[MOS][Fixture] known interior overhauls loaded: {}", loaded.empty() ? "none" : loaded);
		std::string active;
		for (std::size_t r = 0; r < kCompatRules.size(); ++r) {
			const auto& rule = kCompatRules[r];
			auto& resolved = g_rules[r];
			resolved.pluginLoaded = PluginLoaded(data, rule.plugin);
			if (!resolved.pluginLoaded) continue;
			if (rule.kind == CompatKind::kEquivalentBase) {
				auto* form = data->LookupForm(rule.local, rule.plugin);
				resolved.base = form ? form->GetFormID() : 0;
			}
			if (!active.empty()) active += "; ";
			if (rule.kind == CompatKind::kBlockerPlugin)
				active += fmt::format("{} hidden by {}", kFixtures[rule.fixture].name, rule.plugin);
			else
				active += fmt::format("{} {} {} {:06X}", kFixtures[rule.fixture].name,
					rule.kind == CompatKind::kBlockerReference ? "hidden if present:" : "accepts base",
					rule.plugin, rule.local);
		}
		logger::info("[MOS][Fixture] compatibility rules active: {}", active.empty() ? "none" : active);
	}

	void Apply(RE::TESDataHandler* data)
	{
		if (!data) return;
		for (std::size_t i = 0; i < kFixtures.size(); ++i) {
			const auto& f = kFixtures[i];
			auto& applied = g_applied[i];
			auto* mirror = data->LookupForm<RE::TESObjectREFR>(f.mirrorLocal, kCore);
			auto* light = data->LookupForm<RE::TESObjectREFR>(f.lightLocal, kCore);
			auto* controller = data->LookupForm<RE::TESObjectREFR>(f.controllerLocal, kCore);
			if (!mirror) continue;
			// Inn/home mirrors are same-state XESP children of the controller
			// (Initially Disabled). Enable(parent) does not enable those children,
			// so a keep that only re-shows what we hid this session leaves the
			// STAT disabled — GOG 2026-09-18 Sleeping Giant 07000A10 status=4
			// after B05 came on.
			if (!controller)
				continue;
			if (controller->IsDisabled()) {
				SetHidden(mirror, true);
				SetHidden(light, true);
				applied.hidden = true;
				continue;
			}
			auto state = ReadAnchor(data, f);
			const char* blocker = nullptr;
			ApplyCompat(data, i, state, blocker);
			const auto placement = Evaluate(f, state);
			const bool changed = placement.decision != applied.decision || !applied.logged ||
				std::fabs(placement.position.x - applied.position.x) > 0.1F ||
				std::fabs(placement.position.y - applied.position.y) > 0.1F ||
				std::fabs(placement.position.z - applied.position.z) > 0.1F;
			switch (placement.decision) {
			case Decision::kHide:
				SetHidden(mirror, true);
				SetHidden(light, true);
				applied.hidden = true;
				break;
			case Decision::kFollow:
			case Decision::kKeep:
				SetHidden(mirror, false);
				SetHidden(light, false);
				applied.hidden = false;
				if (placement.decision == Decision::kFollow || applied.moved) {
					Place(mirror, placement.position, placement.yaw);
					if (light) {
						Place(light, LightPosition(f, placement), 0.0F);
					}
					applied.moved = placement.decision == Decision::kFollow;
				}
				break;
			}
			if (changed) {
				logger::info("[MOS][Fixture] {} anchor {:08X} present={} disabled={} base={:08X}{} pos=({:.1f},{:.1f},{:.1f}) yaw={:.3f} -> {} ({}{}{}) mirror=({:.1f},{:.1f},{:.1f}) yaw={:.3f}",
					f.name, f.anchorReference, state.present, state.disabled, state.base,
					state.baseEquivalent ? " (verified equivalent)" : "",
					state.position.x, state.position.y, state.position.z, state.yaw,
					placement.decision == Decision::kKeep ? "keep" : placement.decision == Decision::kFollow ? "follow" : "hide",
					placement.reason, blocker ? ": " : "", blocker ? blocker : "",
					placement.position.x, placement.position.y, placement.position.z, placement.yaw);
				applied.decision = placement.decision;
				applied.position = placement.position;
				applied.yaw = placement.yaw;
				applied.logged = true;
			}
		}
		ApplyVanillaClutter(data);
	}
}
