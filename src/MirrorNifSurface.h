#pragma once

#include "MirrorNifContract.h"
#include <RE/B/BSGeometry.h>
#include <RE/T/TESObjectREFR.h>
#include <vector>

namespace MirrorNifSurface
{
	/** A synchronous observation may reuse its last validated immutable NIF
	 * declaration. No cache survives the outer scope or retains an engine owner.
	 * Do not hold this scope across engine calls that mutate a scene graph. */
	class ValidationScope
	{
	public:
		ValidationScope() noexcept;
		~ValidationScope();
		ValidationScope(const ValidationScope&) = delete;
		ValidationScope& operator=(const ValidationScope&) = delete;
	private:
		bool active_{ false };
	};
	// Internal identifier. The standalone product enables this by default;
	// no Data file is required or shipped.
	inline constexpr auto kEnableMarker = L"Data\\RealisticReflections_NifMirrors.enable";
	struct Snapshot
	{
		RE::NiPointer<RE::BSGeometry> geometry{};
		RE::NiTransform world{};
		MirrorNifContract::PlaneValues localPlane{};
		MirrorNifContract::WorldPane plane{};
		std::vector<DirectX::XMFLOAT2> triangles{};
		std::uint64_t signature{ 0 };
		bool appCulled{ false };
	};
	// Called only by the standalone mirror product. The shared development
	// product does not enable this author-facing runtime path.
	void OnDataLoaded() noexcept;
	[[nodiscard]] bool Enabled() noexcept;
	[[nodiscard]] bool Matches(RE::TESObjectREFR* reference) noexcept;
	[[nodiscard]] bool IsNamedPane(RE::BSGeometry* geometry) noexcept;
	[[nodiscard]] bool MatchesGeometry(RE::TESObjectREFR* reference, RE::BSGeometry* geometry) noexcept;
	[[nodiscard]] bool Read(RE::TESObjectREFR* reference, Snapshot& output,
		bool copyTriangles = true) noexcept;
}
