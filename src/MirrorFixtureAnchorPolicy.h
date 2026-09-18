#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string_view>

// Owner, 2026-09-16: "make sure all our mirrors are compatible or work with
// these" (the popular modlists' interior overhauls). The 2026-09-16 inspection
// of 29 mods against our 11 cells found three situations: the vanilla dresser
// or table a mirror sits on is untouched (JK's, Ryn's, Lux, ELFX, SPHI, Snazzy
// BOS: same REFR, a new base with the same top within a unit), it is nudged a
// few units (Distinct Interiors' Honeyside end table +7), or it is re-used far
// away in a re-authored room with a different base (HS Player Homes/inns,
// RRR Enhanced, Eli's and Ravens Breezehome). A mirror authored against the
// vanilla pose must therefore be judged against the live anchor at runtime:
// keep it, follow the anchor, or hide it -- never leave it floating over a
// dresser that is no longer there. Pure, engine-free, so it can be tested.
namespace MirrorFixtureAnchorPolicy
{
	enum class Mounting : std::uint8_t
	{
		kTable = 0,  // stands on the anchor's top surface
		kWall = 1,   // hangs on the wall behind/above the anchor
		kFloor = 2   // stands on the floor; the anchor is the floor piece
	};

	struct Vec3
	{
		float x{}, y{}, z{};
	};

	struct Fixture
	{
		std::string_view name;
		std::uint32_t mirrorLocal;      // RealisticReflectionsMirrors.esm REFR
		std::uint32_t lightLocal;       // its local face light
		std::uint32_t controllerLocal;  // enable-parent controller
		std::uint32_t anchorReference;  // Skyrim.esm REFR (0 = free-standing)
		std::uint32_t anchorBase;       // Skyrim.esm base object of that REFR
		Mounting mounting;
		Vec3 anchorPosition;
		float anchorYaw;
		float anchorTop;    // base bounds z max, in the anchor's own units
		float anchorScale;  // vanilla reference scale
		Vec3 mirrorPosition;
		float mirrorYaw;
		float mirrorScale;
		Vec3 lightPosition;  // authored point, never derived from an already moved light
	};

	struct AnchorState
	{
		bool present{};
		bool disabled{};
		bool deleted{};
		Vec3 position{};
		float yaw{};
		std::uint32_t base{};
		float top{};        // live base bounds z max
		float scale{ 1.0F };
		// A different base that was verified to be the same surface behind the
		// frame (packaging/mirror-fixture-compat.json, e.g. JK's end wall).
		bool baseEquivalent{};
		// A known overhaul puts its own geometry where the mirror hangs.
		bool blocked{};
	};

	// Out-of-the-box compatibility data (2026-09-17 modlist matrix): no ESP
	// patches, just what the runtime may conclude when these files are loaded.
	enum class CompatKind : std::uint8_t
	{
		kEquivalentBase = 0,   // plugin + local base: same wall as the vanilla anchor
		kBlockerPlugin = 1,    // plugin loaded: the room is rebuilt, hide the mirror
		kBlockerReference = 2  // plugin + local REFR present and enabled: hide
	};

	struct CompatRule
	{
		std::uint8_t fixture;   // index into kFixtures
		CompatKind kind;
		std::string_view plugin;
		std::uint32_t local;    // 0 for kBlockerPlugin
	};

	enum class Decision : std::uint8_t
	{
		kKeep,    // anchor as authored: leave the mirror where the master put it
		kFollow,  // anchor moved/re-based within reach: move the mirror with it
		kHide     // anchor gone, disabled, or out of reach: disable the mirror
	};

	struct Placement
	{
		Decision decision;
		Vec3 position;
		float yaw;
		const char* reason;
	};

