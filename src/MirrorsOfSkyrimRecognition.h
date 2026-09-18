#pragma once

#include "MirrorInteriorEligibility.h"

#include <array>
#include <vector>
#include <cstdint>
#include <span>

#include <DirectXMath.h>

namespace RE
{
	class TESBoundObject;
	class TESObjectREFR;
	class TESObjectSTAT;
}

namespace MirrorRecognition
{
	using CurrentCandidateOperation = void (*)(void*) noexcept;

	/** Normalized world-space reflecting surface. */
	struct Plane
	{
		DirectX::XMFLOAT3 normal{ 0.0F, 0.0F, 1.0F };
		float distance{ 0.0F };
	};

	/** Value-only surface selection recovered from one standing-mirror transform. */
	struct PlaneSelection
	{
		Plane plane{};
		DirectX::XMFLOAT3 center{};
		DirectX::XMFLOAT3 authoredNormal{};
		DirectX::XMFLOAT3 tangent{};
		DirectX::XMFLOAT3 bitangent{};
		std::int32_t normalAxis{ -1 };
		float facingCosine{ 0.0F };
		float confidence{ 0.0F };
		bool viewOriented{ false };
		bool renderable{ false };
	};

	/** Thread-safe value snapshot of the closest renderable standing mirror. */
	struct ActiveMirror
	{
		std::uint32_t formID{ 0 };
		std::uint64_t candidateGeneration{ 0 };
		MirrorInteriorEligibility::CellIdentity parentCell{};
		PlaneSelection plane{};
		std::array<float, 3> worldHalfExtents{};
		std::array<DirectX::XMFLOAT3, 3> worldAxes{};
		std::uintptr_t nifPaneToken{ 0 };
		std::uint64_t nifSignature{ 0 };
		bool captureRequested{ true };
	};

	/** Resolve exact Mirrors of Skyrim STAT records and subscribe to reference events. */
	void OnDataLoaded();

	/** Admit standing-mirror selection only after mirror activation succeeds. */
	void OnMirrorActivationCommitted() noexcept;

	/** Install the single chained lighting-setup observer. */
	[[nodiscard]] bool EnsureLightingSetupObserverInstalled() noexcept;

	/** True only after the lighting-setup observer was installed successfully. */
	[[nodiscard]] bool LightingSetupObserverReady() noexcept;

	/** Resolve standing-mirror STATs if needed and scan the attached world. */
	void RecoverStandingMirrors(const char* reason);

	/** Clear stale candidates and enumerate the attached world after a save/new game. */
	void OnGameLoaded();
	/** Revoke selection and outgoing-save references before world teardown. */
	void OnPreLoadGame() noexcept;

	/** Emit standing-mirror recognition counters. */
	void LogDiagnostics(const char* reason);

	/** Return the exact anchor STAT used by standing-mirror pane delivery. */
	[[nodiscard]] RE::TESBoundObject* GetOwnedMirrorBase() noexcept;

	/** True when at least one exact standing-mirror STAT resolved. */
	[[nodiscard]] bool HasOwnedMirrorBase() noexcept;

	/** Exact RealisticReflectionsMirrors.esm:0x800 readiness used by mirror activation. */
	[[nodiscard]] bool MirrorsOfSkyrimDataReady() noexcept;

	/** Pointer-identity check against the exact standing-mirror catalog. */
	[[nodiscard]] bool IsOwnedMirrorBase(const RE::TESBoundObject* base) noexcept;

	/**
	 * Synchronously admit one newly placed persistent mirror.  This closes the
	 * vanilla PlaceAtMe event gap for the first-party inventory placement path.
	 */
	[[nodiscard]] bool ObserveCreatedReference(
		RE::TESObjectREFR* reference) noexcept;

	/** Record one successfully published standing-mirror reflection render. */
	void RecordReflectionRender() noexcept;

	/**
	 * Re-read transforms and return the closest usable mirror for the supplied eye.
	 * Only value data crosses the candidate mutex.
	 */
	[[nodiscard]] bool TryGetActiveMirror(
		const DirectX::XMFLOAT3& eye,
		ActiveMirror& output) noexcept;
	using ActiveMirrorSet = std::vector<ActiveMirror>;
	/** False means collection failed; an empty successful result retires the roster. */
	[[nodiscard]] bool GetActiveMirrors(
		const DirectX::XMFLOAT3& eye, ActiveMirrorSet& output) noexcept;
	[[nodiscard]] bool TryGetMirror(std::uint32_t formID, std::uint64_t generation,
		const DirectX::XMFLOAT3& eye, ActiveMirror& output) noexcept;

	/** True only when selection held a recent exact delivery owner. */
	[[nodiscard]] bool LastSelectionDeferredForOwnerLease() noexcept;

	/** Renew the bounded owner lease after a successful custom pane draw. */
	void RecordSuccessfulDeliveryOwner(
		std::uint32_t formID,
		std::uint64_t candidateGeneration) noexcept;

	enum class ValidatedVisibleContinuityResult : std::uint8_t
	{
		kRecorded,
		kInvalidIdentity,
		kLocationRejected,
		kNativeVisibilityMissing,
		kException
	};

	/** Renew the distinct bounded lease after exact native visibility proof. */
	[[nodiscard]] ValidatedVisibleContinuityResult
		RecordValidatedVisibleContinuityOwner(
			std::uint32_t formID,
			std::uint64_t candidateGeneration,
			MirrorInteriorEligibility::Status expectedStatus,
			const MirrorInteriorEligibility::LocationIdentity& expectedLocation) noexcept;

	/** Test a copied candidate identity without exposing its retained handle. */
	[[nodiscard]] bool IsCandidateCurrent(
		std::uint32_t formID,
		std::uint64_t candidateGeneration) noexcept;

	/** Re-resolve a candidate and require its current parent-cell identity. */
	[[nodiscard]] bool IsCandidateCurrentInParentCell(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		const MirrorInteriorEligibility::CellIdentity& expectedParentCell) noexcept;

	/** Re-resolve a candidate and validate its attached current location. */
	[[nodiscard]] bool IsCandidateCurrentAtLocation(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		MirrorInteriorEligibility::Status status,
		const MirrorInteriorEligibility::LocationIdentity& expectedLocation) noexcept;

	/**
	 * Run one non-reentrant operation while the exact ready candidate generation
	 * remains protected from revision or removal.
	 */
	[[nodiscard]] bool RunWhileCandidateCurrent(
		std::uint32_t formID,
		std::uint64_t candidateGeneration,
		CurrentCandidateOperation operation,
		void* state) noexcept;
}
