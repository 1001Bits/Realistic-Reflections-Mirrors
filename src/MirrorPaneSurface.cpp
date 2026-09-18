#include "PCH.h"

#include "MirrorPaneSurface.h"
#include "MirrorProductIdentity.h"
#include "MirrorRecognitionSurfaceAPI.h"
#include "MirrorFramePublication.h"

#include <cstring>

namespace MirrorPaneSurface
{
	namespace
	{
		constexpr auto kPaneNameLiteral = MirrorProductIdentity::kPaneName;

		struct RawPane
		{
			RE::BSGeometry* geometry{ nullptr };
			RE::NiTransform world{};
			bool appCulled{ false };
			bool whiterunArch{ false };
		};

		struct SuppressionState
		{
			RE::NiPointer<RE::BSGeometry> geometry{};
			std::uint32_t formID{ 0 };
			std::uint64_t generation{ 0 };
			bool changedBit{ false };
		};

		stl::no_destructor<RE::BSFixedString> g_paneName{};
		stl::no_destructor<RE::BSFixedString> g_mirrorTag{};
		std::mutex g_suppressionLock{};
		stl::no_destructor<std::vector<SuppressionState>> g_suppressions{};

		[[nodiscard]] bool IsSupportedFlatRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			return (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
			       (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			       SupportedRuntimePolicy::IsExactVRRuntime();
		}

		[[nodiscard]] ResolveStatus LookupReferenceSEH(
			std::uint32_t formID,
			RE::TESObjectREFR*& output) noexcept
		{
			output = nullptr;
			__try {
				output = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
				return output ? ResolveStatus::kResolved : ResolveStatus::kReferenceMissing;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return ResolveStatus::kException;
			}
		}

		[[nodiscard]] ResolveStatus ReadRoot3DSEH(
			RE::TESObjectREFR* reference,
			RE::NiAVObject*& output) noexcept
		{
			output = nullptr;
			__try {
				output = reference ? reference->Get3D() : nullptr;
				return output ? ResolveStatus::kResolved : ResolveStatus::kRootMissing;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return ResolveStatus::kException;
			}
		}

		[[nodiscard]] ResolveStatus MatchesOwnedReferenceSEH(
			RE::TESObjectREFR* reference,
			std::uint32_t formID) noexcept
		{
			__try {
				const bool owned = reference &&
					reference->GetFormID() == formID &&
					MirrorRecognition::IsOwnedMirrorBase(reference->GetObjectReference());
				return owned ? ResolveStatus::kResolved : ResolveStatus::kReferenceNotOwned;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return ResolveStatus::kException;
			}
		}

		[[nodiscard]] ResolveStatus ResolveRawPaneSEH(
			RE::NiAVObject* root,
			RE::TESObjectREFR* reference,
			RawPane& output) noexcept
		{
			__try {
				if (!root || !reference)
					return ResolveStatus::kException;

				auto* object = root->GetObjectByName(g_paneName.get());
				if (!object)
					return ResolveStatus::kPaneMissing;
				auto* geometry = netimmerse_cast<RE::BSGeometry*>(object);
				if (!geometry)
					return ResolveStatus::kPaneWrongType;

				const char* rawName = geometry->name.c_str();
				if (!rawName || std::string_view{ rawName } != kPaneNameLiteral)
					return ResolveStatus::kPaneNameMismatch;

				auto* extra = geometry->GetExtraData(g_mirrorTag.get());
				if (!extra)
					return ResolveStatus::kPaneTagMissing;
				auto* integerTag = netimmerse_cast<RE::NiIntegerExtraData*>(extra);
				if (!integerTag)
					return ResolveStatus::kPaneTagWrongType;
				if (integerTag->value != MirrorProductIdentity::kPaneIntegerValue)
					return ResolveStatus::kPaneTagValueMismatch;

				auto* owner = geometry->GetUserData();
				if (!owner)
					return ResolveStatus::kPaneOwnerMissing;
				if (owner->GetFormID() != reference->GetFormID() ||
					owner->GetObjectReference() != reference->GetObjectReference() ||
					!MirrorRecognition::IsOwnedMirrorBase(owner->GetObjectReference()))
					return ResolveStatus::kPaneOwnerMismatch;

				output.geometry = geometry;
				output.world = geometry->world;
				output.appCulled = geometry->GetAppCulled();
				// This branch has already verified the product's exact owned base.
				// Author-declared replacement contours take the NIF path above it.
				const auto local = reference->GetObjectReference()->GetLocalFormID();
				output.whiterunArch = local == 0x801 || local == 0x810;
				return ResolveStatus::kResolved;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				return ResolveStatus::kException;
			}
		}

		[[nodiscard]] bool IsFiniteTransform(const RE::NiTransform& transform) noexcept
		{
			const auto x = transform.rotate.GetVectorX();
			const auto y = transform.rotate.GetVectorY();
			const auto z = transform.rotate.GetVectorZ();
			return std::isfinite(transform.translate.x) && std::isfinite(transform.translate.y) &&
			       std::isfinite(transform.translate.z) && std::isfinite(transform.scale) &&
			       std::abs(transform.scale) > 1.0e-6F &&
			       std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z) &&
			       std::isfinite(y.x) && std::isfinite(y.y) && std::isfinite(y.z) &&
			       std::isfinite(z.x) && std::isfinite(z.y) && std::isfinite(z.z);
		}

