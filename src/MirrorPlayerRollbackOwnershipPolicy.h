#pragma once

#include <cstdint>

namespace MirrorPlayerRollbackOwnershipPolicy
{
	enum class Status : std::uint8_t
	{
		kAccepted,
		kBusy,
		kInvalid,
		kTerminal
	};

	/**
	 * Engine-independent ownership state for the one player app-cull mutation.
	 * The native adapter serializes every transition.  A live scope owns the
	 * domain with no retained identity; a failed rollback atomically transfers
	 * that ownership and its capacity-one retained root into the recovery phase.
	 */
	struct State
	{
		std::uintptr_t retainedRootIdentity{ 0 };
		bool priorAppCulled{ false };
		bool ownerActive{ false };
		bool recoveryPending{ false };
		bool ownsReference{ false };
		bool terminal{ false };
	};

	[[nodiscard]] constexpr Status TryBeginScope(State& state) noexcept
	{
		if (state.terminal)
			return Status::kTerminal;
		if (state.ownerActive || state.recoveryPending || state.ownsReference ||
			state.retainedRootIdentity != 0) {
			return Status::kBusy;
		}
		state.ownerActive = true;
		return Status::kAccepted;
	}

	[[nodiscard]] constexpr Status TransferFailedRollback(
		State& state,
		const std::uintptr_t retainedRootIdentity,
		const bool priorAppCulled) noexcept
	{
		if (state.terminal)
			return Status::kTerminal;
		if (!state.ownerActive || state.recoveryPending || state.ownsReference ||
			state.retainedRootIdentity != 0 || retainedRootIdentity == 0) {
			state.terminal = true;
			return Status::kInvalid;
		}
		state.retainedRootIdentity = retainedRootIdentity;
		state.priorAppCulled = priorAppCulled;
		state.recoveryPending = true;
		state.ownsReference = true;
		return Status::kAccepted;
	}

	[[nodiscard]] constexpr bool IsExactPendingRecovery(
		const State& state,
		const std::uintptr_t retainedRootIdentity,
		const bool priorAppCulled) noexcept
	{
		return !state.terminal && state.ownerActive && state.recoveryPending &&
			state.ownsReference && retainedRootIdentity != 0 &&
			state.retainedRootIdentity == retainedRootIdentity &&
			state.priorAppCulled == priorAppCulled;
	}

	/** Tombstone every replayable identity only after exact native readback. */
	[[nodiscard]] constexpr Status TombstoneAfterExactReadback(
		State& state,
		const std::uintptr_t retainedRootIdentity,
		const bool priorAppCulled) noexcept
	{
		if (!IsExactPendingRecovery(
				state, retainedRootIdentity, priorAppCulled)) {
			state.terminal = true;
			return Status::kInvalid;
		}
		state.retainedRootIdentity = 0;
		state.priorAppCulled = false;
		state.recoveryPending = false;
		state.ownsReference = false;
		return Status::kAccepted;
	}

	/** Close the owner only after the retained native reference released cleanly. */
	[[nodiscard]] constexpr Status CompleteReferenceRelease(
		State& state,
		const bool released) noexcept
	{
		if (!state.ownerActive || state.recoveryPending || state.ownsReference ||
			state.retainedRootIdentity != 0) {
			state.terminal = true;
			return Status::kInvalid;
		}
		if (!released) {
			state.terminal = true;
			return Status::kTerminal;
		}
		state.ownerActive = false;
		return Status::kAccepted;
	}

	[[nodiscard]] constexpr bool AdmissionBlocked(const State& state) noexcept
	{
		return state.terminal || state.ownerActive || state.recoveryPending ||
			state.ownsReference || state.retainedRootIdentity != 0;
	}
}