	// Reach limits in world units. A table mirror follows its dresser inside
	// a room-sized radius. A wall mirror only follows small nudges of its wall
	// piece: larger moves invalidate the surrounding clearance evidence.
	// A floor mirror follows its floor piece wherever the room went.
	inline constexpr float kTableFollowLimit = 600.0F;
	inline constexpr float kWallFollowLimit = 8.0F;
	inline constexpr float kFloorFollowLimit = 4096.0F;
	inline constexpr float kSameSpotLimit = 0.5F;
	inline constexpr float kYawTolerance = 0.02F;
	[[nodiscard]] inline bool Finite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
	}

	[[nodiscard]] inline float WrapAngle(float a) noexcept
	{
		constexpr float kTwoPi = 6.28318530718F;
		return Finite(a) ? std::remainder(a, kTwoPi) : 0.0F;
	}

	[[nodiscard]] inline Vec3 RotateZ(Vec3 v, float yaw) noexcept
	{
		const float c = std::cos(yaw), s = std::sin(yaw);
		return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
	}

	// Derive each placement from the authored pair, including a return home.
	// Using the light's last live position caused cumulative drift after nudges.
	[[nodiscard]] inline Vec3 LightPosition(const Fixture& f, const Placement& p) noexcept
	{
		const Vec3 offset{ f.lightPosition.x - f.mirrorPosition.x,
			f.lightPosition.y - f.mirrorPosition.y, f.lightPosition.z - f.mirrorPosition.z };
		// Skyrim's positive heading rotates clockwise in world XY.
		const auto moved = RotateZ(offset, -WrapAngle(p.yaw - f.mirrorYaw));
		return { p.position.x + moved.x, p.position.y + moved.y, p.position.z + moved.z };
	}

	[[nodiscard]] inline Placement Evaluate(const Fixture& f, const AnchorState& a) noexcept
	{
		const Placement keep{ Decision::kKeep, f.mirrorPosition, f.mirrorYaw, "authored pose" };
		if (a.blocked)
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "a loaded overhaul rebuilds this spot" };
		if (f.anchorReference == 0)
			return keep;
		if (!a.present || a.deleted)
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "anchor reference missing or deleted" };
		if (a.disabled)
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "anchor reference disabled" };
		if (!Finite(a.position.x) || !Finite(a.position.y) || !Finite(a.position.z) ||
			!Finite(a.yaw) || !Finite(a.scale) || !(a.scale > 0.0F))
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "anchor transform invalid" };
		if (f.mounting == Mounting::kWall && std::fabs(a.scale - f.anchorScale) > 0.001F)
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "wall anchor rescaled" };
		const Vec3 delta{ a.position.x - f.anchorPosition.x, a.position.y - f.anchorPosition.y, a.position.z - f.anchorPosition.z };
		const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
		const float yawDelta = WrapAngle(a.yaw - f.anchorYaw);
		const bool sameBase = a.base == f.anchorBase || a.baseEquivalent;
		if (distance < kSameSpotLimit && std::fabs(yawDelta) < kYawTolerance && sameBase)
			return keep;
		const float limit = f.mounting == Mounting::kWall ? kWallFollowLimit :
		                    f.mounting == Mounting::kFloor ? kFloorFollowLimit : kTableFollowLimit;
		if (distance > limit)
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "anchor moved beyond reach" };
		if (f.mounting == Mounting::kWall && (std::fabs(yawDelta) >= kYawTolerance || !sameBase))
			return { Decision::kHide, f.mirrorPosition, f.mirrorYaw, "wall anchor turned or replaced" };
		// Carry the authored offset in the anchor's frame to the live anchor.
		const Vec3 authoredOffset{ f.mirrorPosition.x - f.anchorPosition.x, f.mirrorPosition.y - f.anchorPosition.y,
			f.mirrorPosition.z - f.anchorPosition.z };
		// Engine heading increases clockwise in world XY.
		const Vec3 world = RotateZ(authoredOffset, -yawDelta);
		Vec3 position{ a.position.x + world.x, a.position.y + world.y, a.position.z + world.z };
		if (f.mounting == Mounting::kTable && (!sameBase || std::fabs(a.scale - f.anchorScale) > 0.001F)) {
			// A swapped or rescaled dresser has a different top: keep the authored
			// lift above the surface, measured from the live bounds.
			const float authoredLift = f.mirrorPosition.z - (f.anchorPosition.z + f.anchorTop * f.anchorScale);
			position.z = a.position.z + a.top * a.scale + authoredLift;
		}
		return { Decision::kFollow, position, WrapAngle(f.mirrorYaw + yawDelta),
			sameBase ? "anchor moved; mirror follows" : "anchor re-based; mirror follows its new top" };
	}
}
