#pragma once

#include <cstdint>

namespace HandMirrorExactPanePassCohortPolicy
{
	inline constexpr std::uint8_t kMaximumCoalescedNormalReturns = 2;
	inline constexpr std::uint8_t kOverflowNormalReturnCount =
		kMaximumCoalescedNormalReturns + 1;
	static_assert(kOverflowNormalReturnCount == 3);

	/**
	 * Exact equality proof for a later normal return against the value snapshot
	 * staged by the preceding accepted return in the same outer first-person
	 * call.
	 *
	 * Receipt, owner, stable surface, device, and context are immutable cohort
	 * identity. Retained resources, geometry, and presentation are pass-local
	 * values: Skyrim may legally change them between two submissions, while each
	 * submission must independently prove its own target exact through return.
	 * Callers still compare every field exactly so diagnostics can partition
	 * immutable rejection from allowed last-return-authoritative drift.
	 */
	struct ValueIdentityEquality
	{
		bool receiptSourceSequenceEqual{ false };  // S
		bool receiptMainViewFrameEqual{ false };    // M
		bool receiptGraphicsFrameEqual{ false };    // M5-G
		bool ownerIdentityEqual{ false };
		bool stableGenerationEqual{ false };
		bool surfaceIdentityEqual{ false };
		bool geometryIdentityEqual{ false };
		bool deviceIdentityEqual{ false };
		bool contextIdentityEqual{ false };
		bool retainedColorResourceIdentityEqual{ false };
		bool retainedDepthResourceIdentityEqual{ false };
		bool viewBitsEqual{ false };
		bool projectionBitsEqual{ false };
		bool originBitsEqual{ false };
		bool viewportBitsEqual{ false };

		constexpr bool operator==(const ValueIdentityEquality&) const noexcept =
			default;
	};

	[[nodiscard]] constexpr bool IsBitExactValueIdentity(
		const ValueIdentityEquality& equality) noexcept
	{
		return equality.receiptSourceSequenceEqual &&
		       equality.receiptMainViewFrameEqual &&
		       equality.receiptGraphicsFrameEqual &&
		       equality.ownerIdentityEqual &&
		       equality.stableGenerationEqual &&
		       equality.surfaceIdentityEqual &&
		       equality.geometryIdentityEqual &&
		       equality.deviceIdentityEqual &&
		       equality.contextIdentityEqual &&
		       equality.retainedColorResourceIdentityEqual &&
		       equality.retainedDepthResourceIdentityEqual &&
		       equality.viewBitsEqual && equality.projectionBitsEqual &&
		       equality.originBitsEqual && equality.viewportBitsEqual;
	}

	/** Immutable identity that permits two exact pane submissions to coalesce. */
	[[nodiscard]] constexpr bool IsSameImmutableCohortIdentity(
		const ValueIdentityEquality& equality) noexcept
	{
		return equality.receiptSourceSequenceEqual &&
		       equality.receiptMainViewFrameEqual &&
		       equality.receiptGraphicsFrameEqual &&
		       equality.ownerIdentityEqual &&
		       equality.stableGenerationEqual &&
		       equality.surfaceIdentityEqual &&
		       equality.deviceIdentityEqual &&
		       equality.contextIdentityEqual;
	}

	/**
	 * Classification supplied by the caller for evidence that cannot join the
	 * exact cohort.  Ordinary, coherently observed absence/drift darkens only
	 * this outer call.  Native/query/invariant faults are explicitly terminal.
	 * A caller that observes such a fault must select kTerminalDark; omitting
	 * the observation is outside this value-only policy's authority.
	 */
	enum class DivergenceClass : std::uint8_t
	{
		kCoherentDark,
		kTerminalDark
	};

	struct PassReturnInput
	{
		bool normalReturnProven{ false };
		bool valueSnapshotComplete{ false };
		ValueIdentityEquality equalityToStagedFirst{};
		DivergenceClass divergenceClass{ DivergenceClass::kCoherentDark };

		constexpr bool operator==(const PassReturnInput&) const noexcept = default;
	};

	enum class CohortPhase : std::uint8_t
	{
		kOpen,
		kFirstStaged,
		kSecondCoalesced,
		kCoherentDark,
		kTerminalDark,
		kClosedDelivered,
		kClosedCoherentDark,
		kClosedTerminalDark
	};

