#pragma once

#include <cstdint>

namespace MirrorCameraUploadLeasePolicy
{
	enum class Phase : std::uint8_t
	{
		kInactive,
		kArmed,
		kPatching,
		kPatched,
		kFailed
	};

	enum class PatchOutcome : std::uint8_t
	{
		kPatched,
		kNotOwned,
		kRejected
	};

	inline constexpr std::uint32_t kPrivateCameraFlags = 8;
	inline constexpr std::uint32_t kPrivateCameraOptionalFlag = 4;

	[[nodiscard]] constexpr bool IsInitialPrivateUpload(
		const std::uint32_t cameraFlags) noexcept
	{
		// The pinned private wrapper's first SetCameraData call is exactly flags 8.
		return cameraFlags == kPrivateCameraFlags;
	}

	[[nodiscard]] constexpr bool IsPrivateUpload(
		const std::uint32_t cameraFlags) noexcept
	{
		// After the exact flags-8 owner is established, private repacks use 8 and
		// 12. Flags 1 is the foreign PreResolve tail and must pass through untouched.
		return (cameraFlags & ~kPrivateCameraOptionalFlag) == kPrivateCameraFlags;
	}

	[[nodiscard]] constexpr bool IsInitialAttempt(const Phase phase) noexcept
	{
		return phase == Phase::kArmed;
	}

	[[nodiscard]] constexpr bool IsRepeatedAttempt(
		const Phase phase,
		const bool mirrorCapture) noexcept
	{
		return phase == Phase::kPatched && mirrorCapture;
	}

	[[nodiscard]] constexpr Phase CompleteAttempt(
		const PatchOutcome outcome,
		const bool reentrantUploadSeen) noexcept
	{
		return outcome == PatchOutcome::kPatched && !reentrantUploadSeen ?
			Phase::kPatched : Phase::kFailed;
	}
}
