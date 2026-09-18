#pragma once

#include "PlanarMipChainPolicy.h"

#include <atomic>
#include <cstdint>

/**
 * Lowered (non-raised) hand-mirror presentation policy.
 *
 * Raised presentation is the portrait window; lowered presentation is the
 * conventional physical reflection captured through the previous frame's
 * frozen source frustum united with the pane fit.  Two owner-visible defects
 * (2026-09-03/04 runs) came from that lowered path:
 *
 * 1. The lowered pane hugs the screen edge, so a single frame of camera motion
 *    moved part of the live aperture outside the one-frame-old capture frustum.
 *    The fresh draw then reported partial/uncovered projection coverage and the
 *    pane replayed its cached last-good image for the whole motion, which the
 *    owner saw as a frozen face lingering in the lowered mirror.
 * 2. That cached image had no age bound in lowered mode, so a stale frame could
 *    be re-presented indefinitely.
 *
 * Both are pure-value decisions kept here so they can be unit-tested without
 * the renderer.
 */
namespace HandMirrorLoweredPresentationPolicy
{
	inline std::atomic_bool reducedResolutionEnabled{ false };
	// Owner 2026-09-14: both hand sliders run 512-4096. The two poses share one
	// allocation sized to the larger setting; the smaller pose renders into a
	// reduced mip view of it (up to three levels: 4096 -> 512), so raising or
	// lowering never reallocates. Without the lowered-resolution controls the
	// target stays the fixed 2048 base.
	inline constexpr std::uint32_t kMinimumResolution = 512;
	inline constexpr std::uint32_t kMaximumResolution = 4096;
	inline constexpr std::uint32_t kMaximumReducedMips = 3;
	inline constexpr std::uint32_t kRaisedResolution = 2048;
	inline constexpr std::uint32_t kLoweredResolution = 1024;

	[[nodiscard]] constexpr bool IsSupportedResolution(const std::uint32_t resolution) noexcept
	{
		return resolution >= kMinimumResolution && resolution <= kMaximumResolution &&
			(resolution & (resolution - 1)) == 0;
	}

	/** Complete mip chain length of a square power-of-two dimension. */
	[[nodiscard]] constexpr std::uint32_t MipLevelsFor(std::uint32_t dimension) noexcept
	{
		std::uint32_t levels = 1;
		while (dimension > 1) { dimension >>= 1; ++levels; }
		return levels;
	}

	/** Mip index at which `base` shrinks to `reduced` (0 when not smaller). */
	[[nodiscard]] constexpr std::uint32_t ReducedMip(
		const std::uint32_t base, std::uint32_t reduced) noexcept
	{
		std::uint32_t mip = 0;
		while (reduced < base && mip < kMaximumReducedMips) { reduced <<= 1; ++mip; }
		return mip;
	}

	/** Edge of the shared hand allocation for the current settings. */
	[[nodiscard]] constexpr std::uint32_t TargetDimension(
		const bool reducedEnabled,
		const std::uint32_t raisedResolution = kRaisedResolution,
		const std::uint32_t loweredResolution = kLoweredResolution) noexcept
	{
		if (!reducedEnabled)
			return kRaisedResolution;
		const std::uint32_t raised = IsSupportedResolution(raisedResolution) ? raisedResolution : kRaisedResolution;
		const std::uint32_t lowered = IsSupportedResolution(loweredResolution) ? loweredResolution : kLoweredResolution;
		return raised > lowered ? raised : lowered;
	}

	/** Reduced views precreated below the base: every power of two down to 512. */
	[[nodiscard]] constexpr std::uint32_t ReducedViewCount(
		const bool reducedEnabled,
		const std::uint32_t raisedResolution = kRaisedResolution,
		const std::uint32_t loweredResolution = kLoweredResolution) noexcept
	{
		return reducedEnabled ?
			ReducedMip(TargetDimension(true, raisedResolution, loweredResolution), kMinimumResolution) : 0u;
	}

	[[nodiscard]] constexpr std::uint32_t CaptureMip(
		const bool raised, const bool reducedEnabled,
		const std::uint32_t raisedResolution = kRaisedResolution,
		const std::uint32_t loweredResolution = kLoweredResolution) noexcept
	{
		if (!reducedEnabled)
			return 0u;
		const std::uint32_t selected = raised ?
			(IsSupportedResolution(raisedResolution) ? raisedResolution : kRaisedResolution) :
			(IsSupportedResolution(loweredResolution) ? loweredResolution : kLoweredResolution);
		return ReducedMip(TargetDimension(true, raisedResolution, loweredResolution), selected);
	}

	/** Completed frames retain their captured size, even if the menu changes later. */
	[[nodiscard]] constexpr bool CaptureDimensionsAllowed(
		std::uint32_t width, std::uint32_t height, bool reducedEnabled) noexcept
	{
		return width == height &&
			(reducedEnabled ? IsSupportedResolution(width) : width == kRaisedResolution);
	}

