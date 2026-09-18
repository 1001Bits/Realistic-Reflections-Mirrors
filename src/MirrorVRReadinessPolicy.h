#pragma once

#include <cstdint>

namespace MirrorVRReadinessPolicy
{
	// A VR build is not a VR renderer port.  Each bit below names evidence that
	// must exist before Mirrors of Skyrim may install any reflective hand-mirror
	// hook on Skyrim VR 1.4.15.  Keep this value contract engine-independent so
	// offline tests can lock the fail-closed boundary without resolving a single
	// runtime address.
	enum class Blocker : std::uint32_t
	{
		kNone = 0,
		kRuntimeAddressLibraryDeploymentUnvalidated = 1u << 0,
		kHandAssetsNotAuthoredForVR = 1u << 1,
		kPluginFormatNotAcceptedForVR = 1u << 2,
		kFirstPersonCallSiteUnmapped = 1u << 3,
		kStereoCaptureScheduleUnproven = 1u << 4,
		kStereoPresentationTargetUnproven = 1u << 5,
		kStereoCameraLayoutUnproven = 1u << 6,
		kSetCameraDataUnmapped = 1u << 7,
		kCurrentCameraRestoreUnmapped = 1u << 8,
		kCurrentAccumulatorSetterUnmapped = 1u << 9,
		kWorldFrustumBuilderUnmapped = 1u << 10,
		kNativeCameraOwnershipUnmapped = 1u << 11,
		kSceneListFenceCallSiteUnmapped = 1u << 12,
		kGenericDrawHookUnaccepted = 1u << 13,
		kSoftEffectHookUnaccepted = 1u << 14,
		kBillboardHookUnaccepted = 1u << 15,
		kParticleCameraHookUnaccepted = 1u << 16,
		kLightSelectionHooksUnaccepted = 1u << 17,
		kPhysicalClipPathUnaccepted = 1u << 18,
		kAvatarPoseAndVRIKUnaccepted = 1u << 19
	};

	using BlockerMask = std::uint32_t;

	[[nodiscard]] constexpr BlockerMask Mask(const Blocker value) noexcept
	{
		return static_cast<BlockerMask>(value);
	}

	[[nodiscard]] constexpr bool Contains(
		const BlockerMask mask,
		const Blocker blocker) noexcept
	{
		return (mask & Mask(blocker)) != 0;
	}

	struct Evidence
	{
		bool runtimeAddressLibraryDeploymentValidated{ false };
		bool handAssetsAuthoredForVR{ false };
		bool pluginFormatAcceptedForVR{ false };
		bool firstPersonCallSiteMapped{ false };
		bool stereoCaptureScheduleProven{ false };
		bool stereoPresentationTargetProven{ false };
		bool stereoCameraLayoutProven{ false };
		bool setCameraDataMapped{ false };
		bool currentCameraRestoreMapped{ false };
		bool currentAccumulatorSetterMapped{ false };
		bool worldFrustumBuilderMapped{ false };
		bool nativeCameraOwnershipMapped{ false };
		bool sceneListFenceCallSiteMapped{ false };
		bool genericDrawHookAccepted{ false };
		bool softEffectHookAccepted{ false };
		bool billboardHookAccepted{ false };
		bool particleCameraHookAccepted{ false };
		bool lightSelectionHooksAccepted{ false };
		bool physicalClipPathAccepted{ false };
		bool avatarPoseAndVRIKAccepted{ false };
	};

	struct Decision
	{
		BlockerMask blockers{};

		[[nodiscard]] constexpr bool Ready() const noexcept
		{
			return blockers == 0;
		}
	};

