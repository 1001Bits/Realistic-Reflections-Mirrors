#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace StandingMirrorPlacementPolicy
{
	/** Exterior placement may cross cells in one worldspace; interiors may not. */
	[[nodiscard]] constexpr bool SamePlacementLocation(
		std::uintptr_t originCell, std::uintptr_t originWorld,
		std::uintptr_t currentCell, std::uintptr_t currentWorld) noexcept
	{
		return originCell != 0 && currentCell != 0 && originWorld == currentWorld &&
			(originWorld != 0 || originCell == currentCell);
	}

	inline constexpr std::array<std::uint32_t, 5> kStaticLocalFormIDs{
		0x800, 0x801, 0x802, 0x803, 0x804
	};
	inline constexpr std::array<std::uint32_t, 5> kInventoryLocalFormIDs{
		0x805, 0x806, 0x807, 0x808, 0x809
	};
	inline constexpr std::array<std::uint32_t, 5> kPreviewStaticLocalFormIDs{
		0x80A, 0x80B, 0x80C, 0x80D, 0x80E
	};
	// These bases are never exposed as inventory or house-authoring records.
	// A dynamic reference using one of them is therefore an exact, save-stable
	// placement-controller product and may safely be converted back into a kit.
	inline constexpr std::array<std::uint32_t, 5> kPlacedStaticLocalFormIDs{
		0x80F, 0x810, 0x811, 0x812, 0x813
	};
	inline constexpr float kPreviewDistance = 240.0F;
	inline constexpr float kMaximumAimDistance = 1024.0F;
	inline constexpr float kPi = 3.14159265358979323846F;
	inline constexpr float kTwoPi = 2.0F * kPi;
	inline constexpr float kRotationStepRadians = 7.5F * kPi / 180.0F;
	inline constexpr std::uint32_t kAllRequiredControlBits = 0x0FU;

	struct RequiredControlState
	{
		bool activate{ false };
		bool menu{ false };
		bool fighting{ false };
		bool wheelZoom{ false };
	};

	[[nodiscard]] constexpr std::uint32_t RequiredControlMask(
		const RequiredControlState& state) noexcept
	{
		return (state.activate ? 1U << 0 : 0U) |
		       (state.menu ? 1U << 1 : 0U) |
		       (state.fighting ? 1U << 2 : 0U) |
		       (state.wheelZoom ? 1U << 3 : 0U);
	}

	[[nodiscard]] constexpr bool AllRequiredControlsEnabled(
		const RequiredControlState& state) noexcept
	{
		return RequiredControlMask(state) == kAllRequiredControlBits;
	}

	[[nodiscard]] constexpr bool MatchesMappedInput(
		std::uint32_t inputID, std::uint32_t mappedID,
		std::uint32_t invalidMappedID) noexcept
	{
		return mappedID != invalidMappedID && inputID == mappedID;
	}

	[[nodiscard]] constexpr std::optional<std::size_t> StyleForInventoryLocalForm(
		std::uint32_t localFormID) noexcept
	{
		for (std::size_t index = 0; index < kInventoryLocalFormIDs.size(); ++index) {
			if (kInventoryLocalFormIDs[index] == localFormID)
				return index;
		}
		return std::nullopt;
	}

	[[nodiscard]] constexpr std::optional<std::size_t> StyleForStaticLocalForm(
		std::uint32_t localFormID) noexcept
	{
		for (std::size_t index = 0; index < kStaticLocalFormIDs.size(); ++index) {
			if (kStaticLocalFormIDs[index] == localFormID)
				return index;
		}
		return std::nullopt;
	}

	[[nodiscard]] constexpr std::optional<std::size_t> StyleForPlacedStaticLocalForm(
		std::uint32_t localFormID) noexcept
	{
		for (std::size_t index = 0; index < kPlacedStaticLocalFormIDs.size(); ++index) {
			if (kPlacedStaticLocalFormIDs[index] == localFormID)
				return index;
		}
		return std::nullopt;
	}

	[[nodiscard]] inline float NormalizeRadians(float radians) noexcept
	{
		if (!std::isfinite(radians))
			return 0.0F;
		radians = std::fmod(radians, kTwoPi);
		if (radians < 0.0F)
			radians += kTwoPi;
		return radians;
	}

	/**
	 * The standing-mirror pane normal is authored along local +X.  Return the
	 * Skyrim's clockwise Z rotation maps local +X to (cos(yaw), -sin(yaw)).
	 * Point that normal from the preview back at the player; user offsets are
	 * already expressed in the same clockwise convention as SetAngle.
	 */
	[[nodiscard]] inline float FacingYawRadians(
		float previewX, float previewY, float playerX, float playerY,
		float userOffsetRadians) noexcept
	{
		const float dx = playerX - previewX;
		const float dy = playerY - previewY;
		if (!std::isfinite(dx) || !std::isfinite(dy) ||
			(dx * dx + dy * dy) < 1.0e-6F) {
			return NormalizeRadians(userOffsetRadians);
		}
		return NormalizeRadians(std::atan2(-dy, dx) + userOffsetRadians);
	}

	// Follow-mode placement (Campfire-style): the preview stands a chosen
	// distance in front of the player, on the floor, facing the player.  The
	// player moves it by walking and turning; wheel/Up/Down change the distance
	// and Left/Right rotate it in kRotationStepRadians steps.
	inline constexpr float kMinimumPreviewDistance = 96.0F;
	inline constexpr float kMaximumPreviewDistance = 640.0F;
	inline constexpr float kPreviewDistanceStep = 32.0F;
	// Vertical Havok floor probe at the preview's X/Y: from this far above the
	// player's feet to this far below them.
	inline constexpr float kFloorProbeRise = 96.0F;
	inline constexpr float kFloorProbeDrop = 512.0F;
	// Fraction of the remaining distance the preview closes per update.
	inline constexpr float kPreviewFollowSmoothing = 0.45F;

	struct Point3
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	[[nodiscard]] inline float ClampPreviewDistance(float distance) noexcept
	{
		if (!std::isfinite(distance))
			return kPreviewDistance;
		if (distance < kMinimumPreviewDistance)
			return kMinimumPreviewDistance;
		if (distance > kMaximumPreviewDistance)
			return kMaximumPreviewDistance;
		return distance;
	}

	/** Target point on the player's heading at the given distance, at foot height. */
	[[nodiscard]] inline Point3 FollowTarget(
		float playerX, float playerY, float playerZ, float yawRadians,
		float distance) noexcept
	{
		const float clamped = ClampPreviewDistance(distance);
		return {
			playerX + std::sin(yawRadians) * clamped,
			playerY + std::cos(yawRadians) * clamped,
			playerZ
		};
	}

	// VR stick placement on top of follow mode.  The bearing is frozen as
	// placement goes live, so head turns leave the preview alone: walking
	// (left stick Y, still the player's) carries it along at the same offset,
	// left stick X slides it across the bearing, right stick X turns it and
	// right stick Y pushes it along the bearing.  Rates are per second of
	// full deflection; the dead zone is rescaled so motion starts from zero
	// at its edge instead of jumping.
	inline constexpr float kStickDeadZone = 0.2F;
	inline constexpr float kStickSlideUnitsPerSecond = 160.0F;
	inline constexpr float kStickDistanceUnitsPerSecond = 160.0F;
	inline constexpr float kStickRotateRadiansPerSecond = 0.5F * kPi;
	inline constexpr float kMaximumPreviewLateral = 320.0F;
	// Longest gap one stick sample may integrate over (hitches, menu returns);
	// anything longer counts as this much.
	inline constexpr float kMaximumStickDeltaSeconds = 0.1F;

	// VR trigger: an analog pull whose first event may already carry a hold
	// time, so the digital IsDown() edge is unreliable.  Confirm exactly once
	// when the pull rises through kVRTriggerPressValue; release below it.
	inline constexpr float kVRTriggerPressValue = 0.5F;
	inline constexpr float kVRTriggerReleaseValue = 0.2F;

	// Observe menu input before placement starts and copy that history into the
	// placement gate. A held selection stays disarmed; an unseen controller can
	// confirm on its first pull. Each input has its own release latch.
	class VRConfirmationGate
	{
	public:
		[[nodiscard]] bool Observe(std::uint32_t device, std::uint32_t button,
			float value, bool confirmationAllowed = true) noexcept
		{
			Entry* entry = nullptr;
			for (auto& candidate : entries_) {
				if (candidate.used && candidate.device == device && candidate.button == button) {
					entry = &candidate;
					break;
				}
			}
			if (!entry) {
				for (auto& candidate : entries_) {
					if (!candidate.used) {
						candidate = { device, button, true, confirmationAllowed };
						entry = &candidate;
						break;
					}
				}
			}
			if (!entry)
				return false;
			if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
				entry->armed = false;
				return false;
			}
			if (value <= kVRTriggerReleaseValue) {
				entry->armed = true;
				return false;
			}
			if (value < kVRTriggerPressValue)
				return false;
			const bool fresh = entry->armed;
			entry->armed = false;
			return fresh && confirmationAllowed;
		}

	private:
		struct Entry
		{
			std::uint32_t device{}, button{};
			bool used{}, armed{};
		};
		std::array<Entry, 16> entries_{};
	};

	/** Returns true exactly once per pull; `held` is the caller's latch. */
	[[nodiscard]] constexpr bool TriggerRisingEdge(bool& held, float value) noexcept
	{
		const bool pulled = value == value && value >= kVRTriggerPressValue;
		if (pulled == held)
			return false;
		held = pulled;
		return pulled;
	}

	/** Signed stick deflection with the dead zone removed and rescaled to [-1, 1]. */
	[[nodiscard]] inline float StickDeflection(float value) noexcept
	{
		if (!std::isfinite(value))
			return 0.0F;
		const float magnitude = std::abs(value);
		if (magnitude <= kStickDeadZone)
			return 0.0F;
		const float scaled = (magnitude - kStickDeadZone) / (1.0F - kStickDeadZone);
		const float clamped = scaled > 1.0F ? 1.0F : scaled;
		return value < 0.0F ? -clamped : clamped;
	}

	[[nodiscard]] inline float ClampStickDeltaSeconds(float seconds) noexcept
	{
		if (!std::isfinite(seconds) || seconds < 0.0F)
			return 0.0F;
		return seconds > kMaximumStickDeltaSeconds ? kMaximumStickDeltaSeconds : seconds;
	}

	[[nodiscard]] inline float ClampPreviewLateral(float lateral) noexcept
	{
		if (!std::isfinite(lateral))
			return 0.0F;
		if (lateral < -kMaximumPreviewLateral)
			return -kMaximumPreviewLateral;
		if (lateral > kMaximumPreviewLateral)
			return kMaximumPreviewLateral;
		return lateral;
	}

	/** Follow target slid across the heading; positive lateral is the player's right. */
	[[nodiscard]] inline Point3 FollowTarget(
		float playerX, float playerY, float playerZ, float yawRadians,
		float distance, float lateral) noexcept
	{
		const auto ahead = FollowTarget(playerX, playerY, playerZ, yawRadians, distance);
		const float clamped = ClampPreviewLateral(lateral);
		return {
			ahead.x + std::cos(yawRadians) * clamped,
			ahead.y - std::sin(yawRadians) * clamped,
			ahead.z
		};
	}

	/**
	 * Convert a floor-probe hit fraction (0 at the top of the probe, 1 at the
	 * bottom) into a world height.  Fractions of exactly 1 mean no hit.
	 */
	[[nodiscard]] inline bool FloorHeightFromProbe(
		float referenceZ, float hitFraction, float& output) noexcept
	{
		if (!std::isfinite(referenceZ) || !std::isfinite(hitFraction) ||
			hitFraction < 0.0F || hitFraction >= 1.0F) {
			return false;
		}
		output = (referenceZ + kFloorProbeRise) -
			hitFraction * (kFloorProbeRise + kFloorProbeDrop);
		return std::isfinite(output);
	}

	[[nodiscard]] inline Point3 SmoothTowards(
		const Point3& current, const Point3& target, float factor) noexcept
	{
		if (!std::isfinite(factor) || factor <= 0.0F || factor >= 1.0F)
			return target;
		return {
			current.x + (target.x - current.x) * factor,
			current.y + (target.y - current.y) * factor,
			current.z + (target.z - current.z) * factor
		};
	}
}