	struct CohortState
	{
		// Default-construct at outer entry and thereafter use only nextState from
		// this policy.  The engine-owning caller must independently retain and
		// cross-check the staged value/resource snapshot before acting on Close.
		CohortPhase phase{ CohortPhase::kOpen };
		std::uint8_t normalReturnCount{ 0 };

		constexpr bool operator==(const CohortState&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidCohortState(
		const CohortState state) noexcept
	{
		if (state.normalReturnCount > kOverflowNormalReturnCount)
			return false;

		switch (state.phase) {
		case CohortPhase::kOpen:
			return state.normalReturnCount == 0;
		case CohortPhase::kFirstStaged:
			return state.normalReturnCount == 1;
		case CohortPhase::kSecondCoalesced:
			return state.normalReturnCount == 2;
		case CohortPhase::kCoherentDark:
			return state.normalReturnCount >= 1;
		case CohortPhase::kTerminalDark:
			return true;
		case CohortPhase::kClosedDelivered:
			return state.normalReturnCount == 1 ||
			       state.normalReturnCount == 2;
		case CohortPhase::kClosedCoherentDark:
		case CohortPhase::kClosedTerminalDark:
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr bool IsKnownDivergenceClass(
		const DivergenceClass value) noexcept
	{
		return value == DivergenceClass::kCoherentDark ||
		       value == DivergenceClass::kTerminalDark;
	}

	[[nodiscard]] constexpr std::uint8_t CountNormalReturnBounded(
		const std::uint8_t count) noexcept
	{
		return count >= kOverflowNormalReturnCount ?
			kOverflowNormalReturnCount : static_cast<std::uint8_t>(count + 1);
	}

	enum class ReturnAction : std::uint8_t
	{
		kStageFirst,
		kReplaceWithLatestCoalescedWithoutVisibilityUpdateOrDraw,
		kCoherentDark,
		kTerminalDark
	};

	struct ReturnTransition
	{
		CohortState nextState{ CohortPhase::kTerminalDark, 0 };
		ReturnAction action{ ReturnAction::kTerminalDark };
		std::uint8_t normalReturnsBefore{ 0 };

		constexpr bool operator==(const ReturnTransition&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool AllowsReturnTimeVisibilityUpdateOrDraw(
		const ReturnTransition&) noexcept
	{
		// The first return stages values and an optional immutable-compatible second
		// replaces it. The one visible opportunity still belongs to outer close.
		return false;
	}

	[[nodiscard]] constexpr ReturnTransition MakeTerminalTransition(
		const std::uint8_t returnsBefore,
		const bool countNormalReturn) noexcept
	{
		const auto boundedBefore = returnsBefore > kOverflowNormalReturnCount ?
			kOverflowNormalReturnCount : returnsBefore;
		return {
			{ CohortPhase::kTerminalDark,
				countNormalReturn ? CountNormalReturnBounded(boundedBefore) :
					boundedBefore },
			ReturnAction::kTerminalDark,
			boundedBefore
		};
	}

	/**
	 * Observe one returned exact-pane candidate.  normalReturnsBefore is copied
	 * into the result so runtime code can select count-before actions without a
	 * second counter.  No return-time transition grants visibility or a draw.
	 */
	[[nodiscard]] constexpr ReturnTransition ObserveReturn(
		const CohortState state,
		const PassReturnInput& input) noexcept
	{
		const auto boundedBefore =
			state.normalReturnCount > kOverflowNormalReturnCount ?
				kOverflowNormalReturnCount : state.normalReturnCount;
		if (!IsValidCohortState(state) ||
			!IsKnownDivergenceClass(input.divergenceClass)) {
			return MakeTerminalTransition(
				boundedBefore, input.normalReturnProven);
		}

		switch (state.phase) {
		case CohortPhase::kClosedDelivered:
		case CohortPhase::kClosedCoherentDark:
		case CohortPhase::kClosedTerminalDark:
			return MakeTerminalTransition(
				boundedBefore, input.normalReturnProven);
		case CohortPhase::kTerminalDark:
			return { state, ReturnAction::kTerminalDark, boundedBefore };
		default:
			break;
		}

		if (!input.normalReturnProven) {
			// An entered native pane call without a normal-return proof is never a
			// coherent per-frame miss.
			return MakeTerminalTransition(boundedBefore, false);
		}

		const auto counted = CountNormalReturnBounded(boundedBefore);
		if (input.divergenceClass == DivergenceClass::kTerminalDark) {
			return {
				{ CohortPhase::kTerminalDark, counted },
				ReturnAction::kTerminalDark,
				boundedBefore
			};
		}

		if (state.phase == CohortPhase::kCoherentDark) {
			return {
				{ CohortPhase::kCoherentDark, counted },
				ReturnAction::kCoherentDark,
				boundedBefore
			};
		}

		if (!input.valueSnapshotComplete) {
			return {
				{ CohortPhase::kCoherentDark, counted },
				ReturnAction::kCoherentDark,
				boundedBefore
			};
		}

		if (boundedBefore == 0) {
			return {
				{ CohortPhase::kFirstStaged, 1 },
				ReturnAction::kStageFirst,
				0
			};
		}

		if (boundedBefore == 1 &&
			state.phase == CohortPhase::kFirstStaged) {
			if (IsSameImmutableCohortIdentity(
					input.equalityToStagedFirst)) {
				return {
					{ CohortPhase::kSecondCoalesced, 2 },
					ReturnAction::
						kReplaceWithLatestCoalescedWithoutVisibilityUpdateOrDraw,
					1
				};
			}
			return {
				{ CohortPhase::kCoherentDark, 2 },
				ReturnAction::kCoherentDark,
				1
			};
		}

		// A third or later normal return is a coherent dark cohort even when it
		// is bit-identical to the staged first return.  The count saturates at the
		// first over-capacity value and can never wrap back into admission.
		return {
			{ CohortPhase::kCoherentDark, counted },
			ReturnAction::kCoherentDark,
			boundedBefore
		};
	}

	enum class CloseAction : std::uint8_t
	{
		kIssueOneDeliveryAndVisibilityOpportunity,
		kCoherentDark,
		kTerminalDark
	};

	struct CloseTransition
	{
		CohortState nextState{ CohortPhase::kClosedTerminalDark, 0 };
		CloseAction action{ CloseAction::kTerminalDark };
		std::uint8_t deliveryOpportunityCount{ 0 };
		std::uint8_t visibilityUpdateOpportunityCount{ 0 };

		constexpr bool operator==(const CloseTransition&) const noexcept = default;
	};

	/** Close one outer first-person call exactly once. */
	[[nodiscard]] constexpr CloseTransition CloseOuter(
		const CohortState state) noexcept
	{
		if (!IsValidCohortState(state)) {
			return {
				{ CohortPhase::kClosedTerminalDark,
					state.normalReturnCount > kOverflowNormalReturnCount ?
						kOverflowNormalReturnCount : state.normalReturnCount },
				CloseAction::kTerminalDark,
				0,
				0
			};
		}

		switch (state.phase) {
		case CohortPhase::kFirstStaged:
		case CohortPhase::kSecondCoalesced:
			return {
				{ CohortPhase::kClosedDelivered, state.normalReturnCount },
				CloseAction::kIssueOneDeliveryAndVisibilityOpportunity,
				1,
				1
			};
		case CohortPhase::kOpen:
		case CohortPhase::kCoherentDark:
			return {
				{ CohortPhase::kClosedCoherentDark, state.normalReturnCount },
				CloseAction::kCoherentDark,
				0,
				0
			};
		case CohortPhase::kTerminalDark:
			return {
				{ CohortPhase::kClosedTerminalDark, state.normalReturnCount },
				CloseAction::kTerminalDark,
				0,
				0
			};
		case CohortPhase::kClosedDelivered:
		case CohortPhase::kClosedCoherentDark:
		case CohortPhase::kClosedTerminalDark:
		default:
			// A second close is an invariant fault and cannot mint another
			// opportunity.
			return {
				{ CohortPhase::kClosedTerminalDark, state.normalReturnCount },
				CloseAction::kTerminalDark,
				0,
				0
			};
		}
	}
}
