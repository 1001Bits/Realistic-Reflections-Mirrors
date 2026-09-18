#include "PCH.h"
#include "MirrorCaptureWorkPolicy.h"
#include "MirrorFleetPolicy.h"
#include "MirrorContentProfile.h"

#include "MirrorFramePublication.h"
#include "MirrorPaneNames.h"
#include "MirrorNifSurface.h"
#include "MirrorTextureSetSurface.h"
#include "MirrorSelectionPolicy.h"
#include "MirrorsOfSkyrimShadowMapBypass.h"
#include "MirrorActivation.h"
#include "MirrorsOfSkyrimPaneDelivery.h"
#include "MirrorsOfSkyrimRecognition.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MirrorRecognition
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kDiagnosticPeriod = std::chrono::seconds(10);
		constexpr std::uint32_t kHookClockCheckMask = 0xFFF;
		constexpr float kEpsilon = 1.0e-6F;
		constexpr std::string_view kPaneName = "TrueMirror:0";
		constexpr std::string_view kPaneTag = "TrueMirror";
		constexpr std::int32_t kPaneTagValue = 1;
		constexpr std::array<std::uint32_t, 10> kStandingMirrorLocalFormIDs{
			0x800, 0x801, 0x802, 0x803, 0x804,
			0x80F, 0x810, 0x811, 0x812, 0x813
		};

		static_assert(sizeof(RE::TESBoundObject::BOUND_DATA) == 0xC);
		static_assert(sizeof(RE::NiTransform) == 0x34);
		static_assert(sizeof(RE::BSRenderPass) == 0x48);

		struct DetectionCounters
		{
			std::atomic<std::uint64_t> scans{ 0 };
			std::atomic<std::uint64_t> referencesEnumerated{ 0 };
			std::atomic<std::uint64_t> baseMatches{ 0 };
			std::atomic<std::uint64_t> threeDReady{ 0 };
			std::atomic<std::uint64_t> threeDMissing{ 0 };
			std::atomic<std::uint64_t> candidatesDerived{ 0 };
			std::atomic<std::uint64_t> candidatesRejected{ 0 };
			std::atomic<std::uint64_t> candidateRevisions{ 0 };
			std::atomic<std::uint64_t> registryCapacityRejects{ 0 };
			std::atomic<std::uint64_t> deletedRejects{ 0 };
			std::atomic<std::uint64_t> disabledRejects{ 0 };
			std::atomic<std::uint64_t> unloadedRejects{ 0 };
			std::atomic<std::uint64_t> detachedRejects{ 0 };
			std::atomic<std::uint64_t> locationRejects{ 0 };
			std::atomic<std::uint64_t> viewSamples{ 0 };
			std::atomic<std::uint64_t> viewUsableNow{ 0 };
			std::atomic<std::uint64_t> viewRejectedNow{ 0 };
			std::atomic<std::uint64_t> lightingSetupCalls{ 0 };
			std::atomic<std::uint64_t> paneDraws{ 0 };
			std::atomic<std::uint64_t> taggedPaneDraws{ 0 };
			std::atomic<std::uint64_t> namedPaneDraws{ 0 };
			std::atomic<std::uint64_t> ownedPaneDraws{ 0 };
			std::atomic<std::uint64_t> reflectionRenders{ 0 };
			std::atomic<std::uint64_t> activeCandidates{ 0 };
			std::atomic<std::uint64_t> mainWorldPaneVisibility{ 0 };
			std::atomic<std::uint64_t> visibilitySelections{ 0 };
			std::atomic<std::uint64_t> unseenSelectionRejects{ 0 };
			std::atomic<std::uint64_t> ownershipLeaseHolds{ 0 };
			std::atomic<std::uint64_t> validatedVisibleContinuityRenewals{ 0 };
			std::atomic<std::uint64_t> validatedVisibleContinuityLeaseSelections{ 0 };
			std::atomic<std::uint64_t> ownerKeeps{ 0 };
			std::atomic<std::uint64_t> ownerSwitches{ 0 };
			std::atomic<std::uint64_t> unrelatedMutationsPreserved{ 0 };
		};

		struct OrientedBounds
		{
			RE::NiPointer<RE::BSGeometry> nifPane{};
			std::uint64_t nifSignature{ 0 };
			DirectX::XMFLOAT3 localCenter{};
			DirectX::XMFLOAT3 worldCenter{};
			std::array<DirectX::XMFLOAT3, 3> worldAxes{};
			std::array<float, 3> localHalfExtents{};
			std::array<float, 3> worldHalfExtents{};
		};

		struct Candidate
		{
			RE::ObjectRefHandle handle{};
			RE::FormID formID{ 0 };
			std::uint64_t generation{ 0 };
			std::uintptr_t baseToken{ 0 };
			MirrorInteriorEligibility::CellIdentity parentCell{};
			OrientedBounds bounds{};
			std::int32_t normalAxis{ -1 };
			bool boundsReady{ false };
		};

		struct CandidateIdentity
		{
			RE::FormID formID{ 0 };
			std::uint64_t generation{ 0 };

			[[nodiscard]] friend bool operator==(
				const CandidateIdentity&,
				const CandidateIdentity&) noexcept = default;
		};

		struct TimedCandidateIdentity : CandidateIdentity
		{
			std::int64_t milliseconds{ 0 };
		};

		struct VisibilityObservation
		{
			std::uint64_t generation{ 0 };
			std::int64_t milliseconds{ 0 };
		};

		enum class BoundsResult
		{
			kReady,
			kMissing3D,
			kInvalidTransform
		};

		struct ParentLocationSnapshot
		{
			MirrorInteriorEligibility::CellIdentity cell{};
			MirrorInteriorEligibility::WorldspaceIdentity worldspace{};
			bool attached{ false };
			bool interior{ false };
		};

		struct FormIdentityDiagnostics
		{
			RE::FormID runtimeFormID{ 0 };
			RE::FormID localFormID{ 0 };
			RE::FormType formType{ RE::FormType::None };
			const RE::TESFile* sourceFile{ nullptr };
			bool exactSource{ false };
		};

		DetectionCounters g_counters{};
		// STAT for the house/catalog and preview mirrors, ACTI for the placed
		// mirrors (V157: native "E) Store Mirror" activation prompt).
		std::array<RE::TESBoundObject*, kStandingMirrorLocalFormIDs.size()>
			g_mirrorBases{};
		stl::no_destructor<RE::BSFixedString> g_mirrorTag{};
		std::mutex g_candidateLock{};
		std::unordered_map<RE::FormID, Candidate> g_candidates{};
		std::unordered_map<RE::FormID, VisibilityObservation>
			g_mainWorldPaneVisibleAt{};
		std::uint64_t g_lastCandidateGeneration{ 0 };
		std::atomic<std::int64_t> g_lastPeriodicLogMilliseconds{ 0 };
		std::atomic<std::uint32_t> g_hookClockDivider{ 0 };
		std::mutex g_ownerLock{};
		CandidateIdentity g_stickyActiveMirror{};
		TimedCandidateIdentity g_lastDeliveryOwner{};
		TimedCandidateIdentity g_lastValidatedVisibleContinuityOwner{};
		std::atomic_bool g_mainWorldVisibilityObserved{ false };
		thread_local bool g_lastSelectionDeferredForOwnerLease{ false };
		std::atomic<std::uint32_t> g_activeSwitchLogBudget{ 128 };
		bool g_eventsRegistered = false;
		bool g_lightingObserverInstalled = false;
		std::atomic_bool g_mirrorSelectionEnabled{ false };
		std::atomic_bool g_loadTransitionSuspended{ false };
		std::atomic<std::uint64_t> g_loadEpoch{ 0 };

		[[nodiscard]] bool SuppressWorkDuringLoad() noexcept
		{
			return g_loadTransitionSuspended.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool Finite(float value) noexcept
		{
			return std::isfinite(value);
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT3& value) noexcept
		{
			return Finite(value.x) && Finite(value.y) && Finite(value.z);
		}

		[[nodiscard]] float Dot(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right) noexcept
		{
			return left.x * right.x + left.y * right.y + left.z * right.z;
		}

		[[nodiscard]] DirectX::XMFLOAT3 Add(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right) noexcept
		{
			return { left.x + right.x, left.y + right.y, left.z + right.z };
		}

		[[nodiscard]] DirectX::XMFLOAT3 Subtract(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right) noexcept
		{
			return { left.x - right.x, left.y - right.y, left.z - right.z };
		}

		[[nodiscard]] DirectX::XMFLOAT3 Scale(
			const DirectX::XMFLOAT3& value,
			float scale) noexcept
		{
			return { value.x * scale, value.y * scale, value.z * scale };
		}

		[[nodiscard]] DirectX::XMFLOAT3 Cross(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right) noexcept
		{
			return {
				left.y * right.z - left.z * right.y,
				left.z * right.x - left.x * right.z,
				left.x * right.y - left.y * right.x
			};
		}

		[[nodiscard]] bool Normalize(DirectX::XMFLOAT3& value) noexcept
		{
			if (!Finite(value))
				return false;
			const float lengthSquared = Dot(value, value);
			if (!Finite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon)
				return false;
			value = Scale(value, 1.0F / std::sqrt(lengthSquared));
			return true;
		}

		[[nodiscard]] bool NormalizePlane(Plane& plane) noexcept
		{
			if (!Finite(plane.distance))
				return false;
			const float lengthSquared = Dot(plane.normal, plane.normal);
			if (!Finite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon)
				return false;
			const float inverseLength = 1.0F / std::sqrt(lengthSquared);
			plane.normal = Scale(plane.normal, inverseLength);
			plane.distance *= inverseLength;
			return Finite(plane.normal) && Finite(plane.distance);
		}

		[[nodiscard]] bool BuildOrientedBounds(
			const std::array<std::int16_t, 3>& minimumBounds,
			const std::array<std::int16_t, 3>& maximumBounds,
			const DirectX::XMFLOAT3& translation,
			const std::array<DirectX::XMFLOAT3, 3>& rotationColumns,
			float scale,
			OrientedBounds& output) noexcept
		{
			output = {};
			if (!Finite(translation) || !Finite(scale) || scale <= kEpsilon)
				return false;

			auto axes = rotationColumns;
			for (auto& axis : axes) {
				if (!Normalize(axis))
					return false;
			}
			constexpr float kMaximumAxisDot = 1.0e-3F;
			if (std::abs(Dot(axes[0], axes[1])) > kMaximumAxisDot ||
				std::abs(Dot(axes[0], axes[2])) > kMaximumAxisDot ||
				std::abs(Dot(axes[1], axes[2])) > kMaximumAxisDot) {
				return false;
			}

			std::array<float, 3> localCenter{};
			std::array<float, 3> localHalfExtents{};
			for (std::size_t axis = 0; axis < 3; ++axis) {
				const float minimum = static_cast<float>(minimumBounds[axis]);
				const float maximum = static_cast<float>(maximumBounds[axis]);
				if (!(maximum > minimum))
					return false;
				localCenter[axis] = (minimum + maximum) * 0.5F;
				localHalfExtents[axis] = (maximum - minimum) * 0.5F;
			}

			DirectX::XMFLOAT3 worldCenter = translation;
			for (std::size_t axis = 0; axis < 3; ++axis) {
				worldCenter = Add(
					worldCenter, Scale(axes[axis], localCenter[axis] * scale));
			}
			if (!Finite(worldCenter))
				return false;

			std::array<float, 3> worldHalfExtents{};
			for (std::size_t axis = 0; axis < 3; ++axis) {
				worldHalfExtents[axis] = localHalfExtents[axis] * scale;
				if (!Finite(worldHalfExtents[axis]))
					return false;
			}

			output.localCenter = {
				localCenter[0], localCenter[1], localCenter[2] };
			output.worldCenter = worldCenter;
			output.worldAxes = axes;
			output.localHalfExtents = localHalfExtents;
			output.worldHalfExtents = worldHalfExtents;
			return true;
		}

		[[nodiscard]] bool SelectPlaneFromBasis(
			const OrientedBounds& bounds,
			const DirectX::XMFLOAT3& eye,
			PlaneSelection& output) noexcept
		{
			output = {};
			if (!Finite(bounds.worldCenter) || !Finite(eye))
				return false;

			auto axes = bounds.worldAxes;
			for (auto& axis : axes) {
				if (!Normalize(axis))
					return false;
			}

			DirectX::XMFLOAT3 toEye = Subtract(eye, bounds.worldCenter);
			const bool hasEyeDirection = Normalize(toEye);
			if (bounds.nifPane) {
				// The authored plane is explicit and one-sided; neither OBND axis
				// guesses nor automatic facing flips apply to a prepared NIF.
				output.plane = { axes[0], Dot(axes[0],bounds.worldCenter) };
				output.center = bounds.worldCenter;
				output.authoredNormal = axes[0];
				output.tangent = axes[1];
				output.bitangent = axes[2];
				output.normalAxis = 0;
				output.facingCosine = hasEyeDirection ? Dot(axes[0],toEye) : 0.0f;
				output.confidence = 1.0f;
				output.viewOriented = hasEyeDirection;
				output.renderable = hasEyeDirection && output.facingCosine >= 0.02f;
				return NormalizePlane(output.plane);
			}
			const bool extentsValid =
				Finite(bounds.worldHalfExtents[0]) &&
				bounds.worldHalfExtents[0] > kEpsilon &&
				Finite(bounds.worldHalfExtents[1]) &&
				bounds.worldHalfExtents[1] > kEpsilon &&
				Finite(bounds.worldHalfExtents[2]) &&
				bounds.worldHalfExtents[2] > kEpsilon;
			if (!extentsValid)
				return false;

			std::array<std::int32_t, 3> order{ 0, 1, 2 };
			std::sort(order.begin(), order.end(), [&](std::int32_t left, std::int32_t right) {
				return bounds.worldHalfExtents[left] < bounds.worldHalfExtents[right];
			});
			const float thickness = bounds.worldHalfExtents[order[0]];
			const float nextExtent = bounds.worldHalfExtents[order[1]];
			const float ratio = thickness / nextExtent;
			constexpr float kMaximumThicknessRatio = 0.35F;
			if (!Finite(ratio) || ratio > kMaximumThicknessRatio)
				return false;
			const std::int32_t normalAxis = order[0];
			const float shapeConfidence = 1.0F - ratio;

			const DirectX::XMFLOAT3 authoredNormal = axes[normalAxis];
			DirectX::XMFLOAT3 normal = authoredNormal;
			std::int32_t tangentAxis = (normalAxis + 1) % 3;
			const std::int32_t otherAxis = (normalAxis + 2) % 3;
			if (bounds.worldHalfExtents[otherAxis] >
				bounds.worldHalfExtents[tangentAxis]) {
				tangentAxis = otherAxis;
			}

			DirectX::XMFLOAT3 tangent = Subtract(
				axes[tangentAxis],
				Scale(normal, Dot(axes[tangentAxis], normal)));
			if (!Normalize(tangent)) {
				tangentAxis = tangentAxis == otherAxis ?
					(normalAxis + 1) % 3 : otherAxis;
				tangent = Subtract(
					axes[tangentAxis],
					Scale(normal, Dot(axes[tangentAxis], normal)));
				if (!Normalize(tangent))
					return false;
			}
			DirectX::XMFLOAT3 bitangent = Cross(normal, tangent);
			if (!Normalize(bitangent))
				return false;
			const std::int32_t remainingAxis = 3 - normalAxis - tangentAxis;
			if (remainingAxis >= 0 && remainingAxis < 3 &&
				Dot(bitangent, axes[remainingAxis]) < 0.0F) {
				tangent = Scale(tangent, -1.0F);
				bitangent = Scale(bitangent, -1.0F);
			}

			float facingCosine = 0.0F;
			bool viewOriented = false;
			if (hasEyeDirection) {
				const float signedFacing = Dot(normal, toEye);
				if (!Finite(signedFacing))
					return false;
				if (signedFacing < 0.0F)
					normal = Scale(normal, -1.0F);
				facingCosine = std::abs(signedFacing);
				viewOriented = true;
			}

			output.plane.normal = normal;
			output.plane.distance = Dot(normal, bounds.worldCenter);
			output.center = bounds.worldCenter;
			output.authoredNormal = authoredNormal;
			output.tangent = tangent;
			output.bitangent = bitangent;
			output.normalAxis = normalAxis;
			output.facingCosine = facingCosine;
			output.confidence = std::clamp(
				0.65F * shapeConfidence + 0.35F * facingCosine, 0.0F, 1.0F);
			output.viewOriented = viewOriented;
			output.renderable = viewOriented && facingCosine >= 0.02F;
			return NormalizePlane(output.plane);
		}

		[[nodiscard]] bool ExactlyEqual(float left, float right) noexcept
		{
			return Finite(left) && Finite(right) && left == right;
		}

		[[nodiscard]] bool ExactlyEqual(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right) noexcept
		{
			return ExactlyEqual(left.x, right.x) &&
			       ExactlyEqual(left.y, right.y) &&
			       ExactlyEqual(left.z, right.z);
		}

		[[nodiscard]] bool ExactlyEqual(
			const std::array<float, 3>& left,
			const std::array<float, 3>& right) noexcept
		{
			for (std::size_t index = 0; index < left.size(); ++index) {
				if (!ExactlyEqual(left[index], right[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool SameRenderableBounds(
			const OrientedBounds& left,
			const OrientedBounds& right) noexcept
		{
			if (left.nifPane.get() != right.nifPane.get() || left.nifSignature != right.nifSignature ||
				!ExactlyEqual(left.localCenter, right.localCenter) ||
				!ExactlyEqual(left.worldCenter, right.worldCenter) ||
				!ExactlyEqual(left.localHalfExtents, right.localHalfExtents) ||
				!ExactlyEqual(left.worldHalfExtents, right.worldHalfExtents)) {
				return false;
			}
			for (std::size_t index = 0; index < left.worldAxes.size(); ++index) {
				if (!ExactlyEqual(left.worldAxes[index], right.worldAxes[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool BoundsRequireGenerationAdvance(
			bool previousReady,
			const OrientedBounds& previousBounds,
			bool currentReady,
			const OrientedBounds& currentBounds) noexcept
		{
			if (previousReady != currentReady)
				return true;
			return previousReady &&
			       !SameRenderableBounds(previousBounds, currentBounds);
		}

		[[nodiscard]] char LowerASCII(char value) noexcept
		{
			return value >= 'A' && value <= 'Z' ?
				static_cast<char>(value + ('a' - 'A')) : value;
		}

		[[nodiscard]] bool FilenameEquals(
			std::string_view left,
			std::string_view right) noexcept
		{
			if (left.size() != right.size())
				return false;
			for (std::size_t index = 0; index < left.size(); ++index) {
				if (LowerASCII(left[index]) != LowerASCII(right[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool IsStandingMirrorLocalFormID(
			std::uint32_t localFormID) noexcept
		{
			return std::ranges::find(kStandingMirrorLocalFormIDs, localFormID) !=
			       kStandingMirrorLocalFormIDs.end();
		}

		[[nodiscard]] RE::TESBoundObject* ResolvedBase(
			std::uint32_t localFormID) noexcept
		{
			for (std::size_t index = 0; index < kStandingMirrorLocalFormIDs.size();
				 ++index) {
				if (kStandingMirrorLocalFormIDs[index] == localFormID)
					return g_mirrorBases[index];
			}
			return nullptr;
		}

		[[nodiscard]] bool AnyMirrorBaseResolved() noexcept
		{
			return std::ranges::any_of(
				g_mirrorBases, [](const auto* base) { return base != nullptr; });
		}

		[[nodiscard]] bool IdentifyMirrorBase(
			const RE::TESBoundObject* base) noexcept
		{
			if (!base)
				return false;
			std::size_t matches = 0;
			for (const auto* candidate : g_mirrorBases) {
				if (candidate == base)
					++matches;
			}
			return matches == 1;
		}

		[[nodiscard]] bool PublicMirrorHookRequested() noexcept
		{
			const auto prepared = MirrorActivation::GlobalLifecycle().Prepared();
			return prepared && !prepared->IsConflicted() &&
			       MirrorActivation::Contains(
				       prepared->Origins(),
				       MirrorActivation::Origin::kPublicMirrorsOfSkyrim) &&
			       MirrorActivation::ContainsAll(
				       prepared->Masks().HookRequest(),
				       MirrorActivation::kMirrorFeatures);
		}

		[[nodiscard]] bool LockCandidateMutexNoexcept() noexcept
		{
			try {
				g_candidateLock.lock();
				return true;
			} catch (...) {
				return false;
			}
		}

		[[nodiscard]] std::string_view FilenameOrNone(
			const RE::TESFile* file) noexcept
		{
			return file ? file->GetFilename() : std::string_view{ "<none>" };
		}

		[[nodiscard]] bool ReadFormIdentity(
			const RE::TESForm* form,
			FormIdentityDiagnostics& output) noexcept
		{
			output = {};
			if (!form)
				return false;
			output.runtimeFormID = form->GetFormID();
			output.formType = form->GetFormType();
			output.sourceFile = form->GetFile(0);
			if (output.sourceFile) {
				output.localFormID = form->GetLocalFormID();
				output.exactSource = FilenameEquals(
					output.sourceFile->GetFilename(), MirrorContentProfile::StandingPlugin(output.localFormID)) &&
					IsStandingMirrorLocalFormID(output.localFormID);
			}
			return true;
		}

		void LogFormIdentity(const char* label, const RE::TESForm* form)
		{
			FormIdentityDiagnostics identity{};
			if (!ReadFormIdentity(form, identity)) {
				logger::info(
					"[MirrorsOfSkyrim][Recognition][activation] {}: null", label);
				return;
			}
			logger::info(
				"[MirrorsOfSkyrim][Recognition][activation] {}: address=0x{:016X} "
				"runtime={:08X} type={}({:02X}) source={} local={:08X} exactSource={}",
				label, reinterpret_cast<std::uintptr_t>(form), identity.runtimeFormID,
				RE::FormTypeToString(identity.formType),
				static_cast<std::uint32_t>(identity.formType),
				FilenameOrNone(identity.sourceFile), identity.localFormID,
				identity.exactSource);
		}

		void LogStaticArrayDiagnostics(
			RE::TESDataHandler* dataHandler,
			std::uint32_t localFormID)
		{
			const auto kPluginName = MirrorContentProfile::StandingPlugin(localFormID);
			std::size_t inspected = 0;
			std::size_t exactSource = 0;
			std::size_t exactIdentity = 0;
			auto& statics = dataHandler->GetFormArray<RE::TESObjectSTAT>();
			for (auto* form : statics) {
				if (!form)
					continue;
				++inspected;
				const auto* source = form->GetFile(0);
				if (!source || !FilenameEquals(source->GetFilename(), kPluginName))
					continue;
				++exactSource;
				if (form->GetLocalFormID() != localFormID)
					continue;
				++exactIdentity;
				logger::info(
					"[MirrorsOfSkyrim][Recognition][activation] STAT-array exact hit: "
					"address=0x{:016X} runtime={:08X} source={} local={:08X}",
					reinterpret_cast<std::uintptr_t>(form), form->GetFormID(),
					FilenameOrNone(source), localFormID);
			}
			logger::info(
				"[MirrorsOfSkyrim][Recognition][activation] STAT-array scan: "
				"inspected={} plugin={} exactSource={} exactPluginLocal={} authoritative=false",
				inspected, kPluginName, exactSource, exactIdentity);
		}

		[[nodiscard]] RE::TESBoundObject* ResolveMirrorBaseWithDiagnostics(
			RE::TESDataHandler* dataHandler,
			std::uint32_t localFormID)
		{
			const auto kPluginName = MirrorContentProfile::StandingPlugin(localFormID);
			if (localFormID >= 0x80F && !MirrorContentProfile::AddonLoaded()) return nullptr;
			if (!dataHandler) {
				logger::error(
					"[MirrorsOfSkyrim][Recognition][activation] TESDataHandler is null at DataLoaded");
				return nullptr;
			}

			const auto* discovered = dataHandler->LookupModByName(kPluginName);
			const auto* loadedFull = dataHandler->LookupLoadedModByName(kPluginName);
			const auto* loadedLight = dataHandler->LookupLoadedLightModByName(kPluginName);
			logger::info(
				"[MirrorsOfSkyrim][Recognition][activation] loaded membership: "
				"discovered={} full={} light={} fullSame={} lightSame={} "
				"loadedFullCount={} loadedLightCount={}",
				discovered != nullptr, loadedFull != nullptr, loadedLight != nullptr,
				discovered && loadedFull == discovered,
				discovered && loadedLight == discovered,
				dataHandler->GetLoadedModCount(), dataHandler->GetLoadedLightModCount());

			if (!discovered) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition][activation] {} was not discovered",
					kPluginName);
				LogStaticArrayDiagnostics(dataHandler, localFormID);
				return nullptr;
			}

			const auto compileIndex = discovered->GetCompileIndex();
			const auto smallIndex = discovered->GetSmallFileCompileIndex();
			logger::info(
				"[MirrorsOfSkyrim][Recognition][activation] discovered file: name={} "
				"isLight={} compileIndex={:02X} smallFileCompileIndex={:03X} "
				"masterCount={} formCount={} lastError={} version={:.2f}",
				discovered->GetFilename(), discovered->IsLight(), compileIndex,
				smallIndex, discovered->masterCount, discovered->formCount,
				discovered->lastError.underlying(), discovered->version);

			if (compileIndex == 0xFF) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition][activation] {} is inactive: compileIndex=FF",
					kPluginName);
			} else if (discovered->IsLight() && !loadedLight) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition][activation] active light file is absent from the compiled collection");
			} else if (!discovered->IsLight() && !loadedFull) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition][activation] active full file is absent from the compiled collection");
			}

			const RE::FormID lookupRuntimeID =
				dataHandler->LookupFormID(localFormID, kPluginName);
			const RE::FormID directRuntimeID = compileIndex == 0xFF ? 0 :
				(static_cast<RE::FormID>(compileIndex) << 24) +
				(static_cast<RE::FormID>(smallIndex) << 12) + localFormID;
			// The file's own raw slot is its master count, not a fixed 0x01.
			const RE::FormID rawSelfFormID =
				(static_cast<RE::FormID>(discovered->masterCount & 0xFFU) << 24) |
				localFormID;
			const RE::FormID rawMappedRuntimeID =
				discovered->GetRuntimeFormID(rawSelfFormID);
			logger::info(
				"[MirrorsOfSkyrim][Recognition][activation] runtime IDs: local={:08X} "
				"LookupFormID={:08X} direct={:08X} rawSelf={:08X}->runtime={:08X} "
				"lookupEqualsDirect={} lookupEqualsRaw={}",
				localFormID, lookupRuntimeID, directRuntimeID, rawSelfFormID,
				rawMappedRuntimeID,
				lookupRuntimeID != 0 && lookupRuntimeID == directRuntimeID,
				lookupRuntimeID != 0 && lookupRuntimeID == rawMappedRuntimeID);

			auto* untyped = dataHandler->LookupForm(localFormID, kPluginName);
			LogFormIdentity("untyped LookupForm", untyped);
			RE::TESBoundObject* typed = dataHandler->LookupForm<RE::TESObjectSTAT>(
				localFormID, kPluginName);
			if (!typed) {
				typed = dataHandler->LookupForm<RE::TESObjectACTI>(
					localFormID, kPluginName);
			}
			logger::info(
				"[MirrorsOfSkyrim][Recognition][activation] typed STAT resolution: "
				"resolved={} matchesUntyped={}",
				typed != nullptr, typed && typed == untyped);
			if (typed)
				LogFormIdentity("typed STAT", typed);
			else if (untyped) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition][activation] local form resolved but is not STAT (actual type={})",
					RE::FormTypeToString(untyped->GetFormType()));
			}

			LogStaticArrayDiagnostics(dataHandler, localFormID);
			if (typed) {
				FormIdentityDiagnostics resolved{};
				if (!ReadFormIdentity(typed, resolved) || !resolved.exactSource ||
					resolved.localFormID != localFormID) {
					logger::critical(
						"[MirrorsOfSkyrim][Recognition][activation] typed lookup for {}:0x{:03X} lost exact source identity; rejected",
						kPluginName, localFormID);
					return nullptr;
				}
			}
			return typed;
		}

		[[nodiscard]] bool IsSupportedRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			return (REL::Module::IsSE() &&
					version == REL::Version{ 1, 5, 97, 0 }) ||
			       (REL::Module::IsAE() &&
					SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			       SupportedRuntimePolicy::IsExactVRRuntime();
		}

		[[nodiscard]] RE::NiAVObject* ReadRoot3DSEH(
			RE::TESObjectREFR* reference) noexcept
		{
			__try {
				return reference ? reference->Get3D() : nullptr;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		[[nodiscard]] bool MatchesOwnedBaseSEH(
			RE::TESObjectREFR* reference) noexcept
		{
			__try {
				return reference &&
				       (IdentifyMirrorBase(reference->GetObjectReference()) || MirrorNifSurface::Matches(reference));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] std::int64_t CurrentMilliseconds() noexcept
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				Clock::now().time_since_epoch()).count();
		}

		[[nodiscard]] MirrorSelectionPolicy::ReferenceStatus ReadReferenceStatusSEH(
			RE::TESObjectREFR* reference,
			const Candidate* expectedCandidate = nullptr) noexcept
		{
			MirrorSelectionPolicy::ReferenceObservation observation{};
			__try {
				observation.referencePresent = reference != nullptr;
				if (!reference)
					return MirrorSelectionPolicy::ClassifyReference(observation);
				auto* const base = reference->GetObjectReference();
				observation.baseMatches = MatchesOwnedBaseSEH(reference) &&
					(!expectedCandidate || reinterpret_cast<std::uintptr_t>(base) == expectedCandidate->baseToken);
				observation.deleted =
					reference->IsDeleted() || reference->IsMarkedForDeletion();
				observation.disabled = reference->IsDisabled();
				observation.threeDLoaded = reference->Is3DLoaded();
				observation.rootPresent = reference->Get3D() != nullptr;
				auto* const parentCell = reference->GetParentCell();
				observation.parentCellPresent = parentCell != nullptr;
				observation.parentCellAttached =
					parentCell && parentCell->IsAttached();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				observation = {};
			}
			return MirrorSelectionPolicy::ClassifyReference(observation);
		}

		void RecordReferenceReject(
			MirrorSelectionPolicy::ReferenceStatus status) noexcept
		{
			switch (status) {
			case MirrorSelectionPolicy::ReferenceStatus::kDeleted:
				g_counters.deletedRejects.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorSelectionPolicy::ReferenceStatus::kDisabled:
				g_counters.disabledRejects.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorSelectionPolicy::ReferenceStatus::kUnloaded:
			case MirrorSelectionPolicy::ReferenceStatus::kRootMissing:
				g_counters.unloadedRejects.fetch_add(1, std::memory_order_relaxed);
				break;
			case MirrorSelectionPolicy::ReferenceStatus::kParentCellMissing:
			case MirrorSelectionPolicy::ReferenceStatus::kParentCellDetached:
				g_counters.detachedRejects.fetch_add(1, std::memory_order_relaxed);
				break;
			default:
				break;
			}
		}

		[[nodiscard]] bool RemoveInsteadOfRetain(
			MirrorSelectionPolicy::ReferenceStatus status) noexcept
		{
			return MirrorSelectionPolicy::RequiresPermanentRemoval(status);
		}

		[[nodiscard]] bool CanUpsertCandidateLocked(RE::FormID formID) noexcept
		{
			const bool alreadyTracked = g_candidates.contains(formID);
			const auto admission = MirrorSelectionPolicy::ClassifyRegistryAdmission(
				g_candidates.size(), alreadyTracked, formID, g_candidates.max_size());
			if (admission == MirrorSelectionPolicy::RegistryAdmission::kInsert ||
				admission ==
					MirrorSelectionPolicy::RegistryAdmission::kUpdateExisting) {
				return true;
			}
			if (admission ==
				MirrorSelectionPolicy::RegistryAdmission::kRejectCapacity) {
				g_counters.registryCapacityRejects.fetch_add(
					1, std::memory_order_relaxed);
			}
			return false;
		}

		[[nodiscard]] bool ReadParentLocationSEH(
			RE::TESObjectREFR* reference,
			ParentLocationSnapshot& output) noexcept
		{
			output = {};
			__try {
				auto* parentCell = reference ? reference->GetParentCell() : nullptr;
				if (!parentCell)
					return false;
				output.cell.address = reinterpret_cast<std::uintptr_t>(parentCell);
				output.cell.formID = parentCell->GetFormID();
				output.attached = parentCell->IsAttached();
				output.interior = parentCell->IsInteriorCell();
				if (auto* worldspace = reference->GetWorldspace()) {
					output.worldspace.address =
						reinterpret_cast<std::uintptr_t>(worldspace);
					output.worldspace.formID = worldspace->GetFormID();
				}
				return output.cell.address != 0 && output.cell.formID != 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				return false;
			}
		}

		[[nodiscard]] bool ParentLocationMatches(
			const ParentLocationSnapshot& parent,
			MirrorInteriorEligibility::Status status,
			const MirrorInteriorEligibility::LocationIdentity& expected) noexcept
		{
			if (!parent.attached)
				return false;
			if (status == MirrorInteriorEligibility::Status::kInterior) {
				return parent.interior &&
				       MirrorInteriorEligibility::SameValidCell(
					       parent.cell, expected.playerCell);
			}
			if (status == MirrorInteriorEligibility::Status::kExterior) {
				return !parent.interior &&
				       MirrorInteriorEligibility::SameValidWorldspace(
					       parent.worldspace, expected.worldspace);
			}
			return false;
		}

		[[nodiscard]] MirrorInteriorEligibility::Status ReadCurrentLocationSEH(
			MirrorInteriorEligibility::LocationIdentity& output) noexcept
		{
			output = {};
			MirrorInteriorEligibility::Observation observation{};
			__try {
				auto* const world = RE::TES::GetSingleton();
				observation.worldAvailable = world != nullptr;
				auto* const player = RE::PlayerCharacter::GetSingleton();
				observation.playerAvailable = player != nullptr;

				auto* const worldInterior = world ? world->interiorCell : nullptr;
				observation.worldInteriorPresent = worldInterior != nullptr;
				if (worldInterior) {
					observation.worldInteriorFlag = worldInterior->IsInteriorCell();
					observation.worldInteriorAttached = worldInterior->IsAttached();
				}

				auto* const playerCell = player ? player->GetParentCell() : nullptr;
				observation.playerCellPresent = playerCell != nullptr;
				if (playerCell) {
					output.playerCell.address =
						reinterpret_cast<std::uintptr_t>(playerCell);
					output.playerCell.formID = playerCell->GetFormID();
					observation.playerCellInteriorFlag =
						playerCell->IsInteriorCell();
					observation.playerCellAttached = playerCell->IsAttached();
				}
				observation.sameCell =
					worldInterior && playerCell && worldInterior == playerCell;

				auto* const playerWorldspace =
					player ? player->GetWorldspace() : nullptr;
				observation.playerWorldSpacePresent = playerWorldspace != nullptr;
				if (playerWorldspace) {
					output.worldspace.address =
						reinterpret_cast<std::uintptr_t>(playerWorldspace);
					output.worldspace.formID = playerWorldspace->GetFormID();
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				return MirrorInteriorEligibility::Status::kUnavailable;
			}
			return MirrorInteriorEligibility::Classify(observation);
		}

		[[nodiscard]] bool ReadParentCellIdentitySEH(
			RE::TESObjectREFR* reference,
			MirrorInteriorEligibility::CellIdentity& output) noexcept
		{
			output = {};
			__try {
				auto* parentCell = reference ? reference->GetParentCell() : nullptr;
				if (!parentCell)
					return false;
				output.address = reinterpret_cast<std::uintptr_t>(parentCell);
				output.formID = parentCell->GetFormID();
				return output.address != 0 && output.formID != 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = {};
				return false;
			}
		}

		[[nodiscard]] bool CopyWorldTransformSEH(
			RE::NiAVObject* root,
			RE::NiTransform& output) noexcept
		{
			__try {
				if (!root)
					return false;
				output = root->world;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool ReadWorldTransform(
			RE::TESObjectREFR* reference,
			RE::NiTransform& output) noexcept
		{
			RE::NiPointer<RE::NiAVObject> root{ ReadRoot3DSEH(reference) };
			return root && CopyWorldTransformSEH(root.get(), output);
		}

		[[nodiscard]] bool ReadEyePositionSEH(RE::NiPoint3& output) noexcept
		{
			__try {
				if (auto* camera = RE::PlayerCamera::GetSingleton();
					camera && camera->cameraRoot) {
					output = camera->cameraRoot->world.translate;
					return std::isfinite(output.x) && std::isfinite(output.y) &&
					       std::isfinite(output.z);
				}
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					output = player->GetPosition();
					return std::isfinite(output.x) && std::isfinite(output.y) &&
					       std::isfinite(output.z);
				}
				return false;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] DirectX::XMFLOAT3 ToFloat3(
			const RE::NiPoint3& point) noexcept
		{
			return { point.x, point.y, point.z };
		}

		[[nodiscard]] bool SelectCandidateForEye(
			const OrientedBounds& bounds,
			const RE::NiPoint3* eye,
			PlaneSelection& selection) noexcept
		{
			return SelectPlaneFromBasis(
				bounds, eye ? ToFloat3(*eye) : bounds.worldCenter, selection);
		}

		[[nodiscard]] BoundsResult BuildBoundsForReference(
			RE::TESObjectREFR* reference,
			OrientedBounds& orientedBounds) noexcept
		{
			if (!reference)
				return BoundsResult::kInvalidTransform;
			MirrorNifSurface::Snapshot authored{};
			if (MirrorNifSurface::Read(reference,authored,false)) {
				orientedBounds = {};
				orientedBounds.nifPane = authored.geometry;
				orientedBounds.nifSignature = authored.signature;
				orientedBounds.localCenter = MirrorNifContract::At(authored.localPlane,0);
				orientedBounds.worldCenter = authored.plane.center;
				orientedBounds.worldAxes = { authored.plane.normal, authored.plane.tangent, authored.plane.bitangent };
				orientedBounds.localHalfExtents = { 0.001f, authored.localPlane[12], authored.localPlane[13] };
				orientedBounds.worldHalfExtents = { 0.001f, authored.plane.halfWidth, authored.plane.halfHeight };
				return BoundsResult::kReady;
			}
			auto* const mirrorBase = reference->GetObjectReference();
			if (!IdentifyMirrorBase(mirrorBase))
				return BoundsResult::kInvalidTransform;

			RE::NiTransform world{};
			if (!ReadWorldTransform(reference, world))
				return BoundsResult::kMissing3D;

			const auto& sourceBounds = mirrorBase->boundData;
			const std::array<std::int16_t, 3> minimum{
				sourceBounds.boundMin.x,
				sourceBounds.boundMin.y,
				sourceBounds.boundMin.z
			};
			const std::array<std::int16_t, 3> maximum{
				sourceBounds.boundMax.x,
				sourceBounds.boundMax.y,
				sourceBounds.boundMax.z
			};
			const std::array<DirectX::XMFLOAT3, 3> rotationColumns{
				ToFloat3(world.rotate.GetVectorX()),
				ToFloat3(world.rotate.GetVectorY()),
				ToFloat3(world.rotate.GetVectorZ())
			};
			return BuildOrientedBounds(
				minimum, maximum, ToFloat3(world.translate), rotationColumns,
				world.scale, orientedBounds) ?
				BoundsResult::kReady : BoundsResult::kInvalidTransform;
		}

		void RefreshViewCounters();

		void InvalidateCandidatePublicationAndDelivery() noexcept
		{
			MirrorFramePublication::Invalidate("recognition-candidate");
			MirrorPaneDelivery::OnCandidateInvalidated();
		}

		void InvalidateCandidateMutationIfOwned(RE::FormID formID, std::uint64_t epoch) noexcept
		{
			if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
				return;
			if (MirrorFramePublication::MultiMirrorEnabled() && formID != 0) {
				MirrorFramePublication::InvalidateCandidate(formID, "recognition-candidate");
				MirrorPaneDelivery::OnCandidateInvalidated(formID);
				return;
			}
			bool invalidate = true;
			try {
				std::scoped_lock lock{ g_ownerLock };
				invalidate = formID != 0 &&
				             formID == g_stickyActiveMirror.formID;
			} catch (...) {
				invalidate = true;
			}
			if (invalidate) {
				InvalidateCandidatePublicationAndDelivery();
			} else {
				g_counters.unrelatedMutationsPreserved.fetch_add(
					1, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] bool IsRecent(
			std::int64_t now,
			std::int64_t observation,
			std::int64_t leaseMilliseconds) noexcept
		{
			return observation > 0 && now >= observation &&
			       now - observation <= leaseMilliseconds;
		}

		void RecordMainWorldPaneVisibility(
			RE::FormID formID,
			RE::TESObjectREFR* exactOwner) noexcept
		{
			if (SuppressWorkDuringLoad() || formID == 0 || !exactOwner ||
				!MirrorPaneDelivery::IsInsideMainWorld()) {
				return;
			}
			try {
				std::scoped_lock lock{ g_candidateLock };
				const auto candidate = g_candidates.find(formID);
				if (SuppressWorkDuringLoad() || candidate == g_candidates.end() ||
					!g_mirrorSelectionEnabled.load(std::memory_order_acquire)) {
					return;
				}
				auto retained = candidate->second.handle.get();
				if (!retained || retained.get() != exactOwner)
					return;
				g_mainWorldPaneVisibleAt.insert_or_assign(
					formID,
					VisibilityObservation{
						candidate->second.generation, CurrentMilliseconds() });
				g_mainWorldVisibilityObserved.store(true, std::memory_order_release);
				g_counters.mainWorldPaneVisibility.fetch_add(
					1, std::memory_order_relaxed);
			} catch (...) {
			}
		}

		[[nodiscard]] bool PreserveOrAssignGenerationLocked(
			Candidate& candidate) noexcept
		{
			const auto existing = g_candidates.find(candidate.formID);
			const bool sameHandleLifetime = existing != g_candidates.end() &&
				existing->second.handle == candidate.handle &&
				existing->second.generation != 0;
			if (sameHandleLifetime) {
				const bool baseIdentityChanged =
					existing->second.baseToken != candidate.baseToken;
				const bool parentCellChanged =
					MirrorInteriorEligibility::RequiresCandidateGenerationAdvance(
						existing->second.parentCell, candidate.parentCell);
				if (!baseIdentityChanged && !parentCellChanged &&
					!BoundsRequireGenerationAdvance(
						existing->second.boundsReady, existing->second.bounds,
						candidate.boundsReady, candidate.bounds)) {
					candidate.generation = existing->second.generation;
					return false;
				}
				g_counters.candidateRevisions.fetch_add(
					1, std::memory_order_relaxed);
			}

			++g_lastCandidateGeneration;
			if (g_lastCandidateGeneration == 0)
				++g_lastCandidateGeneration;
			candidate.generation = g_lastCandidateGeneration;
			return true;
		}

		void RemoveCandidate(RE::FormID formID, std::uint64_t epoch)
		{
			bool removed = false;
			{
				std::scoped_lock lock{ g_candidateLock };
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return;
				removed = g_candidates.erase(formID) != 0;
				// The load boundary clears the entire map under this same mutex.
				g_mainWorldPaneVisibleAt.erase(formID);
				const auto remaining = g_candidates.size();
				g_counters.activeCandidates.store(
					remaining, std::memory_order_relaxed);
				if (remaining == 0) {
					g_counters.viewUsableNow.store(0, std::memory_order_relaxed);
					g_counters.viewRejectedNow.store(0, std::memory_order_relaxed);
				}
			}
			if (removed)
				InvalidateCandidateMutationIfOwned(formID, epoch);
		}

		void ClearCandidates()
		{
			bool changed = false;
			{
				std::scoped_lock lock{ g_ownerLock };
				changed = g_stickyActiveMirror.formID != 0;
				g_stickyActiveMirror = {};
				g_lastDeliveryOwner = {};
				g_lastValidatedVisibleContinuityOwner = {};
			}
			{
				std::scoped_lock lock{ g_candidateLock };
				changed = !g_candidates.empty() || changed;
				g_candidates.clear();
				g_mainWorldPaneVisibleAt.clear();
				g_counters.activeCandidates.store(0, std::memory_order_relaxed);
				g_counters.viewUsableNow.store(0, std::memory_order_relaxed);
				g_counters.viewRejectedNow.store(0, std::memory_order_relaxed);
			}
			g_mainWorldVisibilityObserved.store(false, std::memory_order_release);
			if (changed)
				InvalidateCandidatePublicationAndDelivery();
		}

		void ObserveMatchingReference(
			RE::TESObjectREFR* reference,
			const char* reason)
		{
			MirrorNifSurface::ValidationScope nifValidation{};
			const auto epoch = g_loadEpoch.load(std::memory_order_acquire);
			if (SuppressWorkDuringLoad() || !reference)
				return;
			auto* const mirrorBase = reference->GetObjectReference();
			if (!MatchesOwnedBaseSEH(reference))
				return;

			g_counters.baseMatches.fetch_add(1, std::memory_order_relaxed);
			const RE::FormID formID = reference->GetFormID();
			if (formID == 0)
				return;
			MirrorInteriorEligibility::CellIdentity parentCell{};
			(void)ReadParentCellIdentitySEH(reference, parentCell);
			OrientedBounds orientedBounds{};
			const auto referenceStatus = ReadReferenceStatusSEH(reference);
			if (referenceStatus !=
				MirrorSelectionPolicy::ReferenceStatus::kEligible) {
				RecordReferenceReject(referenceStatus);
			}
			if (RemoveInsteadOfRetain(referenceStatus)) {
				RemoveCandidate(formID, epoch);
				return;
			}
			if (MirrorSelectionPolicy::IsTransientLifecycleStatus(referenceStatus)) {
				bool preserved = false;
				{
					std::scoped_lock lock{ g_candidateLock };
					const auto existing = g_candidates.find(formID);
					preserved = existing != g_candidates.end() &&
						existing->second.handle == reference->GetHandle() &&
						existing->second.baseToken ==
							reinterpret_cast<std::uintptr_t>(mirrorBase);
				}
				if (preserved) {
					logger::info(
						"[MirrorsOfSkyrim][Recognition] ref {:08X} transient "
						"lifecycle status={} ({}) preserves its exact generation",
						formID, static_cast<std::uint32_t>(referenceStatus), reason);
					return;
				}
			}

			const auto boundsResult =
				referenceStatus ==
						MirrorSelectionPolicy::ReferenceStatus::kEligible ?
					BuildBoundsForReference(reference, orientedBounds) :
					BoundsResult::kMissing3D;
			if (boundsResult != BoundsResult::kReady) {
				if (boundsResult == BoundsResult::kMissing3D) {
					g_counters.threeDMissing.fetch_add(
						1, std::memory_order_relaxed);
				} else {
					g_counters.threeDReady.fetch_add(
						1, std::memory_order_relaxed);
					g_counters.candidatesRejected.fetch_add(
						1, std::memory_order_relaxed);
				}
				Candidate pending{};
				pending.handle = reference->GetHandle();
				pending.formID = formID;
				pending.baseToken = reinterpret_cast<std::uintptr_t>(mirrorBase);
				pending.parentCell = parentCell;
				bool candidateChanged = false;
				bool admitted = false;
				{
					std::scoped_lock lock{ g_candidateLock };
					admitted = CanUpsertCandidateLocked(formID);
					if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
						return;
					if (admitted) {
						candidateChanged =
							PreserveOrAssignGenerationLocked(pending);
						g_candidates.insert_or_assign(formID, pending);
					}
					g_counters.activeCandidates.store(
						g_candidates.size(), std::memory_order_relaxed);
				}
				if (!admitted) {
					logger::warn(
						"[MirrorsOfSkyrim][Recognition] ref {:08X} rejected ({}): "
						"candidate capacity {} reached",
						formID, reason,
						g_candidates.max_size());
					return;
				}
				if (candidateChanged)
					InvalidateCandidateMutationIfOwned(formID, epoch);
				RefreshViewCounters();
				if (boundsResult == BoundsResult::kMissing3D) {
					logger::info(
						"[MirrorsOfSkyrim][Recognition] ref {:08X} matched ({}) "
						"but its lifecycle is not ready (status={})",
						formID, reason,
						static_cast<std::uint32_t>(referenceStatus));
				} else {
					logger::warn(
						"[MirrorsOfSkyrim][Recognition] ref {:08X} matched ({}) "
						"but its OBND/world transform is invalid; retained for retry",
						formID, reason);
				}
				return;
			}
			g_counters.threeDReady.fetch_add(1, std::memory_order_relaxed);

			RE::NiPoint3 eye{};
			const bool hasEye = ReadEyePositionSEH(eye);
			PlaneSelection selection{};
			if (!SelectCandidateForEye(
					orientedBounds, hasEye ? &eye : nullptr, selection)) {
				g_counters.candidatesRejected.fetch_add(
					1, std::memory_order_relaxed);
				RemoveCandidate(formID, epoch);
				logger::warn(
					"[MirrorsOfSkyrim][Recognition] ref {:08X} matched ({}) "
					"but OBND has no usable thin axis",
					formID, reason);
				return;
			}

			g_counters.candidatesDerived.fetch_add(1, std::memory_order_relaxed);
			Candidate candidate{};
			candidate.handle = reference->GetHandle();
			candidate.formID = formID;
			candidate.baseToken = reinterpret_cast<std::uintptr_t>(mirrorBase);
			candidate.parentCell = parentCell;
			candidate.bounds = orientedBounds;
			candidate.normalAxis = selection.normalAxis;
			candidate.boundsReady = true;
			bool candidateChanged = false;
			bool admitted = false;
			{
				std::scoped_lock lock{ g_candidateLock };
				admitted = CanUpsertCandidateLocked(formID);
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return;
				if (admitted) {
					candidateChanged =
						PreserveOrAssignGenerationLocked(candidate);
					g_candidates.insert_or_assign(formID, candidate);
				}
				g_counters.activeCandidates.store(
					g_candidates.size(), std::memory_order_relaxed);
			}
			if (!admitted) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition] ref {:08X} rejected ({}): "
					"candidate capacity {} reached",
					formID, reason,
					g_candidates.max_size());
				return;
			}
			if (candidateChanged)
				InvalidateCandidateMutationIfOwned(formID, epoch);
			RefreshViewCounters();

			const auto& unsignedNormal =
				orientedBounds.worldAxes[selection.normalAxis];
			logger::info(
				"[MirrorsOfSkyrim][Recognition] candidate ref={:08X} source={} "
				"center=({:.3f},{:.3f},{:.3f}) thinAxis={} "
				"unsignedNormal=({:.4f},{:.4f},{:.4f}) "
				"extents=({:.3f},{:.3f},{:.3f}) facing={:.5f} renderable={}",
				formID, reason, orientedBounds.worldCenter.x,
				orientedBounds.worldCenter.y, orientedBounds.worldCenter.z,
				selection.normalAxis, unsignedNormal.x, unsignedNormal.y,
				unsignedNormal.z, orientedBounds.worldHalfExtents[0],
				orientedBounds.worldHalfExtents[1],
				orientedBounds.worldHalfExtents[2], selection.facingCosine,
				selection.renderable);
		}

		template <class Enumerator>
		void EnumerateReferences(Enumerator&& enumerate, const char* reason)
		{
			if (SuppressWorkDuringLoad() || !AnyMirrorBaseResolved())
				return;
			const auto epoch = g_loadEpoch.load(std::memory_order_acquire);
			g_counters.scans.fetch_add(1, std::memory_order_relaxed);
			std::vector<RE::NiPointer<RE::TESObjectREFR>> matches{};
			enumerate([&](RE::TESObjectREFR* reference) {
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return RE::BSContainer::ForEachResult::kStop;
				g_counters.referencesEnumerated.fetch_add(
					1, std::memory_order_relaxed);
				if (MatchesOwnedBaseSEH(reference))
					matches.emplace_back(reference);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (const auto& reference : matches) {
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return;
				ObserveMatchingReference(reference.get(), reason);
			}
		}

		void ScanCell(RE::TESObjectCELL* cell, const char* reason)
		{
			if (SuppressWorkDuringLoad() || !cell)
				return;
			EnumerateReferences(
				[cell](auto&& callback) {
					cell->ForEachReference(
						std::forward<decltype(callback)>(callback));
				},
				reason);
			LogDiagnostics(reason);
		}

		void ScanLoadedWorld(const char* reason)
		{
			if (SuppressWorkDuringLoad())
				return;
			if (auto* world = RE::TES::GetSingleton()) {
				EnumerateReferences(
					[world](auto&& callback) {
						world->ForEachReference(
							std::forward<decltype(callback)>(callback));
					},
					reason);
			}
			LogDiagnostics(reason);
		}

		class ReferenceEventSink final :
			public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
			public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
			public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
			public RE::BSTEventSink<RE::TESFormDeleteEvent>
		{
		public:
			static ReferenceEventSink& GetSingleton()
			{
				static ReferenceEventSink singleton{};
				return singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESCellFullyLoadedEvent* event,
				RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
			{
				if (SuppressWorkDuringLoad())
					return RE::BSEventNotifyControl::kContinue;
				if (event)
					ScanCell(event->cell, "cell-loaded");
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESCellAttachDetachEvent* event,
				RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
			{
				if (SuppressWorkDuringLoad())
					return RE::BSEventNotifyControl::kContinue;
				if (!AnyMirrorBaseResolved() || !event || !event->reference)
					return RE::BSEventNotifyControl::kContinue;
				auto* reference = event->reference.get();
				if (!MatchesOwnedBaseSEH(reference))
					return RE::BSEventNotifyControl::kContinue;
				g_counters.referencesEnumerated.fetch_add(
					1, std::memory_order_relaxed);
				ObserveMatchingReference(
					reference,
					event->attached ? "reference-attached" : "reference-detached");
				LogDiagnostics(
					event->attached ? "reference-attached" : "reference-detached");
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESObjectLoadedEvent* event,
				RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				const auto epoch = g_loadEpoch.load(std::memory_order_acquire);
				if (SuppressWorkDuringLoad())
					return RE::BSEventNotifyControl::kContinue;
				if (!AnyMirrorBaseResolved() || !event || event->formID == 0)
					return RE::BSEventNotifyControl::kContinue;
				if (!event->loaded) {
					RemoveCandidate(event->formID, epoch);
					MirrorTextureSetSurface::Forget(event->formID);
					g_counters.unloadedRejects.fetch_add(
						1, std::memory_order_relaxed);
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* reference =
					RE::TESForm::LookupByID<RE::TESObjectREFR>(event->formID);
				if (!MatchesOwnedBaseSEH(reference))
					return RE::BSEventNotifyControl::kContinue;
				g_counters.referencesEnumerated.fetch_add(
					1, std::memory_order_relaxed);
				ObserveMatchingReference(reference, "object-loaded");
				LogDiagnostics("object-loaded");
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESFormDeleteEvent* event,
				RE::BSTEventSource<RE::TESFormDeleteEvent>*) override
			{
				const auto epoch = g_loadEpoch.load(std::memory_order_acquire);
				if (SuppressWorkDuringLoad())
					return RE::BSEventNotifyControl::kContinue;
				if (event) {
					RemoveCandidate(event->formID, epoch);
					MirrorTextureSetSurface::Forget(event->formID);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		[[nodiscard]] bool IsRecognizedPaneGeometrySEH(
			RE::BSGeometry* geometry,
			bool& tagged,
			bool& named,
			bool& owned,
			bool& exactOwnedPane,
			RE::FormID& ownerFormID,
			RE::TESObjectREFR*& ownerReference) noexcept
		{
			tagged = false;
			named = false;
			owned = false;
			exactOwnedPane = false;
			ownerFormID = 0;
			ownerReference = nullptr;
			__try {
				if (!geometry)
					return false;
				bool exactTag = false;
				if (auto* extra = geometry->GetExtraData(g_mirrorTag.get())) {
					if (auto* integer =
							netimmerse_cast<RE::NiIntegerExtraData*>(extra)) {
						tagged = true;
						exactTag = integer->value == kPaneTagValue;
					}
				}
				const char* rawName = geometry->name.c_str();
				const std::string_view name = rawName ? rawName : "";
				if (MirrorNifSurface::IsNamedPane(geometry)) {
					auto* const owner = geometry->GetUserData();
					if (!MirrorNifSurface::MatchesGeometry(owner,geometry)) return false;
					tagged = named = owned = exactOwnedPane = true;
					ownerFormID = owner->GetFormID();
					ownerReference = owner;
					return true;
				}
				named = MirrorPaneNames::IsMirrorName(name);
				if (auto* owner = geometry->GetUserData()) {
					// Ordinary draws must not traverse unrelated NIF scene graphs.
					owned = IdentifyMirrorBase(owner->GetObjectReference());
					if (owned) {
						ownerFormID = owner->GetFormID();
						ownerReference = owner;
					}
				}
				exactOwnedPane = owned && exactTag && name == kPaneName;
				return tagged || named;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void RefreshViewCounters()
		{
			MirrorNifSurface::ValidationScope nifValidation{};
			const auto epoch = g_loadEpoch.load(std::memory_order_acquire);
			if (SuppressWorkDuringLoad())
				return;
			RE::NiPoint3 eye{};
			const bool hasEye = ReadEyePositionSEH(eye);
			std::vector<Candidate> snapshot{};
			{
				std::scoped_lock lock{ g_candidateLock };
				snapshot.reserve(g_candidates.size());
				for (const auto& [formID, candidate] : g_candidates) {
					(void)formID;
					snapshot.push_back(candidate);
				}
			}

			std::vector<Candidate> refreshed{};
			std::vector<Candidate> stale{};
			std::vector<RE::FormID> transient{};
			refreshed.reserve(snapshot.size());
			for (auto candidate : snapshot) {
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return;
				auto reference = candidate.handle.get();
				const auto referenceStatus =
					ReadReferenceStatusSEH(reference.get(), &candidate);
				if (referenceStatus !=
					MirrorSelectionPolicy::ReferenceStatus::kEligible) {
					RecordReferenceReject(referenceStatus);
				}
				if (RemoveInsteadOfRetain(referenceStatus)) {
					stale.push_back(candidate);
					continue;
				}
				if (MirrorSelectionPolicy::IsTransientLifecycleStatus(
						referenceStatus)) {
					transient.push_back(candidate.formID);
					continue;
				}
				auto* const currentBase = reference->GetObjectReference();
				candidate.baseToken = reinterpret_cast<std::uintptr_t>(currentBase);
				candidate.parentCell = {};
				(void)ReadParentCellIdentitySEH(
					reference.get(), candidate.parentCell);

				OrientedBounds currentBounds{};
				const auto boundsResult =
					BuildBoundsForReference(reference.get(), currentBounds);
				if (boundsResult != BoundsResult::kReady) {
					candidate.bounds = {};
					candidate.normalAxis = -1;
					candidate.boundsReady = false;
					refreshed.push_back(candidate);
					continue;
				}
				candidate.bounds = currentBounds;
				candidate.boundsReady = true;
				PlaneSelection selection{};
				(void)SelectCandidateForEye(
					candidate.bounds, hasEye ? &eye : nullptr, selection);
				candidate.normalAxis = selection.normalAxis;
				refreshed.push_back(candidate);
			}

			std::uint64_t usable = 0;
			std::uint64_t rejected = 0;
			std::vector<Candidate> newlyReady{};
			std::vector<RE::FormID> mutatedCandidates{};
			newlyReady.reserve(refreshed.size());
			{
				std::scoped_lock lock{ g_candidateLock };
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return;
				for (auto candidate : refreshed) {
					auto current = g_candidates.find(candidate.formID);
					if (current != g_candidates.end() &&
						current->second.handle == candidate.handle &&
						current->second.generation == candidate.generation) {
						const bool becameReady =
							!current->second.boundsReady && candidate.boundsReady;
						if (PreserveOrAssignGenerationLocked(candidate))
							mutatedCandidates.push_back(candidate.formID);
						current->second = candidate;
						if (becameReady)
							newlyReady.push_back(candidate);
					}
				}
				for (const auto& staleCandidate : stale) {
					const auto current = g_candidates.find(staleCandidate.formID);
					if (current != g_candidates.end() &&
						current->second.handle == staleCandidate.handle &&
						current->second.generation == staleCandidate.generation) {
						mutatedCandidates.push_back(staleCandidate.formID);
						g_candidates.erase(current);
						g_mainWorldPaneVisibleAt.erase(staleCandidate.formID);
					}
				}
				for (const auto& [formID, candidate] : g_candidates) {
					PlaneSelection selection{};
					if (std::ranges::find(transient, formID) == transient.end() &&
						candidate.boundsReady &&
						SelectCandidateForEye(
							candidate.bounds, hasEye ? &eye : nullptr, selection) &&
						selection.renderable) {
						++usable;
					} else {
						++rejected;
					}
				}
				g_counters.activeCandidates.store(
					g_candidates.size(), std::memory_order_relaxed);
			}
			for (const auto formID : mutatedCandidates)
				InvalidateCandidateMutationIfOwned(formID, epoch);
			for (const auto& candidate : newlyReady) {
				g_counters.threeDReady.fetch_add(1, std::memory_order_relaxed);
				g_counters.candidatesDerived.fetch_add(1, std::memory_order_relaxed);
				logger::info(
					"[MirrorsOfSkyrim][Recognition] pending ref {:08X} now has 3D; "
					"center=({:.3f},{:.3f},{:.3f}) thinAxis={}",
					candidate.formID, candidate.bounds.worldCenter.x,
					candidate.bounds.worldCenter.y,
					candidate.bounds.worldCenter.z, candidate.normalAxis);
			}
			g_counters.viewSamples.fetch_add(1, std::memory_order_relaxed);
			g_counters.viewUsableNow.store(usable, std::memory_order_relaxed);
			g_counters.viewRejectedNow.store(rejected, std::memory_order_relaxed);
		}

		void MaybeLogPeriodicDiagnostics() noexcept
		{
			try {
				if (g_counters.activeCandidates.load(std::memory_order_relaxed) == 0)
					return;
				const auto now = CurrentMilliseconds();
				auto previous = g_lastPeriodicLogMilliseconds.load(
					std::memory_order_relaxed);
				if (now - previous <
					std::chrono::duration_cast<std::chrono::milliseconds>(
						kDiagnosticPeriod).count()) {
					return;
				}
				if (!g_lastPeriodicLogMilliseconds.compare_exchange_strong(
						previous, now, std::memory_order_relaxed)) {
					return;
				}
				RefreshViewCounters();
				LogDiagnostics("periodic");
			} catch (...) {
			}
		}

		struct LightingSetupHook
		{
			static void thunk(
				RE::BSShader* shader,
				RE::BSRenderPass* pass,
				std::uint32_t flags)
			{
				if (SuppressWorkDuringLoad()) {
					func(shader, pass, flags);
					MirrorShadowMapBypass::OnSetupGeometryReturned(pass);
					return;
				}
				g_counters.lightingSetupCalls.fetch_add(
					1, std::memory_order_relaxed);
				bool tagged = false;
				bool named = false;
				bool owned = false;
				bool exactOwnedPane = false;
				RE::FormID ownerFormID = 0;
				RE::TESObjectREFR* ownerReference = nullptr;
				if (pass && IsRecognizedPaneGeometrySEH(
						pass->geometry, tagged, named, owned, exactOwnedPane,
						ownerFormID, ownerReference)) {
					g_counters.paneDraws.fetch_add(1, std::memory_order_relaxed);
					if (tagged) {
						g_counters.taggedPaneDraws.fetch_add(
							1, std::memory_order_relaxed);
					}
					if (named) {
						g_counters.namedPaneDraws.fetch_add(
							1, std::memory_order_relaxed);
					}
					if (owned) {
						g_counters.ownedPaneDraws.fetch_add(
							1, std::memory_order_relaxed);
					}
				}
				func(shader, pass, flags);
				if (exactOwnedPane && MirrorNifSurface::IsNamedPane(pass->geometry) &&
					MirrorPaneDelivery::IsInsideMainWorld() &&
					g_mirrorSelectionEnabled.load(std::memory_order_acquire)) {
					// Handles native PlaceAtMe / delayed 3D with no bespoke item script.
					// Only admit once: existing candidates revalidate at selection.
					bool known = false;
					{ std::scoped_lock lock{g_candidateLock}; known = g_candidates.contains(ownerFormID); }
					if (!known) {
						try { ObserveMatchingReference(ownerReference,"nif-pane-visible"); }
						catch (...) { /* Keep the native fallback. */ }
					}
				}
				if (exactOwnedPane)
					RecordMainWorldPaneVisibility(ownerFormID, ownerReference);
				MirrorShadowMapBypass::OnSetupGeometryReturned(pass);
				const auto divider = g_hookClockDivider.fetch_add(
					1, std::memory_order_relaxed) + 1;
				if ((divider & kHookClockCheckMask) == 0)
					MaybeLogPeriodicDiagnostics();
			}

			static inline REL::Relocation<decltype(thunk)> func{};
		};

		[[nodiscard]] bool InstallLightingSetupObserver()
		{
			if (g_lightingObserverInstalled)
				return true;
			if (!IsSupportedRuntime())
				return false;
			g_mirrorTag.get() = kPaneTag.data();
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSLightingShader[0] };
			LightingSetupHook::func =
				vtable.write_vfunc(0x6, LightingSetupHook::thunk);
			g_lightingObserverInstalled = true;
			logger::info(
				"[MirrorsOfSkyrim][Recognition] chained lighting-setup observer installed");
			return true;
		}
	}

	static bool ResolveStandingMirrorBases(const char* reason)
	{
		auto* const dataHandler = RE::TESDataHandler::GetSingleton();
		g_mirrorBases = {};
		for (std::size_t index = 0;
			 index < kStandingMirrorLocalFormIDs.size(); ++index) {
			const auto localFormID = kStandingMirrorLocalFormIDs[index];
			if (MirrorContentProfile::StandingPlugin(localFormID) ==
					MirrorContentProfile::kAddonPlugin &&
				!MirrorContentProfile::AddonLoaded())
				continue;
			auto* const resolved =
				ResolveMirrorBaseWithDiagnostics(dataHandler, localFormID);
			g_mirrorBases[index] = resolved;
			if (resolved) {
				logger::info(
					"[MirrorsOfSkyrim][Recognition] resolved exact standing-mirror STAT "
					"{:08X} from {}:0x{:03X} ({})",
					resolved->GetFormID(), MirrorContentProfile::StandingPlugin(localFormID),
					localFormID, reason);
			} else if (MirrorContentProfile::StandingPlugin(localFormID) !=
						   MirrorContentProfile::kAddonPlugin ||
					   MirrorContentProfile::AddonLoaded()) {
				logger::info(
					"[MirrorsOfSkyrim][Recognition] standing-mirror identity "
					"{}:0x{:03X} is unresolved ({})",
					MirrorContentProfile::StandingPlugin(localFormID), localFormID, reason);
			}
		}

		bool ambiguousBases = false;
		for (std::size_t left = 0; left < g_mirrorBases.size(); ++left) {
			if (!g_mirrorBases[left])
				continue;
			for (std::size_t right = left + 1;
				 right < g_mirrorBases.size(); ++right) {
				if (g_mirrorBases[left] == g_mirrorBases[right]) {
					ambiguousBases = true;
					logger::critical(
						"[MirrorsOfSkyrim][Recognition] identities {}:0x{:03X} and "
						"{}:0x{:03X} resolved to one base pointer; catalog rejected",
						MirrorContentProfile::StandingPlugin(kStandingMirrorLocalFormIDs[left]), kStandingMirrorLocalFormIDs[left],
						MirrorContentProfile::StandingPlugin(kStandingMirrorLocalFormIDs[right]),
						kStandingMirrorLocalFormIDs[right]);
				}
			}
		}
		if (ambiguousBases)
			g_mirrorBases = {};
		if (!AnyMirrorBaseResolved()) {
			static std::atomic<bool> loggedIdle{ false };
			if (!loggedIdle.exchange(true, std::memory_order_acq_rel)) {
				logger::warn(
					"[MirrorsOfSkyrim][Recognition] no exact standing-mirror STAT resolved; recognition is idle ({})",
					reason);
			}
			return false;
		}

		if (!g_eventsRegistered) {
			if (auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton()) {
				auto& sink = ReferenceEventSink::GetSingleton();
				eventSource->AddEventSink<RE::TESCellFullyLoadedEvent>(&sink);
				eventSource->AddEventSink<RE::TESCellAttachDetachEvent>(&sink);
				eventSource->AddEventSink<RE::TESObjectLoadedEvent>(&sink);
				eventSource->AddEventSink<RE::TESFormDeleteEvent>(&sink);
				g_eventsRegistered = true;
				logger::info(
					"[MirrorsOfSkyrim][Recognition] reference event sinks registered ({})",
					reason);
			} else {
				logger::error(
					"[MirrorsOfSkyrim][Recognition] ScriptEventSourceHolder is null; reference events unavailable");
			}
		}
		return true;
	}

	void RecoverStandingMirrors(const char* reason)
	{
		if (!AnyMirrorBaseResolved())
			(void)ResolveStandingMirrorBases(reason);
		if (AnyMirrorBaseResolved())
			ScanLoadedWorld(reason);
	}

	void OnDataLoaded()
	{
		if (!IsSupportedRuntime()) {
			logger::warn(
				"[MirrorsOfSkyrim][Recognition] disabled: runtime {} is outside the exact SE/AE gate",
				REL::Module::get().version().string());
			return;
		}

		g_mirrorSelectionEnabled.store(false, std::memory_order_release);
		MirrorNifSurface::OnDataLoaded();
		(void)ResolveStandingMirrorBases("data-loaded");

		if (PublicMirrorHookRequested() &&
			!EnsureLightingSetupObserverInstalled()) {
			logger::critical(
				"[MirrorsOfSkyrim][Recognition] mirror preparation refused: exact-pane visibility observer unavailable");
		}
		LogDiagnostics("data-loaded");
	}

	void OnMirrorActivationCommitted() noexcept
	{
		const auto committed = MirrorActivation::GlobalLifecycle().Committed();
		if (!committed || committed->IsConflicted())
			return;
		const bool mirrorEnabled = MirrorSelectionPolicy::PublicSelectionReady({
			MirrorActivation::ContainsAll(
				committed->Masks().Active(), MirrorActivation::kMirrorFeatures),
			MirrorsOfSkyrimDataReady(),
			LightingSetupObserverReady() });
		g_mirrorSelectionEnabled.store(mirrorEnabled, std::memory_order_release);
		if (mirrorEnabled) {
			logger::info(
				"[MirrorsOfSkyrim][Recognition] exact standing-mirror identity admitted for selection");
		}
	}

	bool EnsureLightingSetupObserverInstalled() noexcept
	{
		try {
			return InstallLightingSetupObserver();
		} catch (...) {
			return false;
		}
	}

	bool LightingSetupObserverReady() noexcept
	{
		return g_lightingObserverInstalled;
	}

	RE::TESBoundObject* GetOwnedMirrorBase() noexcept
	{
		return ResolvedBase(0x800);
	}

	bool HasOwnedMirrorBase() noexcept
	{
		return AnyMirrorBaseResolved();
	}

	bool MirrorsOfSkyrimDataReady() noexcept
	{
		return ResolvedBase(0x800) != nullptr;
	}

	bool IsOwnedMirrorBase(const RE::TESBoundObject* base) noexcept
	{
		return IdentifyMirrorBase(base);
	}

	bool ObserveCreatedReference(RE::TESObjectREFR* reference) noexcept
	{
		if (SuppressWorkDuringLoad())
			return false;
		try {
			if (!reference || !reference->IsDynamicForm() ||
				!MatchesOwnedBaseSEH(reference)) {
				return false;
			}

			const RE::FormID formID = reference->GetFormID();
			if (formID == 0)
				return false;
			const RE::ObjectRefHandle handle = reference->GetHandle();
			auto keepAlive = handle.get();
			if (!keepAlive || keepAlive.get() != reference)
				return false;

			g_counters.referencesEnumerated.fetch_add(
				1, std::memory_order_relaxed);
			ObserveMatchingReference(reference, "created-reference");

			std::scoped_lock lock{ g_candidateLock };
			const auto candidate = g_candidates.find(formID);
			return candidate != g_candidates.end() &&
			       candidate->second.handle == handle &&
			       candidate->second.generation != 0;
		} catch (...) {
			return false;
		}
	}

	void RecordReflectionRender() noexcept
	{
		g_counters.reflectionRenders.fetch_add(1, std::memory_order_relaxed);
	}

	void OnPreLoadGame() noexcept
	{
		g_loadTransitionSuspended.store(true, std::memory_order_release);
		MirrorTextureSetSurface::OnPreLoadGame();
		g_loadEpoch.fetch_add(1, std::memory_order_acq_rel);
		try { ClearCandidates(); } catch (...) {}
		// Revoke even when the candidate map was already empty.
		InvalidateCandidatePublicationAndDelivery();
	}

	void OnGameLoaded()
	{
		ClearCandidates();
		MirrorTextureSetSurface::OnGameLoaded();
		g_loadTransitionSuspended.store(false, std::memory_order_release);
		RecoverStandingMirrors("game-loaded");
	}

	static bool ResolveMirrorSelection(
		const DirectX::XMFLOAT3& eyePosition,
		ActiveMirror& output, ActiveMirrorSet* ranked = nullptr,
		std::uint32_t exactFormID = 0,
		std::uint64_t exactGeneration = 0, bool* collectionFailed = nullptr) noexcept
	{
		if (collectionFailed)
			*collectionFailed = false;
		try {
			MirrorNifSurface::ValidationScope nifValidation{};
			const auto epoch = g_loadEpoch.load(std::memory_order_acquire);
			g_lastSelectionDeferredForOwnerLease = false;
			output = {};
			if (SuppressWorkDuringLoad() ||
				!g_mirrorSelectionEnabled.load(std::memory_order_acquire) ||
				!AnyMirrorBaseResolved() || !std::isfinite(eyePosition.x) ||
				!std::isfinite(eyePosition.y) || !std::isfinite(eyePosition.z)) {
				return false;
			}
			const RE::NiPoint3 eye{
				eyePosition.x, eyePosition.y, eyePosition.z };
			MirrorInteriorEligibility::LocationIdentity currentLocation{};
			const auto currentLocationStatus = ReadCurrentLocationSEH(currentLocation);
			if (!MirrorInteriorEligibility::AllowsMirrorCapture(
					currentLocationStatus)) {
				return false;
			}

			CandidateIdentity stickyIdentity{};
			TimedCandidateIdentity lastDeliveryOwner{};
			TimedCandidateIdentity lastValidatedVisibleContinuityOwner{};
			{
				std::scoped_lock lock{ g_ownerLock };
				stickyIdentity = g_stickyActiveMirror;
				lastDeliveryOwner = g_lastDeliveryOwner;
				lastValidatedVisibleContinuityOwner =
					g_lastValidatedVisibleContinuityOwner;
			}
			const std::uint32_t stickyFormID = stickyIdentity.formID;
			const auto now = CurrentMilliseconds();

			std::vector<Candidate> candidateStorage{};
			std::array<Candidate, 1> exactStorage{};
			std::span<const Candidate> candidates{};
			{
				std::scoped_lock lock{ g_candidateLock };
				if (exactFormID != 0) {
					// Per-pane delivery already names its owner. Copying every loaded
					// candidate here made each exact lookup scale with the whole registry.
					if (const auto found = g_candidates.find(exactFormID);
						found != g_candidates.end()) {
						exactStorage[0] = found->second;
						candidates = exactStorage;
					}
				} else {
					candidateStorage.reserve(g_candidates.size());
					for (const auto& [formID, candidate] : g_candidates) {
						(void)formID;
						candidateStorage.push_back(candidate);
					}
					candidates = candidateStorage;
				}
			}

			struct EvaluatedCandidate
			{
				MirrorSelectionPolicy::Candidate policy{};
				ActiveMirror mirror{};
				std::int64_t lastVisibleMilliseconds{};
			};
			std::vector<EvaluatedCandidate> evaluated{};
			if (!exactFormID) evaluated.reserve(candidates.size());
			std::vector<RE::FormID> mutatedCandidates{};
			for (auto candidate : candidates) {
				if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
					return false;
				if (exactFormID != 0 && candidate.formID != exactFormID)
					continue;
				auto reference = candidate.handle.get();
				const auto referenceStatus =
					ReadReferenceStatusSEH(reference.get(), &candidate);
				if (referenceStatus !=
					MirrorSelectionPolicy::ReferenceStatus::kEligible) {
					RecordReferenceReject(referenceStatus);
				}
				if (RemoveInsteadOfRetain(referenceStatus)) {
					std::scoped_lock lock{ g_candidateLock };
					const auto current = g_candidates.find(candidate.formID);
					if (current != g_candidates.end() &&
						current->second.handle == candidate.handle &&
						current->second.generation == candidate.generation) {
						mutatedCandidates.push_back(candidate.formID);
						g_candidates.erase(current);
						g_mainWorldPaneVisibleAt.erase(candidate.formID);
					}
					continue;
				}
				if (MirrorSelectionPolicy::IsTransientLifecycleStatus(
						referenceStatus)) {
					continue;
				}
				candidate.parentCell = {};
				(void)ReadParentCellIdentitySEH(
					reference.get(), candidate.parentCell);

				OrientedBounds bounds{};
				const auto boundsResult =
					referenceStatus ==
							MirrorSelectionPolicy::ReferenceStatus::kEligible ?
						BuildBoundsForReference(reference.get(), bounds) :
						BoundsResult::kMissing3D;
				if (boundsResult != BoundsResult::kReady) {
					std::scoped_lock lock{ g_candidateLock };
					const auto current = g_candidates.find(candidate.formID);
					if (current == g_candidates.end() ||
						current->second.handle != candidate.handle ||
						current->second.generation != candidate.generation) {
						continue;
					}
					candidate.bounds = {};
					candidate.normalAxis = -1;
					candidate.boundsReady = false;
					if (PreserveOrAssignGenerationLocked(candidate))
						mutatedCandidates.push_back(candidate.formID);
					current->second = candidate;
					continue;
				}

				PlaneSelection selection{};
				const bool selected = SelectCandidateForEye(bounds, &eye, selection);
				candidate.bounds = bounds;
				candidate.normalAxis = selected ? selection.normalAxis : -1;
				candidate.boundsReady = true;
				bool candidateCurrent = false;
				std::int64_t lastVisibleMilliseconds = 0;
				{
					std::scoped_lock lock{ g_candidateLock };
					const auto current = g_candidates.find(candidate.formID);
					if (current != g_candidates.end() &&
						current->second.handle == candidate.handle &&
						current->second.generation == candidate.generation) {
						if (PreserveOrAssignGenerationLocked(candidate))
							mutatedCandidates.push_back(candidate.formID);
						current->second = candidate;
						candidateCurrent = true;
						if (const auto visible =
								g_mainWorldPaneVisibleAt.find(candidate.formID);
							visible != g_mainWorldPaneVisibleAt.end() &&
							visible->second.generation == candidate.generation) {
							lastVisibleMilliseconds = visible->second.milliseconds;
						}
					}
				}
				if (!candidateCurrent || !selected || !selection.renderable)
					continue;

				ParentLocationSnapshot parentLocation{};
				if (!ReadParentLocationSEH(reference.get(), parentLocation) ||
					!ParentLocationMatches(
						parentLocation, currentLocationStatus, currentLocation)) {
					g_counters.locationRejects.fetch_add(
						1, std::memory_order_relaxed);
					continue;
				}

				const float dx = bounds.worldCenter.x - eyePosition.x;
				const float dy = bounds.worldCenter.y - eyePosition.y;
				const float dz = bounds.worldCenter.z - eyePosition.z;
				const float distanceSquared = dx * dx + dy * dy + dz * dz;
				if (!std::isfinite(distanceSquared))
					continue;

				EvaluatedCandidate value{};
				value.lastVisibleMilliseconds = lastVisibleMilliseconds;
				value.policy.formID = candidate.formID;
				value.policy.generation = candidate.generation;
				value.policy.distanceSquared = distanceSquared;
				value.policy.selectable = true;
				value.policy.recentlyVisible = IsRecent(
					now, lastVisibleMilliseconds,
					MirrorSelectionPolicy::kMainWorldVisibilityLeaseMilliseconds);
				value.mirror.formID = candidate.formID;
				value.mirror.candidateGeneration = candidate.generation;
				value.mirror.parentCell = candidate.parentCell;
				value.mirror.plane = selection;
				value.mirror.worldHalfExtents = bounds.worldHalfExtents;
				value.mirror.worldAxes = bounds.worldAxes;
				value.mirror.nifPaneToken = reinterpret_cast<std::uintptr_t>(bounds.nifPane.get());
				value.mirror.nifSignature = bounds.nifSignature;
				if (exactFormID != 0) {
					for (const auto formID : mutatedCandidates)
						InvalidateCandidateMutationIfOwned(formID, epoch);
					if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire) ||
						value.mirror.candidateGeneration != exactGeneration)
						return false;
					output = value.mirror;
					return true;
				}
				evaluated.push_back(value);
			}

			{
				std::scoped_lock lock{ g_candidateLock };
				g_counters.activeCandidates.store(
					g_candidates.size(), std::memory_order_relaxed);
			}
			for (const auto formID : mutatedCandidates)
				InvalidateCandidateMutationIfOwned(formID, epoch);

			if (SuppressWorkDuringLoad() || epoch != g_loadEpoch.load(std::memory_order_acquire))
				return false;
			if (exactFormID != 0) {
				for (const auto& candidate : evaluated) {
					if (candidate.mirror.formID == exactFormID &&
						candidate.mirror.candidateGeneration == exactGeneration) {
						output = candidate.mirror;
						return true;
					}
				}
				return false;
			}
			if (ranked) {
				ranked->reserve(evaluated.size());
				std::sort(evaluated.begin(), evaluated.end(), [](const auto& a, const auto& b) {
					if (a.policy.recentlyVisible != b.policy.recentlyVisible)
						return a.policy.recentlyVisible;
					if (a.policy.distanceSquared != b.policy.distanceSquared)
						return a.policy.distanceSquared < b.policy.distanceSquared;
					return a.policy.formID < b.policy.formID;
				});
				for (const auto& candidate : evaluated) {
					if (MirrorCaptureWorkPolicy::enabled.load(std::memory_order_relaxed)) {
						const bool observed = g_mainWorldVisibilityObserved.load(std::memory_order_acquire);
						if (MirrorFleetPolicy::enabled.load(std::memory_order_relaxed) ||
							MirrorCaptureWorkPolicy::Retain(observed, candidate.policy.recentlyVisible,
							now, candidate.lastVisibleMilliseconds)) {
							auto mirror = candidate.mirror;
							mirror.captureRequested = MirrorCaptureWorkPolicy::CaptureDemand(
								observed, candidate.policy.recentlyVisible);
							ranked->push_back(mirror);
						}
						continue;
					}
					MirrorFramePublication::Snapshot retained{};
					if (!candidate.policy.recentlyVisible &&
						g_mainWorldVisibilityObserved.load(std::memory_order_acquire) &&
						!MirrorFramePublication::TryGetSnapshot(candidate.mirror.formID,
							candidate.mirror.candidateGeneration, retained))
						continue;
					ranked->push_back(candidate.mirror);
				}
				return !ranked->empty();
			}

			std::vector<MirrorSelectionPolicy::Candidate> policyCandidates{};
			policyCandidates.reserve(evaluated.size());
			for (const auto& candidate : evaluated)
				policyCandidates.push_back(candidate.policy);
			bool stickyIdentityTracked = false;
			{
				std::scoped_lock lock{ g_candidateLock };
				const auto sticky = g_candidates.find(stickyIdentity.formID);
				stickyIdentityTracked = sticky != g_candidates.end() &&
					sticky->second.generation == stickyIdentity.generation;
			}
			const auto ownershipLeaseSource =
				MirrorSelectionPolicy::ClassifyOwnershipLease({
					.ownerFormID = stickyIdentity.formID,
					.ownerGeneration = stickyIdentity.generation,
					.ownerIdentityTracked = stickyIdentityTracked,
					.successfulDeliveryFormID = lastDeliveryOwner.formID,
					.successfulDeliveryGeneration = lastDeliveryOwner.generation,
					.successfulDeliveryRecent = IsRecent(
						now, lastDeliveryOwner.milliseconds,
						MirrorSelectionPolicy::
							kDeliveryOwnershipLeaseMilliseconds),
					.validatedVisibleContinuityFormID =
						lastValidatedVisibleContinuityOwner.formID,
					.validatedVisibleContinuityGeneration =
						lastValidatedVisibleContinuityOwner.generation,
					.validatedVisibleContinuityRecent = IsRecent(
						now, lastValidatedVisibleContinuityOwner.milliseconds,
						MirrorSelectionPolicy::
							kValidatedVisibleContinuityLeaseMilliseconds) });
			const bool ownerLeaseActive = ownershipLeaseSource !=
				MirrorSelectionPolicy::OwnershipLeaseSource::kNone;
			if (ownershipLeaseSource ==
				MirrorSelectionPolicy::OwnershipLeaseSource::
					kValidatedVisibleContinuity) {
				g_counters.validatedVisibleContinuityLeaseSelections.fetch_add(
					1, std::memory_order_relaxed);
			}
			const auto decision = MirrorSelectionPolicy::Select(
				policyCandidates,
				MirrorSelectionPolicy::Request{
					.ownerFormID = stickyIdentity.formID,
					.ownerGeneration = stickyIdentity.generation,
					.ownerLeaseActive = ownerLeaseActive,
					.visibilityEvidenceAvailable =
						g_mainWorldVisibilityObserved.load(
							std::memory_order_acquire) });
			if (decision.index == MirrorSelectionPolicy::kNoCandidate) {
				if (MirrorSelectionPolicy::DefersCaptureWithoutInvalidation(
						decision.action)) {
					g_lastSelectionDeferredForOwnerLease = true;
					g_counters.ownershipLeaseHolds.fetch_add(
						1, std::memory_order_relaxed);
				} else if (decision.action ==
					MirrorSelectionPolicy::DecisionAction::kRejectUnseen) {
					g_counters.unseenSelectionRejects.fetch_add(
						1, std::memory_order_relaxed);
				}
				return false;
			}
			if (decision.index >= evaluated.size())
				return false;

			output = evaluated[decision.index].mirror;
			if (decision.action ==
				MirrorSelectionPolicy::DecisionAction::kKeepOwner) {
				g_counters.ownerKeeps.fetch_add(1, std::memory_order_relaxed);
			}
			if (evaluated[decision.index].policy.recentlyVisible) {
				g_counters.visibilitySelections.fetch_add(
					1, std::memory_order_relaxed);
			}
			const CandidateIdentity selectedIdentity{
				output.formID, output.candidateGeneration };
			if (selectedIdentity != stickyIdentity) {
				{
					std::scoped_lock lock{ g_ownerLock };
					if (g_stickyActiveMirror != stickyIdentity)
						return false;
					g_stickyActiveMirror = selectedIdentity;
					g_lastDeliveryOwner = {};
					g_lastValidatedVisibleContinuityOwner = {};
				}
				if (stickyFormID != 0) {
					g_counters.ownerSwitches.fetch_add(
						1, std::memory_order_relaxed);
					InvalidateCandidatePublicationAndDelivery();
				}
				auto budget = g_activeSwitchLogBudget.load(
					std::memory_order_relaxed);
				while (budget != 0 &&
					!g_activeSwitchLogBudget.compare_exchange_weak(
						budget, budget - 1, std::memory_order_acq_rel)) {
				}
				if (budget != 0) {
					float previousDistance = -1.0F;
					for (const auto& candidate : evaluated) {
						if (candidate.policy.formID == stickyFormID &&
							candidate.policy.generation ==
								stickyIdentity.generation) {
							previousDistance =
								std::sqrt(candidate.policy.distanceSquared);
							break;
						}
					}
					logger::warn(
						"[MirrorsOfSkyrim][Recognition][active-switch] {:08X} -> "
						"{:08X} dist(new/previous)={:.1f}/{:.1f} action={} visible={}",
						stickyFormID, output.formID,
						std::sqrt(evaluated[decision.index].policy.distanceSquared),
						previousDistance,
						static_cast<std::uint32_t>(decision.action),
						evaluated[decision.index].policy.recentlyVisible);
				}
			}

			return output.formID != 0 && output.candidateGeneration != 0;
		} catch (...) {
			if (collectionFailed)
				*collectionFailed = true;
			g_lastSelectionDeferredForOwnerLease = false;
			output = {};
			return false;
		}
	}

	bool TryGetActiveMirror(const DirectX::XMFLOAT3& eye, ActiveMirror& output) noexcept
	{
		return ResolveMirrorSelection(eye, output);
	}

	bool GetActiveMirrors(const DirectX::XMFLOAT3& eye, ActiveMirrorSet& output) noexcept
	{
		output.clear();
		ActiveMirror unused{};
		bool failed = false;
		if (!ResolveMirrorSelection(eye, unused, &output, 0, 0, &failed))
			output.clear();
		return !failed;
	}

	bool TryGetMirror(std::uint32_t formID, std::uint64_t generation,
		const DirectX::XMFLOAT3& eye, ActiveMirror& output) noexcept
	{
		output = {};
		return formID != 0 && generation != 0 &&
			ResolveMirrorSelection(eye, output, nullptr, formID, generation);
	}

	bool LastSelectionDeferredForOwnerLease() noexcept
	{
		return g_lastSelectionDeferredForOwnerLease;
	}

	void RecordSuccessfulDeliveryOwner(
		std::uint32_t formID,
		std::uint64_t candidateGeneration) noexcept
	{
		if (SuppressWorkDuringLoad() || formID == 0 || candidateGeneration == 0)
			return;
		try {
			std::scoped_lock lock{ g_candidateLock, g_ownerLock };
			const CandidateIdentity identity{ formID, candidateGeneration };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady ||
				g_stickyActiveMirror != identity) {
				return;
			}
			g_lastDeliveryOwner.formID = formID;
			g_lastDeliveryOwner.generation = candidateGeneration;
			g_lastDeliveryOwner.milliseconds = CurrentMilliseconds();
			g_lastValidatedVisibleContinuityOwner = {};
		} catch (...) {
		}
	}

	ValidatedVisibleContinuityResult RecordValidatedVisibleContinuityOwner(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		MirrorInteriorEligibility::Status expectedStatus,
		const MirrorInteriorEligibility::LocationIdentity& expectedLocation) noexcept
	{
		if (SuppressWorkDuringLoad() || formID == 0 || candidateGeneration == 0)
			return ValidatedVisibleContinuityResult::kInvalidIdentity;
		if (!IsCandidateCurrent(formID, candidateGeneration))
			return ValidatedVisibleContinuityResult::kInvalidIdentity;
		if (!IsCandidateCurrentAtLocation(
				formID, candidateGeneration, expectedStatus, expectedLocation)) {
			return ValidatedVisibleContinuityResult::kLocationRejected;
		}
		try {
			const auto now = CurrentMilliseconds();
			std::scoped_lock lock{ g_candidateLock, g_ownerLock };
			const CandidateIdentity identity{ formID, candidateGeneration };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady ||
				g_stickyActiveMirror != identity) {
				return ValidatedVisibleContinuityResult::kInvalidIdentity;
			}
			const auto visible = g_mainWorldPaneVisibleAt.find(formID);
			if (visible == g_mainWorldPaneVisibleAt.end() ||
				visible->second.generation != candidateGeneration ||
				!IsRecent(
					now, visible->second.milliseconds,
					MirrorSelectionPolicy::kMainWorldVisibilityLeaseMilliseconds)) {
				return ValidatedVisibleContinuityResult::kNativeVisibilityMissing;
			}
			g_lastValidatedVisibleContinuityOwner.formID = formID;
			g_lastValidatedVisibleContinuityOwner.generation = candidateGeneration;
			g_lastValidatedVisibleContinuityOwner.milliseconds = now;
			g_counters.validatedVisibleContinuityRenewals.fetch_add(
				1, std::memory_order_relaxed);
			return ValidatedVisibleContinuityResult::kRecorded;
		} catch (...) {
			return ValidatedVisibleContinuityResult::kException;
		}
	}

	bool IsCandidateCurrent(
		std::uint32_t formID,
		std::uint64_t candidateGeneration) noexcept
	{
		if (SuppressWorkDuringLoad() || formID == 0 || candidateGeneration == 0)
			return false;
		try {
			std::scoped_lock lock{ g_candidateLock };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady) {
				return false;
			}
			auto reference = candidate->second.handle.get();
			return ReadReferenceStatusSEH(reference.get(), &candidate->second) ==
			       MirrorSelectionPolicy::ReferenceStatus::kEligible;
		} catch (...) {
			return false;
		}
	}

	bool IsCandidateCurrentInParentCell(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		const MirrorInteriorEligibility::CellIdentity& expectedParentCell) noexcept
	{
		if (SuppressWorkDuringLoad() || formID == 0 || candidateGeneration == 0 ||
			!MirrorInteriorEligibility::SameValidCell(
				expectedParentCell, expectedParentCell)) {
			return false;
		}

		Candidate snapshot{};
		try {
			std::scoped_lock lock{ g_candidateLock };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady) {
				return false;
			}
			snapshot = candidate->second;
		} catch (...) {
			return false;
		}

		auto reference = snapshot.handle.get();
		if (ReadReferenceStatusSEH(reference.get(), &snapshot) !=
			MirrorSelectionPolicy::ReferenceStatus::kEligible) {
			return false;
		}
		MirrorInteriorEligibility::CellIdentity currentParentCell{};
		if (!ReadParentCellIdentitySEH(reference.get(), currentParentCell) ||
			!MirrorInteriorEligibility::SameValidCell(
				currentParentCell, expectedParentCell)) {
			return false;
		}

		try {
			std::scoped_lock lock{ g_candidateLock };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.handle != snapshot.handle ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady) {
				return false;
			}
		} catch (...) {
			return false;
		}

		currentParentCell = {};
		return ReadReferenceStatusSEH(reference.get(), &snapshot) ==
				MirrorSelectionPolicy::ReferenceStatus::kEligible &&
		       ReadParentCellIdentitySEH(reference.get(), currentParentCell) &&
		       MirrorInteriorEligibility::SameValidCell(
			       currentParentCell, expectedParentCell);
	}

	bool IsCandidateCurrentAtLocation(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		MirrorInteriorEligibility::Status status,
		const MirrorInteriorEligibility::LocationIdentity& expectedLocation) noexcept
	{
		if (SuppressWorkDuringLoad() || formID == 0 || candidateGeneration == 0 ||
			!MirrorInteriorEligibility::AllowsMirrorCapture(status)) {
			return false;
		}
		if (status == MirrorInteriorEligibility::Status::kInterior &&
			!MirrorInteriorEligibility::SameValidCell(
				expectedLocation.playerCell, expectedLocation.playerCell)) {
			return false;
		}
		if (status == MirrorInteriorEligibility::Status::kExterior &&
			!MirrorInteriorEligibility::SameValidWorldspace(
				expectedLocation.worldspace, expectedLocation.worldspace)) {
			return false;
		}

		Candidate snapshot{};
		try {
			std::scoped_lock lock{ g_candidateLock };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady) {
				return false;
			}
			snapshot = candidate->second;
		} catch (...) {
			return false;
		}

		auto reference = snapshot.handle.get();
		if (ReadReferenceStatusSEH(reference.get(), &snapshot) !=
			MirrorSelectionPolicy::ReferenceStatus::kEligible) {
			return false;
		}
		ParentLocationSnapshot currentLocation{};
		if (!ReadParentLocationSEH(reference.get(), currentLocation) ||
			!ParentLocationMatches(currentLocation, status, expectedLocation)) {
			return false;
		}

		try {
			std::scoped_lock lock{ g_candidateLock };
			const auto candidate = g_candidates.find(formID);
			if (candidate == g_candidates.end() ||
				candidate->second.handle != snapshot.handle ||
				candidate->second.generation != candidateGeneration ||
				!candidate->second.boundsReady) {
				return false;
			}
		} catch (...) {
			return false;
		}

		currentLocation = {};
		return ReadReferenceStatusSEH(reference.get(), &snapshot) ==
				MirrorSelectionPolicy::ReferenceStatus::kEligible &&
		       ReadParentLocationSEH(reference.get(), currentLocation) &&
		       ParentLocationMatches(currentLocation, status, expectedLocation);
	}

	bool RunWhileCandidateCurrent(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		CurrentCandidateOperation operation,
		void* state) noexcept
	{
		if (SuppressWorkDuringLoad() || formID == 0 || candidateGeneration == 0 || !operation)
			return false;

		bool ran = false;
		if (!LockCandidateMutexNoexcept())
			return false;
		__try {
			const auto candidate = g_candidates.find(formID);
			if (!SuppressWorkDuringLoad() && candidate != g_candidates.end() &&
				candidate->second.generation == candidateGeneration &&
				candidate->second.boundsReady) {
				auto* const reference =
					RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
				if (ReadReferenceStatusSEH(reference, &candidate->second) !=
					MirrorSelectionPolicy::ReferenceStatus::kEligible) {
					__leave;
				}
				ran = true;
				operation(state);
			}
		} __finally {
			g_candidateLock.unlock();
		}
		return ran;
	}

	void LogDiagnostics(const char* reason)
	{
		logger::info(
			"[MirrorsOfSkyrim][Recognition][{}] scans={} refs={} matched={} "
			"3dReady={} 3dMissing={} candidates={} rejected={} revisions={} "
			"capacityRejects={} deletedRejects={} disabledRejects={} "
			"unloadedRejects={} detachedRejects={} locationRejects={} active={} "
			"viewSamples={} viewUsableNow={} viewRejectedNow={} setupCalls={} "
			"paneDraws={} tagged={} named={} owned={} reflectionRenders={} "
			"mainWorldPaneVisibility={} visibilitySelections={} "
			"unseenSelectionRejects={} ownershipLeaseHolds={} "
			"validatedVisibleContinuity(renewals/leaseSelections)={}/{} "
			"ownerKeeps={} ownerSwitches={} unrelatedMutationsPreserved={}",
			reason ? reason : "status",
			g_counters.scans.load(std::memory_order_relaxed),
			g_counters.referencesEnumerated.load(std::memory_order_relaxed),
			g_counters.baseMatches.load(std::memory_order_relaxed),
			g_counters.threeDReady.load(std::memory_order_relaxed),
			g_counters.threeDMissing.load(std::memory_order_relaxed),
			g_counters.candidatesDerived.load(std::memory_order_relaxed),
			g_counters.candidatesRejected.load(std::memory_order_relaxed),
			g_counters.candidateRevisions.load(std::memory_order_relaxed),
			g_counters.registryCapacityRejects.load(std::memory_order_relaxed),
			g_counters.deletedRejects.load(std::memory_order_relaxed),
			g_counters.disabledRejects.load(std::memory_order_relaxed),
			g_counters.unloadedRejects.load(std::memory_order_relaxed),
			g_counters.detachedRejects.load(std::memory_order_relaxed),
			g_counters.locationRejects.load(std::memory_order_relaxed),
			g_counters.activeCandidates.load(std::memory_order_relaxed),
			g_counters.viewSamples.load(std::memory_order_relaxed),
			g_counters.viewUsableNow.load(std::memory_order_relaxed),
			g_counters.viewRejectedNow.load(std::memory_order_relaxed),
			g_counters.lightingSetupCalls.load(std::memory_order_relaxed),
			g_counters.paneDraws.load(std::memory_order_relaxed),
			g_counters.taggedPaneDraws.load(std::memory_order_relaxed),
			g_counters.namedPaneDraws.load(std::memory_order_relaxed),
			g_counters.ownedPaneDraws.load(std::memory_order_relaxed),
			g_counters.reflectionRenders.load(std::memory_order_relaxed),
			g_counters.mainWorldPaneVisibility.load(std::memory_order_relaxed),
			g_counters.visibilitySelections.load(std::memory_order_relaxed),
			g_counters.unseenSelectionRejects.load(std::memory_order_relaxed),
			g_counters.ownershipLeaseHolds.load(std::memory_order_relaxed),
			g_counters.validatedVisibleContinuityRenewals.load(
				std::memory_order_relaxed),
			g_counters.validatedVisibleContinuityLeaseSelections.load(
				std::memory_order_relaxed),
			g_counters.ownerKeeps.load(std::memory_order_relaxed),
			g_counters.ownerSwitches.load(std::memory_order_relaxed),
			g_counters.unrelatedMutationsPreserved.load(
				std::memory_order_relaxed));
	}
}
