#pragma once

#include "HandMirrorPortraitViewPolicy.h"

namespace HandMirrorNeutralCalibrationPolicy
{
	using namespace HandMirrorPortraitViewPolicy;

	/** One settled calibration per raise. Drawing while walking postpones the
	 * correction until the reference is still; subsequent animation cannot pan
	 * the camera. The initial/prior anchor remains usable while gathering evidence.
	 */
	struct State
	{
		ReferencePose startPose{};
		FaceAnchor sum{};
		std::uint64_t lastSource{}, startedMilliseconds{};
		std::uint32_t samples{};
		bool tracking{}, completed{};

		void Reset() noexcept { *this = {}; }

		bool Observe(const ReferencePose& pose, const Point& face,
			std::uint64_t source, std::uint64_t milliseconds, FaceAnchor& anchor) noexcept
		{
			if (completed || source == 0 || source <= lastSource)
				return false;
			FaceAnchor sample{};
			StableView ignored{};
			if (!BuildStableView(pose, face, sample, ignored))
				return false;
			lastSource = source;
			const float dx = pose.position.x - startPose.position.x;
			const float dy = pose.position.y - startPose.position.y;
			const float dz = pose.position.z - startPose.position.z;
			const bool still = tracking && milliseconds >= startedMilliseconds &&
				dx * dx + dy * dy + dz * dz <= 0.25F &&
				std::abs(std::remainder(pose.yaw - startPose.yaw, 6.283185307F)) <= 0.02F &&
				std::abs(pose.pitch - startPose.pitch) <= 0.02F;
			if (!still) {
				tracking = true;
				startPose = pose;
				startedMilliseconds = milliseconds;
				samples = 0;
				sum = {};
			}
			// Let the raise animation settle before sampling its neutral pose.
			if (milliseconds - startedMilliseconds < 300) {
				return false;
			}
			sum.right += sample.right;
			sum.forward += sample.forward;
			sum.height += sample.height;
			++samples;
			if (samples < 8)
				return false;
			const float divisor = static_cast<float>(samples);
			anchor = { sum.right / divisor, sum.forward / divisor, sum.height / divisor, true };
			completed = true;
			return true;
		}
	};
}
