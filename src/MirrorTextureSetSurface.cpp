#include "PCH.h"
#include "MirrorTextureSetSurface.h"
#include "MirrorTextureSetGeometry.h"
#include "MirrorTextureSetPath.h"
#include "MirrorNifSurface.h"
#include <RE/B/BSLightingShaderMaterialBase.h>
#include <RE/N/NiSourceTexture.h>
#include <RE/T/TESModelTextureSwap.h>
#include <cstring>
#include <unordered_map>

namespace MirrorTextureSetSurface
{
	namespace
	{
		std::atomic_bool g_enabled{false},g_suspended{false};
		std::atomic_uint64_t g_epoch{0};
		std::atomic_uint32_t g_rejectLogs{0};
		RE::BGSTextureSet* g_textureSet{};
		struct Raw
		{
			RE::TESObjectREFR* reference{};
			RE::NiAVObject* root{};
			RE::BSGeometry* geometry{};
			RE::NiTransform world{};
			RE::BSGraphics::TriShape* renderer{};
			const std::uint8_t* vertices{};
			const std::uint16_t* indices{};
			std::uint64_t descriptor{};
			std::uint32_t formID{},vertexCount{},triangleCount{},index3D{};
			RE::BGSTextureSet* textureSet{};
			bool culled{};
			const char* issue{"unmarked"};
			std::uint8_t geometryType{};
			std::uint32_t materialType{};
			char markerPath[260]{},diffusePath[260]{};
		};
		struct Cached
		{
			RE::NiPointer<RE::NiAVObject> root{};
			RE::NiPointer<RE::BSGeometry> geometry{};
			Raw identity{};
			MirrorTextureSetGeometry::Surface surface{};
			std::uint64_t signature{};
		};
		std::mutex g_lock;
		stl::no_destructor<std::unordered_map<std::uint32_t,Cached>> g_cache;

