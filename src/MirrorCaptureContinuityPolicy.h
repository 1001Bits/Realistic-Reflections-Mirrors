#pragma once

#include <cstdint>

namespace MirrorCaptureContinuityPolicy
{
	enum class FailureStage : std::uint8_t
	{
		kRenderTargetUnavailable,
		kCameraPreparationUnavailable,
		kStableAdmissionChanged,
		kTargetBeginUnavailable,
		kExactMainUnavailable,
		kCleanPrivatePassSkipped,
		kPlayerEvidenceUnavailable,
		kMipGenerationUnavailable,
		kResourceRetentionUnavailable,
		kDeferredPairUnavailable,
		kPublishRejected,
		kIdentityChanged,
		kLocationChanged,
		kCleanupFault,
		kGlobalFault
	};

	struct Observation
	{
		bool featureEnabled{ false };
		bool mirrorSelected{ false };
		bool exactIdentityCurrent{ false };
		bool locationCurrent{ false };
		bool captureRoleDisjoint{ false };
		bool cleanupRestored{ false };
		bool faulted{ false };
	};

	[[nodiscard]] constexpr bool IsTransientStage(const FailureStage stage) noexcept
	{
		switch (stage) {
		case FailureStage::kRenderTargetUnavailable:
		case FailureStage::kCameraPreparationUnavailable:
		case FailureStage::kStableAdmissionChanged:
		case FailureStage::kTargetBeginUnavailable:
		case FailureStage::kExactMainUnavailable:
		case FailureStage::kCleanPrivatePassSkipped:
		case FailureStage::kPlayerEvidenceUnavailable:
		case FailureStage::kMipGenerationUnavailable:
		case FailureStage::kResourceRetentionUnavailable:
		case FailureStage::kDeferredPairUnavailable:
			return true;
		default:
			return false;
		}
	}

	/**
	 * Retain a completed mirror publication only when a clean failure occurred on
	 * a physically disjoint replacement target for the same live surface.
	 *
	 * Publish rejection is deliberately not transient: after all prerequisites
	 * passed it normally means the attempt token or resource contract changed.
	 */
	[[nodiscard]] constexpr bool PreserveCompletedFrame(
		const Observation& observation,
		const FailureStage stage) noexcept
	{
		return observation.featureEnabled && observation.mirrorSelected &&
			observation.exactIdentityCurrent && observation.locationCurrent &&
			observation.captureRoleDisjoint && observation.cleanupRestored &&
			!observation.faulted && IsTransientStage(stage);
	}
}
