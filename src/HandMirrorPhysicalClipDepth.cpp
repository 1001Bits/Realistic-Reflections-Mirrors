#if !defined(MIRRORS_OF_SKYRIM_PHYSICAL_CLIP_DEPTH_TEST)
#	include "PCH.h"
#endif

#include "HandMirrorPhysicalClipDepth.h"

#include "D3D11OutputMergerState.h"
#include "EngineDeviceIdentity.h"
#include "HandMirrorLoweredPresentationPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

#include <wrl/client.h>

namespace HandMirrorPhysicalClipDepth
{
	namespace
	{
#include "HandMirrorPhysicalDepthComputeShader.inc"

		constexpr std::size_t kTargetSlotCount = 2;
		constexpr std::size_t kSpareSlotCount = 14;
		// Reusable R32 outputs, including the two active slots: the same 128 MiB
		// as two 4096-square outputs. Active results are never evicted; an unusual
		// larger framebuffer can exceed this alone, in which case no spares remain.
		constexpr std::uint64_t kStorageBudget = 128ULL * 1024 * 1024;
		constexpr UINT kClassInstanceCapacity = 256;

		[[nodiscard]] bool GetComputeShader(
			ID3D11DeviceContext* context,
			ID3D11ComputeShader** shader,
			ID3D11ClassInstance** classes,
			UINT* count) noexcept
		{
			// Only the returned count transfers references. A forwarding getter
			// can leave arbitrary values in the unused tail of the caller's array.
			*count = 0;
			UINT reportedCount = kClassInstanceCapacity;
			context->CSGetShader(shader, classes, &reportedCount);
#if defined(MIRRORS_OF_SKYRIM_PHYSICAL_CLIP_DEPTH_TEST)
			if (Testing::afterComputeShaderQuery)
				Testing::afterComputeShaderQuery(classes, kClassInstanceCapacity, reportedCount);
#endif
			*count = (std::min)(reportedCount, kClassInstanceCapacity);
			for (UINT index = *count; index < kClassInstanceCapacity; ++index)
				classes[index] = nullptr;
			return reportedCount <= kClassInstanceCapacity;
		}

		struct Constants
		{
			DirectX::XMFLOAT4X4 inverseObliqueProjection{};
			DirectX::XMFLOAT4X4 conventionalProjection{};
			std::uint32_t width{ 0 };
			std::uint32_t height{ 0 };
			std::uint32_t padding0{ 0 };
			std::uint32_t padding1{ 0 };
		};
		static_assert(sizeof(Constants) == 0x90);

		struct TargetSlot
		{
			std::uintptr_t targetIdentity{ 0 };
			std::uint32_t width{ 0 };
			std::uint32_t height{ 0 };
			std::uint64_t resolvedGeneration{ 0 };
			std::uint64_t resolvedDepthSnapshotEpoch{ 0 };
			DirectX::XMFLOAT4X4 resolvedConventionalProjection{};
			DirectX::XMFLOAT4X4 resolvedObliqueProjection{};
			Microsoft::WRL::ComPtr<ID3D11Resource> sourceDepthResource{};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
			Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav{};
			std::uint64_t lastUse{ 0 };
		};

		struct DeviceState
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device{};
			Microsoft::WRL::ComPtr<ID3D11Device> resourceDevice{};
			Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader{};
			Microsoft::WRL::ComPtr<ID3D11Buffer> constants{};
			std::array<TargetSlot, kTargetSlotCount> slots{};
			std::array<TargetSlot, kSpareSlotCount> spares{};
			std::uint64_t clock{};
		};

		struct Counters
		{
			std::atomic<std::uint64_t> deviceInitializations{ 0 };
			std::atomic<std::uint64_t> resourceCreations{ 0 };
			std::atomic<std::uint64_t> storageReuses{ 0 };
			std::atomic<std::uint64_t> storageEvictions{ 0 };
			std::atomic<std::uint64_t> storageBytes{ 0 };
			std::atomic<std::uint64_t> resolveAttempts{ 0 };
			std::atomic<std::uint64_t> dispatches{ 0 };
			std::atomic<std::uint64_t> cacheHits{ 0 };
			std::atomic<std::uint64_t> restores{ 0 };
			std::atomic<std::uint64_t> rejects{ 0 };
			std::atomic<std::uint64_t> faults{ 0 };
		};

