#pragma once

#include "HandMirrorFrameReceiptPolicy.h"
#include "HandMirrorRuntimeBridgePolicy.h"

namespace HandMirrorOuterFirstPersonClosePolicy
{
	struct CandidateValue
	{
		HandMirrorFrameReceiptPolicy::FrameReceipt receipt{};
		HandMirrorRuntimeBridgePolicy::MovingSurfaceSample surface{};
		bool hiddenThirdPersonFresh{ false };
	};

	struct ValueIdentityEquality
	{
		bool receipt{ false };
		bool receiptBindsBothSurfaces{ false };
		bool hiddenFresh{ false };
		bool owner{ false };
		bool stableGeneration{ false };
		bool surfaceIdentity{ false };
		bool geometry{ false };

		[[nodiscard]] constexpr bool AllExact() const noexcept
		{
			return receipt && receiptBindsBothSurfaces && hiddenFresh && owner &&
			       stableGeneration && surfaceIdentity && geometry;
		}
	};

	[[nodiscard]] constexpr bool SameOwnerAndGeometry(
		const HandMirrorRuntimeBridgePolicy::MovingSurfaceSample& left,
		const HandMirrorRuntimeBridgePolicy::MovingSurfaceSample& right) noexcept
	{
		using namespace HandMirrorRuntimeBridgePolicy;
		return IsValidMovingSurfaceSample(left) &&
		       IsValidMovingSurfaceSample(right) && left.owner == right.owner &&
		       left.sourceSequence == right.sourceSequence &&
		       left.pose.stableGeneration == right.pose.stableGeneration &&
		       left.pose.mainViewFrame == right.pose.mainViewFrame &&
		       left.pose.observedPaneSubtree == right.pose.observedPaneSubtree &&
		       left.pose.authoredSurface == right.pose.authoredSurface &&
		       SameGeometryBits(left.pose.geometry, right.pose.geometry);
	}

	/**
	 * Stable identity which must survive the native pane call.
	 *
	 * Skyrim is allowed to finalize the equipped clone's presentation transform
	 * while drawing it, so entry geometry is observation evidence rather than the
	 * authoritative returned pose.  Owner, receipt binding, pane subtree, and
	 * authored-surface identity may not drift.
	 */
	[[nodiscard]] constexpr bool SameOwnerAndSurfaceIdentity(
		const HandMirrorRuntimeBridgePolicy::MovingSurfaceSample& left,
		const HandMirrorRuntimeBridgePolicy::MovingSurfaceSample& right) noexcept
	{
		using namespace HandMirrorRuntimeBridgePolicy;
		return IsValidMovingSurfaceSample(left) &&
		       IsValidMovingSurfaceSample(right) && left.owner == right.owner &&
		       left.sourceSequence == right.sourceSequence &&
		       left.pose.stableGeneration == right.pose.stableGeneration &&
		       left.pose.mainViewFrame == right.pose.mainViewFrame &&
		       left.pose.observedPaneSubtree == right.pose.observedPaneSubtree &&
		       left.pose.authoredSurface == right.pose.authoredSurface;
	}

	[[nodiscard]] constexpr ValueIdentityEquality Compare(
		const CandidateValue& staged,
		const CandidateValue& current) noexcept
	{
		using namespace HandMirrorFrameReceiptPolicy;
		using namespace HandMirrorRuntimeBridgePolicy;
		const bool validSurfaces = IsValidMovingSurfaceSample(staged.surface) &&
			IsValidMovingSurfaceSample(current.surface);
		return {
			.receipt = IsValidFrameReceipt(staged.receipt) &&
				IsValidFrameReceipt(current.receipt) &&
				staged.receipt == current.receipt,
			.receiptBindsBothSurfaces =
				validSurfaces &&
				staged.surface.sourceSequence == staged.receipt.sourceSequence &&
				current.surface.sourceSequence == current.receipt.sourceSequence &&
				staged.surface.pose.mainViewFrame ==
					staged.receipt.mainViewFrame &&
				current.surface.pose.mainViewFrame ==
					current.receipt.mainViewFrame,
			.hiddenFresh = staged.hiddenThirdPersonFresh &&
				current.hiddenThirdPersonFresh,
			.owner = validSurfaces && staged.surface.owner == current.surface.owner,
			.stableGeneration = validSurfaces &&
				staged.surface.pose.stableGeneration ==
					current.surface.pose.stableGeneration,
			.surfaceIdentity = validSurfaces &&
				staged.surface.sourceSequence == current.surface.sourceSequence &&
				staged.surface.pose.mainViewFrame ==
					current.surface.pose.mainViewFrame &&
				staged.surface.pose.observedPaneSubtree ==
					current.surface.pose.observedPaneSubtree &&
				staged.surface.pose.authoredSurface ==
					current.surface.pose.authoredSurface,
			.geometry = validSurfaces && SameGeometryBits(
				staged.surface.pose.geometry, current.surface.pose.geometry)
		};
	}
}
