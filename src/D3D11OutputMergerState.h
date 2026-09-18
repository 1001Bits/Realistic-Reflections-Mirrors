#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) && defined(_MSC_VER)
#	include <Windows.h>
#endif

#include <d3d11.h>

namespace D3D11OutputMergerState
{
	/** Complete result of querying the graphics-output UAV bind space. */
	enum class UAVState : std::uint8_t
	{
		kNone,
		kBound,
		kFault
	};

#if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) && defined(_MSC_VER)
	/** Test-only native-fault seams used by the focused WARP executable. */
	namespace Testing
	{
		enum class FaultPoint : std::uint8_t
		{
			kNone,
			kAfterDeviceQuery,
			kAfterFeatureLevelQuery,
			kAfterDeviceRelease,
			kAfterRenderTargetQuery,
			kAfterRenderTargetRelease,
			kAfterUAVQuery,
			kAfterUAVRelease
		};

		struct Counters
		{
			std::uint32_t deviceReleases{ 0 };
			std::uint32_t renderTargetReleases{ 0 };
			std::uint32_t uavReleases{ 0 };
		};

		inline thread_local FaultPoint selectedFault{ FaultPoint::kNone };
		inline thread_local Counters counters{};

		inline void Reset() noexcept
		{
			selectedFault = FaultPoint::kNone;
			counters = {};
		}

		inline void InjectOnce(const FaultPoint point) noexcept
		{
			counters = {};
			selectedFault = point;
		}

		[[nodiscard]] inline Counters ReadCounters() noexcept
		{
			return counters;
		}

		inline void MaybeRaise(const FaultPoint point) noexcept
		{
			if (selectedFault != point)
				return;
			selectedFault = FaultPoint::kNone;
			::RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
		}
	}
#endif

	namespace Detail
	{
		enum class ReleaseKind : std::uint8_t
		{
			kDevice,
			kRenderTarget,
			kUAV
		};

		inline bool GetDeviceGuarded(
			ID3D11DeviceContext* context,
			ID3D11Device** device) noexcept
		{
#if defined(_MSC_VER)
			__try {
				context->GetDevice(device);
#	if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE)
				Testing::MaybeRaise(Testing::FaultPoint::kAfterDeviceQuery);
#	endif
				return true;
			} __except (1) {
				return false;
			}
#else
			context->GetDevice(device);
			return true;
#endif
		}

		inline bool GetFeatureLevelGuarded(
			ID3D11Device* device,
			D3D_FEATURE_LEVEL& featureLevel) noexcept
		{
#if defined(_MSC_VER)
			__try {
				featureLevel = device->GetFeatureLevel();
#	if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE)
				Testing::MaybeRaise(Testing::FaultPoint::kAfterFeatureLevelQuery);
#	endif
				return true;
			} __except (1) {
				return false;
			}
#else
			featureLevel = device->GetFeatureLevel();
			return true;
#endif
		}

		inline bool GetRenderTargetsGuarded(
			ID3D11DeviceContext* context,
			ID3D11RenderTargetView** renderTargets) noexcept
		{
#if defined(_MSC_VER)
			__try {
				context->OMGetRenderTargets(
					D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
					renderTargets,
					nullptr);
#	if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE)
				Testing::MaybeRaise(Testing::FaultPoint::kAfterRenderTargetQuery);
#	endif
				return true;
			} __except (1) {
				return false;
			}
#else
			context->OMGetRenderTargets(
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, renderTargets, nullptr);
			return true;
#endif
		}

		inline bool GetUAVsGuarded(
			ID3D11DeviceContext* context,
			const UINT firstSlot,
			const UINT viewCount,
			ID3D11UnorderedAccessView** views) noexcept
		{
#if defined(_MSC_VER)
			__try {
				context->OMGetRenderTargetsAndUnorderedAccessViews(
					0, nullptr, nullptr, firstSlot, viewCount, views);
#	if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE)
				Testing::MaybeRaise(Testing::FaultPoint::kAfterUAVQuery);
#	endif
				return true;
			} __except (1) {
				return false;
			}
#else
			context->OMGetRenderTargetsAndUnorderedAccessViews(
				0, nullptr, nullptr, firstSlot, viewCount, views);
			return true;
#endif
		}

