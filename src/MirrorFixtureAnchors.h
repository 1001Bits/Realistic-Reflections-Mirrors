#pragma once

#include <cstdint>

namespace RE
{
	class TESDataHandler;
	class TESObjectREFR;
}

// Runtime side of MirrorFixtureAnchorPolicy: read each fixture's live anchor,
// keep/follow/hide the mirror and its light accordingly, and log which known
// interior overhauls are loaded so a tester's log explains what happened.
namespace MirrorFixtureAnchors
{
	void OnDataLoaded();
	void OnPreLoadGame();
	// Called after the furnishing controllers were applied (game thread).
	void Apply(RE::TESDataHandler* data);
	// The vanilla bedroom markers are the enable signal for home mirrors. A mod
	// that parents such a marker to the player (Ravens Breezehome: "opposite
	// of PlayerRef", i.e. never enabled) has taken furnishing purchase out of
	// the game; fall back to house ownership so the mirror can still appear.
	[[nodiscard]] bool MarkerBypassed(const RE::TESObjectREFR* marker) noexcept;
	[[nodiscard]] bool CellOwnedByPlayer(const RE::TESObjectREFR* reference) noexcept;
}
