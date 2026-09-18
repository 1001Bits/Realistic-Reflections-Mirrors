#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace HandMirrorPostResolveCallSiteHookPolicy
{
	/**
	 * Exact Skyrim SE 1.5.97 primary first-person pre/post-resolve call.
	 *
	 * RenderFirstPersonView+0x103 calls the native camera wrapper at 0x12C15C0
	 * with RCX=NiCamera*, RDX=BSShaderAccumulator*, and R8D=0x43/0x53.  The
	 * wrapper completes both accumulator phases before it returns.  Owning this
	 * single caller therefore brackets the pane callbacks without replacing the
	 * process-wide BSShaderAccumulator vtable.
	 */
	inline constexpr std::uint64_t kSE1597RenderFirstPersonViewID = 100411;
	inline constexpr std::uintptr_t kSE1597RenderFirstPersonViewRVA = 0x12E21A0;
	inline constexpr std::uint64_t kSE1597RenderPreAndPostResolveID = 99789;
	inline constexpr std::uintptr_t kSE1597RenderPreAndPostResolveRVA = 0x12C15C0;
	inline constexpr std::size_t kCallOffsetFromRenderFirstPersonView = 0x103;
	inline constexpr std::uintptr_t kSE1597CallSiteRVA = 0x12E22A3;
	inline constexpr std::size_t kRel32CallSize = 5;
	inline constexpr std::uint8_t kRel32CallOpcode = 0xE8;

	// Exact caller context from 0x12E2287 through 0x12E22B0.  Only the call's
	// rel32 bytes are variable in the matcher; the decoded target is validated
	// independently against the exact address-library wrapper.
	inline constexpr std::size_t kWindowOffsetFromRenderFirstPersonView = 0xE7;
	inline constexpr std::size_t kCallOffsetInWindow = 0x1C;
	inline constexpr std::array<std::uint8_t, kCallOffsetInWindow> kCallPrefix{
		0x40, 0xF6, 0xDE,
		0x45, 0x1B, 0xC0,
		0x41, 0x83, 0xE0, 0x10,
		0x41, 0x83, 0xC8, 0x43,
		0x48, 0x8B, 0x15, 0x74, 0x11, 0xF5, 0x01,
		0x48, 0x8B, 0x0D, 0x4D, 0x11, 0xF5, 0x01
	};
	inline constexpr std::array<std::uint8_t, 9> kCallSuffix{
		0x83, 0x3D, 0xB1, 0x5C, 0xD4, 0x01, 0x01, 0x74, 0x14
	};
	inline constexpr std::size_t kCallWindowSize =
		kCallPrefix.size() + kRel32CallSize + kCallSuffix.size();
	inline constexpr std::size_t kFiveByteBranchStubSize = 14;

	inline constexpr std::uint32_t kPrimaryFlagsWithoutDepth = 0x43;
	inline constexpr std::uint32_t kPrimaryFlagsWithDepth = 0x53;
	inline constexpr std::uint32_t kMinimumExactPaneReturnDelta = 1;
	inline constexpr std::uint32_t kMaximumExactPaneReturnDelta = 2;

	/**
	 * Skyrim VR 1.4.15.  The live VR first-person renderer is 0x13244E0 (the
	 * VR Address Library maps ID 100411 to a never-called flat-shaped copy, so
	 * the owner pins REL::VariantID(100411, 107129, 0x13244E0)).  It calls the
	 * same NiCamera pre/post-resolve wrapper (0x12FF010, ID 99789) twice, at
	 * +0x27E and +0x641, exactly like SE (+0x103, +0x2E8).  The primary call
	 * passes R8D = 0 (`xor r8d,r8d`) instead of 0x43/0x53.
	 */
	inline constexpr std::uintptr_t kVR1415RenderFirstPersonViewRVA = 0x13244E0;
	inline constexpr std::uintptr_t kVR1415RenderPreAndPostResolveRVA = 0x12FF010;
	inline constexpr std::size_t kVRCallOffsetFromRenderFirstPersonView = 0x27E;
	inline constexpr std::uintptr_t kVR1415CallSiteRVA = 0x132475E;
	inline constexpr std::size_t kVRWindowOffsetFromRenderFirstPersonView = 0x262;
	inline constexpr std::array<std::uint8_t, kCallOffsetInWindow> kVRCallPrefix{
		0x01, 0x02, 0x00, 0x00, 0x00,
		0x0F, 0xBA, 0xE8, 0x07,
		0x89, 0x05, 0x5F, 0xC6, 0xE5, 0x01,
		0x45, 0x33, 0xC0,
		0x48, 0x8B, 0x15, 0xBD, 0x12, 0x16, 0x02,
		0x48, 0x8B, 0xCB
	};
	inline constexpr std::array<std::uint8_t, 9> kVRCallSuffix{
		0x8B, 0x05, 0x47, 0xC6, 0xE5, 0x01, 0x83, 0x3D, 0xF8
	};
	inline constexpr std::uint32_t kVRPrimaryFlags = 0;

	inline constexpr std::wstring_view kEnableMarker =
		L"RealisticReflections_HandMirrorCleanMissContinuity.enable";
	inline constexpr bool kNativeRunsBeforePresentationCallback = true;
	inline constexpr bool kPresentationRunsAfterCompleteWrapper = true;

	[[nodiscard]] constexpr bool MatchesCallWindow(
		const std::span<const std::uint8_t> window) noexcept
	{
		return window.size() == kCallWindowSize &&
			std::equal(kCallPrefix.begin(), kCallPrefix.end(), window.begin()) &&
			window[kCallOffsetInWindow] == kRel32CallOpcode &&
			std::equal(
				kCallSuffix.begin(), kCallSuffix.end(),
				window.begin() + kCallOffsetInWindow + kRel32CallSize);
	}

	[[nodiscard]] constexpr bool MatchesVRCallWindow(
		const std::span<const std::uint8_t> window) noexcept
	{
		return window.size() == kCallWindowSize &&
			std::equal(kVRCallPrefix.begin(), kVRCallPrefix.end(), window.begin()) &&
			window[kCallOffsetInWindow] == kRel32CallOpcode &&
			std::equal(
				kVRCallSuffix.begin(), kVRCallSuffix.end(),
				window.begin() + kCallOffsetInWindow + kRel32CallSize);
	}

	[[nodiscard]] constexpr bool MatchesCallWindowFor(
		const bool vr,
		const std::span<const std::uint8_t> window) noexcept
	{
		return vr ? MatchesVRCallWindow(window) : MatchesCallWindow(window);
	}

	[[nodiscard]] constexpr bool DecodeCallTargetFor(
		const bool vr,
		const std::uintptr_t windowAddress,
		const std::span<const std::uint8_t> window,
		std::uintptr_t& target) noexcept
	{
		target = 0;
		if (!MatchesCallWindowFor(vr, window))
			return false;
		std::uint32_t rawDisplacement = 0;
		for (std::size_t index = 0; index < sizeof(rawDisplacement); ++index) {
			rawDisplacement |= static_cast<std::uint32_t>(
				window[kCallOffsetInWindow + 1 + index]) << (index * 8);
		}
		const auto displacement = std::bit_cast<std::int32_t>(rawDisplacement);
		const auto callSite = windowAddress + kCallOffsetInWindow;
		target = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(callSite + kRel32CallSize) + displacement);
		return target != 0;
	}

	[[nodiscard]] constexpr bool DecodeCallTarget(
		const std::uintptr_t windowAddress,
		const std::span<const std::uint8_t> window,
		std::uintptr_t& target) noexcept
	{
		target = 0;
		if (!MatchesCallWindow(window))
			return false;
		std::uint32_t rawDisplacement = 0;
		for (std::size_t index = 0; index < sizeof(rawDisplacement); ++index) {
			rawDisplacement |= static_cast<std::uint32_t>(
				window[kCallOffsetInWindow + 1 + index]) << (index * 8);
		}
		const auto displacement = std::bit_cast<std::int32_t>(rawDisplacement);
		const auto callSite = windowAddress + kCallOffsetInWindow;
		target = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(callSite + kRel32CallSize) + displacement);
		return target != 0;
	}

	[[nodiscard]] constexpr bool MatchesFiveByteBranchStub(
		const std::span<const std::uint8_t> stub,
		const std::uintptr_t expectedDestination) noexcept
	{
		if (stub.size() != kFiveByteBranchStubSize || stub[0] != 0xFF ||
			stub[1] != 0x25 || stub[2] != 0 || stub[3] != 0 || stub[4] != 0 ||
			stub[5] != 0) {
			return false;
		}
		std::uintptr_t destination = 0;
		for (std::size_t index = 0; index < sizeof(destination); ++index) {
			destination |= static_cast<std::uintptr_t>(stub[6 + index]) <<
				(index * 8);
		}
		return destination == expectedDestination;
	}

	enum class Runtime : std::uint8_t
	{
		kUnsupported,
		kSE1597,
		kSkyrimVR1415
	};

	struct InstallFacts
	{
		Runtime runtime{ Runtime::kUnsupported };
		bool reflectionRequested{ false };
		bool cleanMissContinuityRequested{ false };
		bool exactEmptyEnableMarkerPresent{ false };
	};

	[[nodiscard]] constexpr bool IsSupportedRuntime(const Runtime runtime) noexcept
	{
		return runtime == Runtime::kSE1597 || runtime == Runtime::kSkyrimVR1415;
	}

	[[nodiscard]] constexpr bool ShouldInstall(const InstallFacts& facts) noexcept
	{
		return IsSupportedRuntime(facts.runtime) && facts.reflectionRequested &&
			facts.cleanMissContinuityRequested &&
			facts.exactEmptyEnableMarkerPresent;
	}

	[[nodiscard]] constexpr bool IsPrimaryFlags(
		const std::uint32_t flags) noexcept
	{
		return flags == kPrimaryFlagsWithoutDepth ||
			flags == kPrimaryFlagsWithDepth;
	}

	[[nodiscard]] constexpr bool IsPrimaryFlagsFor(
		const Runtime runtime,
		const std::uint32_t flags) noexcept
	{
		return runtime == Runtime::kSkyrimVR1415 ? flags == kVRPrimaryFlags :
		                                           IsPrimaryFlags(flags);
	}

	struct CallSiteLayout
	{
		std::uintptr_t renderFirstPersonViewRVA{ 0 };
		std::uintptr_t renderPreAndPostResolveRVA{ 0 };
		std::size_t callOffset{ 0 };
		std::size_t windowOffset{ 0 };
		std::uintptr_t callSiteRVA{ 0 };
	};

	[[nodiscard]] constexpr CallSiteLayout SelectCallSiteLayout(
		const Runtime runtime) noexcept
	{
		if (runtime == Runtime::kSkyrimVR1415) {
			return CallSiteLayout{
				.renderFirstPersonViewRVA = kVR1415RenderFirstPersonViewRVA,
				.renderPreAndPostResolveRVA = kVR1415RenderPreAndPostResolveRVA,
				.callOffset = kVRCallOffsetFromRenderFirstPersonView,
				.windowOffset = kVRWindowOffsetFromRenderFirstPersonView,
				.callSiteRVA = kVR1415CallSiteRVA
			};
		}
		return CallSiteLayout{
			.renderFirstPersonViewRVA = kSE1597RenderFirstPersonViewRVA,
			.renderPreAndPostResolveRVA = kSE1597RenderPreAndPostResolveRVA,
			.callOffset = kCallOffsetFromRenderFirstPersonView,
			.windowOffset = kWindowOffsetFromRenderFirstPersonView,
			.callSiteRVA = kSE1597CallSiteRVA
		};
	}

	enum class InvocationAction : std::uint8_t
	{
		kChainOnly,
		kBeginOwningPrimary,
		kFailStopOwnershipLost,
		kFailStopTokenExhausted
	};

	struct InvocationFacts
	{
		bool exactRuntime{ false };
		bool hookInstalled{ false };
		bool hookOwned{ false };
		bool exactEmptyEnableMarkerLatched{ false };
		bool reflectionEnabled{ false };
		bool cleanMissContinuityEffective{ false };
		bool firstPersonTLSActive{ false };
		bool firstPersonAlreadyReturned{ false };
		bool lifecycleTransitionSuppressed{ false };
		bool lifecycleTransitionSuspendedLive{ false };
		bool privatePassActive{ false };
		bool nestedInvocation{ false };
		bool exactPrimaryFlags{ false };
		bool entryFrameComplete{ false };
		bool tokenAvailable{ false };
	};

	[[nodiscard]] constexpr InvocationAction SelectInvocationAction(
		const InvocationFacts& facts) noexcept
	{
		if (!facts.exactRuntime || !facts.hookInstalled ||
			!facts.exactEmptyEnableMarkerLatched || !facts.reflectionEnabled ||
			!facts.cleanMissContinuityEffective || !facts.firstPersonTLSActive ||
			facts.firstPersonAlreadyReturned ||
			facts.lifecycleTransitionSuppressed ||
			facts.lifecycleTransitionSuspendedLive || facts.privatePassActive ||
			facts.nestedInvocation || !facts.exactPrimaryFlags ||
			!facts.entryFrameComplete) {
			return InvocationAction::kChainOnly;
		}
		if (!facts.hookOwned)
			return InvocationAction::kFailStopOwnershipLost;
		if (!facts.tokenAvailable)
			return InvocationAction::kFailStopTokenExhausted;
		return InvocationAction::kBeginOwningPrimary;
	}

	enum class CompletionAction : std::uint8_t
	{
		kChainOnly,
		kPresentLatestStagedReturn,
		kFailStopOwnershipLost,
		kFailStopReturnCountRegressed,
		kFailStopReturnCountOverflow
	};

	struct CompletionFacts
	{
		bool exactRuntime{ false };
		bool hookInstalled{ false };
		bool hookOwned{ false };
		bool exactEmptyEnableMarkerLatched{ false };
		bool reflectionEnabled{ false };
		bool cleanMissContinuityEffective{ false };
		bool firstPersonTLSActive{ false };
		bool firstPersonAlreadyReturned{ false };
		bool lifecycleTransitionSuppressed{ false };
		bool lifecycleTransitionSuspendedLive{ false };
		bool privatePassActive{ false };
		bool owningInvocationActive{ false };
		bool exactInvocationArguments{ false };
		bool exactPrimaryFlags{ false };
		bool exactInvocationToken{ false };
		bool exactStageToken{ false };
		bool exactStageReturnCount{ false };
		bool stagedReturnComplete{ false };
		std::uint32_t entryReturnCount{ 0 };
		std::uint32_t exitReturnCount{ 0 };
	};

	[[nodiscard]] constexpr CompletionAction SelectCompletionAction(
		const CompletionFacts& facts) noexcept
	{
		if (!facts.exactRuntime || !facts.hookInstalled ||
			!facts.exactEmptyEnableMarkerLatched || !facts.reflectionEnabled ||
			!facts.cleanMissContinuityEffective || !facts.firstPersonTLSActive ||
			facts.firstPersonAlreadyReturned ||
			facts.lifecycleTransitionSuppressed ||
			facts.lifecycleTransitionSuspendedLive || facts.privatePassActive ||
			!facts.owningInvocationActive || !facts.exactInvocationArguments ||
			!facts.exactPrimaryFlags) {
			return CompletionAction::kChainOnly;
		}
		if (!facts.hookOwned)
			return CompletionAction::kFailStopOwnershipLost;
		if (facts.exitReturnCount < facts.entryReturnCount)
			return CompletionAction::kFailStopReturnCountRegressed;
		const auto delta = facts.exitReturnCount - facts.entryReturnCount;
		if (delta == 0)
			return CompletionAction::kChainOnly;
		if (delta > kMaximumExactPaneReturnDelta)
			return CompletionAction::kFailStopReturnCountOverflow;
		if (!facts.exactInvocationToken || !facts.exactStageToken ||
			!facts.exactStageReturnCount || !facts.stagedReturnComplete) {
			return CompletionAction::kChainOnly;
		}
		return CompletionAction::kPresentLatestStagedReturn;
	}

	static_assert(
		kSE1597RenderFirstPersonViewRVA +
			kCallOffsetFromRenderFirstPersonView ==
		kSE1597CallSiteRVA);
	static_assert(
		kWindowOffsetFromRenderFirstPersonView + kCallOffsetInWindow ==
		kCallOffsetFromRenderFirstPersonView);
	static_assert(
		kVR1415RenderFirstPersonViewRVA +
			kVRCallOffsetFromRenderFirstPersonView ==
		kVR1415CallSiteRVA);
	static_assert(
		kVRWindowOffsetFromRenderFirstPersonView + kCallOffsetInWindow ==
		kVRCallOffsetFromRenderFirstPersonView);
	static_assert(kNativeRunsBeforePresentationCallback);
	static_assert(kPresentationRunsAfterCompleteWrapper);
}
