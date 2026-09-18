#include "PCH.h"

#include "MirrorAcquisition.h"
#include "MirrorMerchantStock.h"
#include "MirrorFurnishingCatalog.h"
#include "MirrorContentProfile.h"
#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "MirrorFixtureAnchors.h"

namespace MirrorAcquisition
{
	namespace
	{
		constexpr auto kPlugin = MirrorContentProfile::kAddonPlugin;
		RE::TESGlobal* g_crafting = nullptr;
		std::array<std::array<RE::ContainerObject*, 2>, 8> g_owned{};
		std::atomic_bool g_applyQueued{ false };
		std::atomic_bool g_furnishingsQueued{ false };
		std::atomic_bool g_worldReady{ false };

		void ApplyFurnishings()
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data || !g_worldReady.load()) return;
			for (const auto& entry : MirrorFurnishingCatalog::kFixtures) {
				const bool portable = entry.controller >= 0xB0B;
				if (portable && !MirrorContentProfile::AddonLoaded()) continue;
				auto* controller = data->LookupForm<RE::TESObjectREFR>(entry.controller,
					portable ? kPlugin : MirrorContentProfile::kCorePlugin);
				if (!controller) continue;
				bool enabled = entry.home ? homesEnabled.load() : innsEnabled.load();
				if (entry.vanillaParent) {
					auto* parent = data->LookupForm<RE::TESObjectREFR>(entry.vanillaParent, "Skyrim.esm");
					// A home overhaul that parents the bedroom marker to the player
					// (Ravens Breezehome) has removed furnishing purchase from the
					// game; the owned house is then the only signal left.
					if (MirrorFixtureAnchors::MarkerBypassed(parent))
						enabled = enabled && MirrorFixtureAnchors::CellOwnedByPlayer(controller);
					else
						enabled = MirrorFurnishingCatalog::Enabled(enabled, true, parent != nullptr, parent && parent->IsDisabled());
				}
				if (controller->IsDisabled() == enabled) {
					if (enabled) controller->Enable(false);
					else controller->Disable();
					logger::info("[MOS][Acquisition] furnishing controller {:03X} enabled={}", entry.controller, enabled);
				}
			}
			// The mirrors themselves: keep, follow or hide against the live anchor.
			MirrorFixtureAnchors::Apply(data);
		}
		struct Merchant { std::uint32_t container; std::uint32_t kit; };
		// Exact Skyrim.esm bases from hand-mirror-merchant-audit-v1.json.
		constexpr std::array kMerchants{
			Merchant{ 0x09CAF8, 0x806 }, Merchant{ 0x078C0C, 0x806 },
			Merchant{ 0x0A6BFC, 0x805 }, Merchant{ 0x09E0D9, 0x809 },
			Merchant{ 0x0A29AD, 0x806 }, Merchant{ 0x0A6C06, 0x808 },
			Merchant{ 0x0A3F10, 0x807 }, Merchant{ 0x09DA63, 0x807 }
		};
		struct GameAllocator
		{
			std::array<RE::TESBoundObject*, 2> wanted{};
			bool IsUnmodified(RE::ContainerObject* entry) const noexcept
			{
				return entry->count == 1 && !entry->itemExtra &&
					(entry->obj == wanted[0] || entry->obj == wanted[1]);
			}
			RE::ContainerObject** AllocateList(std::uint32_t size) noexcept
			{
				return RE::calloc<RE::ContainerObject*>(size);
			}
			RE::ContainerObject* MakeEntry(RE::TESBoundObject* object) noexcept
			{
				auto* memory = RE::malloc<RE::ContainerObject>();
				return memory ? std::construct_at(memory, object, 1) : nullptr;
			}
			void FreeEntry(RE::ContainerObject* entry) noexcept
			{
				std::destroy_at(entry);
				RE::free(entry);
			}
			void FreeList(RE::ContainerObject** list) noexcept { RE::free(list); }
		};
	}

	void ApplySettings()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) return;
		ApplyFurnishings();
		if (!g_crafting || !MirrorContentProfile::AddonLoaded()) return;
		g_crafting->value = craftingEnabled.load() ? 1.0F : 0.0F;
		using namespace HandMirrorApprovedContentReadOnlyObserver;
		auto* hand = data->LookupForm<RE::TESObjectARMO>(
			kFiligreeV1ArmorLocalFormID, kFiligreeV1PluginBasename);
		if (!hand) {
			logger::warn("[MOS][Acquisition] Hand item missing; merchant additions skipped");
			return;
		}
		GameAllocator allocator;
		for (std::size_t index = 0; index < kMerchants.size(); ++index) {
			const auto& target = kMerchants[index];
			auto* container = data->LookupForm<RE::TESObjectCONT>(target.container, "Skyrim.esm");
			auto* kit = data->LookupForm<RE::TESObjectMISC>(target.kit, kPlugin);
			if (!container || !kit) {
				logger::warn("[MOS][Acquisition] Missing forms for merchant {:06X}", target.container);
				continue;
			}
			allocator.wanted = { hand, kit };
			const auto result = merchantsEnabled.load() ? MirrorMerchantStock::EnsurePair(
				container->containerObjects, container->numContainerObjects,
				allocator.wanted, allocator, &g_owned[index]) : MirrorMerchantStock::RemoveOwned(
					container->containerObjects, container->numContainerObjects, g_owned[index], allocator);
			logger::info("[MOS][Acquisition] Merchant {:06X}, kit {:03X}, stock result={} (0=existing, 1=added, 2=invalid, 3=allocation failed, 4=removed)",
				target.container, target.kit, static_cast<unsigned>(result));
		}
		logger::info("[MOS][Acquisition] merchants={} crafting={}", merchantsEnabled.load(), craftingEnabled.load());
	}

	void OnDataLoaded()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		g_crafting = data ? data->LookupForm<RE::TESGlobal>(0x826, kPlugin) : nullptr;
		// Persistent REFRs and their save state are applied at New/PostLoadGame.
		ApplySettings();
	}
	void OnPreLoadGame()
	{
		g_worldReady.store(false);
		MirrorFixtureAnchors::OnPreLoadGame();
	}
	void OnGameLoaded() { g_worldReady.store(true); ApplySettings(); }

	void QueueApply()
	{
		if (g_applyQueued.exchange(true)) return;
		if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([] {
			g_applyQueued.store(false);
			ApplySettings();
		});
		else g_applyQueued.store(false);
	}

	void QueueFurnishings()
	{
		if (g_furnishingsQueued.exchange(true)) return;
		if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([] {
			g_furnishingsQueued.store(false);
			ApplyFurnishings();
		});
		else g_furnishingsQueued.store(false);
	}
}
