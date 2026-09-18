#include "PCH.h"

#include "MirrorSpellArtRetarget.h"

namespace MirrorSpellArtRetarget
{
	namespace
	{
		// Flames hand art is a few dozen objects; the bound is generous so a
		// heavier modded art still fits, and a subtree that does not fit is
		// refused whole rather than half moved.
		constexpr std::size_t kMaximumSaved = 512;
		constexpr std::size_t kTraversalStack = 256;

		struct SavedObject
		{
			RE::NiAVObject* object{ nullptr };
			RE::NiTransform world{};
			RE::NiBound bound{};
		};

		struct Span
		{
			std::size_t first{ 0 };
			std::size_t last{ 0 };
			RE::NiTransform delta{};
		};

		struct State
		{
			std::array<SavedObject, kMaximumSaved> saved{};
			std::size_t savedCount{ 0 };
			std::array<RE::NiAVObject*, kMaximumRoots> roots{};
			std::size_t rootCount{ 0 };
		};

		State g_state;
		std::atomic<bool> g_active{ false };
		std::atomic<DWORD> g_owner{ 0 };

		std::atomic<std::uint64_t> g_leases{ 0 };
		std::atomic<std::uint64_t> g_roots{ 0 };
		std::atomic<std::uint64_t> g_restores{ 0 };
		std::atomic<std::uint64_t> g_overflowRefusals{ 0 };
		std::atomic<std::uint64_t> g_faults{ 0 };
		std::atomic<std::uint64_t> g_drawChecks{ 0 };

		struct Names
		{
			RE::BSFixedString left{ "NPC L MagicNode [LMag]" };
			RE::BSFixedString right{ "NPC R MagicNode [RMag]" };
		};

		// Created once and never destroyed: the engine's string pool is gone
		// by the time static destructors would run.
		[[nodiscard]] const Names* GetNames() noexcept
		{
			try {
				static const Names* names = new Names{};
				return names;
			} catch (...) {
				return nullptr;
			}
		}

