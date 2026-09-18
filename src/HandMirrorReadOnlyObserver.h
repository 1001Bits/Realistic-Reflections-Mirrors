#pragma once

#include <cstdint>

namespace RE
{
	class BSRenderPass;
}

namespace HandMirrorReadOnlyObserver
{
	/** Value-only token spanning one native generic-geometry draw call. */
	struct GenericDrawToken
	{
		std::uint64_t mainViewEpoch{ 0 };
		std::uint64_t entrySequence{ 0 };
		std::uint8_t phaseOrdinal{ 0 };
		std::uint8_t path{ 0xFF };
		bool active{ false };
	};

	/** Inspect only the global diagnostic marker; installs nothing. */
	void PrepareAtInputLoaded() noexcept;
	[[nodiscard]] bool Requested() noexcept;

	/**
	 * Validate/install the sole first-person caller patch after the existing
	 * RenderWorld owner is ready.  The generic-draw seam is acquired through
	 * GeometryDrawObserver's single existing owner.
	 */
	void CompleteInputLoaded(bool renderWorldOwnerReady) noexcept;

	/** Register the process-lifetime TESEquipEvent wake-only sink and arm. */
	void OnDataLoaded() noexcept;
	/** Invalidate value epochs; performs no scene read. */
	void OnGameLoaded() noexcept;

	// Fanout from SecondView's already-owned RenderWorld caller wrapper.
	void OnPreWorld() noexcept;
	void OnEnterWorld() noexcept;
	void OnWorldReturnedNormally() noexcept;
	void OnWorldFinally() noexcept;

	// Owned unique RenderFirstPersonView caller wrapper.
	void OnPreFirstPerson() noexcept;
	void OnEnterFirstPerson() noexcept;
	void OnFirstPersonReturnedNormally() noexcept;
	void OnFirstPersonFinally() noexcept;

	// Skyrim VR: with VRIK the player's visible body is the third-person biped,
	// drawn inside RenderWorld, so the exact main-view scope wraps the world
	// pass instead of RenderFirstPersonView.  Driven from the RenderWorld caller.
	void VRWorldScopeEnter() noexcept;
	void VRWorldScopeReturned() noexcept;
	void VRWorldScopeFinally() noexcept;
	/** Fail-stop one fanout callback without affecting its native owner. */
	void FailStopCallbackFault() noexcept;

	// Fanout from GeometryDrawObserver's single generic-draw detour owner.
	[[nodiscard]] GenericDrawToken OnGenericDrawEntry(
		RE::BSRenderPass* pass) noexcept;
	void OnGenericDrawReturnedNormally(const GenericDrawToken& token) noexcept;

	[[nodiscard]] bool IsHookInstalled() noexcept;
	[[nodiscard]] bool IsEnabled() noexcept;
	[[nodiscard]] bool IsFaultStopped() noexcept;
	void MaybeLogPeriodic() noexcept;
	void LogDiagnostics(const char* reason);
}
