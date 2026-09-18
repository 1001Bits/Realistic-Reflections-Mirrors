#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "PlanarMirrorMath.h"
#include "HandMirrorRuntimeBridgePolicy.h"
#include "MultiMirrorPolicy.h"

namespace MirrorFramePublication
{
	struct WallSnapshotKey
	{
		std::uint32_t candidateFormID{ 0 };
		std::uint64_t candidateGeneration{ 0 };
		std::uint64_t captureSequence{ 0 };
		std::uintptr_t colorResourceIdentity{ 0 };
		std::uintptr_t depthResourceIdentity{ 0 };

		constexpr bool operator==(const WallSnapshotKey&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool IsValidWallSnapshotKey(
		const WallSnapshotKey& key) noexcept
	{
		return key.candidateFormID != 0 && key.candidateGeneration != 0 &&
		       key.captureSequence != 0 && key.colorResourceIdentity != 0 &&
		       key.depthResourceIdentity != 0 &&
		       key.colorResourceIdentity != key.depthResourceIdentity;
	}

	struct BridgePublicationContext
	{
		std::uint64_t sourceSequence{ 0 };
		std::uint64_t mainViewFrame{ 0 };
		/** Exact owner grant issued by the shared hand/wall singleton arbiter. */
		std::uint64_t ownerLeaseSequence{ 0 };
		HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity privateTarget{};

		constexpr bool operator==(const BridgePublicationContext&) const noexcept =
			default;
	};

	enum class ExactRetirementStatus : std::uint8_t
	{
		kRetired,
		/** The exact publication was already absent or atomically replaced. */
		kExactPriorUnavailable,
		/** Caller-supplied successor, target, token, or receipt evidence is invalid. */
		kInvalidEvidence
	};

	enum class AttemptStartPolicy : std::uint8_t
	{
		/** Historical single-target behavior: revoke the completed frame immediately. */
		kInvalidateCompletedFrame,
		/**
		 * Revoke only the outstanding producer token. The caller guarantees that the
		 * next attempt renders into a resource which cannot alias the completed frame.
		 */
		kPreserveCompletedFrame
	};

	/**
	 * Opaque producer authorization for one reflected capture attempt.  Starting
	 * another attempt or explicitly invalidating publication revokes the token.
	 */
	struct AttemptToken
	{
		std::uint32_t candidateFormID{ 0 };
		std::uint64_t candidateGeneration{ 0 };
		std::uint64_t attemptSequence{ 0 };
		std::size_t slot{ 0 };

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return candidateFormID != 0 && candidateGeneration != 0 &&
			       attemptSequence != 0;
		}
	};

	/** Stable metadata plus COM-retained SRVs consumed by the later M5/evidence draws. */
	struct Snapshot
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV{};
		DirectX::XMFLOAT4X4 reflectedViewProjection{};
		DirectX::XMFLOAT3 reflectedOrigin{};
		// The raster main view of the frame the capture was posed from (read at
		// the fence immediately after the engine's SetCameraData). A pane drawn
		// with THIS pair is self-consistent — at most one frame late — instead
		// of mixing the capture with a different frame's camera (the measured
		// moving ghosting).
		DirectX::XMFLOAT4X4 mainViewProjection{};
		DirectX::XMFLOAT3 mainOrigin{};
		bool mainViewValid{ false };
		// Nonzero only for exact-main captures deferred until after the source
		// main-world invocation.  M5 accepts such a frame only on the immediately
		// following invocation and uses the retained main view above.
		std::uint64_t sourceMainWorldFrame{ 0 };
		PlanarMirrorMath::Plane capturePlane{};
		std::uint32_t candidateFormID{ 0 };
		std::uint64_t candidateGeneration{ 0 };
		std::uint64_t captureSequence{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		DXGI_FORMAT colorFormat{ DXGI_FORMAT_UNKNOWN };
		/** Exact internal identity used only for hand/wall singleton retirement. */
		HandMirrorRuntimeBridgePolicy::PriorPublicationIdentity bridgeIdentity{};
		/** Always-populated opaque key for exact legacy-frame revocation. */
		WallSnapshotKey wallKey{};
		bool valid{ false };
		std::size_t slot{ 0 };
	};