		[[nodiscard]] bool ReadRootsSEH(RE::PlayerCharacter* a_player,
			RE::NiAVObject*& a_firstPerson, RE::NiAVObject*& a_thirdPerson) noexcept
		{
			__try {
				if (a_player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode)
					return false;
				a_firstPerson = a_player->Get3D1(true);
				a_thirdPerson = a_player->Get3D1(false);
				return a_firstPerson && a_thirdPerson && a_firstPerson != a_thirdPerson;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] RE::NiAVObject* FindSEH(RE::NiAVObject* a_root, const RE::BSFixedString& a_name) noexcept
		{
			__try {
				return a_root->GetObjectByName(a_name);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		// The magic node carries art only while a spell is readied; an empty
		// node is not worth a cull.
		[[nodiscard]] bool HasChildSEH(RE::NiAVObject* a_object) noexcept
		{
			__try {
				auto* node = a_object->AsNode();
				if (!node)
					return false;
				auto& children = node->GetChildren();
				for (std::uint16_t i = 0; i < children.capacity(); ++i) {
					if (children[i].get())
						return true;
				}
				return false;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool DeltaSEH(RE::NiAVObject* a_source, RE::NiAVObject* a_destination,
			RE::NiTransform& a_delta) noexcept
		{
			__try {
				const auto& source = a_source->world;
				if (!(source.scale > 1.0e-4f) || !(a_destination->world.scale > 1.0e-4f))
					return false;
				a_delta = a_destination->world * source.Invert();
				return std::isfinite(a_delta.translate.x) && std::isfinite(a_delta.translate.y) &&
					std::isfinite(a_delta.translate.z) && std::isfinite(a_delta.scale);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		enum class SaveResult : std::uint8_t
		{
			kSaved,
			kOverflow,
			kFault
		};

		[[nodiscard]] SaveResult SaveSubtreeSEH(RE::NiAVObject* a_root) noexcept
		{
			__try {
				RE::NiAVObject* stack[kTraversalStack]{};
				std::size_t top = 0;
				stack[top++] = a_root;
				while (top != 0) {
					auto* object = stack[--top];
					if (g_state.savedCount >= kMaximumSaved)
						return SaveResult::kOverflow;
					auto& slot = g_state.saved[g_state.savedCount++];
					slot.object = object;
					slot.world = object->world;
					slot.bound = object->worldBound;
					auto* node = object->AsNode();
					if (!node)
						continue;
					auto& children = node->GetChildren();
					for (std::uint16_t i = 0; i < children.capacity(); ++i) {
						auto* child = children[i].get();
						if (!child)
							continue;
						if (top >= kTraversalStack)
							return SaveResult::kOverflow;
						stack[top++] = child;
					}
				}
				return SaveResult::kSaved;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return SaveResult::kFault;
			}
		}

		[[nodiscard]] bool ApplySEH(const Span& a_span) noexcept
		{
			__try {
				for (auto i = a_span.first; i < a_span.last; ++i) {
					const auto& saved = g_state.saved[i];
					saved.object->world = a_span.delta * saved.world;
					saved.object->worldBound.center = a_span.delta * saved.bound.center;
					saved.object->worldBound.radius = saved.bound.radius * a_span.delta.scale;
				}
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool RestoreSEH() noexcept
		{
			__try {
				for (std::size_t i = 0; i < g_state.savedCount; ++i) {
					const auto& saved = g_state.saved[i];
					saved.object->world = saved.world;
					saved.object->worldBound = saved.bound;
				}
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
	}

	std::size_t Begin(RE::NiAVObject** a_roots, std::size_t a_capacity) noexcept
	{
		End();
		if (!a_roots || a_capacity == 0)
			return 0;
		const auto* names = GetNames();
		auto* player = RE::PlayerCharacter::GetSingleton();
		RE::NiAVObject* firstPerson = nullptr;
		RE::NiAVObject* thirdPerson = nullptr;
		if (!names || !player || !ReadRootsSEH(player, firstPerson, thirdPerson))
			return 0;

		std::array<Span, kMaximumRoots> spans{};
		std::size_t count = 0;
		for (const auto* name : { &names->left, &names->right }) {
			if (count >= kMaximumRoots || count >= a_capacity)
				break;
			auto* source = FindSEH(firstPerson, *name);
			auto* destination = FindSEH(thirdPerson, *name);
			if (!source || !destination || source == destination || !HasChildSEH(source))
				continue;
			Span span{};
			if (!DeltaSEH(source, destination, span.delta))
				continue;
			span.first = g_state.savedCount;
			switch (SaveSubtreeSEH(source)) {
			case SaveResult::kSaved:
				break;
			case SaveResult::kOverflow:
				g_overflowRefusals.fetch_add(1, std::memory_order_relaxed);
				g_state.savedCount = 0;
				return 0;
			case SaveResult::kFault:
			default:
				g_faults.fetch_add(1, std::memory_order_relaxed);
				g_state.savedCount = 0;
				return 0;
			}
			span.last = g_state.savedCount;
			spans[count] = span;
			g_state.roots[count] = source;
			++count;
		}
		if (count == 0) {
			g_state.savedCount = 0;
			return 0;
		}

		// Everything is saved before anything moves, so End() restores a
		// partial application exactly.
		g_state.rootCount = count;
		g_owner.store(GetCurrentThreadId(), std::memory_order_relaxed);
		g_active.store(true, std::memory_order_release);
		for (std::size_t i = 0; i < count; ++i) {
			if (!ApplySEH(spans[i])) {
				g_faults.fetch_add(1, std::memory_order_relaxed);
				End();
				return 0;
			}
		}
		for (std::size_t i = 0; i < count; ++i)
			a_roots[i] = g_state.roots[i];
		g_leases.fetch_add(1, std::memory_order_relaxed);
		g_roots.fetch_add(count, std::memory_order_relaxed);
		return count;
	}

	void End() noexcept
	{
		if (g_state.savedCount == 0 && !g_active.load(std::memory_order_acquire))
			return;
		if (g_active.load(std::memory_order_acquire) &&
			g_owner.load(std::memory_order_relaxed) != GetCurrentThreadId()) {
			// Only the capturing thread may restore; another thread reaching
			// here is a caller error, and moving objects under it is worse.
			g_faults.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!RestoreSEH())
			g_faults.fetch_add(1, std::memory_order_relaxed);
		g_active.store(false, std::memory_order_release);
		g_state.savedCount = 0;
		g_state.rootCount = 0;
		g_state.roots = {};
		g_restores.fetch_add(1, std::memory_order_relaxed);
	}

	bool IsLeasedRoot(const RE::NiAVObject* a_object) noexcept
	{
		if (!a_object || !g_active.load(std::memory_order_acquire) ||
			g_owner.load(std::memory_order_relaxed) != GetCurrentThreadId())
			return false;
		for (std::size_t i = 0; i < g_state.rootCount; ++i) {
			if (g_state.roots[i] == a_object)
				return true;
		}
		return false;
	}

	void RecordDrawCheck() noexcept
	{
		g_drawChecks.fetch_add(1, std::memory_order_relaxed);
	}

	Diagnostics Snapshot() noexcept
	{
		return {
			.leases = g_leases.load(std::memory_order_relaxed),
			.roots = g_roots.load(std::memory_order_relaxed),
			.restores = g_restores.load(std::memory_order_relaxed),
			.overflowRefusals = g_overflowRefusals.load(std::memory_order_relaxed),
			.faults = g_faults.load(std::memory_order_relaxed),
			.drawChecks = g_drawChecks.load(std::memory_order_relaxed)
		};
	}
}
