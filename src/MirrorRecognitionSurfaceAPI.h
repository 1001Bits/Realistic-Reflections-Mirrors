#pragma once

#include <cstdint>

namespace RE
{
	class TESBoundObject;
	class TESObjectSTAT;
}

namespace MirrorRecognition
{
	/** Shared wall-pane surface queries implemented by either product owner. */
	[[nodiscard]] RE::TESBoundObject* GetOwnedMirrorBase() noexcept;
	[[nodiscard]] bool HasOwnedMirrorBase() noexcept;
	[[nodiscard]] bool IsOwnedMirrorBase(const RE::TESBoundObject* base) noexcept;
	[[nodiscard]] bool IsCandidateCurrent(
		std::uint32_t formID,
		std::uint64_t candidateGeneration) noexcept;
}