	/** Startup-only, default-off mode. Changing mode revokes every publication. */
	void ConfigureMultiMirror(bool enabled) noexcept;
	[[nodiscard]] bool MultiMirrorEnabled() noexcept;
	void InvalidateSlot(std::size_t slot, const char* reason) noexcept;
	void InvalidateCandidate(std::uint32_t formID, const char* reason) noexcept;
	using SnapshotSet = std::vector<Snapshot>;
	[[nodiscard]] std::size_t GetSnapshots(SnapshotSet& output) noexcept;
	[[nodiscard]] bool TryGetSnapshot(
		std::uint32_t formID, std::uint64_t generation, Snapshot& output) noexcept;
	/** Must be checked BEFORE binding a write target; COM retention alone is insufficient. */
	[[nodiscard]] bool CaptureResourcesDisjoint(
		const void* colorResource, const void* depthResource) noexcept;

	/**
	 * Begin one reflected attempt for this exact M2 candidate lifetime. A valid
	 * preserve request revokes only the previous attempt token; it is legal only
	 * when the producer uses a disjoint capture resource. Invalid identity always
	 * revokes the completed frame and returns an invalid token.
	 */
	[[nodiscard]] AttemptToken BeginReflectedAttempt(
		std::uint32_t candidateFormID,
		std::uint64_t candidateGeneration,
		AttemptStartPolicy policy =
			AttemptStartPolicy::kInvalidateCompletedFrame,
		std::size_t slot = 0) noexcept;

	/** Revoke one still-current attempt token without changing the completed frame. */
	[[nodiscard]] bool CancelReflectedAttempt(
		const AttemptToken& attempt) noexcept;

	/** Revoke every outstanding producer token and release the published SRV. */
	void Invalidate() noexcept;
	/** Same, tagging the bounded `[RR][M3][wall-invalidate]` log with a reason. */
	void Invalidate(const char* reason) noexcept;
	/** Compare-and-revoke one exact snapshot; never clears a replacement frame. */
	[[nodiscard]] bool InvalidateExact(const WallSnapshotKey& key) noexcept;

	/**
	 * Publish a completed reflected capture.  The caller must invoke this only
	 * after the private RT is unbound and renderer camera/accumulator restoration
	 * succeeds.  A revoked token or malformed payload is rejected.
	 */
	[[nodiscard]] bool Publish(
		const AttemptToken& attempt,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV,
		const DirectX::XMFLOAT4X4& reflectedViewProjection,
		const DirectX::XMFLOAT3& reflectedOrigin,
		const PlanarMirrorMath::Plane& capturePlane,
		std::uint32_t width,
		std::uint32_t height,
		DXGI_FORMAT colorFormat,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV = {},
		const DirectX::XMFLOAT4X4* mainViewProjection = nullptr,
		const DirectX::XMFLOAT3* mainOrigin = nullptr,
		std::uint64_t sourceMainWorldFrame = 0,
		const BridgePublicationContext& bridgeContext = {}) noexcept;

	/**
	 * Atomically revoke one exact wall publication for a disjoint successor and
	 * return the typed receipt consumed by the hand/wall bridge policy.
	 */
	[[nodiscard]] ExactRetirementStatus RetireExactForSuccessor(
		const HandMirrorRuntimeBridgePolicy::PriorPublicationIdentity& prior,
		const HandMirrorRuntimeBridgePolicy::MirrorOwnerIdentity& successor,
		std::uint64_t successorOwnerLeaseSequence,
		std::uint64_t retirementToken,
		const std::array<HandMirrorRuntimeBridgePolicy::PrivateTargetIdentity, 2>&
			successorTargets,
		HandMirrorRuntimeBridgePolicy::PublicationRetirementReceipt& output) noexcept;

	/**
	 * Copy the latest valid metadata, retaining its SRV object for the caller.
	 * Consumers must keep this scoped to their synchronous draw.  Invalidation
	 * cannot revoke the COM object, but a later producer render can replace the
	 * texture contents; the retained reference is not a frozen image snapshot.
	 */
	[[nodiscard]] bool TryGetSnapshot(Snapshot& output) noexcept;
}