	[[nodiscard]] constexpr std::uint32_t CaptureResolution(
		const bool raised, const bool reducedEnabled) noexcept
	{
		return TargetDimension(reducedEnabled) >> CaptureMip(raised, reducedEnabled);
	}

	/** Validate the complete sampled chain, including a reduced (mip 1..3) hand view. */
	[[nodiscard]] constexpr bool ColorViewDimensionsMatch(
		std::uint32_t textureWidth, std::uint32_t textureHeight,
		std::uint32_t textureMips, std::uint32_t firstMip, std::uint32_t viewMips,
		std::uint32_t frameWidth, std::uint32_t frameHeight,
		bool reducedHandAllowed) noexcept
	{
		if (!PlanarMipChainPolicy::IsCompleteMinificationChain(
				textureWidth, textureHeight, textureMips))
			return false;
		if (firstMip == 0)
			return textureWidth == frameWidth && textureHeight == frameHeight &&
				viewMips == textureMips;
		return reducedHandAllowed && firstMip >= 1 && firstMip <= kMaximumReducedMips &&
			textureWidth == textureHeight && IsSupportedResolution(textureWidth) &&
			frameWidth == (textureWidth >> firstMip) && frameHeight == frameWidth &&
			frameWidth >= kMinimumResolution && viewMips == textureMips - firstMip;
	}

	[[nodiscard]] constexpr bool DepthViewDimensionsMatch(
		std::uint32_t textureWidth, std::uint32_t textureHeight,
		std::uint32_t textureMips, std::uint32_t firstMip, std::uint32_t viewMips,
		std::uint32_t frameWidth, std::uint32_t frameHeight,
		bool reducedHandAllowed) noexcept
	{
		if (viewMips != 1)
			return false;
		if (textureMips == 1 && firstMip == 0)
			return textureWidth == frameWidth && textureHeight == frameHeight;
		return reducedHandAllowed && textureMips >= 2 &&
			textureMips <= 1 + kMaximumReducedMips && firstMip < textureMips &&
			textureWidth == textureHeight && IsSupportedResolution(textureWidth) &&
			frameWidth == (textureWidth >> firstMip) && frameHeight == frameWidth &&
			frameWidth >= kMinimumResolution;
	}

	/** The reflected world has different motion from the main scene under the pane. */
	[[nodiscard]] constexpr bool RejectNativeTemporalHistory(
		const bool livePoseEnabled,
		const bool outerFinal,
		const bool raised) noexcept
	{
		return outerFinal && (raised || livePoseEnabled);
	}

	/** The input-intent bit can stay set after block animation/menu release. */
	[[nodiscard]] constexpr bool RaisedPresentation(
		const bool livePoseEnabled,
		const bool animatedBlocking,
		const bool wantsBlocking,
		const bool weaponDrawn) noexcept
	{
		return animatedBlocking ||
			(!livePoseEnabled && wantsBlocking && weaponDrawn);
	}

	/** A physical mirror can face a room which does not contain the player. */
	[[nodiscard]] constexpr bool PlayerEvidenceAccepted(
		const bool livePoseEnabled,
		const bool raised,
		const bool exactTargetSubmissionAndCleanup,
		const bool exactTargetSkinnedDraw) noexcept
	{
		return exactTargetSubmissionAndCleanup &&
			(exactTargetSkinnedDraw || (livePoseEnabled && !raised));
	}

	/**
	 * Scale applied to the frozen source frustum before it is united with the
	 * pane fit for a lowered hand capture.  1.35 keeps full aperture coverage
	 * through roughly ten degrees of camera yaw between the calibrating frame
	 * and the presenting frame, independently of the capture resolution.
	 */
	inline constexpr float kSourceFrustumOverscan = 1.35F;

	/**
	 * Maximum age (in main-world source sequences) of a cached last-good frame
	 * that a lowered pane may still replay.  Twelve sources is about 0.2 s at
	 * 60 Hz: long enough to bridge the ordinary one-frame calibration lag and a
	 * short projection miss, short enough that a lowered mirror never shows an
	 * old image the owner can perceive as frozen.
	 */
	inline constexpr std::uint64_t kLoweredLastGoodMaxSourceAge = 12;

	[[nodiscard]] constexpr float OverscanFrustumEdge(
		const float low,
		const float high,
		const float overscan,
		const bool wantHigh) noexcept
	{
		const float centre = 0.5F * (low + high);
		const float half = 0.5F * (high - low) * overscan;
		return wantHigh ? centre + half : centre - half;
	}