		thread_local DeviceState g_device{};
		Counters g_counters{};
		std::atomic_bool g_faulted{ false };

		[[nodiscard]] bool FiniteMatrix(
			const DirectX::XMFLOAT4X4& matrix) noexcept
		{
			const float* values = &matrix._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!std::isfinite(values[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool SameMatrixBits(
			const DirectX::XMFLOAT4X4& left,
			const DirectX::XMFLOAT4X4& right) noexcept
		{
			return std::memcmp(&left, &right, sizeof(left)) == 0;
		}

		[[nodiscard]] bool ValidProjectionPair(
			const ProjectionPair& projection) noexcept
		{
			if (!projection.valid || projection.generation == 0 ||
				!FiniteMatrix(projection.conventional) ||
				!FiniteMatrix(projection.oblique) ||
				SameMatrixBits(projection.conventional, projection.oblique)) {
				return false;
			}
			// Oblique clipping is a column-2 rewrite. X, Y and W must remain the
			// frozen conventional projection so portrait framing cannot change.
			const float* conventional = &projection.conventional._11;
			const float* oblique = &projection.oblique._11;
			for (std::size_t index = 0; index < 16; ++index) {
				const std::size_t column = index % 4;
				if (column != 2 &&
					std::bit_cast<std::uint32_t>(conventional[index]) !=
						std::bit_cast<std::uint32_t>(oblique[index])) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool ValidateDepthBindings(
			const Bindings& bindings,
			ID3D11Resource* sourceDepthResource) noexcept
		{
			if (!sourceDepthResource || !bindings.colorRTV ||
				!bindings.writableDepthDSV || !bindings.readOnlyDepthDSV ||
				!bindings.obliqueDepthSRV) {
				return false;
			}
			Microsoft::WRL::ComPtr<ID3D11Resource> writableDepth{};
			Microsoft::WRL::ComPtr<ID3D11Resource> readOnlyDepth{};
			Microsoft::WRL::ComPtr<ID3D11Resource> color{};
			bindings.writableDepthDSV->GetResource(writableDepth.GetAddressOf());
			bindings.readOnlyDepthDSV->GetResource(readOnlyDepth.GetAddressOf());
			bindings.colorRTV->GetResource(color.GetAddressOf());
			if (!writableDepth || !readOnlyDepth || !color ||
				writableDepth.Get() != sourceDepthResource ||
				readOnlyDepth.Get() != sourceDepthResource ||
				color.Get() == sourceDepthResource) {
				return false;
			}

			D3D11_DEPTH_STENCIL_VIEW_DESC writableDescription{};
			D3D11_DEPTH_STENCIL_VIEW_DESC readOnlyDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderDescription{};
			bindings.writableDepthDSV->GetDesc(&writableDescription);
			bindings.readOnlyDepthDSV->GetDesc(&readOnlyDescription);
			bindings.obliqueDepthSRV->GetDesc(&shaderDescription);
			if (writableDescription.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
				readOnlyDescription.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
				shaderDescription.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				writableDescription.Texture2D.MipSlice != shaderDescription.Texture2D.MostDetailedMip ||
				readOnlyDescription.Texture2D.MipSlice != shaderDescription.Texture2D.MostDetailedMip ||
				writableDescription.Format != DXGI_FORMAT_D24_UNORM_S8_UINT ||
				readOnlyDescription.Format != DXGI_FORMAT_D24_UNORM_S8_UINT ||
				shaderDescription.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
				(writableDescription.Flags & D3D11_DSV_READ_ONLY_DEPTH) != 0 ||
				(readOnlyDescription.Flags & D3D11_DSV_READ_ONLY_DEPTH) == 0) {
				return false;
			}

			Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture{};
			if (FAILED(sourceDepthResource->QueryInterface(
					IID_PPV_ARGS(depthTexture.GetAddressOf()))) || !depthTexture) {
				return false;
			}
			D3D11_TEXTURE2D_DESC depthDescription{};
			depthTexture->GetDesc(&depthDescription);
			return HandMirrorLoweredPresentationPolicy::DepthViewDimensionsMatch(
				depthDescription.Width, depthDescription.Height, depthDescription.MipLevels,
				shaderDescription.Texture2D.MostDetailedMip, shaderDescription.Texture2D.MipLevels,
				bindings.width, bindings.height,
				bindings.reducedViewsAllowed ||
					HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed)) &&
				depthDescription.ArraySize == 1 &&
				depthDescription.SampleDesc.Count == 1;
		}

		void EnterFault() noexcept
		{
			if (!g_faulted.exchange(true, std::memory_order_acq_rel))
				g_counters.faults.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] std::uint64_t StorageBytes() noexcept
		{
			std::uint64_t bytes = 0;
			for (const auto& slot : g_device.slots)
				if (slot.texture) bytes += std::uint64_t(slot.width) * slot.height * 4;
			for (const auto& slot : g_device.spares)
				if (slot.texture) bytes += std::uint64_t(slot.width) * slot.height * 4;
			return bytes;
		}

		void TrimStorage() noexcept
		{
			while (StorageBytes() > kStorageBudget) {
				TargetSlot* oldest = nullptr;
				for (auto& spare : g_device.spares)
					if (spare.texture && (!oldest || spare.lastUse < oldest->lastUse)) oldest = &spare;
				if (!oldest) break;
				*oldest = {};
				g_counters.storageEvictions.fetch_add(1, std::memory_order_relaxed);
			}
			g_counters.storageBytes.store(StorageBytes(), std::memory_order_relaxed);
		}

		void ParkStorage(TargetSlot&& slot) noexcept
		{
			if (!slot.texture) return;
			TargetSlot* oldest = &g_device.spares[0];
			for (auto& spare : g_device.spares) {
				if (!spare.texture) { oldest = &spare; break; }
				if (spare.lastUse < oldest->lastUse) oldest = &spare;
			}
			if (oldest->texture) g_counters.storageEvictions.fetch_add(1, std::memory_order_relaxed);
			// Spare storage must not retain a capture's depth or a valid result key.
			// In particular, holding sourceDepthResource here would extend the main
			// capture pool's physical memory beyond its own eviction budget.
			slot.sourceDepthResource.Reset();
			slot.targetIdentity = slot.resolvedGeneration = slot.resolvedDepthSnapshotEpoch = 0;
			slot.resolvedConventionalProjection = {};
			slot.resolvedObliqueProjection = {};
			*oldest = std::move(slot);
		}

		[[nodiscard]] bool CreateTargetSlot(
			TargetSlot& slot,
			ID3D11Device* device,
			ID3D11Resource* sourceDepthResource,
			const Bindings& bindings) noexcept
		{
			if (!device || !sourceDepthResource || bindings.targetIdentity == 0 ||
				bindings.width == 0 || bindings.height == 0)
				return false;

			TargetSlot replacement{};
			if (slot.width == bindings.width && slot.height == bindings.height &&
				slot.texture && slot.srv && slot.uav) {
				replacement = std::move(slot);
				slot = {};
			} else {
				for (auto& spare : g_device.spares) {
					if (spare.width == bindings.width && spare.height == bindings.height &&
						spare.texture && spare.srv && spare.uav) {
						replacement = std::move(spare);
						spare = {};
						break;
					}
				}
			}
			if (replacement.texture) {
				g_counters.storageReuses.fetch_add(1, std::memory_order_relaxed);
			} else {
				D3D11_TEXTURE2D_DESC description{};
				description.Width = bindings.width;
				description.Height = bindings.height;
				description.MipLevels = 1;
				description.ArraySize = 1;
				description.Format = DXGI_FORMAT_R32_FLOAT;
				description.SampleDesc.Count = 1;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags =
					D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

				if (FAILED(device->CreateTexture2D(
						&description, nullptr, replacement.texture.GetAddressOf())) ||
					FAILED(device->CreateShaderResourceView(
						replacement.texture.Get(), nullptr, replacement.srv.GetAddressOf())) ||
					FAILED(device->CreateUnorderedAccessView(
						replacement.texture.Get(), nullptr, replacement.uav.GetAddressOf()))) {
					return false;
				}
				g_counters.resourceCreations.fetch_add(1, std::memory_order_relaxed);
			}
			ParkStorage(std::move(slot));
			replacement.targetIdentity = bindings.targetIdentity;
			replacement.width = bindings.width;
			replacement.height = bindings.height;
			replacement.sourceDepthResource = sourceDepthResource;
			replacement.resolvedGeneration = replacement.resolvedDepthSnapshotEpoch = 0;
			replacement.resolvedConventionalProjection = {};
			replacement.resolvedObliqueProjection = {};
			replacement.lastUse = ++g_device.clock;
			slot = std::move(replacement);
			TrimStorage();
			return true;
		}

		[[nodiscard]] TargetSlot* SelectTargetSlot(
			ID3D11Device* device,
			ID3D11Resource* sourceDepthResource,
			const Bindings& bindings) noexcept
		{
			for (auto& slot : g_device.slots) {
				if (slot.targetIdentity == bindings.targetIdentity &&
					slot.width == bindings.width && slot.height == bindings.height &&
					slot.sourceDepthResource.Get() == sourceDepthResource &&
					slot.texture && slot.srv && slot.uav) {
					slot.lastUse = ++g_device.clock;
					return &slot;
				}
			}
			TargetSlot* empty = nullptr;
			for (auto& slot : g_device.slots) {
				if (slot.targetIdentity == 0) {
					empty = &slot;
					break;
				}
			}
			if (!empty) {
				// Keep the two most recently consumed results. Projection generations
				// belong to independent producers and cannot order slot usage.
				empty = &g_device.slots[0];
				if (g_device.slots[1].lastUse < g_device.slots[0].lastUse) {
					empty = &g_device.slots[1];
				}
			}
			return CreateTargetSlot(
				*empty, device, sourceDepthResource, bindings) ? empty : nullptr;
		}

		struct DispatchState
		{
			const Bindings* bindings{ nullptr };
			TargetSlot* slot{ nullptr };
			const Constants* constants{ nullptr };
			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
			ID3D11DepthStencilView* depthStencil{ nullptr };
			ID3D11ComputeShader* computeShader{ nullptr };
			std::array<ID3D11ClassInstance*, kClassInstanceCapacity>
				classInstances{};
			UINT classInstanceCount{ kClassInstanceCapacity };
			ID3D11Buffer* constantBuffer{ nullptr };
			ID3D11ShaderResourceView* shaderResource{ nullptr };
			ID3D11UnorderedAccessView* unorderedAccess{ nullptr };
			bool snapshotComplete{ false };
			bool mutationOwned{ false };
			bool restored{ false };
			const char* phase{ "snapshot" };
			const char* firstFailure{ nullptr };
			std::uint32_t exceptionCode{ 0 };
		};

		template <class T>
		[[nodiscard]] bool ReleaseOwned(T*& value) noexcept
		{
			T* const owned = value;
			value = nullptr;
			if (!owned)
				return true;
#if defined(_MSC_VER)
			__try {
				owned->Release();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#else
			owned->Release();
			return true;
#endif
		}

		[[nodiscard]] bool ReleaseSnapshot(DispatchState* state) noexcept
		{
			if (!state)
				return false;
			bool released = true;
			for (auto& value : state->renderTargets)
				released = ReleaseOwned(value) && released;
			released = ReleaseOwned(state->depthStencil) && released;
			released = ReleaseOwned(state->computeShader) && released;
			for (UINT index = 0; index < state->classInstanceCount; ++index)
				released = ReleaseOwned(state->classInstances[index]) && released;
			released = ReleaseOwned(state->constantBuffer) && released;
			released = ReleaseOwned(state->shaderResource) && released;
			released = ReleaseOwned(state->unorderedAccess) && released;
			return released;
		}

		[[nodiscard]] bool VerifyRestored(DispatchState* state) noexcept
		{
			if (!state || !state->bindings || !state->bindings->context)
				return false;
			auto* context = state->bindings->context;
			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
			ID3D11DepthStencilView* depthStencil = nullptr;
			ID3D11ComputeShader* shader = nullptr;
			std::array<ID3D11ClassInstance*, kClassInstanceCapacity> classes{};
			UINT classCount = kClassInstanceCapacity;
			ID3D11Buffer* constantBuffer = nullptr;
			ID3D11ShaderResourceView* shaderResource = nullptr;
			ID3D11UnorderedAccessView* unorderedAccess = nullptr;
			context->OMGetRenderTargets(
				static_cast<UINT>(renderTargets.size()), renderTargets.data(),
				&depthStencil);
			const bool shaderQueryValid =
				GetComputeShader(context, &shader, classes.data(), &classCount);
			context->CSGetConstantBuffers(0, 1, &constantBuffer);
			context->CSGetShaderResources(0, 1, &shaderResource);
			context->CSGetUnorderedAccessViews(0, 1, &unorderedAccess);
			bool same = shaderQueryValid && depthStencil == state->depthStencil &&
				shader == state->computeShader && classCount == state->classInstanceCount &&
				constantBuffer == state->constantBuffer &&
				shaderResource == state->shaderResource &&
				unorderedAccess == state->unorderedAccess;
			for (std::size_t index = 0; index < renderTargets.size(); ++index)
				same = same && renderTargets[index] == state->renderTargets[index];
			for (UINT index = 0; index < classCount && index < classes.size(); ++index)
				same = same && classes[index] == state->classInstances[index];
			for (auto& value : renderTargets)
				same = ReleaseOwned(value) && same;
			same = ReleaseOwned(depthStencil) && same;
			same = ReleaseOwned(shader) && same;
			for (UINT index = 0; index < classCount; ++index)
				same = ReleaseOwned(classes[index]) && same;
			same = ReleaseOwned(constantBuffer) && same;
			same = ReleaseOwned(shaderResource) && same;
			same = ReleaseOwned(unorderedAccess) && same;
			return same;
		}

		[[nodiscard]] bool SnapshotState(DispatchState* state) noexcept
		{
			if (!state || !state->bindings || !state->bindings->context)
				return false;
			auto* context = state->bindings->context;
			if (D3D11OutputMergerState::QueryUAVState(context) !=
				D3D11OutputMergerState::UAVState::kNone) {
				return false;
			}
			context->OMGetRenderTargets(
				static_cast<UINT>(state->renderTargets.size()),
				state->renderTargets.data(), &state->depthStencil);
			if (!GetComputeShader(context,
				&state->computeShader, state->classInstances.data(),
				&state->classInstanceCount)) {
				return false;
			}
			context->CSGetConstantBuffers(0, 1, &state->constantBuffer);
			context->CSGetShaderResources(0, 1, &state->shaderResource);
			context->CSGetUnorderedAccessViews(0, 1, &state->unorderedAccess);
			state->snapshotComplete =
				state->renderTargets[0] == state->bindings->colorRTV &&
				state->depthStencil == state->bindings->writableDepthDSV;
			return state->snapshotComplete;
		}

		[[nodiscard]] bool VerifyDispatchBindings(DispatchState* state) noexcept
		{
			if (!state || !state->bindings || !state->bindings->context ||
				!state->slot) {
				return false;
			}
			auto* context = state->bindings->context;
			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
			ID3D11DepthStencilView* depthStencil = nullptr;
			ID3D11ComputeShader* shader = nullptr;
			std::array<ID3D11ClassInstance*, kClassInstanceCapacity> classes{};
			UINT classCount = kClassInstanceCapacity;
			ID3D11Buffer* constantBuffer = nullptr;
			ID3D11ShaderResourceView* shaderResource = nullptr;
			ID3D11UnorderedAccessView* unorderedAccess = nullptr;
			context->OMGetRenderTargets(
				static_cast<UINT>(renderTargets.size()), renderTargets.data(),
				&depthStencil);
			const bool shaderQueryValid =
				GetComputeShader(context, &shader, classes.data(), &classCount);
			context->CSGetConstantBuffers(0, 1, &constantBuffer);
			context->CSGetShaderResources(0, 1, &shaderResource);
			context->CSGetUnorderedAccessViews(0, 1, &unorderedAccess);
			bool same = shaderQueryValid && depthStencil == state->bindings->readOnlyDepthDSV &&
				shader == g_device.shader.Get() && classCount == 0 &&
				constantBuffer == g_device.constants.Get() &&
				shaderResource == state->bindings->obliqueDepthSRV &&
				unorderedAccess == state->slot->uav.Get();
			for (std::size_t index = 0; index < renderTargets.size(); ++index)
				same = same && renderTargets[index] == state->renderTargets[index];
			for (auto& value : renderTargets)
				same = ReleaseOwned(value) && same;
			same = ReleaseOwned(depthStencil) && same;
			same = ReleaseOwned(shader) && same;
			for (UINT index = 0; index < classCount; ++index)
				same = ReleaseOwned(classes[index]) && same;
			same = ReleaseOwned(constantBuffer) && same;
			same = ReleaseOwned(shaderResource) && same;
			same = ReleaseOwned(unorderedAccess) && same;
			return same;
		}

		[[nodiscard]] bool RestoreState(DispatchState* state) noexcept
		{
			if (!state)
				return false;
			state->phase = "restore-bindings";
			bool restored = !state->mutationOwned;
			if (state->mutationOwned && state->bindings &&
				state->bindings->context) {
				auto* context = state->bindings->context;
				ID3D11ShaderResourceView* nullSRV = nullptr;
				ID3D11UnorderedAccessView* nullUAV = nullptr;
				constexpr UINT preserveCounter = D3D11_KEEP_UNORDERED_ACCESS_VIEWS;
				context->CSSetShaderResources(0, 1, &nullSRV);
				context->CSSetUnorderedAccessViews(0, 1, &nullUAV, &preserveCounter);
				context->OMSetRenderTargets(
					static_cast<UINT>(state->renderTargets.size()),
					state->renderTargets.data(), state->depthStencil);
				context->CSSetShader(
					state->computeShader, state->classInstances.data(),
					state->classInstanceCount);
				context->CSSetConstantBuffers(0, 1, &state->constantBuffer);
				context->CSSetShaderResources(0, 1, &state->shaderResource);
				context->CSSetUnorderedAccessViews(
					0, 1, &state->unorderedAccess, &preserveCounter);
				restored = VerifyRestored(state);
			}
			state->restored = restored;
			if (!restored && !state->firstFailure)
				state->firstFailure = state->phase;
			state->phase = "release-snapshot";
			const bool released = ReleaseSnapshot(state);
			if (!released && !state->firstFailure)
				state->firstFailure = state->phase;
			if (restored && released)
				g_counters.restores.fetch_add(1, std::memory_order_relaxed);
			return restored && released;
		}

		[[nodiscard]] bool DispatchRaw(DispatchState* state) noexcept
		{
			if (!state || !state->bindings || !state->slot || !state->constants ||
				!g_device.shader || !g_device.constants ||
				!SnapshotState(state)) {
				return false;
			}
			auto* context = state->bindings->context;
			state->phase = "bind-compute";
			context->UpdateSubresource(
				g_device.constants.Get(), 0, nullptr, state->constants, 0, 0);
			state->mutationOwned = true;
			context->OMSetRenderTargets(
				static_cast<UINT>(state->renderTargets.size()),
				state->renderTargets.data(), state->bindings->readOnlyDepthDSV);
			ID3D11Buffer* constantBuffer = g_device.constants.Get();
			ID3D11ShaderResourceView* source =
				state->bindings->obliqueDepthSRV;
			ID3D11UnorderedAccessView* output = state->slot->uav.Get();
			constexpr UINT preserveCounter = D3D11_KEEP_UNORDERED_ACCESS_VIEWS;
			context->CSSetShader(g_device.shader.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &constantBuffer);
			context->CSSetShaderResources(0, 1, &source);
			context->CSSetUnorderedAccessViews(0, 1, &output, &preserveCounter);
			state->phase = "verify-compute-bindings";
			if (!VerifyDispatchBindings(state))
				return false;
			state->phase = "dispatch";
			context->Dispatch(
				(state->bindings->width + 7u) / 8u,
				(state->bindings->height + 7u) / 8u, 1);
			return true;
		}

		[[nodiscard]] bool DispatchWithFinally(DispatchState* state) noexcept
		{
			bool dispatched = false;
			bool restored = false;
#if defined(_MSC_VER)
			__try {
				__try {
					dispatched = DispatchRaw(state);
				} __finally {
					if (!dispatched)
						state->firstFailure = state->phase;
					restored = RestoreState(state);
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				state->exceptionCode = GetExceptionCode();
				if (!state->firstFailure)
					state->firstFailure = state->phase;
				dispatched = false;
				restored = false;
			}
#else
			dispatched = DispatchRaw(state);
			restored = RestoreState(state);
#endif
			return dispatched && restored;
		}
	}

	bool EnsureDevice(ID3D11Device* device) noexcept
	{
		if (!device || g_faulted.load(std::memory_order_acquire))
			return false;
		if (g_device.device.Get() == device && g_device.shader &&
			g_device.constants) {
			return true;
		}
		// Preflight receives Skyrim's forwarding device, whereas CS context
		// GetDevice can return the real device. Preserve the original creator
		// (and its wrapped compute shader) instead of rebuilding on every pass.
		if (g_device.shader && g_device.constants &&
			g_device.resourceDevice.Get() == device &&
			EngineDeviceIdentity::ForwardingDeviceAccepted()) {
			return true;
		}

		DeviceState replacement{};
		replacement.device = device;
		if (FAILED(device->CreateComputeShader(
				g_handMirrorPhysicalDepthComputeShader,
				sizeof(g_handMirrorPhysicalDepthComputeShader), nullptr,
				replacement.shader.GetAddressOf()))) {
			return false;
		}
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = sizeof(Constants);
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(device->CreateBuffer(
				&description, nullptr, replacement.constants.GetAddressOf()))) {
			return false;
		}
		replacement.constants->GetDevice(replacement.resourceDevice.GetAddressOf());
		if (!replacement.resourceDevice)
			return false;
		g_device = std::move(replacement);
		g_counters.storageBytes.store(0, std::memory_order_relaxed);
		g_counters.deviceInitializations.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	ResolveResult Resolve(const Bindings& bindings) noexcept
	{
		g_counters.resolveAttempts.fetch_add(1, std::memory_order_relaxed);
		const auto reject = []() noexcept {
			g_counters.rejects.fetch_add(1, std::memory_order_relaxed);
			return ResolveResult{};
		};
		if (g_faulted.load(std::memory_order_acquire) || !bindings.context ||
			!bindings.colorRTV || !bindings.writableDepthDSV ||
			!bindings.readOnlyDepthDSV || !bindings.obliqueDepthSRV ||
			bindings.targetIdentity == 0 || bindings.depthSnapshotEpoch == 0 ||
			bindings.width == 0 ||
			bindings.height == 0 || !ValidProjectionPair(bindings.projection)) {
			return reject();
		}

		Microsoft::WRL::ComPtr<ID3D11Device> device{};
		bindings.context->GetDevice(device.GetAddressOf());
		if (!device || !EnsureDevice(device.Get()))
			return reject();
		Microsoft::WRL::ComPtr<ID3D11Resource> sourceDepthResource{};
		bindings.obliqueDepthSRV->GetResource(sourceDepthResource.GetAddressOf());
		if (!sourceDepthResource ||
			!ValidateDepthBindings(bindings, sourceDepthResource.Get()))
			return reject();
		auto* slot = SelectTargetSlot(
			g_device.device.Get(), sourceDepthResource.Get(), bindings);
		if (!slot)
			return reject();
		if (slot->resolvedGeneration == bindings.projection.generation &&
			slot->resolvedDepthSnapshotEpoch == bindings.depthSnapshotEpoch &&
			SameMatrixBits(
				slot->resolvedConventionalProjection,
				bindings.projection.conventional) &&
			SameMatrixBits(
				slot->resolvedObliqueProjection,
				bindings.projection.oblique)) {
			g_counters.cacheHits.fetch_add(1, std::memory_order_relaxed);
			return { slot->srv.Get(), ResolveStatus::kCached };
		}

		using namespace DirectX;
		const XMMATRIX oblique = XMLoadFloat4x4(&bindings.projection.oblique);
		XMVECTOR determinant{};
		const XMMATRIX inverseOblique = XMMatrixInverse(&determinant, oblique);
		const float determinantValue = XMVectorGetX(determinant);
		if (!std::isfinite(determinantValue) ||
			std::abs(determinantValue) <= 1.0e-7F) {
			return reject();
		}
		Constants constants{};
		XMStoreFloat4x4(&constants.inverseObliqueProjection, inverseOblique);
		constants.conventionalProjection = bindings.projection.conventional;
		constants.width = bindings.width;
		constants.height = bindings.height;
		if (!FiniteMatrix(constants.inverseObliqueProjection))
			return reject();

		DispatchState dispatch{
			.bindings = &bindings,
			.slot = slot,
			.constants = &constants
		};
		if (!DispatchWithFinally(&dispatch)) {
			EnterFault();
#if !defined(MIRRORS_OF_SKYRIM_PHYSICAL_CLIP_DEPTH_TEST)
			logger::error(
				"[RR][HandMirror][physical-depth] transaction failed: first={} last={} nativeCode=0x{:08X} snapshot={} mutationOwned={} restored={} classes={}",
				dispatch.firstFailure ? dispatch.firstFailure : "unknown",
				dispatch.phase, dispatch.exceptionCode, dispatch.snapshotComplete,
				dispatch.mutationOwned, dispatch.restored, dispatch.classInstanceCount);
#endif
			return { nullptr, ResolveStatus::kFaulted };
		}
		slot->resolvedGeneration = bindings.projection.generation;
		slot->resolvedDepthSnapshotEpoch = bindings.depthSnapshotEpoch;
		slot->resolvedConventionalProjection = bindings.projection.conventional;
		slot->resolvedObliqueProjection = bindings.projection.oblique;
		g_counters.dispatches.fetch_add(1, std::memory_order_relaxed);
		return { slot->srv.Get(), ResolveStatus::kApplied };
	}

	Diagnostics GetDiagnostics() noexcept
	{
		return {
			.deviceInitializations = g_counters.deviceInitializations.load(
				std::memory_order_relaxed),
			.resourceCreations = g_counters.resourceCreations.load(
				std::memory_order_relaxed),
			.storageReuses = g_counters.storageReuses.load(std::memory_order_relaxed),
			.storageEvictions = g_counters.storageEvictions.load(std::memory_order_relaxed),
			.storageBytes = g_counters.storageBytes.load(std::memory_order_relaxed),
			.resolveAttempts = g_counters.resolveAttempts.load(
				std::memory_order_relaxed),
			.dispatches = g_counters.dispatches.load(std::memory_order_relaxed),
			.cacheHits = g_counters.cacheHits.load(std::memory_order_relaxed),
			.restores = g_counters.restores.load(std::memory_order_relaxed),
			.rejects = g_counters.rejects.load(std::memory_order_relaxed),
			.faults = g_counters.faults.load(std::memory_order_relaxed),
			.faulted = g_faulted.load(std::memory_order_acquire)
		};
	}

	bool Faulted() noexcept
	{
		return g_faulted.load(std::memory_order_acquire);
	}
}
