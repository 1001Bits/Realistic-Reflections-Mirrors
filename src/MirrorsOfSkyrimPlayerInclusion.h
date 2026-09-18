#pragma once

#include "MirrorPlayerDrawPassProbe.h"

#include <cstdint>

#include <DirectXMath.h>

namespace RE
{
	class NiAVObject;
}

namespace MirrorPlayerInclusion
{
	/**
	 * Describes whether the retained body/base player root is already reached
	 * by the world-scene cull or needs one additional native descriptor cull.
	 */
	enum class Coverage : std::uint8_t
	{
		kDisabled,
		kUnsupported,
		kNoPlayerRoot,
		kCoveredByWorldRoot,
		kSeparateRoot,
		kFailed
	};

	/** Prepare the exact draw observer after the shared mirror hooks are ready. */
	void OnInputLoaded(
		bool secondViewHooksReady,
		bool mirrorCaptureRequested,
		bool handMirrorCaptureRequested);

	/** Arm prepared behavior only after the shared mirror pass becomes active. */
	void OnDataLoaded(
		bool secondViewEnabled,
		bool handMirrorCaptureEnabled);

	/** Promote the wall-mirror scope after clean mirror activation commits. */
	void OnActivationCommitted() noexcept;

	/** Retry a retained catastrophic app-cull rollback at a safe load boundary. */
	void OnGameLoaded() noexcept;

	[[nodiscard]] bool IsEnabled() noexcept;
	/** True after the shared exact-target player draw observer was installed. */
	[[nodiscard]] bool HookReady() noexcept;
	[[nodiscard]] bool IsEnabledFor(
		PlayerDrawPassProbe::CaptureKind captureKind) noexcept;

	/**
	 * Value-only snapshot used by mirror admission even when player inclusion is
	 * disabled. The explicit body root is retained only while its conservative
	 * world bound is copied; no scene-graph pointer escapes.
	 */
	struct BodyBoundsSnapshot
	{
		std::uintptr_t rootAddress{ 0 };
		DirectX::XMFLOAT3 playerPosition{};
		DirectX::XMFLOAT3 boundCenter{};
		float boundRadius{ 0.0f };
	};

	/** Exact value-only result retained after the synchronous draw probe closes. */
	struct FinalizedDrawEvidence
	{
		PlayerDrawPassProbe::Result probe{};
		Coverage coverage{ Coverage::kFailed };
		std::uintptr_t retainedRootIdentity{ 0 };
		std::uintptr_t expectedRenderTargetIdentity{ 0 };
		bool probeArmed{ false };
		bool finalized{ false };
		bool nativeDrawDispatched{ false };
	};

	[[nodiscard]] bool TrySnapshotBodyBounds(
		BodyBoundsSnapshot& output) noexcept;

	enum class WorldCullExclusionMode : std::uint8_t { kHandComposition, kMirrorComposition };

	/**
	 * Exact-runtime, synchronous scope for the private mirror pass. It retains
	 * PlayerCharacter::Get3D1(false), saves and clears only that body root's
	 * app-cull bit.  The hand path also requires expectedBodyRoot to equal the
	 * synchronously retained Get3D1(false) identity used by its clip evidence;
	 * the wall route leaves that optional identity null.
	 *
	 * classifies its relationship to Main::WorldRootNode, and restores the exact
	 * saved bit on every explicit/fallback exit.
	 */
	class Scope
	{
	public:
		explicit Scope(
			RE::NiAVObject* worldSceneRoot,
			ID3D11RenderTargetView* expectedRenderTarget,
			PlayerDrawPassProbe::CaptureKind captureKind =
				PlayerDrawPassProbe::CaptureKind::kMirror,
			RE::NiAVObject* expectedBodyRoot = nullptr) noexcept;
		~Scope();

		Scope(const Scope&) = delete;
		Scope(Scope&&) = delete;
		Scope& operator=(const Scope&) = delete;
		Scope& operator=(Scope&&) = delete;

		/** True when the private pass may proceed safely. */
		[[nodiscard]] bool Ready() const noexcept;

		/**
		 * Non-null when the body/base player root needs an independent descriptor
		 * cull. The exact hand route may instead use
		 * BeginWorldCullExclusion/RestoreForExplicitCull to prove disjoint direct
		 * culls into one accumulator before a single wrapper dispatch.
		 * The pointer remains retained until this scope is destroyed.
		 */
		[[nodiscard]] RE::NiAVObject* AdditionalCullRoot() const noexcept;

