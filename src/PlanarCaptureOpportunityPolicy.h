#pragma once

#include "PlanarCaptureScheduler.h"

#include <cstdint>

namespace PlanarCaptureOpportunityPolicy
{
	using Channel = PlanarCaptureScheduler::Channel;

	enum class Opportunity : std::uint8_t
	{
		kFenceTail,
		kDeferredExactMain
	};

	enum class Action : std::uint8_t
	{
		kNone,
		kRender,
		kReserveExactMirror,
		kCleanupBorrow
	};

	enum class Status : std::uint8_t
	{
		kAccepted,
		kNoEligibleChannel,
		kInvalidSourceSequence,
		kInvalidChannel,
		kInvalidMirrorIdentity,
		kDuplicateFenceDecision,
		kNoFenceDecision,
		kSourceSequenceMismatch,
		kFenceRenderAlreadySelected,
		kNoPendingExactMirror,
		kBorrowUnavailable,
		kDeferredAlreadyHandled,
		kRenderBudgetAlreadyConsumed
	};

	struct MirrorIdentity
	{
		std::uint32_t formID{ 0 };
		std::uint64_t candidateGeneration{ 0 };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return formID != 0 && candidateGeneration != 0;
		}
	};

	enum class MirrorLocationKind : std::uint8_t
	{
		kUnavailable,
		kInterior,
		kExterior
	};

	/**
	 * Pointer-free location identity safe to retain across the chained main-world
	 * invocation. Runtime object addresses are deliberately excluded: the
	 * deferred path reacquires them and performs the stronger synchronous check.
	 */
	struct MirrorLocationKey
	{
		MirrorLocationKind kind{ MirrorLocationKind::kUnavailable };
		std::uint32_t cellFormID{ 0 };
		std::uint32_t worldspaceFormID{ 0 };

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			switch (kind) {
			case MirrorLocationKind::kInterior:
				return cellFormID != 0 && worldspaceFormID == 0;
			case MirrorLocationKind::kExterior:
				return cellFormID == 0 && worldspaceFormID != 0;
			case MirrorLocationKind::kUnavailable:
			default:
				return false;
			}
		}
	};

	[[nodiscard]] constexpr bool SameMirrorLocation(
		const MirrorLocationKey& left,
		const MirrorLocationKey& right) noexcept
	{
		if (!left || !right || left.kind != right.kind)
			return false;
		switch (left.kind) {
		case MirrorLocationKind::kInterior:
			return left.cellFormID == right.cellFormID;
		case MirrorLocationKind::kExterior:
			return left.worldspaceFormID == right.worldspaceFormID;
		case MirrorLocationKind::kUnavailable:
		default:
			return false;
		}
	}

	/** Marker-off exact-main compatibility is intentionally mirror-only. */
	[[nodiscard]] constexpr bool LegacyDeferredAllows(Channel channel) noexcept
	{
		return channel == Channel::kMirror;
	}

	struct ExactMirrorTicket
	{
		std::uint64_t sourceSequence{ 0 };
		MirrorIdentity mirror{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return sourceSequence != 0 && static_cast<bool>(mirror);
		}
	};

	struct Decision
	{
		Opportunity opportunity{ Opportunity::kFenceTail };
		Action action{ Action::kNone };
		Status status{ Status::kNoFenceDecision };
		Channel channel{ Channel::kNone };
		std::uint64_t sourceSequence{ 0 };
		MirrorIdentity mirror{};

		[[nodiscard]] constexpr bool ShouldRender() const noexcept
		{
			return action == Action::kRender;
		}

		[[nodiscard]] constexpr bool ShouldReserveExactMirror() const noexcept
		{
			return action == Action::kReserveExactMirror;
		}

		[[nodiscard]] constexpr bool ShouldCleanupBorrow() const noexcept
		{
			return action == Action::kCleanupBorrow;
		}
	};

	[[nodiscard]] constexpr bool IsCaptureChannel(Channel channel) noexcept
	{
		switch (channel) {
		case Channel::kMirror:




			return true;
		case Channel::kNone:
		default:
			return false;
		}
	}

	/**
	 * Routes one already-arbitrated channel between the two verified capture
	 * opportunities. Generic planar channels render only at the scene-list fence;
	 * an enabled exact-main mirror becomes a value-only ticket consumed after the
	 * same main-world invocation returns. The coordinator never owns engine
	 * pointers, leases, cameras, publication attempts, or borrowed list views.
	 *
	 * One Coordinator is render-thread owned. It deliberately contains no atomics:
	 * cross-thread access is a caller error, while every transition remains fully
	 * engine-independent and deterministic for offline testing.
	 */
	class Coordinator
	{
	public:
		[[nodiscard]] constexpr Decision PlanFence(
			std::uint64_t sourceSequence,
			Channel selectedChannel,
			bool exactMainMirrorEnabled,
			MirrorIdentity mirror = {}) noexcept
		{
			if (sourceSequence == 0) {
				Reset();
				return FenceDecision(
					Action::kNone, Status::kInvalidSourceSequence,
					Channel::kNone, 0, {});
			}

			if (fenceObserved_ && fenceSourceSequence_ == sourceSequence) {
				return FenceDecision(
					Action::kNone, Status::kDuplicateFenceDecision,
					Channel::kNone, sourceSequence, {});
			}

			BeginFenceEpoch(sourceSequence);
			if (selectedChannel == Channel::kNone) {
				return FenceDecision(
					Action::kNone, Status::kNoEligibleChannel,
					Channel::kNone, sourceSequence, {});
			}
			if (!IsCaptureChannel(selectedChannel)) {
				return FenceDecision(
					Action::kNone, Status::kInvalidChannel,
					Channel::kNone, sourceSequence, {});
			}

			if (selectedChannel == Channel::kMirror && exactMainMirrorEnabled) {
				if (!mirror) {
					return FenceDecision(
						Action::kNone, Status::kInvalidMirrorIdentity,
						Channel::kNone, sourceSequence, {});
				}
				pendingMirror_ = { sourceSequence, mirror };
				return FenceDecision(
					Action::kReserveExactMirror, Status::kAccepted,
					Channel::kMirror, sourceSequence, mirror);
			}

			renderBudgetConsumed_ = true;
			return FenceDecision(
				Action::kRender, Status::kAccepted, selectedChannel,
				sourceSequence,
				selectedChannel == Channel::kMirror ? mirror : MirrorIdentity{});
		}

		[[nodiscard]] constexpr Decision ResolveDeferred(
			std::uint64_t sourceSequence,
			bool borrowActive) noexcept
		{
			Decision decision{
				Opportunity::kDeferredExactMain,
				Action::kCleanupBorrow,
				Status::kNoFenceDecision,
				Channel::kNone,
				sourceSequence,
				{}
			};
			if (!fenceObserved_)
				return decision;
			if (deferredHandled_) {
				decision.status = Status::kDeferredAlreadyHandled;
				return decision;
			}

			deferredHandled_ = true;
			if (sourceSequence == 0 || sourceSequence != fenceSourceSequence_) {
				pendingMirror_ = {};
				decision.status = sourceSequence == 0 ?
					Status::kInvalidSourceSequence :
					Status::kSourceSequenceMismatch;
				return decision;
			}
			if (!pendingMirror_) {
				decision.status = renderBudgetConsumed_ ?
					Status::kFenceRenderAlreadySelected :
					Status::kNoPendingExactMirror;
				return decision;
			}

			const ExactMirrorTicket ticket = pendingMirror_;
			pendingMirror_ = {};
			if (!borrowActive) {
				decision.status = Status::kBorrowUnavailable;
				return decision;
			}
			if (renderBudgetConsumed_) {
				decision.status = Status::kRenderBudgetAlreadyConsumed;
				return decision;
			}

			renderBudgetConsumed_ = true;
			decision.action = Action::kRender;
			decision.status = Status::kAccepted;
			decision.channel = Channel::kMirror;
			decision.mirror = ticket.mirror;
			return decision;
		}

		constexpr void Reset() noexcept
		{
			fenceSourceSequence_ = 0;
			fenceObserved_ = false;
			deferredHandled_ = false;
			renderBudgetConsumed_ = false;
			pendingMirror_ = {};
		}

		[[nodiscard]] constexpr ExactMirrorTicket PendingMirror() const noexcept
		{
			return pendingMirror_;
		}

		[[nodiscard]] constexpr bool RenderBudgetConsumed() const noexcept
		{
			return renderBudgetConsumed_;
		}

		[[nodiscard]] constexpr std::uint64_t FenceSourceSequence() const noexcept
		{
			return fenceSourceSequence_;
		}

	private:
		[[nodiscard]] static constexpr Decision FenceDecision(
			Action action,
			Status status,
			Channel channel,
			std::uint64_t sourceSequence,
			MirrorIdentity mirror) noexcept
		{
			return {
				Opportunity::kFenceTail,
				action,
				status,
				channel,
				sourceSequence,
				mirror
			};
		}

		constexpr void BeginFenceEpoch(std::uint64_t sourceSequence) noexcept
		{
			fenceSourceSequence_ = sourceSequence;
			fenceObserved_ = true;
			deferredHandled_ = false;
			renderBudgetConsumed_ = false;
			// A new fence epoch supersedes any exact-main ticket that was not
			// consumed by its own post-main callback.
			pendingMirror_ = {};
		}

		std::uint64_t fenceSourceSequence_{ 0 };
		bool fenceObserved_{ false };
		bool deferredHandled_{ false };
		bool renderBudgetConsumed_{ false };
		ExactMirrorTicket pendingMirror_{};
	};
}