		[[nodiscard]] bool ReadAppCulledSEH(RE::BSGeometry* geometry, bool& output) noexcept
		{
			__try {
				if (!geometry)
					return false;
				output = geometry->GetAppCulled();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool SetAppCulledSEH(RE::BSGeometry* geometry, bool value) noexcept
		{
			__try {
				if (!geometry)
					return false;
				geometry->SetAppCulled(value);
				return geometry->GetAppCulled() == value;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] RestoreResult RestoreLocked(SuppressionState& suppression) noexcept
		{
			if (suppression.geometry && suppression.changedBit) {
				bool appCulled = false;
				const bool read = ReadAppCulledSEH(suppression.geometry.get(), appCulled);
				if ((!read || appCulled) &&
					!SetAppCulledSEH(suppression.geometry.get(), false))
					return RestoreResult::kFailed;
				suppression = {};
				return RestoreResult::kRestored;
			}
			suppression = {};
			return RestoreResult::kNothingOwned;
		}
	}

	const char* ToString(ResolveStatus status) noexcept
	{
		switch (status) {
		case ResolveStatus::kResolved:
			return "resolved";
		case ResolveStatus::kInvalidFormID:
			return "invalid-form-id";
		case ResolveStatus::kBaseUnavailable:
			return "base-unavailable";
		case ResolveStatus::kReferenceMissing:
			return "reference-missing";
		case ResolveStatus::kReferenceNotOwned:
			return "reference-not-owned";
		case ResolveStatus::kRootMissing:
			return "root-missing";
		case ResolveStatus::kPaneMissing:
			return "pane-missing";
		case ResolveStatus::kPaneWrongType:
			return "pane-wrong-type";
		case ResolveStatus::kPaneNameMismatch:
			return "pane-name-mismatch";
		case ResolveStatus::kPaneTagMissing:
			return "pane-tag-missing";
		case ResolveStatus::kPaneTagWrongType:
			return "pane-tag-wrong-type";
		case ResolveStatus::kPaneTagValueMismatch:
			return "pane-tag-value-mismatch";
		case ResolveStatus::kPaneOwnerMissing:
			return "pane-owner-missing";
		case ResolveStatus::kPaneOwnerMismatch:
			return "pane-owner-mismatch";
		case ResolveStatus::kTransformInvalid:
			return "transform-invalid";
		case ResolveStatus::kException:
			return "exception";
		default:
			return "unknown";
		}
	}

	void OnDataLoaded() noexcept
	{
		if (!IsSupportedFlatRuntime())
			return;
		if (RestoreSuppression() == RestoreResult::kFailed)
			logger::critical("[RR][M5d] pane app-cull rollback remained pending at DataLoaded");
		try {
			g_paneName.get() = kPaneNameLiteral.data();
			g_mirrorTag.get() = MirrorProductIdentity::kPaneIntegerTag.data();
			auto* const preferredBase = MirrorRecognition::GetOwnedMirrorBase();
			logger::info(
				"[RR][M5c] pane surface using exact M2 base catalog: anyResolved={} preferredRuntimeFormID={:08X}",
				MirrorRecognition::HasOwnedMirrorBase(),
				preferredBase ? preferredBase->GetFormID() : 0);
		} catch (...) {
			logger::error("[RR][M5c] pane surface identity initialization failed");
		}
	}

	ResolveStatus Resolve(std::uint32_t formID, Snapshot& output) noexcept
	{
		try {
			output = {};
			if (formID == 0)
				return ResolveStatus::kInvalidFormID;
			if (!MirrorRecognition::HasOwnedMirrorBase() && !MirrorNifSurface::Enabled())
				return ResolveStatus::kBaseUnavailable;

			RE::TESObjectREFR* rawReference = nullptr;
			auto status = LookupReferenceSEH(formID, rawReference);
			if (status != ResolveStatus::kResolved)
				return status;
			RE::NiPointer<RE::TESObjectREFR> reference{ rawReference };
			MirrorNifSurface::Snapshot nif{};
			if (MirrorNifSurface::Read(reference.get(),nif)) {
				output.formID = formID;
				output.geometry = nif.geometry;
				output.world = nif.world;
				output.appCulled = nif.appCulled;
				output.nif = std::move(nif);
				return ResolveStatus::kResolved;
			}
			status = MatchesOwnedReferenceSEH(reference.get(), formID);
			if (status != ResolveStatus::kResolved)
				return status;

			RE::NiAVObject* rawRoot = nullptr;
			status = ReadRoot3DSEH(reference.get(), rawRoot);
			if (status != ResolveStatus::kResolved)
				return status;
			RE::NiPointer<RE::NiAVObject> root{ rawRoot };

			RawPane raw{};
			status = ResolveRawPaneSEH(root.get(), reference.get(), raw);
			if (status != ResolveStatus::kResolved)
				return status;
			if (!raw.geometry || !IsFiniteTransform(raw.world))
				return ResolveStatus::kTransformInvalid;

			output.formID = formID;
			output.geometry.reset(raw.geometry);
			output.world = raw.world;
			output.appCulled = raw.appCulled;
			output.whiterunArch = raw.whiterunArch;
			return ResolveStatus::kResolved;
		} catch (...) {
			output = {};
			return ResolveStatus::kException;
		}
	}

	SuppressResult Suppress(
		const Snapshot& pane,
		std::uint64_t candidateGeneration) noexcept
	{
		try {
			if (!pane.geometry || pane.formID == 0 || candidateGeneration == 0)
				return SuppressResult::kFailed;

			std::scoped_lock lock{ g_suppressionLock };
			SuppressionState* chosen = nullptr;
			const bool multiMirror = MirrorFramePublication::MultiMirrorEnabled();
			const auto capacity = multiMirror ? g_suppressions.get().size() :
				(std::min)(std::size_t{ 1 }, g_suppressions.get().size());
			for (std::size_t i = 0; i < capacity; ++i) {
				auto& candidate = g_suppressions.get()[i];
				if (candidate.formID == pane.formID) {
					chosen = &candidate;
					break;
				}
				if (!chosen && !candidate.geometry)
					chosen = &candidate;
			}
			if (!chosen && !multiMirror && capacity == 1)
				chosen = &g_suppressions.get()[0];
			if (!chosen) {
				// Allocate before taking ownership of any scene bit.
				g_suppressions.get().emplace_back();
				chosen = &g_suppressions.get().back();
			}
			auto& suppression = *chosen;
			// Serialize the final lifetime check with candidate invalidation's
			// RestoreSuppression call.  If removal wins first, stale delivery cannot
			// re-hide the pane; if suppression wins first, invalidation restores it
			// immediately after this lock is released.
			if (!MirrorRecognition::IsCandidateCurrent(
					pane.formID, candidateGeneration)) {
				return RestoreLocked(suppression) == RestoreResult::kFailed ?
				           SuppressResult::kFailed : SuppressResult::kCandidateInvalid;
			}
			const bool samePane = suppression.geometry.get() == pane.geometry.get() &&
				suppression.formID == pane.formID &&
				suppression.generation == candidateGeneration;
			if (!samePane) {
				const bool ownedPriorBit = suppression.changedBit;
				const auto restored = RestoreLocked(suppression);
				if (ownedPriorBit && restored != RestoreResult::kRestored)
					return SuppressResult::kFailed;
			}

			bool appCulled = false;
			if (!ReadAppCulledSEH(pane.geometry.get(), appCulled))
				return SuppressResult::kFailed;

			if (samePane && appCulled)
				return SuppressResult::kMaintained;

			suppression.geometry = pane.geometry;
			suppression.formID = pane.formID;
			suppression.generation = candidateGeneration;
			suppression.changedBit = false;
			if (appCulled)
				return SuppressResult::kAlreadyCulled;

			// Claim conservative rollback ownership before the write.  The setter
			// verifies by reading afterward, so a false return can mean the write
			// succeeded and only verification faulted.  Never discard the retained
			// pane in that ambiguous state.
			suppression.changedBit = true;
			if (!SetAppCulledSEH(pane.geometry.get(), true)) {
				(void) RestoreLocked(suppression);
				return SuppressResult::kFailed;
			}
			return SuppressResult::kSuppressed;
		} catch (...) {
			return SuppressResult::kFailed;
		}
	}

	RestoreResult RestoreSuppression(std::uint32_t formID) noexcept
	{
		try {
			std::scoped_lock lock{ g_suppressionLock };
			RestoreResult result = RestoreResult::kNothingOwned;
			for (auto& suppression : g_suppressions.get()) {
				if (formID != 0 && suppression.formID != formID)
					continue;
				const auto restored = RestoreLocked(suppression);
				if (restored == RestoreResult::kFailed ||
					(result != RestoreResult::kFailed && restored == RestoreResult::kRestored))
					result = restored;
			}
			return result;
		} catch (...) {
			return RestoreResult::kFailed;
		}
	}
}
