#pragma once

#include "HandMirrorFrameReceiptPolicy.h"

namespace HandMirrorOpticalCadencePolicy
{
	using HandMirrorFrameReceiptPolicy::FrameReceipt;

	// Optical observations follow main views, even when another mirror owns this
	// source's one capture opportunity. Never overwrite a sample from this source.
	[[nodiscard]] constexpr bool NeedsObservation(bool independentScheduling,
		const FrameReceipt& retained, const FrameReceipt& current) noexcept
	{
		return independentScheduling &&
			HandMirrorFrameReceiptPolicy::IsValidFrameReceipt(current) &&
			(!HandMirrorFrameReceiptPolicy::IsValidFrameReceipt(retained) ||
				current.sourceSequence > retained.sourceSequence);
	}

	[[nodiscard]] constexpr bool Adjacent(const FrameReceipt& previous,
		const FrameReceipt& current) noexcept
	{
		using namespace HandMirrorFrameReceiptPolicy;
		return IsValidFrameReceipt(previous) && IsValidFrameReceipt(current) &&
			IsStrictSuccessor(previous.sourceSequence, current.sourceSequence) &&
			IsStrictSuccessor(previous.mainViewFrame, current.mainViewFrame) &&
			IsStrictSuccessor(previous.graphicsFrame, current.graphicsFrame);
	}
}