	[[nodiscard]] constexpr Decision Evaluate(const Evidence& evidence) noexcept
	{
		Decision result{};
		if (!evidence.runtimeAddressLibraryDeploymentValidated) {
			result.blockers |=
				Mask(Blocker::kRuntimeAddressLibraryDeploymentUnvalidated);
		}
		if (!evidence.handAssetsAuthoredForVR)
			result.blockers |= Mask(Blocker::kHandAssetsNotAuthoredForVR);
		if (!evidence.pluginFormatAcceptedForVR)
			result.blockers |= Mask(Blocker::kPluginFormatNotAcceptedForVR);
		if (!evidence.firstPersonCallSiteMapped)
			result.blockers |= Mask(Blocker::kFirstPersonCallSiteUnmapped);
		if (!evidence.stereoCaptureScheduleProven)
			result.blockers |= Mask(Blocker::kStereoCaptureScheduleUnproven);
		if (!evidence.stereoPresentationTargetProven)
			result.blockers |= Mask(Blocker::kStereoPresentationTargetUnproven);
		if (!evidence.stereoCameraLayoutProven)
			result.blockers |= Mask(Blocker::kStereoCameraLayoutUnproven);
		if (!evidence.setCameraDataMapped)
			result.blockers |= Mask(Blocker::kSetCameraDataUnmapped);
		if (!evidence.currentCameraRestoreMapped)
			result.blockers |= Mask(Blocker::kCurrentCameraRestoreUnmapped);
		if (!evidence.currentAccumulatorSetterMapped)
			result.blockers |= Mask(Blocker::kCurrentAccumulatorSetterUnmapped);
		if (!evidence.worldFrustumBuilderMapped)
			result.blockers |= Mask(Blocker::kWorldFrustumBuilderUnmapped);
		if (!evidence.nativeCameraOwnershipMapped)
			result.blockers |= Mask(Blocker::kNativeCameraOwnershipUnmapped);
		if (!evidence.sceneListFenceCallSiteMapped)
			result.blockers |= Mask(Blocker::kSceneListFenceCallSiteUnmapped);
		if (!evidence.genericDrawHookAccepted)
			result.blockers |= Mask(Blocker::kGenericDrawHookUnaccepted);
		if (!evidence.softEffectHookAccepted)
			result.blockers |= Mask(Blocker::kSoftEffectHookUnaccepted);
		if (!evidence.billboardHookAccepted)
			result.blockers |= Mask(Blocker::kBillboardHookUnaccepted);
		if (!evidence.particleCameraHookAccepted)
			result.blockers |= Mask(Blocker::kParticleCameraHookUnaccepted);
		if (!evidence.lightSelectionHooksAccepted)
			result.blockers |= Mask(Blocker::kLightSelectionHooksUnaccepted);
		if (!evidence.physicalClipPathAccepted)
			result.blockers |= Mask(Blocker::kPhysicalClipPathUnaccepted);
		if (!evidence.avatarPoseAndVRIKAccepted)
			result.blockers |= Mask(Blocker::kAvatarPoseAndVRIKUnaccepted);
		return result;
	}

	// 2026-09-04: the owner accepted Skyrim VR 1.4.15 as a supported runtime.
	// Every call site, camera seam, accumulator/descriptor layout and vtable
	// slot was re-derived from the decompiled VR executable (Ghidra Combined
	// project; evidence archived under
	// .runtime-review/v158-vr-first-deploy-20260904/ghidra-vr-id-evidence and
	// the VR entries of the runtime policy headers).  Each hook still verifies
	// its own byte window against the live image before it arms, so an
	// unexpected VR build fails closed per hook rather than per product.
	inline constexpr Evidence kCurrentSkyrimVR1415Evidence{
		.runtimeAddressLibraryDeploymentValidated = true,
		.handAssetsAuthoredForVR = true,
		.pluginFormatAcceptedForVR = true,
		.firstPersonCallSiteMapped = true,
		.stereoCaptureScheduleProven = true,
		.stereoPresentationTargetProven = true,
		.stereoCameraLayoutProven = true,
		.setCameraDataMapped = true,
		.currentCameraRestoreMapped = true,
		.currentAccumulatorSetterMapped = true,
		.worldFrustumBuilderMapped = true,
		.nativeCameraOwnershipMapped = true,
		.sceneListFenceCallSiteMapped = true,
		.genericDrawHookAccepted = true,
		.softEffectHookAccepted = true,
		.billboardHookAccepted = true,
		.particleCameraHookAccepted = true,
		.lightSelectionHooksAccepted = true,
		.physicalClipPathAccepted = true,
		.avatarPoseAndVRIKAccepted = true
	};
	inline constexpr Decision kCurrentSkyrimVR1415Decision =
		Evaluate(kCurrentSkyrimVR1415Evidence);

	inline constexpr BlockerMask kAllCurrentBlockers = 0;
	inline constexpr BlockerMask kEveryBlocker = (1u << 20) - 1u;

	static_assert(kCurrentSkyrimVR1415Decision.blockers == kAllCurrentBlockers);
	static_assert(kCurrentSkyrimVR1415Decision.Ready());
	static_assert(Evaluate(Evidence{}).blockers == kEveryBlocker);
}
