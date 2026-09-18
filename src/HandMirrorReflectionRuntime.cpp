#include "PCH.h"
#include "MirrorPerformance.h"
#include "HandMirrorSettings.h"
#include "MirrorCaptureWorkPolicy.h"

#include "HandMirrorReflectionRuntime.h"
#include "EngineDeviceIdentity.h"

#include "HandMirrorApprovedContentReadOnlyObserver.h"
#include "HandMirrorExactPanePassCohortPolicy.h"
#include "HandMirrorLivePaneReturnPolicy.h"
#include "HandMirrorLastGoodPresentationPolicy.h"
#include "HandMirrorLoweredPresentationPolicy.h"
#include "MirrorContentProfile.h"
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
#	include "HandMirrorLateTargetSurvivalProbe.h"
#endif
#include "HandMirrorOuterFirstPersonClosePolicy.h"
#include "HandMirrorPostResolveCallSiteHookPolicy.h"
#include "HandMirrorPresentationRetargetPolicy.h"
#include "MirrorCameraHandRuntimeSeam.h"
#include "MirrorFramePublication.h"
#include "MirrorsOfSkyrimPaneDelivery.h"
#include "SecondView.h"
#include "MirrorCaptureSizing.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <format>
#include <system_error>
#include <utility>

namespace HandMirrorReflectionRuntime
{
	static MirrorCaptureSizing::Extent PrivateExtent() noexcept
	{
		return MirrorCaptureSizing::ForSquare(HandMirrorLoweredPresentationPolicy::TargetDimension(
			HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed),
			static_cast<std::uint32_t>(HandMirrorSettings::RaisedResolution()),
			static_cast<std::uint32_t>(HandMirrorSettings::LoweredResolution())));
	}
	std::uint32_t PrivateTargetDimension() noexcept { return PrivateExtent().width; }
	std::uint32_t PrivateTargetHeight() noexcept { return PrivateExtent().height; }
	std::uint32_t PrivateTargetMipLevels() noexcept { return PrivateExtent().MipLevels(); }
	std::uint32_t PrivateCaptureMip(bool raised) noexcept
	{
		if (MirrorCaptureSizing::UsesFramebuffer()) return 0;
		return HandMirrorLoweredPresentationPolicy::CaptureMip(raised,
			HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed),
			HandMirrorSettings::RaisedResolution(), HandMirrorSettings::LoweredResolution());
	}
	std::uint32_t PrivateTargetReducedViews() noexcept
	{
		if (MirrorCaptureSizing::UsesFramebuffer()) return 0;
		return HandMirrorLoweredPresentationPolicy::ReducedViewCount(
			HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed),
			static_cast<std::uint32_t>(HandMirrorSettings::RaisedResolution()),
			static_cast<std::uint32_t>(HandMirrorSettings::LoweredResolution()));
	}

	namespace
	{
		namespace Bridge = HandMirrorRuntimeBridgePolicy;
		namespace Cohort = HandMirrorExactPanePassCohortPolicy;
		namespace ClosePolicy = HandMirrorOuterFirstPersonClosePolicy;
		namespace Content = HandMirrorContentRuntimePolicy;
		namespace LiveReturn = HandMirrorLivePaneReturnPolicy;
		namespace LastGoodPolicy = HandMirrorLastGoodPresentationPolicy;
		namespace LoweredPolicy = HandMirrorLoweredPresentationPolicy;
		namespace PostResolveHookPolicy =
			HandMirrorPostResolveCallSiteHookPolicy;
		namespace Presentation = HandMirrorPresentationRetargetPolicy;
		namespace ReceiptPolicy = HandMirrorFrameReceiptPolicy;
		namespace StorePolicy = HandMirrorInternalPublicationStore;
		namespace Reservation = HandMirrorCaptureReservationPolicy;
		[[nodiscard]] MirrorPaneDelivery::CurrentMainDrawStateStatus QueryHandDrawState(
			MirrorPaneDelivery::CurrentMainDrawState& main) noexcept
		{
			return MirrorPaneDelivery::QueryCurrentMainDrawState(main,
				HandMirrorApprovedContentReadOnlyObserver::VRRuntimeFixEnabled());
		}

		[[nodiscard]] bool TryGetHandDrawState(
			MirrorPaneDelivery::CurrentMainDrawState& main) noexcept
		{
			return QueryHandDrawState(main) ==
				MirrorPaneDelivery::CurrentMainDrawStateStatus::kReady;
		}

		[[nodiscard]] MirrorPaneRenderer::DrawStatus DrawHandPane(
			MirrorPaneRenderer::Renderer& renderer,
			const MirrorPaneDelivery::CurrentMainDrawState& main,
			const MirrorPaneRenderer::DrawRequest& request,
			bool* motionOutput = nullptr) noexcept
		{
			if (!HandMirrorApprovedContentReadOnlyObserver::VRRuntimeFixEnabled())
				return renderer.Draw(main.context, request, motionOutput);
			if (motionOutput)
				*motionOutput = false;
			if (main.eyeCount != main.eyes.size())
				return MirrorPaneRenderer::DrawStatus::kInvalidArgument;
			return HandMirrorStereoPresentation::Draw(
				renderer, main.context, request, main.eyes);
		}

		inline constexpr bool kPresentAtExactPaneReturn = true;
		inline constexpr std::uint32_t kMaximumHandCoverTransitionLogs = 32;
		inline constexpr std::uint32_t kMaximumHandCoverEvidenceLogs = 64;

		std::atomic_bool g_prepareAttempted{ false };
		std::atomic_bool g_requested{ false };
		std::atomic_bool g_cleanMissContinuityRequested{ false };
		std::atomic_bool g_postResolveEnableMarkerLatched{ false };
		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_inventoryMenuOpen{ false };
		std::atomic_bool g_inventoryTransitionSuspended{ false };
		std::atomic_bool g_inventoryResumeRequested{ false };
		std::atomic_bool g_equipWakePending{ false };
		std::atomic_bool g_loadTransitionSuspended{ false };
		std::atomic<HANDLE> g_handMirrorEvidenceEvent{ nullptr };

		using PostResolveFunction = void(
			RE::NiCamera*, RE::BSShaderAccumulator*, std::uint32_t);
		std::atomic<PostResolveFunction*> g_postResolveNative{ nullptr };
		std::atomic_bool g_postResolveHookInstallAttempted{ false };
		std::atomic_bool g_postResolveHookInstalled{ false };
		std::atomic<std::uintptr_t> g_postResolveCallSiteAddress{ 0 };
		std::atomic<std::uintptr_t> g_postResolveInstalledBranchTarget{ 0 };
		thread_local bool g_postResolveCallbackActive{ false };

		struct PostResolveHookDiagnostics
		{
			std::atomic<std::uint64_t> installAttempts{ 0 };
			std::atomic<std::uint64_t> installSuccesses{ 0 };
			std::atomic<std::uint64_t> installFailures{ 0 };
			std::atomic<std::uint64_t> transactionRollbackFailures{ 0 };
			std::atomic<std::uint64_t> nativeCalls{ 0 };
			std::atomic<std::uint64_t> nativeReturns{ 0 };
			std::atomic<std::uint64_t> callbackCalls{ 0 };
			std::atomic<std::uint64_t> callbackReentries{ 0 };
			std::atomic<std::uint64_t> nestedChains{ 0 };
			std::atomic<std::uint64_t> inactiveChains{ 0 };
			std::atomic<std::uint64_t> privatePassSkips{ 0 };
			std::atomic<std::uint64_t> liveLifecycleSkips{ 0 };
			std::atomic<std::uint64_t> entryFrameRejects{ 0 };
			std::atomic<std::uint64_t> zeroDeltaChains{ 0 };
			std::atomic<std::uint64_t> unstampedDeltaRejects{ 0 };
			std::atomic<std::uint64_t> ownershipChecks{ 0 };
			std::atomic<std::uint64_t> ownershipLosses{ 0 };
			std::atomic<std::uint64_t> callbackExceptions{ 0 };
			std::atomic<std::uint64_t> shutdownRestores{ 0 };
			std::atomic<std::uint64_t> shutdownOwnershipSkips{ 0 };
			std::atomic<std::uint32_t> lastFlags{ 0 };
			std::atomic<std::uint32_t> lastException{ 0 };
		};
		PostResolveHookDiagnostics g_postResolveHookDiagnostics{};

		void FailStopInternal() noexcept;

		constexpr wchar_t kHandMirrorEvidenceEventName[] =
			L"Local\\MirrorsOfSkyrimHandMirrorEvidenceF10";

		struct PostResolveMarkerObservation
		{
			std::filesystem::path path{};
			std::error_code error{};
			bool present{ false };
			bool accepted{ false };
		};

		[[nodiscard]] PostResolveMarkerObservation
			ObservePostResolveEnableMarker() noexcept
		{
			PostResolveMarkerObservation observation{};
			try {
				std::array<wchar_t, 32768> executable{};
				const DWORD length = GetModuleFileNameW(
					nullptr, executable.data(),
					static_cast<DWORD>(executable.size()));
				if (length == 0 || length >= executable.size()) {
					observation.error = std::error_code{
						static_cast<int>(GetLastError()), std::system_category() };
					return observation;
				}
				observation.path = std::filesystem::path{
					std::wstring_view{ executable.data(), length } }.parent_path() /
					L"Data" / std::wstring{ PostResolveHookPolicy::kEnableMarker };
				observation.present =
					std::filesystem::exists(observation.path, observation.error);
				if (observation.error || !observation.present)
					return observation;

				const DWORD attributes = GetFileAttributesW(observation.path.c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES ||
					(attributes & (FILE_ATTRIBUTE_DIRECTORY |
						FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
					return observation;
				}
				const HANDLE file = CreateFileW(
					observation.path.c_str(), GENERIC_READ,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL |
						FILE_FLAG_OPEN_REPARSE_POINT,
					nullptr);
				if (file == INVALID_HANDLE_VALUE)
					return observation;
				BY_HANDLE_FILE_INFORMATION information{};
				observation.accepted = GetFileInformationByHandle(
					file, std::addressof(information)) != FALSE &&
					(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
						FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
					information.nFileSizeHigh == 0 && information.nFileSizeLow == 0;
				(void)CloseHandle(file);
			} catch (...) {
				observation.accepted = false;
			}
			return observation;
		}

		void SetHandMirrorEvidenceEventActive(const bool active) noexcept
		{
			if (!active) {
				if (const HANDLE stale = g_handMirrorEvidenceEvent.exchange(
						nullptr, std::memory_order_acq_rel)) {
					(void)CloseHandle(stale);
				}
				return;
			}
			if (g_handMirrorEvidenceEvent.load(std::memory_order_acquire))
				return;

			// Auto-reset is intentional: one F10 request authorizes exactly one
			// successful outer-final evidence line. Creation failure is diagnostic-only.
			const HANDLE created = CreateEventW(
				nullptr, FALSE, FALSE, kHandMirrorEvidenceEventName);
			if (!created)
				return;
			HANDLE expected = nullptr;
			if (!g_handMirrorEvidenceEvent.compare_exchange_strong(
					expected, created, std::memory_order_release,
					std::memory_order_acquire)) {
				(void)CloseHandle(created);
			}
		}

		[[nodiscard]] bool HandLifecycleTransitionSuspended() noexcept
		{
			return g_loadTransitionSuspended.load(std::memory_order_acquire) ||
				g_inventoryMenuOpen.load(std::memory_order_acquire) ||
				g_inventoryTransitionSuspended.load(std::memory_order_acquire) ||
				g_equipWakePending.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool CleanMissContinuityEffective() noexcept
		{
			return g_cleanMissContinuityRequested.load(std::memory_order_acquire) &&
			       g_enabled.load(std::memory_order_acquire) &&
			       !g_faulted.load(std::memory_order_acquire);
		}

		struct VisibilityState
		{
			Content::HandMirrorOwnerIdentity accumulatedOwner{};
			Content::HandMirrorOwnerIdentity currentHandOwner{};
			std::uint64_t lastVisibleSourceSequence{ 0 };
			std::uint64_t lastVisibilityUpdateSourceSequence{ 0 };
			std::uint32_t consecutiveRaisedVisibleFrames{ 0 };
			std::uint32_t consecutiveIneligibleFrames{ 0 };
			Content::MirrorOwnerSelection currentOwner{
				Content::MirrorOwnerSelection::kDark };
			bool ambiguousThisSource{ false };
			// Raised/lowered state of the most recently returned exact pane; a
			// lowered pane yields the capture channel to an eligible wall mirror.
			bool raisedPresentation{ false };
			bool loweredYieldLogged{ false };
			// Last main-world source at which a wall candidate was eligible; a
			// lowered hand stays yielded for kWallYieldHoldSources after it so the
			// two owners cannot alternate frame by frame (03:35 run: 550 flips).
			std::uint64_t lastWallEligibleSource{ 0 };
			// Last source on which the hand was committed for capture.  A raised
			// hand and an eligible wall interleave source by source; neither
			// publication is retired by the other (disjoint private targets).
			std::uint64_t lastHandCaptureSource{ 0 };
		};

		struct NativeSlot
		{
			MirrorPaneRenderer::PublishedFrame frame{};
			StorePolicy::PublicationSnapshot publication{};
			LastGoodPolicy::PresentationMode presentationMode{ LastGoodPolicy::PresentationMode::kUnknown };
			bool valid{ false };
		};

		enum class CohortRuntimeStatus : std::uint8_t
		{
			kNone,
			kFirstStaged,
			kSingleAccepted,
			kDoubleAccepted,
			kZeroFallbackPresented,
			kZeroDark,
			kMismatchDark,
			kOverflowDark
		};

		// Typed partition of first-return authority failures plus allowed native
		// presentation/geometry drift.  The latter remain visible in telemetry but
		// are deliberately excluded from the authorization mask.
		enum class FirstReturnReject : std::uint32_t
		{
			kNone = 0,
			kCandidateUnavailable = 1u << 0,
			kCurrentTarget = 1u << 1,
			kMainViewFrame = 1u << 2,
			kGraphicsFrame = 1u << 3,
			kReceipt = 1u << 4,
			kExactPass = 1u << 5,
			kDevice = 1u << 6,
			kContext = 1u << 7,
			kResourcesUnchecked = 1u << 8,
			kResources = 1u << 9,
			kOwner = 1u << 10,
			kSurface = 1u << 11,
			kView = 1u << 12,
			kProjection = 1u << 13,
			kViewProjection = 1u << 14,
			kOrigin = 1u << 15,
			kViewport = 1u << 16,
			kStableGeneration = 1u << 17,
			kGeometry = 1u << 18,
			kReturnedPresentationInvalid = 1u << 19
		};

		using FirstReturnRejectMask = std::uint32_t;

		[[nodiscard]] constexpr FirstReturnRejectMask Mask(
			const FirstReturnReject value) noexcept
		{
			return static_cast<FirstReturnRejectMask>(value);
		}

		[[nodiscard]] constexpr bool HasFirstReturnReject(
			const FirstReturnRejectMask rejects,
			const FirstReturnReject value) noexcept
		{
			return (rejects & Mask(value)) != 0;
		}

		struct FirstReturnValidationEvidence
		{
			bool candidateAvailable{ false };
			bool currentTargetExact{ false };
			bool mainViewFrameExact{ false };
			bool graphicsFrameExact{ false };
			bool receiptExact{ false };
			bool exactPass{ false };
			bool deviceExact{ false };
			bool contextExact{ false };
			bool resourcesChecked{ false };
			bool resourcesExact{ false };
			bool ownerExact{ false };
			bool surfaceExact{ false };
			bool viewExact{ false };
			bool projectionExact{ false };
			bool viewProjectionExact{ false };
			bool originExact{ false };
			bool viewportExact{ false };
			bool stableGenerationExact{ false };
			bool geometryExact{ false };
			bool returnedPresentationComplete{ false };
		};

		[[nodiscard]] constexpr FirstReturnRejectMask
		ClassifyFirstReturnRejects(
			const FirstReturnValidationEvidence& evidence) noexcept
		{
			FirstReturnRejectMask rejects = 0;
			if (!evidence.currentTargetExact)
				rejects |= Mask(FirstReturnReject::kCurrentTarget);
			if (!evidence.mainViewFrameExact)
				rejects |= Mask(FirstReturnReject::kMainViewFrame);
			if (!evidence.graphicsFrameExact)
				rejects |= Mask(FirstReturnReject::kGraphicsFrame);
			if (!evidence.deviceExact)
				rejects |= Mask(FirstReturnReject::kDevice);
			if (!evidence.contextExact)
				rejects |= Mask(FirstReturnReject::kContext);
			if (!evidence.resourcesChecked) {
				rejects |= Mask(FirstReturnReject::kResourcesUnchecked);
			} else if (!evidence.resourcesExact) {
				rejects |= Mask(FirstReturnReject::kResources);
			}
			if (!evidence.viewExact)
				rejects |= Mask(FirstReturnReject::kView);
			if (!evidence.projectionExact)
				rejects |= Mask(FirstReturnReject::kProjection);
			if (!evidence.viewProjectionExact)
				rejects |= Mask(FirstReturnReject::kViewProjection);
			if (!evidence.originExact)
				rejects |= Mask(FirstReturnReject::kOrigin);
			if (!evidence.viewportExact)
				rejects |= Mask(FirstReturnReject::kViewport);

			if (!evidence.candidateAvailable) {
				return rejects |
					Mask(FirstReturnReject::kCandidateUnavailable);
			}
			if (!evidence.receiptExact)
				rejects |= Mask(FirstReturnReject::kReceipt);
			if (!evidence.exactPass)
				rejects |= Mask(FirstReturnReject::kExactPass);
			if (!evidence.ownerExact)
				rejects |= Mask(FirstReturnReject::kOwner);
			if (!evidence.surfaceExact)
				rejects |= Mask(FirstReturnReject::kSurface);
			if (!evidence.stableGenerationExact) {
				rejects |= Mask(FirstReturnReject::kStableGeneration);
			}
			if (!evidence.geometryExact)
				rejects |= Mask(FirstReturnReject::kGeometry);
			if (!evidence.returnedPresentationComplete) {
				rejects |= Mask(
					FirstReturnReject::kReturnedPresentationInvalid);
			}
			return rejects;
		}

		inline constexpr FirstReturnRejectMask kFirstReturnDriftMask =
			Mask(FirstReturnReject::kView) |
			Mask(FirstReturnReject::kProjection) |
			Mask(FirstReturnReject::kViewProjection) |
			Mask(FirstReturnReject::kOrigin) |
			Mask(FirstReturnReject::kViewport) |
			Mask(FirstReturnReject::kGeometry);

		enum class SecondReturnDifference : std::uint32_t
		{
			kNone = 0,
			kReceiptSource = 1u << 0,
			kReceiptMainView = 1u << 1,
			kReceiptGraphics = 1u << 2,
			kOwner = 1u << 3,
			kStableGeneration = 1u << 4,
			kSurfaceIdentity = 1u << 5,
			kGeometry = 1u << 6,
			kDevice = 1u << 7,
			kContext = 1u << 8,
			kColorResource = 1u << 9,
			kDepthResource = 1u << 10,
			kView = 1u << 11,
			kProjection = 1u << 12,
			kOrigin = 1u << 13,
			kViewport = 1u << 14
		};

		using SecondReturnDifferenceMask = std::uint32_t;

		[[nodiscard]] constexpr SecondReturnDifferenceMask Mask(
			const SecondReturnDifference value) noexcept
		{
			return static_cast<SecondReturnDifferenceMask>(value);
		}

		[[nodiscard]] constexpr SecondReturnDifferenceMask
		ClassifySecondReturnDifferences(
			const Cohort::ValueIdentityEquality& equality) noexcept
		{
			SecondReturnDifferenceMask differences = 0;
			if (!equality.receiptSourceSequenceEqual)
				differences |= Mask(SecondReturnDifference::kReceiptSource);
			if (!equality.receiptMainViewFrameEqual)
				differences |= Mask(SecondReturnDifference::kReceiptMainView);
			if (!equality.receiptGraphicsFrameEqual)
				differences |= Mask(SecondReturnDifference::kReceiptGraphics);
			if (!equality.ownerIdentityEqual)
				differences |= Mask(SecondReturnDifference::kOwner);
			if (!equality.stableGenerationEqual)
				differences |= Mask(SecondReturnDifference::kStableGeneration);
			if (!equality.surfaceIdentityEqual)
				differences |= Mask(SecondReturnDifference::kSurfaceIdentity);
			if (!equality.geometryIdentityEqual)
				differences |= Mask(SecondReturnDifference::kGeometry);
			if (!equality.deviceIdentityEqual)
				differences |= Mask(SecondReturnDifference::kDevice);
			if (!equality.contextIdentityEqual)
				differences |= Mask(SecondReturnDifference::kContext);
			if (!equality.retainedColorResourceIdentityEqual)
				differences |= Mask(SecondReturnDifference::kColorResource);
			if (!equality.retainedDepthResourceIdentityEqual)
				differences |= Mask(SecondReturnDifference::kDepthResource);
			if (!equality.viewBitsEqual)
				differences |= Mask(SecondReturnDifference::kView);
			if (!equality.projectionBitsEqual)
				differences |= Mask(SecondReturnDifference::kProjection);
			if (!equality.originBitsEqual)
				differences |= Mask(SecondReturnDifference::kOrigin);
			if (!equality.viewportBitsEqual)
				differences |= Mask(SecondReturnDifference::kViewport);
			return differences;
		}

		inline constexpr SecondReturnDifferenceMask kSecondReturnDriftMask =
			Mask(SecondReturnDifference::kGeometry) |
			Mask(SecondReturnDifference::kColorResource) |
			Mask(SecondReturnDifference::kDepthResource) |
			Mask(SecondReturnDifference::kView) |
			Mask(SecondReturnDifference::kProjection) |
			Mask(SecondReturnDifference::kOrigin) |
			Mask(SecondReturnDifference::kViewport);

		struct CohortDiagnostics
		{
			ReceiptPolicy::FrameReceipt lastReceipt{};
			std::uint64_t genericEntries{ 0 };
			std::uint64_t genericInactiveOrNullPass{ 0 };
			std::uint64_t genericThreadRejects{ 0 };
			std::uint64_t genericSourceZero{ 0 };
			std::uint64_t mainQueryReady{ 0 };
			std::uint64_t mainQueryDeliveryFaulted{ 0 };
			std::uint64_t mainQueryPrivatePassActive{ 0 };
			std::uint64_t mainQueryMainWorldActive{ 0 };
			std::uint64_t mainQueryMainWorldFrameMissing{ 0 };
			std::uint64_t mainQueryDeferredSnapshotInvalid{ 0 };
			std::uint64_t mainQueryDeferredFrameMismatch{ 0 };
			std::uint64_t mainQueryDeferredGraphicsMissing{ 0 };
			std::uint64_t mainQueryEngineStateUnavailable{ 0 };
			std::uint64_t mainQueryRefractionPassActive{ 0 };
			std::uint64_t mainQueryGraphicsFrameMismatch{ 0 };
			std::uint64_t genericInvalidReceiptOrTarget{ 0 };
			std::uint64_t genericRunWithReached{ 0 };
			std::uint64_t captureBegins{ 0 };
			std::uint64_t publications{ 0 };
			std::uint64_t firstStages{ 0 };
			std::uint64_t latestRestages{ 0 };
			std::uint64_t singleAccepted{ 0 };
			std::uint64_t doubleAccepted{ 0 };
			std::uint64_t zeroDark{ 0 };
			std::uint64_t mismatchDark{ 0 };
			std::uint64_t overflowDark{ 0 };
			std::uint64_t visibilityAdvances{ 0 };
			std::uint64_t publicationConsumes{ 0 };
			std::uint64_t directDrawAttempts{ 0 };
			std::uint64_t directDrawSuccesses{ 0 };
			std::uint64_t directDrawProjectionMisses{ 0 };
			std::uint64_t directReflectedRetargets{ 0 };
			std::uint64_t directReflectedRetargetRejects{ 0 };
			std::uint64_t exactActualOmFreshDrawAttempts{ 0 };
			std::uint64_t exactActualOmFreshDrawSuccesses{ 0 };
			std::uint64_t exactActualOmLastGoodReplayCalls{ 0 };
			std::uint64_t exactActualOmLastGoodReplayDraws{ 0 };
			std::uint64_t postResolveCallbacks{ 0 };
			std::uint64_t postResolveNewStageObservations{ 0 };
			std::uint64_t postResolveTokenRejects{ 0 };
			std::uint64_t postResolveSourceRejects{ 0 };
			std::uint64_t postResolveReceiptRejects{ 0 };
			std::uint64_t postResolveFrameRejects{ 0 };
			std::uint64_t postResolveAccumulatorRejects{ 0 };
			std::uint64_t postResolveTargetRejects{ 0 };
			std::uint64_t postResolveFreshDrawAttempts{ 0 };
			std::uint64_t postResolveFreshDrawSuccesses{ 0 };
			std::uint64_t postResolveLastGoodReplayCalls{ 0 };
			std::uint64_t postResolveLastGoodReplayDraws{ 0 };
			std::uint64_t postResolvePresentations{ 0 };
			MirrorPaneRenderer::DrawStatus lastPostResolveDrawStatus{
				MirrorPaneRenderer::DrawStatus::kNoPublishedFrame };
			std::uint64_t outerFreshDrawAttempts{ 0 };
			std::uint64_t outerFreshDrawSuccesses{ 0 };
			std::uint64_t outerLastGoodReplayCalls{ 0 };
			std::uint64_t outerLastGoodReplayDraws{ 0 };
			std::uint64_t stablePresentationRetryAttempts{ 0 };
			std::uint64_t stablePresentationRetryMainViewNotVisible{ 0 };
			std::uint64_t stablePresentationRetryReflectedUncovered{ 0 };
			std::uint64_t stablePresentationRetrySuccesses{ 0 };
			std::uint64_t stablePresentationRetryFailures{ 0 };
			std::uint64_t handCoverClipChecks{ 0 };
			std::uint64_t handCoverClipInvalid{ 0 };
			std::uint64_t handCoverMainWRisks{ 0 };
			std::uint64_t handCoverReflectedWRisks{ 0 };
			std::uint64_t handCoverRiskTransitions{ 0 };
			std::uint32_t handCoverTransitionLogs{ 0 };
			std::uint64_t handCoverEvidenceSignals{ 0 };
			std::uint32_t handCoverEvidenceLogs{ 0 };
			std::uint64_t handCoverEvidenceWaitFailures{ 0 };
			float lastHandCoverMinimumMainW{ 0.0f };
			float lastHandCoverMinimumReflectedW{ 0.0f };
			std::uint8_t lastHandCoverMainPositiveWVertices{ 0 };
			std::uint8_t lastHandCoverReflectedPositiveWVertices{ 0 };
			std::uint8_t lastHandCoverVertexCount{ 0 };
			std::uint8_t lastHandCoverRiskMask{ 0 };
			bool handCoverRiskStateInitialized{ false };
			std::uint64_t affineMotionEligible{ 0 };
			std::uint64_t affineMotionWritten{ 0 };
			std::uint64_t affineMotionWriteRejects{ 0 };
			std::uint64_t affineHistoryMissingRejects{ 0 };
			std::uint64_t affineHistoryReceiptRejects{ 0 };
			std::uint64_t affineHistoryIdentityRejects{ 0 };
			std::uint64_t affineHistoryViewportRejects{ 0 };
			std::uint64_t affineHistoryCaptureRejects{ 0 };
			std::uint64_t affineHistoryResets{ 0 };
			std::uint64_t affineHistoryNoFinalDrawResets{ 0 };
			std::uint64_t affineHistoryLoweredResets{ 0 };
			std::uint64_t affineHistoryNoncurrentPublicationResets{ 0 };
			std::uint64_t affineHistoryMotionTargetResets{ 0 };
			std::uint64_t affineHistoryDriftResets{ 0 };
			std::uint64_t affineHistoryDrawFailureResets{ 0 };
			std::uint64_t affineHistoryLifecycleResets{ 0 };
			std::uint64_t affineHistoryCommits{ 0 };
			std::uint64_t cleanMissContinuityArms{ 0 };
			std::uint64_t cleanMissContinuityDraws{ 0 };
			std::uint64_t cleanMissContinuityIdentityRejects{ 0 };
			std::uint64_t cleanMissContinuityExpiryReleases{ 0 };
			std::uint64_t orderingPredecessorPromotions{ 0 };
			std::uint64_t zeroFallbackEntryRetainAttempts{ 0 };
			std::uint64_t zeroFallbackEntryRetains{ 0 };
			std::uint64_t zeroFallbackEntrySurfaceSelections{ 0 };
			std::uint64_t zeroFallbackDetachedSurfaceSelections{ 0 };
			std::uint64_t zeroFallbackReturnStateRejects{ 0 };
			std::uint64_t zeroFallbackEntryMissingRejects{ 0 };
			std::uint64_t zeroFallbackCandidateRejects{ 0 };
			std::uint64_t zeroFallbackRevalidateRejects{ 0 };
			std::uint64_t zeroFallbackLiveReturnRejects{ 0 };
			std::uint64_t zeroFallbackPublicationRejects{ 0 };
			std::uint64_t zeroFallbackStages{ 0 };
			std::uint64_t zeroFallbackDraws{ 0 };
			std::uint64_t lastGoodCommits{ 0 };
			std::uint64_t lastGoodApertureCoverageRejects{ 0 };
			std::uint64_t lastGoodSnapshotRejects{ 0 };
			std::uint64_t lastGoodReplayAttempts{ 0 };
			std::uint64_t lastGoodReplayDraws{ 0 };
			std::uint64_t lastGoodReplayCoverageRejects{ 0 };
			std::uint64_t lastGoodReplayDetachedCoverageFallbacks{ 0 };
			std::uint64_t lastGoodReplayRefreshCoverageRejects{ 0 };
			std::uint64_t lastGoodReplayIdentityRejects{ 0 };
			std::uint64_t lastGoodReplayPresentationModeRejects{ 0 };
			std::uint64_t lastGoodReplayTargetRejects{ 0 };
			std::uint64_t lastGoodReplayLoweredStaleRejects{ 0 };
			std::uint64_t livePosePublicationModeRejects{ 0 };
			std::uint64_t livePosePublicationAgeRejects{ 0 };
			std::uint64_t livePoseReplayMissingPoseRejects{ 0 };
			std::uint64_t currentPresentationPoseReplaySamples{ 0 };
			std::uint64_t framePresentationReturns{ 0 };
			std::uint64_t framePresentationSamples{ 0 };
			std::uint64_t currentPresentationPoseReplayRejects{ 0 };
			std::uint64_t temporalHistoryRejectRequests{ 0 };
			std::uint64_t temporalHistoryRejectWrites{ 0 };
			std::uint64_t loweredTemporalHistoryRejectRequests{ 0 };
			std::uint64_t handLoweredYieldsToWall{ 0 };
			std::uint64_t wallPublicationHolds{ 0 };
			std::uint64_t wallPublicationHoldsWallIneligible{ 0 };
			std::uint64_t wallPublicationHoldsAmbiguous{ 0 };
			std::uint64_t wallPublicationHoldsHandHysteresis{ 0 };
			std::uint64_t interleaveHandSources{ 0 };
			std::uint64_t interleaveWallSources{ 0 };
			std::uint64_t interleaveHandGrantHolds{ 0 };
			// Why the hand channel is not being scheduled. The owner reported the
			// hand mirror "off again" on 2026-09-15 while every existing counter
			// was frozen: attempts, refresh-cap skips and the lowered-yield count
			// all stop together when the fence returns anything but kHand, so none
			// of them can name the reason. These four inputs are what the decision
			// is actually made of, sampled on the source that produced the outcome.
			std::uint64_t fenceHandSelections{ 0 };
			std::uint64_t fenceWallSelections{ 0 };
			std::uint64_t fenceDarkSelections{ 0 };
			std::uint64_t fenceInvalidSelections{ 0 };
			std::uint32_t fenceLastInputs{ 0 };  // bit 0 priorFrameVisible, 1 setting,
			                                     // 2 handEligible, 3 wallRecentlyEligible,
			                                     // 4 raised, 5 exactEquippedHand
			int lastReplayDecision{ -1 };
			std::uint64_t lastGoodInvalidations{ 0 };
			MirrorPaneRenderer::DrawStatus lastDirectDrawStatus{
				MirrorPaneRenderer::DrawStatus::kNoPublishedFrame };
			std::uint64_t firstReturnChecks{ 0 };
			std::uint64_t firstReturnPasses{ 0 };
			std::uint64_t firstReturnCandidateRejects{ 0 };
			std::uint64_t firstReturnTargetRejects{ 0 };
			std::uint64_t firstReturnMainViewFrameRejects{ 0 };
			std::uint64_t firstReturnGraphicsFrameRejects{ 0 };
			std::uint64_t firstReturnReceiptRejects{ 0 };
			std::uint64_t firstReturnPassRejects{ 0 };
			std::uint64_t firstReturnDeviceRejects{ 0 };
			std::uint64_t firstReturnContextRejects{ 0 };
			std::uint64_t firstReturnResourcesUnchecked{ 0 };
			std::uint64_t firstReturnResourceRejects{ 0 };
			std::uint64_t firstReturnOwnerRejects{ 0 };
			std::uint64_t firstReturnSurfaceRejects{ 0 };
			std::uint64_t firstReturnViewRejects{ 0 };
			std::uint64_t firstReturnProjectionRejects{ 0 };
			std::uint64_t firstReturnViewProjectionRejects{ 0 };
			std::uint64_t firstReturnOriginRejects{ 0 };
			std::uint64_t firstReturnViewportRejects{ 0 };
			std::uint64_t firstReturnStableGenerationRejects{ 0 };
			std::uint64_t firstReturnGeometryRejects{ 0 };
			std::uint64_t firstReturnPresentationInvalidRejects{ 0 };
			std::uint64_t firstReturnUnclassifiedRejects{ 0 };
			std::uint64_t firstReturnDiagnosticDivergences{ 0 };
			FirstReturnRejectMask lastFirstReturnRejectMask{ 0 };
			FirstReturnRejectMask lastFirstReturnDriftMask{ 0 };
			std::uint64_t secondReturnChecks{ 0 };
			std::uint64_t secondReturnPasses{ 0 };
			std::uint64_t secondReturnDiagnosticDivergences{ 0 };
			SecondReturnDifferenceMask lastSecondReturnRejectMask{ 0 };
			SecondReturnDifferenceMask lastSecondReturnDriftMask{ 0 };
			std::uint32_t lastReturnCount{ 0 };
			CohortRuntimeStatus lastStatus{ CohortRuntimeStatus::kNone };
		};

		enum class HandAffineHistoryReject : std::uint8_t
		{
			kEligible,
			kMissing,
			kReceipt,
			kIdentity,
			kViewport,
			kCaptureSequence
		};

		/**
		 * Hand-only history admitted by one completed outer-final presentation.
		 *
		 * MirrorPaneRenderer::MotionHistory intentionally stays a renderer value.
		 * This wrapper binds it to the exact hand receipt, live pane identity and
		 * viewport which authorized that presentation; an early exact-pane draw can
		 * therefore never become temporal history for the enclosing final writer.
		 */
		struct HandAffineMotionHistory
		{
			MirrorPaneRenderer::MotionHistory renderer{};
			ReceiptPolicy::FrameReceipt receipt{};
			Bridge::MovingSurfaceSample surface{};
			D3D11_VIEWPORT viewport{};
			bool valid{ false };
		};

		enum class HandAffineHistoryResetReason : std::uint8_t
		{
			kNoFinalDraw,
			kLoweredPresentation,
			kNoncurrentPublication,
			kMotionTargetUnavailableOrInvalid,
			kHistoryDrift,
			kDrawFailure,
			kLifecycle
		};

		void RecordFirstReturnValidation(
			CohortDiagnostics& diagnostics,
			const FirstReturnValidationEvidence& evidence,
			const bool accepted) noexcept
		{
			const auto differences = ClassifyFirstReturnRejects(evidence);
			const auto rejects = differences & ~kFirstReturnDriftMask;
			const auto drifts = differences & kFirstReturnDriftMask;
			++diagnostics.firstReturnChecks;
			diagnostics.lastFirstReturnRejectMask = rejects;
			diagnostics.lastFirstReturnDriftMask = drifts;
			if (accepted)
				++diagnostics.firstReturnPasses;
			if (accepted != (rejects == 0))
				++diagnostics.firstReturnDiagnosticDivergences;
			if (!accepted && rejects == 0)
				++diagnostics.firstReturnUnclassifiedRejects;

			if (HasFirstReturnReject(
					differences, FirstReturnReject::kCandidateUnavailable)) {
				++diagnostics.firstReturnCandidateRejects;
			}
			if (HasFirstReturnReject(differences, FirstReturnReject::kCurrentTarget))
				++diagnostics.firstReturnTargetRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kMainViewFrame))
				++diagnostics.firstReturnMainViewFrameRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kGraphicsFrame))
				++diagnostics.firstReturnGraphicsFrameRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kReceipt))
				++diagnostics.firstReturnReceiptRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kExactPass))
				++diagnostics.firstReturnPassRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kDevice))
				++diagnostics.firstReturnDeviceRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kContext))
				++diagnostics.firstReturnContextRejects;
			if (HasFirstReturnReject(
					differences, FirstReturnReject::kResourcesUnchecked)) {
				++diagnostics.firstReturnResourcesUnchecked;
			}
			if (HasFirstReturnReject(differences, FirstReturnReject::kResources))
				++diagnostics.firstReturnResourceRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kOwner))
				++diagnostics.firstReturnOwnerRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kSurface))
				++diagnostics.firstReturnSurfaceRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kView))
				++diagnostics.firstReturnViewRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kProjection))
				++diagnostics.firstReturnProjectionRejects;
			if (HasFirstReturnReject(
					differences, FirstReturnReject::kViewProjection)) {
				++diagnostics.firstReturnViewProjectionRejects;
			}
			if (HasFirstReturnReject(differences, FirstReturnReject::kOrigin))
				++diagnostics.firstReturnOriginRejects;
			if (HasFirstReturnReject(differences, FirstReturnReject::kViewport))
				++diagnostics.firstReturnViewportRejects;
			if (HasFirstReturnReject(
					differences, FirstReturnReject::kStableGeneration)) {
				++diagnostics.firstReturnStableGenerationRejects;
			}
			if (HasFirstReturnReject(differences, FirstReturnReject::kGeometry))
				++diagnostics.firstReturnGeometryRejects;
			if (HasFirstReturnReject(
					differences,
					FirstReturnReject::kReturnedPresentationInvalid)) {
				++diagnostics.firstReturnPresentationInvalidRejects;
			}
		}

		void RecordSecondReturnValidation(
			CohortDiagnostics& diagnostics,
			const Cohort::ValueIdentityEquality& equality,
			const bool accepted) noexcept
		{
			const auto differences = ClassifySecondReturnDifferences(equality);
			const auto rejects = differences & ~kSecondReturnDriftMask;
			const auto drifts = differences & kSecondReturnDriftMask;
			++diagnostics.secondReturnChecks;
			diagnostics.lastSecondReturnRejectMask = rejects;
			diagnostics.lastSecondReturnDriftMask = drifts;
			if (accepted)
				++diagnostics.secondReturnPasses;
			if (accepted != (rejects == 0))
				++diagnostics.secondReturnDiagnosticDivergences;
		}

		struct RuntimeState
		{
			std::optional<StorePolicy::Store> store{};
			std::optional<Reservation::Coordinator> coordinator{};
			StorePolicy::Store::PhysicalTargets targets{};
			Reservation::FenceReservation pendingReservation{};
			std::uint64_t pendingGrantedOwnerLease{ 0 };
			StorePolicy::PublicationSnapshot currentPublication{};
			StorePolicy::RetirementReceipt heldPredecessorRetirement{};
			std::uint64_t heldCleanMissExpirySource{ 0 };
			std::uint64_t heldCleanMissValidatedSource{ 0 };
			std::uint64_t lastPresentedPublicationToken{ 0 };
			std::array<NativeSlot, StorePolicy::kPhysicalRoleCapacity> nativeSlots{};
			VisibilityState visibility{};
			MirrorPaneRenderer::Renderer renderer{};
			HandAffineMotionHistory affineMotionHistory{};
			struct StableHandPresentation
			{
				DirectX::XMFLOAT4 centerClip{};
				DirectX::XMFLOAT4 tangentClipDelta{};
				DirectX::XMFLOAT4 bitangentClipDelta{};
				Bridge::MirrorOwnerIdentity owner{};
				D3D11_VIEWPORT viewport{};
				bool valid{ false };
			} stablePresentation{};
			struct LastGoodHandPresentation
			{
				MirrorPaneRenderer::PublishedFrame frame{};
				StorePolicy::PublicationSnapshot sourcePublication{};
				Bridge::MovingSurfaceSample liveSurface{};
				MirrorPaneRenderer::MainView mainView{};
				D3D11_VIEWPORT viewport{};
				LiveReturn::LiveDeliveryTargetIdentity target{};
				MirrorPaneDelivery::RetainedMainTargetHandle retainedTarget{};
				std::uintptr_t deviceIdentity{ 0 };
				// Optical mode of the committed draw (raised portrait window or
				// lowered physical reflection).  Replay is confined to one mode.
				HandMirrorLastGoodPresentationPolicy::PresentationMode
					presentationMode{
						HandMirrorLastGoodPresentationPolicy::PresentationMode::
							kUnknown };
				// Main-world source sequence of the returned surface this frame was
				// committed against; bounds replay age while lowered.
				std::uint64_t committedSourceSequence{ 0 };
				bool valid{ false };
			} lastGoodPresentation{};
			Microsoft::WRL::ComPtr<ID3D11Texture2D>
				lastGoodReplayTexture{};
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
				lastGoodReplaySRV{};
			std::uintptr_t lastGoodReplayDeviceIdentity{ 0 };
			// Token zero cannot serve as a typed wall-retirement receipt.  Seed one
			// closed/dark epoch; the first real hand lease is therefore token 2.
			std::uint64_t ownerLeaseCursor{ 1 };
			std::uint64_t loadGeneration{ 1 };
			std::uint64_t deliveryFrame{ 0 };
			std::uint64_t inventoryResumeFenceSource{ 0 };
			std::optional<Bridge::OwnerGrantInput> stagedWallRetirementGrant{};
			std::uint64_t stagedWallRetirementSource{ 0 };
			Bridge::TokenIssuerState wallRetirementTokens{};
			std::uint32_t activeInternalReaders{ 0 };
			CohortDiagnostics diagnostics{};
			DWORD renderThreadID{ 0 };
			bool targetsInitialized{ false };
			bool terminalRendererScrubPending{ false };
		};

		RuntimeState& State() noexcept
		{
			static stl::no_destructor<RuntimeState> state{};
			return state.get();
		}

		void ResetHandAffineHistory(
			RuntimeState& state,
			HandAffineHistoryResetReason reason) noexcept;
		[[nodiscard]] MirrorPaneRenderer::PaneTransform MakePaneTransform(
			const Bridge::MovingSurfaceSample& surface,
			const Bridge::MirrorOwnerIdentity& owner) noexcept;

		struct StagedFirstPersonDelivery
		{
			Bridge::MovingSurfaceSample returnedSurface{};
			MirrorPaneDelivery::CurrentMainDrawState main{};
			ReceiptPolicy::FrameReceipt receipt{};
			std::uintptr_t deviceIdentity{ 0 };
			std::uintptr_t contextIdentity{ 0 };
			std::uintptr_t colorResourceIdentity{ 0 };
			std::uintptr_t depthResourceIdentity{ 0 };
			MirrorPaneDelivery::RetainedMainTargetHandle retainedMainTarget{};
			bool graphIndicatesRaisedPresentation{ false };
			bool valid{ false };
		};

		struct FirstPersonTLS
		{
			StagedFirstPersonDelivery staged{};
			// Same-call frame raster, independent of hidden-3P capture freshness.
			// Never combine a late world-camera restore with the first-person pose.
			StagedFirstPersonDelivery framePresentation{};
			// Freeze the complete outer-call identity before Skyrim enters
			// RenderFirstPersonView. A zero-pane-callback frame cannot prove or acquire
			// this target only after the native call has already returned.
			MirrorPaneDelivery::RetainedMainTargetHandle outerTargetAtEntry{};
			ReceiptPolicy::FrameReceipt outerReceiptAtEntry{};
			Bridge::MovingSurfaceSample outerSurfaceAtEntry{};
			Bridge::MovingSurfaceSample outerSurfaceAtReturn{};
			Bridge::PrivateTargetIdentity outerCaptureTargetAtEntry{};
			LiveReturn::LiveDeliveryTargetIdentity outerLiveTargetAtEntry{};
			MirrorPaneDelivery::RetainedMainTargetHandle activeGenericTarget{};
			Cohort::CohortState cohort{};
			std::uint32_t exactNormalReturnCount{ 0 };
			// Stamped only after an exact pane return has retained its actual-OM color
			// target while the owning primary wrapper call is current.
			std::uint64_t postResolveStageToken{ 0 };
			std::uint64_t postResolvePresentedToken{ 0 };
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
			std::uint64_t postResolvePresentedCaptureSequence{ 0 };
#endif
			ReceiptPolicy::FrameReceipt postResolveExpectedReceipt{};
			Bridge::PrivateTargetIdentity postResolveExpectedCaptureTarget{};
			std::uintptr_t postResolveExpectedDeviceIdentity{ 0 };
			std::uintptr_t postResolveExpectedContextIdentity{ 0 };
			std::uint32_t postResolveExpectedReturnCount{ 0 };
			bool postResolveExpectedInvocationValid{ false };
			bool exactVisibilityObserved{ false };
			// The output target is independent evidence.  A missing pane candidate is
			// precisely the case in which last-good continuity needs this retained,
			// entry-to-return revalidated destination.
			bool outerTargetEntryValid{ false };
			bool outerEntryValid{ false };
			bool outerSurfaceReturnValid{ false };
			bool outerSurfaceReturnObserverExplicitlyAbsent{ false };
			bool active{ false };
			bool returnedNormally{ false };
			bool lifecycleTransitionSuppressed{ false };
		};
		thread_local FirstPersonTLS g_firstPerson{};

		struct PostResolveInvocationTLS
		{
			ReceiptPolicy::FrameReceipt entryReceipt{};
			Bridge::PrivateTargetIdentity entryCaptureTarget{};
			std::uintptr_t cameraIdentity{ 0 };
			std::uintptr_t accumulatorIdentity{ 0 };
			std::uintptr_t deviceIdentity{ 0 };
			std::uintptr_t contextIdentity{ 0 };
			std::uint64_t token{ 0 };
			std::uint32_t entryReturnCount{ 0 };
			std::uint32_t flags{ 0 };
			std::uint32_t nativeDepth{ 0 };
			bool foreignExactReturnObserved{ false };
			bool active{ false };
		};
		thread_local PostResolveInvocationTLS g_postResolveInvocation{};
		thread_local std::uint64_t g_postResolveTokenCursor{ 0 };

		[[nodiscard]] constexpr bool IsExactSuccessor(
			const std::uint64_t predecessor,
			const std::uint64_t successor) noexcept
		{
			return predecessor != 0 &&
				predecessor != (std::numeric_limits<std::uint64_t>::max)() &&
				predecessor + 1 == successor;
		}

		[[nodiscard]] bool PublicationMatchesVisibleSurface(
			const StorePolicy::PublicationSnapshot& publication,
			const Bridge::MovingSurfaceSample& surface) noexcept
		{
			return StorePolicy::IsExactInternalHandPublication(publication) &&
				Bridge::IsValidMovingSurfaceSample(surface) &&
				publication.publication.attempt.surface.sourceSequence ==
					surface.sourceSequence &&
				publication.publication.attempt.owner.kind ==
					Bridge::OwnerKind::kHand &&
				publication.publication.attempt.owner.hand == surface.owner &&
				publication.publication.attempt.surface.pose.stableGeneration ==
					surface.pose.stableGeneration &&
				publication.publication.attempt.surface.pose.observedPaneSubtree ==
					surface.pose.observedPaneSubtree &&
				publication.publication.attempt.surface.pose.authoredSurface ==
					surface.pose.authoredSurface;
		}

		[[nodiscard]] bool HeldSourceAdvancedPredecessorValid(
			const RuntimeState& state) noexcept
		{
			const auto& retirement = state.heldPredecessorRetirement;
			if (!state.store || !state.targetsInitialized ||
				!StorePolicy::IsValidRetirementReceipt(retirement) ||
				retirement.cause != StorePolicy::RetirementCause::kSourceAdvanced ||
				retirement.retired.publication.attempt.owner.kind !=
					Bridge::OwnerKind::kHand ||
				retirement.retired.physicalRole >= state.nativeSlots.size()) {
				return false;
			}
			const auto& slot = state.nativeSlots[retirement.retired.physicalRole];
			return slot.valid && StorePolicy::SamePublicationSnapshot(
				slot.publication, retirement.retired);
		}

		[[nodiscard]] bool CleanMissContinuityHoldValid(
			const RuntimeState& state) noexcept
		{
			const auto& retirement = state.heldPredecessorRetirement;
			const auto capturedSource = retirement.retired.publication.attempt.surface
				.sourceSequence;
			return CleanMissContinuityEffective() &&
			       state.heldCleanMissExpirySource != 0 &&
			       HeldSourceAdvancedPredecessorValid(state) &&
			       state.heldCleanMissExpirySource > capturedSource &&
			       state.heldCleanMissExpirySource - capturedSource <=
				       Presentation::kMaximumConsecutiveCleanMissSources;
		}

		[[nodiscard]] bool CleanMissContinuityMatchesVisibleSurface(
			const RuntimeState& state,
			const Bridge::MovingSurfaceSample& surface) noexcept
		{
			if (!CleanMissContinuityHoldValid(state) ||
				!Bridge::IsValidMovingSurfaceSample(surface) ||
				surface.sourceSequence != state.heldCleanMissExpirySource) {
				return false;
			}
			// A clean capture miss can leave the fence scheduler dark even though the
			// enclosing first-person return has just revalidated the exact equipped clone
			// and current pane. The live surface below still has to match the complete
			// hand owner, generation, clone subtree, and authored aperture on every
			// bounded continuation source before the retained image may draw.
			const auto& oldSurface = state.heldPredecessorRetirement.retired
				.publication.attempt.surface;
			return oldSurface.owner == surface.owner &&
			       oldSurface.pose.stableGeneration ==
				       surface.pose.stableGeneration &&
			       oldSurface.pose.observedPaneSubtree ==
				       surface.pose.observedPaneSubtree &&
			       oldSurface.pose.authoredSurface == surface.pose.authoredSurface;
		}

		[[nodiscard]] bool PublicationMatchesDeliverySurface(
			const RuntimeState& state,
			const StorePolicy::PublicationSnapshot& publication,
			const Bridge::MovingSurfaceSample& surface) noexcept
		{
			if (PublicationMatchesVisibleSurface(publication, surface))
				return true;
			return CleanMissContinuityMatchesVisibleSurface(state, surface) &&
			       StorePolicy::SamePublicationSnapshot(
				       publication,
				       state.heldPredecessorRetirement.retired);
		}

		[[nodiscard]] bool CleanMissContinuityTargetDisjoint(
			const RuntimeState& state,
			const StorePolicy::PublicationSnapshot& publication,
			const Bridge::MovingSurfaceSample& surface,
			const Bridge::PrivateTargetIdentity& currentMainTarget) noexcept
		{
			if (!CleanMissContinuityMatchesVisibleSurface(state, surface) ||
				!StorePolicy::SamePublicationSnapshot(
					publication, state.heldPredecessorRetirement.retired)) {
				return true;
			}
			return Bridge::ArePrivateTargetsPhysicallyDisjoint(
				publication.publication.attempt.privateTarget,
				currentMainTarget);
		}

		[[nodiscard]] bool HeldPredecessorMatchesStaged(
			const RuntimeState& state,
			const StagedFirstPersonDelivery& staged) noexcept
		{
			const auto& retirement = state.heldPredecessorRetirement;
			if (!StorePolicy::IsValidRetirementReceipt(retirement) ||
				retirement.cause != StorePolicy::RetirementCause::kSourceAdvanced ||
				!staged.valid || !staged.retainedMainTarget ||
				!PublicationMatchesVisibleSurface(
					retirement.retired, staged.returnedSurface) ||
				retirement.retired.physicalRole >= state.nativeSlots.size()) {
				return false;
			}
			const auto& slot = state.nativeSlots[retirement.retired.physicalRole];
			return slot.valid &&
				StorePolicy::SamePublicationSnapshot(
					slot.publication, retirement.retired);
		}

		[[nodiscard]] bool TryPromoteOrderingPredecessorToCleanMiss(
			RuntimeState& state,
			const StagedFirstPersonDelivery& staged,
			const bool orderingPredecessorWasDrawn) noexcept
		{
			if (!orderingPredecessorWasDrawn || !CleanMissContinuityEffective() ||
				state.heldCleanMissExpirySource != 0 ||
				state.currentPublication.valid || !state.store ||
				!HeldPredecessorMatchesStaged(state, staged)) {
				return false;
			}
			const auto& retirement = state.heldPredecessorRetirement;
			const auto capturedSource = retirement.retired.publication.attempt.surface
				.sourceSequence;
			const auto audit = state.store->Inspect();
			if (audit.phase != StorePolicy::StorePhase::kReady ||
				audit.fault != StorePolicy::FaultReason::kNone ||
				audit.currentPublicationCount != 0 || audit.activeAttemptCount != 0 ||
				audit.pendingRetirementCount != 1 ||
				staged.receipt.sourceSequence != capturedSource ||
				!IsExactSuccessor(capturedSource, audit.sourceSequence)) {
				return false;
			}
			state.heldCleanMissExpirySource = audit.sourceSequence;
			state.heldCleanMissValidatedSource = 0;
			++state.diagnostics.orderingPredecessorPromotions;
			return true;
		}

		[[nodiscard]] bool ExactSuccessorDeliveryPending(
			const RuntimeState& state,
			const ReceiptPolicy::FrameReceipt& receipt) noexcept
		{
			const auto& staged = g_firstPerson.staged;
			return HeldPredecessorMatchesStaged(state, staged) &&
				IsExactSuccessor(staged.receipt.sourceSequence, receipt.sourceSequence) &&
				IsExactSuccessor(staged.receipt.mainViewFrame, receipt.mainViewFrame) &&
				IsExactSuccessor(staged.receipt.graphicsFrame, receipt.graphicsFrame);
		}

		[[nodiscard]] bool FinalFirstPersonDeliveryPending(
			const ReceiptPolicy::FrameReceipt& receipt) noexcept
		{
			if (!ReceiptPolicy::IsValidFrameReceipt(receipt))
				return false;
			if (!g_firstPerson.active)
				return true;
			if (g_firstPerson.returnedNormally || g_firstPerson.activeGenericTarget)
				return false;
			const bool beforeAnyPaneReturn =
				g_firstPerson.exactNormalReturnCount == 0 &&
				!g_firstPerson.exactVisibilityObserved &&
				!g_firstPerson.staged.valid &&
				!g_firstPerson.staged.retainedMainTarget;
			if (beforeAnyPaneReturn)
				return true;

			// RenderPlayerView may schedule the deferred capture after an exact pane
			// return while walking. The retained same-receipt target proves that the
			// enclosing first-person call still has one final presentation seam.
			const auto& staged = g_firstPerson.staged;
			const bool exactCurrentReceipt =
				g_firstPerson.exactNormalReturnCount != 0 &&
				g_firstPerson.exactVisibilityObserved && staged.valid &&
				staged.retainedMainTarget && staged.receipt == receipt &&
				Bridge::IsValidMovingSurfaceSample(staged.returnedSurface) &&
				staged.returnedSurface.sourceSequence == receipt.sourceSequence &&
				staged.returnedSurface.pose.mainViewFrame == receipt.mainViewFrame;
			return exactCurrentReceipt ||
				(g_firstPerson.exactNormalReturnCount != 0 &&
					g_firstPerson.exactVisibilityObserved &&
					ExactSuccessorDeliveryPending(State(), receipt));
		}

		struct CandidateCopy
		{
			HandMirrorApprovedContentReadOnlyObserver::RuntimeCandidateSnapshot
				snapshot{};
			bool copied{ false };
		};

		struct EquippedPresentationIdentityCopy
		{
			HandMirrorApprovedContentReadOnlyObserver::
				RuntimeEquippedPresentationIdentity identity{};
			bool copied{ false };
		};

		[[nodiscard]] bool CopyCandidate(
			const HandMirrorApprovedContentReadOnlyObserver::RuntimeCandidateSnapshot&
				snapshot,
			const HandMirrorApprovedContentReadOnlyObserver::RuntimeBorrowedScene&,
			void* opaque) noexcept
		{
			auto* output = static_cast<CandidateCopy*>(opaque);
			if (!output)
				return false;
			output->snapshot = snapshot;
			output->copied = true;
			return true;
		}

		[[nodiscard]] bool CopyEquippedPresentationIdentity(
			const HandMirrorApprovedContentReadOnlyObserver::
				RuntimeEquippedPresentationIdentity& identity,
			void* opaque) noexcept
		{
			auto* output = static_cast<EquippedPresentationIdentityCopy*>(opaque);
			if (!output || !identity.valid)
				return false;
			output->identity = identity;
			output->copied = true;
			return true;
		}

		[[nodiscard]] bool ReleaseShaderViewRawSEH(
			ID3D11ShaderResourceView* view) noexcept
		{
			if (!view)
				return true;
			bool released = false;
			__try {
				view->Release();
				released = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				released = false;
			}
			return released;
		}

		[[nodiscard]] bool ClearNativeRole(
			RuntimeState& state, const std::uint32_t role) noexcept
		{
			if (role >= state.nativeSlots.size())
				return false;
			auto* color = state.nativeSlots[role].frame.colorSRV.Detach();
			auto* depth = state.nativeSlots[role].frame.depthSRV.Detach();
			state.nativeSlots[role].frame = {};
			state.nativeSlots[role].publication = {};
			state.nativeSlots[role].valid = false;
			if (state.currentPublication.valid &&
				state.currentPublication.physicalRole == role) {
				state.currentPublication = {};
				state.lastPresentedPublicationToken = 0;
			}
			const bool depthReleased = ReleaseShaderViewRawSEH(depth);
			const bool colorReleased = ReleaseShaderViewRawSEH(color);
			return depthReleased && colorReleased;
		}

		[[nodiscard]] bool BurnUnusedOwnerGrant(
			RuntimeState& state,
			const std::uint64_t ownerLeaseSequence) noexcept
		{
			if (ownerLeaseSequence == 0 || state.ownerLeaseCursor ==
				(std::numeric_limits<std::uint64_t>::max)() ||
				ownerLeaseSequence != state.ownerLeaseCursor + 1) {
				return false;
			}
			const auto burnPolicy = HeldSourceAdvancedPredecessorValid(state) ?
				StorePolicy::OwnerLeaseBurnPolicy::
					kAllowSourceAdvancedPendingRetirement :
				StorePolicy::OwnerLeaseBurnPolicy::kRequireNoPendingRetirement;
			if (state.targetsInitialized &&
				(!state.store ||
					!state.store->BurnUnusedOwnerLease(
						ownerLeaseSequence, burnPolicy))) {
				return false;
			}
			state.ownerLeaseCursor = ownerLeaseSequence;
			return true;
		}

		[[nodiscard]] bool ReleaseRetirement(
			RuntimeState& state,
			const StorePolicy::RetirementReceipt& receipt) noexcept
		{
			if (receipt == StorePolicy::RetirementReceipt{})
				return true;
			if (!state.store || !StorePolicy::IsValidRetirementReceipt(receipt) ||
				receipt.retired.physicalRole >= state.nativeSlots.size()) {
				return false;
			}
			const bool everyInternalReaderDrained =
				state.activeInternalReaders == 0;
			const bool soleOwnedImmediateContextUnbound =
				SecondView::IsHandPrivateTargetUnbound(
					receipt.retired.publication.attempt.privateTarget);
			if (!everyInternalReaderDrained || !soleOwnedImmediateContextUnbound) {
				return false;
			}
			const auto status = state.store->ReleaseRetired({
				.receipt = receipt,
				.everyInternalReaderDrained = everyInternalReaderDrained,
				// Only the engine immediate context is owned/used; private hand views
				// never leave this module or enter a deferred/foreign context.
				.targetUnboundFromEveryContext =
					soleOwnedImmediateContextUnbound });
			if (status != StorePolicy::ReleaseStatus::kReleased)
				return false;
			return ClearNativeRole(state, receipt.retired.physicalRole);
		}

		[[nodiscard]] bool ReleaseHeldPredecessor(RuntimeState& state) noexcept
		{
			const auto retirement = state.heldPredecessorRetirement;
			state.heldPredecessorRetirement = {};
			state.heldCleanMissExpirySource = 0;
			state.heldCleanMissValidatedSource = 0;
			return retirement == StorePolicy::RetirementReceipt{} ||
				ReleaseRetirement(state, retirement);
		}

		[[nodiscard]] bool ReleaseHeldPredecessorForLifecycle(
			RuntimeState& state) noexcept
		{
			// Lifecycle invalidation owns both kinds of held source-advance receipt:
			// a clean-miss hold has a nonzero bounded source, while the ordering hold
			// deliberately uses zero. Leaving the latter pinned makes the next
			// AdvanceStoreSource reject and incorrectly process-fault the hand route.
			return ReleaseHeldPredecessor(state);
		}

		[[nodiscard]] bool CanHoldSourceAdvancedPredecessor(
			const RuntimeState& state,
			const std::uint64_t nextSourceSequence,
			const StorePolicy::RetirementReceipt& retirement) noexcept
		{
			if (state.heldPredecessorRetirement !=
					StorePolicy::RetirementReceipt{} ||
				state.activeInternalReaders != 0 || !g_firstPerson.active ||
				g_firstPerson.returnedNormally || g_firstPerson.activeGenericTarget ||
				!StorePolicy::IsValidRetirementReceipt(retirement) ||
				retirement.cause != StorePolicy::RetirementCause::kSourceAdvanced ||
				!StorePolicy::SamePublicationSnapshot(
					retirement.retired, state.currentPublication) ||
				!PublicationMatchesVisibleSurface(
					retirement.retired, g_firstPerson.staged.returnedSurface) ||
				!IsExactSuccessor(
					g_firstPerson.staged.receipt.sourceSequence,
					nextSourceSequence) ||
				retirement.retired.physicalRole >= state.nativeSlots.size() ||
				!SecondView::IsHandPrivateTargetUnbound(
					retirement.retired.publication.attempt.privateTarget)) {
				return false;
			}
			const auto& slot = state.nativeSlots[retirement.retired.physicalRole];
			return slot.valid &&
				StorePolicy::SamePublicationSnapshot(
					slot.publication, retirement.retired);
		}

		[[nodiscard]] bool CanHoldCleanMissContinuityPredecessor(
			const RuntimeState& state,
			const std::uint64_t nextSourceSequence,
			const StorePolicy::RetirementReceipt& retirement) noexcept
		{
			if (!CleanMissContinuityEffective() ||
				state.heldPredecessorRetirement !=
					StorePolicy::RetirementReceipt{} ||
				state.heldCleanMissExpirySource != 0 ||
				state.activeInternalReaders != 0 ||
				!StorePolicy::IsValidRetirementReceipt(retirement) ||
				retirement.cause != StorePolicy::RetirementCause::kSourceAdvanced ||
				!StorePolicy::SamePublicationSnapshot(
					retirement.retired, state.currentPublication) ||
				retirement.retired.publication.nativePublicationToken !=
					state.lastPresentedPublicationToken ||
				retirement.retired.publication.attempt.owner.kind !=
					Bridge::OwnerKind::kHand ||
				state.visibility.currentOwner !=
					Content::MirrorOwnerSelection::kHand ||
				state.visibility.currentHandOwner !=
					retirement.retired.publication.attempt.owner.hand ||
				!IsExactSuccessor(
					retirement.retired.publication.attempt.surface.sourceSequence,
					nextSourceSequence) ||
				retirement.retired.physicalRole >= state.nativeSlots.size() ||
				!SecondView::IsHandPrivateTargetUnbound(
					retirement.retired.publication.attempt.privateTarget)) {
				return false;
			}
			const auto& slot = state.nativeSlots[retirement.retired.physicalRole];
			return slot.valid && StorePolicy::SamePublicationSnapshot(
				slot.publication, retirement.retired);
		}

		[[nodiscard]] StorePolicy::PublicationSnapshot
		DeliveryPublicationForSurface(
			const RuntimeState& state,
			const Bridge::MovingSurfaceSample& surface,
			const Bridge::PrivateTargetIdentity& currentMainTarget) noexcept
		{
			if (PublicationMatchesVisibleSurface(state.currentPublication, surface))
				return state.currentPublication;
			if (PublicationMatchesVisibleSurface(
					state.heldPredecessorRetirement.retired, surface)) {
				return state.heldPredecessorRetirement.retired;
			}
			if (CleanMissContinuityMatchesVisibleSurface(state, surface) &&
				Bridge::ArePrivateTargetsPhysicallyDisjoint(
					state.heldPredecessorRetirement.retired.publication.attempt
						.privateTarget,
					currentMainTarget)) {
				return state.heldPredecessorRetirement.retired;
			}
			return {};
		}

		[[nodiscard]] StorePolicy::PublicationSnapshot
		ZeroCallbackDeliveryPublicationForSurface(
			const RuntimeState& state,
			const Bridge::MovingSurfaceSample& surface,
			const Bridge::PrivateTargetIdentity& currentMainTarget) noexcept
		{
			// A fresh exact publication is always preferred.  A capture-gap frame may
			// use only the already-bounded clean-miss predecessor: it must still own the
			// same equipped clone/aperture on this source and its private target must be
			// physically disjoint from the frozen main-view target.
			if (PublicationMatchesVisibleSurface(state.currentPublication, surface))
				return state.currentPublication;
			if (!CleanMissContinuityMatchesVisibleSurface(state, surface) ||
				!StorePolicy::IsExactInternalHandPublication(
					state.heldPredecessorRetirement.retired) ||
				!Bridge::ArePrivateTargetsPhysicallyDisjoint(
					state.heldPredecessorRetirement.retired.publication.attempt
						.privateTarget,
					currentMainTarget)) {
				return {};
			}
			return state.heldPredecessorRetirement.retired;
		}

		[[nodiscard]] bool RetireCurrentPublicationDarkAfterOuterReturn(
			RuntimeState& state,
			const std::uint64_t sourceSequence,
			const std::uint8_t observedPaneNormalReturnCount) noexcept
		{
			if (!state.currentPublication.valid)
				return true;
			if (!state.store || sourceSequence == 0 ||
				state.currentPublication.publication.attempt.surface.sourceSequence !=
					sourceSequence) {
				return false;
			}
			StorePolicy::DarkRetirementResult retired{};
			const bool paneEnteredAndReturned =
				observedPaneNormalReturnCount != 0;
			const auto status = state.store->RetireDarkAfterOuterFirstPersonReturn({
				.currentPublication = state.currentPublication,
				.sourceSequence = sourceSequence,
				.consumer = Bridge::PublicationConsumer::kInternalHandPane,
				.observedPaneNormalReturnCount = observedPaneNormalReturnCount,
				.outerFirstPersonTLSActive = g_firstPerson.active,
				.outerFirstPersonReturnedNormally = true,
				.exactPaneSubmittedByCurrentMainView = paneEnteredAndReturned,
				.nativePaneDrawReturnedNormally = paneEnteredAndReturned }, retired);
			if (status != StorePolicy::DarkRetirementStatus::kRetiredDark ||
				!ReleaseRetirement(state, retired.retirement)) {
				return false;
			}
			state.currentPublication = {};
			state.lastPresentedPublicationToken = 0;
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kNoFinalDraw);
			return true;
		}

		std::atomic<std::uint32_t> g_storeAuditLogBudget{ 24 };
		// Hand-resolution reallocations adopted without losing the runtime.
		std::atomic<std::uint64_t> g_privateTargetRetargets{ 0 };

		// Bounded audit of the internal publication store and its coordinator at
		// the sites that used to fail silently (V146 run 17:06: the store went
		// quarantined with the runtime still enabled and the raised hand pane
		// stayed black while a placed mirror reflected).
		void LogStoreAuditNoexcept(
			const RuntimeState& state,
			const char* site,
			const int status) noexcept
		{
			auto budget = g_storeAuditLogBudget.load(std::memory_order_acquire);
			if (budget == 0 || !g_storeAuditLogBudget.compare_exchange_strong(
					budget, budget - 1, std::memory_order_acq_rel)) {
				return;
			}
			try {
				decltype(state.store->Inspect()) store{};
				if (state.store)
					store = state.store->Inspect();
				decltype(state.coordinator->Inspect()) coordinator{};
				if (state.coordinator)
					coordinator = state.coordinator->Inspect();
				logger::warn(
					"[RR][HandMirrorReflection][store-audit] site={} status={} store(phase/fault/source/current/active/retired)={}/{}/{}/{}/{}/{} coordinator(phase/fault/pending/active/lastSource)={}/{}/{}/{}/{} pending(source/lease)={}/{} leaseCursor={} owner={} targets={} faulted={}",
					site ? site : "?", status,
					static_cast<int>(store.phase), static_cast<int>(store.fault),
					store.sourceSequence, store.currentPublicationCount,
					store.activeAttemptCount, store.pendingRetirementCount,
					static_cast<int>(coordinator.phase),
					static_cast<int>(coordinator.fault),
					coordinator.pendingReservationCount,
					coordinator.activeTicketCount,
					coordinator.lastSourceSequence,
					state.pendingReservation.sourceSequence,
					state.pendingGrantedOwnerLease, state.ownerLeaseCursor,
					static_cast<int>(state.visibility.currentOwner),
					state.targetsInitialized,
					g_faulted.load(std::memory_order_relaxed));
			} catch (...) {
			}
		}

		[[nodiscard]] bool AdvanceStoreSource(
			RuntimeState& state,
			const std::uint64_t sourceSequence) noexcept
		{
			if (!state.store || !state.targetsInitialized)
				return true;
			const auto audit = state.store->Inspect();
			if (audit.phase != StorePolicy::StorePhase::kReady) {
				LogStoreAuditNoexcept(state, "advance-not-ready", 0);
				return false;
			}
			if (audit.sourceSequence == sourceSequence)
				return true;
			bool extendCleanMissContinuity = false;
			if (state.heldPredecessorRetirement !=
					StorePolicy::RetirementReceipt{}) {
				// V50 ordering predecessors remain strictly tied to the current outer
				// writer. A clean-miss predecessor may cross another source only after the
				// immediately preceding outer return revalidated the exact live identity.
				if (state.heldCleanMissExpirySource == 0 ||
					!CleanMissContinuityHoldValid(state) ||
					sourceSequence <= state.heldCleanMissExpirySource) {
					return false;
				}
				const auto capturedSource = state.heldPredecessorRetirement.retired
					.publication.attempt.surface.sourceSequence;
				extendCleanMissContinuity =
					!state.currentPublication.valid &&
					Presentation::CanExtendConsecutiveCleanMiss(
						capturedSource, state.heldCleanMissExpirySource,
						sourceSequence,
						state.heldCleanMissValidatedSource ==
							state.heldCleanMissExpirySource);
				if (!extendCleanMissContinuity) {
					if (!ReleaseHeldPredecessor(state))
						return false;
					++state.diagnostics.cleanMissContinuityExpiryReleases;
				}
			}
			if (sourceSequence <= audit.sourceSequence)
				return false;
			if (!state.coordinator)
				return false;
			const auto coordinatorAudit = state.coordinator->Inspect();
			if (coordinatorAudit.activeTicketCount != 0)
				return false;
			if (Reservation::IsValidFenceReservation(state.pendingReservation)) {
				if (coordinatorAudit.pendingReservationCount != 1 ||
					state.coordinator->CancelFenceReservation(
						*state.store, state.pendingReservation) !=
						Reservation::CancelReservationStatus::kCancelledDark) {
					return false;
				}
				state.pendingReservation = {};
				const auto abandonedLease = state.pendingGrantedOwnerLease;
				state.pendingGrantedOwnerLease = 0;
				if (!BurnUnusedOwnerGrant(state, abandonedLease))
					return false;
			} else if (coordinatorAudit.pendingReservationCount != 0 ||
				state.pendingGrantedOwnerLease != 0) {
				return false;
			}
			StorePolicy::InvalidationResult invalidation{};
			const auto status = state.store->OnSourceAdvanced(
				sourceSequence, invalidation);
			if (status == StorePolicy::InvalidationStatus::kQuarantined ||
				status == StorePolicy::InvalidationStatus::kUnavailable ||
				status == StorePolicy::InvalidationStatus::kInvalidEvidence ||
				status == StorePolicy::InvalidationStatus::kTokenExhausted) {
				return false;
			}
			if (extendCleanMissContinuity) {
				if (status != StorePolicy::InvalidationStatus::kNoState ||
					invalidation.retirement != StorePolicy::RetirementReceipt{} ||
					invalidation.activeAttemptAborted ||
					state.currentPublication.valid) {
					return false;
				}
				state.heldCleanMissExpirySource = sourceSequence;
				state.heldCleanMissValidatedSource = 0;
				return true;
			}
			if (CanHoldCleanMissContinuityPredecessor(
					state, sourceSequence, invalidation.retirement)) {
				state.heldPredecessorRetirement = invalidation.retirement;
				state.heldCleanMissExpirySource = sourceSequence;
				state.heldCleanMissValidatedSource = 0;
				state.currentPublication = {};
				state.lastPresentedPublicationToken = 0;
				++state.diagnostics.cleanMissContinuityArms;
				return true;
			}
			if (CanHoldSourceAdvancedPredecessor(
					state, sourceSequence, invalidation.retirement)) {
				state.heldPredecessorRetirement = invalidation.retirement;
				state.heldCleanMissExpirySource = 0;
				state.heldCleanMissValidatedSource = 0;
				state.currentPublication = {};
				state.lastPresentedPublicationToken = 0;
				return true;
			}
			return ReleaseRetirement(state, invalidation.retirement);
		}

		[[nodiscard]] bool EnsureRenderThread(RuntimeState& state) noexcept
		{
			const DWORD current = GetCurrentThreadId();
			if (state.renderThreadID == 0) {
				state.renderThreadID = current;
				return true;
			}
			return state.renderThreadID == current;
		}

		void RecordCurrentMainDrawStateStatus(
			CohortDiagnostics& diagnostics,
			const MirrorPaneDelivery::CurrentMainDrawStateStatus status) noexcept
		{
			switch (status) {
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::kReady:
				++diagnostics.mainQueryReady;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::kDeliveryFaulted:
				++diagnostics.mainQueryDeliveryFaulted;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::kPrivatePassActive:
				++diagnostics.mainQueryPrivatePassActive;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::kMainWorldActive:
				++diagnostics.mainQueryMainWorldActive;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kMainWorldFrameMissing:
				++diagnostics.mainQueryMainWorldFrameMissing;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kDeferredSnapshotInvalid:
				++diagnostics.mainQueryDeferredSnapshotInvalid;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kDeferredMainWorldFrameMismatch:
				++diagnostics.mainQueryDeferredFrameMismatch;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kDeferredGraphicsFrameMissing:
				++diagnostics.mainQueryDeferredGraphicsMissing;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kEngineStateUnavailable:
				++diagnostics.mainQueryEngineStateUnavailable;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kRefractionPassActive:
				++diagnostics.mainQueryRefractionPassActive;
				break;
			case MirrorPaneDelivery::CurrentMainDrawStateStatus::
				kGraphicsFrameMismatch:
				++diagnostics.mainQueryGraphicsFrameMismatch;
				break;
			}
		}

		[[nodiscard]] bool SameMatrixBits(
			const DirectX::XMFLOAT4X4& left,
			const DirectX::XMFLOAT4X4& right) noexcept
		{
			for (std::size_t row = 0; row < 4; ++row) {
				for (std::size_t column = 0; column < 4; ++column) {
					if (!Bridge::SameFloatBits(
							left.m[row][column], right.m[row][column])) {
						return false;
					}
				}
			}
			return true;
		}

		[[nodiscard]] bool IsFiniteMatrix(
			const DirectX::XMFLOAT4X4& matrix) noexcept
		{
			for (const auto& row : matrix.m) {
				for (const float value : row) {
					if (!std::isfinite(value))
						return false;
				}
			}
			return true;
		}

		[[nodiscard]] LiveReturn::FrozenCaptureTargetIdentity
		MakeFrozenCaptureTargetIdentity(
			const Bridge::PrivateTargetIdentity& target) noexcept
		{
			return {
				target.allocationGeneration,
				target.colorResourceToken,
				target.depthResourceToken
			};
		}

		[[nodiscard]] LiveReturn::LiveDeliveryTargetIdentity
		MakeLiveDeliveryTargetIdentity(
			const MirrorPaneDelivery::RetainedMainTargetHandle& target) noexcept
		{
			if (!target)
				return {};
			return {
				reinterpret_cast<std::uintptr_t>(target->device),
				reinterpret_cast<std::uintptr_t>(target->context),
				reinterpret_cast<std::uintptr_t>(target->colorRTV),
				reinterpret_cast<std::uintptr_t>(target->depthDSV),
				reinterpret_cast<std::uintptr_t>(target->colorResource),
				reinterpret_cast<std::uintptr_t>(target->depthResource)
			};
		}

		/**
		 * The equip sink wakes for every biped change, including an unrelated helmet.
		 * Preserve presentation only when the complete approved mirror graph is still
		 * byte-identical. Observer generations may advance across an unrelated equip
		 * wake or a transient unreadable sample, so structural pointer/form/asset
		 * identity is the continuity authority here. A real rebuilt root/clone still
		 * changes those exact pointer fields and remains a hard break.
		 */
		[[nodiscard]] bool SameEquippedPresentationOwner(
			const Bridge::MirrorOwnerIdentity& cached,
			const Content::HandMirrorOwnerIdentity& equipped) noexcept
		{
			if (cached.kind != Bridge::OwnerKind::kHand ||
				!Bridge::IsValidOwner(cached) ||
				!Content::IsValidHandMirrorOwnerIdentity(equipped)) {
				return false;
			}
			auto normalized = equipped;
			normalized.stableGeneration = cached.hand.stableGeneration;
			return normalized == cached.hand;
		}

		[[nodiscard]] bool SameLastGoodPresentationTarget(
			const LiveReturn::LiveDeliveryTargetIdentity& cached,
			const LiveReturn::LiveDeliveryTargetIdentity& current) noexcept
		{
			// Callers use this only with a staged authoritative target or an exact
			// entry-to-return revalidated target. An incidental outer-return OM sample
			// never reaches this comparison.
			return LiveReturn::IsValidLiveDeliveryTarget(cached) &&
				LiveReturn::IsValidLiveDeliveryTarget(current) &&
				cached == current;
		}

		[[nodiscard]] bool ReturnedPresentationComplete(
			const MirrorPaneDelivery::CurrentMainDrawState& main) noexcept
		{
			const auto& viewport = main.viewport;
			return main.valid && IsFiniteMatrix(main.view) &&
			       IsFiniteMatrix(main.projection) &&
			       IsFiniteMatrix(main.viewProjection) &&
			       std::isfinite(main.origin.x) && std::isfinite(main.origin.y) &&
			       std::isfinite(main.origin.z) &&
			       std::isfinite(viewport.TopLeftX) &&
			       std::isfinite(viewport.TopLeftY) &&
			       std::isfinite(viewport.Width) &&
			       std::isfinite(viewport.Height) &&
			       std::isfinite(viewport.MinDepth) &&
			       std::isfinite(viewport.MaxDepth) &&
			       viewport.TopLeftX >= 0.0F && viewport.TopLeftY >= 0.0F &&
			       viewport.Width > 0.0F && viewport.Height > 0.0F &&
			       viewport.MinDepth >= 0.0F && viewport.MaxDepth <= 1.0F &&
			       viewport.MinDepth <= viewport.MaxDepth;
		}

		[[nodiscard]] bool SameViewportBits(
			const D3D11_VIEWPORT& left,
			const D3D11_VIEWPORT& right) noexcept
		{
			return Bridge::SameFloatBits(left.TopLeftX, right.TopLeftX) &&
			       Bridge::SameFloatBits(left.TopLeftY, right.TopLeftY) &&
			       Bridge::SameFloatBits(left.Width, right.Width) &&
			       Bridge::SameFloatBits(left.Height, right.Height) &&
			       Bridge::SameFloatBits(left.MinDepth, right.MinDepth) &&
			       Bridge::SameFloatBits(left.MaxDepth, right.MaxDepth);
		}

#ifndef MIRRORS_OF_SKYRIM_STANDALONE
		[[nodiscard]] HandMirrorLateTargetSurvivalProbe::TargetSample
		MakeLateTargetSurvivalSample(
			const RuntimeState& state,
			const MirrorPaneDelivery::RetainedMainTargetHandle& retainedTarget,
			const D3D11_VIEWPORT& viewport,
			const ReceiptPolicy::FrameReceipt& receipt,
			const std::uint64_t captureSequence) noexcept
		{
			const auto& stable = state.stablePresentation;
			const bool exactTarget = retainedTarget && retainedTarget->device &&
				retainedTarget->context && retainedTarget->colorRTV &&
				retainedTarget->colorResource;
			const bool exactPresentation = stable.valid &&
				SameViewportBits(stable.viewport, viewport) &&
				std::isfinite(stable.centerClip.x) &&
				std::isfinite(stable.centerClip.y) &&
				std::isfinite(stable.centerClip.z) &&
				std::isfinite(stable.centerClip.w) && stable.centerClip.w > 1.0e-5F;
			return {
				.retainedTarget = retainedTarget,
				.viewport = viewport,
				.paneCenterClip = stable.centerClip,
				.sourceSequence = receipt.sourceSequence,
				.mainWorldFrame = receipt.mainViewFrame,
				.captureSequence = captureSequence,
				.graphicsFrame = receipt.graphicsFrame,
				.valid = exactTarget && exactPresentation &&
					ReceiptPolicy::IsValidFrameReceipt(receipt) &&
					captureSequence != 0 && captureSequence !=
						(std::numeric_limits<std::uint64_t>::max)()
			};
		}
#endif

		[[nodiscard]] bool ExactHandPaneIdentity(
			const Bridge::MovingSurfaceSample& left,
			const Bridge::MovingSurfaceSample& right) noexcept
		{
			return Bridge::IsValidMovingSurfaceSample(left) &&
			       Bridge::IsValidMovingSurfaceSample(right) &&
			       left.owner == right.owner &&
			       left.pose.stableGeneration == right.pose.stableGeneration &&
			       left.pose.observedPaneSubtree == right.pose.observedPaneSubtree &&
			       left.pose.authoredSurface == right.pose.authoredSurface;
		}

		[[nodiscard]] HandAffineHistoryReject ClassifyHandAffineHistory(
			const HandAffineMotionHistory& history,
			const ReceiptPolicy::FrameReceipt& currentReceipt,
			const Bridge::MovingSurfaceSample& currentSurface,
			const MirrorPaneRenderer::PaneTransform& currentPane,
			const D3D11_VIEWPORT& currentViewport,
			const std::uint64_t currentCaptureSequence) noexcept
		{
			if (!history.valid || !history.renderer.valid)
				return HandAffineHistoryReject::kMissing;
			if (!ReceiptPolicy::IsValidFrameReceipt(history.receipt) ||
				!ReceiptPolicy::IsValidFrameReceipt(currentReceipt) ||
				history.surface.sourceSequence != history.receipt.sourceSequence ||
				history.surface.pose.mainViewFrame != history.receipt.mainViewFrame ||
				currentSurface.sourceSequence != currentReceipt.sourceSequence ||
				currentSurface.pose.mainViewFrame != currentReceipt.mainViewFrame ||
				!IsExactSuccessor(
					history.receipt.sourceSequence, currentReceipt.sourceSequence) ||
				!IsExactSuccessor(
					history.receipt.mainViewFrame, currentReceipt.mainViewFrame) ||
				!IsExactSuccessor(
					static_cast<std::uint64_t>(history.receipt.graphicsFrame),
					static_cast<std::uint64_t>(currentReceipt.graphicsFrame)) ||
				history.renderer.deliveryFrame != history.receipt.graphicsFrame) {
				return HandAffineHistoryReject::kReceipt;
			}
			if (!ExactHandPaneIdentity(history.surface, currentSurface) ||
				history.renderer.pane.owner.kind != Bridge::OwnerKind::kHand ||
				currentPane.owner.kind != Bridge::OwnerKind::kHand ||
				history.renderer.pane.owner.hand != history.surface.owner ||
				currentPane.owner.hand != currentSurface.owner ||
				!Bridge::SameOwner(
					history.renderer.pane.owner, currentPane.owner)) {
				return HandAffineHistoryReject::kIdentity;
			}
			if (!SameViewportBits(history.viewport, currentViewport))
				return HandAffineHistoryReject::kViewport;
			if (history.renderer.captureSequence == 0 ||
				currentCaptureSequence == 0 ||
				history.renderer.captureSequence > currentCaptureSequence) {
				return HandAffineHistoryReject::kCaptureSequence;
			}
			return HandAffineHistoryReject::kEligible;
		}

		void RecordHandAffineHistoryClassification(
			CohortDiagnostics& diagnostics,
			const HandAffineHistoryReject result) noexcept
		{
			switch (result) {
			case HandAffineHistoryReject::kEligible:
				++diagnostics.affineMotionEligible;
				break;
			case HandAffineHistoryReject::kMissing:
				++diagnostics.affineHistoryMissingRejects;
				break;
			case HandAffineHistoryReject::kReceipt:
				++diagnostics.affineHistoryReceiptRejects;
				break;
			case HandAffineHistoryReject::kIdentity:
				++diagnostics.affineHistoryIdentityRejects;
				break;
			case HandAffineHistoryReject::kViewport:
				++diagnostics.affineHistoryViewportRejects;
				break;
			case HandAffineHistoryReject::kCaptureSequence:
				++diagnostics.affineHistoryCaptureRejects;
				break;
			}
		}

		void ResetHandAffineHistory(
			RuntimeState& state,
			const HandAffineHistoryResetReason reason) noexcept
		{
			if (!state.affineMotionHistory.valid)
				return;
			state.affineMotionHistory = {};
			++state.diagnostics.affineHistoryResets;
			switch (reason) {
			case HandAffineHistoryResetReason::kNoFinalDraw:
				++state.diagnostics.affineHistoryNoFinalDrawResets;
				break;
			case HandAffineHistoryResetReason::kLoweredPresentation:
				++state.diagnostics.affineHistoryLoweredResets;
				break;
			case HandAffineHistoryResetReason::kNoncurrentPublication:
				++state.diagnostics.affineHistoryNoncurrentPublicationResets;
				break;
			case HandAffineHistoryResetReason::kMotionTargetUnavailableOrInvalid:
				++state.diagnostics.affineHistoryMotionTargetResets;
				break;
			case HandAffineHistoryResetReason::kHistoryDrift:
				++state.diagnostics.affineHistoryDriftResets;
				break;
			case HandAffineHistoryResetReason::kDrawFailure:
				++state.diagnostics.affineHistoryDrawFailureResets;
				break;
			case HandAffineHistoryResetReason::kLifecycle:
				++state.diagnostics.affineHistoryLifecycleResets;
				break;
			}
		}

		[[nodiscard]] bool HasExactRetainedMotionTarget(
			const MirrorPaneDelivery::RetainedMainTargetHandle& target) noexcept
		{
			return target && target->device && target->context &&
			       target->motionRTV && target->motionResource &&
			       target->motionRTV != target->colorRTV &&
			       target->motionResource != target->colorResource &&
			       target->motionResource != target->depthResource;
		}

		void CommitHandAffineHistory(
			RuntimeState& state,
			const ReceiptPolicy::FrameReceipt& receipt,
			const Bridge::MovingSurfaceSample& surface,
			const D3D11_VIEWPORT& actualViewport,
			const MirrorPaneRenderer::MotionHistory& rendererHistory) noexcept
		{
			state.affineMotionHistory = {
				.renderer = rendererHistory,
				.receipt = receipt,
				.surface = surface,
				.viewport = actualViewport,
				.valid = true
			};
			++state.diagnostics.affineHistoryCommits;
		}

		[[nodiscard]] Cohort::ValueIdentityEquality ReturnedPaneCohortEquality(
			const StagedFirstPersonDelivery& staged,
			const GenericDrawToken& token,
			const HandMirrorApprovedContentReadOnlyObserver::
				RuntimeCandidateSnapshot& returned,
			const MirrorPaneDelivery::CurrentMainDrawState& main,
			const MirrorPaneDelivery::RetainedMainTargetHandle& activeTarget)
			noexcept
		{
			if (!staged.valid || !activeTarget || !staged.retainedMainTarget)
				return {};
			const bool currentMatchesBothRetainedTargets =
				MirrorPaneDelivery::RevalidateCurrentMainDrawTarget(
					main, activeTarget) &&
				MirrorPaneDelivery::RevalidateCurrentMainDrawTarget(
					main, staged.retainedMainTarget);
			// Each return independently proves its frozen capture target from entry
			// through normal return. That pass-local target may differ between the two
			// pane submissions and must not contaminate retained-resource equality.
			const bool colorIdentityEqual = currentMatchesBothRetainedTargets &&
				token.colorResourceIdentity == staged.colorResourceIdentity &&
				reinterpret_cast<std::uintptr_t>(activeTarget->colorResource) ==
					staged.colorResourceIdentity;
			const bool depthIdentityEqual = currentMatchesBothRetainedTargets &&
				token.depthResourceIdentity == staged.depthResourceIdentity &&
				reinterpret_cast<std::uintptr_t>(activeTarget->depthResource) ==
					staged.depthResourceIdentity;
			const auto& firstSurface = staged.returnedSurface;
			const auto& nextSurface = returned.surface;
			return {
				.receiptSourceSequenceEqual =
					staged.receipt.sourceSequence == token.receipt.sourceSequence &&
					returned.receipt.sourceSequence == token.receipt.sourceSequence,
				.receiptMainViewFrameEqual =
					staged.receipt.mainViewFrame == token.receipt.mainViewFrame &&
					returned.receipt.mainViewFrame == token.receipt.mainViewFrame &&
					main.mainWorldFrame == staged.main.mainWorldFrame,
				.receiptGraphicsFrameEqual =
					staged.receipt.graphicsFrame == token.receipt.graphicsFrame &&
					returned.receipt.graphicsFrame == token.receipt.graphicsFrame &&
					main.graphicsFrame == staged.main.graphicsFrame,
				.ownerIdentityEqual = firstSurface.owner == nextSurface.owner,
				.stableGenerationEqual =
					firstSurface.owner.stableGeneration ==
						nextSurface.owner.stableGeneration &&
					firstSurface.pose.stableGeneration ==
						nextSurface.pose.stableGeneration,
				.surfaceIdentityEqual =
					firstSurface.sourceSequence == nextSurface.sourceSequence &&
					firstSurface.pose.mainViewFrame ==
						nextSurface.pose.mainViewFrame &&
					firstSurface.pose.observedPaneSubtree ==
						nextSurface.pose.observedPaneSubtree &&
					firstSurface.pose.authoredSurface ==
						nextSurface.pose.authoredSurface,
				.geometryIdentityEqual = Bridge::SameGeometryBits(
					firstSurface.pose.geometry, nextSurface.pose.geometry),
				.deviceIdentityEqual =
					token.deviceIdentity == staged.deviceIdentity &&
					reinterpret_cast<std::uintptr_t>(activeTarget->device) ==
						staged.deviceIdentity &&
					reinterpret_cast<std::uintptr_t>(main.device) ==
						staged.deviceIdentity,
				.contextIdentityEqual =
					token.contextIdentity == staged.contextIdentity &&
					reinterpret_cast<std::uintptr_t>(activeTarget->context) ==
						staged.contextIdentity &&
					reinterpret_cast<std::uintptr_t>(main.context) ==
						staged.contextIdentity,
				.retainedColorResourceIdentityEqual = colorIdentityEqual,
				.retainedDepthResourceIdentityEqual = depthIdentityEqual,
				.viewBitsEqual = SameMatrixBits(main.view, staged.main.view) &&
					SameMatrixBits(
						main.viewProjection, staged.main.viewProjection),
				.projectionBitsEqual = SameMatrixBits(
					main.projection, staged.main.projection),
				.originBitsEqual = Bridge::SameFloat3Bits(
					{ main.origin.x, main.origin.y, main.origin.z },
					{ staged.main.origin.x, staged.main.origin.y,
						staged.main.origin.z }),
				.viewportBitsEqual = SameViewportBits(
					main.viewport, staged.main.viewport)
			};
		}

		[[nodiscard]] int RecordD3DIdentityException(bool& faulted) noexcept
		{
			faulted = true;
			return EXCEPTION_EXECUTE_HANDLER;
		}

		template <class View>
		[[nodiscard]] bool TryResourceIdentitySEH(
			View* view,
			std::uintptr_t& output,
			bool& faulted) noexcept
		{
			output = 0;
			faulted = false;
			if (!view)
				return false;
			ID3D11Resource* resource = nullptr;
			__try {
				view->GetResource(&resource);
				output = reinterpret_cast<std::uintptr_t>(resource);
			} __except (RecordD3DIdentityException(faulted)) {
				output = 0;
			}
			if (resource) {
				__try {
					resource->Release();
				} __except (RecordD3DIdentityException(faulted)) {
				}
			}
			if (faulted)
				output = 0;
			return !faulted && output != 0;
		}

		[[nodiscard]] bool TryDeviceIdentitySEH(
			ID3D11DeviceChild* child,
			std::uintptr_t& output,
			bool& faulted) noexcept
		{
			output = 0;
			faulted = false;
			if (!child)
				return false;
			ID3D11Device* device = nullptr;
			__try {
				child->GetDevice(&device);
				output = reinterpret_cast<std::uintptr_t>(device);
			} __except (RecordD3DIdentityException(faulted)) {
				output = 0;
			}
			if (device) {
				__try {
					device->Release();
				} __except (RecordD3DIdentityException(faulted)) {
				}
			}
			if (faulted)
				output = 0;
			return !faulted && output != 0;
		}

		thread_local bool g_d3dIdentityQueryFault{ false };

		template <class View>
		[[nodiscard]] std::uintptr_t ResourceIdentity(View* view) noexcept
		{
			std::uintptr_t output = 0;
			bool faulted = false;
			(void)TryResourceIdentitySEH(view, output, faulted);
			g_d3dIdentityQueryFault = g_d3dIdentityQueryFault || faulted;
			return output;
		}

		[[nodiscard]] std::uintptr_t DeviceIdentity(
			ID3D11DeviceChild* child) noexcept
		{
			std::uintptr_t output = 0;
			bool faulted = false;
			(void)TryDeviceIdentitySEH(child, output, faulted);
			g_d3dIdentityQueryFault = g_d3dIdentityQueryFault || faulted;
			return output;
		}

		void ObserveReturnedExactPane(
			RuntimeState& state,
			const Bridge::MovingSurfaceSample& surface,
			const bool raisedPresentation) noexcept
		{
			auto& visibility = state.visibility;
			if (!Bridge::IsValidMovingSurfaceSample(surface))
				return;
			visibility.raisedPresentation = raisedPresentation;
			if (visibility.lastVisibilityUpdateSourceSequence ==
				surface.sourceSequence) {
				// The exact hand contract admits one returned pane opportunity per
				// source.  Even a duplicate with the same owner is ambiguous: it could
				// otherwise consume or present one private frame twice.
				visibility.ambiguousThisSource = true;
				return;
			}
			const bool adjacent = visibility.lastVisibleSourceSequence != 0 &&
				visibility.lastVisibleSourceSequence + 1 == surface.sourceSequence;
			if (!adjacent || visibility.accumulatedOwner != surface.owner) {
				visibility.consecutiveRaisedVisibleFrames = 1;
				visibility.accumulatedOwner = surface.owner;
			} else if (visibility.consecutiveRaisedVisibleFrames !=
				(std::numeric_limits<std::uint32_t>::max)()) {
				++visibility.consecutiveRaisedVisibleFrames;
			}
			visibility.consecutiveIneligibleFrames = 0;
			visibility.lastVisibleSourceSequence = surface.sourceSequence;
			visibility.lastVisibilityUpdateSourceSequence = surface.sourceSequence;
			visibility.ambiguousThisSource = false;
		}

		[[nodiscard]] bool SamePlaneBits(
			const PlanarMirrorMath::Plane& left,
			const PlanarMirrorMath::Plane& right) noexcept
		{
			return Bridge::SameFloatBits(left.normal.x, right.normal.x) &&
			       Bridge::SameFloatBits(left.normal.y, right.normal.y) &&
			       Bridge::SameFloatBits(left.normal.z, right.normal.z) &&
			       Bridge::SameFloatBits(left.distance, right.distance);
		}

		struct CompletedFrameResourceEvidence
		{
			std::uintptr_t colorResource{ 0 };
			std::uintptr_t depthResource{ 0 };
			std::uintptr_t colorDevice{ 0 };
			std::uintptr_t depthDevice{ 0 };
			D3D11_TEXTURE2D_DESC colorDesc{};
			D3D11_TEXTURE2D_DESC depthDesc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC colorViewDesc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDesc{};
			bool valid{ false };
		};

		template <class Interface>
		void ReleaseD3DInterfaceSEH(Interface*& value, bool& faulted) noexcept
		{
			if (!value)
				return;
			__try {
				value->Release();
			} __except (RecordD3DIdentityException(faulted)) {
			}
			value = nullptr;
		}

		[[nodiscard]] bool ScrubCompletedNativeFrameSEH(
			CompletedNativeFrame& frame) noexcept
		{
			auto* color = frame.colorSRV.Detach();
			auto* depth = frame.depthSRV.Detach();
			// Remove every value authorization before the first native vtable call.
			// A failing Release can therefore neither leave a reachable publication
			// identity nor make the caller's later destructor retry the same object.
			frame = {};
			bool releaseFaulted = false;
			ReleaseD3DInterfaceSEH(depth, releaseFaulted);
			ReleaseD3DInterfaceSEH(color, releaseFaulted);
			return !releaseFaulted;
		}

		[[nodiscard]] bool ScrubRendererFrameSEH(
			MirrorPaneRenderer::PublishedFrame& frame) noexcept
		{
			auto* color = frame.colorSRV.Detach();
			auto* depth = frame.depthSRV.Detach();
			frame = {};
			bool releaseFaulted = false;
			ReleaseD3DInterfaceSEH(depth, releaseFaulted);
			ReleaseD3DInterfaceSEH(color, releaseFaulted);
			return !releaseFaulted;
		}

		[[nodiscard]] bool ClearLastGoodPresentation(
			RuntimeState& state) noexcept
		{
			auto prior = std::move(state.lastGoodPresentation);
			auto* replaySRV = state.lastGoodReplaySRV.Detach();
			auto* replayTexture = state.lastGoodReplayTexture.Detach();
			state.lastGoodPresentation = {};
			state.lastGoodReplayDeviceIdentity = 0;
			if (prior.valid)
				++state.diagnostics.lastGoodInvalidations;
			prior.retainedTarget.reset();
			const bool targetReleased = !MirrorPaneDelivery::Faulted();
			const bool frameReleased = ScrubRendererFrameSEH(prior.frame);
			bool releaseFaulted = false;
			ReleaseD3DInterfaceSEH(replaySRV, releaseFaulted);
			ReleaseD3DInterfaceSEH(replayTexture, releaseFaulted);
			return targetReleased && frameReleased && !releaseFaulted &&
				!MirrorPaneDelivery::Faulted();
		}

		[[nodiscard]] bool LastGoodPresentationValid(
			const RuntimeState& state) noexcept
		{
			const auto& cached = state.lastGoodPresentation;
			return cached.valid && cached.deviceIdentity != 0 &&
				LiveReturn::IsValidLiveDeliveryTarget(cached.target) &&
				cached.target.deviceIdentity == cached.deviceIdentity &&
				cached.retainedTarget &&
				MakeLiveDeliveryTargetIdentity(cached.retainedTarget) ==
					cached.target &&
				StorePolicy::IsExactInternalHandPublication(
					cached.sourcePublication) &&
				state.lastGoodReplayTexture && state.lastGoodReplaySRV &&
				state.lastGoodReplayDeviceIdentity == cached.deviceIdentity &&
				cached.frame.valid && cached.frame.colorSRV &&
				cached.frame.colorSRV.Get() == state.lastGoodReplaySRV.Get() &&
				cached.frame.owner.kind == Bridge::OwnerKind::kHand &&
				cached.frame.audience ==
					Bridge::PublicationAudience::kInternalHandOnly &&
				cached.frame.captureSequence ==
					cached.sourcePublication.publication.captureSequence &&
				Bridge::SameOwner(
					cached.frame.owner,
					cached.sourcePublication.publication.attempt.owner) &&
				Bridge::IsValidMovingSurfaceSample(cached.liveSurface) &&
				Bridge::SameOwner(
					cached.frame.owner,
					Bridge::MakeHandOwner(cached.liveSurface.owner));
		}

		[[nodiscard]] bool LastGoodMatchesPublication(
			const RuntimeState& state,
			const StorePolicy::PublicationSnapshot& publication) noexcept
		{
			if (!LastGoodPresentationValid(state) ||
				!StorePolicy::IsExactInternalHandPublication(publication)) {
				return false;
			}
			return StorePolicy::SamePublicationSnapshot(
					state.lastGoodPresentation.sourcePublication, publication) &&
				Bridge::SameOwner(
					state.lastGoodPresentation.frame.owner,
					publication.publication.attempt.owner);
		}

		/**
		 * Copy immediately after a fully covered final draw. Holding the Store SRV is
		 * insufficient: a later private capture can overwrite that physical texture
		 * before its presentation miss is discovered at first-person return.
		 */
		[[nodiscard]] bool SnapshotLastGoodColor(
			RuntimeState& state,
			const MirrorPaneRenderer::PublishedFrame& frame,
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& output,
			bool& faulted) noexcept
		{
			output.Reset();
			faulted = false;
			if (!frame.valid || !frame.colorSRV || !device || !context)
				return false;

			ID3D11Resource* sourceResource = nullptr;
			ID3D11Texture2D* sourceTexture = nullptr;
			ID3D11Texture2D* newTexture = nullptr;
			ID3D11ShaderResourceView* newSRV = nullptr;
			D3D11_TEXTURE2D_DESC textureDesc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			bool nativeFaulted = false;
			bool copied = false;
			bool installNew = false;
			__try {
				frame.colorSRV->GetResource(&sourceResource);
				if (sourceResource && SUCCEEDED(sourceResource->QueryInterface(
						__uuidof(ID3D11Texture2D),
						reinterpret_cast<void**>(&sourceTexture))) &&
					sourceTexture) {
					if (sourceTexture == state.lastGoodReplayTexture.Get() &&
						state.lastGoodReplaySRV) {
						copied = true;
					} else {
						sourceTexture->GetDesc(&textureDesc);
						frame.colorSRV->GetDesc(&srvDesc);
						const bool recreate = !state.lastGoodReplayTexture ||
							!state.lastGoodReplaySRV ||
							state.lastGoodReplayDeviceIdentity !=
								reinterpret_cast<std::uintptr_t>(device);
						if (recreate) {
							if (SUCCEEDED(device->CreateTexture2D(
									&textureDesc, nullptr, &newTexture)) &&
								newTexture &&
								SUCCEEDED(device->CreateShaderResourceView(
									newTexture, &srvDesc, &newSRV)) &&
								newSRV) {
								context->CopyResource(newTexture, sourceTexture);
								copied = true;
								installNew = true;
							}
						} else if (state.lastGoodReplayTexture &&
							state.lastGoodReplaySRV) {
							D3D11_SHADER_RESOURCE_VIEW_DESC cachedView{};
							state.lastGoodReplaySRV->GetDesc(&cachedView);
							const bool sameView = cachedView.Format == srvDesc.Format &&
								cachedView.ViewDimension == srvDesc.ViewDimension &&
								cachedView.Texture2D.MostDetailedMip == srvDesc.Texture2D.MostDetailedMip &&
								cachedView.Texture2D.MipLevels == srvDesc.Texture2D.MipLevels;
							copied = sameView || (SUCCEEDED(device->CreateShaderResourceView(
								state.lastGoodReplayTexture.Get(), &srvDesc, &newSRV)) && newSRV);
							if (copied)
								context->CopyResource(state.lastGoodReplayTexture.Get(), sourceTexture);
						}
					}
				}
			} __except (RecordD3DIdentityException(nativeFaulted)) {
				copied = false;
			}
			ReleaseD3DInterfaceSEH(sourceTexture, nativeFaulted);
			ReleaseD3DInterfaceSEH(sourceResource, nativeFaulted);
			if (!copied || nativeFaulted) {
				ReleaseD3DInterfaceSEH(newSRV, nativeFaulted);
				ReleaseD3DInterfaceSEH(newTexture, nativeFaulted);
				faulted = nativeFaulted;
				return false;
			}

			if (!installNew && newSRV) {
				auto* priorSRV = state.lastGoodReplaySRV.Detach();
				state.lastGoodReplaySRV.Attach(newSRV);
				newSRV = nullptr;
				ReleaseD3DInterfaceSEH(priorSRV, nativeFaulted);
				if (nativeFaulted) {
					faulted = true;
					return false;
				}
			}
			if (installNew) {
				auto* priorSRV = state.lastGoodReplaySRV.Detach();
				auto* priorTexture = state.lastGoodReplayTexture.Detach();
				state.lastGoodReplayTexture.Attach(newTexture);
				state.lastGoodReplaySRV.Attach(newSRV);
				newTexture = nullptr;
				newSRV = nullptr;
				state.lastGoodReplayDeviceIdentity =
					reinterpret_cast<std::uintptr_t>(device);
				bool releaseFaulted = false;
				ReleaseD3DInterfaceSEH(priorSRV, releaseFaulted);
				ReleaseD3DInterfaceSEH(priorTexture, releaseFaulted);
				if (releaseFaulted) {
					faulted = true;
					return false;
				}
			}
			ReleaseD3DInterfaceSEH(newSRV, nativeFaulted);
			ReleaseD3DInterfaceSEH(newTexture, nativeFaulted);
			if (nativeFaulted || !state.lastGoodReplaySRV) {
				faulted = nativeFaulted;
				return false;
			}
			output = state.lastGoodReplaySRV;
			return static_cast<bool>(output);
		}

		[[nodiscard]] bool CommitLastGoodPresentation(
			RuntimeState& state,
			const StorePolicy::PublicationSnapshot& sourcePublication,
			const MirrorPaneRenderer::PublishedFrame& frame,
			const Bridge::MovingSurfaceSample& liveSurface,
			const MirrorPaneRenderer::MainView& mainView,
			const D3D11_VIEWPORT& viewport,
			const MirrorPaneDelivery::RetainedMainTargetHandle& retainedTarget,
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			const MirrorPaneRenderer::DrawStatus status,
			const bool outerFinal,
			const bool raisedPresentation) noexcept
		{
			const auto target =
				MakeLiveDeliveryTargetIdentity(retainedTarget);
			const auto exactOwner = Bridge::IsValidMovingSurfaceSample(liveSurface) &&
				Bridge::SameOwner(
					frame.owner, Bridge::MakeHandOwner(liveSurface.owner));
			const bool baseCommitEligible = LastGoodPolicy::CanCommit(
				outerFinal,
				status == MirrorPaneRenderer::DrawStatus::kDrawn,
				true,
				exactOwner,
				frame.valid && static_cast<bool>(frame.colorSRV),
				device != nullptr);
			const bool fullReflectedApertureCoverage = baseCommitEligible &&
				MirrorPaneRenderer::HasFullHandReflectedApertureCoverage(
					MakePaneTransform(liveSurface, frame.owner), frame);
			if (baseCommitEligible && !fullReflectedApertureCoverage)
				++state.diagnostics.lastGoodApertureCoverageRejects;
			if (!LastGoodPolicy::CanCommit(
					outerFinal,
					status == MirrorPaneRenderer::DrawStatus::kDrawn,
					fullReflectedApertureCoverage,
					exactOwner,
					frame.valid && static_cast<bool>(frame.colorSRV),
					device != nullptr) ||
				!context || !LiveReturn::IsValidLiveDeliveryTarget(target) ||
				target.deviceIdentity !=
					reinterpret_cast<std::uintptr_t>(device) ||
				target.contextIdentity !=
					reinterpret_cast<std::uintptr_t>(context) ||
				!StorePolicy::IsExactInternalHandPublication(sourcePublication) ||
				!Bridge::SameOwner(
					frame.owner,
					sourcePublication.publication.attempt.owner) ||
				frame.captureSequence !=
					sourcePublication.publication.captureSequence) {
				return true;
			}

			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> snapshotSRV{};
			bool snapshotFaulted = false;
			// An exact completed publication already has an immutable backup. Refresh
			// the live pose/target below without copying its HDR mip chain again.
			const bool reuseSnapshot = MirrorCaptureWorkPolicy::enabled.load(std::memory_order_relaxed) &&
				LastGoodMatchesPublication(state, sourcePublication) &&
				state.lastGoodReplayDeviceIdentity == reinterpret_cast<std::uintptr_t>(device);
			if (reuseSnapshot) snapshotSRV = state.lastGoodReplaySRV;
			if (!reuseSnapshot && !SnapshotLastGoodColor(
					state, frame, device, context, snapshotSRV,
					snapshotFaulted)) {
				++state.diagnostics.lastGoodSnapshotRejects;
				if (snapshotFaulted)
					return false;
				// Allocation/query failure only rejects this cache refresh and never
				// rewrites a successful pane draw into a terminal renderer fault.  The
				// detached snapshot is output-target independent, so a same-device RTV,
				// DSV, or viewport rotation may retain it and rebind on a later replay.
				// Only a device replacement makes the texture itself unusable.
				if (LastGoodPresentationValid(state) &&
					state.lastGoodPresentation.deviceIdentity !=
						reinterpret_cast<std::uintptr_t>(device) &&
					!ClearLastGoodPresentation(state)) {
					return false;
				}
				return true;
			}
			auto nextFrame = frame;
			nextFrame.colorSRV = std::move(snapshotSRV);
			nextFrame.depthSRV.Reset();
			auto prior = std::move(state.lastGoodPresentation);
			state.lastGoodPresentation = {
				.frame = std::move(nextFrame),
				.sourcePublication = sourcePublication,
				.liveSurface = liveSurface,
				.mainView = mainView,
				.viewport = viewport,
				.target = target,
				.retainedTarget = retainedTarget,
				.deviceIdentity = reinterpret_cast<std::uintptr_t>(device),
				.presentationMode = LastGoodPolicy::ClassifyPresentationMode(
					raisedPresentation),
				.committedSourceSequence =
					HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled() ?
						sourcePublication.publication.attempt.surface.sourceSequence :
						liveSurface.sourceSequence,
				.valid = true
			};
			prior.retainedTarget.reset();
			const bool targetReleased = !MirrorPaneDelivery::Faulted();
			const bool frameReleased = ScrubRendererFrameSEH(prior.frame);
			const bool committed = targetReleased && frameReleased &&
				!MirrorPaneDelivery::Faulted();
			if (committed)
				++state.diagnostics.lastGoodCommits;
			return committed;
		}

		[[nodiscard]] bool QueryCompletedFrameResourcesSEH(
			ID3D11ShaderResourceView* colorSRV,
			ID3D11ShaderResourceView* depthSRV,
			CompletedFrameResourceEvidence& output,
			bool& faulted) noexcept
		{
			output = {};
			faulted = false;
			if (!colorSRV || !depthSRV)
				return false;
			ID3D11Resource* colorResource = nullptr;
			ID3D11Resource* depthResource = nullptr;
			ID3D11Texture2D* colorTexture = nullptr;
			ID3D11Texture2D* depthTexture = nullptr;
			ID3D11Device* colorDevice = nullptr;
			ID3D11Device* depthDevice = nullptr;
			bool queried = false;
			__try {
				colorSRV->GetResource(&colorResource);
				depthSRV->GetResource(&depthResource);
				if (colorResource && depthResource &&
					SUCCEEDED(colorResource->QueryInterface(
						__uuidof(ID3D11Texture2D),
						reinterpret_cast<void**>(&colorTexture))) &&
					SUCCEEDED(depthResource->QueryInterface(
						__uuidof(ID3D11Texture2D),
						reinterpret_cast<void**>(&depthTexture))) &&
					colorTexture && depthTexture) {
					colorTexture->GetDesc(&output.colorDesc);
					depthTexture->GetDesc(&output.depthDesc);
					colorSRV->GetDesc(&output.colorViewDesc);
					depthSRV->GetDesc(&output.depthViewDesc);
					colorTexture->GetDevice(&colorDevice);
					depthTexture->GetDevice(&depthDevice);
					output.colorResource =
						reinterpret_cast<std::uintptr_t>(colorResource);
					output.depthResource =
						reinterpret_cast<std::uintptr_t>(depthResource);
					output.colorDevice =
						reinterpret_cast<std::uintptr_t>(colorDevice);
					output.depthDevice =
						reinterpret_cast<std::uintptr_t>(depthDevice);
					queried = output.colorResource != 0 &&
						output.depthResource != 0 && output.colorDevice != 0 &&
						output.depthDevice != 0;
				}
			} __except (RecordD3DIdentityException(faulted)) {
				queried = false;
			}
			ReleaseD3DInterfaceSEH(colorDevice, faulted);
			ReleaseD3DInterfaceSEH(depthDevice, faulted);
			ReleaseD3DInterfaceSEH(colorTexture, faulted);
			ReleaseD3DInterfaceSEH(depthTexture, faulted);
			ReleaseD3DInterfaceSEH(colorResource, faulted);
			ReleaseD3DInterfaceSEH(depthResource, faulted);
			output.valid = queried && !faulted;
			return output.valid;
		}

		[[nodiscard]] bool ValidateCompletedNativeFrame(
			const CaptureLease& lease,
			const CompletedNativeFrame& frame,
			bool& nativeQueryFault) noexcept
		{
			nativeQueryFault = false;
			const bool reducedEnabled = LoweredPolicy::reducedResolutionEnabled.load(
				std::memory_order_relaxed);
			if (!lease.valid || !frame.valid || !frame.colorSRV || !frame.depthSRV ||
				frame.targetIdentity != lease.ticket.attempt.privateTarget ||
				!Bridge::IsValidPrivateTarget(frame.targetIdentity) ||
				!Bridge::SameMovingSurface(
					frame.capturedSurface, lease.ticket.attempt.surface) ||
				frame.deviceIdentity == 0 ||
				!(MirrorCaptureSizing::UsesFramebuffer() ?
					MirrorCaptureSizing::CompletedExtentAllowed({ frame.width, frame.height }, true) :
					LoweredPolicy::CaptureDimensionsAllowed(frame.width, frame.height, reducedEnabled)) ||
				frame.colorFormat != DXGI_FORMAT_R16G16B16A16_FLOAT ||
				!IsFiniteMatrix(frame.reflectedViewProjection) ||
				!std::isfinite(frame.reflectedOrigin.x) ||
				!std::isfinite(frame.reflectedOrigin.y) ||
				!std::isfinite(frame.reflectedOrigin.z) ||
				!std::isfinite(frame.frozenSourceOrigin.x) ||
				!std::isfinite(frame.frozenSourceOrigin.y) ||
				!std::isfinite(frame.frozenSourceOrigin.z)) {
				return false;
			}

			CompletedFrameResourceEvidence resources{};
			if (!QueryCompletedFrameResourcesSEH(
					frame.colorSRV.Get(), frame.depthSRV.Get(), resources,
					nativeQueryFault) ||
				resources.colorResource != frame.targetIdentity.colorResourceToken ||
				resources.depthResource != frame.targetIdentity.depthResourceToken) {
				return false;
			}
			const auto& colorDesc = resources.colorDesc;
			const auto& depthDesc = resources.depthDesc;
			const auto& colorView = resources.colorViewDesc;
			const auto& depthView = resources.depthViewDesc;
			if (colorView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				depthView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				!LoweredPolicy::ColorViewDimensionsMatch(colorDesc.Width, colorDesc.Height,
					colorDesc.MipLevels, colorView.Texture2D.MostDetailedMip,
					colorView.Texture2D.MipLevels, frame.width, frame.height, reducedEnabled) ||
				colorView.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
				colorDesc.ArraySize != 1 ||
				colorDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
				colorDesc.SampleDesc.Count != 1 ||
				(colorDesc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) == 0 ||
				!LoweredPolicy::DepthViewDimensionsMatch(depthDesc.Width, depthDesc.Height,
					depthDesc.MipLevels, depthView.Texture2D.MostDetailedMip,
					depthView.Texture2D.MipLevels, frame.width, frame.height, reducedEnabled) ||
				depthView.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
				depthDesc.ArraySize != 1 ||
				depthDesc.Format != DXGI_FORMAT_R24G8_TYPELESS ||
				depthDesc.SampleDesc.Count != 1) {
				return false;
			}
			const auto identity = EngineDeviceIdentity::Current(frame.deviceIdentity);
			return EngineDeviceIdentityPolicy::OwnedByEngineDevice(
					   identity, resources.colorDevice) &&
			       EngineDeviceIdentityPolicy::OwnedByEngineDevice(
					   identity, resources.depthDevice);
		}

		[[nodiscard]] MirrorPaneRenderer::PaneTransform MakePaneTransform(
			const Bridge::MovingSurfaceSample& surface,
			const Bridge::MirrorOwnerIdentity& owner) noexcept
		{
			const auto& geometry = surface.pose.geometry;
			return {
				.center = { geometry.center.x, geometry.center.y, geometry.center.z },
				.tangentExtent = { geometry.tangent.x * geometry.halfWidth,
					geometry.tangent.y * geometry.halfWidth,
					geometry.tangent.z * geometry.halfWidth },
				.bitangentExtent = { geometry.bitangent.x * geometry.halfHeight,
					geometry.bitangent.y * geometry.halfHeight,
					geometry.bitangent.z * geometry.halfHeight },
				.owner = owner
			};
		}

		/**
		 * Retarget bases for the glued hand reflection: the authored bases of the
		 * captured and live panes.  The window itself is anchored at capture time
		 * to the portrait camera's up (SecondView), which is the only reference
		 * that pitches rigidly with the held pane.
		 */
		void SelectHandRetargetBases(
			const MirrorPaneRenderer::PaneTransform& capturedPane,
			const MirrorPaneRenderer::PaneTransform& livePane,
			MirrorPaneRenderer::PaneTransform& capturedBasis,
			MirrorPaneRenderer::PaneTransform& liveBasis) noexcept
		{
			// Authored bases only.  The V109 world-up re-expression rolled the glued
			// image as a side-yawed pane pitched with the camera (the owner saw the
			// reflection rotate left-to-right on mouse-up), and replay is now confined
			// to one optical mode and a few frames, so the exact authored decal
			// mapping (identity for the same pane) is the correct retarget.
			capturedBasis = capturedPane;
			liveBasis = livePane;
		}

		/**
		 * The V114/V115 owner runs that exhibited a yaw-dependent directional
		 * smear both used the x16 anisotropic hand sampler. The intended isotropic
		 * A/B was selected only after V114 had already fault-stopped, so it never
		 * rendered a frame. Use the stable trilinear full-mip lane deterministically
		 * for hand mirrors: it preserves the +0.25 LOD bias and 2x2 spatial resolve
		 * without a yaw-varying anisotropic tap axis. Do not poll or switch marker
		 * state mid-session.
		 */
		[[nodiscard]] MirrorPaneRenderer::SamplingMode SelectHandSamplingMode() noexcept
		{
			return MirrorPaneRenderer::SamplingMode::kStableHandIsotropic;
		}

		[[nodiscard]] const char* SamplingModeName(
			MirrorPaneRenderer::SamplingMode mode) noexcept
		{
			switch (mode) {
			case MirrorPaneRenderer::SamplingMode::kBaseMipOnly:
				return "base-mip-only";
			case MirrorPaneRenderer::SamplingMode::kStableHandIsotropic:
				return "isotropic-full-chain";
			case MirrorPaneRenderer::SamplingMode::kStableHandFullMipChain:
				return "anisotropic-full-chain";
			default:
				return "other";
			}
		}

		/** Ready the hand renderer with the currently selected sampler. */
		void EnsureHandRenderer(
			RuntimeState& state,
			ID3D11Device* device,
			MirrorPaneRenderer::InitializationStatus& initialization) noexcept
		{
			const auto mode = SelectHandSamplingMode();
			if (!state.renderer.Ready() ||
				state.renderer.ActiveSamplingMode() != mode) {
				(void)state.renderer.Initialize(device, &initialization, mode);
			}
		}

		[[nodiscard]] bool CaptureStableHandPresentation(
			RuntimeState::StableHandPresentation& output,
			const MirrorPaneRenderer::PaneTransform& pane,
			const MirrorPaneRenderer::MainView& main,
			const D3D11_VIEWPORT& viewport) noexcept
		{
			if (!Bridge::IsValidOwner(pane.owner) ||
				pane.owner.kind != Bridge::OwnerKind::kHand) {
				return false;
			}
			const auto project = [&main](const DirectX::XMFLOAT3& point) noexcept {
				const auto relative = DirectX::XMVectorSet(
					point.x - main.origin.x, point.y - main.origin.y,
					point.z - main.origin.z, 1.0F);
				DirectX::XMFLOAT4 clip{};
				DirectX::XMStoreFloat4(
					&clip,
					DirectX::XMVector4Transform(
						relative, DirectX::XMLoadFloat4x4(&main.viewProjection)));
				return clip;
			};
			const auto finite4 = [](const DirectX::XMFLOAT4& value) noexcept {
				return std::isfinite(value.x) && std::isfinite(value.y) &&
					std::isfinite(value.z) && std::isfinite(value.w);
			};
			const DirectX::XMFLOAT3 tangentPoint{
				pane.center.x + pane.tangentExtent.x,
				pane.center.y + pane.tangentExtent.y,
				pane.center.z + pane.tangentExtent.z };
			const DirectX::XMFLOAT3 bitangentPoint{
				pane.center.x + pane.bitangentExtent.x,
				pane.center.y + pane.bitangentExtent.y,
				pane.center.z + pane.bitangentExtent.z };
			const auto centerClip = project(pane.center);
			const auto tangentClip = project(tangentPoint);
			const auto bitangentClip = project(bitangentPoint);
			if (!finite4(centerClip) || !finite4(tangentClip) ||
				!finite4(bitangentClip) || !(centerClip.w > 1.0e-5F) ||
				!std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) ||
				viewport.Width <= 0.0F || viewport.Height <= 0.0F) {
				return false;
			}
			output = {
				.centerClip = centerClip,
				.tangentClipDelta = {
					tangentClip.x - centerClip.x,
					tangentClip.y - centerClip.y,
					tangentClip.z - centerClip.z,
					tangentClip.w - centerClip.w },
				.bitangentClipDelta = {
					bitangentClip.x - centerClip.x,
					bitangentClip.y - centerClip.y,
					bitangentClip.z - centerClip.z,
					bitangentClip.w - centerClip.w },
				.owner = pane.owner,
				.viewport = viewport,
				.valid = true
			};
			return true;
		}

		[[nodiscard]] bool BuildStableHandPresentationMainView(
			const RuntimeState::StableHandPresentation& stable,
			const MirrorPaneRenderer::PaneTransform& pane,
			const D3D11_VIEWPORT& viewport,
			MirrorPaneRenderer::MainView& output) noexcept
		{
			output = {};
			// A cached screen rectangle belongs to the old animation pose. It must
			// never place a reflection while the physical frame is moving down.
			if (HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled() ||
				HandMirrorApprovedContentReadOnlyObserver::VRRuntimeFixEnabled())
				return false;
			if (!stable.valid || !Bridge::SameOwner(stable.owner, pane.owner) ||
				!SameViewportBits(stable.viewport, viewport)) {
				return false;
			}
			const auto lengthSquared = [](const DirectX::XMFLOAT3& value) noexcept {
				return value.x * value.x + value.y * value.y + value.z * value.z;
			};
			const float tangentLengthSquared = lengthSquared(pane.tangentExtent);
			const float bitangentLengthSquared = lengthSquared(pane.bitangentExtent);
			const float orthogonality =
				pane.tangentExtent.x * pane.bitangentExtent.x +
				pane.tangentExtent.y * pane.bitangentExtent.y +
				pane.tangentExtent.z * pane.bitangentExtent.z;
			if (!std::isfinite(tangentLengthSquared) ||
				!std::isfinite(bitangentLengthSquared) ||
				!std::isfinite(orthogonality) || tangentLengthSquared <= 1.0e-6F ||
				bitangentLengthSquared <= 1.0e-6F ||
				std::abs(orthogonality) > 1.0e-3F *
					std::sqrt(tangentLengthSquared * bitangentLengthSquared)) {
				return false;
			}

			const float* tangentDelta = &stable.tangentClipDelta.x;
			const float* bitangentDelta = &stable.bitangentClipDelta.x;
			const float* center = &stable.centerClip.x;
			float* matrix = &output.viewProjection._11;
			for (std::size_t column = 0; column < 4; ++column) {
				const float tangentScale = tangentDelta[column] / tangentLengthSquared;
				const float bitangentScale =
					bitangentDelta[column] / bitangentLengthSquared;
				matrix[column] = pane.tangentExtent.x * tangentScale +
					pane.bitangentExtent.x * bitangentScale;
				matrix[4 + column] = pane.tangentExtent.y * tangentScale +
					pane.bitangentExtent.y * bitangentScale;
				matrix[8 + column] = pane.tangentExtent.z * tangentScale +
					pane.bitangentExtent.z * bitangentScale;
				matrix[12 + column] = center[column];
				if (!std::isfinite(matrix[column]) ||
					!std::isfinite(matrix[4 + column]) ||
					!std::isfinite(matrix[8 + column]) ||
					!std::isfinite(matrix[12 + column])) {
					output = {};
					return false;
				}
			}
			output.origin = pane.center;
			return true;
		}

		struct DeliveryExecutionState
		{
			RuntimeState* runtime{ nullptr };
			StorePolicy::PublicationSnapshot acquired{};
			Bridge::MovingSurfaceSample visibleSurface{};
			MirrorPaneDelivery::CurrentMainDrawState main{};
			StorePolicy::ConsumptionResult consumed{};
			StorePolicy::ConsumeStatus consumeStatus{
				StorePolicy::ConsumeStatus::kUnavailable };
			MirrorPaneRenderer::DrawStatus drawStatus{
				MirrorPaneRenderer::DrawStatus::kNotInitialized };
			bool bodyReturnedNormally{ false };
			bool exceptionCaught{ false };
		};

		[[nodiscard]] bool ShutdownRendererSEH(
			MirrorPaneRenderer::Renderer& renderer) noexcept;

		void ExecuteDeliveryBody(DeliveryExecutionState& execution)
		{
			auto& state = *execution.runtime;
			auto& slot = state.nativeSlots[execution.acquired.physicalRole];
			MirrorPaneRenderer::InitializationStatus initialization{};
			EnsureHandRenderer(state, execution.main.device, initialization);
			if (state.deliveryFrame ==
				(std::numeric_limits<std::uint64_t>::max)()) {
				return;
			}
			++state.deliveryFrame;
			bool drawn = false;
			{
				// The request owns temporary ComPtr readers.  This inner scope must end
				// before Store consumption can produce a retirement receipt.
				const auto pane = MakePaneTransform(
					execution.visibleSurface,
					execution.acquired.publication.attempt.owner);
				const MirrorPaneRenderer::DrawRequest request{
					.frame = slot.frame,
					.pane = pane,
					.mainView = { execution.main.viewProjection,
						execution.main.origin },
					.targets = { execution.main.colorRTV, nullptr,
						execution.main.depthDSV, execution.main.viewport },
					.motionHistory = {},
					.deliveryFrame = state.deliveryFrame
				};
				execution.drawStatus = state.renderer.Ready() ?
					DrawHandPane(state.renderer, execution.main, request) :
					MirrorPaneRenderer::DrawStatus::kNotInitialized;
				drawn = MirrorPaneRenderer::IsSuccessfulDraw(execution.drawStatus);
			}
			execution.consumeStatus =
				state.store->ConsumeAtFirstPersonOpportunity(
					{
						.acquired = execution.acquired,
						.visibleSurface = execution.visibleSurface,
						.visibleDrawSnapshot =
							execution.acquired.phaseSnapshots.visibleDraw,
						.sourceSequence = execution.visibleSurface.sourceSequence,
						.consumer =
							Bridge::PublicationConsumer::kInternalHandPane,
						.exactFirstPersonOpportunity = true,
						.firstPersonTLSActive = g_firstPerson.active,
						.exactPaneSubmittedByCurrentMainView = true,
						.nativePaneDrawReturnedNormally = true,
						.writableMainColorDepthAndViewProven =
							execution.main.valid && execution.main.context &&
							execution.main.colorRTV && execution.main.depthDSV,
						.customRendererSucceeded = drawn
					}, execution.consumed);
			execution.bodyReturnedNormally = true;
		}

		__declspec(noinline) void ExecuteDeliveryWithReaderFinally(
			DeliveryExecutionState& execution) noexcept
		{
			if (!execution.runtime ||
				execution.runtime->activeInternalReaders ==
					(std::numeric_limits<std::uint32_t>::max)()) {
				return;
			}
			++execution.runtime->activeInternalReaders;
			__try {
				__try {
					ExecuteDeliveryBody(execution);
				} __finally {
					if (execution.runtime->activeInternalReaders != 0)
						--execution.runtime->activeInternalReaders;
					if (execution.runtime->activeInternalReaders == 0 &&
						execution.runtime->terminalRendererScrubPending) {
						(void)ShutdownRendererSEH(execution.runtime->renderer);
						execution.runtime->terminalRendererScrubPending = false;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				execution.exceptionCaught = true;
			}
		}

		// Hand presentation happens at the exact native pane's normal return.  At
		// that seam the pass-local main color/depth target is still authoritative;
		// delaying the draw until the enclosing first-person call returns loses that
		// guarantee on live Skyrim.  The Store continues to own/retire the completed
		// private frame, while this reader only borrows it synchronously for the same
		// renderer used by placed mirrors.
		struct DirectPaneDeliveryExecutionState
		{
			RuntimeState* runtime{ nullptr };
			StorePolicy::PublicationSnapshot publication{};
			Bridge::MovingSurfaceSample paneSurface{};
			MirrorPaneDelivery::CurrentMainDrawState main{};
			MirrorPaneDelivery::RetainedMainTargetHandle retainedOuterFinalTarget{};
			ReceiptPolicy::FrameReceipt receipt{};
			MirrorPaneRenderer::MotionHistory committedHistory{};
			HandAffineHistoryReject historyClassification{
				HandAffineHistoryReject::kMissing };
			MirrorPaneRenderer::DrawStatus drawStatus{
				MirrorPaneRenderer::DrawStatus::kNoPublishedFrame };
			bool outerFinal{ false };
			bool graphIndicatesRaisedPresentation{ false };
			bool affineAuthorizedForCurrentPublication{ false };
			bool retainedOuterFinalMotionCurrent{ false };
			bool motionTargetAvailable{ false };
			bool affineMotionOutputEnabled{ false };
			bool bodyReturnedNormally{ false };
			bool exceptionCaught{ false };
		};

		void ObserveHandCoverProjection(
			CohortDiagnostics& diagnostics,
			const ReceiptPolicy::FrameReceipt& receipt,
			const MirrorPaneRenderer::DrawRequest& request,
			const char* route)
		{
			constexpr std::uint8_t kMainWRisk = 1u << 0;
			constexpr std::uint8_t kReflectedWRisk = 1u << 1;
			constexpr std::uint8_t kInvalid = 1u << 2;
			constexpr std::uint8_t kNonFinite = 1u << 3;

			const auto telemetry =
				MirrorPaneRenderer::InspectHandCoverProjection(
					request.pane, request.mainView, request.frame);
			++diagnostics.handCoverClipChecks;
			std::uint8_t riskMask = 0;
			if (!telemetry.handAperture || !telemetry.valid) {
				++diagnostics.handCoverClipInvalid;
				riskMask |= kInvalid;
			}
			if (!telemetry.finite)
				riskMask |= kNonFinite;
			if (telemetry.MainClipWAtRisk()) {
				++diagnostics.handCoverMainWRisks;
				riskMask |= kMainWRisk;
			}
			if (telemetry.ReflectedClipWAtRisk()) {
				++diagnostics.handCoverReflectedWRisks;
				riskMask |= kReflectedWRisk;
			}

			const bool stateChanged =
				!diagnostics.handCoverRiskStateInitialized ||
				diagnostics.lastHandCoverRiskMask != riskMask;
			if (diagnostics.handCoverRiskStateInitialized && stateChanged)
				++diagnostics.handCoverRiskTransitions;
			diagnostics.handCoverRiskStateInitialized = true;
			diagnostics.lastHandCoverRiskMask = riskMask;
			diagnostics.lastHandCoverMinimumMainW =
				telemetry.minimumMainClipW;
			diagnostics.lastHandCoverMinimumReflectedW =
				telemetry.minimumReflectedClipW;
			diagnostics.lastHandCoverMainPositiveWVertices =
				telemetry.mainPositiveWVertices;
			diagnostics.lastHandCoverReflectedPositiveWVertices =
				telemetry.reflectedPositiveWVertices;
			diagnostics.lastHandCoverVertexCount = telemetry.coverVertexCount;

			if (stateChanged && diagnostics.handCoverTransitionLogs <
					kMaximumHandCoverTransitionLogs) {
				++diagnostics.handCoverTransitionLogs;
				logger::info(
					"[RR][HandMirrorReflection][cover-clip] transition route={} source={} main={} graphics={} mask=0x{:02X} valid={} finite={} positiveW(main/reflected/total)={}/{}/{} minimumW(main/reflected)={:.6f}/{:.6f}",
					route ? route : "unknown", receipt.sourceSequence,
					receipt.mainViewFrame, receipt.graphicsFrame,
					static_cast<unsigned>(riskMask),
					telemetry.valid, telemetry.finite,
					static_cast<unsigned>(telemetry.mainPositiveWVertices),
					static_cast<unsigned>(telemetry.reflectedPositiveWVertices),
					static_cast<unsigned>(telemetry.coverVertexCount),
					telemetry.minimumMainClipW,
					telemetry.minimumReflectedClipW);
			}
		}

		[[nodiscard]] std::uint64_t HashProjection(
			const DirectX::XMFLOAT4X4& projection) noexcept
		{
			constexpr std::uint64_t kOffset = UINT64_C(14695981039346656037);
			constexpr std::uint64_t kPrime = UINT64_C(1099511628211);
			std::uint64_t value = kOffset;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(&projection);
			for (std::size_t index = 0; index < sizeof(projection); ++index) {
				value ^= bytes[index];
				value *= kPrime;
			}
			return value;
		}

		void PollHandMirrorEvidenceEvent(
			CohortDiagnostics& diagnostics,
			const ReceiptPolicy::FrameReceipt& receipt,
			const MirrorPaneRenderer::DrawRequest& request,
			const MirrorPaneRenderer::DrawStatus status,
			const char* route)
		{
			if (!MirrorPaneRenderer::IsSuccessfulDraw(status))
				return;
			const HANDLE event =
				g_handMirrorEvidenceEvent.load(std::memory_order_acquire);
			if (!event)
				return;
			const DWORD waitStatus = WaitForSingleObject(event, 0);
			if (waitStatus == WAIT_TIMEOUT)
				return;
			if (waitStatus != WAIT_OBJECT_0) {
				++diagnostics.handCoverEvidenceWaitFailures;
				return;
			}

			++diagnostics.handCoverEvidenceSignals;
			if (diagnostics.handCoverEvidenceLogs >=
					kMaximumHandCoverEvidenceLogs) {
				return;
			}
			++diagnostics.handCoverEvidenceLogs;
			const auto telemetry =
				MirrorPaneRenderer::InspectHandCoverProjection(
					request.pane, request.mainView, request.frame);
			logger::info(
				"[RR][HandMirrorReflection][evidence-f10] signal={} log={}/{} route={} status={} source={} main={} graphics={} capture={} delivery={} cover(hand/valid/finite/positiveMain/positiveReflected/total/minimumMainW/minimumReflectedW)={}/{}/{}/{}/{}/{}/{:.6f}/{:.6f} projectionHash(main/reflected)=0x{:016X}/0x{:016X}",
				diagnostics.handCoverEvidenceSignals,
				diagnostics.handCoverEvidenceLogs,
				kMaximumHandCoverEvidenceLogs,
				route ? route : "unknown",
				MirrorPaneRenderer::ToString(status), receipt.sourceSequence,
				receipt.mainViewFrame, receipt.graphicsFrame,
				request.frame.captureSequence, request.deliveryFrame,
				telemetry.handAperture, telemetry.valid, telemetry.finite,
				static_cast<unsigned>(telemetry.mainPositiveWVertices),
				static_cast<unsigned>(telemetry.reflectedPositiveWVertices),
				static_cast<unsigned>(telemetry.coverVertexCount),
				telemetry.minimumMainClipW,
				telemetry.minimumReflectedClipW,
				HashProjection(request.mainView.viewProjection),
				HashProjection(request.frame.reflectedViewProjection));
		}

		void ExecuteDirectPaneDeliveryBody(
			DirectPaneDeliveryExecutionState& execution)
		{
			if (!HandMirrorSettings::ReflectInPose(execution.graphIndicatesRaisedPresentation)) {
				execution.bodyReturnedNormally = true;
				return;
			}
			auto& state = *execution.runtime;
			if (!StorePolicy::IsExactInternalHandPublication(
					execution.publication) ||
				execution.publication.physicalRole >= state.nativeSlots.size() ||
				!PublicationMatchesDeliverySurface(
					state, execution.publication, execution.paneSurface)) {
				execution.bodyReturnedNormally = true;
				return;
			}
			auto& slot = state.nativeSlots[execution.publication.physicalRole];
			if (!slot.valid ||
				!StorePolicy::SamePublicationSnapshot(
					slot.publication, execution.publication)) {
				execution.bodyReturnedNormally = true;
				return;
			}
			if (HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled()) {
				if (LastGoodPolicy::PresentationModeChanged(slot.presentationMode,
						LastGoodPolicy::ClassifyPresentationMode(
							execution.graphIndicatesRaisedPresentation))) {
					++state.diagnostics.livePosePublicationModeRejects;
					execution.bodyReturnedNormally = true;
					return;
				}
				if (LoweredPolicy::LoweredLastGoodStale(
						!execution.graphIndicatesRaisedPresentation,
						execution.publication.publication.attempt.surface.sourceSequence,
						execution.receipt.sourceSequence)) {
					++state.diagnostics.livePosePublicationAgeRejects;
					execution.bodyReturnedNormally = true;
					return;
				}
			}

			MirrorPaneRenderer::InitializationStatus initialization{};
			EnsureHandRenderer(state, execution.main.device, initialization);
			const auto pane = MakePaneTransform(
				execution.paneSurface,
				execution.publication.publication.attempt.owner);
			const auto capturedPane = MakePaneTransform(
				execution.publication.publication.attempt.surface,
				execution.publication.publication.attempt.owner);
			MirrorPaneRenderer::PaneTransform capturedBasis{};
			MirrorPaneRenderer::PaneTransform liveBasis{};
			SelectHandRetargetBases(capturedPane, pane, capturedBasis, liveBasis);
			MirrorPaneRenderer::PublishedFrame retargetedFrame{};
			if (!Presentation::RetargetReflectedProjectionToLivePane(
					slot.frame, capturedBasis, liveBasis, retargetedFrame)) {
				++state.diagnostics.directReflectedRetargetRejects;
				execution.drawStatus =
					MirrorPaneRenderer::DrawStatus::kProjectionRejected;
				execution.bodyReturnedNormally = true;
				return;
			}
			++state.diagnostics.directReflectedRetargets;
			// Retargeting preserves the pane-plane colour crop, but its inverse is an
			// affine pane-frame transform rather than a capture-world reconstruction.
			// Drop depth from this ephemeral copy so a future target/history plumbing
			// change cannot accidentally enable invalid reflected motion vectors.
			retargetedFrame.depthSRV.Reset();
			const bool affineRequested = execution.outerFinal &&
				execution.graphIndicatesRaisedPresentation &&
				execution.affineAuthorizedForCurrentPublication;
			// Neither the portrait nor the lowered reflected room follows the
			// main-scene motion underneath the pane. Keep current reflection pixels
			// instead of blending them with unrelated history during movement.
			// Only the outer-final draw may write the validated optional target.
			const bool rejectHistoryRequested = LoweredPolicy::RejectNativeTemporalHistory(
				HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled(),
				execution.outerFinal, execution.graphIndicatesRaisedPresentation);
			if (rejectHistoryRequested) {
				++state.diagnostics.temporalHistoryRejectRequests;
				if (!execution.graphIndicatesRaisedPresentation)
					++state.diagnostics.loweredTemporalHistoryRejectRequests;
			}
			execution.motionTargetAvailable = rejectHistoryRequested &&
				execution.retainedOuterFinalMotionCurrent &&
				HasExactRetainedMotionTarget(execution.retainedOuterFinalTarget);
			MirrorPaneRenderer::MotionHistory previousHistory{};
			ID3D11RenderTargetView* motionRTV = nullptr;
			std::uint64_t requestDeliveryFrame = 0;
			MirrorPaneRenderer::MotionMode motionMode =
				MirrorPaneRenderer::MotionMode::kDepthReconstructedWorld;
			if (rejectHistoryRequested) {
				motionMode = MirrorPaneRenderer::MotionMode::kRejectHistory;
				if (execution.motionTargetAvailable)
					motionRTV = execution.retainedOuterFinalTarget->motionRTV;
			}
			if (affineRequested) {
				requestDeliveryFrame = execution.receipt.graphicsFrame;
				if (execution.motionTargetAvailable) {
					// The raw pointer is borrowed synchronously from the retained owner;
					// staged.main never carries slot 7 across RenderFirstPersonView.
					motionRTV = execution.retainedOuterFinalTarget->motionRTV;
					execution.historyClassification = ClassifyHandAffineHistory(
						state.affineMotionHistory, execution.receipt,
						execution.paneSurface, pane, execution.main.viewport,
						retargetedFrame.captureSequence);
					RecordHandAffineHistoryClassification(
						state.diagnostics, execution.historyClassification);
					if (execution.historyClassification ==
						HandAffineHistoryReject::kEligible) {
						previousHistory = state.affineMotionHistory.renderer;
					} else if (execution.historyClassification !=
						HandAffineHistoryReject::kMissing) {
						ResetHandAffineHistory(
							state, HandAffineHistoryResetReason::kHistoryDrift);
					}
				}
			} else {
				if (state.deliveryFrame ==
					(std::numeric_limits<std::uint64_t>::max)()) {
					execution.bodyReturnedNormally = true;
					return;
				}
				requestDeliveryFrame = ++state.deliveryFrame;
			}
			const MirrorPaneRenderer::DrawRequest request{
				.frame = std::move(retargetedFrame),
				.pane = pane,
				.mainView = { execution.main.viewProjection,
					execution.main.origin },
				.targets = { execution.main.colorRTV, motionRTV,
					execution.main.depthDSV, execution.main.viewport },
				.motionHistory = previousHistory,
				.motionMode = motionMode,
				.deliveryFrame = requestDeliveryFrame
			};
			bool motionOutputEnabled = false;
			if (execution.outerFinal) {
				ObserveHandCoverProjection(
					state.diagnostics, execution.receipt, request, "live");
			}
			const auto liveCoverage =
				MirrorPaneRenderer::ClassifyProjectionCoverage(
					request.pane, request.mainView, request.frame);
			// Partial hand coverage writes only part of the aperture and exposes the
			// opaque authored fallback underneath. At the sole outer-final writer it
			// is therefore a miss, not a successful presentation.
			execution.drawStatus = execution.outerFinal &&
				liveCoverage != MirrorPaneRenderer::DrawStatus::kDrawn ?
				liveCoverage :
				(state.renderer.Ready() ?
					DrawHandPane(state.renderer,
						execution.main, request, &motionOutputEnabled) :
					MirrorPaneRenderer::DrawStatus::kNotInitialized);
			const bool liveDrawSucceeded = execution.outerFinal ?
				execution.drawStatus == MirrorPaneRenderer::DrawStatus::kDrawn :
				MirrorPaneRenderer::IsSuccessfulDraw(execution.drawStatus);
			if (liveDrawSucceeded) {
				if (!CommitLastGoodPresentation(
						state, execution.publication, request.frame,
						execution.paneSurface,
						request.mainView, execution.main.viewport,
						execution.retainedOuterFinalTarget,
						execution.main.device,
						execution.main.context, execution.drawStatus,
						execution.outerFinal,
						execution.graphIndicatesRaisedPresentation)) {
					execution.drawStatus = MirrorPaneRenderer::DrawStatus::
						kContextStateRestoreFailed;
					execution.bodyReturnedNormally = true;
					return;
				}
				if (execution.outerFinal) {
					PollHandMirrorEvidenceEvent(
						state.diagnostics, execution.receipt, request,
						execution.drawStatus, "live");
				}
				execution.affineMotionOutputEnabled = motionOutputEnabled;
				if (rejectHistoryRequested && motionOutputEnabled)
					++state.diagnostics.temporalHistoryRejectWrites;
				if (affineRequested) {
					execution.committedHistory = {
						.mainViewProjection = request.mainView.viewProjection,
						.reflectedViewProjection =
							request.frame.reflectedViewProjection,
						.mainOrigin = request.mainView.origin,
						.reflectedOrigin = request.frame.reflectedOrigin,
						.pane = request.pane,
						.captureSequence = request.frame.captureSequence,
						.deliveryFrame = execution.receipt.graphicsFrame,
						.valid = true
					};
				}
				(void)CaptureStableHandPresentation(
					state.stablePresentation, pane, request.mainView,
					execution.main.viewport);
			} else if (execution.drawStatus ==
					MirrorPaneRenderer::DrawStatus::kProjectionMainViewNotVisible ||
				execution.drawStatus ==
					MirrorPaneRenderer::DrawStatus::kProjectionReflectedUncovered ||
				execution.drawStatus ==
					MirrorPaneRenderer::DrawStatus::kDrawnPartialCoverage) {
				const auto originalProjectionMiss = execution.drawStatus;
				++state.diagnostics.stablePresentationRetryAttempts;
				if (originalProjectionMiss == MirrorPaneRenderer::DrawStatus::
						kProjectionMainViewNotVisible) {
					++state.diagnostics.stablePresentationRetryMainViewNotVisible;
				} else if (originalProjectionMiss ==
					MirrorPaneRenderer::DrawStatus::
						kProjectionReflectedUncovered) {
					++state.diagnostics.stablePresentationRetryReflectedUncovered;
				}
				MirrorPaneRenderer::MainView stableMain{};
				if (BuildStableHandPresentationMainView(
						state.stablePresentation, pane, execution.main.viewport,
						stableMain)) {
						auto stableRequest = request;
						stableRequest.mainView = stableMain;
						bool stableMotionOutputEnabled = false;
						if (execution.outerFinal) {
							ObserveHandCoverProjection(
								state.diagnostics, execution.receipt,
								stableRequest, "stable-retry");
						}
						const auto stableCoverage =
							MirrorPaneRenderer::ClassifyProjectionCoverage(
								stableRequest.pane, stableRequest.mainView,
								stableRequest.frame);
						execution.drawStatus = execution.outerFinal &&
							stableCoverage !=
								MirrorPaneRenderer::DrawStatus::kDrawn ?
							stableCoverage :
							DrawHandPane(state.renderer,
								execution.main, stableRequest,
								&stableMotionOutputEnabled);
						const bool stableDrawSucceeded = execution.outerFinal ?
							execution.drawStatus ==
								MirrorPaneRenderer::DrawStatus::kDrawn :
							MirrorPaneRenderer::IsSuccessfulDraw(
								execution.drawStatus);
						if (stableDrawSucceeded) {
							if (!CommitLastGoodPresentation(
									state, execution.publication,
									stableRequest.frame,
									execution.paneSurface,
									stableRequest.mainView,
									execution.main.viewport,
									execution.retainedOuterFinalTarget,
									execution.main.device,
									execution.main.context,
									execution.drawStatus,
									execution.outerFinal,
									execution.graphIndicatesRaisedPresentation)) {
								execution.drawStatus = MirrorPaneRenderer::
									DrawStatus::kContextStateRestoreFailed;
								execution.bodyReturnedNormally = true;
								return;
							}
							if (execution.outerFinal) {
								PollHandMirrorEvidenceEvent(
									state.diagnostics, execution.receipt,
									stableRequest, execution.drawStatus,
									"stable-retry");
							}
							++state.diagnostics.stablePresentationRetrySuccesses;
							execution.affineMotionOutputEnabled =
								stableMotionOutputEnabled;
							if (affineRequested) {
								execution.committedHistory = {
									.mainViewProjection =
										stableRequest.mainView.viewProjection,
									.reflectedViewProjection =
										stableRequest.frame.reflectedViewProjection,
									.mainOrigin = stableRequest.mainView.origin,
									.reflectedOrigin =
										stableRequest.frame.reflectedOrigin,
									.pane = stableRequest.pane,
									.captureSequence =
										stableRequest.frame.captureSequence,
									.deliveryFrame = execution.receipt.graphicsFrame,
									.valid = true
								};
							}
						}
					}
				const bool retrySucceeded = execution.outerFinal ?
					execution.drawStatus == MirrorPaneRenderer::DrawStatus::kDrawn :
					MirrorPaneRenderer::IsSuccessfulDraw(execution.drawStatus);
				if (!retrySucceeded)
					++state.diagnostics.stablePresentationRetryFailures;
			}
			execution.bodyReturnedNormally = true;
		}

		[[nodiscard]] bool TryReplayLastGoodPresentation(
			RuntimeState& state,
			const Bridge::MovingSurfaceSample* currentSurface,
			const MirrorPaneDelivery::CurrentMainDrawState& main,
			const LiveReturn::LiveDeliveryTargetIdentity& currentTarget,
			const MirrorPaneDelivery::RetainedMainTargetHandle&
				currentRetainedTarget,
			const bool targetReplacementProven,
			const LastGoodPolicy::PresentationMode livePresentationMode,
			MirrorPaneRenderer::DrawStatus& outputStatus,
			const bool outerFinal = false) noexcept
		{
			outputStatus = MirrorPaneRenderer::DrawStatus::kNoPublishedFrame;
			if (!HandMirrorSettings::ReflectInPose(
					livePresentationMode == LastGoodPolicy::PresentationMode::kRaised))
				return ClearLastGoodPresentation(state);
			EquippedPresentationIdentityCopy equipped{};
			const auto equippedStatus =
				HandMirrorApprovedContentReadOnlyObserver::
					RunWithEquippedPresentationIdentity(
						&CopyEquippedPresentationIdentity, &equipped);
			using EquippedStatus = HandMirrorApprovedContentReadOnlyObserver::
				RuntimeEquippedPresentationStatus;
			if (equippedStatus == EquippedStatus::kFaulted)
				return false;
			const bool exactEquipped =
				equippedStatus == EquippedStatus::kExactEquipped &&
				equipped.copied && equipped.identity.valid;
			const bool exactOwner = exactEquipped &&
				equipped.identity.valid && LastGoodPresentationValid(state) &&
				SameEquippedPresentationOwner(
					state.lastGoodPresentation.frame.owner,
					equipped.identity.owner);
			const auto equipmentEvidence = equippedStatus ==
					EquippedStatus::kPositivelyUnequipped ?
				LastGoodPolicy::EquipmentEvidence::kPositivelyUnequipped :
				(exactEquipped ? LastGoodPolicy::EquipmentEvidence::kExactEquipped :
					LastGoodPolicy::EquipmentEvidence::kIndeterminate);
			const bool targetAvailable = main.valid && currentRetainedTarget &&
				ReturnedPresentationComplete(main) &&
				LiveReturn::IsValidLiveDeliveryTarget(currentTarget) &&
				MakeLiveDeliveryTargetIdentity(currentRetainedTarget) == currentTarget &&
				main.device == currentRetainedTarget->device &&
				main.context == currentRetainedTarget->context &&
				main.colorRTV == currentRetainedTarget->colorRTV &&
				main.depthDSV == currentRetainedTarget->depthDSV;
			const bool targetIdentityUnchanged = targetAvailable &&
				SameLastGoodPresentationTarget(
					state.lastGoodPresentation.target, currentTarget) &&
				SameViewportBits(
					state.lastGoodPresentation.viewport, main.viewport);
			const auto decision = LastGoodPolicy::EvaluateReplay({
				.lastGoodValid = LastGoodPresentationValid(state),
				.lifecycleStable = !HandLifecycleTransitionSuspended(),
				.equipment = equipmentEvidence,
				.exactOwnerUnchanged = exactOwner,
				.presentationModeChanged =
					LastGoodPolicy::PresentationModeChanged(
						state.lastGoodPresentation.presentationMode,
						livePresentationMode),
				.deviceUnchanged = main.device &&
					state.lastGoodPresentation.deviceIdentity ==
						reinterpret_cast<std::uintptr_t>(main.device),
				.liveTargetAvailable = targetAvailable,
				.targetIdentityUnchanged = targetIdentityUnchanged,
				.targetReplacementProven = targetReplacementProven,
				.requireCurrentPose =
					HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled(),
				.currentPoseAvailable = currentSurface &&
					Bridge::IsValidMovingSurfaceSample(*currentSurface) &&
					livePresentationMode != LastGoodPolicy::PresentationMode::kUnknown
			});
			state.diagnostics.lastReplayDecision = static_cast<int>(decision);
			if (currentSurface &&
				(decision == LastGoodPolicy::ReplayDecision::kReplay ||
				 decision == LastGoodPolicy::ReplayDecision::kReplayToReplacement) &&
				LoweredPolicy::LoweredLastGoodStale(
					livePresentationMode ==
						LastGoodPolicy::PresentationMode::kLowered,
					state.lastGoodPresentation.committedSourceSequence,
					currentSurface->sourceSequence)) {
				// A lowered pane shows the live room or nothing.  An aged cached
				// frame reads as a frozen image, so drop it and let the authored
				// surface show until the next fresh capture lands.
				if (!ClearLastGoodPresentation(state))
					return false;
				++state.diagnostics.lastGoodReplayLoweredStaleRejects;
				return true;
			}
			const bool replayToReplacement = decision ==
				LastGoodPolicy::ReplayDecision::kReplayToReplacement;
			if (decision != LastGoodPolicy::ReplayDecision::kReplay &&
				!replayToReplacement) {
				if (decision == LastGoodPolicy::ReplayDecision::kLifecycleTransition ||
					decision == LastGoodPolicy::ReplayDecision::kMirrorNotEquipped ||
					decision == LastGoodPolicy::ReplayDecision::kOwnerChanged ||
					decision == LastGoodPolicy::ReplayDecision::kDeviceChanged) {
					if (!ClearLastGoodPresentation(state))
						return false;
					++state.diagnostics.lastGoodReplayIdentityRejects;
				} else if (decision ==
					LastGoodPolicy::ReplayDecision::kPresentationModeChanged) {
					// The live pane changed optical mode (raised portrait window vs
					// lowered physical reflection).  A last-good frame belongs to one
					// mode only; drop it so a lowered pane paints Skyrim's authored
					// surface instead of a stale glued portrait, and the next raised
					// hold starts from its own first complete draw.
					if (!ClearLastGoodPresentation(state))
						return false;
					++state.diagnostics.lastGoodReplayPresentationModeRejects;
				} else if (decision ==
					LastGoodPolicy::ReplayDecision::kTargetUnavailable) {
					++state.diagnostics.lastGoodReplayTargetRejects;
				} else if (decision == LastGoodPolicy::ReplayDecision::kLivePoseUnavailable) {
					++state.diagnostics.livePoseReplayMissingPoseRejects;
				}
				return true;
			}

			auto& cached = state.lastGoodPresentation;
			const auto replayOwner = exactEquipped ?
				Bridge::MakeHandOwner(equipped.identity.owner) :
				cached.frame.owner;
			if (currentSurface &&
				(!Bridge::IsValidMovingSurfaceSample(*currentSurface) ||
				 !SameEquippedPresentationOwner(
					 Bridge::MakeHandOwner(currentSurface->owner),
					 replayOwner.hand))) {
				++state.diagnostics.lastGoodReplayIdentityRejects;
				return true;
			}

			const auto cachedPane = MakePaneTransform(
				cached.liveSurface, replayOwner);
			auto replayPane = cachedPane;
			auto replayFrame = cached.frame;
			replayFrame.owner = replayOwner;
			auto replayMain = cached.mainView;
			bool replayUsesDetachedMapping = currentSurface == nullptr;
			if (currentSurface) {
				const auto livePane =
					MakePaneTransform(*currentSurface, replayOwner);
				MirrorPaneRenderer::PublishedFrame liveFrame{};
				const MirrorPaneRenderer::MainView liveMain{
					main.viewProjection, main.origin };
				auto liveStatus = MirrorPaneRenderer::DrawStatus::
					kProjectionRejected;
				MirrorPaneRenderer::PaneTransform cachedBasis{};
				MirrorPaneRenderer::PaneTransform liveBasis{};
				SelectHandRetargetBases(
					cachedPane, livePane, cachedBasis, liveBasis);
				bool liveMappingReady =
					Presentation::RetargetReflectedProjectionToLivePane(
						replayFrame, cachedBasis, liveBasis, liveFrame);
				if (liveMappingReady) {
					liveStatus = MirrorPaneRenderer::ClassifyProjectionCoverage(
						livePane, liveMain, liveFrame);
					liveMappingReady = liveStatus ==
						MirrorPaneRenderer::DrawStatus::kDrawn;
				}
				if (!liveMappingReady) {
					// Boundary-equal affine retargeting can lose a last row of
					// fragments to floating-point clipping. Map the live aperture
					// into a one-percent inset of the already coverage-valid source
					// before giving up; this remains a current-aperture draw.
					auto insetCapturedPane = cachedPane;
					constexpr float kReplayCoverageInset = 0.99F;
					insetCapturedPane.tangentExtent.x *= kReplayCoverageInset;
					insetCapturedPane.tangentExtent.y *= kReplayCoverageInset;
					insetCapturedPane.tangentExtent.z *= kReplayCoverageInset;
					insetCapturedPane.bitangentExtent.x *= kReplayCoverageInset;
					insetCapturedPane.bitangentExtent.y *= kReplayCoverageInset;
					insetCapturedPane.bitangentExtent.z *= kReplayCoverageInset;
					MirrorPaneRenderer::PaneTransform insetCapturedBasis{};
					SelectHandRetargetBases(
						insetCapturedPane, livePane, insetCapturedBasis, liveBasis);
					liveFrame = {};
					liveMappingReady =
						Presentation::RetargetReflectedProjectionToLivePane(
							replayFrame, insetCapturedBasis, liveBasis, liveFrame);
					if (liveMappingReady) {
						liveStatus =
							MirrorPaneRenderer::ClassifyProjectionCoverage(
								livePane, liveMain, liveFrame);
						liveMappingReady = liveStatus ==
							MirrorPaneRenderer::DrawStatus::kDrawn;
					}
				}
				if (!liveMappingReady) {
					if (HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled()) {
						++state.diagnostics.lastGoodReplayCoverageRejects;
						outputStatus = liveStatus;
						return true;
					}
					// The live retarget cannot replace the complete cached mapping
					// unless it owns the full visible aperture. Retain the detached
					// last-good pane/main mapping for this destination instead of
					// exposing Skyrim's opaque fallback for one frame. Equipment,
					// owner, lifecycle, device, and target were all re-proved above.
					++state.diagnostics.lastGoodReplayCoverageRejects;
					++state.diagnostics.
						lastGoodReplayDetachedCoverageFallbacks;
					replayUsesDetachedMapping = true;
				} else {
					replayPane = livePane;
					replayFrame = std::move(liveFrame);
					replayMain = liveMain;
				}
			}
			replayFrame.depthSRV.Reset();
			if (!state.renderer.Ready() ||
				state.renderer.ActiveSamplingMode() != SelectHandSamplingMode()) {
				MirrorPaneRenderer::InitializationStatus initialization{};
				EnsureHandRenderer(state, main.device, initialization);
			}
			if (state.deliveryFrame ==
					(std::numeric_limits<std::uint64_t>::max)()) {
				return false;
			}
			MirrorPaneDelivery::RetainedMainTargetHandle replayMotionTarget{};
			const bool rejectHistory = outerFinal && currentSurface && !replayUsesDetachedMapping &&
				HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled();
			if (rejectHistory) {
				++state.diagnostics.temporalHistoryRejectRequests;
				if (livePresentationMode == LastGoodPolicy::PresentationMode::kLowered)
					++state.diagnostics.loweredTemporalHistoryRejectRequests;
				MirrorPaneDelivery::CurrentMainDrawState currentMain{};
				if (QueryHandDrawState(currentMain) ==
						MirrorPaneDelivery::CurrentMainDrawStateStatus::kReady &&
					currentMain.mainWorldFrame == main.mainWorldFrame &&
					currentMain.graphicsFrame == main.graphicsFrame &&
					SecondView::CurrentMainWorldSourceSequence() == currentSurface->sourceSequence) {
					(void)MirrorPaneDelivery::TryAttachCurrentOptionalMotionTarget(
						currentMain, currentRetainedTarget, replayMotionTarget);
				}
				if (MirrorPaneDelivery::Faulted())
					return false;
			}
			const MirrorPaneRenderer::DrawRequest request{
				.frame = std::move(replayFrame),
				.pane = replayPane,
				.mainView = replayMain,
				.targets = { main.colorRTV,
					replayMotionTarget && HasExactRetainedMotionTarget(replayMotionTarget) ?
						replayMotionTarget->motionRTV : nullptr,
					main.depthDSV, main.viewport },
				.motionHistory = {},
				.motionMode = rejectHistory ? MirrorPaneRenderer::MotionMode::kRejectHistory :
					MirrorPaneRenderer::MotionMode::kPaneAffine,
				.deliveryFrame = ++state.deliveryFrame
			};
			++state.diagnostics.lastGoodReplayAttempts;
			outputStatus = MirrorPaneRenderer::ClassifyProjectionCoverage(
				request.pane, request.mainView, request.frame);
			if (outputStatus != MirrorPaneRenderer::DrawStatus::kDrawn) {
				++state.diagnostics.lastGoodReplayCoverageRejects;
				state.diagnostics.lastDirectDrawStatus = outputStatus;
				return true;
			}
			++state.diagnostics.directDrawAttempts;
			bool motionOutput = false;
			outputStatus = state.renderer.Ready() ?
				DrawHandPane(state.renderer, main, request, &motionOutput) :
				MirrorPaneRenderer::DrawStatus::kNotInitialized;
			if (rejectHistory && motionOutput)
				++state.diagnostics.temporalHistoryRejectWrites;
			state.diagnostics.lastDirectDrawStatus = outputStatus;
			if (outputStatus == MirrorPaneRenderer::DrawStatus::kDrawn) {
				++state.diagnostics.lastGoodReplayDraws;
				++state.diagnostics.directDrawSuccesses;
				(void)CaptureStableHandPresentation(
					state.stablePresentation, request.pane, request.mainView,
					main.viewport);
				// The detached pixels remain the same complete frame. A successful draw
				// proves the current destination, but ordinary coverage proved only its
				// main-visible subset. Refresh the cached pane mapping only under the same
				// full-aperture invariant used for a new detached snapshot.
				const bool refreshHasFullAperture =
					MirrorPaneRenderer::HasFullHandReflectedApertureCoverage(
						request.pane, request.frame);
				if (refreshHasFullAperture && !replayUsesDetachedMapping) {
					auto refreshedFrame = request.frame;
					refreshedFrame.owner = cached.frame.owner;
					cached.frame = std::move(refreshedFrame);
					if (currentSurface) {
						auto refreshedSurface = *currentSurface;
						LastGoodPolicy::NormalizeReplaySurfaceGeneration(
							refreshedSurface, cached.liveSurface);
						cached.liveSurface = std::move(refreshedSurface);
					}
					cached.mainView = request.mainView;
				} else if (!refreshHasFullAperture) {
					++state.diagnostics.lastGoodReplayRefreshCoverageRejects;
				}
				cached.viewport = main.viewport;
				cached.target = currentTarget;
				auto priorRetainedTarget = std::move(cached.retainedTarget);
				cached.retainedTarget = currentRetainedTarget;
				priorRetainedTarget.reset();
				if (MirrorPaneDelivery::Faulted())
					return false;
			} else if (outputStatus ==
					MirrorPaneRenderer::DrawStatus::kProjectionMainViewNotVisible ||
				outputStatus == MirrorPaneRenderer::DrawStatus::
					kProjectionReflectedUncovered ||
				outputStatus == MirrorPaneRenderer::DrawStatus::
					kDrawnPartialCoverage) {
				++state.diagnostics.lastGoodReplayCoverageRejects;
				++state.diagnostics.directDrawProjectionMisses;
			}
			return outputStatus !=
				MirrorPaneRenderer::DrawStatus::kContextStateRestoreFailed;
		}

		__declspec(noinline) void ExecuteDirectPaneDeliveryWithReaderFinally(
			DirectPaneDeliveryExecutionState& execution) noexcept
		{
			if (!execution.runtime ||
				execution.runtime->activeInternalReaders ==
					(std::numeric_limits<std::uint32_t>::max)()) {
				return;
			}
			++execution.runtime->activeInternalReaders;
			__try {
				__try {
					ExecuteDirectPaneDeliveryBody(execution);
				} __finally {
					if (execution.runtime->activeInternalReaders != 0)
						--execution.runtime->activeInternalReaders;
					if (execution.runtime->activeInternalReaders == 0 &&
						execution.runtime->terminalRendererScrubPending) {
						(void)ShutdownRendererSEH(execution.runtime->renderer);
						execution.runtime->terminalRendererScrubPending = false;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				execution.exceptionCaught = true;
			}
		}

		enum class RetainedTargetPresentationRoute : std::uint8_t
		{
			kExactPaneReturn,
			kPostResolve
		};

		struct RetainedTargetPresentationResult
		{
			MirrorPaneRenderer::DrawStatus status{
				MirrorPaneRenderer::DrawStatus::kNoPublishedFrame };
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
			std::uint64_t captureSequence{ 0 };
#endif
			bool freshAttempted{ false };
			bool freshDrawn{ false };
			bool replayAttempted{ false };
			bool replayDrawn{ false };
		};

		/**
		 * Submit the newest exact publication, or the detached last-good image, to
		 * one already-authorized retained first-person target.  Both the immediate
		 * pane-return writer and the post-native post-resolve writer use this single
		 * implementation; only the latter is a full-coverage final writer.
		 */
		[[nodiscard]] bool PresentFreshOrLastGoodToRetainedTarget(
			RuntimeState& state,
			const Bridge::MovingSurfaceSample& surface,
			const ReceiptPolicy::FrameReceipt& receipt,
			const Bridge::PrivateTargetIdentity& captureReceiptTarget,
			const MirrorPaneDelivery::CurrentMainDrawState& main,
			const MirrorPaneDelivery::RetainedMainTargetHandle& retainedTarget,
			const LiveReturn::LiveDeliveryTargetIdentity& liveTarget,
			const bool graphIndicatesRaisedPresentation,
			const RetainedTargetPresentationRoute route,
			RetainedTargetPresentationResult& result) noexcept
		{
			result = {};
			if (!HandMirrorSettings::ReflectInPose(graphIndicatesRaisedPresentation))
				return ClearLastGoodPresentation(state);
			if (!Bridge::IsValidMovingSurfaceSample(surface) ||
				!ReceiptPolicy::IsValidFrameReceipt(receipt) || !main.valid ||
				!ReturnedPresentationComplete(main) || !retainedTarget ||
				!LiveReturn::IsValidLiveDeliveryTarget(liveTarget) ||
				MakeLiveDeliveryTargetIdentity(retainedTarget) != liveTarget ||
				main.device != retainedTarget->device ||
				main.context != retainedTarget->context ||
				main.colorRTV != retainedTarget->colorRTV ||
				main.depthDSV != retainedTarget->depthDSV) {
				return true;
			}

			const bool postResolve =
				route == RetainedTargetPresentationRoute::kPostResolve;
			const auto publication = DeliveryPublicationForSurface(
				state, surface, captureReceiptTarget);
			const bool samePublishedPane = publication.valid &&
				StorePolicy::IsExactInternalHandPublication(publication) &&
				PublicationMatchesDeliverySurface(state, publication, surface) &&
				CleanMissContinuityTargetDisjoint(
					state, publication, surface, captureReceiptTarget);
			if (samePublishedPane) {
				DirectPaneDeliveryExecutionState execution{
					.runtime = &state,
					.publication = publication,
					.paneSurface = surface,
					.main = main,
					.retainedOuterFinalTarget = postResolve ?
						retainedTarget :
						MirrorPaneDelivery::RetainedMainTargetHandle{},
					.receipt = receipt,
					.outerFinal = postResolve,
					.graphIndicatesRaisedPresentation =
						graphIndicatesRaisedPresentation,
					// The later enclosing-return route remains the sole motion-history
					// owner. This seam is a color-continuity final writer only.
					.affineAuthorizedForCurrentPublication = false,
					.retainedOuterFinalMotionCurrent = false
				};
				const auto previousStatus = state.diagnostics.lastDirectDrawStatus;
				ExecuteDirectPaneDeliveryWithReaderFinally(execution);
				result.freshAttempted = true;
				result.status = execution.drawStatus;
				++state.diagnostics.directDrawAttempts;
				state.diagnostics.lastDirectDrawStatus = execution.drawStatus;
				if (postResolve)
					++state.diagnostics.postResolveFreshDrawAttempts;
				else
					++state.diagnostics.exactActualOmFreshDrawAttempts;

				const bool successful = postResolve ?
					execution.drawStatus == MirrorPaneRenderer::DrawStatus::kDrawn :
					MirrorPaneRenderer::IsSuccessfulDraw(execution.drawStatus);
				if (successful) {
					result.freshDrawn = true;
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
					result.captureSequence =
						execution.publication.publication.captureSequence;
#endif
					++state.diagnostics.directDrawSuccesses;
					if (postResolve)
						++state.diagnostics.postResolveFreshDrawSuccesses;
					else
						++state.diagnostics.exactActualOmFreshDrawSuccesses;
				} else if (execution.drawStatus ==
						MirrorPaneRenderer::DrawStatus::kProjectionMainViewNotVisible ||
					execution.drawStatus ==
						MirrorPaneRenderer::DrawStatus::kProjectionReflectedUncovered) {
					++state.diagnostics.directDrawProjectionMisses;
				}

				const auto routeAttempts = postResolve ?
					state.diagnostics.postResolveFreshDrawAttempts :
					state.diagnostics.exactActualOmFreshDrawAttempts;
				const auto routeSuccesses = postResolve ?
					state.diagnostics.postResolveFreshDrawSuccesses :
					state.diagnostics.exactActualOmFreshDrawSuccesses;
				if (routeAttempts == 1 || previousStatus != execution.drawStatus) {
					const auto restoreTelemetry =
						MirrorPaneRenderer::GetRestoreFaultTelemetry();
					logger::info(
						"[RR][HandMirrorReflection] {} direct draw status={} source={} main={} graphics={} attempts={} successes={} restoreFault(count/step/code)={}/{}/{:#x} first(kind/index/name/code/pointer/module)={}/{}/{}/{:#x}/{:#x}/{:#x} masks(release/step)={:#x}/{:#x}",
						postResolve ? "post-resolve retained-target" :
							"exact-pane actual-om",
						MirrorPaneRenderer::ToString(execution.drawStatus),
						receipt.sourceSequence, receipt.mainViewFrame,
						receipt.graphicsFrame, routeAttempts, routeSuccesses,
						restoreTelemetry.faults, restoreTelemetry.lastStep,
						restoreTelemetry.lastExceptionCode,
						restoreTelemetry.firstKind, restoreTelemetry.firstIndex,
						restoreTelemetry.firstKind == 1 ?
							MirrorPaneRenderer::ToString(
								static_cast<MirrorPaneRenderer::SnapshotOrdinal>(
									restoreTelemetry.firstIndex)) :
							"-",
						restoreTelemetry.firstExceptionCode,
						restoreTelemetry.firstPointer,
						restoreTelemetry.firstPointerModule,
						restoreTelemetry.releaseOrdinalMask,
						restoreTelemetry.restoreStepMask);
				}
				if (!execution.bodyReturnedNormally || execution.exceptionCaught ||
					state.activeInternalReaders != 0 ||
					execution.drawStatus == MirrorPaneRenderer::DrawStatus::
						kContextStateRestoreFailed) {
					return false;
				}
			}

			if (!result.freshDrawn && LastGoodPresentationValid(state)) {
				const auto previousStatus = state.diagnostics.lastDirectDrawStatus;
				MirrorPaneRenderer::DrawStatus replayStatus{};
				result.replayAttempted = true;
				if (postResolve)
					++state.diagnostics.postResolveLastGoodReplayCalls;
				else
					++state.diagnostics.exactActualOmLastGoodReplayCalls;
				if (!TryReplayLastGoodPresentation(
						state, &surface, main, liveTarget, retainedTarget, true,
						LastGoodPolicy::ClassifyPresentationMode(
							graphIndicatesRaisedPresentation),
						replayStatus)) {
					return false;
				}
				result.status = replayStatus;
				if (replayStatus == MirrorPaneRenderer::DrawStatus::kDrawn) {
					result.replayDrawn = true;
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
					result.captureSequence =
						state.lastGoodPresentation.frame.captureSequence;
#endif
					if (postResolve)
						++state.diagnostics.postResolveLastGoodReplayDraws;
					else
						++state.diagnostics.exactActualOmLastGoodReplayDraws;
				}
				const auto routeCalls = postResolve ?
					state.diagnostics.postResolveLastGoodReplayCalls :
					state.diagnostics.exactActualOmLastGoodReplayCalls;
				const auto routeDraws = postResolve ?
					state.diagnostics.postResolveLastGoodReplayDraws :
					state.diagnostics.exactActualOmLastGoodReplayDraws;
				if (routeCalls == 1 || previousStatus != replayStatus) {
					logger::info(
						"[RR][HandMirrorReflection] {} last-good replay status={} source={} main={} graphics={} calls={} draws={}",
						postResolve ? "post-resolve retained-target" :
							"exact-pane actual-om",
						MirrorPaneRenderer::ToString(replayStatus),
						receipt.sourceSequence, receipt.mainViewFrame,
						receipt.graphicsFrame, routeCalls, routeDraws);
				}
			}

			{
				static std::atomic<std::uint32_t> interleaveDiagBudget{ 160 };
				const bool interleaveActive =
					state.visibility.raisedPresentation &&
					(state.diagnostics.interleaveHandSources +
						state.diagnostics.interleaveWallSources) != 0;
				auto budget = interleaveDiagBudget.load(std::memory_order_acquire);
				if (interleaveActive && budget != 0 &&
					interleaveDiagBudget.compare_exchange_strong(
						budget, budget - 1, std::memory_order_acq_rel)) {
					try {
						const auto& capturedCenter =
							publication.publication.attempt.surface.pose.geometry.center;
						const auto& liveCenter = surface.pose.geometry.center;
						logger::info(
							"[RR][HandMirror][interleave-present] route={} source={} owner={} pubValid={} samePane={} fresh(attempted/drawn/status)={}/{}/{} replay(attempted/drawn/decision/status)={}/{}/{}/{} lastGood={} held={} pubSource={} captured=({:.0f},{:.0f},{:.0f}) live=({:.0f},{:.0f},{:.0f})",
							postResolve ? "post-resolve" : "exact-pane",
							surface.sourceSequence,
							static_cast<int>(state.visibility.currentOwner),
							publication.valid, samePublishedPane,
							result.freshAttempted, result.freshDrawn,
							MirrorPaneRenderer::ToString(result.freshAttempted ?
								state.diagnostics.lastDirectDrawStatus : result.status),
							result.replayAttempted, result.replayDrawn,
							state.diagnostics.lastReplayDecision,
							MirrorPaneRenderer::ToString(result.status),
							LastGoodPresentationValid(state),
							state.heldPredecessorRetirement !=
								StorePolicy::RetirementReceipt{},
							publication.publication.attempt.surface.sourceSequence,
							capturedCenter.x, capturedCenter.y, capturedCenter.z,
							liveCenter.x, liveCenter.y, liveCenter.z);
					} catch (...) {
					}
				}
			}
			if (postResolve) {
				state.diagnostics.lastPostResolveDrawStatus = result.status;
				if (result.freshDrawn || result.replayDrawn)
					++state.diagnostics.postResolvePresentations;
			}
			return true;
		}

		void FinalizeOuterFinalAffineHistory(
			RuntimeState& state,
			const DirectPaneDeliveryExecutionState& execution) noexcept
		{
			if (!execution.outerFinal)
				return;
			if (!execution.graphIndicatesRaisedPresentation) {
				ResetHandAffineHistory(
					state, HandAffineHistoryResetReason::kLoweredPresentation);
				return;
			}
			if (!execution.affineAuthorizedForCurrentPublication) {
				ResetHandAffineHistory(
					state, HandAffineHistoryResetReason::kNoncurrentPublication);
				return;
			}
			if (!MirrorPaneRenderer::IsSuccessfulDraw(execution.drawStatus) ||
				!execution.committedHistory.valid) {
				ResetHandAffineHistory(
					state, HandAffineHistoryResetReason::kDrawFailure);
				return;
			}
			if (!execution.motionTargetAvailable) {
				ResetHandAffineHistory(
					state,
					HandAffineHistoryResetReason::kMotionTargetUnavailableOrInvalid);
				return;
			}
			if (execution.historyClassification ==
					HandAffineHistoryReject::kEligible &&
				!execution.affineMotionOutputEnabled) {
				++state.diagnostics.affineMotionWriteRejects;
				ResetHandAffineHistory(
					state,
					HandAffineHistoryResetReason::kMotionTargetUnavailableOrInvalid);
				return;
			}
			if (execution.affineMotionOutputEnabled)
				++state.diagnostics.affineMotionWritten;
			CommitHandAffineHistory(
				state, execution.receipt, execution.paneSurface,
				execution.main.viewport, execution.committedHistory);
		}

		[[nodiscard]] bool ScrubNativeSlotsForFailStop(
			RuntimeState& state) noexcept
		{
			std::array<ID3D11ShaderResourceView*,
				StorePolicy::kPhysicalRoleCapacity * 2> detached{};
			std::size_t cursor = 0;
			for (auto& slot : state.nativeSlots) {
				detached[cursor++] = slot.frame.colorSRV.Detach();
				detached[cursor++] = slot.frame.depthSRV.Detach();
				// No ComPtr remains in the slot, so the value scrub below performs no
				// native call and cannot interrupt authorization removal.
				slot.frame = {};
				slot.publication = {};
				slot.valid = false;
			}
			bool releaseFaulted = false;
			for (auto*& view : detached)
				ReleaseD3DInterfaceSEH(view, releaseFaulted);
			return !releaseFaulted;
		}

		bool ShutdownRendererSEH(
			MirrorPaneRenderer::Renderer& renderer) noexcept
		{
			bool released = false;
			__try {
				released = renderer.ShutdownSafely();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				released = false;
			}
			return released;
		}

		void FailStopInternal() noexcept
		{
			if (g_faulted.exchange(true, std::memory_order_acq_rel))
				return;
			LogStoreAuditNoexcept(State(), "fail-stop", 0);
			try {
				// Symbol resolution took four seconds on the render thread in the
				// September 11 ENB run. Keep bounded raw frames for offline PDB
				// analysis; never load symbols while the game is rendering.
				std::array<void*, 16> trace{};
				const auto count = CaptureStackBackTrace(
					1, static_cast<DWORD>(trace.size()), trace.data(), nullptr);
				for (USHORT index = 0; index < count; ++index) {
					MEMORY_BASIC_INFORMATION memory{};
					(void)VirtualQuery(trace[index], &memory, sizeof(memory));
					const auto address = reinterpret_cast<std::uintptr_t>(trace[index]);
					const auto base = reinterpret_cast<std::uintptr_t>(memory.AllocationBase);
					logger::warn(
						"[RR][HandMirrorReflection][fail-stop] #{} address={:#x} imageBase={:#x} offset={:#x} (raw; resolve offline)",
						index, address, base, base != 0 ? address - base : 0);
				}
			} catch (...) {
			}
			g_faulted.store(true, std::memory_order_release);
			g_enabled.store(false, std::memory_order_release);
			SetHandMirrorEvidenceEventActive(false);
			MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
			auto& state = State();
			if (state.store) {
				if (state.coordinator)
					state.coordinator->QuarantineExternalNativeFault(*state.store);
				else
					state.store->QuarantineExternalNativeFault();
			}
			state.pendingReservation = {};
			state.pendingGrantedOwnerLease = 0;
			state.currentPublication = {};
			state.heldPredecessorRetirement = {};
			state.heldCleanMissExpirySource = 0;
			state.heldCleanMissValidatedSource = 0;
			state.lastPresentedPublicationToken = 0;
			state.visibility = {};
			state.stagedWallRetirementGrant.reset();
			state.stagedWallRetirementSource = 0;
			// Scrub every native authorization, publication value and runtime-held SRV.
			// The physical RenderTarget owner and any currently bound D3D state retain
			// their own references; these private publication references must not remain
			// reachable after Store/Coordinator quarantine.
			(void)ScrubNativeSlotsForFailStop(state);
			(void)ClearLastGoodPresentation(state);
			// Store::PhysicalTargets contains scalar identity values only. The actual
			// RenderTarget owners remain pinned in SecondView across quarantine; this
			// scrub cannot release or destroy a possibly bound D3D object.
			state.targets = {};
			state.targetsInitialized = false;
			state.ownerLeaseCursor = 0;
			state.loadGeneration = 0;
			state.deliveryFrame = 0;
			state.inventoryResumeFenceSource = 0;
			state.wallRetirementTokens = {};
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			state.stablePresentation = {};
			if (state.activeInternalReaders == 0) {
				(void)ShutdownRendererSEH(state.renderer);
				state.terminalRendererScrubPending = false;
			} else {
				// The synchronous reader finally performs the terminal detach/guarded
				// release as soon as its last stack-owned request has been destroyed.
				state.terminalRendererScrubPending = true;
			}
			state.renderThreadID = 0;
			g_firstPerson = {};
		}

		[[nodiscard]] bool ExactSE1597Runtime() noexcept
		{
			return REL::Module::IsSE() &&
				REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 };
		}

		[[nodiscard]] bool ExactVR1415Runtime() noexcept
		{
			return SupportedRuntimePolicy::IsExactVRRuntime();
		}

		// Runtimes whose primary RenderFirstPersonView -> RenderPreAndPostResolve
		// call site is mapped: SE 1.5.97 (+0x103, R8D 0x43/0x53) and VR 1.4.15
		// (+0x27E, R8D 0).
		[[nodiscard]] PostResolveHookPolicy::Runtime PostResolveRuntime() noexcept
		{
			if (ExactSE1597Runtime())
				return PostResolveHookPolicy::Runtime::kSE1597;
			if (ExactVR1415Runtime())
				return PostResolveHookPolicy::Runtime::kSkyrimVR1415;
			return PostResolveHookPolicy::Runtime::kUnsupported;
		}

		[[nodiscard]] bool IsReadableRange(
			const std::uintptr_t address,
			const std::size_t size) noexcept
		{
			if (!address || !size || address >
					(std::numeric_limits<std::uintptr_t>::max)() - size) {
				return false;
			}
			MEMORY_BASIC_INFORMATION information{};
			if (VirtualQuery(
					reinterpret_cast<const void*>(address), &information,
					sizeof(information)) != sizeof(information) ||
				information.State != MEM_COMMIT ||
				(information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}
			const auto regionBegin =
				reinterpret_cast<std::uintptr_t>(information.BaseAddress);
			if (regionBegin >
					(std::numeric_limits<std::uintptr_t>::max)() -
					information.RegionSize) {
				return false;
			}
			const auto regionEnd = regionBegin + information.RegionSize;
			const auto rangeEnd = address + size;
			const DWORD protection = information.Protect & 0xFF;
			const bool readable = protection == PAGE_READONLY ||
				protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
			return readable && address >= regionBegin && rangeEnd <= regionEnd;
		}

		[[nodiscard]] bool IsExecutableAddress(
			const std::uintptr_t address) noexcept
		{
			if (!address)
				return false;
			MEMORY_BASIC_INFORMATION information{};
			if (VirtualQuery(
					reinterpret_cast<const void*>(address), &information,
					sizeof(information)) != sizeof(information) ||
				information.State != MEM_COMMIT ||
				(information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}
			const DWORD protection = information.Protect & 0xFF;
			return protection == PAGE_EXECUTE ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool CopyCodeBytesSEH(
			const std::uintptr_t address,
			void* output,
			const std::size_t size) noexcept
		{
			if (!address || !output || !size || !IsReadableRange(address, size))
				return false;
			__try {
				std::memcpy(output, reinterpret_cast<const void*>(address), size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool ReadRel32CallTargetSEH(
			const std::uintptr_t callSite,
			std::uintptr_t& target) noexcept
		{
			target = 0;
			std::array<std::uint8_t, PostResolveHookPolicy::kRel32CallSize>
				instruction{};
			if (!CopyCodeBytesSEH(
					callSite, instruction.data(), instruction.size()) ||
				instruction[0] != PostResolveHookPolicy::kRel32CallOpcode) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(
				std::addressof(displacement), instruction.data() + 1,
				sizeof(displacement));
			target = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(callSite + instruction.size()) +
				displacement);
			return target != 0;
		}

		[[nodiscard]] bool MatchFiveByteBranchStubSEH(
			const std::uintptr_t branchTarget,
			const std::uintptr_t expectedDestination) noexcept
		{
			if (!IsExecutableAddress(branchTarget) ||
				!IsExecutableAddress(expectedDestination)) {
				return false;
			}
			std::array<std::uint8_t,
				PostResolveHookPolicy::kFiveByteBranchStubSize> stub{};
			return CopyCodeBytesSEH(
					branchTarget, stub.data(), stub.size()) &&
				PostResolveHookPolicy::MatchesFiveByteBranchStub(
					stub, expectedDestination);
		}

		[[nodiscard]] bool BuildRel32CallInstruction(
			const std::uintptr_t callSite,
			const std::uintptr_t target,
			std::array<std::uint8_t, PostResolveHookPolicy::kRel32CallSize>&
				instruction) noexcept
		{
			instruction = {};
			if (!callSite || !target)
				return false;
			const auto nextInstruction = callSite + instruction.size();
			const auto displacement64 = static_cast<std::int64_t>(target) -
				static_cast<std::int64_t>(nextInstruction);
			if (displacement64 < (std::numeric_limits<std::int32_t>::min)() ||
				displacement64 > (std::numeric_limits<std::int32_t>::max)()) {
				return false;
			}
			instruction[0] = PostResolveHookPolicy::kRel32CallOpcode;
			const auto displacement = static_cast<std::int32_t>(displacement64);
			std::memcpy(
				instruction.data() + 1, std::addressof(displacement),
				sizeof(displacement));
			return true;
		}

		[[nodiscard]] bool RestoreRel32CallIfOwnedSEH(
			const std::uintptr_t callSite,
			const std::uintptr_t installedBranchTarget,
			const std::uintptr_t nativeTarget) noexcept
		{
			std::array<std::uint8_t, PostResolveHookPolicy::kRel32CallSize>
				nativeInstruction{};
			std::array<std::uint8_t, PostResolveHookPolicy::kRel32CallSize>
				installedInstruction{};
			if (!BuildRel32CallInstruction(
					callSite, nativeTarget, nativeInstruction) ||
				!BuildRel32CallInstruction(
					callSite, installedBranchTarget, installedInstruction)) {
				return false;
			}
			bool restored = false;
			__try {
				restored = REL::safe_write(
					callSite, nativeInstruction.data(), nativeInstruction.size(),
					installedInstruction.data(), installedInstruction.size());
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				restored = false;
			}
			return restored;
		}

		struct PostResolvePresentationHook;

		[[nodiscard]] std::uintptr_t PostResolveThunkAddress() noexcept;

		[[nodiscard]] bool PostResolveHookStillOwned() noexcept
		{
			if (!g_postResolveHookInstalled.load(std::memory_order_acquire))
				return false;
			g_postResolveHookDiagnostics.ownershipChecks.fetch_add(
				1, std::memory_order_relaxed);
			const auto callSite =
				g_postResolveCallSiteAddress.load(std::memory_order_acquire);
			const auto installedBranch =
				g_postResolveInstalledBranchTarget.load(std::memory_order_acquire);
			std::uintptr_t currentTarget = 0;
			return callSite && installedBranch &&
				ReadRel32CallTargetSEH(callSite, currentTarget) &&
				currentTarget == installedBranch &&
				MatchFiveByteBranchStubSEH(
					installedBranch, PostResolveThunkAddress());
		}

		[[nodiscard]] bool PostResolveHookRequired() noexcept
		{
			return PostResolveHookPolicy::ShouldInstall({
				.runtime = PostResolveRuntime(),
				.reflectionRequested =
					g_requested.load(std::memory_order_acquire),
				.cleanMissContinuityRequested =
					g_cleanMissContinuityRequested.load(std::memory_order_acquire),
				.exactEmptyEnableMarkerPresent =
					g_postResolveEnableMarkerLatched.load(std::memory_order_acquire)
			});
		}

		[[nodiscard]] bool PostResolveHookReady() noexcept
		{
			return !PostResolveHookRequired() || PostResolveHookStillOwned();
		}

		[[nodiscard]] bool BeginPostResolveInvocation(
			RE::NiCamera* camera,
			RE::BSShaderAccumulator* accumulator,
			const std::uint32_t flags) noexcept
		{
			const auto postResolveRuntime = PostResolveRuntime();
			const bool exactRuntime =
				PostResolveHookPolicy::IsSupportedRuntime(postResolveRuntime);
			const bool exactPrimaryFlags =
				PostResolveHookPolicy::IsPrimaryFlagsFor(postResolveRuntime, flags);
			const bool hookInstalled =
				g_postResolveHookInstalled.load(std::memory_order_acquire);
			const bool markerLatched =
				g_postResolveEnableMarkerLatched.load(std::memory_order_acquire);
			const bool reflectionEnabled = IsEnabled();
			const bool continuityEffective = CleanMissContinuityEffective();
			const bool privatePassActive = SecondView::IsInsidePrivateCapture();
			const bool lifecycleTransitionSuspendedLive =
				HandLifecycleTransitionSuspended();
			const bool baseInvocationCandidate = exactRuntime && hookInstalled &&
				markerLatched && reflectionEnabled && continuityEffective &&
				g_firstPerson.active && !g_firstPerson.returnedNormally &&
				!g_firstPerson.lifecycleTransitionSuppressed &&
				!privatePassActive && !lifecycleTransitionSuspendedLive && camera &&
				accumulator && exactPrimaryFlags;

			MirrorPaneDelivery::CurrentMainDrawState entryMain{};
			ReceiptPolicy::FrameReceipt entryReceipt{};
			const auto sourceSequence = baseInvocationCandidate ?
				SecondView::CurrentMainWorldSourceSequence() : 0;
			MirrorPaneDelivery::CurrentMainDrawStateStatus entryStatus =
				MirrorPaneDelivery::CurrentMainDrawStateStatus::kEngineStateUnavailable;
			if (sourceSequence != 0) {
				entryStatus = QueryHandDrawState(entryMain);
				RecordCurrentMainDrawStateStatus(State().diagnostics, entryStatus);
				if (entryStatus == MirrorPaneDelivery::
						CurrentMainDrawStateStatus::kReady && entryMain.valid) {
					entryReceipt = {
						sourceSequence, entryMain.mainWorldFrame,
						entryMain.graphicsFrame
					};
				}
			}
			const bool entryFrameComplete = entryStatus == MirrorPaneDelivery::
					CurrentMainDrawStateStatus::kReady && entryMain.valid &&
				ReceiptPolicy::IsValidFrameReceipt(entryReceipt) &&
				Bridge::IsValidPrivateTarget(entryMain.captureReceiptTarget) &&
				entryMain.device && entryMain.context &&
				ReturnedPresentationComplete(entryMain);
			const bool otherwiseExactPrimary =
				baseInvocationCandidate && entryFrameComplete;
			const bool hookOwned = !otherwiseExactPrimary ||
				PostResolveHookStillOwned();
			const bool tokenAvailable = g_postResolveTokenCursor !=
				(std::numeric_limits<std::uint64_t>::max)();
			const auto action = PostResolveHookPolicy::SelectInvocationAction({
				.exactRuntime = exactRuntime,
				.hookInstalled = hookInstalled,
				.hookOwned = hookOwned,
				.exactEmptyEnableMarkerLatched = markerLatched,
				.reflectionEnabled = reflectionEnabled,
				.cleanMissContinuityEffective = continuityEffective,
				.firstPersonTLSActive = g_firstPerson.active,
				.firstPersonAlreadyReturned = g_firstPerson.returnedNormally,
				.lifecycleTransitionSuppressed =
					g_firstPerson.lifecycleTransitionSuppressed,
				.lifecycleTransitionSuspendedLive =
					lifecycleTransitionSuspendedLive,
				.privatePassActive = privatePassActive,
				.nestedInvocation = false,
				.exactPrimaryFlags = exactPrimaryFlags,
				.entryFrameComplete = entryFrameComplete,
				.tokenAvailable = tokenAvailable
			});
			if (action ==
					PostResolveHookPolicy::InvocationAction::kFailStopOwnershipLost) {
				g_postResolveHookDiagnostics.ownershipLosses.fetch_add(
					1, std::memory_order_relaxed);
				FailStopInternal();
				return false;
			}
			if (action ==
					PostResolveHookPolicy::InvocationAction::kFailStopTokenExhausted) {
				FailStopInternal();
				return false;
			}
			if (action !=
					PostResolveHookPolicy::InvocationAction::kBeginOwningPrimary) {
				if (privatePassActive) {
					g_postResolveHookDiagnostics.privatePassSkips.fetch_add(
						1, std::memory_order_relaxed);
				}
				if (lifecycleTransitionSuspendedLive) {
					g_postResolveHookDiagnostics.liveLifecycleSkips.fetch_add(
						1, std::memory_order_relaxed);
				}
				if (baseInvocationCandidate && !entryFrameComplete) {
					g_postResolveHookDiagnostics.entryFrameRejects.fetch_add(
						1, std::memory_order_relaxed);
				}
				g_postResolveHookDiagnostics.inactiveChains.fetch_add(
					1, std::memory_order_relaxed);
				return false;
			}

			const auto token = ++g_postResolveTokenCursor;
			g_firstPerson.postResolveStageToken = 0;
			g_firstPerson.postResolveExpectedReceipt = {};
			g_firstPerson.postResolveExpectedCaptureTarget = {};
			g_firstPerson.postResolveExpectedDeviceIdentity = 0;
			g_firstPerson.postResolveExpectedContextIdentity = 0;
			g_firstPerson.postResolveExpectedReturnCount = 0;
			g_firstPerson.postResolveExpectedInvocationValid = false;
			g_postResolveInvocation = {
				.entryReceipt = entryReceipt,
				.entryCaptureTarget = entryMain.captureReceiptTarget,
				.cameraIdentity = reinterpret_cast<std::uintptr_t>(camera),
				.accumulatorIdentity =
					reinterpret_cast<std::uintptr_t>(accumulator),
				.deviceIdentity =
					reinterpret_cast<std::uintptr_t>(entryMain.device),
				.contextIdentity =
					reinterpret_cast<std::uintptr_t>(entryMain.context),
				.token = token,
				.entryReturnCount = g_firstPerson.exactNormalReturnCount,
				.flags = flags,
				.nativeDepth = 1,
				.active = true
			};
			return true;
		}

		void OnPostResolveReturnedRaw(
			RE::NiCamera* camera,
			RE::BSShaderAccumulator* accumulator,
			const std::uint32_t flags) noexcept
		{
			g_postResolveHookDiagnostics.callbackCalls.fetch_add(
				1, std::memory_order_relaxed);
			g_postResolveHookDiagnostics.lastFlags.store(
				flags, std::memory_order_relaxed);
			auto& invocation = g_postResolveInvocation;
			const auto postResolveRuntime = PostResolveRuntime();
			const bool exactRuntime =
				PostResolveHookPolicy::IsSupportedRuntime(postResolveRuntime);
			const bool exactPrimaryFlags =
				PostResolveHookPolicy::IsPrimaryFlagsFor(postResolveRuntime, flags);
			const bool hookInstalled =
				g_postResolveHookInstalled.load(std::memory_order_acquire);
			const bool markerLatched =
				g_postResolveEnableMarkerLatched.load(std::memory_order_acquire);
			const bool reflectionEnabled = IsEnabled();
			const bool continuityEffective = CleanMissContinuityEffective();
			const bool privatePassActive = SecondView::IsInsidePrivateCapture();
			const bool lifecycleTransitionSuspendedLive =
				HandLifecycleTransitionSuspended();
			const bool exactInvocationArguments = camera && accumulator &&
				invocation.cameraIdentity ==
					reinterpret_cast<std::uintptr_t>(camera) &&
				invocation.accumulatorIdentity ==
					reinterpret_cast<std::uintptr_t>(accumulator) &&
				invocation.flags == flags;
			const bool baseCandidate = exactRuntime && hookInstalled &&
				markerLatched && reflectionEnabled && continuityEffective &&
				g_firstPerson.active && !g_firstPerson.returnedNormally &&
				!g_firstPerson.lifecycleTransitionSuppressed && invocation.active &&
				invocation.nativeDepth == 0 && exactInvocationArguments &&
				exactPrimaryFlags &&
				!privatePassActive && !lifecycleTransitionSuspendedLive;
			const bool hookOwned = !baseCandidate || PostResolveHookStillOwned();
			const bool exactInvocationToken = invocation.token != 0 &&
				!invocation.foreignExactReturnObserved &&
				invocation.token != g_firstPerson.postResolvePresentedToken;
			const auto action = PostResolveHookPolicy::SelectCompletionAction({
				.exactRuntime = exactRuntime,
				.hookInstalled = hookInstalled,
				.hookOwned = hookOwned,
				.exactEmptyEnableMarkerLatched = markerLatched,
				.reflectionEnabled = reflectionEnabled,
				.cleanMissContinuityEffective = continuityEffective,
				.firstPersonTLSActive = g_firstPerson.active,
				.firstPersonAlreadyReturned = g_firstPerson.returnedNormally,
				.lifecycleTransitionSuppressed =
					g_firstPerson.lifecycleTransitionSuppressed,
				.lifecycleTransitionSuspendedLive =
					lifecycleTransitionSuspendedLive,
				.privatePassActive = privatePassActive,
				.owningInvocationActive = invocation.active &&
					invocation.nativeDepth == 0,
				.exactInvocationArguments = exactInvocationArguments,
				.exactPrimaryFlags = exactPrimaryFlags &&
					invocation.flags == flags,
				.exactInvocationToken = exactInvocationToken,
				.exactStageToken = g_firstPerson.postResolveStageToken ==
					invocation.token,
				.exactStageReturnCount =
					g_firstPerson.postResolveExpectedReturnCount ==
						g_firstPerson.exactNormalReturnCount,
				.stagedReturnComplete =
					g_firstPerson.postResolveExpectedInvocationValid &&
					g_firstPerson.staged.valid &&
					static_cast<bool>(g_firstPerson.staged.retainedMainTarget),
				.entryReturnCount = invocation.entryReturnCount,
				.exitReturnCount = g_firstPerson.exactNormalReturnCount
			});
			if (action == PostResolveHookPolicy::CompletionAction::kChainOnly) {
				if (g_firstPerson.exactNormalReturnCount ==
					invocation.entryReturnCount) {
					g_postResolveHookDiagnostics.zeroDeltaChains.fetch_add(
						1, std::memory_order_relaxed);
				} else {
					g_postResolveHookDiagnostics.unstampedDeltaRejects.fetch_add(
						1, std::memory_order_relaxed);
				}
				return;
			}
			auto& state = State();
			++state.diagnostics.postResolveCallbacks;
			if (action == PostResolveHookPolicy::CompletionAction::
					kFailStopOwnershipLost) {
				g_postResolveHookDiagnostics.ownershipLosses.fetch_add(
					1, std::memory_order_relaxed);
				FailStopInternal();
				return;
			}
			if (action == PostResolveHookPolicy::CompletionAction::
					kFailStopReturnCountRegressed ||
				action == PostResolveHookPolicy::CompletionAction::
					kFailStopReturnCountOverflow) {
				FailStopInternal();
				return;
			}

			const auto& expectedReceipt =
				g_firstPerson.postResolveExpectedReceipt;
			if (!ReceiptPolicy::IsValidFrameReceipt(invocation.entryReceipt) ||
				!Bridge::IsValidPrivateTarget(invocation.entryCaptureTarget) ||
				invocation.deviceIdentity == 0 || invocation.contextIdentity == 0 ||
				!g_firstPerson.postResolveExpectedInvocationValid ||
				g_firstPerson.postResolveStageToken != invocation.token ||
				g_firstPerson.postResolveExpectedReturnCount !=
					g_firstPerson.exactNormalReturnCount ||
				expectedReceipt != invocation.entryReceipt ||
				!Bridge::IsValidPrivateTarget(
					g_firstPerson.postResolveExpectedCaptureTarget) ||
				g_firstPerson.postResolveExpectedCaptureTarget !=
					invocation.entryCaptureTarget ||
				g_firstPerson.postResolveExpectedDeviceIdentity !=
					invocation.deviceIdentity ||
				g_firstPerson.postResolveExpectedContextIdentity !=
					invocation.contextIdentity) {
				++state.diagnostics.postResolveTokenRejects;
				return;
			}

			const auto currentSource =
				SecondView::CurrentMainWorldSourceSequence();
			if (currentSource == 0 ||
				currentSource != invocation.entryReceipt.sourceSequence) {
				++state.diagnostics.postResolveSourceRejects;
				return;
			}
			const auto& staged = g_firstPerson.staged;
			if (staged.receipt.mainViewFrame !=
					invocation.entryReceipt.mainViewFrame ||
				staged.receipt.graphicsFrame !=
					invocation.entryReceipt.graphicsFrame ||
				staged.returnedSurface.pose.mainViewFrame !=
					invocation.entryReceipt.mainViewFrame) {
				++state.diagnostics.postResolveFrameRejects;
				return;
			}
			if (staged.receipt.sourceSequence !=
					invocation.entryReceipt.sourceSequence ||
				staged.main.captureReceiptTarget != invocation.entryCaptureTarget ||
				staged.returnedSurface.sourceSequence !=
					invocation.entryReceipt.sourceSequence ||
				staged.deviceIdentity != invocation.deviceIdentity ||
				staged.contextIdentity != invocation.contextIdentity) {
				++state.diagnostics.postResolveReceiptRejects;
				return;
			}
			const auto stagedLiveTarget = MakeLiveDeliveryTargetIdentity(
				staged.retainedMainTarget);
			const bool retainedTargetExact = staged.retainedMainTarget &&
				staged.retainedMainTarget->colorRTV &&
				LiveReturn::IsValidLiveDeliveryTarget(stagedLiveTarget) &&
				stagedLiveTarget.deviceIdentity == staged.deviceIdentity &&
				stagedLiveTarget.contextIdentity == staged.contextIdentity &&
				stagedLiveTarget.colorResourceIdentity ==
					staged.colorResourceIdentity &&
				stagedLiveTarget.depthResourceIdentity ==
					staged.depthResourceIdentity &&
				staged.main.device == staged.retainedMainTarget->device &&
				staged.main.context == staged.retainedMainTarget->context &&
				staged.main.colorRTV == staged.retainedMainTarget->colorRTV &&
				staged.main.depthDSV == staged.retainedMainTarget->depthDSV;
			if (!retainedTargetExact || !ReturnedPresentationComplete(staged.main)) {
				++state.diagnostics.postResolveTargetRejects;
				return;
			}

			if (SecondView::IsInsidePrivateCapture()) {
				g_postResolveHookDiagnostics.privatePassSkips.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			if (HandLifecycleTransitionSuspended()) {
				g_postResolveHookDiagnostics.liveLifecycleSkips.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			g_firstPerson.postResolvePresentedToken = invocation.token;
			++state.diagnostics.postResolveNewStageObservations;

			// Never read a post-native accumulator camera or current OM destination.
			// The exact pane return owns this detached color target and staged camera.
			// MirrorPaneRenderer validates the retained depth identity but binds no DSV
			// for the internal hand lane, preserving the proven no-depth final write.
			auto deliveryMain = staged.main;
			deliveryMain.device = staged.retainedMainTarget->device;
			deliveryMain.context = staged.retainedMainTarget->context;
			deliveryMain.colorRTV = staged.retainedMainTarget->colorRTV;
			deliveryMain.motionRTV = nullptr;
			deliveryMain.depthDSV = staged.retainedMainTarget->depthDSV;
			deliveryMain.valid = true;
			RetainedTargetPresentationResult result{};
			if (!PresentFreshOrLastGoodToRetainedTarget(
					state, staged.returnedSurface, staged.receipt,
					staged.main.captureReceiptTarget, deliveryMain,
					staged.retainedMainTarget, stagedLiveTarget,
					staged.graphIndicatesRaisedPresentation,
					RetainedTargetPresentationRoute::kPostResolve, result) ||
				MirrorPaneDelivery::Faulted()) {
				FailStopInternal();
			}
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
			else if ((result.freshDrawn || result.replayDrawn) &&
				HandMirrorLateTargetSurvivalProbe::Ready()) {
				g_firstPerson.postResolvePresentedCaptureSequence =
					result.captureSequence;
				HandMirrorLateTargetSurvivalProbe::ObservePostResolveA(
					MakeLateTargetSurvivalSample(
						state, staged.retainedMainTarget, deliveryMain.viewport,
						staged.receipt, result.captureSequence));
			}
#endif
		}

		[[nodiscard]] int RecordPostResolveCallbackException(
			const unsigned long code) noexcept
		{
			g_postResolveHookDiagnostics.callbackExceptions.fetch_add(
				1, std::memory_order_relaxed);
			g_postResolveHookDiagnostics.lastException.store(
				code, std::memory_order_release);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		__declspec(noinline) void InvokePostResolveCallbackSEH(
			RE::NiCamera* camera,
			RE::BSShaderAccumulator* accumulator,
			const std::uint32_t flags) noexcept
		{
			if (g_postResolveCallbackActive) {
				g_postResolveHookDiagnostics.callbackReentries.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			bool callbackFaulted = false;
			g_postResolveCallbackActive = true;
			__try {
				__try {
					OnPostResolveReturnedRaw(camera, accumulator, flags);
				} __finally {
					g_postResolveCallbackActive = false;
				}
			} __except (RecordPostResolveCallbackException(GetExceptionCode())) {
				callbackFaulted = true;
			}
			if (callbackFaulted)
				FailStopInternal();
		}

		struct PostResolvePresentationHook
		{
			static __declspec(noinline) void thunk(
				RE::NiCamera* camera,
				RE::BSShaderAccumulator* accumulator,
				const std::uint32_t flags)
			{
				auto* native =
					g_postResolveNative.load(std::memory_order_acquire);
				if (!native) {
					g_postResolveHookDiagnostics.ownershipLosses.fetch_add(
						1, std::memory_order_relaxed);
					FailStopInternal();
					return;
				}
				g_postResolveHookDiagnostics.nativeCalls.fetch_add(
					1, std::memory_order_relaxed);
				const bool nested = g_postResolveInvocation.active;
				bool nestedDepthPushed = false;
				if (nested) {
					g_postResolveHookDiagnostics.nestedChains.fetch_add(
						1, std::memory_order_relaxed);
					if (g_postResolveInvocation.nativeDepth !=
						(std::numeric_limits<std::uint32_t>::max)()) {
						++g_postResolveInvocation.nativeDepth;
						nestedDepthPushed = true;
					} else {
						FailStopInternal();
					}
				}

				const bool owning = !nested && BeginPostResolveInvocation(
					camera, accumulator, flags);
				// Chain the exact current call target exactly once.  The TLS token remains
				// live across the complete camera wrapper, including both accumulator
				// phases, and the retained-color writer runs only after normal return.
				__try {
					native(camera, accumulator, flags);
					g_postResolveHookDiagnostics.nativeReturns.fetch_add(
						1, std::memory_order_relaxed);
					if (owning) {
						g_postResolveInvocation.nativeDepth = 0;
						InvokePostResolveCallbackSEH(camera, accumulator, flags);
					}
				} __finally {
					if (nested) {
						if (nestedDepthPushed)
							--g_postResolveInvocation.nativeDepth;
					} else if (owning) {
						g_postResolveInvocation = {};
					}
				}
			}
		};

		[[nodiscard]] std::uintptr_t PostResolveThunkAddress() noexcept
		{
			return std::bit_cast<std::uintptr_t>(
				&PostResolvePresentationHook::thunk);
		}

		void RestorePostResolveCallSiteAtProcessShutdown() noexcept
		{
			if (!g_postResolveHookInstalled.load(std::memory_order_acquire))
				return;
			const auto callSite =
				g_postResolveCallSiteAddress.load(std::memory_order_acquire);
			const auto installedBranch =
				g_postResolveInstalledBranchTarget.load(std::memory_order_acquire);
			auto* native =
				g_postResolveNative.load(std::memory_order_acquire);
			const auto nativeAddress = std::bit_cast<std::uintptr_t>(native);
			std::uintptr_t currentTarget = 0;
			if (!callSite || !installedBranch || !nativeAddress ||
				!ReadRel32CallTargetSEH(callSite, currentTarget) ||
				currentTarget != installedBranch ||
				!MatchFiveByteBranchStubSEH(
					installedBranch, PostResolveThunkAddress())) {
				g_postResolveHookDiagnostics.shutdownOwnershipSkips.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			if (!RestoreRel32CallIfOwnedSEH(
					callSite, installedBranch, nativeAddress)) {
				g_postResolveHookDiagnostics.shutdownOwnershipSkips.fetch_add(
					1, std::memory_order_relaxed);
				return;
			}
			std::uintptr_t restoredTarget = 0;
			if (ReadRel32CallTargetSEH(callSite, restoredTarget) &&
				restoredTarget == nativeAddress) {
				g_postResolveHookInstalled.store(false, std::memory_order_release);
				g_postResolveHookDiagnostics.shutdownRestores.fetch_add(
					1, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] bool InstallPostResolvePresentationHook() noexcept
		{
			if (!PostResolveHookRequired())
				return true;
			g_postResolveHookDiagnostics.installAttempts.fetch_add(
				1, std::memory_order_relaxed);
			if (g_postResolveHookInstallAttempted.exchange(
					true, std::memory_order_acq_rel)) {
				return PostResolveHookStillOwned();
			}

			const auto postResolveRuntime = PostResolveRuntime();
			const bool vr = postResolveRuntime ==
				PostResolveHookPolicy::Runtime::kSkyrimVR1415;
			const auto layout =
				PostResolveHookPolicy::SelectCallSiteLayout(postResolveRuntime);
			const auto moduleBase = REL::Module::get().base();
			// The VR address library maps 100411 to a dead, never-called copy
			// (0x13218C0); the live VR 1.4.15 RenderFirstPersonView is 0x13244E0.
			// VariantID resolves the SE/AE IDs through their databases and the VR
			// offset directly.  99789 is in the VR CSV (0x12FF010) but is pinned
			// the same way so both resolutions are checked against one source.
			const auto firstPersonAddress = REL::VariantID(
				PostResolveHookPolicy::kSE1597RenderFirstPersonViewID, 107129,
				PostResolveHookPolicy::kVR1415RenderFirstPersonViewRVA).address();
			const auto nativeAddress = REL::VariantID(
				PostResolveHookPolicy::kSE1597RenderPreAndPostResolveID, 106436,
				PostResolveHookPolicy::kVR1415RenderPreAndPostResolveRVA).address();
			const auto callOffset = layout.callOffset;
			const auto windowAddress = firstPersonAddress + layout.windowOffset;
			const auto callSite = firstPersonAddress + callOffset;
			const auto thunkAddress = PostResolveThunkAddress();
			std::array<std::uint8_t, PostResolveHookPolicy::kCallWindowSize>
				callWindow{};
			std::uintptr_t decodedTarget = 0;
			const bool exactContract =
				PostResolveHookPolicy::IsSupportedRuntime(postResolveRuntime) &&
				firstPersonAddress == moduleBase + layout.renderFirstPersonViewRVA &&
				nativeAddress == moduleBase + layout.renderPreAndPostResolveRVA &&
				callSite == moduleBase + layout.callSiteRVA &&
				IsExecutableAddress(nativeAddress) &&
				IsExecutableAddress(thunkAddress) &&
				CopyCodeBytesSEH(
					windowAddress, callWindow.data(), callWindow.size()) &&
				PostResolveHookPolicy::MatchesCallWindowFor(vr, callWindow) &&
				PostResolveHookPolicy::DecodeCallTargetFor(
					vr, windowAddress, callWindow, decodedTarget) &&
				decodedTarget == nativeAddress;
			if (!exactContract ||
				std::atexit(&RestorePostResolveCallSiteAtProcessShutdown) != 0) {
				g_postResolveHookDiagnostics.installFailures.fetch_add(
					1, std::memory_order_relaxed);
				logger::critical(
					"[RR][HandMirrorReflection][post-resolve] refused exact {} RenderFirstPersonView+0x{:X} call hook: caller signature or native rel32 target failed",
					vr ? "VR 1.4.15" : "SE 1.5.97", callOffset);
				return false;
			}

			g_postResolveNative.store(
				std::bit_cast<PostResolveFunction*>(decodedTarget),
				std::memory_order_release);
			std::uintptr_t captured = 0;
			try {
				captured = SKSE::GetTrampoline().write_call<5>(
					callSite, PostResolvePresentationHook::thunk);
			} catch (...) {
				captured = 0;
			}
			std::uintptr_t installedBranch = 0;
			const bool patchOwned = ReadRel32CallTargetSEH(
					callSite, installedBranch) &&
				MatchFiveByteBranchStubSEH(installedBranch, thunkAddress);
			if (captured != decodedTarget || !patchOwned) {
				const auto rollbackTarget = captured != 0 &&
					IsExecutableAddress(captured) ? captured : decodedTarget;
				g_postResolveNative.store(
					std::bit_cast<PostResolveFunction*>(rollbackTarget),
					std::memory_order_release);
				if (patchOwned) {
					(void)RestoreRel32CallIfOwnedSEH(
						callSite, installedBranch, rollbackTarget);
				}
				std::uintptr_t afterRollbackTarget = 0;
				const bool rollbackReadable = ReadRel32CallTargetSEH(
					callSite, afterRollbackTarget);
				const bool rollbackProven = rollbackReadable &&
					afterRollbackTarget == rollbackTarget;
				if (rollbackProven) {
					g_postResolveNative.store(nullptr, std::memory_order_release);
				} else {
					// A readable non-native target is still ambiguous here: it can be a
					// peer's branch, or our just-written branch whose stub verification
					// failed.  Retain the captured native chain in either case so an
					// unexpectedly reachable thunk can never suppress Skyrim's wrapper.
					g_postResolveCallSiteAddress.store(
						callSite, std::memory_order_release);
					g_postResolveInstalledBranchTarget.store(
						installedBranch, std::memory_order_release);
					g_postResolveHookInstalled.store(true, std::memory_order_release);
					g_postResolveHookDiagnostics.transactionRollbackFailures.fetch_add(
						1, std::memory_order_relaxed);
				}
				g_postResolveHookDiagnostics.installFailures.fetch_add(
					1, std::memory_order_relaxed);
				logger::critical(
					"[RR][HandMirrorReflection][post-resolve] RenderFirstPersonView+0x{:X} call patch verification failed",
					callOffset);
				return false;
			}

			g_postResolveCallSiteAddress.store(
				callSite, std::memory_order_release);
			g_postResolveInstalledBranchTarget.store(
				installedBranch, std::memory_order_release);
			g_postResolveHookInstalled.store(true, std::memory_order_release);
			g_postResolveHookDiagnostics.installSuccesses.fetch_add(
				1, std::memory_order_relaxed);
			logger::info(
				"[RR][HandMirrorReflection][post-resolve] exact SE 1.5.97 RenderFirstPersonView+0x103 call installed; callSite=0x{:X} capturedTarget=0x{:X} branch=0x{:X} hook=0x{:X}",
				callSite, captured, installedBranch, thunkAddress);
			return true;
		}
	}

	void PrepareAtInputLoaded() noexcept
	{
		if (g_prepareAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		try {
			// Reflection remains a supported product request. Consecutive clean-miss
			// continuity and this exact primary callsite seam share the existing exact-empty
			// default-off Data marker; neither may be hard-enabled by source defaults.
			// Hand capture stays dormant until the add-on plugin is actually in Data.
			// Requesting it on the core-only product fail-stops a store that was never
			// created (GOG 2026-09-18: targets=false after game-loaded).
			g_requested.store(
				MirrorContentProfile::AddonCandidatePresent(),
				std::memory_order_release);
			const auto marker = ObservePostResolveEnableMarker();
			g_cleanMissContinuityRequested.store(
				marker.accepted, std::memory_order_release);
			g_postResolveEnableMarkerLatched.store(
				marker.accepted, std::memory_order_release);
			if (marker.accepted) {
				logger::warn(
					"[RR][HandMirrorReflection][clean-miss/post-resolve] ENABLED by exact empty marker: {}",
					marker.path.string());
			} else if (marker.error) {
				logger::error(
					"[RR][HandMirrorReflection][clean-miss/post-resolve] marker inspection failed; continuity and hook remain default-off: {} ({})",
					marker.path.string(), marker.error.message());
			} else {
				logger::info(
					"[RR][HandMirrorReflection][clean-miss/post-resolve] default-off; exact empty marker {} {}",
					marker.path.string(),
					marker.present ? "was rejected" : "is absent");
			}
			if (!InstallPostResolvePresentationHook()) {
				FailStopInternal();
				return;
			}
			logger::info(
				"[RR][HandMirrorReflection] supported-style reflection requested; clean-miss continuity and primary-wrapper post-resolve writer requested={} by the shared exact-empty marker; shared owners remain dormant until exact content is proven at DataLoaded",
				marker.accepted);
		} catch (...) {
			g_requested.store(false, std::memory_order_release);
			g_cleanMissContinuityRequested.store(false, std::memory_order_release);
			g_postResolveEnableMarkerLatched.store(false, std::memory_order_release);
			g_faulted.store(true, std::memory_order_release);
		}
	}

	bool Requested() noexcept
	{
		return g_requested.load(std::memory_order_acquire);
	}

	void OnDataLoaded() noexcept
	{
		g_enabled.store(false, std::memory_order_release);
		SetHandMirrorEvidenceEventActive(false);
		MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
		if (!Requested() || g_faulted.load(std::memory_order_acquire))
			return;
		const bool staged = HandMirrorApprovedContentReadOnlyObserver::IsEnabled() &&
			SecondView::HooksReady() && MirrorCameraOverride::HookReady();
		MirrorCameraOverride::SetHandMirrorRuntimeEnabled(staged);
		logger::info(
			"[RR][HandMirrorReflection] DataLoaded camera dependency staged={} runtimeEnabled=false privateTargets=2x{}x{} audience=internal-hand-only apiV1=false thirdPersonDelivery=false",
			staged, PrivateTargetDimension(), PrivateTargetHeight());
	}

	void CompleteDataLoadedActivation() noexcept
	{
		if (!Requested()) {
			g_enabled.store(false, std::memory_order_release);
			SetHandMirrorEvidenceEventActive(false);
			MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
			logger::info(
				"[RR][HandMirrorReflection] DataLoaded final dependencyReady=false runtimeEnabled=false (add-on absent)");
			return;
		}
		const bool postResolveReady = PostResolveHookReady();
		if (PostResolveHookRequired() && !postResolveReady) {
			g_postResolveHookDiagnostics.ownershipLosses.fetch_add(
				1, std::memory_order_relaxed);
			FailStopInternal();
		}
		const bool ready = Requested() &&
			!g_faulted.load(std::memory_order_acquire) &&
			postResolveReady &&
			HandMirrorApprovedContentReadOnlyObserver::IsEnabled() &&
			SecondView::HandCaptureDependenciesReady() &&
			MirrorCameraOverride::HandMirrorRuntimeReady() &&
			MirrorPaneDelivery::SharedSeamReady() &&
			!MirrorPaneDelivery::Faulted() &&
			Reservation::CanEnableHandCaptureRuntime();
		g_enabled.store(ready, std::memory_order_release);
		SetHandMirrorEvidenceEventActive(ready);
		if (!ready)
			MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
		logger::info(
			"[RR][HandMirrorReflection] DataLoaded final dependencyReady={} runtimeEnabled={} cleanMissContinuity(requested/effective)={}/{} reservation/native/publication/deliveryWired={}/{}/{}/{} apiV1=false thirdPersonDelivery=false",
			ready, ready,
			g_cleanMissContinuityRequested.load(std::memory_order_acquire),
			CleanMissContinuityEffective(),
			Reservation::kCaptureReservationRuntimeWired,
			Reservation::kNativeHandCaptureRuntimeWired,
			Reservation::kInternalHandPublicationRuntimeWired,
			Reservation::kFirstPersonHandDeliveryRuntimeWired);
	}

	void OnPreLoadGame() noexcept
	{
		// The post-load reset owns Store/Coordinator invalidation and guarded COM
		// retirement.  PreLoadGame only revokes every hot-path authorization, so an
		// in-flight callback can take its existing lifecycle-cleanup path without a
		// stale hand publication being presented while Skyrim tears the world down.
		g_loadTransitionSuspended.store(true, std::memory_order_release);
		g_enabled.store(false, std::memory_order_release);
		SetHandMirrorEvidenceEventActive(false);
		MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
	}

	void OnGameLoaded() noexcept
	{
		auto& state = State();
		SetHandMirrorEvidenceEventActive(false);
		if (!Requested()) {
			g_enabled.store(false, std::memory_order_release);
			MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
			g_firstPerson = {};
			g_loadTransitionSuspended.store(false, std::memory_order_release);
			return;
		}
		// A native/cleanup fault is process-terminal.  Keep the poisoned
		// coordinator/store and any still-pinned COM roles intact; rebuilding a
		// fresh Ready store here would erase quarantine evidence and could release
		// a resource whose owned-context unbind proof never completed.
		if (g_faulted.load(std::memory_order_acquire)) {
			g_enabled.store(false, std::memory_order_release);
			SetHandMirrorEvidenceEventActive(false);
			MirrorCameraOverride::SetHandMirrorRuntimeEnabled(false);
			g_firstPerson = {};
			return;
		}
		if (state.loadGeneration ==
			(std::numeric_limits<std::uint64_t>::max)()) {
			FailStopInternal();
			return;
		}
		const auto nextLoadGeneration = state.loadGeneration + 1;
		if (state.targetsInitialized) {
			if (!state.store || !state.coordinator ||
				state.store->Inspect().phase != StorePolicy::StorePhase::kReady) {
				FailStopInternal();
				return;
			}
			const auto coordinatorAudit = state.coordinator->Inspect();
			if (coordinatorAudit.activeTicketCount != 0 ||
				state.activeInternalReaders != 0) {
				FailStopInternal();
				return;
			}
			if (Reservation::IsValidFenceReservation(state.pendingReservation)) {
				if (coordinatorAudit.pendingReservationCount != 1 ||
					!CancelPendingHandReservation(
						state.pendingReservation.sourceSequence)) {
					FailStopInternal();
					return;
				}
			} else if (coordinatorAudit.pendingReservationCount != 0 ||
				state.pendingGrantedOwnerLease != 0) {
				FailStopInternal();
				return;
			}
			if (!ReleaseHeldPredecessor(state)) {
				FailStopInternal();
				return;
			}
			StorePolicy::InvalidationResult invalidation{};
			const auto status = state.store->OnLoadInvalidated(
				nextLoadGeneration, invalidation);
			if (status == StorePolicy::InvalidationStatus::kQuarantined ||
				status == StorePolicy::InvalidationStatus::kUnavailable ||
				status == StorePolicy::InvalidationStatus::kInvalidEvidence ||
				status == StorePolicy::InvalidationStatus::kTokenExhausted ||
				!ReleaseRetirement(state, invalidation.retirement) ||
				!ScrubNativeSlotsForFailStop(state)) {
				FailStopInternal();
				return;
			}
		} else if ((state.store &&
				state.store->Inspect().phase != StorePolicy::StorePhase::kUninitialized) ||
			(state.coordinator && state.coordinator->Inspect().phase !=
				Reservation::CoordinatorPhase::kUninitialized)) {
			FailStopInternal();
			return;
		}
		// Store, Coordinator, physical-target identity and every typed issuer remain
		// process-monotonic across load.  Resetting them would make a stale pre-load
		// receipt replayable because the receipts intentionally carry no load epoch.
		state.loadGeneration = nextLoadGeneration;
		state.pendingReservation = {};
		state.pendingGrantedOwnerLease = 0;
		state.currentPublication = {};
		state.heldPredecessorRetirement = {};
		state.heldCleanMissExpirySource = 0;
		state.heldCleanMissValidatedSource = 0;
		state.lastPresentedPublicationToken = 0;
		state.visibility = {};
		state.stagedWallRetirementGrant.reset();
		state.stagedWallRetirementSource = 0;
		ResetHandAffineHistory(
			state, HandAffineHistoryResetReason::kLifecycle);
		if (!ClearLastGoodPresentation(state)) {
			FailStopInternal();
			return;
		}
		state.stablePresentation = {};
		state.inventoryResumeFenceSource = 0;
		g_firstPerson = {};
		g_inventoryMenuOpen.store(false, std::memory_order_release);
		g_inventoryResumeRequested.store(false, std::memory_order_release);
		g_inventoryTransitionSuspended.store(false, std::memory_order_release);
		g_equipWakePending.store(false, std::memory_order_release);
		// PreLoadGame revoked the camera-owner latch itself. Re-stage the already
		// installed hook before asking HandMirrorRuntimeReady(), otherwise that
		// readiness query would make recovery permanently self-failing.
		const bool cameraStaged = Requested() &&
			!g_faulted.load(std::memory_order_acquire) &&
			HandMirrorApprovedContentReadOnlyObserver::IsEnabled() &&
			SecondView::HooksReady() && MirrorCameraOverride::HookReady();
		MirrorCameraOverride::SetHandMirrorRuntimeEnabled(cameraStaged);
		const bool postResolveReady = PostResolveHookReady();
		if (PostResolveHookRequired() && !postResolveReady) {
			g_postResolveHookDiagnostics.ownershipLosses.fetch_add(
				1, std::memory_order_relaxed);
			FailStopInternal();
			return;
		}
		const bool ready = Requested() &&
			!g_faulted.load(std::memory_order_acquire) &&
			postResolveReady &&
			HandMirrorApprovedContentReadOnlyObserver::IsEnabled() &&
			SecondView::HandCaptureDependenciesReady() &&
			MirrorCameraOverride::HandMirrorRuntimeReady() &&
			MirrorPaneDelivery::SharedSeamReady() &&
			!MirrorPaneDelivery::Faulted() &&
			Reservation::CanEnableHandCaptureRuntime();
		MirrorCameraOverride::SetHandMirrorRuntimeEnabled(ready);
		g_enabled.store(ready, std::memory_order_release);
		g_loadTransitionSuspended.store(false, std::memory_order_release);
		SetHandMirrorEvidenceEventActive(ready);
	}

	void OnInventoryMenuOpenChanged(const bool open) noexcept
	{
		if (open) {
			g_inventoryResumeRequested.store(false, std::memory_order_release);
			g_inventoryTransitionSuspended.store(true, std::memory_order_release);
			g_inventoryMenuOpen.store(true, std::memory_order_release);
			return;
		}
		g_inventoryTransitionSuspended.store(true, std::memory_order_release);
		(void)g_inventoryMenuOpen.exchange(false, std::memory_order_acq_rel);
		g_inventoryResumeRequested.store(true, std::memory_order_release);
	}

	void OnPlayerEquipWake() noexcept
	{
		g_equipWakePending.store(true, std::memory_order_release);
	}

	bool LifecycleTransitionSuspended() noexcept
	{
		return HandLifecycleTransitionSuspended();
	}

	void FailStopCallbackFault() noexcept
	{
		FailStopInternal();
	}

	static FenceSelection EvaluateFenceOwnerUncounted(
		const std::uint64_t sourceSequence,
		const std::uint32_t wallFormID,
		const std::uint64_t wallGeneration,
		const bool exactWallEligible,
		const bool legacyWallPublicationExists,
		const Bridge::PriorPublicationIdentity& legacyWallPublication,
		const bool independentSlot) noexcept;

	// The decision has many exits; count the outcome in one place so the log can
	// say which one was taken without a counter at every return.
	FenceSelection EvaluateFenceOwner(
		const std::uint64_t sourceSequence,
		const std::uint32_t wallFormID,
		const std::uint64_t wallGeneration,
		const bool exactWallEligible,
		const bool legacyWallPublicationExists,
		const Bridge::PriorPublicationIdentity& legacyWallPublication,
		const bool independentSlot) noexcept
	{
		const auto outcome = EvaluateFenceOwnerUncounted(sourceSequence, wallFormID,
			wallGeneration, exactWallEligible, legacyWallPublicationExists,
			legacyWallPublication, independentSlot);
		auto& tally = State().diagnostics;
		if (!outcome.valid) ++tally.fenceInvalidSelections;
		else if (outcome.selection == MirrorSelection::kHand) ++tally.fenceHandSelections;
		else if (outcome.selection == MirrorSelection::kWall) ++tally.fenceWallSelections;
		else ++tally.fenceDarkSelections;
		return outcome;
	}

	FenceSelection EvaluateFenceOwnerUncounted(
		const std::uint64_t sourceSequence,
		const std::uint32_t wallFormID,
		const std::uint64_t wallGeneration,
		const bool exactWallEligible,
		const bool legacyWallPublicationExists,
		const Bridge::PriorPublicationIdentity& legacyWallPublication,
		const bool independentSlot) noexcept
	{
		FenceSelection output{};
		if (!IsEnabled() || sourceSequence == 0)
			return output;
		auto& state = State();
		if (g_inventoryMenuOpen.load(std::memory_order_acquire)) {
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			return output;
		}
		if (g_inventoryTransitionSuspended.load(std::memory_order_acquire)) {
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			if (g_inventoryMenuOpen.load(std::memory_order_acquire))
				return output;
			if (g_inventoryResumeRequested.exchange(
					false, std::memory_order_acq_rel)) {
				state.inventoryResumeFenceSource = sourceSequence;
				return output;
			}
			if (state.inventoryResumeFenceSource == 0 ||
				sourceSequence <= state.inventoryResumeFenceSource) {
				return output;
			}
			if (g_inventoryMenuOpen.load(std::memory_order_acquire))
				return output;
			state.inventoryResumeFenceSource = 0;
			g_inventoryTransitionSuspended.store(false, std::memory_order_release);
		}
		const bool equipWake =
			g_equipWakePending.exchange(false, std::memory_order_acq_rel);
		if (equipWake) {
			if (!ReleaseHeldPredecessorForLifecycle(state)) {
				FailStopInternal();
				return output;
			}
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			state.stablePresentation = {};
		}
		if (state.stagedWallRetirementGrant) {
			FailStopInternal();
			return output;
		}
		if (!EnsureRenderThread(state) || !AdvanceStoreSource(state, sourceSequence)) {
			FailStopInternal();
			return output;
		}

		auto& visibility = state.visibility;
		EquippedPresentationIdentityCopy equippedIdentity{};
		const auto equippedPresentationStatus =
			HandMirrorApprovedContentReadOnlyObserver::
				RunWithEquippedPresentationIdentity(
					&CopyEquippedPresentationIdentity, &equippedIdentity);
		using EquippedPresentationStatus =
			HandMirrorApprovedContentReadOnlyObserver::
				RuntimeEquippedPresentationStatus;
		if (equippedPresentationStatus ==
			EquippedPresentationStatus::kFaulted) {
			FailStopInternal();
			return output;
		}
		const bool exactEquippedHand = equippedPresentationStatus ==
				EquippedPresentationStatus::kExactEquipped &&
			equippedIdentity.copied && equippedIdentity.identity.valid;
		const bool positivelyUnequipped = equippedPresentationStatus ==
			EquippedPresentationStatus::kPositivelyUnequipped;
		const bool equipmentIndeterminate = equippedPresentationStatus ==
			EquippedPresentationStatus::kIndeterminate;
		if (positivelyUnequipped) {
			if (LastGoodPresentationValid(state) &&
				!ClearLastGoodPresentation(state)) {
				FailStopInternal();
				return output;
			}
			visibility = {};
		} else if (equipWake && exactEquippedHand) {
			const bool sameAcquiredGraph = exactEquippedHand &&
				visibility.currentOwner ==
					Content::MirrorOwnerSelection::kHand &&
				SameEquippedPresentationOwner(
					Bridge::MakeHandOwner(visibility.currentHandOwner),
					equippedIdentity.identity.owner);
			const bool sameLastGoodGraph = exactEquippedHand &&
				LastGoodPresentationValid(state) &&
				SameEquippedPresentationOwner(
					state.lastGoodPresentation.frame.owner,
					equippedIdentity.identity.owner);
			if (LastGoodPresentationValid(state) && !sameLastGoodGraph &&
				!ClearLastGoodPresentation(state)) {
				FailStopInternal();
				return output;
			}
			if (sameAcquiredGraph || sameLastGoodGraph) {
				visibility.accumulatedOwner = equippedIdentity.identity.owner;
				visibility.currentHandOwner = equippedIdentity.identity.owner;
				visibility.currentOwner =
					Content::MirrorOwnerSelection::kHand;
				visibility.consecutiveRaisedVisibleFrames = (std::max)(
					visibility.consecutiveRaisedVisibleFrames,
					Content::kHandAcquireHysteresisFrames);
				visibility.consecutiveIneligibleFrames = 0;
			} else {
				visibility = {};
			}
		} else if (equipWake && equipmentIndeterminate) {
			// An equip sink also fires for helmets and other unrelated biped
			// changes.  Until the shield slots positively prove absence or a fresh
			// exact graph proves replacement, retain the established mirror lease.
			if (LastGoodPresentationValid(state) &&
				state.lastGoodPresentation.frame.owner.kind ==
					Bridge::OwnerKind::kHand) {
				visibility.accumulatedOwner =
					state.lastGoodPresentation.frame.owner.hand;
				visibility.currentHandOwner =
					state.lastGoodPresentation.frame.owner.hand;
				visibility.currentOwner =
					Content::MirrorOwnerSelection::kHand;
				visibility.consecutiveRaisedVisibleFrames = (std::max)(
					visibility.consecutiveRaisedVisibleFrames,
					Content::kHandAcquireHysteresisFrames);
				visibility.consecutiveIneligibleFrames = 0;
			}
		}
		if (exactEquippedHand) {
			if (LastGoodPresentationValid(state) &&
				!SameEquippedPresentationOwner(
					state.lastGoodPresentation.frame.owner,
					equippedIdentity.identity.owner) &&
				!ClearLastGoodPresentation(state)) {
				FailStopInternal();
				return output;
			}
			visibility.accumulatedOwner = equippedIdentity.identity.owner;
			visibility.consecutiveIneligibleFrames = 0;
		}
		const bool establishedEquipmentLease = equipmentIndeterminate &&
			visibility.currentOwner == Content::MirrorOwnerSelection::kHand &&
			Content::IsValidHandMirrorOwnerIdentity(
				visibility.currentHandOwner) &&
			(!LastGoodPresentationValid(state) ||
			 SameEquippedPresentationOwner(
				 state.lastGoodPresentation.frame.owner,
				 visibility.currentHandOwner));
		const bool exactPriorFrameVisible =
			visibility.lastVisibleSourceSequence != 0 &&
			visibility.lastVisibleSourceSequence + 1 == sourceSequence &&
			!visibility.ambiguousThisSource &&
			Content::IsValidHandMirrorOwnerIdentity(visibility.accumulatedOwner);
		// Retain the historical one-source callback bridge as a fallback when the
		// equipped graph cannot be sampled. Exact equipped identity above is the
		// primary authority and no longer expires merely because a pane callback is
		// absent; post-world still revalidates the full capture candidate.
		const bool oneSourceHandContinuity =
			visibility.lastVisibleSourceSequence != 0 &&
			sourceSequence > visibility.lastVisibleSourceSequence &&
			sourceSequence - visibility.lastVisibleSourceSequence == 2 &&
			visibility.currentOwner == Content::MirrorOwnerSelection::kHand &&
			visibility.currentHandOwner == visibility.accumulatedOwner &&
			visibility.consecutiveIneligibleFrames == 0 &&
			!visibility.ambiguousThisSource &&
			Content::IsValidHandMirrorOwnerIdentity(visibility.accumulatedOwner);
		// Capture ownership follows the exact equipped object, not the incidental
		// presence of a native pane callback.  Callback evidence still supplies the
		// freshest live pose, but its absence may never turn an unchanged equipped
		// mirror dark or cancel the next private capture.
		const bool priorFrameVisible = exactEquippedHand ||
			establishedEquipmentLease ||
			exactPriorFrameVisible || oneSourceHandContinuity;
		const bool handSettingEligible = HandMirrorSettings::ReflectInPose(visibility.raisedPresentation);
		output.handSlotReserved = priorFrameVisible && handSettingEligible;
		output.raisedPresentation = visibility.raisedPresentation;
		if (!handSettingEligible && LastGoodPresentationValid(state) && !ClearLastGoodPresentation(state)) {
			FailStopInternal(); return output;
		}
		// One planar capture channel exists.  A lowered hand pane yields it to an
		// eligible standing/wall mirror so a placed mirror reflects while the hand
		// mirror stays equipped; raising the hand mirror takes the channel back.
		if (exactWallEligible)
			visibility.lastWallEligibleSource = sourceSequence;
		const bool wallRecentlyEligible = exactWallEligible ||
			(visibility.lastWallEligibleSource != 0 &&
				sourceSequence >= visibility.lastWallEligibleSource &&
				sourceSequence - visibility.lastWallEligibleSource <=
					LoweredPolicy::kWallYieldHoldSources);
		const bool handEligible = handSettingEligible && (independentSlot ? priorFrameVisible :
			LoweredPolicy::HandEligibleAgainstWall(
				priorFrameVisible, visibility.raisedPresentation, wallRecentlyEligible));
		state.diagnostics.fenceLastInputs =
			(priorFrameVisible ? 0x01u : 0u) | (handSettingEligible ? 0x02u : 0u) |
			(handEligible ? 0x04u : 0u) | (wallRecentlyEligible ? 0x08u : 0u) |
			(visibility.raisedPresentation ? 0x10u : 0u) |
			(exactEquippedHand ? 0x20u : 0u);
		if (priorFrameVisible && !handEligible) {
			++state.diagnostics.handLoweredYieldsToWall;
			if (!visibility.loweredYieldLogged) {
				visibility.loweredYieldLogged = true;
				logger::info(
					"[RR][HandMirrorReflection] lowered hand pane yields the capture channel to wall {:08X} generation={}",
					wallFormID, wallGeneration);
			}
		} else if (visibility.loweredYieldLogged && handEligible) {
			visibility.loweredYieldLogged = false;
			logger::info(
				"[RR][HandMirrorReflection] hand pane eligible again raised={} wallEligible={}",
				visibility.raisedPresentation, exactWallEligible);
		}
		if (!handEligible) {
			visibility.consecutiveRaisedVisibleFrames = 0;
			if (visibility.consecutiveIneligibleFrames !=
				(std::numeric_limits<std::uint32_t>::max)()) {
				++visibility.consecutiveIneligibleFrames;
			}
		}

		const Bridge::WallOwnerIdentity wall{
			exactWallEligible ? wallFormID : 0,
			exactWallEligible ? wallGeneration : 0 };
		if (state.ownerLeaseCursor ==
			(std::numeric_limits<std::uint64_t>::max)()) {
			FailStopInternal();
			return output;
		}
		const Content::HandWallArbitrationInput arbitration{
			.currentOwner = visibility.currentOwner,
			.candidateHandOwner = handEligible ?
				(exactEquippedHand ? equippedIdentity.identity.owner :
					visibility.accumulatedOwner) :
				Content::HandMirrorOwnerIdentity{},
			.accumulatedHandOwner = visibility.accumulatedOwner,
			.currentHandOwner = visibility.currentHandOwner,
			.consecutiveRaisedVisibleHandFrames =
				visibility.consecutiveRaisedVisibleFrames,
			.consecutiveHandIneligibleFrames =
				visibility.consecutiveIneligibleFrames,
			.exactHandEligible = handEligible,
			.exactWallEligible = exactWallEligible,
			// A duplicate returned hand pane makes the *hand* identity ambiguous.
			// While the hand is not a candidate (lowered, yielded to the wall) that
			// ambiguity must not darken the wall: the wall's private capture renders
			// the third-person clone's pane, which itself counted as a second
			// return (owner run 2026-09-04 15:24: wall captured every 6th source,
			// 8,070 holds against 1,712 captures).
			.ownerIdentityAmbiguous = visibility.ambiguousThisSource && handEligible
		};
		Bridge::OwnerGrantInput grantInput{
			.arbitration = arbitration,
			.exactWallCandidate = wall,
			.eligibleHandCandidates = { handEligible ? 1U : 0U, false },
			.previousPublication = legacyWallPublication,
			.retirementReceipt = {},
			.nextOwnerLeaseSequence = state.ownerLeaseCursor + 1,
			.wallCandidateCurrent = exactWallEligible
		};
		const bool legacyWallPublicationAcquirable =
			legacyWallPublicationExists;
		if (legacyWallPublicationExists !=
			(legacyWallPublication != Bridge::PriorPublicationIdentity{})) {
			return output;
		}
		if (legacyWallPublicationAcquirable) {
			if (!Bridge::IsValidPriorPublication(legacyWallPublication)) {
				return output;
			}
			// Never claim the singleton is empty while it is acquirable. Retain an
			// exact wall decision, or stage hand/dark until the selected kMirror
			// opportunity atomically compare-and-retires this exact publication.
			const auto desired = Content::SelectMirrorOwner(arbitration);
			output.sourceSequence = sourceSequence;
			output.exactPriorFrameHandVisible = priorFrameVisible;
			const auto currentWallOwner = Bridge::MakeWallOwner(wall);
			if (desired == Content::MirrorOwnerSelection::kWall &&
				Bridge::IsValidWallOwner(wall) &&
				legacyWallPublication.owner == currentWallOwner) {
				output.selection = MirrorSelection::kWall;
				output.owner = currentWallOwner;
				output.ownerLeaseSequence =
					legacyWallPublication.ownerLeaseSequence;
				output.valid = true;
				return output;
			}
			// V146: a raised hand and the retained wall publication coexist.  The
			// hand and wall private targets are physically disjoint, so the hand
			// grant no longer retires the wall frame; when the same wall is also
			// eligible this source the two owners take turns source by source.
			if (desired == Content::MirrorOwnerSelection::kHand) {
				const bool sameWallCurrent = Bridge::IsValidWallOwner(wall) &&
					legacyWallPublication.owner == currentWallOwner;
				// V153: source-by-source alternation kept the wall live but the hand
				// pane presents only a publication captured on its own source, so it
				// went dark on every wall source (owner run 18:37).  Until that
				// presentation contract admits a one-source-old frame, a raised hand
				// captures every source and the wall keeps its last frame (frozen,
				// never black); lowering the hand returns the wall to live.
				constexpr bool kAlternateHandAndWall = true;
				const bool handCapturedLastSource = kAlternateHandAndWall &&
					visibility.lastHandCaptureSource != 0 &&
					visibility.lastHandCaptureSource + 1 == sourceSequence;
				if (sameWallCurrent && handCapturedLastSource) {
					++state.diagnostics.interleaveWallSources;
					output.selection = MirrorSelection::kWall;
					output.owner = currentWallOwner;
					output.ownerLeaseSequence =
						legacyWallPublication.ownerLeaseSequence;
					output.legacyWallPublicationRetirementRequired = false;
					output.valid = true;
					return output;
				}
				Bridge::OwnerGrantInput coexistInput = grantInput;
				coexistInput.previousPublication = {};
				coexistInput.retirementReceipt = {};
				const auto coexistGrant = Bridge::DecideOwnerGrant(coexistInput);
				if (coexistGrant.status == Bridge::OwnerGrantStatus::kGrantedHand) {
					++state.diagnostics.interleaveHandSources;
					output.selection = MirrorSelection::kHand;
					output.owner = coexistGrant.owner;
					output.ownerLeaseSequence = coexistGrant.ownerLeaseSequence;
					output.legacyWallPublicationRetirementRequired = false;
					output.valid = true;
					return output;
				}
				// No hand grant this source: keep the wall frame, present dark-side
				// for the hand rather than retiring the wall.
				++state.diagnostics.interleaveHandGrantHolds;
				output.selection = MirrorSelection::kDark;
				output.owner = {};
				output.legacyWallPublicationRetirementRequired = false;
				output.valid = true;
				return output;
			}
			// The fence runs every source but the mirror channel is scheduled only
			// every few sources; on the sources in between the wall is reported
			// ineligible.  Retiring the completed wall frame there (V129–V139:
			// every publication retired 4 ms later, a fresh lease each mirror
			// fence, PaneDelivery noFrame == every hit) is wrong: with no hand
			// claim and no *different* wall, hold the frame dark-side until the
			// next mirror opportunity re-selects the same wall.
			if (desired != Content::MirrorOwnerSelection::kHand &&
				(!Bridge::IsValidWallOwner(wall) ||
					legacyWallPublication.owner == currentWallOwner)) {
				++state.diagnostics.wallPublicationHolds;
				if (!exactWallEligible)
					++state.diagnostics.wallPublicationHoldsWallIneligible;
				if (arbitration.ownerIdentityAmbiguous)
					++state.diagnostics.wallPublicationHoldsAmbiguous;
				if (exactWallEligible && !arbitration.ownerIdentityAmbiguous)
					++state.diagnostics.wallPublicationHoldsHandHysteresis;
				{
					static std::atomic<std::uint32_t> holdLogBudget{ 40 };
					auto budget = holdLogBudget.load(std::memory_order_acquire);
					if ((state.diagnostics.wallPublicationHolds % 25) == 1 &&
						budget != 0 &&
						holdLogBudget.compare_exchange_strong(
							budget, budget - 1, std::memory_order_acq_rel)) {
						logger::info(
							"[RR][M3][wall-hold] source={} wallEligible={} ambiguous={} rawAmbiguous={} handEligible={} priorVisible={} raised={} currentOwner={} ineligibleFrames={} desired={} holds={}",
							sourceSequence, exactWallEligible,
							arbitration.ownerIdentityAmbiguous,
							visibility.ambiguousThisSource, handEligible,
							priorFrameVisible, visibility.raisedPresentation,
							static_cast<int>(visibility.currentOwner),
							visibility.consecutiveIneligibleFrames,
							static_cast<int>(desired),
							state.diagnostics.wallPublicationHolds);
					}
				}
				output.selection = MirrorSelection::kDark;
				output.owner = {};
				output.legacyWallPublicationRetirementRequired = false;
				output.valid = true;
				return output;
			}
			// Replacing wall A with a different wall B is deliberately two-source:
			// retire A to dark now, then allow B to earn a fresh grant next frame.
			if (desired == Content::MirrorOwnerSelection::kWall) {
				grantInput.arbitration.currentOwner =
					Content::MirrorOwnerSelection::kDark;
				grantInput.arbitration.exactHandEligible = false;
				grantInput.arbitration.exactWallEligible = false;
				grantInput.arbitration.candidateHandOwner = {};
				grantInput.exactWallCandidate = {};
				grantInput.eligibleHandCandidates = {};
				grantInput.wallCandidateCurrent = false;
			}
			state.stagedWallRetirementGrant = grantInput;
			state.stagedWallRetirementSource = sourceSequence;
			output.selection = desired == Content::MirrorOwnerSelection::kHand ?
				MirrorSelection::kHand : MirrorSelection::kDark;
			output.owner = desired == Content::MirrorOwnerSelection::kHand ?
				Bridge::MakeHandOwner(arbitration.candidateHandOwner) :
				Bridge::MirrorOwnerIdentity{};
			output.legacyWallPublicationRetirementRequired = true;
			output.valid = true;
			return output;
		}
		const auto grant = Bridge::DecideOwnerGrant(grantInput);
		output.sourceSequence = sourceSequence;
		output.exactPriorFrameHandVisible = priorFrameVisible;
		output.owner = grant.owner;
		output.ownerLeaseSequence = grant.ownerLeaseSequence;
		output.valid = grant.status == Bridge::OwnerGrantStatus::kGrantedHand ||
			grant.status == Bridge::OwnerGrantStatus::kGrantedWall ||
			grant.status == Bridge::OwnerGrantStatus::kDarkNoEligibleOwner ||
			grant.status == Bridge::OwnerGrantStatus::kDarkAmbiguous;
		if (grant.status == Bridge::OwnerGrantStatus::kGrantedHand)
			output.selection = MirrorSelection::kHand;
		else if (grant.status == Bridge::OwnerGrantStatus::kGrantedWall)
			output.selection = MirrorSelection::kWall;
		return output;
	}

	FenceSelection CompleteFenceOwnerAfterWallRetirement(
		const FenceSelection& staged,
		const StorePolicy::Store::PhysicalTargets& handTargets) noexcept
	{
		FenceSelection output{};
		auto& state = State();
		if (!IsEnabled() || !staged.valid ||
			!staged.legacyWallPublicationRetirementRequired ||
			!state.stagedWallRetirementGrant ||
			state.stagedWallRetirementSource != staged.sourceSequence ||
			!Bridge::ArePrivateTargetsPhysicallyDisjoint(
				handTargets[0], handTargets[1])) {
			state.stagedWallRetirementGrant.reset();
			state.stagedWallRetirementSource = 0;
			FailStopInternal();
			return output;
		}
		auto grantInput = *state.stagedWallRetirementGrant;
		const auto desired = staged.selection == MirrorSelection::kHand ?
			staged.owner : Bridge::MirrorOwnerIdentity{};
		Bridge::PublicationRetirementReceipt retirement{};
		const std::uint64_t successorLease =
			Bridge::IsValidOwner(desired) ? state.ownerLeaseCursor + 1 : 0;
		const auto retirementIssue = Bridge::IssueNextToken(
			state.wallRetirementTokens);
		state.wallRetirementTokens = retirementIssue.nextState;
		if (retirementIssue.status != Bridge::TokenIssueStatus::kIssued ||
			!Bridge::IsValidTokenIssueReceipt(retirementIssue.receipt)) {
			state.stagedWallRetirementGrant.reset();
			state.stagedWallRetirementSource = 0;
			FailStopInternal();
			return output;
		}
		const auto retirementStatus =
			MirrorFramePublication::RetireExactForSuccessor(
				grantInput.previousPublication, desired, successorLease,
				retirementIssue.receipt.issuedToken, handTargets, retirement);
		if (retirementStatus !=
			MirrorFramePublication::ExactRetirementStatus::kRetired) {
			state.stagedWallRetirementGrant.reset();
			state.stagedWallRetirementSource = 0;
			if (retirementStatus ==
				MirrorFramePublication::ExactRetirementStatus::kInvalidEvidence) {
				FailStopInternal();
			}
			return output;
		}
		MirrorPaneDelivery::OnFrameInvalidated();
		// Exact retirement linearized the singleton to dark.  Keep that state even
		// if successor validation or reservation fails later in this source.
		state.visibility.currentOwner = Content::MirrorOwnerSelection::kDark;
		state.visibility.currentHandOwner = {};
		grantInput.retirementReceipt = retirement;
		const auto grant = Bridge::DecideOwnerGrant(grantInput);
		state.stagedWallRetirementGrant.reset();
		state.stagedWallRetirementSource = 0;
		output.sourceSequence = staged.sourceSequence;
		output.exactPriorFrameHandVisible = staged.exactPriorFrameHandVisible;
		output.owner = grant.owner;
		output.ownerLeaseSequence = grant.ownerLeaseSequence;
		output.valid = grant.status == Bridge::OwnerGrantStatus::kGrantedHand ||
			grant.status == Bridge::OwnerGrantStatus::kGrantedWall ||
			grant.status == Bridge::OwnerGrantStatus::kDarkNoEligibleOwner ||
			grant.status == Bridge::OwnerGrantStatus::kDarkAmbiguous;
		if (grant.status == Bridge::OwnerGrantStatus::kGrantedHand)
			output.selection = MirrorSelection::kHand;
		else if (grant.status == Bridge::OwnerGrantStatus::kGrantedWall)
			output.selection = MirrorSelection::kWall;
		if (!output.valid && successorLease != 0 &&
			!BurnUnusedOwnerGrant(state, successorLease)) {
			FailStopInternal();
		}
		return output;
	}

	void AbandonFenceSelection(const FenceSelection& selection) noexcept
	{
		auto& state = State();
		if (!selection.valid)
			return;
		if (selection.legacyWallPublicationRetirementRequired) {
			if (!state.stagedWallRetirementGrant ||
				state.stagedWallRetirementSource != selection.sourceSequence) {
				FailStopInternal();
				return;
			}
			state.stagedWallRetirementGrant.reset();
			state.stagedWallRetirementSource = 0;
			return;
		}
		if (selection.selection == MirrorSelection::kDark) {
			state.visibility.currentOwner = Content::MirrorOwnerSelection::kDark;
			state.visibility.currentHandOwner = {};
			return;
		}
		if (selection.ownerLeaseSequence != 0 &&
			selection.ownerLeaseSequence <= state.ownerLeaseCursor) {
			// An exact preserved wall lease was already issued by its producer
			// (possibly before a later hand grant advanced the cursor).
			return;
		}
		if (!BurnUnusedOwnerGrant(state, selection.ownerLeaseSequence))
			FailStopInternal();
	}

	void CommitFenceSelection(
		const FenceSelection& selection,
		const bool mirrorSchedulerOpportunitySelected) noexcept
	{
		if (!IsEnabled() || !selection.valid)
			return;
		auto& state = State();
		auto& visibility = state.visibility;
		if (selection.selection == MirrorSelection::kDark) {
			visibility.currentOwner = Content::MirrorOwnerSelection::kDark;
			visibility.currentHandOwner = {};
			return;
		}
		if (!mirrorSchedulerOpportunitySelected)
			return;
		if (selection.selection == MirrorSelection::kHand) {
			visibility.currentOwner = Content::MirrorOwnerSelection::kHand;
			visibility.currentHandOwner = selection.owner.hand;
			visibility.lastHandCaptureSource = selection.sourceSequence;
		} else {
			// A newly selected wall earns a real owner lease. A preserved exact wall
			// carries the already-issued lease and does not advance either issuer.
			// A re-selected wall carries the lease issued when it was first granted.
			// Since V146 a hand grant may have advanced the cursor past it (hand /
			// wall interleave), so any lease at or below the cursor is an
			// already-issued, preserved lease; only a fresh cursor+1 grant the wall
			// does not consume is burned (17:41 run: fail-stop from here on the first
			// wall source after a hand grant, hand dark for the session).
			const bool preservedLease = selection.ownerLeaseSequence != 0 &&
				selection.ownerLeaseSequence <= state.ownerLeaseCursor;
			if (selection.ownerLeaseSequence != 0 && !preservedLease &&
				!BurnUnusedOwnerGrant(state, selection.ownerLeaseSequence)) {
				FailStopInternal();
				return;
			}
			visibility.currentOwner = Content::MirrorOwnerSelection::kWall;
			visibility.currentHandOwner = {};
		}
	}

	namespace
	{
		enum class PrivateTargetRetargetOutcome : std::uint8_t
		{
			kRetargeted,
			kDeferred,
			kViolation
		};

		/**
		 * Adopt a freshly allocated pair of private hand targets.
		 *
		 * Both hand resolutions became 512-4096 settings on 2026-09-14 and the quality
		 * levels moved them again on 2026-09-15, so SecondView re-creates the two
		 * private targets whenever the player changes the level or either slider, and
		 * their `allocationGeneration` changes with every re-creation.  The identity
		 * the store was initialised with then stops matching, which used to reach the
		 * `state.targets != targets` fail-stop in ReserveHandAtFence and kill the hand
		 * mirror for the rest of the session.  Owner, 2026-09-16: "I changed the
		 * handmirror to low quality and it became dark and doesn't turn on anymore" --
		 * the SE+ENB run fail-stopped in ReserveHandAtFence four seconds after
		 * `[MOS][Settings] quality level=0`, with `handTargetReallocations=2` and
		 * `backing=1024x1024` naming the reallocation that did it.
		 *
		 * So re-identify instead, on the same terms the post-load reset uses: every
		 * publication built against the retired allocation is invalidated, released and
		 * scrubbed before the new identity is adopted.  Nothing may be in flight, which
		 * is a state the caller can simply wait for -- a deferred retarget declines one
		 * source without faulting and the next source tries again.  Only a failure
		 * *after* the store has been re-identified is a real invariant violation,
		 * because the runtime and the store would then disagree about the allocation.
		 */
		[[nodiscard]] PrivateTargetRetargetOutcome RetargetPrivateTargets(
			RuntimeState& state,
			const StorePolicy::Store::PhysicalTargets& targets) noexcept
		{
			if (!state.store || !state.coordinator ||
				!Bridge::ArePrivateTargetsPhysicallyDisjoint(targets[0], targets[1]) ||
				state.loadGeneration == 0 ||
				state.loadGeneration == (std::numeric_limits<std::uint64_t>::max)()) {
				return PrivateTargetRetargetOutcome::kViolation;
			}
			if (state.store->Inspect().phase != StorePolicy::StorePhase::kReady)
				return PrivateTargetRetargetOutcome::kViolation;
			const auto coordinatorAudit = state.coordinator->Inspect();
			if (coordinatorAudit.activeTicketCount != 0 ||
				coordinatorAudit.pendingReservationCount != 0 ||
				state.activeInternalReaders != 0 ||
				state.pendingGrantedOwnerLease != 0 ||
				Reservation::IsValidFenceReservation(state.pendingReservation)) {
				return PrivateTargetRetargetOutcome::kDeferred;
			}
			if (!ReleaseHeldPredecessor(state))
				return PrivateTargetRetargetOutcome::kDeferred;
			const auto nextLoadGeneration = state.loadGeneration + 1;
			StorePolicy::InvalidationResult invalidation{};
			const auto status =
				state.store->Retarget(targets, nextLoadGeneration, invalidation);
			if (status == StorePolicy::InvalidationStatus::kQuarantined ||
				status == StorePolicy::InvalidationStatus::kUnavailable ||
				status == StorePolicy::InvalidationStatus::kInvalidEvidence ||
				status == StorePolicy::InvalidationStatus::kTokenExhausted) {
				return PrivateTargetRetargetOutcome::kViolation;
			}
			if (!ReleaseRetirement(state, invalidation.retirement) ||
				!ScrubNativeSlotsForFailStop(state) ||
				!ClearLastGoodPresentation(state)) {
				return PrivateTargetRetargetOutcome::kViolation;
			}
			state.loadGeneration = nextLoadGeneration;
			state.targets = targets;
			state.currentPublication = {};
			state.heldPredecessorRetirement = {};
			state.heldCleanMissExpirySource = 0;
			state.heldCleanMissValidatedSource = 0;
			state.lastPresentedPublicationToken = 0;
			state.stagedWallRetirementGrant.reset();
			state.stagedWallRetirementSource = 0;
			state.stablePresentation = {};
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			g_privateTargetRetargets.fetch_add(1, std::memory_order_relaxed);
			logger::info(
				"[RR][HandMirrorReflection] private hand targets re-identified after a "
				"reallocation: loadGeneration={} allocationGenerations={}/{} retargets={}",
				nextLoadGeneration, targets[0].allocationGeneration,
				targets[1].allocationGeneration,
				g_privateTargetRetargets.load(std::memory_order_relaxed));
			return PrivateTargetRetargetOutcome::kRetargeted;
		}
	}

	bool ReserveHandAtFence(
		const FenceSelection& selection,
		const std::uint64_t mainViewFrame,
		const StorePolicy::Store::PhysicalTargets& targets) noexcept
	{
		auto& state = State();
		const bool exactPregrant = IsEnabled() && selection.valid &&
			selection.selection == MirrorSelection::kHand &&
			selection.sourceSequence != 0 &&
			selection.owner.kind == Bridge::OwnerKind::kHand &&
			selection.ownerLeaseSequence != 0 &&
			state.ownerLeaseCursor !=
				(std::numeric_limits<std::uint64_t>::max)() &&
			selection.ownerLeaseSequence == state.ownerLeaseCursor + 1;
		if (!exactPregrant) {
			LogStoreAuditNoexcept(state, "reserve-pregrant", static_cast<int>(selection.selection));
			return false;
		}
		if (HandLifecycleTransitionSuspended()) {
			LogStoreAuditNoexcept(state, "reserve-lifecycle-suspended", 0);
			if (!ReleaseHeldPredecessorForLifecycle(state)) {
				FailStopInternal();
				return false;
			}
			if (!BurnUnusedOwnerGrant(state, selection.ownerLeaseSequence))
				FailStopInternal();
			return false;
		}
		if (mainViewFrame == 0 ||
			!Bridge::ArePrivateTargetsPhysicallyDisjoint(targets[0], targets[1])) {
			LogStoreAuditNoexcept(state, "reserve-targets", static_cast<int>(mainViewFrame));
			if (!BurnUnusedOwnerGrant(state, selection.ownerLeaseSequence))
				FailStopInternal();
			return false;
		}
		if (!EnsureRenderThread(state)) {
			FailStopInternal();
			return false;
		}
		if (!state.store)
			state.store.emplace();
		if (!state.coordinator)
			state.coordinator.emplace();
		if (!state.targetsInitialized) {
			StorePolicy::TokenSeeds seeds{};
			seeds.ownerLease = { state.ownerLeaseCursor, false };
			if (!state.store->Initialize(
					targets, selection.sourceSequence, state.loadGeneration, seeds) ||
				!state.coordinator->Initialize()) {
				FailStopInternal();
				return false;
			}
			state.targets = targets;
			state.targetsInitialized = true;
		} else if (state.targets != targets) {
			// A changed physical identity is a hand-resolution reallocation, not a
			// violation: adopt it, wait for it, or fault -- but never fault merely
			// because the allocation the player asked for is a different one.
			switch (RetargetPrivateTargets(state, targets)) {
			case PrivateTargetRetargetOutcome::kRetargeted:
				if (!AdvanceStoreSource(state, selection.sourceSequence)) {
					FailStopInternal();
					return false;
				}
				break;
			case PrivateTargetRetargetOutcome::kDeferred:
				LogStoreAuditNoexcept(state, "reserve-retarget-deferred", 0);
				if (!BurnUnusedOwnerGrant(state, selection.ownerLeaseSequence))
					FailStopInternal();
				return false;
			case PrivateTargetRetargetOutcome::kViolation:
			default:
				FailStopInternal();
				return false;
			}
		} else if (!AdvanceStoreSource(state, selection.sourceSequence)) {
			FailStopInternal();
			return false;
		}

		Reservation::FenceReservation reservation{};
		const auto status = state.coordinator->ReserveAtFence(
			*state.store,
			{
				.sourceSequence = selection.sourceSequence,
				.mainViewFrame = mainViewFrame,
				.schedulerChannel = Reservation::Channel::kMirror,
				.schedulerBudgetReserved = true,
				.exactHandCandidates = { 1, false },
				.owner = selection.owner
			},
			reservation);
		if (status != Reservation::ReserveStatus::kReserved) {
			LogStoreAuditNoexcept(state, "reserve-status", static_cast<int>(status));
			if (status != Reservation::ReserveStatus::kDarkNoExactHand &&
				status != Reservation::ReserveStatus::kDarkSchedulerRejected)
				FailStopInternal();
			if (!BurnUnusedOwnerGrant(state, selection.ownerLeaseSequence))
				FailStopInternal();
			return false;
		}
		state.pendingReservation = reservation;
		state.pendingGrantedOwnerLease = selection.ownerLeaseSequence;
		return true;
	}

	bool HasPendingHandReservation(
		const std::uint64_t sourceSequence,
		const std::uint64_t mainViewFrame) noexcept
	{
		const auto& pending = State().pendingReservation;
		return IsEnabled() && Reservation::IsValidFenceReservation(pending) &&
		       pending.sourceSequence == sourceSequence &&
		       (mainViewFrame == 0 || pending.mainViewFrame == mainViewFrame);
	}

	bool CancelPendingHandReservation(const std::uint64_t sourceSequence) noexcept
	{
		auto& state = State();
		if (!state.store || !state.coordinator ||
			!Reservation::IsValidFenceReservation(state.pendingReservation) ||
			state.pendingReservation.sourceSequence != sourceSequence) {
			return false;
		}
		const auto status = state.coordinator->CancelFenceReservation(
			*state.store, state.pendingReservation);
		state.pendingReservation = {};
		if (status != Reservation::CancelReservationStatus::kCancelledDark) {
			FailStopInternal();
			state.pendingGrantedOwnerLease = 0;
			return false;
		}
		const auto grantedLease = state.pendingGrantedOwnerLease;
		state.pendingGrantedOwnerLease = 0;
		if (!BurnUnusedOwnerGrant(state, grantedLease)) {
			FailStopInternal();
			return false;
		}
		return true;
	}

	bool BeginPostWorldCapture(
		const Bridge::MovingSurfaceSample& surface,
		const ReceiptPolicy::FrameReceipt& receipt,
		const Bridge::PrivateTargetIdentity& mainViewTarget,
		CaptureLease& output) noexcept
	{
		output = {};
		auto& state = State();
		if (HandLifecycleTransitionSuspended()) {
			if (Reservation::IsValidFenceReservation(state.pendingReservation) &&
				state.pendingReservation.sourceSequence == surface.sourceSequence) {
				(void)CancelPendingHandReservation(surface.sourceSequence);
			}
			if (!ReleaseHeldPredecessorForLifecycle(state))
				FailStopInternal();
			return false;
		}
		if (!IsEnabled() || !state.store || !state.coordinator ||
			!Reservation::IsValidFenceReservation(state.pendingReservation) ||
			!ReceiptPolicy::IsValidFrameReceipt(receipt) ||
			receipt.sourceSequence != surface.sourceSequence ||
			receipt.mainViewFrame != surface.pose.mainViewFrame ||
			!FinalFirstPersonDeliveryPending(receipt)) {
			return false;
		}
		Reservation::CaptureTicket ticket{};
		const auto status = state.coordinator->BeginPostWorldCapture(
			*state.store,
			{
				.reservation = state.pendingReservation,
				.frameReceipt = receipt,
				.firstPersonSurface = surface,
				.mainViewTarget = mainViewTarget,
				.timing = Reservation::CaptureTiming::kPostWorldBeforeFirstPerson,
				.nativeWorldReturnedNormally = true,
				.schedulerReservationStillExclusive = true
			},
			ticket);
		state.pendingReservation = {};
		if (status != Reservation::PostWorldStatus::kCaptureTicketReady) {
			LogStoreAuditNoexcept(state, "begin-post-world", static_cast<int>(status));
			const auto grantedLease = state.pendingGrantedOwnerLease;
			state.pendingGrantedOwnerLease = 0;
			if (!BurnUnusedOwnerGrant(state, grantedLease))
				FailStopInternal();
			return false;
		}
		if (!ReleaseRetirement(state, ticket.retiredPriorPublication)) {
			(void)state.coordinator->AbortCapture(*state.store, ticket);
			FailStopInternal();
			return false;
		}
		if (ticket.attempt.ownerLeaseSequence !=
				state.pendingGrantedOwnerLease ||
			ticket.attempt.ownerLeaseSequence != state.ownerLeaseCursor + 1) {
			(void)state.coordinator->AbortCapture(*state.store, ticket);
			FailStopInternal();
			return false;
		}
		state.pendingGrantedOwnerLease = 0;
		state.ownerLeaseCursor = ticket.attempt.ownerLeaseSequence;
		state.diagnostics.lastReceipt = receipt;
		++state.diagnostics.captureBegins;
		output = { ticket, true };
		return true;
	}

	void AbortPostWorldCapture(const CaptureLease& lease) noexcept
	{
		auto& state = State();
		if (!lease.valid || !state.store || !state.coordinator)
			return;
		if (state.coordinator->AbortCapture(*state.store, lease.ticket) !=
			Reservation::AbortStatus::kAbortedDark) {
			FailStopInternal();
		}
	}

	bool DiscardCompletedNativeFrame(CompletedNativeFrame& nativeFrame) noexcept
	{
		const bool released = ScrubCompletedNativeFrameSEH(nativeFrame);
		if (!released)
			FailStopInternal();
		return released;
	}

	bool CompletePostWorldCapture(
		const CaptureLease& lease,
		const ReceiptPolicy::FrameReceipt& currentReceipt,
		const Bridge::PrivateTargetIdentity& mainViewTarget,
		const StorePolicy::PublicationCleanupProof& cleanup,
		CompletedNativeFrame&& nativeFrame) noexcept
	{
		auto& state = State();
		if (!IsEnabled() || !lease.valid || !state.store || !state.coordinator) {
			const bool frameReleased = ScrubCompletedNativeFrameSEH(nativeFrame);
			AbortPostWorldCapture(lease);
			if (lease.valid || !frameReleased)
				FailStopInternal();
			return false;
		}
		bool nativeQueryFault = false;
		if (!ValidateCompletedNativeFrame(
				lease, nativeFrame, nativeQueryFault)) {
			const bool frameReleased = ScrubCompletedNativeFrameSEH(nativeFrame);
			AbortPostWorldCapture(lease);
			// A clean value mismatch is source-local and may occur while the equipped
			// first-person item advances between the private return and publication.
			// Retry on the next source; only native query/release failure is terminal.
			if (nativeQueryFault || !frameReleased)
				FailStopInternal();
			return false;
		}
		// Linearize the final lifecycle decision as close as possible to the
		// Store/Coordinator publication.  The render-thread caller also checks
		// before entering, but InventoryMenu/equip can flip while it retains SRVs.
		if (HandLifecycleTransitionSuspended()) {
			AbortPostWorldCapture(lease);
			const bool frameReleased =
				ScrubCompletedNativeFrameSEH(nativeFrame);
			if (!frameReleased)
				FailStopInternal();
			return false;
		}
		StorePolicy::PublicationSnapshot publication{};
		const auto status = state.coordinator->CompleteCapture(
			*state.store, lease.ticket,
			{
				.currentFrameReceipt = currentReceipt,
				.currentMainViewTarget = mainViewTarget,
				.timing = Reservation::CaptureTiming::kPostWorldBeforeFirstPerson,
				.exactMirrorRenderAttemptConsumed = true,
				// Walking can place the post-world callback after an exact pane return.
				// Publication remains legal only while that same receipt owns a retained
				// target and the enclosing first-person final delivery is still pending.
				.finalFirstPersonDeliveryPending =
					FinalFirstPersonDeliveryPending(currentReceipt)
			}, cleanup, publication);
		if (status != Reservation::CompleteStatus::kPublished) {
			LogStoreAuditNoexcept(state, "complete-capture", static_cast<int>(status));
			const bool frameReleased = ScrubCompletedNativeFrameSEH(nativeFrame);
			if (status != Reservation::CompleteStatus::kDarkStale ||
				!frameReleased) {
				FailStopInternal();
			}
			return false;
		}
		if (!StorePolicy::IsExactInternalHandPublication(publication) ||
			publication.physicalRole >= state.nativeSlots.size()) {
			(void)ScrubCompletedNativeFrameSEH(nativeFrame);
			FailStopInternal();
			return false;
		}
		MirrorPaneRenderer::PublishedFrame rendererFrame{
			.colorSRV = std::move(nativeFrame.colorSRV),
			.depthSRV = std::move(nativeFrame.depthSRV),
			.reflectedViewProjection = nativeFrame.reflectedViewProjection,
			.reflectedOrigin = nativeFrame.reflectedOrigin,
			.capturePlane = nativeFrame.capturePlane,
			.owner = publication.publication.attempt.owner,
			.audience = Bridge::PublicationAudience::kInternalHandOnly,
			.captureSequence = publication.publication.captureSequence,
			.width = nativeFrame.width,
			.height = nativeFrame.height,
			.colorFormat = nativeFrame.colorFormat,
			.valid = true
		};
		// Ownership moved to rendererFrame; clear every remaining scalar identity
		// in the caller's frame before either publishing or faulting.
		const auto capturedPresentationMode =
			LastGoodPolicy::ClassifyPresentationMode(nativeFrame.raisedPresentation);
		nativeFrame = {};
		auto& publicationSlot = state.nativeSlots[publication.physicalRole];
		if (publicationSlot.valid || publicationSlot.frame.colorSRV ||
			publicationSlot.frame.depthSRV) {
			if (!ClearNativeRole(state, publication.physicalRole)) {
				(void)ScrubRendererFrameSEH(rendererFrame);
				FailStopInternal();
				return false;
			}
		}
		publicationSlot = {
			std::move(rendererFrame), publication, capturedPresentationMode, true };
		state.currentPublication = publication;
		state.lastPresentedPublicationToken = 0;
		state.diagnostics.lastReceipt = currentReceipt;
		++state.diagnostics.publications;
		return true;
	}

	void OnFirstPersonEnter() noexcept
	{
		if (!MirrorPerformance::RenderingEnabled()) return;
		if (!IsEnabled())
			return;
		if (HandLifecycleTransitionSuspended()) {
			auto& state = State();
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			if (!ReleaseHeldPredecessorForLifecycle(state)) {
				FailStopInternal();
				return;
			}
			g_firstPerson = {};
			g_firstPerson.lifecycleTransitionSuppressed = true;
			return;
		}
		if (g_firstPerson.active) {
			FailStopInternal();
			return;
		}
		g_firstPerson = {};
		g_firstPerson.active = true;
		auto& state = State();
		if (!EnsureRenderThread(state)) {
			FailStopInternal();
			return;
		}
		++state.diagnostics.zeroFallbackEntryRetainAttempts;
		const auto sourceSequence = SecondView::CurrentMainWorldSourceSequence();
		MirrorPaneDelivery::CurrentMainDrawState main{};
		const auto mainStatus = QueryHandDrawState(main);
		RecordCurrentMainDrawStateStatus(state.diagnostics, mainStatus);
		if (sourceSequence != 0 &&
			mainStatus == MirrorPaneDelivery::CurrentMainDrawStateStatus::kReady &&
			main.valid && ReturnedPresentationComplete(main)) {
			const ReceiptPolicy::FrameReceipt receipt{
				sourceSequence, main.mainWorldFrame, main.graphicsFrame };
			const bool receiptValid = ReceiptPolicy::IsValidFrameReceipt(receipt);
			if (receiptValid &&
				MirrorPaneDelivery::TryRetainCurrentMainDrawTarget(
					main, g_firstPerson.outerTargetAtEntry) &&
				g_firstPerson.outerTargetAtEntry) {
				const auto liveTarget = MakeLiveDeliveryTargetIdentity(
					g_firstPerson.outerTargetAtEntry);
				if (LiveReturn::IsValidLiveDeliveryTarget(liveTarget)) {
					g_firstPerson.outerReceiptAtEntry = receipt;
					g_firstPerson.outerCaptureTargetAtEntry =
						main.captureReceiptTarget;
					g_firstPerson.outerLiveTargetAtEntry = liveTarget;
					g_firstPerson.outerTargetEntryValid = true;
					++state.diagnostics.zeroFallbackEntryRetains;
				} else {
					g_firstPerson.outerTargetAtEntry.reset();
				}
			}
			CandidateCopy candidate{};
			const bool candidateReturned = receiptValid &&
				HandMirrorApprovedContentReadOnlyObserver::
					RunWithOuterFirstPersonRuntimeCandidate(
						receipt, main.origin, &CopyCandidate, &candidate);
			if (!candidateReturned &&
				HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
				FailStopInternal();
				return;
			}
			// Surface evidence is needed only for a fresh zero-callback publication;
			// detached last-good replay can still use the independently proven target.
			if (g_firstPerson.outerTargetEntryValid && candidateReturned &&
				candidate.copied && candidate.snapshot.receipt == receipt &&
				Bridge::IsValidPrivateTarget(main.captureReceiptTarget)) {
				g_firstPerson.outerSurfaceAtEntry = candidate.snapshot.surface;
				g_firstPerson.outerEntryValid = true;
			}
		}
		if (MirrorPaneDelivery::Faulted())
			FailStopInternal();
	}

	void OnFirstPersonReturnedNormally() noexcept
	{
		if (!MirrorPerformance::RenderingEnabled()) return;
		if (!IsEnabled())
			return;
		if (g_firstPerson.lifecycleTransitionSuppressed ||
			HandLifecycleTransitionSuspended()) {
			auto& state = State();
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			if (!ReleaseHeldPredecessorForLifecycle(state)) {
				FailStopInternal();
				return;
			}
			g_firstPerson.activeGenericTarget.reset();
			g_firstPerson.staged = {};
			g_firstPerson.returnedNormally = true;
			return;
		}
		if (!g_firstPerson.active) {
			FailStopInternal();
			return;
		}
		if constexpr (kPresentAtExactPaneReturn) {
			auto& directState = State();
			const auto returnCount = g_firstPerson.exactNormalReturnCount;
			auto& staged = g_firstPerson.staged;
			StorePolicy::PublicationSnapshot zeroFallbackPublication{};
			// Presentation belongs to the equipped hand owner, not to the incidental
			// native pane callback count. If Skyrim omits the strict callback, admit the
			// enclosing normal-return seam only when its receipt, pane identity, frozen
			// capture target, and pre-retained live target are unchanged from entry.
			if (returnCount == 0 && !staged.valid &&
				!HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled()) {
				const auto sourceSequence =
					SecondView::CurrentMainWorldSourceSequence();
				MirrorPaneDelivery::CurrentMainDrawState main{};
				const auto mainStatus =
					QueryHandDrawState(main);
				RecordCurrentMainDrawStateStatus(
					directState.diagnostics, mainStatus);
				const bool returnStateReady = sourceSequence != 0 &&
					mainStatus ==
						MirrorPaneDelivery::CurrentMainDrawStateStatus::kReady &&
					main.valid;
				if (!returnStateReady) {
					++directState.diagnostics.zeroFallbackReturnStateRejects;
				} else if (!g_firstPerson.outerTargetEntryValid) {
					++directState.diagnostics.zeroFallbackEntryMissingRejects;
				} else {
					const ReceiptPolicy::FrameReceipt receipt{
						sourceSequence, main.mainWorldFrame, main.graphicsFrame };
					CandidateCopy candidate{};
					const bool returnReceiptValid =
						ReceiptPolicy::IsValidFrameReceipt(receipt);
					const bool candidateReturned = returnReceiptValid &&
						HandMirrorApprovedContentReadOnlyObserver::
							RunWithOuterFirstPersonRuntimeCandidate(
								receipt, main.origin, &CopyCandidate, &candidate);
					if (!candidateReturned &&
						HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
						FailStopInternal();
						return;
					}
					g_firstPerson.outerSurfaceReturnObserverExplicitlyAbsent =
						returnReceiptValid && !candidateReturned;
					if (candidateReturned && candidate.copied &&
						candidate.snapshot.receipt == receipt) {
						g_firstPerson.outerSurfaceAtReturn =
							candidate.snapshot.surface;
						g_firstPerson.outerSurfaceReturnValid =
							Bridge::IsValidMovingSurfaceSample(
								g_firstPerson.outerSurfaceAtReturn);
						MirrorPaneDelivery::RetainedMainTargetHandle outputTarget =
							g_firstPerson.outerTargetAtEntry;
						const auto outputViewport = main.viewport;
						MirrorPaneDelivery::RetainedMainTargetHandle refreshedTarget{};
						const bool entryTargetStillCurrent = outputTarget &&
							MirrorPaneDelivery::TryRefreshCurrentMainDrawTarget(
								main, outputTarget, refreshedTarget);
						if (entryTargetStillCurrent)
							outputTarget = std::move(refreshedTarget);
						const auto liveTarget =
							MakeLiveDeliveryTargetIdentity(outputTarget);
						const bool receiptStable =
							receipt == g_firstPerson.outerReceiptAtEntry;
						const bool stablePaneIdentity =
							g_firstPerson.outerEntryValid &&
							g_firstPerson.outerSurfaceReturnValid &&
							ClosePolicy::SameOwnerAndSurfaceIdentity(
								g_firstPerson.outerSurfaceAtEntry,
								candidate.snapshot.surface);
						const LiveReturn::EntryToReturnEvidence returnEvidence{
							.frozenTargetAtEntry = MakeFrozenCaptureTargetIdentity(
								g_firstPerson.outerCaptureTargetAtEntry),
							.frozenTargetAtReturn = MakeFrozenCaptureTargetIdentity(
								main.captureReceiptTarget),
							.liveTargetAtEntry =
								g_firstPerson.outerLiveTargetAtEntry,
							.liveTargetAtReturn = liveTarget,
							.normalReturnProven = true,
							.receiptIdentityStable = receiptStable,
							.stablePaneIdentityStable = stablePaneIdentity,
							.returnedPresentationComplete =
								ReturnedPresentationComplete(main)
						};
						const auto liveReturnStatus =
							LiveReturn::EvaluateEntryToReturn(returnEvidence);
						if (!entryTargetStillCurrent) {
							++directState.diagnostics.zeroFallbackRevalidateRejects;
						} else if (liveReturnStatus !=
							LiveReturn::EntryToReturnStatus::kAccepted) {
							++directState.diagnostics.zeroFallbackLiveReturnRejects;
						} else {
							auto deliveryMain = main;
							deliveryMain.device = outputTarget->device;
							deliveryMain.context = outputTarget->context;
							deliveryMain.colorRTV = outputTarget->colorRTV;
							deliveryMain.motionRTV = nullptr;
							deliveryMain.depthDSV = outputTarget->depthDSV;
							deliveryMain.viewport = outputViewport;
							deliveryMain.valid = true;
							const auto publication =
								ZeroCallbackDeliveryPublicationForSurface(
									directState, candidate.snapshot.surface,
									deliveryMain.captureReceiptTarget);
							if (publication.valid &&
								ReturnedPresentationComplete(deliveryMain)) {
								staged = {
									.returnedSurface = candidate.snapshot.surface,
									.main = deliveryMain,
									.receipt = receipt,
									.deviceIdentity = liveTarget.deviceIdentity,
									.contextIdentity = liveTarget.contextIdentity,
									.colorResourceIdentity =
										liveTarget.colorResourceIdentity,
									.depthResourceIdentity =
										liveTarget.depthResourceIdentity,
									.retainedMainTarget = std::move(outputTarget),
									.graphIndicatesRaisedPresentation = candidate.snapshot.
										graphIndicatesRaisedPresentation,
									.valid = true
								};
								zeroFallbackPublication = publication;
								ObserveReturnedExactPane(
									directState, staged.returnedSurface,
									staged.graphIndicatesRaisedPresentation);
								g_firstPerson.exactVisibilityObserved = true;
								++directState.diagnostics.visibilityAdvances;
								++directState.diagnostics.firstStages;
								++directState.diagnostics.zeroFallbackStages;
							} else {
								++directState.diagnostics.
									zeroFallbackPublicationRejects;
							}
						}
					} else {
						++directState.diagnostics.zeroFallbackCandidateRejects;
					}
				}
				if (MirrorPaneDelivery::Faulted()) {
					FailStopInternal();
					return;
				}
			}
			if (returnCount == 0 && !staged.valid && g_firstPerson.framePresentation.valid) {
				const auto& frame = g_firstPerson.framePresentation;
				MirrorPaneDelivery::CurrentMainDrawState current{};
				// As with an exact pane return, the native frame's actual OM target
				// owns this presentation. The outer main target can be a different
				// color buffer; equating them rejected every V178 zero-return replay.
				// Keep the frame's camera, pose, viewport and retained target together.
				const bool coherent = frame.retainedMainTarget &&
					frame.receipt == g_firstPerson.outerReceiptAtEntry &&
					TryGetHandDrawState(current) &&
					SecondView::CurrentMainWorldSourceSequence() == frame.receipt.sourceSequence &&
					current.mainWorldFrame == frame.receipt.mainViewFrame &&
					current.graphicsFrame == frame.receipt.graphicsFrame &&
					current.captureReceiptTarget == frame.main.captureReceiptTarget &&
					current.captureReceiptTarget == g_firstPerson.outerCaptureTargetAtEntry &&
					current.device == frame.retainedMainTarget->device &&
					current.context == frame.retainedMainTarget->context &&
					ReturnedPresentationComplete(frame.main);
				if (MirrorPaneDelivery::Faulted()) {
					FailStopInternal();
					return;
				}
				if (coherent) {
					zeroFallbackPublication = ZeroCallbackDeliveryPublicationForSurface(
						directState, frame.returnedSurface, frame.main.captureReceiptTarget);
					staged = frame;
					++directState.diagnostics.zeroFallbackStages;
					++directState.diagnostics.currentPresentationPoseReplaySamples;
				} else {
					++directState.diagnostics.currentPresentationPoseReplayRejects;
				}
			}
			const auto deliveryPublication = staged.valid ?
				((returnCount == 0 && HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled()) ||
					zeroFallbackPublication.valid ? zeroFallbackPublication :
					DeliveryPublicationForSurface(
						directState, staged.returnedSurface,
						staged.main.captureReceiptTarget)) :
				StorePolicy::PublicationSnapshot{};
			bool outerFinalDrewCurrent = false;
			bool outerFinalDrewContinuity = false;
			bool outerFinalDrewOrderingPredecessor = false;
			bool outerFinalDrawSucceeded = false;
			bool outerFinalDrawAttempted = false;
			bool lastGoodReplaySucceeded = false;
			bool lastGoodReplayValidatedHeld = false;
			DirectPaneDeliveryExecutionState outerFinalExecution{};
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
			HandMirrorLateTargetSurvivalProbe::TargetSample
				lateProbeOuterReturnSample{};
			bool lateProbeOuterPresentationSucceeded{ false };
#endif
			if (staged.valid && staged.retainedMainTarget &&
				deliveryPublication.valid) {
				// The generic pane return is still useful for identifying and retaining
				// the exact native target, but Skyrim submits its opaque pane after that
				// callback. Present once more only after the enclosing first-person call
				// has returned, so this draw is the final writer to the pane.
				const auto publication = deliveryPublication;
				const bool deliveryPublicationIsCurrent =
					directState.currentPublication.valid &&
					StorePolicy::SamePublicationSnapshot(
						publication, directState.currentPublication) &&
					PublicationMatchesVisibleSurface(
						publication, staged.returnedSurface);
				auto retainedTarget = staged.retainedMainTarget;
				bool retainedMotionCurrent = false;
				MirrorPaneDelivery::CurrentMainDrawState currentMain{};
				const auto currentMainStatus =
					QueryHandDrawState(currentMain);
				RecordCurrentMainDrawStateStatus(
					directState.diagnostics, currentMainStatus);
				const auto currentSourceSequence =
					SecondView::CurrentMainWorldSourceSequence();
				const bool receiptAndRuntimeCurrent =
					currentMainStatus == MirrorPaneDelivery::
						CurrentMainDrawStateStatus::kReady &&
					currentMain.valid &&
					currentSourceSequence == staged.receipt.sourceSequence &&
					currentMain.mainWorldFrame == staged.receipt.mainViewFrame &&
					currentMain.graphicsFrame == staged.receipt.graphicsFrame &&
					currentMain.device == retainedTarget->device &&
					currentMain.context == retainedTarget->context;
				if (receiptAndRuntimeCurrent) {
					MirrorPaneDelivery::RetainedMainTargetHandle refreshedTarget{};
					retainedMotionCurrent = MirrorPaneDelivery::
						TryAttachCurrentOptionalMotionTarget(
							currentMain, retainedTarget, refreshedTarget);
					if (retainedMotionCurrent)
						retainedTarget = std::move(refreshedTarget);
				}
				if (MirrorPaneDelivery::Faulted()) {
					FailStopInternal();
					return;
				}
				auto deliveryMain = staged.main;
				deliveryMain.device = retainedTarget->device;
				deliveryMain.context = retainedTarget->context;
				deliveryMain.colorRTV = retainedTarget->colorRTV;
				deliveryMain.motionRTV = nullptr;
				deliveryMain.depthDSV = retainedTarget->depthDSV;
				deliveryMain.valid = true;
				outerFinalExecution = {
					.runtime = &directState,
					.publication = publication,
					.paneSurface = staged.returnedSurface,
					.main = deliveryMain,
					.retainedOuterFinalTarget = retainedTarget,
					.receipt = staged.receipt,
					.outerFinal = true,
					.graphIndicatesRaisedPresentation =
						staged.graphIndicatesRaisedPresentation,
					.affineAuthorizedForCurrentPublication =
						deliveryPublicationIsCurrent,
					.retainedOuterFinalMotionCurrent = retainedMotionCurrent
				};
				outerFinalDrawAttempted = true;
				ExecuteDirectPaneDeliveryWithReaderFinally(outerFinalExecution);
				++directState.diagnostics.outerFreshDrawAttempts;
				++directState.diagnostics.directDrawAttempts;
				directState.diagnostics.lastDirectDrawStatus =
					outerFinalExecution.drawStatus;
				if (outerFinalExecution.drawStatus ==
					MirrorPaneRenderer::DrawStatus::kDrawn) {
					++directState.diagnostics.outerFreshDrawSuccesses;
					++directState.diagnostics.directDrawSuccesses;
					outerFinalDrawSucceeded = true;
					outerFinalDrewCurrent =
						directState.currentPublication.valid &&
						StorePolicy::SamePublicationSnapshot(
							publication, directState.currentPublication) &&
						PublicationMatchesVisibleSurface(
							publication, staged.returnedSurface);
					outerFinalDrewContinuity =
						CleanMissContinuityMatchesVisibleSurface(
							directState, staged.returnedSurface) &&
						StorePolicy::SamePublicationSnapshot(
							publication,
							directState.heldPredecessorRetirement.retired);
					outerFinalDrewOrderingPredecessor =
						directState.heldCleanMissExpirySource == 0 &&
						HeldPredecessorMatchesStaged(directState, staged) &&
						StorePolicy::SamePublicationSnapshot(
							publication,
							directState.heldPredecessorRetirement.retired);
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
					if (HandMirrorLateTargetSurvivalProbe::Ready()) {
						lateProbeOuterPresentationSucceeded = true;
						lateProbeOuterReturnSample = MakeLateTargetSurvivalSample(
							directState, retainedTarget, deliveryMain.viewport,
							staged.receipt,
							publication.publication.captureSequence);
					}
#endif
				} else if (outerFinalExecution.drawStatus ==
						MirrorPaneRenderer::DrawStatus::
							kProjectionMainViewNotVisible ||
					outerFinalExecution.drawStatus ==
						MirrorPaneRenderer::DrawStatus::
							kProjectionReflectedUncovered) {
					++directState.diagnostics.directDrawProjectionMisses;
					// Bounded detail for a projection miss: the main view the pane
					// is projected with versus the returned pane pose, in the same
					// origin-relative form the renderer uses (1.7.104: every
					// outer-final draw missed while the fence camera looked sane).
					static std::atomic<std::uint32_t> projectionMissLogBudget{ 8 };
					auto missBudget =
						projectionMissLogBudget.load(std::memory_order_acquire);
					if (missBudget != 0 &&
						(directState.diagnostics.directDrawProjectionMisses % 120) == 1 &&
						projectionMissLogBudget.compare_exchange_strong(
							missBudget, missBudget - 1, std::memory_order_acq_rel)) {
						const auto& g = staged.returnedSurface.pose.geometry;
						const auto project = [&deliveryMain](
												 const float x, const float y,
												 const float z) noexcept {
							const auto relative = DirectX::XMVectorSet(
								x - deliveryMain.origin.x, y - deliveryMain.origin.y,
								z - deliveryMain.origin.z, 1.0F);
							DirectX::XMFLOAT4 clip{};
							DirectX::XMStoreFloat4(
								&clip,
								DirectX::XMVector4Transform(
									relative,
									DirectX::XMLoadFloat4x4(&deliveryMain.viewProjection)));
							return clip;
						};
						const auto centerClip = project(g.center.x, g.center.y, g.center.z);
						const auto tangentClip = project(
							g.center.x + g.tangent.x * g.halfWidth,
							g.center.y + g.tangent.y * g.halfWidth,
							g.center.z + g.tangent.z * g.halfWidth);
						try {
							logger::info(
								"[RR][HandMirrorReflection] projection-miss detail status={} main.origin=({:.1f},{:.1f},{:.1f}) view.row4=({:.1f},{:.1f},{:.1f}) proj11/22=({:.3f},{:.3f}) viewport={}x{} pane.center=({:.1f},{:.1f},{:.1f}) half=({:.1f},{:.1f}) normal=({:.2f},{:.2f},{:.2f}) clip.center=({:.2f},{:.2f},{:.2f},{:.2f}) clip.tangent=({:.2f},{:.2f},{:.2f},{:.2f})",
								MirrorPaneRenderer::ToString(outerFinalExecution.drawStatus),
								deliveryMain.origin.x, deliveryMain.origin.y,
								deliveryMain.origin.z, deliveryMain.view._41,
								deliveryMain.view._42, deliveryMain.view._43,
								deliveryMain.projection._11, deliveryMain.projection._22,
								static_cast<int>(deliveryMain.viewport.Width),
								static_cast<int>(deliveryMain.viewport.Height),
								g.center.x, g.center.y, g.center.z, g.halfWidth,
								g.halfHeight, g.normal.x, g.normal.y, g.normal.z,
								centerClip.x, centerClip.y, centerClip.z, centerClip.w,
								tangentClip.x, tangentClip.y, tangentClip.z,
								tangentClip.w);
						} catch (...) {
						}
					}
				}
				static std::atomic<std::uint32_t> outerInterleaveBudget{ 160 };
				const bool outerInterleaveActive =
					directState.visibility.raisedPresentation &&
					(directState.diagnostics.interleaveHandSources +
						directState.diagnostics.interleaveWallSources) != 0;
				auto outerBudget = outerInterleaveBudget.load(std::memory_order_acquire);
				const bool outerInterleaveLog = outerInterleaveActive &&
					outerBudget != 0 &&
					outerInterleaveBudget.compare_exchange_strong(
						outerBudget, outerBudget - 1, std::memory_order_acq_rel);
				if (outerInterleaveLog ||
					(directState.diagnostics.directDrawAttempts % 240) == 1) {
					logger::info(
						"[RR][HandMirrorReflection] outer-final direct draw status={} source={} main={} graphics={} attempts={} successes={}",
						MirrorPaneRenderer::ToString(
							outerFinalExecution.drawStatus),
						staged.receipt.sourceSequence,
						staged.receipt.mainViewFrame,
						staged.receipt.graphicsFrame,
						directState.diagnostics.directDrawAttempts,
						directState.diagnostics.directDrawSuccesses);
				}
				if (!outerFinalExecution.bodyReturnedNormally ||
					outerFinalExecution.exceptionCaught ||
					directState.activeInternalReaders != 0 ||
					outerFinalExecution.drawStatus ==
						MirrorPaneRenderer::DrawStatus::
							kContextStateRestoreFailed) {
					FailStopInternal();
					return;
				}
			}
			// Any nonterminal fresh-path miss may fall back to the detached complete
			// image. Device/target/equipment identity is re-proved below; only a
			// restore fault is terminal and has already fail-stopped the route.
			const bool lastGoodReplayEligible = !outerFinalDrawSucceeded &&
				(!outerFinalDrawAttempted ||
				 outerFinalExecution.drawStatus != MirrorPaneRenderer::DrawStatus::
					 kContextStateRestoreFailed);
			if (!outerFinalDrawSucceeded && lastGoodReplayEligible &&
				LastGoodPresentationValid(directState)) {
				MirrorPaneDelivery::CurrentMainDrawState replayMain{};
				MirrorPaneDelivery::RetainedMainTargetHandle replayTarget{};
				const Bridge::MovingSurfaceSample* replaySurface = nullptr;
				auto replayMode = staged.valid ?
					LastGoodPolicy::ClassifyPresentationMode(staged.graphIndicatesRaisedPresentation) :
					LastGoodPolicy::PresentationMode::kUnknown;
				bool replayUsesDetachedSurface = false;
				bool replayTargetReady = false;
				bool replayTargetAuthoritative = false;
				if (staged.valid && staged.retainedMainTarget) {
					replayTarget = staged.retainedMainTarget;
					replayMain = staged.main;
					replayMain.device = replayTarget->device;
					replayMain.context = replayTarget->context;
					replayMain.colorRTV = replayTarget->colorRTV;
					replayMain.motionRTV = nullptr;
					replayMain.depthDSV = replayTarget->depthDSV;
					replayMain.valid = true;
					replaySurface = &staged.returnedSurface;
					replayTargetReady = ReturnedPresentationComplete(replayMain);
					replayTargetAuthoritative = replayTargetReady;
				} else {
					// A retained COM handle proves lifetime, not current presentation
					// ownership.  Zero-callback replay therefore uses only the target
					// retained before this exact outer call and revalidated after its
					// normal return.  It never writes the cached frame's old destination.
					MirrorPaneDelivery::CurrentMainDrawState currentMain{};
					const auto currentMainStatus =
						QueryHandDrawState(currentMain);
					RecordCurrentMainDrawStateStatus(
						directState.diagnostics, currentMainStatus);
					const ReceiptPolicy::FrameReceipt currentReceipt{
						SecondView::CurrentMainWorldSourceSequence(),
						currentMain.mainWorldFrame,
						currentMain.graphicsFrame
					};
					MirrorPaneDelivery::RetainedMainTargetHandle refreshedTarget{};
					const bool receiptStable =
						g_firstPerson.outerTargetEntryValid &&
						currentMainStatus == MirrorPaneDelivery::
							CurrentMainDrawStateStatus::kReady &&
						ReceiptPolicy::IsValidFrameReceipt(currentReceipt) &&
						currentReceipt == g_firstPerson.outerReceiptAtEntry;
					const bool captureTargetStable = receiptStable &&
						currentMain.captureReceiptTarget ==
							g_firstPerson.outerCaptureTargetAtEntry;
					const bool exactTargetStable = captureTargetStable &&
						MirrorPaneDelivery::TryRefreshCurrentMainDrawTarget(
							currentMain, g_firstPerson.outerTargetAtEntry,
							refreshedTarget) &&
						MakeLiveDeliveryTargetIdentity(refreshedTarget) ==
							g_firstPerson.outerLiveTargetAtEntry;
					bool colorStableDepthReplacement = false;
					if (!exactTargetStable && captureTargetStable) {
						// A missing pane callback can coincide with Skyrim rotating only
						// the native first-person depth attachment. Retain the complete
						// returned pair, but accept it only when the pre-call retained
						// destination proves the same device/context/color view/resource.
						// This weaker relation is last-good-only; fresh publication above
						// still requires exact entry-to-return color and depth identity.
						MirrorPaneDelivery::RetainedMainTargetHandle returnedTarget{};
						if (MirrorPaneDelivery::TryRetainCurrentMainDrawTarget(
								currentMain, returnedTarget)) {
							const auto returnedIdentity =
								MakeLiveDeliveryTargetIdentity(returnedTarget);
							colorStableDepthReplacement = LiveReturn::
								SameColorDestinationForDetachedReplay(
									g_firstPerson.outerLiveTargetAtEntry,
									returnedIdentity);
							if (colorStableDepthReplacement)
								refreshedTarget = std::move(returnedTarget);
						}
					}
					const bool targetStable =
						ReturnedPresentationComplete(currentMain) &&
						(exactTargetStable || colorStableDepthReplacement);
					if (MirrorPaneDelivery::Faulted()) {
						FailStopInternal();
						return;
					}
					const bool zeroReturnReplayTargetStable =
						returnCount == 0 && targetStable;
					bool currentPoseValid = false;
					if (zeroReturnReplayTargetStable &&
						HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled()) {
						const auto& frame = g_firstPerson.framePresentation;
						currentPoseValid = frame.valid && frame.receipt == currentReceipt &&
							frame.main.captureReceiptTarget == currentMain.captureReceiptTarget &&
							SameViewportBits(frame.main.viewport, currentMain.viewport) &&
							LiveReturn::SameColorDestinationForDetachedReplay(
								MakeLiveDeliveryTargetIdentity(frame.retainedMainTarget),
								MakeLiveDeliveryTargetIdentity(refreshedTarget));
						if (currentPoseValid)
							++directState.diagnostics.currentPresentationPoseReplaySamples;
						else
							++directState.diagnostics.currentPresentationPoseReplayRejects;
					}
					const auto replaySurfaceSelection = LastGoodPolicy::
						SelectZeroReturnReplaySurface(
							zeroReturnReplayTargetStable,
							g_firstPerson.outerSurfaceReturnValid,
							g_firstPerson.
								outerSurfaceReturnObserverExplicitlyAbsent,
							g_firstPerson.outerEntryValid,
							Bridge::IsValidMovingSurfaceSample(
								directState.lastGoodPresentation.liveSurface), currentPoseValid,
							HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled());
					switch (replaySurfaceSelection) {
					case LastGoodPolicy::ZeroReturnReplaySurface::kCurrentPresentationPose:
						replaySurface = &g_firstPerson.framePresentation.returnedSurface;
						replayMode = LastGoodPolicy::ClassifyPresentationMode(
							g_firstPerson.framePresentation.graphIndicatesRaisedPresentation);
						break;
					case LastGoodPolicy::ZeroReturnReplaySurface::kReturnSurface:
						replaySurface = &g_firstPerson.outerSurfaceAtReturn;
						break;
					case LastGoodPolicy::ZeroReturnReplaySurface::kEntrySurface:
						replaySurface = &g_firstPerson.outerSurfaceAtEntry;
						++directState.diagnostics.zeroFallbackEntrySurfaceSelections;
						break;
					case LastGoodPolicy::ZeroReturnReplaySurface::
							kDetachedLastGoodSurface:
						// Both same-call observer seams were cleanly absent. Keep the
						// complete cached pane/main-view mapping instead of painting
						// Skyrim's opaque fallback for this one proven destination.
						replayUsesDetachedSurface = true;
						++directState.diagnostics.
							zeroFallbackDetachedSurfaceSelections;
						break;
					case LastGoodPolicy::ZeroReturnReplaySurface::kNone:
					default:
						break;
					}
					if (replaySurface || replayUsesDetachedSurface) {
						replayTarget = std::move(refreshedTarget);
						replayMain = currentPoseValid ? g_firstPerson.framePresentation.main : currentMain;
						replayMain.device = replayTarget->device;
						replayMain.context = replayTarget->context;
						replayMain.colorRTV = replayTarget->colorRTV;
						replayMain.motionRTV = nullptr;
						replayMain.depthDSV = replayTarget->depthDSV;
						replayMain.valid = true;
						replayTargetReady =
							ReturnedPresentationComplete(replayMain);
						replayTargetAuthoritative = replayTargetReady;
					}
				}
				if (replayTargetReady) {
					const auto replayTargetIdentity =
						MakeLiveDeliveryTargetIdentity(replayTarget);
					MirrorPaneRenderer::DrawStatus replayStatus{};
					++directState.diagnostics.outerLastGoodReplayCalls;
					if (!TryReplayLastGoodPresentation(
							directState, replaySurface, replayMain,
							replayTargetIdentity, replayTarget,
							replayTargetAuthoritative,
							replayMode,
							replayStatus, true)) {
						FailStopInternal();
						return;
					}
					lastGoodReplaySucceeded = replayStatus ==
						MirrorPaneRenderer::DrawStatus::kDrawn;
					if (lastGoodReplaySucceeded) {
						++directState.diagnostics.outerLastGoodReplayDraws;
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
						if (HandMirrorLateTargetSurvivalProbe::Ready()) {
							lateProbeOuterPresentationSucceeded = true;
							const auto& replayReceipt = staged.valid ?
								staged.receipt : g_firstPerson.outerReceiptAtEntry;
							lateProbeOuterReturnSample = MakeLateTargetSurvivalSample(
								directState, replayTarget, replayMain.viewport,
								replayReceipt,
								directState.lastGoodPresentation.frame.captureSequence);
						}
#endif
					}
					outerFinalDrawSucceeded = lastGoodReplaySucceeded;
					lastGoodReplayValidatedHeld = lastGoodReplaySucceeded &&
						replaySurface != nullptr &&
						directState.heldCleanMissExpirySource != 0 &&
						CleanMissContinuityHoldValid(directState) &&
						LastGoodMatchesPublication(
							directState,
							directState.heldPredecessorRetirement.retired) &&
						Bridge::ArePrivateTargetsPhysicallyDisjoint(
								directState.heldPredecessorRetirement.retired
									.publication.attempt.privateTarget,
								replayMain.captureReceiptTarget);
				} else {
					++directState.diagnostics.lastGoodReplayTargetRejects;
				}
			}
			if (outerFinalDrawAttempted) {
				FinalizeOuterFinalAffineHistory(
					directState, outerFinalExecution);
			} else {
				ResetHandAffineHistory(
					directState, HandAffineHistoryResetReason::kNoFinalDraw);
			}
			if (directState.heldPredecessorRetirement !=
					StorePolicy::RetirementReceipt{}) {
				if (directState.heldCleanMissExpirySource != 0) {
					const bool exactContinuity = lastGoodReplayValidatedHeld ||
						(staged.valid &&
						 CleanMissContinuityMatchesVisibleSurface(
							 directState, staged.returnedSurface) &&
						 CleanMissContinuityTargetDisjoint(
							 directState,
							 directState.heldPredecessorRetirement.retired,
							 staged.returnedSurface,
							 staged.main.captureReceiptTarget));
					if (!exactContinuity)
						++directState.diagnostics.cleanMissContinuityIdentityRejects;
					if (outerFinalDrewContinuity ||
						lastGoodReplayValidatedHeld)
						++directState.diagnostics.cleanMissContinuityDraws;
					const bool freshReplacementPublished =
						directState.currentPublication.valid;
					if (exactContinuity && !freshReplacementPublished) {
						// This exact returned surface, not the scheduler's cached owner, is
						// authority for one more consecutive clean-miss source.
						directState.heldCleanMissValidatedSource =
							directState.heldCleanMissExpirySource;
					} else if (!ReleaseHeldPredecessor(directState)) {
						FailStopInternal();
						return;
					}
				} else {
					if (!HeldPredecessorMatchesStaged(directState, staged)) {
						FailStopInternal();
						return;
					}
					if (!TryPromoteOrderingPredecessorToCleanMiss(
							directState, staged,
							outerFinalDrewOrderingPredecessor) &&
						!ReleaseHeldPredecessor(directState)) {
						FailStopInternal();
						return;
					}
				}
			}
			if (directState.currentPublication.valid) {
				const auto publishedSource = directState.currentPublication.publication
					.attempt.surface.sourceSequence;
				const bool currentIsExactSuccessor = staged.valid &&
					IsExactSuccessor(
						staged.receipt.sourceSequence, publishedSource);
				const auto boundedReturnCount = static_cast<std::uint8_t>(
					(std::min)(
						returnCount,
						static_cast<std::uint32_t>(
							StorePolicy::kMaximumBoundedPaneNormalReturnCount)));
				const bool retainPresentedCurrent =
					CleanMissContinuityEffective() && outerFinalDrewCurrent &&
					PublicationMatchesVisibleSurface(
						directState.currentPublication, staged.returnedSurface);
				if (retainPresentedCurrent) {
					directState.lastPresentedPublicationToken =
						directState.currentPublication.publication
							.nativePublicationToken;
				} else if (!currentIsExactSuccessor &&
					RetireCurrentPublicationDarkAfterOuterReturn(
						directState, publishedSource, boundedReturnCount)) {
					++directState.diagnostics.publicationConsumes;
				} else if (!currentIsExactSuccessor &&
					directState.currentPublication.valid) {
					FailStopInternal();
					return;
				}
			}
			directState.diagnostics.lastReturnCount = returnCount;
			const bool zeroFallbackPresented =
				returnCount == 0 && outerFinalDrawSucceeded;
			if (zeroFallbackPresented) {
				++directState.diagnostics.zeroFallbackDraws;
				directState.diagnostics.lastStatus =
					CohortRuntimeStatus::kZeroFallbackPresented;
			} else if (returnCount == 0) {
				++directState.diagnostics.zeroDark;
				directState.diagnostics.lastStatus =
					CohortRuntimeStatus::kZeroDark;
			} else if (returnCount == 1) {
				++directState.diagnostics.singleAccepted;
				directState.diagnostics.lastStatus =
					CohortRuntimeStatus::kSingleAccepted;
			} else {
				++directState.diagnostics.doubleAccepted;
				directState.diagnostics.lastStatus =
					CohortRuntimeStatus::kDoubleAccepted;
			}
#ifndef MIRRORS_OF_SKYRIM_STANDALONE
			if (HandMirrorLateTargetSurvivalProbe::Ready()) {
				if (!lateProbeOuterPresentationSucceeded &&
					g_firstPerson.postResolvePresentedCaptureSequence != 0 &&
					staged.valid && staged.retainedMainTarget) {
					lateProbeOuterReturnSample = MakeLateTargetSurvivalSample(
						directState, staged.retainedMainTarget,
						staged.main.viewport, staged.receipt,
						g_firstPerson.postResolvePresentedCaptureSequence);
				}
				HandMirrorLateTargetSurvivalProbe::ObserveOuterReturnB(
					lateProbeOuterReturnSample);
			}
#endif
			// The enclosing normal-return seam has issued the sole presentation
			// opportunity for this first-person call.  Do not make a second write or
			// make visibility ambiguous at a later seam.
			g_firstPerson.returnedNormally = true;
			return;
		}
		auto& state = State();
		const auto staged = g_firstPerson.staged;
		const auto close = Cohort::CloseOuter(g_firstPerson.cohort);
		g_firstPerson.cohort = close.nextState;
		if (close.action == Cohort::CloseAction::kTerminalDark) {
			FailStopInternal();
			return;
		}
		const auto returnCount = close.nextState.normalReturnCount;
		state.diagnostics.lastReturnCount = returnCount;
		const bool cohortOpportunity = close.action == Cohort::CloseAction::
			kIssueOneDeliveryAndVisibilityOpportunity &&
			close.deliveryOpportunityCount == 1 &&
			close.visibilityUpdateOpportunityCount == 1;
		if (cohortOpportunity && !staged.valid) {
			FailStopInternal();
			return;
		}
		const bool acceptedCohort = cohortOpportunity && staged.valid;
		if (!acceptedCohort) {
			state.visibility.ambiguousThisSource = true;
			if (returnCount == 0) {
				++state.diagnostics.zeroDark;
				state.diagnostics.lastStatus = CohortRuntimeStatus::kZeroDark;
			} else if (returnCount >= Cohort::kOverflowNormalReturnCount) {
				++state.diagnostics.overflowDark;
				state.diagnostics.lastStatus = CohortRuntimeStatus::kOverflowDark;
			} else {
				++state.diagnostics.mismatchDark;
				state.diagnostics.lastStatus = CohortRuntimeStatus::kMismatchDark;
			}
			if (state.currentPublication.valid) {
				const auto surface =
					state.currentPublication.publication.attempt.surface;
				const auto currentSource =
					SecondView::CurrentMainWorldSourceSequence();
				if (currentSource == 0 || surface.sourceSequence != currentSource ||
					!RetireCurrentPublicationDarkAfterOuterReturn(
						state, currentSource, returnCount)) {
					FailStopInternal();
					return;
				}
				++state.diagnostics.publicationConsumes;
			}
			g_firstPerson.returnedNormally = true;
			return;
		}
		{
			if (HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
				FailStopInternal();
				return;
			}
			if (!HandMirrorApprovedContentReadOnlyObserver::IsEnabled()) {
				// A healthy observer can coherently disappear on lifecycle teardown,
				// but the already-staged native opportunity is still one-shot.
				const bool publicationWasCurrent = state.currentPublication.valid;
				if (publicationWasCurrent &&
					!RetireCurrentPublicationDarkAfterOuterReturn(
						state, staged.receipt.sourceSequence,
						returnCount)) {
					FailStopInternal();
					return;
				}
				state.visibility.ambiguousThisSource = true;
				++state.diagnostics.mismatchDark;
				state.diagnostics.lastStatus = CohortRuntimeStatus::kMismatchDark;
				if (publicationWasCurrent)
					++state.diagnostics.publicationConsumes;
				g_firstPerson.returnedNormally = true;
				return;
			}
			MirrorPaneDelivery::CurrentMainDrawState main{};
			if (SecondView::CurrentMainWorldSourceSequence() !=
					staged.receipt.sourceSequence ||
				!TryGetHandDrawState(main) || !main.valid) {
				FailStopInternal();
				return;
			}
			const auto stagedLiveTarget =
				MakeLiveDeliveryTargetIdentity(staged.retainedMainTarget);
			const bool exactOuterContinuity =
				main.mainWorldFrame == staged.receipt.mainViewFrame &&
				main.graphicsFrame == staged.receipt.graphicsFrame &&
				main.captureReceiptTarget ==
					staged.main.captureReceiptTarget &&
				reinterpret_cast<std::uintptr_t>(main.device) ==
					staged.deviceIdentity &&
				reinterpret_cast<std::uintptr_t>(main.context) ==
					staged.contextIdentity;
			MirrorPaneDelivery::CurrentMainDrawState deliveryMain = staged.main;
			const bool stagedTargetExact = staged.retainedMainTarget &&
				LiveReturn::IsValidLiveDeliveryTarget(stagedLiveTarget) &&
				stagedLiveTarget.deviceIdentity == staged.deviceIdentity &&
				stagedLiveTarget.contextIdentity == staged.contextIdentity &&
				stagedLiveTarget.colorResourceIdentity ==
					staged.colorResourceIdentity &&
				stagedLiveTarget.depthResourceIdentity ==
					staged.depthResourceIdentity;
			if (stagedTargetExact) {
				deliveryMain.device = staged.retainedMainTarget->device;
				deliveryMain.context = staged.retainedMainTarget->context;
				deliveryMain.colorRTV = staged.retainedMainTarget->colorRTV;
				deliveryMain.motionRTV = nullptr;
				deliveryMain.depthDSV = staged.retainedMainTarget->depthDSV;
				deliveryMain.valid = true;
			}
			const bool exactDeliveryMain = stagedTargetExact &&
				ReturnedPresentationComplete(deliveryMain);
			if (MirrorPaneDelivery::Faulted()) {
				FailStopInternal();
				return;
			}
			CandidateCopy closeCandidate{};
			const bool closeCandidateReturned =
				HandMirrorApprovedContentReadOnlyObserver::
					RunWithExactRuntimeCandidate(
						staged.receipt, staged.main.origin, nullptr,
						&CopyCandidate, &closeCandidate);
			if (!closeCandidateReturned &&
				HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
				FailStopInternal();
				return;
			}
			const auto closeEquality = closeCandidateReturned && closeCandidate.copied ?
				ClosePolicy::Compare(
					{ staged.receipt, staged.returnedSurface, true },
					{ closeCandidate.snapshot.receipt,
						closeCandidate.snapshot.surface,
						closeCandidate.snapshot.
							hiddenThirdPersonFreshForGraphicsFrame }) :
				ClosePolicy::ValueIdentityEquality{};
			const bool exactCloseCandidate = closeCandidateReturned &&
				closeCandidate.copied && closeEquality.AllExact() &&
				ClosePolicy::SameOwnerAndGeometry(
					staged.returnedSurface, closeCandidate.snapshot.surface);
			const bool exactClose = exactOuterContinuity && exactDeliveryMain &&
				exactCloseCandidate;
			if (!exactClose) {
				state.visibility.ambiguousThisSource = true;
				++state.diagnostics.mismatchDark;
				state.diagnostics.lastStatus = CohortRuntimeStatus::kMismatchDark;
			} else {
				ObserveReturnedExactPane(
					state, staged.returnedSurface,
					staged.graphIndicatesRaisedPresentation);
				++state.diagnostics.visibilityAdvances;
				state.diagnostics.lastReceipt = staged.receipt;
				if (returnCount == 1) {
					++state.diagnostics.singleAccepted;
					state.diagnostics.lastStatus =
						CohortRuntimeStatus::kSingleAccepted;
				} else {
					++state.diagnostics.doubleAccepted;
					state.diagnostics.lastStatus =
						CohortRuntimeStatus::kDoubleAccepted;
				}
			}
			if (state.currentPublication.valid) {
				if (!state.store ||
					state.currentPublication.publication.attempt.surface.sourceSequence !=
						staged.receipt.sourceSequence) {
					FailStopInternal();
					return;
				}
				const bool exactSurface = ClosePolicy::SameOwnerAndGeometry(
					staged.returnedSurface,
					state.currentPublication.publication.attempt.surface);
				if (!exactClose || !exactSurface) {
					if (!RetireCurrentPublicationDarkAfterOuterReturn(
							state, staged.receipt.sourceSequence,
							returnCount)) {
						FailStopInternal();
						return;
					}
					++state.diagnostics.publicationConsumes;
				} else {
				const auto visibleSurface =
					state.currentPublication.publication.attempt.surface;
				StorePolicy::PublicationSnapshot acquired{};
				const auto acquireStatus = state.store->TryAcquireInternalHand(
					{
						.owner = state.currentPublication.publication.attempt.owner,
						.surface = visibleSurface,
						.sourceSequence = staged.receipt.sourceSequence,
						.consumer =
							Bridge::PublicationConsumer::kInternalHandPane
					}, acquired);
				if (acquireStatus != StorePolicy::AcquireStatus::kAcquired ||
					acquired.physicalRole >= state.nativeSlots.size()) {
					// Exact-one native opportunity plus an exact live publication makes
					// every non-acquired/out-of-range result an impossible Store/native
					// divergence, never an ordinary dark visibility outcome.
					FailStopInternal();
					return;
				}
				{
					auto& slot = state.nativeSlots[acquired.physicalRole];
					if (!slot.valid ||
						!StorePolicy::SamePublicationSnapshot(
							slot.publication, acquired)) {
						FailStopInternal();
						return;
					}
					DeliveryExecutionState execution{
						.runtime = &state,
						.acquired = acquired,
						.visibleSurface = visibleSurface,
						.main = deliveryMain
					};
					ExecuteDeliveryWithReaderFinally(execution);
					const bool consumed = execution.bodyReturnedNormally &&
						!execution.exceptionCaught &&
						(execution.consumeStatus ==
							StorePolicy::ConsumeStatus::kConsumedVisible ||
							execution.consumeStatus ==
								StorePolicy::ConsumeStatus::kConsumedDarkDrawFailure);
					const bool drawOutcomeAdmissible =
						MirrorPaneRenderer::IsSuccessfulDraw(execution.drawStatus) ||
						execution.drawStatus == MirrorPaneRenderer::DrawStatus::
							kProjectionMainViewNotVisible ||
						execution.drawStatus == MirrorPaneRenderer::DrawStatus::
							kProjectionReflectedUncovered;
					if (!consumed || state.activeInternalReaders != 0 ||
						!ReleaseRetirement(state, execution.consumed.retirement)) {
						FailStopInternal();
						return;
					}
					state.currentPublication = {};
					ResetHandAffineHistory(
						state, HandAffineHistoryResetReason::kNoFinalDraw);
					++state.diagnostics.publicationConsumes;
					if (!drawOutcomeAdmissible) {
						// Renderer state has already restored and the one-shot publication
						// has retired; device/target/alias/map/projection faults are terminal.
						FailStopInternal();
						return;
					}
				}
				}
			}
		}
		g_firstPerson.returnedNormally = true;
	}

	void OnFirstPersonFinally() noexcept
	{
		if (IsEnabled() && HandLifecycleTransitionSuspended()) {
			auto& state = State();
			ResetHandAffineHistory(
				state, HandAffineHistoryResetReason::kLifecycle);
			if (!ReleaseHeldPredecessorForLifecycle(state)) {
				g_firstPerson = {};
				FailStopInternal();
				return;
			}
		}
		const bool abnormal = g_firstPerson.active &&
			!g_firstPerson.returnedNormally && IsEnabled() &&
			!HandLifecycleTransitionSuspended();
		g_firstPerson = {};
		if (abnormal)
			FailStopInternal();
	}

	GenericDrawToken OnGenericDrawEntry(RE::BSRenderPass* pass) noexcept
	{
		GenericDrawToken output{};
		if (!MirrorPerformance::RenderingEnabled()) return output;
		auto& state = State();
		++state.diagnostics.genericEntries;
		if (!IsEnabled() || HandLifecycleTransitionSuspended() ||
			!g_firstPerson.active || !pass) {
			++state.diagnostics.genericInactiveOrNullPass;
			return output;
		}
		// The world scope contains every scene draw in VR. Reject unrelated
		// names before the expensive D3D read; the exact equipment/clone/pass
		// proof below still authorizes the candidate. A name grants no authority.
		if (HandMirrorApprovedContentReadOnlyObserver::VRRuntimeFixEnabled() &&
			(!pass->geometry || !pass->geometry->name.c_str() ||
				std::strcmp(pass->geometry->name.c_str(), "TrueMirror:0") != 0)) {
			return output;
		}
		if (!EnsureRenderThread(state)) {
			++state.diagnostics.genericThreadRejects;
			FailStopInternal();
			return output;
		}
		// The VR body pane is submitted before the late native batch-query seam
		// which normally freezes the main view. Prove its current exact identity
		// first, then record the same guarded raster cohort at this earlier draw.
		if (HandMirrorApprovedContentReadOnlyObserver::VRPresentationFixEnabled()) {
			if (!MirrorPaneDelivery::ObserveCurrentVRHandMainView(pass)) {
				if (MirrorPaneDelivery::Faulted() ||
					HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped())
					FailStopInternal();
				return output;
			}
		}
		const auto sourceSequence = SecondView::CurrentMainWorldSourceSequence();
		MirrorPaneDelivery::CurrentMainDrawState main{};
		if (sourceSequence == 0) {
			++state.diagnostics.genericSourceZero;
			return output;
		}
		const auto mainStatus =
			QueryHandDrawState(main);
		RecordCurrentMainDrawStateStatus(state.diagnostics, mainStatus);
		if (mainStatus !=
				MirrorPaneDelivery::CurrentMainDrawStateStatus::kReady ||
			!main.valid) {
			// Healthy absence can occur on an unrelated generic pass. A guarded
			// target/query fault is process-terminal and must not leave Store or the
			// coordinator available for a later pane call.
			if (MirrorPaneDelivery::Faulted())
				FailStopInternal();
			return output;
		}
		const ReceiptPolicy::FrameReceipt receipt{
			sourceSequence, main.mainWorldFrame, main.graphicsFrame };
		if (!ReceiptPolicy::IsValidFrameReceipt(receipt) ||
			!Bridge::IsValidPrivateTarget(main.captureReceiptTarget)) {
			++state.diagnostics.genericInvalidReceiptOrTarget;
			return output;
		}
		HandMirrorApprovedContentReadOnlyObserver::RuntimePresentationPose framePose{};
		if (g_firstPerson.exactNormalReturnCount == 0 &&
			HandMirrorApprovedContentReadOnlyObserver::TryReadFramePresentationPose(
				pass, receipt, main.origin, framePose)) {
			g_firstPerson.framePresentation = {};
			return {
				.entrySurface = framePose.surface,
				.entryView = main.view,
				.entryProjection = main.projection,
				.entryViewProjection = main.viewProjection,
				.entryOrigin = main.origin,
				.entryViewport = main.viewport,
				.receipt = receipt,
				.captureReceiptTarget = main.captureReceiptTarget,
				.passIdentity = reinterpret_cast<std::uintptr_t>(pass),
				.deviceIdentity = reinterpret_cast<std::uintptr_t>(main.device),
				.contextIdentity = reinterpret_cast<std::uintptr_t>(main.context),
				.framePresentationActive = true,
				.framePresentationRaised = framePose.graphIndicatesRaisedPresentation
			};
		}
		if (HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
			FailStopInternal();
			return output;
		}
		CandidateCopy candidate{};
		++state.diagnostics.genericRunWithReached;
		const bool candidateReturned =
			HandMirrorApprovedContentReadOnlyObserver::RunWithExactRuntimeCandidate(
				receipt, main.origin, pass,
				&CopyCandidate, &candidate);
		if (!candidateReturned &&
			HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
			FailStopInternal();
			return output;
		}
		if (!candidateReturned ||
			!candidate.copied || !candidate.snapshot.exactPassIsFirstPersonPane ||
			candidate.snapshot.receipt != receipt) {
			return output;
		}
		MirrorPaneDelivery::RetainedMainTargetHandle retainedMainTarget{};
		if (!MirrorPaneDelivery::TryRetainCurrentMainDrawTarget(
				main, retainedMainTarget) || !retainedMainTarget) {
			FailStopInternal();
			return output;
		}
		const auto colorResource = reinterpret_cast<std::uintptr_t>(
			retainedMainTarget->colorResource);
		const auto depthResource = reinterpret_cast<std::uintptr_t>(
			retainedMainTarget->depthResource);
		const auto deviceIdentity = reinterpret_cast<std::uintptr_t>(
			retainedMainTarget->device);
		const auto contextIdentity = reinterpret_cast<std::uintptr_t>(
			retainedMainTarget->context);
		if (deviceIdentity == 0 || contextIdentity == 0 || colorResource == 0 ||
			depthResource == 0 || colorResource == depthResource) {
			FailStopInternal();
			return output;
		}
		if (g_firstPerson.activeGenericTarget) {
			FailStopInternal();
			return output;
		}
		g_firstPerson.activeGenericTarget = std::move(retainedMainTarget);
		return {
			.entrySurface = candidate.snapshot.surface,
			.entryView = main.view,
			.entryProjection = main.projection,
			.entryViewProjection = main.viewProjection,
			.entryOrigin = main.origin,
			.entryViewport = main.viewport,
			.receipt = receipt,
			.captureReceiptTarget = main.captureReceiptTarget,
			.passIdentity = reinterpret_cast<std::uintptr_t>(pass),
			.deviceIdentity = deviceIdentity,
			.contextIdentity = contextIdentity,
			.colorResourceIdentity = colorResource,
			.depthResourceIdentity = depthResource,
			.active = true
		};
	}

	// The native frame and pane are rigid siblings. A frame draw supplies the
	// exact first-person raster even when the opaque pane's own callback is absent.
	// Retain it only across this outer call, after normal native return and an
	// unchanged pose/camera receipt. It never authorizes a private capture.
	static void ObserveFramePresentationReturn(
		GenericDrawToken& token, RE::BSRenderPass* pass) noexcept
	{
		if (!token.framePresentationActive)
			return;
		token.framePresentationActive = false;
		++State().diagnostics.framePresentationReturns;
		g_firstPerson.framePresentation = {};
		if (!IsEnabled() || !g_firstPerson.active || HandLifecycleTransitionSuspended() ||
			!pass || token.passIdentity != reinterpret_cast<std::uintptr_t>(pass))
			return;
		MirrorPaneDelivery::CurrentMainDrawState main{};
		HandMirrorApprovedContentReadOnlyObserver::RuntimePresentationPose pose{};
		const bool rasterStable = TryGetHandDrawState(main) &&
			main.valid && SecondView::CurrentMainWorldSourceSequence() == token.receipt.sourceSequence &&
			main.mainWorldFrame == token.receipt.mainViewFrame &&
			main.graphicsFrame == token.receipt.graphicsFrame &&
			main.captureReceiptTarget == token.captureReceiptTarget &&
			reinterpret_cast<std::uintptr_t>(main.device) == token.deviceIdentity &&
			reinterpret_cast<std::uintptr_t>(main.context) == token.contextIdentity &&
			SameMatrixBits(main.view, token.entryView) &&
			SameMatrixBits(main.projection, token.entryProjection) &&
			SameMatrixBits(main.viewProjection, token.entryViewProjection) &&
			Bridge::SameFloat3Bits({ main.origin.x, main.origin.y, main.origin.z },
				{ token.entryOrigin.x, token.entryOrigin.y, token.entryOrigin.z }) &&
			SameViewportBits(main.viewport, token.entryViewport);
		const bool poseStable = rasterStable &&
			HandMirrorApprovedContentReadOnlyObserver::TryReadFramePresentationPose(
				pass, token.receipt, main.origin, pose) && pose.receipt == token.receipt &&
			ClosePolicy::SameOwnerAndSurfaceIdentity(token.entrySurface, pose.surface) &&
			Bridge::SameGeometryBits(token.entrySurface.pose.geometry, pose.surface.pose.geometry) &&
			pose.graphIndicatesRaisedPresentation == token.framePresentationRaised;
		MirrorPaneDelivery::RetainedMainTargetHandle target{};
		D3D11_VIEWPORT viewport{};
		const bool targetRetained = poseStable &&
			MirrorPaneDelivery::TryRetainCurrentOutputMergerTarget(main, target, viewport) &&
			target && SameViewportBits(viewport, token.entryViewport);
		if (MirrorPaneDelivery::Faulted() ||
			HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
			FailStopInternal();
			return;
		}
		if (!targetRetained)
			return;
		++State().diagnostics.framePresentationSamples;
		main.colorRTV = target->colorRTV;
		main.motionRTV = nullptr;
		main.depthDSV = target->depthDSV;
		main.viewport = viewport;
		const auto identity = MakeLiveDeliveryTargetIdentity(target);
		g_firstPerson.framePresentation = {
			.returnedSurface = pose.surface,
			.main = main,
			.receipt = token.receipt,
			.deviceIdentity = identity.deviceIdentity,
			.contextIdentity = identity.contextIdentity,
			.colorResourceIdentity = identity.colorResourceIdentity,
			.depthResourceIdentity = identity.depthResourceIdentity,
			.retainedMainTarget = std::move(target),
			.graphIndicatesRaisedPresentation = pose.graphIndicatesRaisedPresentation,
			.valid = ReturnedPresentationComplete(main)
		};
	}

	void OnGenericDrawReturnedNormally(
		GenericDrawToken& token,
		RE::BSRenderPass* pass) noexcept
	{
		ObserveFramePresentationReturn(token, pass);
		if (!token.active)
			return;
		if (HandLifecycleTransitionSuspended()) {
			token.active = false;
			g_firstPerson.activeGenericTarget.reset();
			return;
		}
		auto& state = State();
		MirrorPaneDelivery::CurrentMainDrawState main{};
		if (!IsEnabled() || !g_firstPerson.active || !pass ||
			token.passIdentity != reinterpret_cast<std::uintptr_t>(pass) ||
			SecondView::CurrentMainWorldSourceSequence() !=
				token.receipt.sourceSequence ||
			!TryGetHandDrawState(main) || !main.valid) {
			// An active entry token proves the native call began.  Losing its pass,
			// source, TLS, or guarded main-state query is a terminal invariant fault.
			token.active = false;
			FailStopInternal();
			return;
		}
		if (!g_firstPerson.activeGenericTarget) {
			token.active = false;
			FailStopInternal();
			return;
		}
		CandidateCopy current{};
		const bool mainViewFrameExact =
			main.mainWorldFrame == token.receipt.mainViewFrame;
		const bool graphicsFrameExact =
			main.graphicsFrame == token.receipt.graphicsFrame;
		const bool currentTargetExact =
			main.captureReceiptTarget == token.captureReceiptTarget;
		const bool deviceExact =
			reinterpret_cast<std::uintptr_t>(main.device) == token.deviceIdentity;
		const bool contextExact =
			reinterpret_cast<std::uintptr_t>(main.context) == token.contextIdentity;
		// Frozen capture receipt and live first-person delivery are separate target
		// domains.  Retain the returned live target only after the immutable prefix
		// is exact, then compare the two independently retained COM identity values.
		const bool resourcesChecked = mainViewFrameExact && graphicsFrameExact &&
			currentTargetExact && deviceExact && contextExact;
		MirrorPaneDelivery::RetainedMainTargetHandle returnedMainTarget{};
		const bool returnedTargetRetained = resourcesChecked &&
			MirrorPaneDelivery::TryRetainCurrentMainDrawTarget(
				main, returnedMainTarget) && returnedMainTarget;
		MirrorPaneDelivery::RetainedMainTargetHandle outputMergerTarget{};
		D3D11_VIEWPORT outputMergerViewport{};
		const bool outputMergerTargetRetained = returnedTargetRetained &&
			MirrorPaneDelivery::TryRetainCurrentOutputMergerTarget(
				main, outputMergerTarget, outputMergerViewport) &&
			outputMergerTarget;
		const auto entryLiveTarget = MakeLiveDeliveryTargetIdentity(
			g_firstPerson.activeGenericTarget);
		const auto returnedLiveTarget = MakeLiveDeliveryTargetIdentity(
			returnedMainTarget);
		const auto outputMergerLiveTarget = MakeLiveDeliveryTargetIdentity(
			outputMergerTarget);
		auto outputMergerMain = main;
		if (outputMergerTargetRetained) {
			outputMergerMain.device = outputMergerTarget->device;
			outputMergerMain.context = outputMergerTarget->context;
			outputMergerMain.colorRTV = outputMergerTarget->colorRTV;
			outputMergerMain.motionRTV = nullptr;
			outputMergerMain.depthDSV = outputMergerTarget->depthDSV;
			outputMergerMain.viewport = outputMergerViewport;
			outputMergerMain.valid = true;
		}
		const bool resourcesExact = returnedTargetRetained &&
			LiveReturn::IsValidLiveDeliveryTarget(entryLiveTarget) &&
			entryLiveTarget == returnedLiveTarget;
		const bool viewExact = SameMatrixBits(main.view, token.entryView);
		const bool projectionExact =
			SameMatrixBits(main.projection, token.entryProjection);
		const bool viewProjectionExact =
			SameMatrixBits(main.viewProjection, token.entryViewProjection);
		const bool originExact = Bridge::SameFloat3Bits(
			{ main.origin.x, main.origin.y, main.origin.z },
			{ token.entryOrigin.x, token.entryOrigin.y, token.entryOrigin.z });
		const bool viewportExact =
			SameViewportBits(main.viewport, token.entryViewport);
		if (MirrorPaneDelivery::Faulted()) {
			token.active = false;
			g_firstPerson.activeGenericTarget.reset();
			FailStopInternal();
			return;
		}
		const bool candidateReturned =
			HandMirrorApprovedContentReadOnlyObserver::RunWithExactRuntimeCandidate(
				token.receipt, main.origin, pass,
				&CopyCandidate, &current);
		if (!candidateReturned &&
			HandMirrorApprovedContentReadOnlyObserver::IsFaultStopped()) {
			token.active = false;
			g_firstPerson.activeGenericTarget.reset();
			FailStopInternal();
			return;
		}
		const bool candidateAvailable = candidateReturned && current.copied;
		const bool exactPass = candidateAvailable &&
			current.snapshot.exactPassIsFirstPersonPane;
		const bool receiptExact = candidateAvailable &&
			current.snapshot.receipt == token.receipt;
		const auto& entrySurface = token.entrySurface;
		const auto& returnedSurface = current.snapshot.surface;
		const bool surfacesValid = candidateAvailable &&
			Bridge::IsValidMovingSurfaceSample(entrySurface) &&
			Bridge::IsValidMovingSurfaceSample(returnedSurface);
		const bool ownerExact =
			surfacesValid && entrySurface.owner == returnedSurface.owner;
		const bool stableGenerationExact = surfacesValid &&
			entrySurface.pose.stableGeneration ==
				returnedSurface.pose.stableGeneration;
		const bool surfaceExact = surfacesValid &&
			entrySurface.sourceSequence == returnedSurface.sourceSequence &&
			entrySurface.pose.mainViewFrame ==
				returnedSurface.pose.mainViewFrame &&
			entrySurface.pose.observedPaneSubtree ==
				returnedSurface.pose.observedPaneSubtree &&
			entrySurface.pose.authoredSurface ==
				returnedSurface.pose.authoredSurface;
		const bool geometryExact = surfacesValid && Bridge::SameGeometryBits(
			entrySurface.pose.geometry, returnedSurface.pose.geometry);
		const bool returnedPresentationComplete =
			ReturnedPresentationComplete(main);
		const LiveReturn::EntryToReturnEvidence returnEvidence{
			.frozenTargetAtEntry = MakeFrozenCaptureTargetIdentity(
				token.captureReceiptTarget),
			.frozenTargetAtReturn = MakeFrozenCaptureTargetIdentity(
				main.captureReceiptTarget),
			.liveTargetAtEntry = entryLiveTarget,
			.liveTargetAtReturn = returnedLiveTarget,
			.normalReturnProven = true,
			.receiptIdentityStable = mainViewFrameExact && graphicsFrameExact &&
				receiptExact,
			.stablePaneIdentityStable = candidateAvailable &&
				ClosePolicy::SameOwnerAndSurfaceIdentity(
					token.entrySurface, current.snapshot.surface),
			.returnedPresentationComplete = returnedPresentationComplete
		};
		// The native draw is allowed to finalize presentation matrices, viewport,
		// origin, and pane geometry.  The returned values are authoritative; only
		// immutable receipt/pane identity and retained live-resource identity
		// authorize the cohort. Each return independently validates its pass-local
		// frozen capture target. Return two adopts its latest presentation and target;
		// outer close then revalidates that complete latest bundle.
		const bool exactCurrent = exactPass && LiveReturn::Accepted(returnEvidence);
		if (g_firstPerson.cohort.normalReturnCount == 0) {
			RecordFirstReturnValidation(
				state.diagnostics,
				{
					.candidateAvailable = candidateAvailable,
					.currentTargetExact = currentTargetExact,
					.mainViewFrameExact = mainViewFrameExact,
					.graphicsFrameExact = graphicsFrameExact,
					.receiptExact = receiptExact,
					.exactPass = exactPass,
					.deviceExact = deviceExact,
					.contextExact = contextExact,
					.resourcesChecked = resourcesChecked,
					.resourcesExact = resourcesExact,
					.ownerExact = ownerExact,
					.surfaceExact = surfaceExact,
					.viewExact = viewExact,
					.projectionExact = projectionExact,
					.viewProjectionExact = viewProjectionExact,
					.originExact = originExact,
					.viewportExact = viewportExact,
					.stableGenerationExact = stableGenerationExact,
					.geometryExact = geometryExact,
					.returnedPresentationComplete =
						returnedPresentationComplete
				},
				exactCurrent);
		}
		if constexpr (kPresentAtExactPaneReturn) {
			if (exactCurrent) {
				const bool exactOwningPostResolvePhase =
					g_postResolveInvocation.active &&
					g_postResolveInvocation.nativeDepth == 1 &&
					!SecondView::IsInsidePrivateCapture() &&
					!HandLifecycleTransitionSuspended() &&
					g_postResolveInvocation.token != 0 &&
					g_postResolveInvocation.entryReceipt == token.receipt &&
					g_postResolveInvocation.entryCaptureTarget ==
						token.captureReceiptTarget &&
					g_postResolveInvocation.deviceIdentity == token.deviceIdentity &&
					g_postResolveInvocation.contextIdentity == token.contextIdentity;
				if (g_postResolveInvocation.active &&
					!exactOwningPostResolvePhase) {
					// A private/nested or foreign-frame exact callback is chain-only for
					// our runtime: it cannot advance visibility/count, issue an immediate
					// draw, overwrite staged delivery, or stamp the restored outer token.
					g_postResolveInvocation.foreignExactReturnObserved = true;
					token.active = false;
					g_firstPerson.activeGenericTarget.reset();
					return;
				}
				if (g_firstPerson.exactNormalReturnCount !=
					(std::numeric_limits<std::uint32_t>::max)()) {
					++g_firstPerson.exactNormalReturnCount;
				}
				if (!g_firstPerson.exactVisibilityObserved) {
					if (state.visibility.lastVisibilityUpdateSourceSequence !=
						returnedSurface.sourceSequence) {
						ObserveReturnedExactPane(
							state, returnedSurface,
							current.snapshot.graphIndicatesRaisedPresentation);
						++state.diagnostics.visibilityAdvances;
					}
					g_firstPerson.exactVisibilityObserved = true;
					++state.diagnostics.firstStages;
				} else {
					++state.diagnostics.latestRestages;
				}

				// Submit once at the immediate pass return, then retain the exact target
				// for the owning primary-wrapper post-native writer. Both routes share the same
				// fresh-or-detached-last-good implementation.
				RetainedTargetPresentationResult immediateResult{};
				if (outputMergerTargetRetained &&
					!PresentFreshOrLastGoodToRetainedTarget(
						state, returnedSurface, token.receipt,
						token.captureReceiptTarget, outputMergerMain,
						outputMergerTarget, outputMergerLiveTarget,
						current.snapshot.graphIndicatesRaisedPresentation,
						RetainedTargetPresentationRoute::kExactPaneReturn,
						immediateResult)) {
					token.active = false;
					g_firstPerson.activeGenericTarget.reset();
					FailStopInternal();
					return;
				}

				if (outputMergerTargetRetained) {
					// Keep the latest exact pane target alive until the enclosing
					// first-person native call has completed. The second legal pane
					// return replaces and releases the first retained target.
					g_firstPerson.staged = {
						.returnedSurface = returnedSurface,
						.main = outputMergerMain,
						.receipt = token.receipt,
						.deviceIdentity = outputMergerLiveTarget.deviceIdentity,
						.contextIdentity = outputMergerLiveTarget.contextIdentity,
						.colorResourceIdentity =
							outputMergerLiveTarget.colorResourceIdentity,
						.depthResourceIdentity =
							outputMergerLiveTarget.depthResourceIdentity,
						.retainedMainTarget = std::move(outputMergerTarget),
						.graphIndicatesRaisedPresentation = current.snapshot.
							graphIndicatesRaisedPresentation,
						.valid = true
					};
					if (exactOwningPostResolvePhase) {
						g_firstPerson.postResolveStageToken =
							g_postResolveInvocation.token;
						g_firstPerson.postResolveExpectedReceipt = token.receipt;
						g_firstPerson.postResolveExpectedCaptureTarget =
							token.captureReceiptTarget;
						g_firstPerson.postResolveExpectedDeviceIdentity =
							token.deviceIdentity;
						g_firstPerson.postResolveExpectedContextIdentity =
							token.contextIdentity;
						g_firstPerson.postResolveExpectedReturnCount =
							g_firstPerson.exactNormalReturnCount;
						g_firstPerson.postResolveExpectedInvocationValid = true;
					}
				}
			}
			token.active = false;
			g_firstPerson.activeGenericTarget.reset();
			return;
		}
		Cohort::ValueIdentityEquality equality{};
		if (exactCurrent && g_firstPerson.cohort.normalReturnCount != 0) {
			equality = ReturnedPaneCohortEquality(
				g_firstPerson.staged, token, current.snapshot, main,
				g_firstPerson.activeGenericTarget);
		}
		if (g_firstPerson.cohort.normalReturnCount == 1) {
			RecordSecondReturnValidation(
				state.diagnostics, equality,
				exactCurrent &&
					Cohort::IsSameImmutableCohortIdentity(equality));
		}
		if (MirrorPaneDelivery::Faulted()) {
			token.active = false;
			g_firstPerson.activeGenericTarget.reset();
			FailStopInternal();
			return;
		}
		const auto transition = Cohort::ObserveReturn(
			g_firstPerson.cohort,
			{
				.normalReturnProven = true,
				.valueSnapshotComplete = exactCurrent,
				.equalityToStagedFirst = equality,
				.divergenceClass = Cohort::DivergenceClass::kCoherentDark
			});
		static_assert(!Cohort::AllowsReturnTimeVisibilityUpdateOrDraw({}));
		g_firstPerson.cohort = transition.nextState;
		if (transition.action == Cohort::ReturnAction::kTerminalDark) {
			token.active = false;
			g_firstPerson.activeGenericTarget.reset();
			FailStopInternal();
			return;
		}
		if (transition.action == Cohort::ReturnAction::kStageFirst ||
			transition.action == Cohort::ReturnAction::
				kReplaceWithLatestCoalescedWithoutVisibilityUpdateOrDraw) {
			// One or two exact pane submissions are legal. The latest complete
			// normal-return bundle owns pass-local pose/presentation; replacing the
			// retained handle releases the earlier bundle before outer close.
			g_firstPerson.staged = {
				.returnedSurface = current.snapshot.surface,
				.main = main,
				.receipt = token.receipt,
				.deviceIdentity = returnedLiveTarget.deviceIdentity,
				.contextIdentity = returnedLiveTarget.contextIdentity,
				.colorResourceIdentity =
					returnedLiveTarget.colorResourceIdentity,
				.depthResourceIdentity =
					returnedLiveTarget.depthResourceIdentity,
				.retainedMainTarget = std::move(returnedMainTarget),
				.graphIndicatesRaisedPresentation = current.snapshot.
					graphIndicatesRaisedPresentation,
				.valid = true
			};
			if (transition.action == Cohort::ReturnAction::kStageFirst)
				++state.diagnostics.firstStages;
			else
				++state.diagnostics.latestRestages;
		} else if (transition.action == Cohort::ReturnAction::kCoherentDark) {
			g_firstPerson.staged = {};
		}
		token.active = false;
		g_firstPerson.activeGenericTarget.reset();
	}

	void OnGenericDrawFinally(GenericDrawToken& token) noexcept
	{
		const bool abnormal = (token.active || token.framePresentationActive) &&
			!HandLifecycleTransitionSuspended();
		token = {};
		g_firstPerson.activeGenericTarget.reset();
		if (abnormal)
			FailStopInternal();
	}

	MirrorPaneRenderer::DrawStatus DrawHandPaneBacking(
		ID3D11DeviceContext* context, const NestedHandPaneGeometry& pane,
		const MirrorPaneRenderer::MainView& view,
		const MirrorPaneRenderer::DrawTargets& targets) noexcept
	{
		auto& state = State();
		if (!context || !IsEnabled() ||
			(state.renderThreadID != 0 && GetCurrentThreadId() != state.renderThreadID))
			return MirrorPaneRenderer::DrawStatus::kInvalidArgument;
		EquippedPresentationIdentityCopy equipped{};
		if (HandMirrorApprovedContentReadOnlyObserver::RunWithEquippedPresentationIdentity(
				&CopyEquippedPresentationIdentity, &equipped) !=
				HandMirrorApprovedContentReadOnlyObserver::RuntimeEquippedPresentationStatus::kExactEquipped ||
			!equipped.copied || !equipped.identity.valid)
			return MirrorPaneRenderer::DrawStatus::kCandidateMismatch;
		Microsoft::WRL::ComPtr<ID3D11Device> device{};
		context->GetDevice(device.GetAddressOf());
		MirrorPaneRenderer::InitializationStatus initialization{};
		if (device) EnsureHandRenderer(state, device.Get(), initialization);
		const MirrorPaneRenderer::DrawRequest request{
			.pane = { pane.center, pane.tangentExtent, pane.bitangentExtent,
				Bridge::MakeHandOwner(equipped.identity.owner) },
			.mainView = view,
			.targets = { targets.colorRTV, nullptr, targets.depthDSV, targets.viewport },
			.worldSpaceDepth = true,
			.opaqueBacking = true
		};
		return state.renderer.Draw(context, request, nullptr);
	}

	MirrorPaneRenderer::DrawStatus DrawLastGoodHandFrameOnPane(
		ID3D11DeviceContext* context,
		const NestedHandPaneGeometry& pane,
		const MirrorPaneRenderer::MainView& view,
		const MirrorPaneRenderer::DrawTargets& targets) noexcept
	{
		auto& state = State();
		if (!context || !IsEnabled() ||
			(state.renderThreadID != 0 && GetCurrentThreadId() != state.renderThreadID)) {
			return MirrorPaneRenderer::DrawStatus::kInvalidArgument;
		}
		// Run 22 (owner): every nested draw reported no published frame although
		// last-good commits were flowing. The full predicate also requires the
		// live first-person delivery target identity, which is irrelevant to a
		// draw into a private capture; only the retained replay copy matters here.
		const auto& cached = state.lastGoodPresentation;
		// Run 23: while the player stood still the presentation was a detached
		// snapshot whose frame SRV is not the replay copy, so the identity test
		// refused every draw. Any valid committed frame is usable here; prefer the
		// replay copy when it exists.
		if (!cached.valid || !cached.frame.valid || !cached.frame.colorSRV ||
			cached.frame.owner.kind != Bridge::OwnerKind::kHand ||
			!Bridge::IsValidMovingSurfaceSample(cached.liveSurface)) {
			return MirrorPaneRenderer::DrawStatus::kNoPublishedFrame;
		}
		// Run 24 (owner: "warped colors", "sometimes see through"): all four live
		// corners collapsed to one point just off the left edge of the captured
		// image (w around 37,779), so the pane sampled a single texel. The
		// retarget was given the publication's *capture attempt* surface, but the
		// committed frame's reflected projection is the portrait sampling
		// projection, which is expressed against the pane the draw was actually
		// made on. Pairing it with any other pane is a wrong mapping, and the two
		// poses differ whenever the capture attempt and the committed draw are
		// not the same frame. The proven main-view replay path
		// (LastGoodReplay above) uses cached.liveSurface for exactly this reason;
		// use the same basis here so the third-person pane shows what the
		// first-person pane shows, retargeted to its own world pose.
		const auto capturedPane = MakePaneTransform(
			cached.liveSurface, cached.frame.owner);
		const MirrorPaneRenderer::PaneTransform livePane{
			.center = pane.center,
			.tangentExtent = pane.tangentExtent,
			.bitangentExtent = pane.bitangentExtent,
			.owner = cached.frame.owner
		};
		MirrorPaneRenderer::PublishedFrame retargeted{};
		if (!Presentation::RetargetReflectedProjectionToLivePane(
				cached.frame, capturedPane, livePane, retargeted)) {
			return MirrorPaneRenderer::DrawStatus::kProjectionRejected;
		}
		// Affine pane retargeting has no capture-world depth; never let a nested
		// draw emit reflected motion from it.
		retargeted.depthSRV.Reset();
		if (state.lastGoodReplayTexture && state.lastGoodReplaySRV)
			retargeted.colorSRV = state.lastGoodReplaySRV;
		if (!state.renderer.Ready()) {
			Microsoft::WRL::ComPtr<ID3D11Device> device{};
			context->GetDevice(device.GetAddressOf());
			MirrorPaneRenderer::InitializationStatus initialization{};
			if (device)
				EnsureHandRenderer(state, device.Get(), initialization);
			if (!state.renderer.Ready())
				return MirrorPaneRenderer::DrawStatus::kNotInitialized;
		}
		const MirrorPaneRenderer::DrawRequest request{
			.frame = std::move(retargeted),
			.pane = livePane,
			.mainView = view,
			.targets = { targets.colorRTV, nullptr, targets.depthDSV, targets.viewport },
			.motionHistory = {},
			.motionMode = MirrorPaneRenderer::MotionMode::kRejectHistory,
			.deliveryFrame = 0,
			// Run 24: about half the nested draws were rejected reflected-uncovered
			// (see-through pane every few seconds); the tiny nested pane may clamp.
			.clampReflectedCoverage = true,
			.worldSpaceDepth = true
		};
		// Run-24 diagnostic: where the live pane's corners land in the captured
		// image (retargeted clip x/w, y/w, w), bounded.
		static std::atomic<int> cornerBudget{ 4 };
		if (cornerBudget.fetch_sub(1, std::memory_order_relaxed) > 0) {
			const auto vp = DirectX::XMLoadFloat4x4(&request.frame.reflectedViewProjection);
			std::string corners;
			for (const float sx : { -1.0F, 1.0F }) for (const float sy : { -1.0F, 1.0F }) {
				const DirectX::XMFLOAT3 corner{
					livePane.center.x + sx * livePane.tangentExtent.x + sy * livePane.bitangentExtent.x - request.frame.reflectedOrigin.x,
					livePane.center.y + sx * livePane.tangentExtent.y + sy * livePane.bitangentExtent.y - request.frame.reflectedOrigin.y,
					livePane.center.z + sx * livePane.tangentExtent.z + sy * livePane.bitangentExtent.z - request.frame.reflectedOrigin.z };
				DirectX::XMFLOAT4 clip{};
				DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
					DirectX::XMVectorSet(corner.x, corner.y, corner.z, 1.0F), vp));
				const float w = std::abs(clip.w) > 1.0e-6F ? clip.w : 1.0e-6F;
				corners += std::format(" ({:.2f},{:.2f},w={:.2f})", clip.x / w, clip.y / w, clip.w);
			}
			logger::info("[MOS][NestedPane] hand frame corners in captured clip space:{} raised={} frame={}x{}",
				corners, cached.presentationMode == HandMirrorLastGoodPresentationPolicy::PresentationMode::kRaised,
				request.frame.width, request.frame.height);
		}
		const auto status = state.renderer.Draw(context, request, nullptr);
		static std::atomic<int> logBudget{ 6 };
		if (!MirrorPaneRenderer::IsSuccessfulDraw(status) && logBudget.fetch_sub(1, std::memory_order_relaxed) > 0) {
			logger::info(
				"[MOS][NestedPane] hand frame on third-person pane rejected status={} captured center=({:.1f},{:.1f},{:.1f}) t=({:.2f},{:.2f},{:.2f}) b=({:.2f},{:.2f},{:.2f}) live center=({:.1f},{:.1f},{:.1f}) t=({:.2f},{:.2f},{:.2f}) b=({:.2f},{:.2f},{:.2f}) raised={}",
				static_cast<unsigned>(status),
				capturedPane.center.x, capturedPane.center.y, capturedPane.center.z,
				capturedPane.tangentExtent.x, capturedPane.tangentExtent.y, capturedPane.tangentExtent.z,
				capturedPane.bitangentExtent.x, capturedPane.bitangentExtent.y, capturedPane.bitangentExtent.z,
				livePane.center.x, livePane.center.y, livePane.center.z,
				livePane.tangentExtent.x, livePane.tangentExtent.y, livePane.tangentExtent.z,
				livePane.bitangentExtent.x, livePane.bitangentExtent.y, livePane.bitangentExtent.z,
				cached.presentationMode == HandMirrorLastGoodPresentationPolicy::PresentationMode::kRaised);
		}
		return status;
	}

	bool IsEnabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire) &&
			!g_loadTransitionSuspended.load(std::memory_order_acquire) &&
			!g_faulted.load(std::memory_order_acquire);
	}

	bool IsFaultStopped() noexcept
	{
		return g_faulted.load(std::memory_order_acquire);
	}

	void LogDiagnostics(const char* reason)
	{
		if (!Requested())
			return;
		const auto& state = State();
		const auto storeAudit = state.store ? state.store->Inspect() :
			StorePolicy::AuditSnapshot{};
		const auto reservationAudit = state.coordinator ?
			state.coordinator->Inspect() : Reservation::AuditSnapshot{};
		logger::info("[RR][HandMirrorReflection][frame-raster] returns={} samples={}",
			state.diagnostics.framePresentationReturns, state.diagnostics.framePresentationSamples);
		const auto* currentSlot = state.currentPublication.valid &&
			state.currentPublication.physicalRole < state.nativeSlots.size() ?
			&state.nativeSlots[state.currentPublication.physicalRole] : nullptr;
		logger::info("[RR][HandMirrorReflection][resolution] reducedLowered={} current={}x{} capture={} backing={}x{}",
			LoweredPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed),
			currentSlot && currentSlot->valid ? currentSlot->frame.width : 0,
			currentSlot && currentSlot->valid ? currentSlot->frame.height : 0,
			currentSlot && currentSlot->valid ? currentSlot->frame.captureSequence : 0,
			PrivateTargetDimension(), PrivateTargetHeight());
		logger::info(
			"[RR][HandMirrorReflection] diagnostics ({}) requested={} enabled={} faultStopped={} target={}x{} targetsInitialized={} receipt(S/M/G)={}/{}/{} generic(entries/inactiveOrNullPass/thread/sourceZero/invalidReceiptTarget/runWithReached)={}/{}/{}/{}/{}/{} mainQuery(ready/deliveryFault/private/mainWorld/noFrame/noDeferred/frameMismatch/noGraphics/engineUnavailable/graphicsMismatch/refractionPass)={}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{} cohort(status/returns/begins/publications/stages/restages/single/double/zeroDark/mismatchDark/overflowDark/visibilityAdvances/consumes)={}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{} direct(attempts/successes/projectionMisses/reflectedRetargets/retargetRejects/last)={}/{}/{}/{}/{}/{} visibility(streak/ineligible/last/currentOwner)={}/{}/{}/{} store(phase/source/current/active/retired)={}/{}/{}/{}/{} reservation(phase/pending/active/lastSource)={}/{}/{}/{} apiV1=false thirdPersonDelivery=false",
			reason ? reason : "diagnostic", Requested(), IsEnabled(),
			IsFaultStopped(), PrivateTargetDimension(), PrivateTargetHeight(),
			state.targetsInitialized,
			state.diagnostics.lastReceipt.sourceSequence,
			state.diagnostics.lastReceipt.mainViewFrame,
			state.diagnostics.lastReceipt.graphicsFrame,
			state.diagnostics.genericEntries,
			state.diagnostics.genericInactiveOrNullPass,
			state.diagnostics.genericThreadRejects,
			state.diagnostics.genericSourceZero,
			state.diagnostics.genericInvalidReceiptOrTarget,
			state.diagnostics.genericRunWithReached,
			state.diagnostics.mainQueryReady,
			state.diagnostics.mainQueryDeliveryFaulted,
			state.diagnostics.mainQueryPrivatePassActive,
			state.diagnostics.mainQueryMainWorldActive,
			state.diagnostics.mainQueryMainWorldFrameMissing,
			state.diagnostics.mainQueryDeferredSnapshotInvalid,
			state.diagnostics.mainQueryDeferredFrameMismatch,
			state.diagnostics.mainQueryDeferredGraphicsMissing,
			state.diagnostics.mainQueryEngineStateUnavailable,
			state.diagnostics.mainQueryGraphicsFrameMismatch,
			state.diagnostics.mainQueryRefractionPassActive,
			static_cast<int>(state.diagnostics.lastStatus),
			state.diagnostics.lastReturnCount,
			state.diagnostics.captureBegins,
			state.diagnostics.publications,
			state.diagnostics.firstStages,
			state.diagnostics.latestRestages,
			state.diagnostics.singleAccepted,
			state.diagnostics.doubleAccepted,
			state.diagnostics.zeroDark,
			state.diagnostics.mismatchDark,
			state.diagnostics.overflowDark,
			state.diagnostics.visibilityAdvances,
			state.diagnostics.publicationConsumes,
			state.diagnostics.directDrawAttempts,
			state.diagnostics.directDrawSuccesses,
			state.diagnostics.directDrawProjectionMisses,
			state.diagnostics.directReflectedRetargets,
			state.diagnostics.directReflectedRetargetRejects,
			MirrorPaneRenderer::ToString(
				state.diagnostics.lastDirectDrawStatus),
			state.visibility.consecutiveRaisedVisibleFrames,
			state.visibility.consecutiveIneligibleFrames,
			state.visibility.lastVisibleSourceSequence,
			static_cast<int>(state.visibility.currentOwner),
			static_cast<int>(storeAudit.phase), storeAudit.sourceSequence,
			storeAudit.currentPublicationCount, storeAudit.activeAttemptCount,
			storeAudit.pendingRetirementCount,
			static_cast<int>(reservationAudit.phase),
			reservationAudit.pendingReservationCount,
			reservationAudit.activeTicketCount,
			reservationAudit.lastSourceSequence);
		logger::info(
			"[RR][HandMirrorReflection][affine-motion] diagnostics ({}) history(valid/S/M/G/capture/delivery)={}/{}/{}/{}/{}/{} motion(eligible/written/writeRejects)={}/{}/{} historyRejects(missing/receipt/identity/viewport/capture)={}/{}/{}/{}/{} resets(total/noFinal/lowered/noncurrentPublication/motionTarget/drift/drawFailure/lifecycle)={}/{}/{}/{}/{}/{}/{}/{} commits={}",
			reason ? reason : "diagnostic",
			state.affineMotionHistory.valid,
			state.affineMotionHistory.receipt.sourceSequence,
			state.affineMotionHistory.receipt.mainViewFrame,
			state.affineMotionHistory.receipt.graphicsFrame,
			state.affineMotionHistory.renderer.captureSequence,
			state.affineMotionHistory.renderer.deliveryFrame,
			state.diagnostics.affineMotionEligible,
			state.diagnostics.affineMotionWritten,
			state.diagnostics.affineMotionWriteRejects,
			state.diagnostics.affineHistoryMissingRejects,
			state.diagnostics.affineHistoryReceiptRejects,
			state.diagnostics.affineHistoryIdentityRejects,
			state.diagnostics.affineHistoryViewportRejects,
			state.diagnostics.affineHistoryCaptureRejects,
			state.diagnostics.affineHistoryResets,
			state.diagnostics.affineHistoryNoFinalDrawResets,
			state.diagnostics.affineHistoryLoweredResets,
			state.diagnostics.affineHistoryNoncurrentPublicationResets,
			state.diagnostics.affineHistoryMotionTargetResets,
			state.diagnostics.affineHistoryDriftResets,
			state.diagnostics.affineHistoryDrawFailureResets,
			state.diagnostics.affineHistoryLifecycleResets,
			state.diagnostics.affineHistoryCommits);
		logger::info(
			"[RR][HandMirrorReflection][stable-presentation] diagnostics ({}) retry(attempts/mainNotVisible/reflectedUncovered/successes/failures)={}/{}/{}/{}/{} currentPose(samples/rejects)={}/{} temporalHistory(requests/lowered/writes)={}/{}/{}",
			reason ? reason : "diagnostic",
			state.diagnostics.stablePresentationRetryAttempts,
			state.diagnostics.stablePresentationRetryMainViewNotVisible,
			state.diagnostics.stablePresentationRetryReflectedUncovered,
			state.diagnostics.stablePresentationRetrySuccesses,
			state.diagnostics.stablePresentationRetryFailures,
			state.diagnostics.currentPresentationPoseReplaySamples,
			state.diagnostics.currentPresentationPoseReplayRejects,
			state.diagnostics.temporalHistoryRejectRequests,
			state.diagnostics.loweredTemporalHistoryRejectRequests,
			state.diagnostics.temporalHistoryRejectWrites);
		logger::info(
			"[RR][HandMirrorReflection][cover-clip] diagnostics ({}) checks={} invalid={} risks(main/reflected)={}/{} transitions={} logs={}/{} evidence(signals/logs/max/waitFailures/eventActive)={}/{}/{}/{}/{} last(mask(bitsMain1/reflected2/invalid4/nonFinite8)/positiveMain/positiveReflected/total/minimumMainW/minimumReflectedW)=0x{:02X}/{}/{}/{}/{:.6f}/{:.6f}",
			reason ? reason : "diagnostic",
			state.diagnostics.handCoverClipChecks,
			state.diagnostics.handCoverClipInvalid,
			state.diagnostics.handCoverMainWRisks,
			state.diagnostics.handCoverReflectedWRisks,
			state.diagnostics.handCoverRiskTransitions,
			state.diagnostics.handCoverTransitionLogs,
			kMaximumHandCoverTransitionLogs,
			state.diagnostics.handCoverEvidenceSignals,
			state.diagnostics.handCoverEvidenceLogs,
			kMaximumHandCoverEvidenceLogs,
			state.diagnostics.handCoverEvidenceWaitFailures,
			g_handMirrorEvidenceEvent.load(std::memory_order_acquire) != nullptr,
			static_cast<unsigned>(state.diagnostics.lastHandCoverRiskMask),
			static_cast<unsigned>(
				state.diagnostics.lastHandCoverMainPositiveWVertices),
			static_cast<unsigned>(
				state.diagnostics.lastHandCoverReflectedPositiveWVertices),
			static_cast<unsigned>(state.diagnostics.lastHandCoverVertexCount),
			state.diagnostics.lastHandCoverMinimumMainW,
			state.diagnostics.lastHandCoverMinimumReflectedW);
		logger::info(
			"[RR][HandMirrorReflection] clean-miss continuity ({}) requested={} effective={} held={} heldThroughSource={} validatedSource={} maxSources={} lastPresentedToken={} arms/draws/identityRejects/expiryReleases/orderingPromotions={}/{}/{}/{}/{}",
			reason ? reason : "diagnostic",
			g_cleanMissContinuityRequested.load(std::memory_order_acquire),
			CleanMissContinuityEffective(),
			state.heldPredecessorRetirement != StorePolicy::RetirementReceipt{},
			state.heldCleanMissExpirySource,
			state.heldCleanMissValidatedSource,
			Presentation::kMaximumConsecutiveCleanMissSources,
			state.lastPresentedPublicationToken,
			state.diagnostics.cleanMissContinuityArms,
			state.diagnostics.cleanMissContinuityDraws,
			state.diagnostics.cleanMissContinuityIdentityRejects,
			state.diagnostics.cleanMissContinuityExpiryReleases,
			state.diagnostics.orderingPredecessorPromotions);
		logger::info(
			"[RR][HandMirrorReflection] zero-return fallback ({}) entryRetain(attempts/successes)={}/{} surfaceSelections(entry/detachedLastGood)={}/{} rejects(returnState/entryMissing/candidate/revalidate/liveReturn/publication)={}/{}/{}/{}/{}/{} stages={} draws={}",
			reason ? reason : "diagnostic",
			state.diagnostics.zeroFallbackEntryRetainAttempts,
			state.diagnostics.zeroFallbackEntryRetains,
			state.diagnostics.zeroFallbackEntrySurfaceSelections,
			state.diagnostics.zeroFallbackDetachedSurfaceSelections,
			state.diagnostics.zeroFallbackReturnStateRejects,
			state.diagnostics.zeroFallbackEntryMissingRejects,
			state.diagnostics.zeroFallbackCandidateRejects,
			state.diagnostics.zeroFallbackRevalidateRejects,
			state.diagnostics.zeroFallbackLiveReturnRejects,
			state.diagnostics.zeroFallbackPublicationRejects,
			state.diagnostics.zeroFallbackStages,
			state.diagnostics.zeroFallbackDraws);
		logger::info(
			"[RR][HandMirrorReflection] last-good presentation ({}) valid={} detachedSnapshot={} source/capture={}/{} commits={} apertureCoverageRejects={} snapshotRejects={} replay(attempts/draws/coverageRejects/detachedCoverageFallbacks/refreshCoverageRejects/identityRejects/presentationModeRejects/targetRejects/loweredStaleRejects)={}/{}/{}/{}/{}/{}/{}/{}/{} invalidations={} loweredYieldsToWall={} wallHolds={} (wallIneligible={} ambiguous={} handHysteresis={}) interleave(hand/wall/holds)={}/{}/{} fence(hand/wall/dark/invalid)={}/{}/{}/{} fenceInputs=0x{:02X}",
			reason ? reason : "diagnostic",
			LastGoodPresentationValid(state),
			state.lastGoodReplayTexture && state.lastGoodReplaySRV,
			state.lastGoodPresentation.sourcePublication.publication
				.attempt.surface.sourceSequence,
			state.lastGoodPresentation.frame.captureSequence,
			state.diagnostics.lastGoodCommits,
			state.diagnostics.lastGoodApertureCoverageRejects,
			state.diagnostics.lastGoodSnapshotRejects,
			state.diagnostics.lastGoodReplayAttempts,
			state.diagnostics.lastGoodReplayDraws,
			state.diagnostics.lastGoodReplayCoverageRejects,
			state.diagnostics.lastGoodReplayDetachedCoverageFallbacks,
			state.diagnostics.lastGoodReplayRefreshCoverageRejects,
			state.diagnostics.lastGoodReplayIdentityRejects,
			state.diagnostics.lastGoodReplayPresentationModeRejects,
			state.diagnostics.lastGoodReplayTargetRejects,
			state.diagnostics.lastGoodReplayLoweredStaleRejects,
			state.diagnostics.lastGoodInvalidations,
			state.diagnostics.handLoweredYieldsToWall,
			state.diagnostics.wallPublicationHolds,
			state.diagnostics.wallPublicationHoldsWallIneligible,
			state.diagnostics.wallPublicationHoldsAmbiguous,
			state.diagnostics.wallPublicationHoldsHandHysteresis,
			state.diagnostics.interleaveHandSources,
			state.diagnostics.interleaveWallSources,
			state.diagnostics.interleaveHandGrantHolds,
			state.diagnostics.fenceHandSelections,
			state.diagnostics.fenceWallSelections,
			state.diagnostics.fenceDarkSelections,
			state.diagnostics.fenceInvalidSelections,
			state.diagnostics.fenceLastInputs);
		logger::info(
			"[RR][HandMirror][live-pose] diagnostics ({}) enabled={} publication(modeRejects/ageRejects)={}/{} replayMissingPoseRejects={} currentRaised={}",
			reason ? reason : "diagnostic",
			HandMirrorApprovedContentReadOnlyObserver::LivePosePresentationEnabled(),
			state.diagnostics.livePosePublicationModeRejects,
			state.diagnostics.livePosePublicationAgeRejects,
			state.diagnostics.livePoseReplayMissingPoseRejects,
			state.visibility.raisedPresentation);
		logger::info(
			"[RR][HandMirrorReflection] hand sampler ({}) selected={} active={} rendererReady={}",
			reason ? reason : "diagnostic",
			SamplingModeName(SelectHandSamplingMode()),
			SamplingModeName(state.renderer.ActiveSamplingMode()),
			state.renderer.Ready());
		logger::info(
			"[RR][HandMirrorReflection] delivery routes ({}) actualOM(freshAttempts/freshSuccesses/replayCalls/replayDraws)={}/{}/{}/{} outer(freshAttempts/freshSuccesses/replayCalls/replayDraws)={}/{}/{}/{}",
			reason ? reason : "diagnostic",
			state.diagnostics.exactActualOmFreshDrawAttempts,
			state.diagnostics.exactActualOmFreshDrawSuccesses,
			state.diagnostics.exactActualOmLastGoodReplayCalls,
			state.diagnostics.exactActualOmLastGoodReplayDraws,
			state.diagnostics.outerFreshDrawAttempts,
			state.diagnostics.outerFreshDrawSuccesses,
			state.diagnostics.outerLastGoodReplayCalls,
			state.diagnostics.outerLastGoodReplayDraws);
		const bool postResolveInstalled =
			g_postResolveHookInstalled.load(std::memory_order_acquire);
		const bool postResolveOwned =
			postResolveInstalled && PostResolveHookStillOwned();
		logger::info(
			"[RR][HandMirrorReflection][post-resolve] diagnostics ({}) marker/required/attempted/installed/owned={}/{}/{}/{}/{} install(attempts/successes/failures/rollbackFailures)={}/{}/{}/{} chain(nativeCalls/nativeReturns/callbacks/reentries/nested/nonPrimary/exceptions/lastFlags/lastException)={}/{}/{}/{}/{}/{}/{}/0x{:08X}/0x{:08X} skips(private/liveLifecycle/entryFrame/zeroDelta/unstampedDelta)={}/{}/{}/{}/{} stage(exact/stageToken/expectedCount/presentedToken/valid/callbacks/new)={}/{}/{}/{}/{}/{}/{} rejects(token/source/receipt/frame/accumulator/target/ownership)={}/{}/{}/{}/{}/{}/{} draw(freshAttempts/freshSuccesses/replayCalls/replayDraws/presentations/status)={}/{}/{}/{}/{}/{} shutdown(restores/ownershipSkips)={}/{}",
			reason ? reason : "diagnostic",
			g_postResolveEnableMarkerLatched.load(std::memory_order_acquire),
			PostResolveHookRequired(),
			g_postResolveHookInstallAttempted.load(std::memory_order_acquire),
			postResolveInstalled, postResolveOwned,
			g_postResolveHookDiagnostics.installAttempts.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.installSuccesses.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.installFailures.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.transactionRollbackFailures.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.nativeCalls.load(std::memory_order_relaxed),
			g_postResolveHookDiagnostics.nativeReturns.load(std::memory_order_relaxed),
			g_postResolveHookDiagnostics.callbackCalls.load(std::memory_order_relaxed),
			g_postResolveHookDiagnostics.callbackReentries.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.nestedChains.load(std::memory_order_relaxed),
			g_postResolveHookDiagnostics.inactiveChains.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.callbackExceptions.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.lastFlags.load(std::memory_order_relaxed),
			g_postResolveHookDiagnostics.lastException.load(std::memory_order_relaxed),
			g_postResolveHookDiagnostics.privatePassSkips.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.liveLifecycleSkips.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.entryFrameRejects.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.zeroDeltaChains.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.unstampedDeltaRejects.load(
				std::memory_order_relaxed),
			g_firstPerson.exactNormalReturnCount,
			g_firstPerson.postResolveStageToken,
			g_firstPerson.postResolveExpectedReturnCount,
			g_firstPerson.postResolvePresentedToken,
			g_firstPerson.staged.valid,
			state.diagnostics.postResolveCallbacks,
			state.diagnostics.postResolveNewStageObservations,
			state.diagnostics.postResolveTokenRejects,
			state.diagnostics.postResolveSourceRejects,
			state.diagnostics.postResolveReceiptRejects,
			state.diagnostics.postResolveFrameRejects,
			state.diagnostics.postResolveAccumulatorRejects,
			state.diagnostics.postResolveTargetRejects,
			g_postResolveHookDiagnostics.ownershipLosses.load(
				std::memory_order_relaxed),
			state.diagnostics.postResolveFreshDrawAttempts,
			state.diagnostics.postResolveFreshDrawSuccesses,
			state.diagnostics.postResolveLastGoodReplayCalls,
			state.diagnostics.postResolveLastGoodReplayDraws,
			state.diagnostics.postResolvePresentations,
			MirrorPaneRenderer::ToString(
				state.diagnostics.lastPostResolveDrawStatus),
			g_postResolveHookDiagnostics.shutdownRestores.load(
				std::memory_order_relaxed),
			g_postResolveHookDiagnostics.shutdownOwnershipSkips.load(
				std::memory_order_relaxed));
		logger::info(
			"[RR][HandMirrorReflection] first-return ({}) checks={} passes={} lastRejectMask=0x{:08X} lastDriftMask=0x{:08X} unclassified={} diagnosticDivergences={} rejects(candidate/captureTarget/frameMain/frameGraphics/receipt/pass/device/context/resourcesUnchecked/liveResources/owner/surface/stableGeneration/presentationInvalid)={}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{}/{} drift(view/projection/viewProjection/origin/viewport/geometry)={}/{}/{}/{}/{}/{}",
			reason ? reason : "diagnostic",
			state.diagnostics.firstReturnChecks,
			state.diagnostics.firstReturnPasses,
			state.diagnostics.lastFirstReturnRejectMask,
			state.diagnostics.lastFirstReturnDriftMask,
			state.diagnostics.firstReturnUnclassifiedRejects,
			state.diagnostics.firstReturnDiagnosticDivergences,
			state.diagnostics.firstReturnCandidateRejects,
			state.diagnostics.firstReturnTargetRejects,
			state.diagnostics.firstReturnMainViewFrameRejects,
			state.diagnostics.firstReturnGraphicsFrameRejects,
			state.diagnostics.firstReturnReceiptRejects,
			state.diagnostics.firstReturnPassRejects,
			state.diagnostics.firstReturnDeviceRejects,
			state.diagnostics.firstReturnContextRejects,
			state.diagnostics.firstReturnResourcesUnchecked,
			state.diagnostics.firstReturnResourceRejects,
			state.diagnostics.firstReturnOwnerRejects,
			state.diagnostics.firstReturnSurfaceRejects,
			state.diagnostics.firstReturnStableGenerationRejects,
			state.diagnostics.firstReturnPresentationInvalidRejects,
			state.diagnostics.firstReturnViewRejects,
			state.diagnostics.firstReturnProjectionRejects,
			state.diagnostics.firstReturnViewProjectionRejects,
			state.diagnostics.firstReturnOriginRejects,
			state.diagnostics.firstReturnViewportRejects,
			state.diagnostics.firstReturnGeometryRejects);
		logger::info(
			"[RR][HandMirrorReflection] second-return ({}) checks={} passes={} lastRejectMask=0x{:08X} lastDriftMask=0x{:08X} diagnosticDivergences={} immutable(receiptS/receiptM/receiptG/owner/generation/surface/device/context)=0x{:08X} passLocal(color/depth/geometry/view+viewProjection/projection/origin/viewport)=0x{:08X}",
			reason ? reason : "diagnostic",
			state.diagnostics.secondReturnChecks,
			state.diagnostics.secondReturnPasses,
			state.diagnostics.lastSecondReturnRejectMask,
			state.diagnostics.lastSecondReturnDriftMask,
			state.diagnostics.secondReturnDiagnosticDivergences,
			state.diagnostics.lastSecondReturnRejectMask,
			state.diagnostics.lastSecondReturnDriftMask);
	}
}
