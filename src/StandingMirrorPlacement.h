#pragma once

namespace RE
{
	namespace BSScript
	{
		class IVirtualMachine;
	}
}

namespace StandingMirrorPlacement
{
	/** Register MOSStandingMirrorNative.BeginPlacement with Papyrus. */
	[[nodiscard]] bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm);

	/** Register the input sink after Skyrim has created its input manager. */
	void OnInputLoaded();

	/** Resolve five exact item/preview/placed triplets and register events. */
	void OnDataLoaded();

	/** Discard transient preview state after a save/new-game boundary. */
	void OnGameLoaded() noexcept;

	/** Quiesce placement without mutating the outgoing inventory during load. */
	void OnPreLoadGame() noexcept;

	void LogDiagnostics(const char* reason);
}
