#pragma once

#include <utility>

namespace SecondViewDriverSchedule
{
	/**
	 * First chain Skyrim's exact DrawWorld_BuildSceneLists JobList::Finish call,
	 * then run the fully bounded private capture and its idempotent pane cleanup.
	 * The caller subsequently executes ProcessAllQueuedLights before it begins
	 * shadow/light rebuilding and main-world accumulation.
	 *
	 * Both sides of RenderWaterEffects are inside the submitted scene-list job
	 * window. Immediately before RenderWorld is too late because the main
	 * accumulator already owns live BSRenderPass light lists there. This fence
	 * tail is the only verified point satisfying both job quiescence and a
	 * downstream native queued-light finalizer.
	 */
	template <class FenceCallback, class CaptureCallback, class CleanupCallback>
	constexpr void DispatchAfterSceneListFence(
		FenceCallback&& a_fence,
		CaptureCallback&& a_capture,
		CleanupCallback&& a_cleanup)
		noexcept(noexcept(std::forward<FenceCallback>(a_fence)()) &&
		         noexcept(std::forward<CaptureCallback>(a_capture)()) &&
		         noexcept(std::forward<CleanupCallback>(a_cleanup)()))
	{
		std::forward<FenceCallback>(a_fence)();
		std::forward<CaptureCallback>(a_capture)();
		std::forward<CleanupCallback>(a_cleanup)();
	}

	/**
	 * The later RenderWorld driver no longer captures. It only guarantees that
	 * fallback suppression is clear and brackets the complete peer-wrapped main
	 * world for the standalone pane consumer.
	 */
	template <class CleanupCallback, class WorldCallback>
	constexpr void DispatchMainWorld(
		CleanupCallback&& a_cleanup,
		WorldCallback&& a_world)
		noexcept(noexcept(std::forward<CleanupCallback>(a_cleanup)()) &&
		         noexcept(std::forward<WorldCallback>(a_world)()))
	{
		std::forward<CleanupCallback>(a_cleanup)();
		std::forward<WorldCallback>(a_world)();
	}
}
