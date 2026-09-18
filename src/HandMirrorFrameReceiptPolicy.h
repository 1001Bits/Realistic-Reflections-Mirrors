#pragma once

#include <cstdint>
#include <limits>

namespace HandMirrorFrameReceiptPolicy
{
	/**
	 * Value receipt for one exact source/main-view/M5 graphics-frame cohort.
	 * The source and main-view serials never wrap; the engine graphics counter can,
	 * so the boundary values are deliberately not admissible evidence.
	 */
	struct FrameReceipt
	{
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		std::uint32_t graphicsFrame{ 0 };

		constexpr bool operator==(const FrameReceipt&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidFrameReceipt(
		const FrameReceipt& receipt) noexcept
	{
		return receipt.sourceSequence != 0 &&
		       receipt.sourceSequence !=
			       (std::numeric_limits<std::uint64_t>::max)() &&
		       receipt.mainViewFrame != 0 &&
		       receipt.mainViewFrame !=
			       (std::numeric_limits<std::uint64_t>::max)() &&
		       receipt.graphicsFrame != 0 &&
		       receipt.graphicsFrame !=
			       (std::numeric_limits<std::uint32_t>::max)();
	}

	struct HiddenNodeFrames
	{
		std::uint32_t root{ 0 };
		std::uint32_t clone{ 0 };
		std::uint32_t item{ 0 };
		std::uint32_t pane{ 0 };

		constexpr bool operator==(const HiddenNodeFrames&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsCoherentHiddenNodeFrames(
		const HiddenNodeFrames& frames) noexcept
	{
		return frames.root != 0 &&
		       frames.root != (std::numeric_limits<std::uint32_t>::max)() &&
		       frames.clone == frames.root && frames.item == frames.root &&
		       frames.pane == frames.root;
	}

	/** Every value which can change the meaning of an observed hidden clone. */
	struct ContinuityIdentity
	{
		std::uint64_t lifecycleGeneration{ 0 };
		std::uint64_t equipGeneration{ 0 };
		std::uint64_t firstPersonRootGeneration{ 0 };
		std::uint64_t thirdPersonRootGeneration{ 0 };
		std::uint64_t firstPersonCloneGeneration{ 0 };
		std::uint64_t thirdPersonCloneGeneration{ 0 };
		std::uint64_t playerIdentity{ 0 };
		std::uint64_t armorIdentity{ 0 };
		std::uint64_t armorAddonIdentity{ 0 };
		std::uint64_t hiddenRootIdentity{ 0 };
		std::uint64_t hiddenCloneIdentity{ 0 };
		std::uint64_t hiddenItemIdentity{ 0 };
		std::uint64_t hiddenPaneIdentity{ 0 };

		constexpr bool operator==(const ContinuityIdentity&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsValidContinuityIdentity(
		const ContinuityIdentity& identity) noexcept
	{
		return identity.lifecycleGeneration != 0 &&
		       identity.equipGeneration != 0 &&
		       identity.firstPersonRootGeneration != 0 &&
		       identity.thirdPersonRootGeneration != 0 &&
		       identity.firstPersonCloneGeneration != 0 &&
		       identity.thirdPersonCloneGeneration != 0 &&
		       identity.playerIdentity != 0 && identity.armorIdentity != 0 &&
		       identity.armorAddonIdentity != 0 &&
		       identity.hiddenRootIdentity != 0 &&
		       identity.hiddenCloneIdentity != 0 &&
		       identity.hiddenItemIdentity != 0 &&
		       identity.hiddenPaneIdentity != 0;
	}

	enum class RuntimeKind : std::uint8_t
	{
		kUnsupported,
		kSkyrimSE1597,
		kSkyrimAE161170,
		kSkyrimVR1415
	};

	[[nodiscard]] constexpr bool AllowsCalibratedPredecessor(
		const RuntimeKind runtime) noexcept
	{
		return runtime == RuntimeKind::kSkyrimSE1597 ||
		       runtime == RuntimeKind::kSkyrimAE161170 ||
		       runtime == RuntimeKind::kSkyrimVR1415;
	}

	enum class FreshnessStatus : std::uint8_t
	{
		kRejectedInvalid,
		kRejectedUnsupportedPredecessor,
		kDirectCurrent,
		kPredecessorSeedDark,
		kPredecessorSeedRereadDark,
		kPredecessorCalibrated,
		kPredecessorCalibratedReread,
		kPredecessorResetDark
	};

	struct FreshnessState
	{
		FrameReceipt receipt{};
		HiddenNodeFrames hidden{};
		ContinuityIdentity identity{};
		bool hasPredecessorSeed{ false };
		bool predecessorCalibrated{ false };

		constexpr bool operator==(const FreshnessState&) const noexcept = default;
	};

	struct FreshnessDecision
	{
		FreshnessState next{};
		FreshnessStatus status{ FreshnessStatus::kRejectedInvalid };
		bool fresh{ false };
		bool sameReceiptReread{ false };
	};

	/**
	 * Immutable proof retained by one accepted runtime snapshot.  Suppression
	 * rereads consume this value without advancing the two-source calibrator.
	 */
	struct FreshnessEvidence
	{
		FrameReceipt receipt{};
		HiddenNodeFrames hidden{};
		ContinuityIdentity identity{};
		FreshnessStatus status{ FreshnessStatus::kRejectedInvalid };

		constexpr bool operator==(const FreshnessEvidence&) const noexcept = default;
	};

	struct FreshnessReread
	{
		FrameReceipt receipt{};
		HiddenNodeFrames hidden{};
		ContinuityIdentity identity{};
	};

	template <class Integer>
	[[nodiscard]] constexpr bool IsStrictSuccessor(
		const Integer predecessor, const Integer successor) noexcept
	{
		return predecessor != 0 &&
		       predecessor != (std::numeric_limits<Integer>::max)() &&
		       successor == static_cast<Integer>(predecessor + 1);
	}

	/** Sources a calibrated predecessor may be behind: 1 (every source) or 2 (interleaved with a wall capture). */
	inline constexpr std::uint64_t kMaximumCalibrationStride = 2;

	/** 0 when `successor` is not a bounded strict successor of `predecessor`; the stride otherwise. */
	template <class Integer>
	[[nodiscard]] constexpr std::uint64_t SuccessorStride(
		const Integer predecessor, const Integer successor) noexcept
	{
		if (predecessor == 0 ||
			predecessor == (std::numeric_limits<Integer>::max)() ||
			successor <= predecessor) {
			return 0;
		}
		const auto stride = static_cast<std::uint64_t>(successor - predecessor);
		return stride <= kMaximumCalibrationStride ? stride : 0;
	}

	[[nodiscard]] constexpr FreshnessState MakePredecessorSeed(
		const FrameReceipt& receipt,
		const HiddenNodeFrames& hidden,
		const ContinuityIdentity& identity,
		const bool calibrated = false) noexcept
	{
		return { receipt, hidden, identity, true, calibrated };
	}

	/**
	 * The exact flat runtimes may calibrate the observed H == G - 1 convention. A
	 * predecessor seed is never fresh until a distinct adjacent S/M/G/H cohort
	 * arrives. Exact rereads of that cohort preserve, but cannot create,
	 * calibration. Every discontinuity replaces it with a new dark seed.
	 */
	[[nodiscard]] constexpr FreshnessDecision EvaluateHiddenFreshness(
		const FreshnessState& prior,
		const RuntimeKind runtime,
		const FrameReceipt& receipt,
		const HiddenNodeFrames& hidden,
		const ContinuityIdentity& identity) noexcept
	{
		if (!IsValidFrameReceipt(receipt) ||
			!IsCoherentHiddenNodeFrames(hidden) ||
			!IsValidContinuityIdentity(identity)) {
			return {};
		}

		if (hidden.root == receipt.graphicsFrame) {
			return { {}, FreshnessStatus::kDirectCurrent, true, false };
		}

		const bool predecessor = receipt.graphicsFrame > 1 &&
			hidden.root == receipt.graphicsFrame - 1;
		if (!predecessor || !AllowsCalibratedPredecessor(runtime)) {
			return { {}, FreshnessStatus::kRejectedUnsupportedPredecessor,
				false, false };
		}

		const auto seedDark = [&](const FreshnessStatus status) constexpr {
			return FreshnessDecision{
				MakePredecessorSeed(receipt, hidden, identity), status, false,
				false };
		};
		if (!prior.hasPredecessorSeed)
			return seedDark(FreshnessStatus::kPredecessorSeedDark);

		if (prior.receipt == receipt) {
			if (prior.hidden != hidden || prior.identity != identity)
				return seedDark(FreshnessStatus::kPredecessorResetDark);
			return { prior,
				prior.predecessorCalibrated ?
					FreshnessStatus::kPredecessorCalibratedReread :
					FreshnessStatus::kPredecessorSeedRereadDark,
				prior.predecessorCalibrated, true };
		}

		// V156: a hand/wall interleave gives the hand every second source.  The
		// hidden capture clone keeps advancing through the wall's source, so a
		// uniform stride of two across S/M/G and every hidden node frame is the
		// same calibrated predecessor chain as a stride of one (owner run
		// 2026-09-04 19:06: every interleaved hand source reset to a dark seed
		// and no hand capture ever calibrated).
		const auto sourceStride = SuccessorStride(
			prior.receipt.sourceSequence, receipt.sourceSequence);
		const bool adjacent = prior.identity == identity &&
			sourceStride != 0 && sourceStride <= kMaximumCalibrationStride &&
			SuccessorStride(prior.receipt.mainViewFrame, receipt.mainViewFrame) ==
				sourceStride &&
			SuccessorStride(prior.receipt.graphicsFrame, receipt.graphicsFrame) ==
				sourceStride &&
			SuccessorStride(prior.hidden.root, hidden.root) == sourceStride &&
			SuccessorStride(prior.hidden.clone, hidden.clone) == sourceStride &&
			SuccessorStride(prior.hidden.item, hidden.item) == sourceStride &&
			SuccessorStride(prior.hidden.pane, hidden.pane) == sourceStride;
		if (!adjacent)
			return seedDark(FreshnessStatus::kPredecessorResetDark);

		return { MakePredecessorSeed(receipt, hidden, identity, true),
			FreshnessStatus::kPredecessorCalibrated, true, false };
	}

	[[nodiscard]] constexpr FreshnessEvidence MakeFreshnessEvidence(
		const FrameReceipt& receipt,
		const HiddenNodeFrames& hidden,
		const ContinuityIdentity& identity,
		const FreshnessDecision& decision) noexcept
	{
		return { receipt, hidden, identity, decision.status };
	}

	/**
	 * Stateless, exact reread of an already accepted receipt.  Direct-current
	 * remains valid everywhere.  A flat-runtime predecessor is accepted only
	 * when the retained snapshot was itself produced by calibrated predecessor
	 * evidence.  A same-receipt reread can preserve that proof but never create it.
	 */
	[[nodiscard]] constexpr bool IsExactSameReceiptFreshReread(
		const RuntimeKind runtime,
		const FreshnessEvidence& accepted,
		const FreshnessReread& current) noexcept
	{
		if (!IsValidFrameReceipt(accepted.receipt) ||
			!IsCoherentHiddenNodeFrames(accepted.hidden) ||
			!IsValidContinuityIdentity(accepted.identity) ||
			!IsValidFrameReceipt(current.receipt) ||
			!IsCoherentHiddenNodeFrames(current.hidden) ||
			!IsValidContinuityIdentity(current.identity) ||
			current.receipt != accepted.receipt ||
			current.hidden != accepted.hidden ||
			current.identity != accepted.identity) {
			return false;
		}

		if (accepted.status == FreshnessStatus::kDirectCurrent)
			return accepted.hidden.root == accepted.receipt.graphicsFrame;

		const bool calibratedPredecessor =
			accepted.status == FreshnessStatus::kPredecessorCalibrated ||
			accepted.status == FreshnessStatus::kPredecessorCalibratedReread;
		return calibratedPredecessor && AllowsCalibratedPredecessor(runtime) &&
		       accepted.receipt.graphicsFrame > 1 &&
		       accepted.hidden.root == accepted.receipt.graphicsFrame - 1;
	}
}