		template <class Interface>
		inline bool ReleaseGuarded(
			Interface*& value,
			const ReleaseKind kind) noexcept
		{
			Interface* const owned = value;
			value = nullptr;
			if (!owned)
				return true;
			(void)kind;

#if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE) && defined(_MSC_VER)
			switch (kind) {
			case ReleaseKind::kDevice:
				++Testing::counters.deviceReleases;
				break;
			case ReleaseKind::kRenderTarget:
				++Testing::counters.renderTargetReleases;
				break;
			case ReleaseKind::kUAV:
				++Testing::counters.uavReleases;
				break;
			}
#endif

#if defined(_MSC_VER)
			__try {
				owned->Release();
#	if defined(RR_SECOND_VIEW_RENDER_TARGET_STANDALONE)
				switch (kind) {
				case ReleaseKind::kDevice:
					Testing::MaybeRaise(Testing::FaultPoint::kAfterDeviceRelease);
					break;
				case ReleaseKind::kRenderTarget:
					Testing::MaybeRaise(Testing::FaultPoint::kAfterRenderTargetRelease);
					break;
				case ReleaseKind::kUAV:
					Testing::MaybeRaise(Testing::FaultPoint::kAfterUAVRelease);
					break;
				}
#	endif
				return true;
			} __except (1) {
				return false;
			}
#else
			(void)kind;
			owned->Release();
			return true;
#endif
		}
	}

	/**
	 * Query every legal pixel-shader UAV slot without allowing a native fault or
	 * a partially AddRef'd query result to escape. A cleanup fault wins over a
	 * successful observation because exact query ownership was not proven.
	 */
	[[nodiscard]] inline UAVState QueryUAVState(ID3D11DeviceContext* context) noexcept
	{
		if (!context)
			return UAVState::kFault;

		ID3D11Device* device = nullptr;
		D3D_FEATURE_LEVEL featureLevel{};
		const bool deviceQuerySucceeded = Detail::GetDeviceGuarded(context, &device);
		bool faulted = !deviceQuerySucceeded || !device;
		if (deviceQuerySucceeded && device &&
			!Detail::GetFeatureLevelGuarded(device, featureLevel))
			faulted = true;
		if (!Detail::ReleaseGuarded(device, Detail::ReleaseKind::kDevice))
			faulted = true;
		if (faulted)
			return UAVState::kFault;

		const UINT slotCount = featureLevel >= D3D_FEATURE_LEVEL_11_1 ?
		                           D3D11_1_UAV_SLOT_COUNT :
		                           D3D11_PS_CS_UAV_REGISTER_COUNT;

		std::array<ID3D11RenderTargetView*,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
		const bool renderTargetQuerySucceeded =
			Detail::GetRenderTargetsGuarded(context, renderTargets.data());
		UINT firstUAVSlot = 0;
		if (renderTargetQuerySucceeded) {
			for (std::size_t index = 0; index < renderTargets.size(); ++index) {
				if (renderTargets[index])
					firstUAVSlot = static_cast<UINT>(index + 1);
			}
		}
		bool renderTargetCleanupSucceeded = true;
		for (auto& renderTarget : renderTargets) {
			if (!Detail::ReleaseGuarded(
					renderTarget, Detail::ReleaseKind::kRenderTarget))
				renderTargetCleanupSucceeded = false;
		}
		if (!renderTargetQuerySucceeded || !renderTargetCleanupSucceeded)
			return UAVState::kFault;
		if (firstUAVSlot >= slotCount)
			return UAVState::kNone;

		std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> views{};
		const UINT viewCount = slotCount - firstUAVSlot;
		const bool uavQuerySucceeded =
			Detail::GetUAVsGuarded(context, firstUAVSlot, viewCount, views.data());
		bool anyBound = false;
		if (uavQuerySucceeded) {
			for (UINT index = 0; index < viewCount; ++index) {
				if (views[index])
					anyBound = true;
			}
		}
		bool uavCleanupSucceeded = true;
		for (auto& view : views) {
			if (!Detail::ReleaseGuarded(view, Detail::ReleaseKind::kUAV))
				uavCleanupSucceeded = false;
		}
		if (!uavQuerySucceeded || !uavCleanupSucceeded)
			return UAVState::kFault;
		return anyBound ? UAVState::kBound : UAVState::kNone;
	}

	/** Backwards-compatible fail-closed predicate for existing non-hand callers. */
	[[nodiscard]] inline bool AnyUAVBound(ID3D11DeviceContext* context) noexcept
	{
		return QueryUAVState(context) != UAVState::kNone;
	}
}
