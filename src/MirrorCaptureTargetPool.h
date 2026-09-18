#pragma once
#include "MirrorSecondViewRenderTarget.h"
#include "MirrorFramePublication.h"
#include "MirrorFleetPolicy.h"
#include "EngineDeviceIdentity.h"
#include "MirrorCaptureSizing.h"
#include "MirrorCapturePoolDiagnostics.h"
#include <chrono>
#include <memory>
#include <utility>

namespace MirrorFleetRuntime
{
	/** Receiver colours remain in stable caller-owned slots; only unowned
	 * scratch allocations and non-published depth enter this pool. */
	class CaptureTargetPool
	{
	public:
		using Target = SecondView::RenderTarget;
		// Bound the rectangular shared-depth buckets and recently used unowned
		// colours together. Completed images
		// live in caller-owned slots and can never be evicted by this budget.
		static constexpr std::uint64_t kMaximumCachedBytes = 256ULL * 1024 * 1024;
		bool EnsureFramebuffer(Target*& target, ID3D11Device* device,
			MirrorCaptureSizing::Extent extent, Target::DebugChannel channel,
			std::uint64_t& generation) noexcept
		{
			if (!device || !extent.Valid() || (device_ && device_.Get() != device)) return false;
			if (target && (target->Bound() || target->LastRestoreStatus() == Target::RestoreStatus::kFault)) return false;
			if (target && target->Ready()) {
				Microsoft::WRL::ComPtr<ID3D11Device> owner;
				target->ColorResourceIdentity()->GetDevice(&owner);
				if (!EngineDeviceIdentityPolicy::OwnedByEngineDevice(
					EngineDeviceIdentity::Current(reinterpret_cast<std::uintptr_t>(device)),
					reinterpret_cast<std::uintptr_t>(owner.Get())) ||
					!MirrorFramePublication::CaptureResourcesDisjoint(target->ColorResourceIdentity(), target->DepthResourceIdentity())) return false;
				if (target->Width() == extent.width && target->Height() == extent.height &&
					target->RenderMip() == 0 && !target->HasReducedViews() &&
					target->ColorFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT) return true;
			}
			if (!device_) device_ = device;
			delete target;
			target = new (std::nothrow) Target();
			generation = 0;
			if (!target) return false;
			if (framebufferSize_ != extent) {
				framebufferDepth_ = {};
				framebufferSize_ = extent;
			}
			const bool reused = framebufferDepth_.texture != nullptr;
			if (!target->Create(device, extent.width, extent.height, DXGI_FORMAT_R16G16B16A16_FLOAT,
				channel, Target::kNoDebugInstance, reused ? &framebufferDepth_ : nullptr)) return false;
			if (!reused) { framebufferDepth_ = target->RetainDepthStorage(); ++depthAllocations_; }
			++colourAllocations_;
			return true;
		}
		bool Ensure(Target*& target, ID3D11Device* device, std::uint32_t size,
			Target::DebugChannel channel, std::uint64_t& generation,
			bool independentDepth = false) noexcept
		{
			return Ensure(target, device, MirrorCaptureSizing::Extent{ size, size }, channel, generation, independentDepth);
		}
		// Placed captures follow the pane's shape (core run 9): each axis is one
		// of the fleet resolutions, so every width x height has its own bucket.
		bool Ensure(Target*& target, ID3D11Device* device, MirrorCaptureSizing::Extent extent,
			Target::DebugChannel channel, std::uint64_t& generation,
			bool independentDepth = false) noexcept
		{
			const auto width = extent.width, height = extent.height;
			if (!device || !ValidSize(width) || !ValidSize(height)) return false;
			// Device recovery requires rebuilding publications and the whole pool together.
			// Never hand a live caller cached storage from another D3D device.
			if (device_ && (device_.Get() != device || independentDepth_ != independentDepth)) return false;
			if (target && (target->Bound() || target->LastRestoreStatus() == Target::RestoreStatus::kFault)) return false;
			if (target && target->Ready()) {
				if (!ValidSize(target->Width()) || !ValidSize(target->Height()) ||
					target->RenderMip() != 0 || target->HasReducedViews() ||
					target->ColorFormat() != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
				Microsoft::WRL::ComPtr<ID3D11Device> owner;
				target->ColorResourceIdentity()->GetDevice(&owner);
				if (!EngineDeviceIdentityPolicy::OwnedByEngineDevice(
					EngineDeviceIdentity::Current(reinterpret_cast<std::uintptr_t>(device)),
					reinterpret_cast<std::uintptr_t>(owner.Get()))) return false;
			}
			if (!device_) { device_ = device; independentDepth_ = independentDepth; }
			const auto index = Index(width, height);
			if (target && target->Ready()) {
				if (!MirrorFramePublication::CaptureResourcesDisjoint(target->ColorResourceIdentity(), target->DepthResourceIdentity())) return false;
				if (target->Width() == width && target->Height() == height) return true;
				Park(Index(target->Width(), target->Height()), target);
			} else delete target;
			target = Take(index);
			depthAge_[index] = ++cacheClock_;
			Trim();
			if (!target) target = new (std::nothrow) Target();
			generation = 0;
			if (!target) return false;
			if (!target->Ready()) {
				auto& depth = depths_[index];
				const bool reused = !independentDepth && depth.texture != nullptr;
				const auto allocationStart = std::chrono::steady_clock::now();
				const bool created = target->Create(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT,
					channel, Target::kNoDebugInstance, reused ? &depth : nullptr);
				const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - allocationStart).count();
				auto& diagnostic = Shape(index);
				const auto microseconds = static_cast<std::uint64_t>((std::max)(elapsed, decltype(elapsed){0}));
				diagnostic.allocationMicroseconds += microseconds;
				diagnostic.maximumAllocationMicroseconds = (std::max)(diagnostic.maximumAllocationMicroseconds, microseconds);
				if (!created) { ++diagnostic.failures; return false; }
				++diagnostic.allocations;
				if (!reused) {
					if (!independentDepth) depth = target->RetainDepthStorage();
					++depthAllocations_;
				}
				++colourAllocations_;
			}
			Trim();
			return true;
		}
		std::uint64_t AuxiliaryBytes() const noexcept
		{
			std::uint64_t bytes = 0;
			if (framebufferDepth_.texture)
				bytes += std::uint64_t(framebufferSize_.width) * framebufferSize_.height * 4;
			for (std::size_t i = 0; i < depths_.size(); ++i) {
				if (depths_[i].texture)
					bytes += static_cast<std::uint64_t>(MirrorFleetPolicy::kResolutions[i / kSizes]) *
						MirrorFleetPolicy::kResolutions[i % kSizes] * 4;
				for (const auto& spare : spares_[i])
					if (spare && spare->Ready()) bytes += TargetBytes(*spare, independentDepth_);
			}
			return bytes;
		}
		std::uint64_t ColourAllocations() const noexcept { return colourAllocations_; }
		std::uint64_t DepthAllocations() const noexcept { return depthAllocations_; }
		std::uint64_t CacheEvictions() const noexcept { return cacheEvictions_; }
		const CapturePoolDiagnostics& Diagnostics() const noexcept { return diagnostics_; }
		static std::uint64_t TargetBytes(const Target& target, bool independentDepth) noexcept
		{
			return MirrorFleetPolicy::ColorBytes(target.Width(),target.Height()) +
				(independentDepth ? std::uint64_t(target.Width())*target.Height()*4 : 0);
		}
	private:
		// On-screen sizing (2026-09-17) moves a mirror between sizes, and its
		// scratch and published targets trade places every capture, so each size
		// keeps two spares: returning to a size reuses both instead of allocating.
		void Park(std::size_t index, Target* target) noexcept
		{
			auto& slots = spares_[index];
			auto& ages = spareAge_[index];
			std::size_t slot = ages[0] <= ages[1] ? 0 : 1;
			if (!slots[0]) slot = 0;
			else if (!slots[1]) slot = 1;
			else { ++cacheEvictions_; ++Shape(index).evictions; }
			slots[slot].reset(target);
			ages[slot] = ++cacheClock_;
		}
		Target* Take(std::size_t index) noexcept
		{
			auto& slots = spares_[index];
			auto& ages = spareAge_[index];
			std::size_t slot = !slots[1] || (slots[0] && ages[0] >= ages[1]) ? 0 : 1;
			ages[slot] = 0;
			if (slots[slot]) ++Shape(index).reuseHits;
			return slots[slot].release();
		}
		void Trim() noexcept
		{
			while (AuxiliaryBytes() > kMaximumCachedBytes) {
				std::size_t oldest = spares_.size(), oldestSlot = 0;
				for (std::size_t i = 0; i < spares_.size(); ++i)
					for (std::size_t slot = 0; slot < kSparesPerSize; ++slot)
						if (spares_[i][slot] && (oldest == spares_.size() ||
							spareAge_[i][slot] < spareAge_[oldest][oldestSlot])) {
							oldest = i;
							oldestSlot = slot;
						}
				if (oldest == spares_.size()) break;
				spares_[oldest][oldestSlot].reset();
				++cacheEvictions_;
				++Shape(oldest).evictions;
			}
			// Shared depth, one per width x height seen. Live targets keep their own
			// references, so dropping the pool's copy only means the next new target
			// of that shape allocates its own. The bucket in use now is kept.
			while (AuxiliaryBytes() > kMaximumCachedBytes) {
				std::size_t oldest = depths_.size();
				for (std::size_t i = 0; i < depths_.size(); ++i)
					if (depths_[i].texture && depthAge_[i] != cacheClock_ &&
						(oldest == depths_.size() || depthAge_[i] < depthAge_[oldest]))
						oldest = i;
				if (oldest == depths_.size()) break;
				depths_[oldest] = {};
				++cacheEvictions_;
			}
		}
		static constexpr std::size_t kSizes = MirrorFleetPolicy::kResolutions.size();
		static_assert(kSizes * kSizes == CapturePoolDiagnostics{}.size());
		CapturePoolShapeDiagnostics& Shape(std::size_t index) noexcept
		{
			auto& diagnostic = diagnostics_[index];
			diagnostic.width = MirrorFleetPolicy::kResolutions[index / kSizes];
			diagnostic.height = MirrorFleetPolicy::kResolutions[index % kSizes];
			return diagnostic;
		}
		static bool ValidSize(std::uint32_t size) noexcept
		{
			return size <= 4096 && static_cast<std::uint32_t>(MirrorFleetPolicy::Resolution(static_cast<int>(size))) == size;
		}
		static std::size_t SizeIndex(std::uint32_t size) noexcept
		{
			for (std::size_t i = 0; i < kSizes; ++i)
				if (static_cast<std::uint32_t>(MirrorFleetPolicy::kResolutions[i]) == size) return i;
			return kSizes - 1;
		}
		static std::size_t Index(std::uint32_t width, std::uint32_t height) noexcept
		{
			return SizeIndex(width) * kSizes + SizeIndex(height);
		}
		std::array<Target::SharedDepth, kSizes * kSizes> depths_{};
		std::array<std::uint64_t, kSizes * kSizes> depthAge_{};
		Target::SharedDepth framebufferDepth_{};
		MirrorCaptureSizing::Extent framebufferSize_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		static constexpr std::size_t kSparesPerSize = 2;
		std::array<std::array<std::unique_ptr<Target>, kSparesPerSize>, kSizes * kSizes> spares_{};
		std::array<std::array<std::uint64_t, kSparesPerSize>, kSizes * kSizes> spareAge_{};
		std::uint64_t colourAllocations_{}, depthAllocations_{};
		std::uint64_t cacheClock_{}, cacheEvictions_{};
		bool independentDepth_{};
		CapturePoolDiagnostics diagnostics_{};
	};
}