		[[nodiscard]] Coverage GetCoverage() const noexcept;

		/**
		 * Exact hand or mirror composition lease, also used by private shadows.
		 * Hide the already-retained body root
		 * while WorldRoot is culled, then restore it for one explicit cull into the
		 * same accumulator. This makes the culls disjoint while leaving Skyrim one
		 * native opaque/ordered-alpha dispatch for the room and player. Close()
		 * remains the final owner of restoration to the entry app-cull value.
		 */
		[[nodiscard]] bool BeginWorldCullExclusion(
			RE::NiAVObject* expectedPlayerRoot,
			WorldCullExclusionMode mode = WorldCullExclusionMode::kHandComposition) noexcept;
		[[nodiscard]] bool RestoreForExplicitCull(
			RE::NiAVObject* expectedPlayerRoot,
			WorldCullExclusionMode mode = WorldCullExclusionMode::kHandComposition) noexcept;

		/**
		 * End the target-matched post-draw observation. The return value reports
		 * whether at least one returned skinned BSLightingShader draw belonged to
		 * the retained body root; it is diagnostic because zero draws can also mean
		 * that the player is legitimately outside the reflected frustum.
		 */
		[[nodiscard]] bool FinishDrawEvidence(bool nativeDrawDispatched) noexcept;
		/** Cached exact-target result after FinishDrawEvidence has closed the probe. */
		[[nodiscard]] bool PlayerDrawEvidenceConfirmed() const noexcept;
		/** Full value-only proof for fail-closed publication admission. */
		[[nodiscard]] FinalizedDrawEvidence ReadFinalizedDrawEvidence() const noexcept;
		/** Sticky guarded/native invariant fault; inspect only after Close(). */
		[[nodiscard]] bool NativeFaulted() const noexcept;

		/**
		 * Re-assert the opaque-body fade immediately before the private dispatch.
		 * The descriptor cull runs with `cameraRelatedUpdates`, which lets Skyrim
		 * recompute `BSFadeNode::currentFade` from camera distance and undo the
		 * lease taken at scope construction — that is why a body faded by a nearby
		 * wall still reflected see-through. No-op when no lease is held.
		 */
		void ReapplyFade() noexcept;

		/** Restore the exact saved fade and app-cull values. Idempotent; Close retries. */
		[[nodiscard]] bool Restore() noexcept;

		/**
		 * Restore the scoped bit and relinquish (or deliberately transfer) the
		 * retained root. This is idempotent and can be called from an SEH
		 * __finally block without relying on C++ unwinding.
		 */
		[[nodiscard]] bool Close() noexcept;

	private:
		[[nodiscard]] bool ReleaseRetainedRoot() noexcept;
		[[nodiscard]] bool ReleaseRollbackOwnerLease(
			bool retainedReferenceReleased) noexcept;

		RE::NiAVObject* playerRoot{ nullptr };
		Coverage coverage{ Coverage::kFailed };
		bool priorAppCulled{ false };
		bool retained{ false };
		// Process-local singleton lease. A failed fade and/or app-cull rollback
		// transfers this lease together with the already-retained root into the fixed
		// recovery slot; it is never released while either mutation is unresolved.
		bool rollbackOwnerLease{ false };
		bool appCullScoped{ false };
		bool worldCullExclusionActive{ false };
		WorldCullExclusionMode worldCullExclusionMode{ WorldCullExclusionMode::kHandComposition };
		bool fadeScoped{ false };
		float priorFadeAlpha{ 1.0f };
		PlayerDrawPassProbe::CaptureKind captureKind{
			PlayerDrawPassProbe::CaptureKind::kMirror };
		PlayerDrawPassProbe::Session drawProbe{};
		PlayerDrawPassProbe::Result drawEvidenceResult{};
		std::uintptr_t retainedRootIdentity{ 0 };
		std::uintptr_t expectedRenderTargetIdentity{ 0 };
		bool drawProbeArmed{ false };
		bool drawEvidenceFinalized{ false };
		bool drawEvidenceNativeDrawDispatched{ false };
		bool drawEvidenceReady{ false };
		bool nativeFaulted{ false };
		bool closed{ false };
		bool closeSucceeded{ false };
	};

	/** Emit aggregate scope/coverage/restore diagnostics. */
	void LogDiagnostics(const char* reason);
}

