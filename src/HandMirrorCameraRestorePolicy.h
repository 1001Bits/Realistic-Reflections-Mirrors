#pragma once

#include <cstdint>

namespace HandMirrorCameraRestorePolicy
{
	enum class RejectReason : std::uint8_t
	{
		kNone,
		kMissingIdentity,
		kWorldFrozenMismatch
	};

	struct Inputs
	{
		std::uintptr_t currentCamera{ 0 };
		std::uintptr_t worldRootCamera{ 0 };
		std::uintptr_t frozenSourceCamera{ 0 };
		std::uintptr_t currentAccumulator{ 0 };
	};

	struct Decision
	{
		std::uintptr_t cameraDataRestoreSource{ 0 };
		RejectReason rejectReason{ RejectReason::kMissingIdentity };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return rejectReason == RejectReason::kNone &&
				cameraDataRestoreSource != 0;
		}
	};

	[[nodiscard]] constexpr Decision Evaluate(const Inputs& inputs) noexcept
	{
		if (inputs.currentCamera == 0 || inputs.worldRootCamera == 0 ||
			inputs.frozenSourceCamera == 0 || inputs.currentAccumulator == 0) {
			return {};
		}
		// The post-world callback can legitimately retain a renderer-current camera
		// which differs from WorldRootCamera.  They are independently owned state:
		// the former is restored through the relocated global, while the latter is
		// the exact source used to restore main camera constants.  Only the frozen
		// raster identity must coincide with the live WorldRoot source.
		if (inputs.worldRootCamera != inputs.frozenSourceCamera) {
			return { 0, RejectReason::kWorldFrozenMismatch };
		}
		return { inputs.worldRootCamera, RejectReason::kNone };
	}
}
