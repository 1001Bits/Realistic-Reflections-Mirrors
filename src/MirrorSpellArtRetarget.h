#pragma once

#include <cstddef>
#include <cstdint>

namespace RE
{
	class NiAVObject;
}

// Spell casting art in placed-mirror captures (owner, core run 4: "the flames
// still not show").
//
// A caster owns exactly one casting-art instance, and in first person the
// engine hangs it under the first-person magic node (ActorMagicCaster::
// GetMagicNode, SE 0x140541EF0). That subtree is culled only by the
// first-person camera, so a reflection -- which draws the hidden third-person
// body -- never sees the fire in the player's hand.
//
// For one capture the lease moves the first-person magic nodes (and the art
// under them) rigidly onto the third-person magic nodes of the same name, hands
// them to the capture's cull, and restores every saved world transform and
// bound before the capture returns. Draws read geometry->world at draw time,
// so the restore has to follow the capture's render, and the capture has to
// finish before RenderFirstPersonView -- which the RenderWorld seam guarantees.
// Render thread only.
namespace MirrorSpellArtRetarget
{
	inline constexpr std::size_t kMaximumRoots = 2;

	struct Diagnostics
	{
		std::uint64_t leases{ 0 };
		std::uint64_t roots{ 0 };
		std::uint64_t restores{ 0 };
		std::uint64_t overflowRefusals{ 0 };
		std::uint64_t faults{ 0 };
		std::uint64_t drawChecks{ 0 };
	};

	// First person only. Writes up to a_capacity leased magic-node roots to
	// a_roots and returns how many were leased; 0 means nothing to cull.
	[[nodiscard]] std::size_t Begin(RE::NiAVObject** a_roots, std::size_t a_capacity) noexcept;
	// Restores every saved transform. Idempotent; safe on every exit path.
	void End() noexcept;
	// True when a_object is one of the currently leased roots (on the thread
	// that owns the lease).
	[[nodiscard]] bool IsLeasedRoot(const RE::NiAVObject* a_object) noexcept;
	void RecordDrawCheck() noexcept;
	[[nodiscard]] Diagnostics Snapshot() noexcept;
}
