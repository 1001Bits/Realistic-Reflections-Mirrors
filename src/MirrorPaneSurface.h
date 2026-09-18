#pragma once

#include <cstdint>

#include <RE/B/BSGeometry.h>
#include "MirrorNifSurface.h"
#include "MirrorBuiltInContour.h"

namespace MirrorPaneSurface
{
	/** Retained, value-stable view of the authored pane for one owned mirror reference. */
	struct Snapshot
	{
		std::uint32_t formID{ 0 };
		RE::NiPointer<RE::BSGeometry> geometry{};
		RE::NiTransform world{};
		bool appCulled{ false };
		MirrorNifSurface::Snapshot nif{};
		bool whiterunArch{ false };
		[[nodiscard]] std::span<const DirectX::XMFLOAT2> DrawTriangles() const noexcept
		{
			if (nif.geometry) return nif.triangles;
			return whiterunArch ? std::span<const DirectX::XMFLOAT2>{MirrorBuiltInContour::kWhiterunArch} :
				std::span<const DirectX::XMFLOAT2>{};
		}
	};

	enum class ResolveStatus : std::uint8_t
	{
		kResolved,
		kInvalidFormID,
		kBaseUnavailable,
		kReferenceMissing,
		kReferenceNotOwned,
		kRootMissing,
		kPaneMissing,
		kPaneWrongType,
		kPaneNameMismatch,
		kPaneTagMissing,
		kPaneTagWrongType,
		kPaneTagValueMismatch,
		kPaneOwnerMissing,
		kPaneOwnerMismatch,
		kTransformInvalid,
		kException
	};

	[[nodiscard]] const char* ToString(ResolveStatus status) noexcept;

	enum class SuppressResult
	{
		kFailed,
		kCandidateInvalid,
		kAlreadyCulled,
		kSuppressed,
		kMaintained
	};

	enum class RestoreResult
	{
		kNothingOwned,
		kRestored,
		kFailed
	};

	/** Reuse M2's exact legacy/clean STAT allowlist and intern the pane/tag names. */
	void OnDataLoaded() noexcept;

	/**
	 * Resolve one current reference by FormID and retain only its exact allowlisted,
	 * directly tagged TrueMirror:0 geometry. No name-only pane is eligible.
	 */
	[[nodiscard]] ResolveStatus Resolve(
		std::uint32_t formID,
		Snapshot& output) noexcept;

	/** Persistently app-cull a validated pane after a matching custom draw succeeds. */
	[[nodiscard]] SuppressResult Suppress(
		const Snapshot& pane,
		std::uint64_t candidateGeneration) noexcept;

	/** Restore only an app-cull bit which this module changed, then release the pane. */
	[[nodiscard]] RestoreResult RestoreSuppression(std::uint32_t formID = 0) noexcept;
}
