#pragma once

#include "MirrorPaneRenderer.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace HandMirrorPresentationRetargetPolicy
{
	/**
	 * The V85 owner run contained one clean 1,509-source capture gap.  Keep one
	 * already-presented physical role through that class of bounded outage, with
	 * power-of-two headroom, but never turn it into an unbounded texture cache.
	 */
	inline constexpr std::uint64_t kMaximumConsecutiveCleanMissSources = 2048;

	[[nodiscard]] constexpr bool CanExtendConsecutiveCleanMiss(
		const std::uint64_t capturedSource,
		const std::uint64_t heldThroughSource,
		const std::uint64_t nextSource,
		const bool heldThroughLiveIdentityValidated) noexcept
	{
		return heldThroughLiveIdentityValidated && capturedSource != 0 &&
		       capturedSource != (std::numeric_limits<std::uint64_t>::max)() &&
		       heldThroughSource > capturedSource &&
		       heldThroughSource - capturedSource <=
			       kMaximumConsecutiveCleanMissSources &&
		       heldThroughSource != (std::numeric_limits<std::uint64_t>::max)() &&
		       nextSource == heldThroughSource + 1 && nextSource > capturedSource &&
		       nextSource - capturedSource <=
			       kMaximumConsecutiveCleanMissSources;
	}

	namespace Detail
	{
		inline constexpr float kProjectiveWEpsilon = 1.0e-5F;

		[[nodiscard]] constexpr bool Finite(const float value) noexcept
		{
			return (std::bit_cast<std::uint32_t>(value) & 0x7F800000U) !=
			       0x7F800000U;
		}

		[[nodiscard]] inline bool Finite(const DirectX::XMFLOAT3& value) noexcept
		{
			return Finite(value.x) && Finite(value.y) && Finite(value.z);
		}

		[[nodiscard]] inline bool Finite(
			const DirectX::XMFLOAT4X4& value) noexcept
		{
			const float* components = &value._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!Finite(components[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] inline bool NonDegenerateBasis(
			const MirrorPaneRenderer::PaneTransform& pane,
			DirectX::XMVECTOR& normal) noexcept
		{
			if (!Finite(pane.center) || !Finite(pane.tangentExtent) ||
				!Finite(pane.bitangentExtent)) {
				return false;
			}
			const auto tangent = DirectX::XMLoadFloat3(&pane.tangentExtent);
			const auto bitangent = DirectX::XMLoadFloat3(&pane.bitangentExtent);
			normal = DirectX::XMVector3Cross(tangent, bitangent);
			const float tangentLengthSquared =
				DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(tangent));
			const float bitangentLengthSquared =
				DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(bitangent));
			const float normalLengthSquared =
				DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(normal));
			return Finite(tangentLengthSquared) &&
			       Finite(bitangentLengthSquared) && Finite(normalLengthSquared) &&
			       tangentLengthSquared > 1.0e-8F &&
			       bitangentLengthSquared > 1.0e-8F &&
			       normalLengthSquared > 1.0e-12F;
		}

		[[nodiscard]] inline DirectX::XMMATRIX BasisRows(
			const MirrorPaneRenderer::PaneTransform& pane,
			const DirectX::XMVECTOR normal) noexcept
		{
			return DirectX::XMMATRIX{
				DirectX::XMVectorSet(
					pane.tangentExtent.x, pane.tangentExtent.y,
					pane.tangentExtent.z, 0.0F),
				DirectX::XMVectorSet(
					pane.bitangentExtent.x, pane.bitangentExtent.y,
					pane.bitangentExtent.z, 0.0F),
				DirectX::XMVectorSet(
					DirectX::XMVectorGetX(normal), DirectX::XMVectorGetY(normal),
					DirectX::XMVectorGetZ(normal), 0.0F),
				DirectX::XMVectorSet(0.0F, 0.0F, 0.0F, 1.0F) };
		}

		[[nodiscard]] inline bool InverseFinite(
			const DirectX::XMMATRIX& matrix,
			DirectX::XMMATRIX& inverse) noexcept
		{
			DirectX::XMVECTOR determinant{};
			inverse = DirectX::XMMatrixInverse(&determinant, matrix);
			DirectX::XMFLOAT4X4 stored{};
			DirectX::XMStoreFloat4x4(&stored, inverse);
			const float determinantValue = DirectX::XMVectorGetX(determinant);
			return Finite(determinantValue) && determinantValue != 0.0F &&
			       Finite(stored);
		}

		/**
		 * A projective transform is unchanged when every coefficient is multiplied
		 * by -1, but the pane shader deliberately requires positive clip W before
		 * dividing.  Skyrim's lowered hand-camera path can emit the equivalent
		 * all-negative homogeneous representative.  Canonicalize only when all four
		 * pane corners prove one strict negative-W half-space; a crossing or near-zero
		 * W remains unsafe and fails closed.
		 */
		[[nodiscard]] inline bool CanonicalizePanePositiveW(
			const MirrorPaneRenderer::PaneTransform& pane,
			DirectX::XMFLOAT4X4& projection) noexcept
		{
			const auto clipW = [&projection](
				const float tangentCoordinate,
				const float bitangentCoordinate,
				const MirrorPaneRenderer::PaneTransform& samplePane) noexcept {
				const DirectX::XMFLOAT3 relative{
					tangentCoordinate * samplePane.tangentExtent.x +
						bitangentCoordinate * samplePane.bitangentExtent.x,
					tangentCoordinate * samplePane.tangentExtent.y +
						bitangentCoordinate * samplePane.bitangentExtent.y,
					tangentCoordinate * samplePane.tangentExtent.z +
						bitangentCoordinate * samplePane.bitangentExtent.z
				};
				return relative.x * projection._14 +
				       relative.y * projection._24 +
				       relative.z * projection._34 + projection._44;
			};

			const float cornerW[4]{
				clipW(-1.0F, -1.0F, pane),
				clipW(+1.0F, -1.0F, pane),
				clipW(+1.0F, +1.0F, pane),
				clipW(-1.0F, +1.0F, pane)
			};
			bool everyPositive = true;
			bool everyNegative = true;
			for (const float value : cornerW) {
				if (!Finite(value))
					return false;
				everyPositive = everyPositive && value > kProjectiveWEpsilon;
				everyNegative = everyNegative && value < -kProjectiveWEpsilon;
			}
			if (everyPositive)
				return true;
			if (!everyNegative)
				return false;

			float* components = &projection._11;
			for (std::size_t index = 0; index < 16; ++index)
				components[index] = -components[index];
			return Finite(projection);
		}
	}

	/**
	 * A pane whose normal is within roughly three degrees of vertical has no
	 * stable in-plane up direction; LevelHandPaneTransform fails closed there and
	 * callers keep the authored basis.
	 */
	inline constexpr float kMinimumLevelUpProjection = 0.05F;

	/**
	 * Re-express a hand pane in a world-up-anchored in-plane basis.
	 *
	 * The authored tangent/bitangent follow every rotation of the pane node about
	 * its own normal.  A reflection glued to those axes therefore rolls the
	 * complete image (player and room together) whenever that node rolls, while
	 * the round rim gives no visual cue that it happened.  A physical mirror
	 * rotated about its normal never rotates its image.  Keep the centre, the
	 * normal, the owner, and both extent lengths; replace only the directions with
	 * world up projected onto the pane plane and the matching in-plane right axis.
	 * Because the authored contract is normal = tangent x bitangent, a level pane
	 * reproduces its authored basis exactly (bitangent x normal = tangent), so the
	 * accepted mirror handedness and crop are unchanged for the ordinary raised
	 * pose.
	 */
	/**
	 * Re-express a hand pane in an in-plane basis whose up is the projection of
	 * an explicit reference up (the capture camera's up) onto the pane plane.
	 * For the raised portrait the reference must be the portrait camera's up,
	 * which pitches rigidly with the held pane: the projected up then stays
	 * fixed relative to the authored basis under mouse pitch while a pane roll
	 * about its own normal (hand sway) still cannot roll the image.  World up
	 * is the wrong reference for a pane yawed to the side: its projection rolls
	 * as the pane pitches, which the owner saw as the reflection rotating
	 * left-to-right while moving the mouse up (V109/V110).
	 */
	[[nodiscard]] inline bool AnchorHandPaneTransform(
		const MirrorPaneRenderer::PaneTransform& pane,
		const DirectX::XMFLOAT3& upReference,
		MirrorPaneRenderer::PaneTransform& output) noexcept
	{
		output = {};
		if (!Detail::Finite(upReference.x) || !Detail::Finite(upReference.y) ||
			!Detail::Finite(upReference.z)) {
			return false;
		}
		DirectX::XMVECTOR normal{};
		if (!Detail::NonDegenerateBasis(pane, normal))
			return false;
		normal = DirectX::XMVector3Normalize(normal);
		const auto tangent = DirectX::XMLoadFloat3(&pane.tangentExtent);
		const auto bitangent = DirectX::XMLoadFloat3(&pane.bitangentExtent);
		const float tangentLength =
			DirectX::XMVectorGetX(DirectX::XMVector3Length(tangent));
		const float bitangentLength =
			DirectX::XMVectorGetX(DirectX::XMVector3Length(bitangent));
		const auto worldUp = DirectX::XMVector3Normalize(
			DirectX::XMLoadFloat3(&upReference));
		if (!Detail::Finite(DirectX::XMVectorGetX(worldUp)))
			return false;
		auto levelUp = DirectX::XMVectorSubtract(
			worldUp,
			DirectX::XMVectorScale(
				normal,
				DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldUp, normal))));
		const float levelUpLength =
			DirectX::XMVectorGetX(DirectX::XMVector3Length(levelUp));
		if (!Detail::Finite(tangentLength) || !Detail::Finite(bitangentLength) ||
			!Detail::Finite(levelUpLength) ||
			levelUpLength < kMinimumLevelUpProjection) {
			return false;
		}
		levelUp = DirectX::XMVectorScale(levelUp, 1.0F / levelUpLength);
		const auto levelRight = DirectX::XMVector3Normalize(
			DirectX::XMVector3Cross(levelUp, normal));
		MirrorPaneRenderer::PaneTransform leveled{};
		leveled.center = pane.center;
		leveled.owner = pane.owner;
		DirectX::XMStoreFloat3(
			&leveled.tangentExtent,
			DirectX::XMVectorScale(levelRight, tangentLength));
		DirectX::XMStoreFloat3(
			&leveled.bitangentExtent,
			DirectX::XMVectorScale(levelUp, bitangentLength));
		DirectX::XMVECTOR leveledNormal{};
		if (!Detail::NonDegenerateBasis(leveled, leveledNormal))
			return false;
		output = leveled;
		return true;
	}

	/** World-up anchoring; retained for the policy tests and future callers. */
	[[nodiscard]] inline bool LevelHandPaneTransform(
		const MirrorPaneRenderer::PaneTransform& pane,
		MirrorPaneRenderer::PaneTransform& output) noexcept
	{
		return AnchorHandPaneTransform(pane, { 0.0F, 0.0F, 1.0F }, output);
	}

	/**
	 * Re-express a retained capture-time reflected projection in the current live
	 * pane basis.  For row-vector DirectXMath convention this maps live pane-local
	 * coordinates into the captured pane basis before applying the captured VP:
	 *
	 *     live offset * inverse(live basis) * captured basis * captured VP
	 *
	 * The live center and both authored extent endpoints therefore receive exactly
	 * the captured center/tangent/bitangent clip crop.  The resulting inverse maps
	 * into this synthetic affine pane frame, not back into capture-world space.
	 * Callers must therefore disable reflected-depth motion reconstruction for a
	 * retargeted frame; the hand direct-delivery path enforces that by dropping its
	 * ephemeral depth SRV and supplying neither a motion RTV nor motion history.
	 */
	[[nodiscard]] inline bool RetargetReflectedProjectionToLivePane(
		const MirrorPaneRenderer::PublishedFrame& capturedFrame,
		const MirrorPaneRenderer::PaneTransform& capturedPane,
		const MirrorPaneRenderer::PaneTransform& livePane,
		MirrorPaneRenderer::PublishedFrame& output) noexcept
	{
		output = {};
		namespace Bridge = HandMirrorRuntimeBridgePolicy;
		if (!capturedFrame.valid ||
			!Bridge::IsValidOwner(capturedFrame.owner) ||
			capturedFrame.owner.kind != Bridge::OwnerKind::kHand ||
			!Bridge::SameOwner(capturedFrame.owner, capturedPane.owner) ||
			!Bridge::SameOwner(capturedFrame.owner, livePane.owner) ||
			!Detail::Finite(capturedFrame.reflectedOrigin) ||
			!Detail::Finite(capturedFrame.reflectedViewProjection)) {
			return false;
		}

		DirectX::XMVECTOR capturedNormal{};
		DirectX::XMVECTOR liveNormal{};
		if (!Detail::NonDegenerateBasis(capturedPane, capturedNormal) ||
			!Detail::NonDegenerateBasis(livePane, liveNormal)) {
			return false;
		}

		const auto capturedBasis =
			Detail::BasisRows(capturedPane, capturedNormal);
		const auto liveBasis = Detail::BasisRows(livePane, liveNormal);
		DirectX::XMMATRIX inverseLive{};
		if (!Detail::InverseFinite(liveBasis, inverseLive))
			return false;

		auto liveToCaptured = DirectX::XMMatrixMultiply(inverseLive, capturedBasis);
		liveToCaptured.r[3] = DirectX::XMVectorSet(
			capturedPane.center.x - capturedFrame.reflectedOrigin.x,
			capturedPane.center.y - capturedFrame.reflectedOrigin.y,
			capturedPane.center.z - capturedFrame.reflectedOrigin.z, 1.0F);
		const auto retargeted = DirectX::XMMatrixMultiply(
			liveToCaptured,
			DirectX::XMLoadFloat4x4(
				&capturedFrame.reflectedViewProjection));
		DirectX::XMFLOAT4X4 retargetedStored{};
		DirectX::XMStoreFloat4x4(&retargetedStored, retargeted);
		if (!Detail::CanonicalizePanePositiveW(livePane, retargetedStored))
			return false;
		const auto canonicalRetargeted =
			DirectX::XMLoadFloat4x4(&retargetedStored);
		DirectX::XMMATRIX inverseRetargeted{};
		if (!Detail::Finite(retargetedStored) ||
			!Detail::InverseFinite(canonicalRetargeted, inverseRetargeted)) {
			return false;
		}

		output = capturedFrame;
		output.reflectedViewProjection = retargetedStored;
		output.reflectedOrigin = livePane.center;
		return true;
	}

	// A predecessor capture is published against the current optical receipt.
	// Rebase its colour mapping to that receipt's pane before delivery performs
	// its usual current-to-live retarget. Keep the real capture origin as metadata;
	// the pane-affine projection still must not reconstruct world-motion depth.
	[[nodiscard]] inline bool RebaseCapturePaneProjection(
		const MirrorPaneRenderer::PublishedFrame& capturedFrame,
		const MirrorPaneRenderer::PaneTransform& capturedPane,
		const MirrorPaneRenderer::PaneTransform& publicationPane,
		DirectX::XMFLOAT4X4& projection) noexcept
	{
		projection = {};
		MirrorPaneRenderer::PublishedFrame rebased{};
		if (!RetargetReflectedProjectionToLivePane(
			capturedFrame, capturedPane, publicationPane, rebased))
			return false;
		const auto& origin = capturedFrame.reflectedOrigin;
		const auto& center = publicationPane.center;
		const auto translation = DirectX::XMMatrixTranslation(
			origin.x - center.x, origin.y - center.y, origin.z - center.z);
		DirectX::XMStoreFloat4x4(&projection, DirectX::XMMatrixMultiply(translation,
			DirectX::XMLoadFloat4x4(&rebased.reflectedViewProjection)));
		return Detail::Finite(projection);
	}
}
