#pragma once
#include <cstdint>
namespace RE { class BSGeometry; class TESObjectREFR; }
namespace MirrorNifSurface { struct Snapshot; }

namespace MirrorTextureSetSurface
{
	inline constexpr auto kAuthoringMaster="RealisticReflectionsMirrors.esm";
	inline constexpr std::uint32_t kTextureSet=0x830;
	void OnDataLoaded() noexcept;
	void OnPreLoadGame() noexcept;
	void OnGameLoaded() noexcept;
	void Forget(std::uint32_t formID) noexcept;
	[[nodiscard]] bool Enabled() noexcept;
	[[nodiscard]] bool IsTaggedGeometry(RE::BSGeometry* geometry) noexcept;
	[[nodiscard]] bool Read(RE::TESObjectREFR* reference, MirrorNifSurface::Snapshot& output,
		bool copyTriangles) noexcept;
}