	/** True when a lowered pane must drop its cached frame because it is too old. */
	[[nodiscard]] constexpr bool LoweredLastGoodStale(
		const bool lowered,
		const std::uint64_t committedSourceSequence,
		const std::uint64_t currentSourceSequence) noexcept
	{
		if (!lowered || committedSourceSequence == 0 || currentSourceSequence == 0)
			return false;
		if (currentSourceSequence <= committedSourceSequence)
			return false;
		return currentSourceSequence - committedSourceSequence >
		       kLoweredLastGoodMaxSourceAge;
	}

	/**
	 * Hand/wall channel arbitration input.  The single planar capture channel
	 * goes to the hand whenever the hand pane is visible: raised it shows the
	 * portrait, lowered it shows the physical reflection of whatever faces it
	 * (owner 2026-09-05: "when the mirror is not raised it should still reflect
	 * what is opposite to it").  An eligible standing/wall mirror interleaves
	 * with the hand source by source, as it already did for a raised pane; it
	 * has the channel to itself only while no hand pane is visible.  Until the
	 * multi-mirror plan lands this is the one-channel compromise.
	 */
	/**
	 * Sources a lowered hand keeps yielding after the last frame a wall
	 * candidate was eligible.  Wall eligibility is re-evaluated every source
	 * and dips for a frame around each grant, so without this hold the hand
	 * re-acquires between wall captures and neither owner presents.
	 */
	inline constexpr std::uint64_t kWallYieldHoldSources = 30;

	[[nodiscard]] constexpr bool HandEligibleAgainstWall(
		const bool handPaneVisible,
		const bool /*handRaised*/,
		const bool /*wallEligible*/) noexcept
	{
		return handPaneVisible;
	}

	static_assert(kSourceFrustumOverscan > 1.0F && kSourceFrustumOverscan < 2.0F);
	static_assert(OverscanFrustumEdge(-1.0F, 1.0F, 1.5F, true) == 1.5F);
	static_assert(OverscanFrustumEdge(-1.0F, 1.0F, 1.5F, false) == -1.5F);
	static_assert(OverscanFrustumEdge(0.0F, 2.0F, 2.0F, true) == 3.0F);
	static_assert(OverscanFrustumEdge(0.0F, 2.0F, 2.0F, false) == -1.0F);
	static_assert(!LoweredLastGoodStale(false, 1, 1000));
	static_assert(!LoweredLastGoodStale(true, 0, 1000));
	static_assert(!LoweredLastGoodStale(true, 100, 100));
	static_assert(!LoweredLastGoodStale(true, 100, 100 + kLoweredLastGoodMaxSourceAge));
	static_assert(LoweredLastGoodStale(true, 100, 101 + kLoweredLastGoodMaxSourceAge));
	static_assert(!LoweredLastGoodStale(true, 200, 100));
	static_assert(HandEligibleAgainstWall(true, true, true));
	static_assert(HandEligibleAgainstWall(true, false, false));
	static_assert(HandEligibleAgainstWall(true, false, true));
	static_assert(!HandEligibleAgainstWall(false, true, false));
	static_assert(!HandEligibleAgainstWall(false, false, true));
	static_assert(IsSupportedResolution(512) && IsSupportedResolution(4096) && !IsSupportedResolution(1536) &&
		!IsSupportedResolution(256) && !IsSupportedResolution(8192));
	static_assert(MipLevelsFor(4096) == 13 && MipLevelsFor(2048) == 12 && MipLevelsFor(512) == 10);
	static_assert(TargetDimension(false, 512, 512) == 2048 && TargetDimension(true, 512, 4096) == 4096 &&
		TargetDimension(true, 1024, 512) == 1024 && TargetDimension(true, 1536, 512) == 2048);
	static_assert(ReducedViewCount(true, 4096, 512) == 3 && ReducedViewCount(true, 512, 512) == 0 &&
		ReducedViewCount(true) == 2 && ReducedViewCount(false, 4096, 4096) == 0);
	static_assert(CaptureMip(true, true, 4096, 512) == 0 && CaptureMip(false, true, 4096, 512) == 3 &&
		CaptureMip(true, true, 512, 2048) == 2 && CaptureMip(false, true, 512, 2048) == 0 &&
		CaptureMip(false, true) == 1 && CaptureMip(true, true) == 0 && CaptureMip(false, false, 512, 512) == 0);
	static_assert(CaptureDimensionsAllowed(512, 512, true) && CaptureDimensionsAllowed(4096, 4096, true) &&
		!CaptureDimensionsAllowed(512, 512, false) && CaptureDimensionsAllowed(2048, 2048, false));
	static_assert(ColorViewDimensionsMatch(4096, 4096, 13, 3, 10, 512, 512, true) &&
		!ColorViewDimensionsMatch(4096, 4096, 13, 4, 9, 256, 256, true) &&
		DepthViewDimensionsMatch(4096, 4096, 4, 3, 1, 512, 512, true) &&
		!DepthViewDimensionsMatch(4096, 4096, 4, 1, 1, 1024, 1024, false));
}
