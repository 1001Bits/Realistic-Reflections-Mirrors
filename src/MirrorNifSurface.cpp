#include "PCH.h"
#include "MirrorNifSurface.h"
#include "HandMirrorSafetySettings.h"
#include "MirrorCaptureWorkPolicy.h"
#include "MirrorTextureSetSurface.h"

#include <RE/N/NiFloatsExtraData.h>
#include <cstring>

namespace MirrorNifSurface
{
	namespace
	{
		std::atomic_bool g_enabled{ false };
		stl::no_destructor<RE::BSFixedString> g_name{}, g_schema{}, g_plane{}, g_triangles{}, g_profile{};
		struct Raw
		{
			RE::BSGeometry* geometry{ nullptr };
			RE::NiTransform world{};
			MirrorNifContract::PlaneValues plane{};
			const float* triangles{ nullptr };
			std::uint32_t count{ 0 };
			std::uint64_t signature{ 0 };
			bool appCulled{ false };
		};
		struct ObservationCache
		{
			RE::TESObjectREFR* reference{};
			RE::NiAVObject* root{};
			Raw raw{};
			std::int32_t profile{};
		};
		thread_local std::uint32_t g_validationDepth{};
		thread_local ObservationCache g_cache{};

		[[nodiscard]] bool SupportedBase(RE::TESBoundObject* base) noexcept
		{
			if (!base) return false;
			switch (base->GetFormType()) {
			case RE::FormType::Static:
			case RE::FormType::MovableStatic:
			case RE::FormType::Activator:
			case RE::FormType::Misc:
			case RE::FormType::Armor:
			case RE::FormType::Weapon:
			case RE::FormType::Container:
			case RE::FormType::Furniture:
				return true;
			default:
				return false;
			}
		}
		[[nodiscard]] bool FindSinglePane(RE::NiAVObject* root, RE::BSGeometry*& result) noexcept
		{
			// Bounded traversal rejects cycles, duplicate declarations and oversized
			// scene graphs. Actors/equipped clones are deliberately a separate owner.
			RE::NiAVObject* pending[4096]{};
			std::size_t size = 1, visits = 0;
			pending[0] = root;
			while (size != 0) {
				auto* object = pending[--size];
				if (!object) continue;
				if (++visits > 4096) return false;
				if (object->name == g_name.get()) {
					if (result || !object->AsTriShape()) return false;
					result = object->AsTriShape();
				}
				if (auto* node = object->AsNode()) {
					const auto& children = node->GetChildren();
					if (children.size() > 4096-size) return false;
					for (const auto& child : children) pending[size++] = child.get();
				}
			}
			return result != nullptr;
		}
		[[nodiscard]] bool ReadRawSEH(RE::TESObjectREFR* reference, Raw& out) noexcept
		{
			__try {
				if (!reference || !SupportedBase(reference->GetObjectReference()) ||
					reference->IsDeleted() || reference->IsMarkedForDeletion() || reference->IsDisabled())
					return false;
				auto* root = reference->Get3D();
				const bool cached = g_validationDepth && g_cache.reference == reference && g_cache.root == root;
				RE::BSGeometry* pane = cached ? g_cache.raw.geometry : nullptr;
				if (!root || (!cached && !FindSinglePane(root,pane)) || !pane ||
					pane->name != g_name.get() || pane->GetUserData() != reference ||
					pane->GetType().underlying() != static_cast<std::uint8_t>(RE::BSGeometry::Type::kTriShape) ||
					pane->GetGeometryRuntimeData().skinInstance || pane->controllers)
					return false;
				auto* schema = netimmerse_cast<RE::NiIntegerExtraData*>(pane->GetExtraData(g_schema.get()));
				auto* profile = netimmerse_cast<RE::NiIntegerExtraData*>(pane->GetExtraData(g_profile.get()));
				auto* plane = netimmerse_cast<RE::NiFloatsExtraData*>(pane->GetExtraData(g_plane.get()));
				auto* triangles = netimmerse_cast<RE::NiFloatsExtraData*>(pane->GetExtraData(g_triangles.get()));
				if (!schema || schema->value != MirrorNifContract::kVersion || !profile ||
					(profile->value != 1 && profile->value != 2) || !plane || plane->size != 14 ||
					!plane->value || !triangles || !triangles->value || triangles->size == 0 ||
					triangles->size%6 != 0 || triangles->size > MirrorNifContract::kMaxTriangleVertices*2 ||
					triangles->size/6 != pane->AsTriShape()->GetTrishapeRuntimeData().triangleCount)
					return false;
				std::memcpy(out.plane.data(),plane->value,sizeof(out.plane));
				if (cached && g_cache.raw.triangles == triangles->value && g_cache.raw.count == triangles->size &&
					g_cache.profile == profile->value &&
					std::memcmp(out.plane.data(), g_cache.raw.plane.data(), sizeof(out.plane)) == 0) {
					out = g_cache.raw;
					out.world = pane->world;
					out.appCulled = pane->GetAppCulled();
					return true;
				}
				if (!MirrorNifContract::ValidPlane(out.plane) ||
					!MirrorNifContract::ValidFloatTriangles({ triangles->value, triangles->size }))
					return false;
				// Include the complete declaration in lifetime comparisons, including
				// contours with identical bounding planes. No borrowed data survives.
				std::uint64_t hash = 14695981039346656037ull;
				for (const auto v : out.plane) { hash ^= std::bit_cast<std::uint32_t>(v); hash *= 1099511628211ull; }
				for (std::uint32_t i = 0; i < triangles->size; ++i) {
					hash ^= std::bit_cast<std::uint32_t>(triangles->value[i]); hash *= 1099511628211ull;
				}
				hash ^= static_cast<std::uint32_t>(profile->value);
				out.geometry = pane;
				out.world = pane->world;
				out.triangles = triangles->value;
				out.count = triangles->size;
				out.signature = hash;
				out.appCulled = pane->GetAppCulled();
				if (g_validationDepth) g_cache = { reference, root, out, profile->value };
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
		[[nodiscard]] bool CopySEH(void* destination, const void* source, std::size_t bytes) noexcept
		{
			__try { std::memcpy(destination,source,bytes); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		class GeometryRetention final : public RE::NiPointer<RE::BSGeometry>
		{
		public:
			[[nodiscard]] bool RetainSEH(RE::BSGeometry* geometry) noexcept
			{
				if (_ptr || !geometry) return false;
				__try {
					// NiPointer::reset stores the pointer BEFORE IncRefCount. A
					// failed retain must not leave an unowned pointer to release.
					geometry->IncRefCount();
					_ptr = geometry;
					return true;
				} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
			}
		};
	}

	ValidationScope::ValidationScope() noexcept
	{
		active_ = MirrorCaptureWorkPolicy::enabled.load(std::memory_order_relaxed);
		if (active_ && g_validationDepth++ == 0) g_cache = {};
	}
	ValidationScope::~ValidationScope()
	{
		if (active_ && --g_validationDepth == 0) g_cache = {};
	}

	void OnDataLoaded() noexcept
	{
		MirrorTextureSetSurface::OnDataLoaded();
		g_enabled.store(false, std::memory_order_release);
		try {
			g_name.get() = MirrorNifContract::kPaneName.data();
			g_schema.get() = MirrorNifContract::kSchemaName.data();
			g_plane.get() = MirrorNifContract::kPlaneName.data();
			g_triangles.get() = MirrorNifContract::kTrianglesName.data();
			g_profile.get() = MirrorNifContract::kProfileName.data();
			const bool enabled = HandMirrorSafetySettings::EmptyMarker(kEnableMarker);
			g_enabled.store(enabled,std::memory_order_release);
			logger::info("[MirrorsOfSkyrim][NifMirrors] model surface schema=1 enabled={} (automatic; world references)",enabled);
		} catch (...) {
			g_enabled.store(false,std::memory_order_release);
		}
	}
	bool Enabled() noexcept { return g_enabled.load(std::memory_order_acquire) || MirrorTextureSetSurface::Enabled(); }
	bool IsNamedPane(RE::BSGeometry* geometry) noexcept
	{
		if (!Enabled()) return false;
		if (MirrorTextureSetSurface::IsTaggedGeometry(geometry)) return true;
		__try { return g_enabled.load(std::memory_order_acquire) && geometry && geometry->name == g_name.get(); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool Matches(RE::TESObjectREFR* reference) noexcept
	{
		if (!Enabled()) return false;
		Raw raw{};
		if (g_enabled.load(std::memory_order_acquire) && ReadRawSEH(reference,raw)) return true;
		Snapshot tagged{};
		return MirrorTextureSetSurface::Read(reference,tagged,false);
	}
	bool MatchesGeometry(RE::TESObjectREFR* reference, RE::BSGeometry* geometry) noexcept
	{
		if (!IsNamedPane(geometry)) return false;
		Raw raw{};
		if (g_enabled.load(std::memory_order_acquire) && ReadRawSEH(reference,raw)) return raw.geometry == geometry;
		Snapshot tagged{};
		return MirrorTextureSetSurface::Read(reference,tagged,false) && tagged.geometry.get()==geometry;
	}
	bool Read(RE::TESObjectREFR* reference, Snapshot& output, bool copyTriangles) noexcept
	{
		try {
			output = {};
			if (!Enabled()) return false;
			Raw raw{};
			if (!g_enabled.load(std::memory_order_acquire) || !ReadRawSEH(reference,raw))
				return MirrorTextureSetSurface::Read(reference,output,copyTriangles);
			Snapshot result{};
			GeometryRetention retained{};
			if (!retained.RetainSEH(raw.geometry)) return false;
			result.geometry = std::move(static_cast<RE::NiPointer<RE::BSGeometry>&>(retained));
			result.world = raw.world;
			result.localPlane = raw.plane;
			result.signature = raw.signature;
			result.appCulled = raw.appCulled;
			const auto x = raw.world.rotate.GetVectorX(), y = raw.world.rotate.GetVectorY(), z = raw.world.rotate.GetVectorZ();
			const MirrorNifContract::Transform transform{
				{raw.world.translate.x,raw.world.translate.y,raw.world.translate.z},
				{{{x.x,x.y,x.z},{y.x,y.y,y.z},{z.x,z.y,z.z}}},raw.world.scale };
			if (!MirrorNifContract::ToWorld(raw.plane,transform,result.plane)) return false;
			if (copyTriangles) {
				result.triangles.resize(raw.count/2);
				if (!CopySEH(result.triangles.data(),raw.triangles,raw.count*sizeof(float)) ||
					!MirrorNifContract::ValidTriangles(result.triangles)) return false;
			}
			output = std::move(result);
			return true;
		} catch (...) { output = {}; return false; }
	}
}
