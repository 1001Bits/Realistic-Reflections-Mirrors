#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace HandMirrorPortraitViewPolicy
{
	inline constexpr float kPitchGain = 0.5F;
	inline constexpr float kMaximumPitchSine = 0.6428F;
	inline constexpr float kCenteredPaneOffset = 0.0F;

	[[nodiscard]] constexpr bool Finite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
	}

	/** Input is the frozen viewer direction, before building the portrait camera.
	 * A level view is level even when the shield's idle animation tilts its pane.
	 * Negative source pitch raises the reflected eye, so looking up uses -Z.
	 */
	[[nodiscard]] inline bool PitchFromView(float x, float y, float z,
		float& pitchSine) noexcept
	{
		pitchSine = 0.0F;
		if (!Finite(x) || !Finite(y) || !Finite(z))
			return false;
		const float length = std::sqrt(x * x + y * y + z * z);
		if (!Finite(length) || length <= 1.0e-5F)
			return false;
		pitchSine = std::clamp(-z / length * kPitchGain,
			-kMaximumPitchSine, kMaximumPitchSine);
		return true;
	}

	struct Point
	{
		float x{}, y{}, z{};
		constexpr bool operator==(const Point&) const noexcept = default;
	};

	struct ReferencePose
	{
		Point position{};
		float yaw{}, pitch{};
		bool valid{};
	};

	struct FaceAnchor
	{
		float right{}, forward{}, height{};
		bool valid{};
		constexpr bool operator==(const FaceAnchor&) const noexcept = default;
	};

	struct StableView
	{
		Point origin{}, forward{}, right{}, up{};
	};

	[[nodiscard]] constexpr bool Finite(const Point& p) noexcept
	{
		return Finite(p.x) && Finite(p.y) && Finite(p.z);
	}

	[[nodiscard]] constexpr bool Valid(const ReferencePose& pose) noexcept
	{
		return pose.valid && Finite(pose.position) && Finite(pose.yaw) && Finite(pose.pitch);
	}

	/** Freeze the neutral face offset once. Subsequent animation must not pan or
	 * steer the whole reflected room. Reference yaw/pitch are player look angles,
	 * not the rendered body's strafe rotation or the first-person camera spring.
	 * The caller stores this reference pose with its exact optical frame cohort.
	 */
	[[nodiscard]] inline bool BuildStableView(const ReferencePose& pose,
		const Point& calibrationFace, FaceAnchor& anchor, StableView& output) noexcept
	{
		if (!Valid(pose))
			return false;
		const float sx = std::sin(pose.yaw);
		const float cy = std::cos(pose.yaw);
		auto pending = anchor;
		if (!pending.valid) {
			if (!Finite(calibrationFace))
				return false;
			const Point delta{ calibrationFace.x - pose.position.x,
				calibrationFace.y - pose.position.y, calibrationFace.z - pose.position.z };
			pending = { delta.x * cy - delta.y * sx,
				delta.x * sx + delta.y * cy, delta.z, true };
		}
		if (!Finite(pending.right) || !Finite(pending.forward) ||
			!Finite(pending.height) || pending.height <= 0.0F ||
			std::abs(pending.right) > 512.0F || std::abs(pending.forward) > 512.0F ||
			pending.height > 512.0F)
			return false;
		const float pitchSine = std::clamp(std::sin(pose.pitch) * kPitchGain,
			-kMaximumPitchSine, kMaximumPitchSine);
		const float pitchCosine = std::sqrt(1.0F - pitchSine * pitchSine);
		const StableView view{
			.origin = { pose.position.x + cy * pending.right + sx * pending.forward,
				pose.position.y - sx * pending.right + cy * pending.forward,
				pose.position.z + pending.height },
			.forward = { sx * pitchCosine, cy * pitchCosine, -pitchSine },
			.right = { cy, -sx, 0.0F },
			.up = { sx * pitchSine, cy * pitchSine, pitchCosine }
		};
		if (!Finite(view.origin) || !Finite(view.forward) || !Finite(view.right) || !Finite(view.up))
			return false;
		anchor = pending;
		output = view;
		return true;
	}
}
