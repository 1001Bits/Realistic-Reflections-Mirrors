#pragma once

#include <atomic>

// Capture cost cuts (owner, 2026-09-17: "do all this"). Each has a switch so
// the built-in benchmark can measure it on and off at the same spot.
namespace MirrorCaptureOptimizations
{
	// Standing mirrors cull only what the pane can show (its viewing cone plus
	// the projection's own margin) instead of the union with the main view's
	// field of view, and submit only the nearby references inside that cone.
	inline std::atomic_bool tightPaneCull{ true };
	// The tiny-object skip (under 1.5 capture pixels) was removed after core
	// run 9: indoors nothing is far enough away to be that small (a coin would
	// need to be over 7,000 units away), and it skipped 0 objects all run.
	// The reflected-surface light selection prepares its candidate lights once
	// per capture instead of once per draw.
	inline std::atomic_bool lightCandidateCache{ true };
	// Indoors, the borrowed main-view lists are culled without the references
	// the near-reference cycle already draws (the replay roster filter).
	inline std::atomic_bool replayRosterFilter{ true };
	// Standing mirrors render at the size they appear on screen (with margin
	// and supersampling), never above the configured resolution.
	inline std::atomic_bool screenSizedCapture{ true };
	// ... and shaped like the pane: each axis sized on its own (core run 9).
	inline std::atomic_bool rectangularCapture{ true };
	// The nearby-reference list is enumerated at most every 0.5 s; captures in
	// between re-check the listed references instead of walking the whole cell.
	inline std::atomic_bool rosterListReuse{ true };
	// A static object's light choice is reused while the scene's light set is
	// unchanged, instead of being recomputed on every capture.
	inline std::atomic_bool lightChoiceCache{ true };
	// Nearby references in rooms the mirror cannot see into are not drawn.
	inline std::atomic_bool roomCull{ true };

	[[nodiscard]] inline bool AnyOn() noexcept
	{
		return tightPaneCull.load(std::memory_order_relaxed) ||
			lightCandidateCache.load(std::memory_order_relaxed) || replayRosterFilter.load(std::memory_order_relaxed) ||
			screenSizedCapture.load(std::memory_order_relaxed) || rectangularCapture.load(std::memory_order_relaxed) ||
			rosterListReuse.load(std::memory_order_relaxed) || lightChoiceCache.load(std::memory_order_relaxed) ||
			roomCull.load(std::memory_order_relaxed);
	}
}
