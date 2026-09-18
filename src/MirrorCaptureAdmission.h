#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace MirrorCaptureAdmission
{
	struct Identity
	{
		std::uint32_t formID{ 0 };
		std::uint64_t generation{ 0 };
		std::uintptr_t cellAddress{ 0 };
		std::uint32_t cellFormID{ 0 };
		std::uintptr_t bodyRootAddress{ 0 };

		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return formID != 0 && generation != 0 && cellAddress != 0 &&
			       cellFormID != 0 && bodyRootAddress != 0;
		}

		[[nodiscard]] friend constexpr bool operator==(
			const Identity&, const Identity&) noexcept = default;
	};

	struct Policy
	{
		// Reject only genuinely degenerate edge-on views. This was 0.25 (about 76
		// degrees off-normal), chosen when the capture reused the player's field of
		// view and oblique angles produced broken images. The capture frustum is
		// now fitted to the pane, so oblique views render correctly and 0.25 was
		// just refusing to reflect - one of the gates behind the 2026-08-07
		// "the mirror keeps going white" reports. A failed oblique projection
		// still fails closed on its own, one frame at a time.
		float minimumFacingCosine{ 0.05f };
		// Enough to reject a single-frame transient without making the mirror feel
		// dead for a quarter of a second after every small movement.
		std::uint32_t minimumStableSamples{ 3 };
		std::int64_t minimumStableMilliseconds{ 80 };
		std::int64_t maximumSampleGapMilliseconds{ 125 };
		std::uint64_t maximumSequenceGap{ 1 };
	};

	struct Sample
	{
		Identity identity{};
		std::uint64_t sequence{ 0 };
		std::int64_t nowMilliseconds{ 0 };
		float facingCosine{ 0.0f };
		float sourcePlaneDistance{ 0.0f };
		float requiredSourceDistance{ 0.0f };
		float playerPlaneDistance{ 0.0f };
		float bodyNearestPlaneDistance{ 0.0f };
		float requiredPlayerDistance{ 0.0f };
	};

	enum class Rejection : std::uint8_t
	{
		kNone,
		kInvalidInput,
		kFacing,
		kSourceClearance,
		kPlayerClearance,
		kStabilizing
	};

	struct Result
	{
		Rejection rejection{ Rejection::kInvalidInput };
		std::uint32_t stableSamples{ 0 };
		std::int64_t stableMilliseconds{ 0 };
		bool accepted{ false };
		bool newlyAccepted{ false };
	};

	class Gate
	{
	public:
		void Reset() noexcept
		{
			state_ = {};
		}

		[[nodiscard]] Result Evaluate(
			const Sample& sample,
			const Policy& policy = {}) noexcept
		{
			Result result{};
			if (!Valid(sample, policy)) {
				Reset();
				return result;
			}

			if (sample.facingCosine < policy.minimumFacingCosine) {
				Reset();
				result.rejection = Rejection::kFacing;
				return result;
			}
			if (!(sample.sourcePlaneDistance > sample.requiredSourceDistance)) {
				Reset();
				result.rejection = Rejection::kSourceClearance;
				return result;
			}
			// Only the player ORIGIN gates. `bodyNearestPlaneDistance` is derived
			// from the body's bounding SPHERE (~78 units, arms and weapons
			// included), so requiring it to clear the plane meant standing at a
			// normal mirror distance was permanently rejected — the 2026-08-07
			// "reflection freezes when I get close" report, 838 rejects in one
			// session. It is not a safety requirement either: the reflected
			// camera sits behind the mirror, so a body in front of the pane is
			// already in the retained half-space and at worst loses a sliver to
			// the clip bias. The field stays in the sample for telemetry.
			if (!(sample.playerPlaneDistance > sample.requiredPlayerDistance)) {
				Reset();
				result.rejection = Rejection::kPlayerClearance;
				return result;
			}

			// The driver evaluates the same source more than once (fence tail and
			// the deferred exact-main stage share one driver call).  A repeated
			// sequence is the same sample, not a gap: treating it as a
			// discontinuity restarted the warm-up after every capture, so a placed
			// mirror captured only every sixth frame (owner run 2026-09-04 16:21:
			// 2,720 stabilizing rejects against 555 accepts).
			const bool sameSample = state_.tracking &&
				state_.identity == sample.identity &&
				sample.sequence == state_.lastSequence &&
				sample.nowMilliseconds >= state_.lastMilliseconds &&
				sample.nowMilliseconds - state_.lastMilliseconds <=
					policy.maximumSampleGapMilliseconds;
			const bool discontinuity = !sameSample && (!state_.tracking ||
				state_.identity != sample.identity ||
				state_.lastSequence == std::numeric_limits<std::uint64_t>::max() ||
				sample.sequence <= state_.lastSequence ||
				sample.sequence - state_.lastSequence > policy.maximumSequenceGap ||
				sample.nowMilliseconds < state_.lastMilliseconds ||
				sample.nowMilliseconds - state_.lastMilliseconds >
					policy.maximumSampleGapMilliseconds);
			if (sameSample) {
				state_.lastMilliseconds = sample.nowMilliseconds;
			} else if (discontinuity) {
				state_.tracking = true;
				state_.identity = sample.identity;
				state_.firstMilliseconds = sample.nowMilliseconds;
				state_.lastMilliseconds = sample.nowMilliseconds;
				state_.lastSequence = sample.sequence;
				state_.stableSamples = 1;
				state_.accepted = false;
			} else {
				state_.lastMilliseconds = sample.nowMilliseconds;
				state_.lastSequence = sample.sequence;
				if (state_.stableSamples != std::numeric_limits<std::uint32_t>::max())
					++state_.stableSamples;
			}

			result.stableSamples = state_.stableSamples;
			result.stableMilliseconds =
				sample.nowMilliseconds - state_.firstMilliseconds;
			if (!state_.accepted &&
				state_.stableSamples >= policy.minimumStableSamples &&
				result.stableMilliseconds >= policy.minimumStableMilliseconds) {
				state_.accepted = true;
				result.newlyAccepted = true;
			}
			result.accepted = state_.accepted;
			result.rejection =
				result.accepted ? Rejection::kNone : Rejection::kStabilizing;
			return result;
		}

		/** Recheck the already admitted sample in the same driver invocation. */
		[[nodiscard]] Result Revalidate(
			const Sample& sample,
			const Policy& policy = {}) noexcept
		{
			Result result{};
			if (!Valid(sample, policy)) {
				Reset();
				return result;
			}
			if (sample.facingCosine < policy.minimumFacingCosine) {
				Reset();
				result.rejection = Rejection::kFacing;
				return result;
			}
			if (!(sample.sourcePlaneDistance > sample.requiredSourceDistance)) {
				Reset();
				result.rejection = Rejection::kSourceClearance;
				return result;
			}
			// Only the player ORIGIN gates. `bodyNearestPlaneDistance` is derived
			// from the body's bounding SPHERE (~78 units, arms and weapons
			// included), so requiring it to clear the plane meant standing at a
			// normal mirror distance was permanently rejected — the 2026-08-07
			// "reflection freezes when I get close" report, 838 rejects in one
			// session. It is not a safety requirement either: the reflected
			// camera sits behind the mirror, so a body in front of the pane is
			// already in the retained half-space and at worst loses a sliver to
			// the clip bias. The field stays in the sample for telemetry.
			if (!(sample.playerPlaneDistance > sample.requiredPlayerDistance)) {
				Reset();
				result.rejection = Rejection::kPlayerClearance;
				return result;
			}

			if (!state_.tracking || !state_.accepted ||
				state_.identity != sample.identity ||
				sample.sequence != state_.lastSequence ||
				sample.nowMilliseconds < state_.lastMilliseconds ||
				sample.nowMilliseconds - state_.lastMilliseconds >
					policy.maximumSampleGapMilliseconds) {
				Reset();
				result.rejection = Rejection::kStabilizing;
				return result;
			}

			result.rejection = Rejection::kNone;
			result.stableSamples = state_.stableSamples;
			result.stableMilliseconds =
				sample.nowMilliseconds - state_.firstMilliseconds;
			result.accepted = true;
			return result;
		}

	private:
		struct State
		{
			Identity identity{};
			std::int64_t firstMilliseconds{ 0 };
			std::int64_t lastMilliseconds{ 0 };
			std::uint64_t lastSequence{ 0 };
			std::uint32_t stableSamples{ 0 };
			bool tracking{ false };
			bool accepted{ false };
		};

		[[nodiscard]] static bool Finite(float value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] static bool Valid(
			const Sample& sample,
			const Policy& policy) noexcept
		{
			return sample.identity.Valid() && sample.sequence != 0 &&
				sample.nowMilliseconds >= 0 &&
				Finite(sample.facingCosine) && sample.facingCosine >= 0.0f &&
				sample.facingCosine <= 1.0001f &&
				Finite(sample.sourcePlaneDistance) &&
				Finite(sample.requiredSourceDistance) &&
				Finite(sample.playerPlaneDistance) &&
				Finite(sample.bodyNearestPlaneDistance) &&
				Finite(sample.requiredPlayerDistance) &&
				sample.requiredSourceDistance >= 0.0f &&
				sample.requiredPlayerDistance >= 0.0f &&
				Finite(policy.minimumFacingCosine) &&
				policy.minimumFacingCosine >= 0.0f &&
				policy.minimumFacingCosine <= 1.0f &&
				policy.minimumStableSamples > 0 &&
				policy.minimumStableMilliseconds >= 0 &&
				policy.maximumSampleGapMilliseconds >= 0 && policy.maximumSequenceGap > 0;
		}

		State state_{};
	};
}
