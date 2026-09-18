#pragma once

#include <cstddef>
#include <cstdint>

namespace HandMirrorBillboardStateLease
{
	/**
	 * Exact SE 1.5.97 NiBillboardNode::OnVisible interception. During the owned
	 * hand pass, native private-camera facing remains authoritative while the
	 * authored center of every disjoint direct-child branch is held fixed in
	 * world space. `requested
	 * == false` is a strict no-op and performs no vtable write, allowing the
	 * caller to bind installation to the existing default-off hand marker.
	 */
	enum class InstallStatus : std::uint8_t
	{
		kNotRequested,
		kInstalled,
		kAlreadyInstalled,
		kUnsupportedRuntime,
		kNativeSignatureMismatch,
		kPatchFailed,
		kFaulted
	};

	enum class BeginStatus : std::uint8_t
	{
		kBegan,
		kNotInstalled,
		kFaulted,
		kNested,
		kHookOwnershipLost,
		kAnotherThreadActive
	};

	/** POD token which must be closed from the caller-owned SEH __finally. */
	struct CaptureToken
	{
		std::uint32_t generation{ 0 };
		std::uint32_t threadID{ 0 };
		BeginStatus status{ BeginStatus::kNotInstalled };
		bool active{ false };
	};

	struct CaptureResult
	{
		std::size_t recordedObjects{ 0 };
		std::size_t duplicateObjects{ 0 };
		std::size_t billboardCallbacks{ 0 };
		std::size_t restoreAttempts{ 0 };
		std::size_t restoreSuccesses{ 0 };
		std::size_t releaseAttempts{ 0 };
		std::size_t releaseSuccesses{ 0 };
		std::size_t nativeCalls{ 0 };
		std::size_t nativeReturns{ 0 };
		std::size_t facingUpdateCalls{ 0 };
		std::size_t facingUpdateSkips{ 0 };
		std::size_t branchAnchors{ 0 };
		std::size_t branchGeometries{ 0 };
		std::size_t branchShifts{ 0 };
		std::size_t branchReadbackFailures{ 0 };
		bool tokenMatched{ false };
		bool rejectedBeforeMutation{ false };
		bool overflowed{ false };
		bool faulted{ false };
		bool clean{ false };
	};

	struct Diagnostics
	{
		std::uint64_t installAttempts{ 0 };
		std::uint64_t installSuccesses{ 0 };
		std::uint64_t beginAttempts{ 0 };
		std::uint64_t begins{ 0 };
		std::uint64_t beginRejects{ 0 };
		std::uint64_t hookCalls{ 0 };
		std::uint64_t inactiveNativeCalls{ 0 };
		std::uint64_t activeNativeCalls{ 0 };
		std::uint64_t activeNativeReturns{ 0 };
		std::uint64_t facingHookCalls{ 0 };
		std::uint64_t inactiveFacingNativeCalls{ 0 };
		std::uint64_t activeFacingUpdateSkips{ 0 };
		std::uint64_t crossThreadFacingCalls{ 0 };
		std::uint64_t recordedObjects{ 0 };
		std::uint64_t duplicateObjects{ 0 };
		std::uint64_t capacityOverflows{ 0 };
		std::uint64_t snapshotFaults{ 0 };
		std::uint64_t retainFaults{ 0 };
		std::uint64_t skippedNativeMutations{ 0 };
		std::uint64_t completedScopes{ 0 };
		std::uint64_t rejectedScopes{ 0 };
		std::uint64_t restoredObjects{ 0 };
		std::uint64_t restoreFailures{ 0 };
		std::uint64_t releasedObjects{ 0 };
		std::uint64_t releaseFailures{ 0 };
		std::uint64_t tokenMismatches{ 0 };
		std::uint64_t crossThreadCallbacks{ 0 };
		std::uint64_t branchAnchors{ 0 };
		std::uint64_t branchGeometries{ 0 };
		std::uint64_t branchShifts{ 0 };
		std::uint64_t branchReadbackFailures{ 0 };
		float maximumPreAnchorDrift{ 0.0F };
		float maximumPostAnchorResidual{ 0.0F };
		std::uint32_t lastExceptionCode{ 0 };
		bool installed{ false };
		bool hookOwned{ false };
		bool faulted{ false };
	};

	[[nodiscard]] InstallStatus Install(bool requested) noexcept;
	[[nodiscard]] CaptureToken BeginCapture() noexcept;

	/**
	 * Disables interception first, restores every first snapshot in reverse
	 * order, verifies readback, then tombstones and releases all retained refs.
	 */
	[[nodiscard]] CaptureResult EndAndRestore(CaptureToken& token) noexcept;

	[[nodiscard]] bool CurrentCaptureHealthy() noexcept;
	[[nodiscard]] bool Installed() noexcept;
	[[nodiscard]] bool OwnsHook() noexcept;
	[[nodiscard]] bool Faulted() noexcept;
	[[nodiscard]] Diagnostics GetDiagnostics() noexcept;
}