		// Diagnostic strings are copied only during reference inspection, inside
		// ReadRawSEH. No engine string pointer escapes into asynchronous logging.
		void CopyDiagnosticPath(char (&output)[260],const char* input) noexcept
		{
			if (!input) return;
			for (std::size_t i=0;i<259;++i) {
				output[i]=input[i];
				if (!output[i]) return;
			}
			output[259]='\0';
		}
		[[nodiscard]] bool LiveMarker(RE::BSGeometry* geometry,Raw* diagnostic=nullptr) noexcept
		{
			if (!geometry) return false;
			if (diagnostic) diagnostic->issue="wrong-shader-property";
			auto* property=netimmerse_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
			if (!property) return false;
			if (diagnostic) diagnostic->issue="missing-lighting-material";
			auto* baseMaterial=property ? property->GetBaseMaterial() : nullptr;
			if (!baseMaterial) return false;
			const auto materialType=baseMaterial->GetType();
			if (diagnostic) {
				diagnostic->issue="wrong-material-type";
				diagnostic->materialType=static_cast<std::uint32_t>(materialType);
			}
			if (materialType!=RE::BSShaderMaterial::Type::kLighting) return false;
			auto* material=static_cast<RE::BSLightingShaderMaterialBase*>(baseMaterial);
			if (diagnostic) {
				diagnostic->issue="missing-material-texture-set";
				if (material->diffuseTexture) CopyDiagnosticPath(diagnostic->diffusePath,material->diffuseTexture->name.c_str());
			}
			if (!material->textureSet) return false;
			const char* path=material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
			const bool matches=MirrorTextureSetPath::IsMarkerPath(path);
			if (diagnostic && !matches) {
				diagnostic->issue="live-texture-path-mismatch";
				CopyDiagnosticPath(diagnostic->markerPath,path);
			}
			return matches;
		}
		[[nodiscard]] RE::TESModelTextureSwap::AlternateTexture* Assignment(RE::TESObjectREFR* reference) noexcept
		{
			if (!reference || reference->IsDeleted() || reference->IsMarkedForDeletion() || reference->IsDisabled()) return nullptr;
			auto* base=reference->GetObjectReference();
			if (!base) return nullptr;
			switch (base->GetFormType()) {
			case RE::FormType::Static: case RE::FormType::MovableStatic: case RE::FormType::Activator:
			case RE::FormType::Misc: case RE::FormType::Weapon: case RE::FormType::Container: case RE::FormType::Furniture:
				break;
			default: return nullptr;
			}
			auto* model=base->As<RE::TESModel>();
			auto* swaps=model ? model->GetAsModelTextureSwap() : nullptr;
			if (!swaps || !swaps->alternateTextures || swaps->numAlternateTextures>4096) return nullptr;
			RE::TESModelTextureSwap::AlternateTexture* found=nullptr;
			for (std::uint32_t i=0;i<swaps->numAlternateTextures;++i) {
				auto* item=&swaps->alternateTextures[i];
				if (item->textureSet!=g_textureSet) continue;
				// One named static part defines one reflection plane.
				// That part may contain disconnected coplanar islands and holes.
				if (found || item->name3D.empty()) return nullptr;
				found=item;
			}
			return found;
		}
		[[nodiscard]] bool ReadRawSEH(RE::TESObjectREFR* reference,Raw& out) noexcept
		{
			__try {
				auto* assignment=Assignment(reference);
				if (!assignment) return false;
				out.formID=reference->GetFormID();out.issue="missing-or-ambiguous-static-3D";
				auto* root=reference->Get3D();
				if (!root) return false;
				RE::NiAVObject* pending[4096]{};
				std::size_t size=1,visits=0;
				pending[0]=root;
				RE::BSGeometry* pane=nullptr;
				while (size) {
					auto* object=pending[--size];
					if (!object) continue;
					if (++visits>4096 || object->controllers) return false;
					// Reject simultaneous NIF declarations instead of silently
					// choosing a different source of reflection authority.
					const char* name=object->name.c_str();
					if (name && std::strcmp(name,MirrorNifContract::kPaneName.data())==0) return false;
					if (object->name==assignment->name3D) {
						if (pane || !object->AsTriShape()) return false;
						pane=object->AsTriShape();
					}
					if (auto* node=object->AsNode()) {
						const auto& children=node->GetChildren();
						if (children.size()>4096-size) return false;
						for (auto& child:children) pending[size++]=child.get();
					}
				}
				out.issue="missing-matching-pane";
				if (!pane) return false;
				out.issue="pane-owner-mismatch";
				if (pane->GetUserData()!=reference) return false;
				out.issue="unsupported-geometry-type";
				out.geometryType=pane->GetType().underlying();
				if (out.geometryType!=static_cast<std::uint8_t>(RE::BSGeometry::Type::kTriShape)) return false;
				out.issue="skinned-pane";
				if (pane->GetGeometryRuntimeData().skinInstance || !LiveMarker(pane,&out)) return false;
				auto* renderer=pane->GetGeometryRuntimeData().rendererData;
				const auto& shape=pane->AsTriShape()->GetTrishapeRuntimeData();
				out.issue="static-CPU-geometry-unavailable";
				if (!renderer || !renderer->rawVertexData || !renderer->rawIndexData ||
					!shape.vertexCount || !shape.triangleCount) return false;
				out.reference=reference;out.root=root;out.geometry=pane;out.world=pane->world;
				out.renderer=renderer;out.vertices=renderer->rawVertexData;out.indices=renderer->rawIndexData;
				out.descriptor=std::bit_cast<std::uint64_t>(renderer->vertexDesc);
				out.issue="vertex-descriptor-mismatch";
				if (out.descriptor!=std::bit_cast<std::uint64_t>(pane->GetGeometryRuntimeData().vertexDesc)) return false;
				out.formID=reference->GetFormID();out.vertexCount=shape.vertexCount;out.triangleCount=shape.triangleCount;
				out.index3D=assignment->index3D;out.textureSet=assignment->textureSet;
				out.culled=pane->GetAppCulled();
				out.issue="ready";
				return out.formID!=0;
			} __except(EXCEPTION_EXECUTE_HANDLER) { out.issue="native-read-fault";return false; }
		}
		[[nodiscard]] bool SameSource(const Raw& a,const Raw& b) noexcept
		{
			return a.reference==b.reference && a.root==b.root && a.geometry==b.geometry && a.renderer==b.renderer &&
				a.vertices==b.vertices && a.indices==b.indices && a.descriptor==b.descriptor &&
				a.vertexCount==b.vertexCount && a.triangleCount==b.triangleCount && a.index3D==b.index3D &&
				a.textureSet==b.textureSet;
		}
		[[nodiscard]] bool CopySEH(void* destination,const void* source,std::size_t bytes) noexcept
		{
			__try { std::memcpy(destination,source,bytes);return true; }
			__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		template<class T> class Retention final : public RE::NiPointer<T>
		{
		public:
			[[nodiscard]] bool Acquire(T* object) noexcept
			{
				if (this->_ptr || !object) return false;
				__try { object->IncRefCount();this->_ptr=object;return true; }
				__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
			}
		};
	}
	bool Enabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire) && !g_suspended.load(std::memory_order_acquire);
	}
	void OnDataLoaded() noexcept
	{
		g_enabled.store(false,std::memory_order_release);
		g_epoch.fetch_add(1,std::memory_order_acq_rel);
		g_rejectLogs.store(0,std::memory_order_relaxed);
		g_textureSet=nullptr;
		try {
			std::scoped_lock lock{g_lock};g_cache.get().clear();
			// Loading the shared authoring texture set enables CK assignments.
			// Existing addons keep the same master and local form identity.
			if (auto* data=RE::TESDataHandler::GetSingleton()) {
				g_textureSet=data->LookupForm<RE::BGSTextureSet>(kTextureSet,kAuthoringMaster);
			}
			g_suspended.store(false,std::memory_order_release);
			g_enabled.store(g_textureSet!=nullptr,std::memory_order_release);
			logger::info("[MirrorsOfSkyrim][CreationKitMirrors] enabled={} master={} surface=MOS_MirrorSurface (automatic; one flat static part)",Enabled(),kAuthoringMaster);
		} catch (...) { g_enabled.store(false,std::memory_order_release); }
	}
	void OnPreLoadGame() noexcept
	{
		g_suspended.store(true,std::memory_order_release);
		g_epoch.fetch_add(1,std::memory_order_acq_rel);
		try { std::scoped_lock lock{g_lock};g_cache.get().clear(); } catch (...) {}
	}
	void OnGameLoaded() noexcept
	{
		try { std::scoped_lock lock{g_lock};g_cache.get().clear();g_suspended.store(false,std::memory_order_release); } catch (...) {}
	}
	void Forget(std::uint32_t formID) noexcept
	{
		if (!g_enabled.load(std::memory_order_acquire)) return;
		try { std::scoped_lock lock{g_lock};g_cache.get().erase(formID); } catch (...) {}
	}
	bool IsTaggedGeometry(RE::BSGeometry* geometry) noexcept
	{
		if (!Enabled()) return false;
		__try {
			if (!geometry || !LiveMarker(geometry)) return false;
			auto* assignment=Assignment(geometry->GetUserData());
			return assignment && assignment->name3D==geometry->name;
		} __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool Read(RE::TESObjectREFR* reference,MirrorNifSurface::Snapshot& output,bool copyTriangles) noexcept
	{
		output={};
		if (!Enabled()) return false;
		try {
			const auto epoch=g_epoch.load(std::memory_order_acquire);
			Raw raw{};
			if (!ReadRawSEH(reference,raw)) {
				if (raw.formID && g_rejectLogs.fetch_add(1,std::memory_order_relaxed)<16)
					logger::info("[MirrorsOfSkyrim][CreationKitMirrors] rejected ref={:08X} reason={} geometryType={} materialType={} materialPath='{}' diffusePath='{}' (limited to first 16 raw rejects)",
						raw.formID,raw.issue,raw.geometryType,raw.materialType,raw.markerPath,raw.diffusePath);
				return false;
			}
			std::scoped_lock lock{g_lock};
			if (!Enabled() || epoch!=g_epoch.load(std::memory_order_acquire)) return false;
			auto it=g_cache.get().find(raw.formID);
			if (it!=g_cache.get().end() && !SameSource(raw,it->second.identity)) {
				g_cache.get().erase(it);it=g_cache.get().end();
			}
			if (it==g_cache.get().end()) {
				const auto stride=static_cast<std::size_t>(raw.descriptor&15u)*4;
				if (stride<16 || stride>60) return false;
				Retention<RE::NiAVObject> root;
				Retention<RE::BSGeometry> geometry;
				if (!root.Acquire(raw.root) || !geometry.Acquire(raw.geometry)) return false;
				std::vector<std::byte> vertices(raw.vertexCount*stride);
				std::vector<std::uint16_t> indices(static_cast<std::size_t>(raw.triangleCount)*3);
				if (!CopySEH(vertices.data(),raw.vertices,vertices.size()) ||
					!CopySEH(indices.data(),raw.indices,indices.size()*sizeof(std::uint16_t))) return false;
				std::vector<MirrorNifContract::Vec3> positions;
				Cached pending;
				const bool valid=MirrorTextureSetGeometry::Decode(vertices,indices,raw.vertexCount,raw.descriptor,positions) &&
					MirrorTextureSetGeometry::Build(positions,pending.surface);
				Raw after{};
				if (!ReadRawSEH(reference,after) || !SameSource(raw,after)) return false;
				pending.root=std::move(static_cast<RE::NiPointer<RE::NiAVObject>&>(root));
				pending.geometry=std::move(static_cast<RE::NiPointer<RE::BSGeometry>&>(geometry));
				pending.identity=raw;
				std::uint64_t hash=14695981039346656037ull;
				const auto hashValue=[&](float value) { hash^=std::bit_cast<std::uint32_t>(value);hash*=1099511628211ull; };
				for (float value:pending.surface.plane) hashValue(value);
				for (auto v:pending.surface.triangles) { hashValue(v.x);hashValue(v.y); }
				hash^=0x434b5458535431ull;hash*=1099511628211ull;
				pending.signature=hash;
				it=g_cache.get().emplace(raw.formID,std::move(pending)).first;
				logger::info("[MirrorsOfSkyrim][CreationKitMirrors] {} ref={:08X} part={} triangles={} reason={}",
					valid ? "accepted" : "rejected",raw.formID,raw.index3D,raw.triangleCount,
					valid ? "flat-static-part" : "nonplanar-inconsistent-facing-or-invalid-static-stream");
			}
			const auto& cached=it->second;
			// Remember geometric rejection while this exact static source is loaded;
			// a mistakenly tagged curved frame must not be decoded every draw/frame.
			if (cached.surface.triangles.empty()) return false;
			MirrorNifSurface::Snapshot result;
			result.geometry=cached.geometry;result.world=raw.world;result.localPlane=cached.surface.plane;
			result.signature=cached.signature;result.appCulled=raw.culled;
			const auto x=raw.world.rotate.GetVectorX(),y=raw.world.rotate.GetVectorY(),z=raw.world.rotate.GetVectorZ();
			const MirrorNifContract::Transform transform{{raw.world.translate.x,raw.world.translate.y,raw.world.translate.z},
				{{{x.x,x.y,x.z},{y.x,y.y,y.z},{z.x,z.y,z.z}}},raw.world.scale};
			if (!MirrorNifContract::ToWorld(result.localPlane,transform,result.plane)) return false;
			if (copyTriangles) result.triangles=cached.surface.triangles;
			if (!Enabled() || epoch!=g_epoch.load(std::memory_order_acquire)) return false;
			output=std::move(result);
			return true;
		} catch (...) { output={};return false; }
	}
}
