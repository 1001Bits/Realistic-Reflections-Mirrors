#include "PCH.h"

#include "SecondViewDepthEqualOverride.h"

#include <cstddef>
#include <cstring>

namespace SecondViewDepthEqualOverride
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		// RendererShadowState depth/stencil fields.  Flat (SE 1.5.97 and every AE
		// runtime) and VR 1.4.15 differ by the eight bytes VR inserts ahead of the
		// depth block; CommonLib models the same shift in its VR_RUNTIME_DATA.
		struct DepthStateLayout
		{
			std::ptrdiff_t flags{ 0x00 };
			std::ptrdiff_t depthMode{ 0x88 };
			std::ptrdiff_t previousDepthMode{ 0x8C };
			std::ptrdiff_t stencilMode{ 0x90 };
			std::ptrdiff_t stencilReference{ 0x94 };
		};
		constexpr DepthStateLayout kFlatLayout{};
		constexpr DepthStateLayout kVRLayout{ 0x00, 0x90, 0x94, 0x98, 0x9C };

		[[nodiscard]] const DepthStateLayout& CurrentLayout() noexcept
		{
			static const DepthStateLayout& selected =
				REL::Module::IsVR() ? kVRLayout : kFlatLayout;
			return selected;
		}

		constexpr std::ptrdiff_t kFlagsOffset = 0x00;
		constexpr std::ptrdiff_t kDepthModeOffset = 0x88;
		constexpr std::ptrdiff_t kPreviousDepthModeOffset = 0x8C;
		constexpr std::ptrdiff_t kStencilModeOffset = 0x90;
		constexpr std::ptrdiff_t kStencilReferenceOffset = 0x94;
		constexpr std::uint32_t kDepthModeDirtyBit =
			static_cast<std::uint32_t>(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);
		constexpr std::uint32_t kTestWrite = static_cast<std::uint32_t>(
			RE::BSGraphics::DepthStencilDepthMode::kTestWrite);
		constexpr std::uint32_t kTestEqual = static_cast<std::uint32_t>(
			RE::BSGraphics::DepthStencilDepthMode::kTestEqual);

		static_assert(kDepthModeDirtyBit == 0x04);
		static_assert(kTestWrite == 3);
		static_assert(kTestEqual == 4);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA,
			stateUpdateFlags) == kFlagsOffset);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA,
			depthStencilDepthMode) == kDepthModeOffset);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA,
			depthStencilDepthModePrevious) == kPreviousDepthModeOffset);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA,
			depthStencilStencilMode) == kStencilModeOffset);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA,
			stencilRef) == kStencilReferenceOffset);

		template <class T>
		void Read(const std::byte* bytes, std::ptrdiff_t offset, T& output) noexcept
		{
			std::memcpy(&output, bytes + offset, sizeof(output));
		}

		template <class T>
		void Write(std::byte* bytes, std::ptrdiff_t offset, const T& value) noexcept
		{
			std::memcpy(bytes + offset, &value, sizeof(value));
		}

		[[nodiscard]] bool CurrentStateIs(
			ID3D11DeviceContext* context,
			ID3D11DepthStencilState* expected,
			std::uint32_t expectedStencilReference) noexcept
		{
			if (!context)
				return false;
			ComPtr<ID3D11DepthStencilState> current;
			std::uint32_t stencilReference = 0;
			context->OMGetDepthStencilState(current.GetAddressOf(), &stencilReference);
			return current.Get() == expected && stencilReference == expectedStencilReference;
		}
	}

	bool Scope::Begin(ID3D11DeviceContext* newContext, void* flatRendererShadow) noexcept
	{
		const auto& layout = CurrentLayout();
		if (active || restorePending || context || savedState || shadow || !newContext ||
			!flatRendererShadow)
			return false;

		ComPtr<ID3D11DepthStencilState> current;
		std::uint32_t stencilReference = 0;
		newContext->OMGetDepthStencilState(current.GetAddressOf(), &stencilReference);
		auto* bytes = reinterpret_cast<std::byte*>(flatRendererShadow);
		Read(bytes, layout.flags, savedFlags);
		Read(bytes, layout.depthMode, savedDepthMode);
		Read(bytes, layout.previousDepthMode, savedPreviousDepthMode);
		Read(bytes, layout.stencilMode, savedStencilMode);
		Read(bytes, layout.stencilReference, savedShadowStencilReference);

		context = newContext;
		savedState = current;
		shadow = flatRendererShadow;
		savedStencilReference = stencilReference;
		active = true;
		restorePending = true;
		failed = false;

		const std::uint32_t seededFlags = savedFlags | kDepthModeDirtyBit;
		Write(bytes, layout.depthMode, kTestWrite);
		Write(bytes, layout.flags, seededFlags);
		std::uint32_t verifiedMode = 0;
		std::uint32_t verifiedFlags = 0;
		Read(bytes, layout.depthMode, verifiedMode);
		Read(bytes, layout.flags, verifiedFlags);
		if (verifiedMode == kTestWrite && (verifiedFlags & kDepthModeDirtyBit) != 0)
			return true;

		failed = true;
		(void)Restore();
		return false;
	}

	bool Scope::ReseedForPrimary() noexcept
	{
		const auto& layout = CurrentLayout();
		if (!active || !restorePending || !shadow) {
			failed = true;
			return false;
		}

		auto* bytes = reinterpret_cast<std::byte*>(shadow);
		std::uint32_t flags = 0;
		Read(bytes, layout.flags, flags);
		flags |= kDepthModeDirtyBit;
		Write(bytes, layout.depthMode, kTestWrite);
		Write(bytes, layout.flags, flags);

		std::uint32_t verifiedMode = 0;
		std::uint32_t verifiedFlags = 0;
		Read(bytes, layout.depthMode, verifiedMode);
		Read(bytes, layout.flags, verifiedFlags);
		if (verifiedMode == kTestWrite &&
			(verifiedFlags & kDepthModeDirtyBit) != 0) {
			return true;
		}

		failed = true;
		return false;
	}

	PrepareResult Scope::PrepareSetDirtyStates() noexcept
	{
		const auto& layout = CurrentLayout();
		if (!active || !restorePending || !shadow) {
			failed = true;
			return PrepareResult::kFailed;
		}
		auto* bytes = reinterpret_cast<std::byte*>(shadow);
		std::uint32_t requestedMode = 0;
		Read(bytes, layout.depthMode, requestedMode);
		if (requestedMode != kTestEqual)
			return PrepareResult::kUnchanged;

		std::uint32_t flags = 0;
		Read(bytes, layout.flags, flags);
		flags |= kDepthModeDirtyBit;
		Write(bytes, layout.depthMode, kTestWrite);
		Write(bytes, layout.flags, flags);
		Read(bytes, layout.depthMode, requestedMode);
		Read(bytes, layout.flags, flags);
		if (requestedMode == kTestWrite && (flags & kDepthModeDirtyBit) != 0)
			return PrepareResult::kTranslated;

		failed = true;
		return PrepareResult::kFailed;
	}

	bool Scope::Restore() noexcept
	{
		const auto& layout = CurrentLayout();
		active = false;
		if (!restorePending)
			return true;
		if (!context || !shadow) {
			failed = true;
			return false;
		}

		context->OMSetDepthStencilState(savedState.Get(), savedStencilReference);
		auto* bytes = reinterpret_cast<std::byte*>(shadow);
		Write(bytes, layout.depthMode, savedDepthMode);
		Write(bytes, layout.previousDepthMode, savedPreviousDepthMode);
		Write(bytes, layout.stencilMode, savedStencilMode);
		Write(bytes, layout.stencilReference, savedShadowStencilReference);
		Write(bytes, layout.flags, savedFlags);

		std::uint32_t flags = 0;
		std::uint32_t depthMode = 0;
		std::uint32_t previousDepthMode = 0;
		std::uint32_t stencilMode = 0;
		std::uint32_t shadowStencilReference = 0;
		Read(bytes, layout.flags, flags);
		Read(bytes, layout.depthMode, depthMode);
		Read(bytes, layout.previousDepthMode, previousDepthMode);
		Read(bytes, layout.stencilMode, stencilMode);
		Read(bytes, layout.stencilReference, shadowStencilReference);
		if (!CurrentStateIs(context.Get(), savedState.Get(), savedStencilReference) ||
			flags != savedFlags || depthMode != savedDepthMode ||
			previousDepthMode != savedPreviousDepthMode ||
			stencilMode != savedStencilMode ||
			shadowStencilReference != savedShadowStencilReference) {
			failed = true;
			return false;
		}

		restorePending = false;
		savedState.Reset();
		context.Reset();
		shadow = nullptr;
		return true;
	}
}
