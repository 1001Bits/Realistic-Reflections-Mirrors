#pragma once

namespace RE
{
	class BSRenderPass;
}

/**
 * Read-only per-pass probe for the hand exact-main private capture.
 *
 * Owner-visible defect (2026-09-02 V110/V111): separate geometries of one rig
 * disagree inside the capture (a helmet piece floats detached on the face,
 * NPC parts appear twice).  Skyrim builds skinned bone matrices once per global
 * frame counter per NiSkinInstance (helper 0x140D74F70 on SE 1.5.97) from the
 * live bone world transforms, while rigid geometry reads its node world
 * transform at its own draw.  Two mechanisms can therefore split one rig:
 *   1. a skin instance whose matrices were already built earlier in the frame by
 *      another consumer, before the pose changed (cache prepopulated), and
 *   2. bone world transforms changing between two draws of the same private
 *      pass (skeleton updated concurrently or by a hook mid-pass).
 * This probe counts both.  It never writes engine state.
 */
namespace HandSkinPoseConsistencyProbe
{
	/** Render thread: the hand exact-main private pass has begun. */
	void BeginScope() noexcept;
	/** Render thread: the hand exact-main private pass has ended (any path). */
	void EndScope() noexcept;
	/** Generic geometry draw entry, before the native draw. No-op outside a scope. */
	void OnBeforeGenericDraw(RE::BSRenderPass* pass) noexcept;
	void LogDiagnostics(const char* reason);
}
