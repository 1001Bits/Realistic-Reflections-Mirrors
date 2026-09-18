#pragma once

#include <cstdint>

namespace MirrorInteriorEligibility
{
	/**
	 * Engine-independent snapshot of Skyrim's current cell ownership.
	 *
	 * `worldInteriorPresent` represents TES::interiorCell, while
	 * `playerCellPresent` represents PlayerCharacter::GetParentCell(). Interiors
	 * require the same attached cell; exteriors require an attached non-interior
	 * player cell with a non-null worldspace. Mirror worldspace identity is checked
	 * separately against the reacquired reference.
	 */
	struct Observation
	{
		bool worldAvailable{ false };
		bool playerAvailable{ false };
		bool worldInteriorPresent{ false };
		bool playerCellPresent{ false };
		bool worldInteriorFlag{ false };
		bool worldInteriorAttached{ false };
		bool playerCellInteriorFlag{ false };
		bool playerCellAttached{ false };
		bool sameCell{ false };
		bool playerWorldSpacePresent{ false };
	};

	enum class Status : std::uint8_t
	{
		kInterior,
		kExterior,
		kUnavailable
	};

	/** Value-only identity retained across the synchronous capture checkpoints. */
	struct CellIdentity
	{
		std::uintptr_t address{ 0 };
		std::uint32_t formID{ 0 };
	};

	struct WorldspaceIdentity
	{
		std::uintptr_t address{ 0 };
		std::uint32_t formID{ 0 };
	};

	struct LocationIdentity
	{
		CellIdentity playerCell{};
		WorldspaceIdentity worldspace{};
	};

	/** Classify a stable interior, a stable exterior, or an unsafe transition. */
	[[nodiscard]] constexpr Status Classify(const Observation& a_observation) noexcept
	{
		if (!a_observation.worldAvailable || !a_observation.playerAvailable ||
			!a_observation.playerCellPresent) {
			return Status::kUnavailable;
		}

		if (!a_observation.worldInteriorPresent) {
			if (a_observation.playerCellInteriorFlag ||
				!a_observation.playerCellAttached ||
				!a_observation.playerWorldSpacePresent) {
				return Status::kUnavailable;
			}
			return Status::kExterior;
		}

		if (!a_observation.worldInteriorFlag ||
			!a_observation.worldInteriorAttached ||
			!a_observation.playerCellInteriorFlag ||
			!a_observation.playerCellAttached ||
			!a_observation.sameCell) {
			return Status::kUnavailable;
		}

		return Status::kInterior;
	}

	/**
	 * Placeable-mirror capture accepts a stable attached interior or exterior.
	 * Transitional, detached, contradictory, and unavailable observations remain
	 * fail-closed. Planar water has its own eligibility path and is unaffected.
	 */
	[[nodiscard]] constexpr bool AllowsMirrorCapture(Status a_status) noexcept
	{
		return a_status == Status::kInterior || a_status == Status::kExterior;
	}

	/**
	 * Interiors have exactly one attached owning cell, so the mirror must live in
	 * it — that check is cheap and prevents capturing a mirror across a load
	 * door. Exteriors have no single owning cell for a view (the player and a
	 * nearby mirror legitimately sit in different cells of the same worldspace),
	 * so cell identity is not required there; recognition's loaded/3D-ready/range
	 * validation governs instead.
	 */
	[[nodiscard]] constexpr bool RequiresCellIdentity(Status a_status) noexcept
	{
		return a_status == Status::kInterior;
	}

	[[nodiscard]] constexpr bool RequiresWorldspaceIdentity(Status a_status) noexcept
	{
		return a_status == Status::kExterior;
	}

	[[nodiscard]] constexpr bool SameValidCell(
		const CellIdentity& a_left,
		const CellIdentity& a_right) noexcept
	{
		return a_left.address != 0 && a_left.formID != 0 &&
		       a_left.address == a_right.address && a_left.formID == a_right.formID;
	}

	/** Exact value comparison used to version candidate parent-cell changes. */
	[[nodiscard]] constexpr bool SameCellValue(
		const CellIdentity& a_left,
		const CellIdentity& a_right) noexcept
	{
		return a_left.address == a_right.address && a_left.formID == a_right.formID;
	}

	[[nodiscard]] constexpr bool SameValidWorldspace(
		const WorldspaceIdentity& a_left,
		const WorldspaceIdentity& a_right) noexcept
	{
		return a_left.address != 0 && a_left.formID != 0 &&
		       a_left.address == a_right.address && a_left.formID == a_right.formID;
	}

	[[nodiscard]] constexpr bool RequiresCandidateGenerationAdvance(
		const CellIdentity& a_previous,
		const CellIdentity& a_current) noexcept
	{
		return !SameCellValue(a_previous, a_current);
	}

	/**
	 * A later checkpoint may proceed only if Skyrim still reports the same kind
	 * of place: the exact retained cell indoors or the exact retained worldspace
	 * outdoors. Exterior player-cell changes within that worldspace are valid.
	 */
	[[nodiscard]] constexpr bool RevalidationAccepts(
		Status a_retainedStatus,
		const LocationIdentity& a_retainedLocation,
		Status a_currentStatus,
		const LocationIdentity& a_currentLocation) noexcept
	{
		if (!AllowsMirrorCapture(a_currentStatus) ||
			a_retainedStatus != a_currentStatus) {
			return false;
		}
		if (RequiresCellIdentity(a_currentStatus)) {
			return SameValidCell(
				a_retainedLocation.playerCell, a_currentLocation.playerCell);
		}
		return RequiresWorldspaceIdentity(a_currentStatus) &&
		       SameValidWorldspace(
			       a_retainedLocation.worldspace, a_currentLocation.worldspace);
	}
}
