#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace MirrorPrivateLODStabilizationPolicy
{
	struct ActivationInputs
	{
		bool requested{ false };
		bool exactMainReplayEnabled{ false };
		bool supplementalCycleEnabled{ false };
		bool supplementalNearReferencesEnabled{ false };
		bool reflectedWorldRootEnabled{ false };
	};

	/**
	 * The derived replay is valid only beside the complete near-reference route.
	 * Reflected-WorldRoot coverage is a different traversal/identity contract and
	 * must never silently activate this experiment.
	 */
	[[nodiscard]] constexpr bool ShouldActivate(
		const ActivationInputs& inputs) noexcept
	{
		return inputs.requested && inputs.exactMainReplayEnabled &&
			inputs.supplementalCycleEnabled &&
			inputs.supplementalNearReferencesEnabled &&
			!inputs.reflectedWorldRootEnabled;
	}

	/**
	 * The roster filter alone (no LOD freeze) only changes which borrowed
	 * entries are culled, and every capture proves its roster before using it:
	 * a capture that runs reflected-WorldRoot coverage (an exterior) has no
	 * authoritative near-reference roster and falls back to the plain replay.
	 * So WorldRoot coverage being available is no reason to switch the filter
	 * off for interiors. Core run 6 (2026-09-17) culled the borrowed lists in
	 * full and then discarded 712,732 of 729,218 replay draws as duplicates.
	 */
	[[nodiscard]] constexpr bool ShouldActivateRosterFilter(
		const ActivationInputs& inputs) noexcept
	{
		return inputs.requested && inputs.exactMainReplayEnabled &&
			inputs.supplementalCycleEnabled &&
			inputs.supplementalNearReferencesEnabled;
	}

	/**
	 * The native descriptor initializer enables camera-related updates.  The
	 * default-off stabilization path changes that value only when a private
	 * descriptor replays shared roots already updated by the completed main
	 * camera, or would repeat another private-camera LOD/hysteresis update.
	 *
	 * A future private-state lease may safely isolate those writes from Skyrim's
	 * main-view state.  Until that lease exists, duplicate traversals must not
	 * perform another camera-related update.
	 */
	enum class CameraUpdateAction : std::uint8_t
	{
		kUseInitializerValue,
		kForceDisabled
	};

	struct CameraUpdateInputs
	{
		bool stabilizationRequested{ false };
		// True when this private descriptor replays shared roots that have already
		// received the main-camera update, or would repeat another private update.
		bool duplicatePrivateTraversal{ false };
		bool isolatedPrivateStateLeaseActive{ false };
	};

	[[nodiscard]] constexpr CameraUpdateAction SelectCameraUpdateAction(
		const CameraUpdateInputs& inputs) noexcept
	{
		if (!inputs.stabilizationRequested ||
			!inputs.duplicatePrivateTraversal ||
			inputs.isolatedPrivateStateLeaseActive) {
			return CameraUpdateAction::kUseInitializerValue;
		}
		return CameraUpdateAction::kForceDisabled;
	}

	[[nodiscard]] constexpr bool ResolveCameraRelatedUpdates(
		CameraUpdateAction action,
		bool initializerValue) noexcept
	{
		return action == CameraUpdateAction::kForceDisabled ?
			false : initializerValue;
	}

	/** Only plugin-owned retained headers may be filtered. */
	enum class HeaderOwnership : std::uint8_t
	{
		kUnknown,
		kEngineOwnedLive,
		kPluginOwnedRetained
	};

	/**
	 * Stable value identity for one retained supplemental static.  The root
	 * address proves exact scene-root coverage; the nonzero reference FormID
	 * rejects null, recycled, or ambiguous roster entries before filtering.
	 */
	struct StaticRootIdentity
	{
		std::uintptr_t root{ 0 };
		std::uint32_t formID{ 0 };
	};

	[[nodiscard]] constexpr bool Valid(const StaticRootIdentity& identity) noexcept
	{
		return identity.root != 0 && identity.formID != 0;
	}

	enum class CoverageStatus : std::uint8_t
	{
		kComplete,
		kNotAuthoritative,
		kCollectionIncomplete,
		kOverflow,
		kCountMismatch,
		kMalformedIdentity
	};

	struct SupplementalCoverageEvidence
	{
		bool authoritative{ false };
		bool collectionComplete{ false };
		bool overflowObserved{ false };
		std::size_t expectedStaticCount{ 0 };
		std::size_t staticCapacity{ 0 };
		std::span<const StaticRootIdentity> identities{};
	};

	/**
	 * Validate the complete supplemental static roster before one borrowed
	 * entry can be omitted.  Duplicate roots or FormIDs are ambiguous identity,
	 * not harmless duplication, and reject the capture.
	 */
	[[nodiscard]] constexpr CoverageStatus ValidateCoverage(
		const SupplementalCoverageEvidence& evidence) noexcept
	{
		if (!evidence.authoritative)
			return CoverageStatus::kNotAuthoritative;
		if (evidence.overflowObserved ||
			evidence.expectedStaticCount > evidence.staticCapacity ||
			evidence.identities.size() > evidence.staticCapacity) {
			return CoverageStatus::kOverflow;
		}
		if (!evidence.collectionComplete)
			return CoverageStatus::kCollectionIncomplete;
		if (evidence.expectedStaticCount != evidence.identities.size())
			return CoverageStatus::kCountMismatch;

		for (std::size_t i = 0; i < evidence.identities.size(); ++i) {
			const auto& identity = evidence.identities[i];
			if (!Valid(identity))
				return CoverageStatus::kMalformedIdentity;
			for (std::size_t j = 0; j < i; ++j) {
				const auto& prior = evidence.identities[j];
				if (identity.root == prior.root || identity.formID == prior.formID)
					return CoverageStatus::kMalformedIdentity;
			}
		}
		return CoverageStatus::kComplete;
	}

	enum class BorrowedEntryAction : std::uint8_t
	{
		kKeep,
		kFilterCoveredStatic,
		kRejectCapture
	};

	enum class DecisionReason : std::uint8_t
	{
		kPolicyDisabled,
		kNoSupplementalMatch,
		kCoveredByCompleteSupplementalRoster,
		kHeaderNotPluginOwned,
		kNotAuthoritative,
		kCollectionIncomplete,
		kOverflow,
		kCountMismatch,
		kMalformedIdentity
	};

	struct BorrowedEntryInputs
	{
		bool stabilizationRequested{ false };
		HeaderOwnership headerOwnership{ HeaderOwnership::kUnknown };
		std::uintptr_t borrowedRoot{ 0 };
		SupplementalCoverageEvidence coverage{};
	};

	struct BorrowedEntryDecision
	{
		BorrowedEntryAction action{ BorrowedEntryAction::kKeep };
		DecisionReason reason{ DecisionReason::kPolicyDisabled };
	};

	[[nodiscard]] constexpr DecisionReason RejectionReason(
		CoverageStatus status) noexcept
	{
		switch (status) {
		case CoverageStatus::kNotAuthoritative:
			return DecisionReason::kNotAuthoritative;
		case CoverageStatus::kCollectionIncomplete:
			return DecisionReason::kCollectionIncomplete;
		case CoverageStatus::kOverflow:
			return DecisionReason::kOverflow;
		case CoverageStatus::kCountMismatch:
			return DecisionReason::kCountMismatch;
		case CoverageStatus::kMalformedIdentity:
			return DecisionReason::kMalformedIdentity;
		case CoverageStatus::kComplete:
		default:
			return DecisionReason::kMalformedIdentity;
		}
	}

	/**
	 * Filter only an exact root proven to be covered by a complete authoritative
	 * supplemental roster, and only from the plugin's retained copy.  Once the
	 * stabilization path is requested, missing proof rejects the whole capture;
	 * it never falls back to a partial surroundings frame.
	 */
	[[nodiscard]] constexpr BorrowedEntryDecision DecideBorrowedEntry(
		const BorrowedEntryInputs& inputs) noexcept
	{
		if (!inputs.stabilizationRequested) {
			return {
				BorrowedEntryAction::kKeep,
				DecisionReason::kPolicyDisabled
			};
		}
		if (inputs.headerOwnership != HeaderOwnership::kPluginOwnedRetained) {
			return {
				BorrowedEntryAction::kRejectCapture,
				DecisionReason::kHeaderNotPluginOwned
			};
		}
		if (inputs.borrowedRoot == 0) {
			return {
				BorrowedEntryAction::kRejectCapture,
				DecisionReason::kMalformedIdentity
			};
		}

		const auto coverageStatus = ValidateCoverage(inputs.coverage);
		if (coverageStatus != CoverageStatus::kComplete) {
			return {
				BorrowedEntryAction::kRejectCapture,
				RejectionReason(coverageStatus)
			};
		}
		for (const auto& identity : inputs.coverage.identities) {
			if (identity.root == inputs.borrowedRoot) {
				return {
					BorrowedEntryAction::kFilterCoveredStatic,
					DecisionReason::kCoveredByCompleteSupplementalRoster
				};
			}
		}
		return {
			BorrowedEntryAction::kKeep,
			DecisionReason::kNoSupplementalMatch
		};
	}
}
