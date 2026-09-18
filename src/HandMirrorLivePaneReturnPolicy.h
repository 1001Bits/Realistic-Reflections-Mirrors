#pragma once

#include <cstdint>

namespace HandMirrorLivePaneReturnPolicy
{
	/**
	 * Immutable M5 capture-receipt target.  This identifies the completed main
	 * world sample; it is not the target on which the later first-person pane is
	 * necessarily submitted.
	 */
	struct FrozenCaptureTargetIdentity
	{
		std::uint64_t allocationGeneration{ 0 };
		std::uint64_t colorResourceIdentity{ 0 };
		std::uint64_t depthResourceIdentity{ 0 };

		constexpr bool operator==(
			const FrozenCaptureTargetIdentity&) const noexcept = default;
	};

	/** Exact COM identities retained from the live first-person draw target. */
	struct LiveDeliveryTargetIdentity
	{
		std::uintptr_t deviceIdentity{ 0 };
		std::uintptr_t contextIdentity{ 0 };
		std::uintptr_t colorViewIdentity{ 0 };
		std::uintptr_t depthViewIdentity{ 0 };
		std::uintptr_t colorResourceIdentity{ 0 };
		std::uintptr_t depthResourceIdentity{ 0 };

		constexpr bool operator==(
			const LiveDeliveryTargetIdentity&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidFrozenCaptureTarget(
		const FrozenCaptureTargetIdentity& target) noexcept
	{
		return target.allocationGeneration != 0 &&
		       target.colorResourceIdentity != 0 &&
		       target.depthResourceIdentity != 0 &&
		       target.colorResourceIdentity != target.depthResourceIdentity;
	}

	[[nodiscard]] constexpr bool IsValidLiveDeliveryTarget(
		const LiveDeliveryTargetIdentity& target) noexcept
	{
		return target.deviceIdentity != 0 && target.contextIdentity != 0 &&
		       target.colorViewIdentity != 0 && target.depthViewIdentity != 0 &&
		       target.colorResourceIdentity != 0 &&
		       target.depthResourceIdentity != 0 &&
		       target.colorViewIdentity != target.depthViewIdentity &&
		       target.colorResourceIdentity != target.depthResourceIdentity;
	}

	/**
	 * A zero-callback detached replay needs the same presentation color
	 * destination, but it does not need the depth view/resource frozen before the
	 * native first-person call. Skyrim may rotate only that depth attachment while
	 * retaining the exact device, context, color RTV, and color resource. The
	 * caller must independently prove the same receipt/capture target and retain
	 * the complete returned color/depth pair before using this weaker relation.
	 * Fresh publication still uses EvaluateEntryToReturn and therefore requires
	 * exact equality of every live target field.
	 */
	[[nodiscard]] constexpr bool
	SameColorDestinationForDetachedReplay(
		const LiveDeliveryTargetIdentity& entry,
		const LiveDeliveryTargetIdentity& returned) noexcept
	{
		return IsValidLiveDeliveryTarget(entry) &&
		       IsValidLiveDeliveryTarget(returned) &&
		       entry.deviceIdentity == returned.deviceIdentity &&
		       entry.contextIdentity == returned.contextIdentity &&
		       entry.colorViewIdentity == returned.colorViewIdentity &&
		       entry.colorResourceIdentity == returned.colorResourceIdentity;
	}

	/**
	 * Evidence for one exact native pane call from entry through normal return.
	 *
	 * Frozen capture and live delivery targets are deliberately separate
	 * domains.  Each domain must remain internally stable, but no equality is
	 * required between the two domains.  Camera/view/viewport presentation is
	 * deliberately absent at entry: native drawing may make that state current.
	 * The caller copies the authoritative presentation only after normal return,
	 * and proves its completeness with returnedPresentationComplete.
	 */
	struct EntryToReturnEvidence
	{
		FrozenCaptureTargetIdentity frozenTargetAtEntry{};
		FrozenCaptureTargetIdentity frozenTargetAtReturn{};
		LiveDeliveryTargetIdentity liveTargetAtEntry{};
		LiveDeliveryTargetIdentity liveTargetAtReturn{};
		bool normalReturnProven{ false };
		bool receiptIdentityStable{ false };
		bool stablePaneIdentityStable{ false };
		bool returnedPresentationComplete{ false };

		constexpr bool operator==(
			const EntryToReturnEvidence&) const noexcept = default;
	};

	enum class EntryToReturnStatus : std::uint8_t
	{
		kAccepted,
		kNativeReturnMissing,
		kFrozenTargetInvalid,
		kFrozenTargetChanged,
		kLiveEntryTargetInvalid,
		kLiveReturnTargetInvalid,
		kLiveDeliveryTargetChanged,
		kReceiptIdentityChanged,
		kStablePaneIdentityChanged,
		kReturnedPresentationIncomplete
	};

	[[nodiscard]] constexpr EntryToReturnStatus EvaluateEntryToReturn(
		const EntryToReturnEvidence& evidence) noexcept
	{
		if (!evidence.normalReturnProven)
			return EntryToReturnStatus::kNativeReturnMissing;
		if (!IsValidFrozenCaptureTarget(evidence.frozenTargetAtEntry) ||
			!IsValidFrozenCaptureTarget(evidence.frozenTargetAtReturn)) {
			return EntryToReturnStatus::kFrozenTargetInvalid;
		}
		if (evidence.frozenTargetAtEntry != evidence.frozenTargetAtReturn)
			return EntryToReturnStatus::kFrozenTargetChanged;
		if (!IsValidLiveDeliveryTarget(evidence.liveTargetAtEntry))
			return EntryToReturnStatus::kLiveEntryTargetInvalid;
		if (!IsValidLiveDeliveryTarget(evidence.liveTargetAtReturn))
			return EntryToReturnStatus::kLiveReturnTargetInvalid;
		if (evidence.liveTargetAtEntry != evidence.liveTargetAtReturn)
			return EntryToReturnStatus::kLiveDeliveryTargetChanged;
		if (!evidence.receiptIdentityStable)
			return EntryToReturnStatus::kReceiptIdentityChanged;
		if (!evidence.stablePaneIdentityStable)
			return EntryToReturnStatus::kStablePaneIdentityChanged;
		if (!evidence.returnedPresentationComplete) {
			return EntryToReturnStatus::kReturnedPresentationIncomplete;
		}
		return EntryToReturnStatus::kAccepted;
	}

	[[nodiscard]] constexpr bool Accepted(
		const EntryToReturnEvidence& evidence) noexcept
	{
		return EvaluateEntryToReturn(evidence) == EntryToReturnStatus::kAccepted;
	}
}
