#include "PCH.h"

#include "MainViewVariantGuardPolicy.h"
#include "MirrorsOfSkyrimCameraOverride.h"

#include "MirrorCameraUploadLeasePolicy.h"
#include "MirrorCameraUpdateRuntimePolicy.h"
#include "MirrorSunShadows.h"
#include "PeerDetection.h"

namespace SecondView
{
	// Forward-declared to avoid an include cycle; defined in SecondView.cpp.
	[[nodiscard]] std::uint64_t MirrorPrimaryPassSequence() noexcept;
	[[nodiscard]] bool IsInsidePrivateCapture() noexcept;
}

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace MirrorCameraOverride
{
	namespace
	{
		constexpr float kPaneFitMarginScale = 1.15F;
		// Exact private-camera uploads use the same origin. One eighth of a game unit
		// leaves float headroom while excluding unrelated camera variants.
		constexpr float kPrivateUploadOriginToleranceUnits = 0.125F;
		constexpr std::ptrdiff_t kCameraWorldTransformOffset = 0x7C;

		// ViewData inner offsets are identical on flat and VR.
		constexpr std::ptrdiff_t kViewDataViewMatrix = 0x30;
		constexpr std::ptrdiff_t kViewDataProjectionMatrix = 0x70;
		constexpr std::ptrdiff_t kViewDataViewProjectionMatrix = 0xB0;

		// Runtime-selected RendererShadowState layout.  Flat SE/AE holds one
		// posAdjust/cameraData; VR 1.4.15 holds two eyes of each (eye 1 follows
		// eye 0 by sizeof(NiPoint3) / sizeof(ViewData)).
		//
		// The private mirror capture is deliberately MONO on VR: the reflected
		// camera has no eye separation, so every eye slot must hold the same
		// basis/view/projection/posAdjust.  VR UpdateCameraData (0xDBCE30) uploads
		// cameraData[currentEyeIndex] and cameraData[1] in a loop, so patching eye
		// 0 alone would leave the right-eye instance on the unpatched camera.
		// All reads of the "current" camera come from eye 0; every write lands in
		// every eye.  Nothing is restored on End(): Skyrim rebuilds both eye slots
		// from the NiCamera on the next SetCameraData, exactly as on flat, and a
		// stale restore after that rebuild would corrupt the main view.
		struct ShadowStateLayout
		{
			std::ptrdiff_t cameraWorldTransform{ 0x7C };
			std::ptrdiff_t posAdjust{ 0 };
			std::ptrdiff_t previousPosAdjust{ 0 };
			std::ptrdiff_t primaryViewData{ 0 };
			std::size_t eyeCount{ 1 };
			std::ptrdiff_t posAdjustEyeStride{ 0x0C };
			std::ptrdiff_t viewDataEyeStride{ 0x250 };

			[[nodiscard]] constexpr std::ptrdiff_t PosAdjust(
				const std::size_t eye = 0) const noexcept
			{
				return posAdjust +
					static_cast<std::ptrdiff_t>(eye) * posAdjustEyeStride;
			}
			[[nodiscard]] constexpr std::ptrdiff_t PreviousPosAdjust(
				const std::size_t eye = 0) const noexcept
			{
				return previousPosAdjust +
					static_cast<std::ptrdiff_t>(eye) * posAdjustEyeStride;
			}
			// viewUp/viewRight/viewForward (0x00..0x2C) followed by viewMat (0x30).
			[[nodiscard]] constexpr std::ptrdiff_t ViewBasis(
				const std::size_t eye = 0) const noexcept
			{
				return primaryViewData +
					static_cast<std::ptrdiff_t>(eye) * viewDataEyeStride;
			}
			[[nodiscard]] constexpr std::ptrdiff_t ViewMatrix(
				const std::size_t eye = 0) const noexcept
			{
				return primaryViewData +
					static_cast<std::ptrdiff_t>(eye) * viewDataEyeStride +
					kViewDataViewMatrix;
			}
			[[nodiscard]] constexpr std::ptrdiff_t ProjectionMatrix(
				const std::size_t eye = 0) const noexcept
			{
				return primaryViewData +
					static_cast<std::ptrdiff_t>(eye) * viewDataEyeStride +
					kViewDataProjectionMatrix;
			}
			[[nodiscard]] constexpr std::ptrdiff_t ViewProjectionMatrix(
				const std::size_t eye = 0) const noexcept
			{
				return primaryViewData +
					static_cast<std::ptrdiff_t>(eye) * viewDataEyeStride +
					kViewDataViewProjectionMatrix;
			}
		};
		constexpr ShadowStateLayout kFlatShadowStateLayout{
			.cameraWorldTransform = kCameraWorldTransformOffset,
			.posAdjust = 0x35C,
			.previousPosAdjust = 0x368,
			.primaryViewData = 0x380,
			.eyeCount = 1
		};
		constexpr ShadowStateLayout kVRShadowStateLayout{
			.cameraWorldTransform = kCameraWorldTransformOffset,
			.posAdjust = 0x3A4,
			.previousPosAdjust = 0x3BC,
			.primaryViewData = 0x3E0,
			.eyeCount = 2
		};
		// Bytes copied from eye 0 into every other eye slot on VR: the three
		// basis vectors plus the view matrix (0x00..0x6F of ViewData).
		constexpr std::size_t kViewBasisAndViewMatrixBytes =
			static_cast<std::size_t>(kViewDataProjectionMatrix);

		static_assert(sizeof(RE::NiPoint3) == 0xC);
		static_assert(sizeof(RE::NiTransform) == 0x34);
		static_assert(sizeof(DirectX::XMFLOAT4X4) == 0x40);
		static_assert(sizeof(RE::BSGraphics::ViewData) == 0x250);
		static_assert(offsetof(RE::BSGraphics::ViewData, viewMat) == kViewDataViewMatrix);
		static_assert(offsetof(RE::BSGraphics::ViewData, projMat) == kViewDataProjectionMatrix);
		static_assert(offsetof(RE::BSGraphics::ViewData, viewProjMat) == kViewDataViewProjectionMatrix);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, posAdjust) ==
			kFlatShadowStateLayout.posAdjust);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, cameraData) ==
			kFlatShadowStateLayout.primaryViewData);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::FLAT_RUNTIME_DATA, previousPosAdjust) ==
			kFlatShadowStateLayout.previousPosAdjust);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, posAdjust) ==
			kVRShadowStateLayout.posAdjust);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, previousPosAdjust) ==
			kVRShadowStateLayout.previousPosAdjust);
		static_assert(offsetof(
			RE::BSGraphics::RendererShadowState::VR_RUNTIME_DATA, cameraData) ==
			kVRShadowStateLayout.primaryViewData);
		static_assert(kFlatShadowStateLayout.ViewMatrix() == 0x380 + 0x30);
		static_assert(kVRShadowStateLayout.ViewProjectionMatrix(1) ==
			0x3E0 + 0x250 + 0xB0);
		static_assert(kVRShadowStateLayout.PosAdjust(1) == 0x3A4 + 0x0C);
		static_assert(kVRShadowStateLayout.PreviousPosAdjust(1) == 0x3BC + 0x0C);
		static_assert(kVRShadowStateLayout.ViewBasis(1) == 0x3E0 + 0x250);
		static_assert(kViewBasisAndViewMatrixBytes == 0x70);
		static_assert(kVRShadowStateLayout.ViewBasis(1) +
			static_cast<std::ptrdiff_t>(kViewBasisAndViewMatrixBytes) ==
			kVRShadowStateLayout.ProjectionMatrix(1));

		[[nodiscard]] const ShadowStateLayout& CurrentShadowStateLayout() noexcept
		{
			return SupportedRuntimePolicy::IsExactVRRuntime() ?
				kVRShadowStateLayout : kFlatShadowStateLayout;
		}

		using OverridePhase = MirrorCameraUploadLeasePolicy::Phase;
		using UploadPatchResult = MirrorCameraUploadLeasePolicy::PatchOutcome;
		std::atomic_bool g_skyRecenterRequested{ false };
		std::atomic_bool g_skyRecenterInstalled{ false };
		std::atomic<std::uint64_t> g_skyRecenters{ 0 };
		// The recenter fires but cancels itself out unless the camera node also
		// carries the reflected origin; these say whether it did.
		std::atomic<std::uint64_t> g_skyCameraTranslationLeases{ 0 };
		std::atomic<std::uint64_t> g_skyCameraTranslationRefusals{ 0 };
		std::atomic<std::uint64_t> g_skyRecenterRejects{ 0 };

		struct OverrideState
		{
			const RE::NiCamera* camera{ nullptr };
			PlanarMirrorMath::Plane plane{};
			PlanarMirrorMath::PaneFit paneFit{};
			DirectX::XMFLOAT4X4 patchedViewProjection{};
			DirectX::XMFLOAT3 patchedOrigin{};
			// A persistent mirror lease must not compound pane-fit/oblique edits when
			// Skyrim calls UpdateCameraData again without first rebuilding ViewData.
			DirectX::XMFLOAT4X4 sourceProjection{};
			HandPhysicalDepthProjection handPhysicalDepthProjection{};
			// Placed captures (core run 4, Skeever beam / Silver-Blood smoke):
			// SoftEffect needs conventional scene depth, the raster is oblique.
			HandPhysicalDepthProjection placedDepthProjection{};
			// Reflected camera origin captured at Begin so the upload seam can
			// prove that the shadow-state block belongs to the armed camera.
			DirectX::XMFLOAT3 expectedOrigin{};
			CameraPose expectedPose{};
			float clipBias{ kDefaultClipBias };
			bool expectedOriginValid{ false };
			bool paneFitValid{ false };
			bool sourceProjectionValid{ false };
			bool handCaptureAtBegin{ false };
			bool handPhysicalRasterClip{ false };
			bool reentrantUploadSeen{ false };
			OverridePhase phase{ OverridePhase::kInactive };
		};

		struct Counters
		{
			std::atomic<std::uint64_t> arms{ 0 };
			std::atomic<std::uint64_t> hookCalls{ 0 };
			std::atomic<std::uint64_t> patchedUploads{ 0 };
			std::atomic<std::uint64_t> failedUploads{ 0 };
			std::atomic<std::uint64_t> paneFitApplied{ 0 };
			std::atomic<std::uint64_t> paneFitFallbacks{ 0 };
			std::atomic<std::uint64_t> paneFitInvalidInputs{ 0 };
			std::atomic<std::uint64_t> paneFitInvalidReferenceProjections{ 0 };
			std::atomic<std::uint64_t> paneFitNotFullyInFront{ 0 };
			std::atomic<std::uint64_t> paneFitInvalidSlopes{ 0 };
			std::atomic<std::uint64_t> paneFitInvalidOutputs{ 0 };
			std::atomic<std::uint64_t> paneFitCornerClamps{ 0 };
			std::atomic<std::uint64_t> variantMismatchedUploads{ 0 };
			std::atomic<std::uint64_t> handPoseMismatchSkips{ 0 };
			std::atomic<std::uint64_t> handStandardProjectionApplied{ 0 };
			std::atomic<std::uint64_t> handPhysicalObliqueProjectionApplied{ 0 };
			std::atomic<std::uint64_t> repeatedUploadAttempts{ 0 };
			std::atomic<std::uint64_t> repeatedPatchedUploads{ 0 };
			std::atomic<std::uint64_t> unownedUploadSkips{ 0 };
			std::atomic<std::uint64_t> repeatedIdentityRejects{ 0 };
			std::atomic<std::uint64_t> reentrantUploadRejects{ 0 };
			std::atomic<std::uint32_t> variantUploadDeltaMaxBits{ 0 };
			// Last applied fitted-frustum half-extent slopes (1/_11, 1/_22).
			std::atomic<std::uint32_t> lastFitHalfSlopeXBits{ 0 };
			std::atomic<std::uint32_t> lastFitHalfSlopeYBits{ 0 };
			// Per-phase gauges: the private mirror pass can patch multiple uploads
			// with different camera origins. Index 0 is the first upload of the pass,
			// index 1 is the second, keyed off
			// SecondView::MirrorPrimaryPassSequence().
			std::atomic<std::uint32_t> phaseFitHalfSlopeXBits[2]{};
			std::atomic<std::uint32_t> phaseOriginXBits[2]{};
			std::atomic<std::uint32_t> phaseOriginYBits[2]{};
			std::atomic<std::uint32_t> phaseOriginZBits[2]{};
			std::atomic<std::uint64_t> unexpectedFlags{ 0 };
			std::atomic<std::uint64_t> nestedBeginRejects{ 0 };
			std::atomic<std::uint64_t> cameraSideRejects{ 0 };
			std::atomic<std::uint64_t> exceptions{ 0 };
		};

		thread_local OverrideState g_override{};
		thread_local std::uint64_t g_phasePassSequence = 0;
		thread_local std::uint32_t g_phaseUploadOrdinal = 0;
		Counters g_counters{};
		std::atomic<unsigned long> g_lastException{ 0 };
		std::atomic<bool> g_installAttempted{ false };
		std::atomic<bool> g_wallMirrorRequested{ false };
		std::atomic<bool> g_handMirrorHookRequested{ false };
		std::atomic<bool> g_hookInstalled{ false };
		std::atomic<bool> g_runtimeEnabled{ false };
		std::atomic<bool> g_handMirrorRuntimeEnabled{ false };
		std::atomic<std::uint64_t> g_handPhysicalDepthGenerationCursor{ 0 };
		thread_local bool g_handMirrorCaptureScope = false;
		thread_local bool g_handMirrorCaptureNativeFaulted = false;
		thread_local DirectX::XMFLOAT4X4 g_handMirrorRasterProjection{};
		thread_local bool g_handMirrorRasterProjectionValid = false;

		[[nodiscard]] bool AllocateHandPhysicalDepthGeneration(
			std::uint64_t& output) noexcept
		{
			output = 0;
			auto current = g_handPhysicalDepthGenerationCursor.load(
				std::memory_order_relaxed);
			while (current != UINT64_MAX) {
				if (g_handPhysicalDepthGenerationCursor.compare_exchange_weak(
						current, current + 1, std::memory_order_acq_rel,
						std::memory_order_relaxed)) {
					output = current + 1;
					return true;
				}
			}
			return false;
		}

		void RecordPaneFitFailure(PlanarMirrorMath::PaneFitFailure failure) noexcept
		{
			switch (failure) {
			case PlanarMirrorMath::PaneFitFailure::kInvalidInput:
				g_counters.paneFitInvalidInputs.fetch_add(1, std::memory_order_relaxed);
				break;
			case PlanarMirrorMath::PaneFitFailure::kInvalidReferenceProjection:
				g_counters.paneFitInvalidReferenceProjections.fetch_add(
					1, std::memory_order_relaxed);
				break;
			case PlanarMirrorMath::PaneFitFailure::kPaneNotFullyInFront:
				g_counters.paneFitNotFullyInFront.fetch_add(1, std::memory_order_relaxed);
				break;
			case PlanarMirrorMath::PaneFitFailure::kInvalidSlope:
				g_counters.paneFitInvalidSlopes.fetch_add(1, std::memory_order_relaxed);
				break;
			case PlanarMirrorMath::PaneFitFailure::kInvalidOutput:
				g_counters.paneFitInvalidOutputs.fetch_add(1, std::memory_order_relaxed);
				break;
			case PlanarMirrorMath::PaneFitFailure::kNone:
			default:
				break;
			}
		}

		[[nodiscard]] bool MultiplyViewProjection(
			const DirectX::XMFLOAT4X4& view,
			const DirectX::XMFLOAT4X4& projection,
			DirectX::XMFLOAT4X4& output) noexcept
		{
			using namespace DirectX;
			XMStoreFloat4x4(
				&output,
				XMMatrixMultiply(XMLoadFloat4x4(&view), XMLoadFloat4x4(&projection)));
			const float* values = &output._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!std::isfinite(values[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool ExpandProjectionToIncludePane(
			const DirectX::XMFLOAT4X4& reference,
			const DirectX::XMFLOAT4X4& paneFitted,
			DirectX::XMFLOAT4X4& output) noexcept
		{
			struct Slopes
			{
				float left;
				float right;
				float bottom;
				float top;
			};
			const auto decode = [](const DirectX::XMFLOAT4X4& projection,
				Slopes& slopes) noexcept {
				if (!std::isfinite(projection._11) ||
					!std::isfinite(projection._22) ||
					!std::isfinite(projection._31) ||
					!std::isfinite(projection._32) ||
					projection._11 <= 1.0e-6F || projection._22 <= 1.0e-6F) {
					return false;
				}
				slopes = {
					(-1.0F - projection._31) / projection._11,
					(1.0F - projection._31) / projection._11,
					(-1.0F - projection._32) / projection._22,
					(1.0F - projection._32) / projection._22
				};
				return std::isfinite(slopes.left) && std::isfinite(slopes.right) &&
					std::isfinite(slopes.bottom) && std::isfinite(slopes.top) &&
					slopes.left < slopes.right && slopes.bottom < slopes.top;
			};

			Slopes source{};
			Slopes pane{};
			if (!decode(reference, source) || !decode(paneFitted, pane))
				return false;
			const float left = std::min(source.left, pane.left);
			const float right = std::max(source.right, pane.right);
			const float bottom = std::min(source.bottom, pane.bottom);
			const float top = std::max(source.top, pane.top);
			if (!std::isfinite(left) || !std::isfinite(right) ||
				!std::isfinite(bottom) || !std::isfinite(top) ||
				right - left <= 1.0e-6F || top - bottom <= 1.0e-6F) {
				return false;
			}
			output = reference;
			output._11 = 2.0F / (right - left);
			output._22 = 2.0F / (top - bottom);
			output._31 = -(right + left) / (right - left);
			output._32 = -(top + bottom) / (top - bottom);
			const float* values = &output._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!std::isfinite(values[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] int RecordException(unsigned long code) noexcept
		{
			g_lastException.store(code, std::memory_order_relaxed);
			g_counters.exceptions.fetch_add(1, std::memory_order_relaxed);
			if (g_handMirrorCaptureScope)
				g_handMirrorCaptureNativeFaulted = true;
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[nodiscard]] bool ReadCameraUpdateEntrySEH(
			const std::uintptr_t address,
			std::array<std::uint8_t,
				MirrorCameraUpdateRuntimePolicy::kEntryWindow.size()>& output) noexcept
		{
			bool read = false;
			__try {
				if (address) {
					std::memcpy(
						output.data(), reinterpret_cast<const void*>(address),
						output.size());
					read = true;
				}
			} __except (RecordException(GetExceptionCode())) {
				read = false;
			}
			return read;
		}

		[[nodiscard]] UploadPatchResult PatchFlatPrimaryViewData(
			const bool repeatedUpload) noexcept
		{
			auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			if (!shadowState) {
				if (g_override.handCaptureAtBegin || g_handMirrorCaptureScope)
					g_handMirrorCaptureNativeFaulted = true;
				return UploadPatchResult::kRejected;
			}

			auto* bytes = reinterpret_cast<std::byte*>(shadowState);
			const auto& layout = CurrentShadowStateLayout();
			RE::NiPoint3 rawOrigin{};
			DirectX::XMFLOAT4X4 view{};
			DirectX::XMFLOAT4X4 projection{};
			std::memcpy(&rawOrigin, bytes + layout.posAdjust, sizeof(rawOrigin));
			std::memcpy(&view, bytes + layout.ViewMatrix(), sizeof(view));
			std::memcpy(&projection, bytes + layout.ProjectionMatrix(), sizeof(projection));

			// Attribute the block to the armed reflected camera before any write.
			// Third-person rendering can interleave camera variants. The measured
			// delta remains diagnostic while the tight test below owns or rejects
			// each mirror upload.
			bool originMatches = true;
			if (g_override.expectedOriginValid) {
				const float deltaX = rawOrigin.x - g_override.expectedOrigin.x;
				const float deltaY = rawOrigin.y - g_override.expectedOrigin.y;
				const float deltaZ = rawOrigin.z - g_override.expectedOrigin.z;
				const float delta = std::sqrt(
					deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
				if (std::isfinite(delta)) {
					auto storedBits = g_counters.variantUploadDeltaMaxBits.load(
						std::memory_order_relaxed);
					while (delta > std::bit_cast<float>(storedBits) &&
						!g_counters.variantUploadDeltaMaxBits.compare_exchange_weak(
							storedBits, std::bit_cast<std::uint32_t>(delta),
							std::memory_order_relaxed)) {
					}
				}
				if (MainViewVariantGuardPolicy::Mismatch(
						delta, MainViewVariantGuardPolicy::kOriginToleranceUnits)) {
					g_counters.variantMismatchedUploads.fetch_add(
						1, std::memory_order_relaxed);
				}
				originMatches = !MainViewVariantGuardPolicy::Mismatch(
					delta, kPrivateUploadOriginToleranceUnits);
			}
			const bool poseMatches =
				ViewMatrixMatchesCameraPose(view, g_override.expectedPose);
			if (!originMatches || !poseMatches) {
				if (g_override.handCaptureAtBegin && !poseMatches) {
					g_counters.handPoseMismatchSkips.fetch_add(
						1, std::memory_order_relaxed);
				}
				g_counters.unownedUploadSkips.fetch_add(
					1, std::memory_order_relaxed);
				return UploadPatchResult::kNotOwned;
			}

			DirectX::XMFLOAT4X4 patchedProjection{};
			DirectX::XMFLOAT4X4 patchedViewProjection{};
			const DirectX::XMFLOAT3 cameraOrigin{ rawOrigin.x, rawOrigin.y, rawOrigin.z };
			// Pane projection coverage: architectural mirrors retain the authored
			// pane fit. A moving hand mirror instead unions that fit with the frozen
			// source FOV, so it gains off-axis coverage without a telephoto crop.
			DirectX::XMFLOAT4X4 referenceProjection = projection;
			if (g_override.handCaptureAtBegin) {
				if (!g_handMirrorRasterProjectionValid) {
					g_handMirrorCaptureNativeFaulted = true;
					return UploadPatchResult::kRejected;
				}
				referenceProjection = g_handMirrorRasterProjection;
			} else {
				if (!g_override.sourceProjectionValid) {
					g_override.sourceProjection = projection;
					g_override.sourceProjectionValid = true;
				}
				referenceProjection = g_override.sourceProjection;
			}
			DirectX::XMFLOAT4X4 baseProjection = referenceProjection;
			if (g_override.paneFitValid) {
				DirectX::XMFLOAT4X4 fitted{};
				PlanarMirrorMath::PaneFitFailure paneFitFailure{};
				bool cornerClamped = false;
				const float paneFitMarginScale = PaneFitMarginScale();
				bool fitReady = PlanarMirrorMath::BuildPaneFittedProjection(
						g_override.paneFit, cameraOrigin, view, referenceProjection,
						paneFitMarginScale, fitted, &paneFitFailure,
						&cornerClamped);
				if (fitReady && g_override.handCaptureAtBegin) {
					DirectX::XMFLOAT4X4 expanded{};
					fitReady = ExpandProjectionToIncludePane(
						referenceProjection, fitted, expanded);
					if (fitReady)
						fitted = expanded;
					else
						paneFitFailure =
							PlanarMirrorMath::PaneFitFailure::kInvalidOutput;
				}
				if (fitReady) {
					baseProjection = fitted;
					g_counters.paneFitApplied.fetch_add(1, std::memory_order_relaxed);
					if (cornerClamped)
						g_counters.paneFitCornerClamps.fetch_add(
							1, std::memory_order_relaxed);
					if (std::abs(fitted._11) > 1.0e-6f && std::abs(fitted._22) > 1.0e-6f) {
						g_counters.lastFitHalfSlopeXBits.store(
							std::bit_cast<std::uint32_t>(1.0f / fitted._11),
							std::memory_order_relaxed);
						g_counters.lastFitHalfSlopeYBits.store(
							std::bit_cast<std::uint32_t>(1.0f / fitted._22),
							std::memory_order_relaxed);
						if (!repeatedUpload) {
							const auto passSequence =
								SecondView::MirrorPrimaryPassSequence();
							if (g_phasePassSequence != passSequence) {
								g_phasePassSequence = passSequence;
								g_phaseUploadOrdinal = 0;
							}
							const std::uint32_t phaseIndex =
								g_phaseUploadOrdinal < 2 ? g_phaseUploadOrdinal : 1;
							++g_phaseUploadOrdinal;
							g_counters.phaseFitHalfSlopeXBits[phaseIndex].store(
								std::bit_cast<std::uint32_t>(1.0f / fitted._11),
								std::memory_order_relaxed);
							g_counters.phaseOriginXBits[phaseIndex].store(
								std::bit_cast<std::uint32_t>(cameraOrigin.x),
								std::memory_order_relaxed);
							g_counters.phaseOriginYBits[phaseIndex].store(
								std::bit_cast<std::uint32_t>(cameraOrigin.y),
								std::memory_order_relaxed);
							g_counters.phaseOriginZBits[phaseIndex].store(
								std::bit_cast<std::uint32_t>(cameraOrigin.z),
								std::memory_order_relaxed);
						}
					}
				} else {
					g_counters.paneFitFallbacks.fetch_add(
						1, std::memory_order_relaxed);
					RecordPaneFitFailure(paneFitFailure);
				}
			}
			if (g_override.handCaptureAtBegin &&
				g_override.handPhysicalRasterClip) {
				g_override.handPhysicalDepthProjection.valid = false;
				if (!PlanarMirrorMath::BuildObliqueViewProjection(
						g_override.plane, g_override.clipBias, cameraOrigin, view,
						baseProjection, patchedProjection,
						patchedViewProjection)) {
					return UploadPatchResult::kRejected;
				}
				g_override.handPhysicalDepthProjection.conventional =
					baseProjection;
				g_override.handPhysicalDepthProjection.oblique =
					patchedProjection;
				g_override.handPhysicalDepthProjection.valid = true;
				g_counters.handPhysicalObliqueProjectionApplied.fetch_add(
					1, std::memory_order_relaxed);
			} else if (g_override.handCaptureAtBegin) {
				// Vanilla SoftEffect reconstructs scene depth from a conventional
				// perspective projection.  An oblique column-2 rewrite makes the
				// reconstruction screen-position dependent, which detached fire and
				// other depth-faded effects from their world geometry.  The hand path
				// already owns a frozen raster projection; keep it unchanged here.
				patchedProjection = baseProjection;
				if (!MultiplyViewProjection(
						view, patchedProjection, patchedViewProjection))
					return UploadPatchResult::kRejected;
				g_counters.handStandardProjectionApplied.fetch_add(
					1, std::memory_order_relaxed);
			} else {
				g_override.placedDepthProjection.valid = false;
				if (!PlanarMirrorMath::BuildObliqueViewProjection(
						g_override.plane,
						g_override.clipBias,
						cameraOrigin,
						view,
						baseProjection,
						patchedProjection,
						patchedViewProjection)) {
					return UploadPatchResult::kRejected;
				}
				if (g_override.placedDepthProjection.generation != 0) {
					g_override.placedDepthProjection.conventional = baseProjection;
					g_override.placedDepthProjection.oblique = patchedProjection;
					g_override.placedDepthProjection.valid = true;
				}
			}

			// Commit only after both matrices are valid. History and unjittered matrices remain untouched.
			// VR mono capture: every eye slot receives the same patched matrices,
			// and eye slots after 0 additionally take eye 0's basis vectors, view
			// matrix, posAdjust and previousPosAdjust so the instanced right-eye
			// draw renders the identical reflected camera into the shared target.
			for (std::size_t eye = 0; eye < layout.eyeCount; ++eye) {
				std::memcpy(bytes + layout.ProjectionMatrix(eye), &patchedProjection, sizeof(patchedProjection));
				std::memcpy(bytes + layout.ViewProjectionMatrix(eye), &patchedViewProjection, sizeof(patchedViewProjection));
			}
			for (std::size_t eye = 1; eye < layout.eyeCount; ++eye) {
				std::memmove(
					bytes + layout.ViewBasis(eye), bytes + layout.ViewBasis(0),
					kViewBasisAndViewMatrixBytes);
				std::memcpy(
					bytes + layout.PosAdjust(eye), &rawOrigin, sizeof(rawOrigin));
				std::memmove(
					bytes + layout.PreviousPosAdjust(eye),
					bytes + layout.PreviousPosAdjust(0), sizeof(rawOrigin));
			}
			g_override.patchedViewProjection = patchedViewProjection;
			g_override.patchedOrigin = cameraOrigin;
			return UploadPatchResult::kPatched;
		}

		[[nodiscard]] UploadPatchResult PatchFlatPrimaryViewDataSEH(
			const bool repeatedUpload) noexcept
		{
			UploadPatchResult result = UploadPatchResult::kRejected;
			__try {
				result = PatchFlatPrimaryViewData(repeatedUpload);
			} __except (RecordException(GetExceptionCode())) {
				result = UploadPatchResult::kRejected;
			}
			return result;
		}

		[[nodiscard]] bool ReadFlatCameraPoseImpl(
			const RE::NiCamera* camera,
			CameraPose& output) noexcept
		{
			if (!camera)
				return false;
			RE::NiTransform world{};
			const auto* bytes = reinterpret_cast<const std::byte*>(camera);
			std::memcpy(&world, bytes + CurrentShadowStateLayout().cameraWorldTransform, sizeof(world));
			output.origin = { world.translate.x, world.translate.y, world.translate.z };
			output.forward = {
				world.rotate.entry[0][0], world.rotate.entry[1][0], world.rotate.entry[2][0] };
			output.up = {
				world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
			output.right = {
				world.rotate.entry[0][2], world.rotate.entry[1][2], world.rotate.entry[2][2] };
			return true;
		}

		[[nodiscard]] bool ReadFlatCameraPoseSEH(
			const RE::NiCamera* camera,
			CameraPose& output) noexcept
		{
			bool result = false;
			__try {
				result = ReadFlatCameraPoseImpl(camera, output);
			} __except (RecordException(GetExceptionCode())) {
				// The equipped-mirror route reads this value first at the exact render fence,
				// before its scoped camera override begins.  Preserve a native read
				// fault across that boundary so it cannot be reclassified as ordinary
				// geometric unavailability.
				if (g_handMirrorRuntimeEnabled.load(std::memory_order_acquire))
					g_handMirrorCaptureNativeFaulted = true;
				result = false;
			}
			return result;
		}

		struct UpdateCameraDataHook
		{
			// Raw ABI verified at the SE call site and matched on AE. Do not replace this with CS's void() helper.
			static void thunk(void* uploadContext, std::uint32_t cameraFlags)
			{
                if (!MirrorSunShadows::OnCameraUpload())
                    RaiseException(0xE04D5355, EXCEPTION_NONCONTINUABLE, 0, nullptr);
				g_counters.hookCalls.fetch_add(1, std::memory_order_relaxed);
				if (g_override.phase == OverridePhase::kPatching) {
					// The outer hook already committed the validated shadow-state edit.
					// Never recurse into a second edit while its trampoline is active.
					g_override.reentrantUploadSeen = true;
					g_counters.reentrantUploadRejects.fetch_add(
						1, std::memory_order_relaxed);
					func(uploadContext, cameraFlags);
					return;
				}

				const bool initialUpload =
					MirrorCameraUploadLeasePolicy::IsInitialAttempt(g_override.phase);
				const bool repeatedUpload =
					MirrorCameraUploadLeasePolicy::IsRepeatedAttempt(
						g_override.phase, true);
				if (!initialUpload && !repeatedUpload) {
					func(uploadContext, cameraFlags);
					return;
				}

				if (g_override.handCaptureAtBegin &&
					(!g_handMirrorCaptureScope || !g_handMirrorRasterProjectionValid ||
					 !g_handMirrorRuntimeEnabled.load(std::memory_order_acquire) ||
					 !g_hookInstalled.load(std::memory_order_acquire))) {
					g_handMirrorCaptureNativeFaulted = true;
					g_override.phase = OverridePhase::kFailed;
					g_counters.failedUploads.fetch_add(1, std::memory_order_relaxed);
					func(uploadContext, cameraFlags);
					return;
				}

				const bool ownedCameraFlags = initialUpload ?
					MirrorCameraUploadLeasePolicy::IsInitialPrivateUpload(cameraFlags) :
					MirrorCameraUploadLeasePolicy::IsPrivateUpload(cameraFlags);
				if (!ownedCameraFlags) {
					// Flags 1 is the known foreign PreResolve tail. Once a mirror upload
					// has been proven, pass such calls through and keep listening for the
					// later private flags 8/12 PostResolve uploads.
					if (initialUpload) {
						g_counters.unexpectedFlags.fetch_add(1, std::memory_order_relaxed);
						g_counters.failedUploads.fetch_add(1, std::memory_order_relaxed);
						g_override.phase = OverridePhase::kFailed;
						if (g_override.handCaptureAtBegin)
							g_handMirrorCaptureNativeFaulted = true;
					}
					func(uploadContext, cameraFlags);
					return;
				}

				if (!IsSupportedFlatRuntime()) {
					g_override.phase = OverridePhase::kFailed;
					g_counters.failedUploads.fetch_add(1, std::memory_order_relaxed);
					if (g_override.handCaptureAtBegin)
						g_handMirrorCaptureNativeFaulted = true;
					func(uploadContext, cameraFlags);
					return;
				}

				if (repeatedUpload) {
					g_counters.repeatedUploadAttempts.fetch_add(
						1, std::memory_order_relaxed);
				}
				g_override.phase = OverridePhase::kPatching;
				g_override.reentrantUploadSeen = false;
				const auto patchResult = PatchFlatPrimaryViewDataSEH(repeatedUpload);
				if (repeatedUpload &&
					patchResult == UploadPatchResult::kNotOwned) {
					g_counters.repeatedIdentityRejects.fetch_add(
						1, std::memory_order_relaxed);
				}

				// Hold kPatching through the original upload. If it re-enters this hook,
				// the nested call chains without touching the leased projection.
				func(uploadContext, cameraFlags);
				if (g_override.phase == OverridePhase::kPatching) {
					const OverridePhase completedPhase =
						MirrorCameraUploadLeasePolicy::CompleteAttempt(
							patchResult, g_override.reentrantUploadSeen);
					g_override.phase = completedPhase;
					if (completedPhase == OverridePhase::kPatched) {
						g_counters.patchedUploads.fetch_add(1, std::memory_order_relaxed);
						if (repeatedUpload) {
							g_counters.repeatedPatchedUploads.fetch_add(
								1, std::memory_order_relaxed);
						}
					} else {
						g_counters.failedUploads.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}

			static inline REL::Relocation<decltype(thunk)> func{};
		};

		// AE 1.7.104 BSSkyShader::SetupGeometry (107403, RVA 0x15525B0) and SE
		// 1.5.97 (100640, RVA 0x12FABB0)
		// copy the geometry transform, then at 0x15526F2 / 0x12FACF2 use accumulator+0x128
		// to select its existing origin correction:
		//   sky translation - WorldRoot camera translation + accumulator camera translation.
		// Without that branch the ~885-unit sky dome stays around the main eye,
		// while the landscape and shader view use our reflected eye. Do not set
		// firstPerson for a whole capture: lighting/pass construction also reads it.
		// This lease is confined to this sky constant-setup call; geometry, culling,
		// the main camera, native cloud animation and star rotation are untouched.
		[[nodiscard]] __declspec(noinline) bool* ReadSkyRecenterFlagRaw()
		{
			using Getter = RE::BSShaderAccumulator*();
			static REL::Relocation<Getter> getter{ RELOCATION_ID(98997, 105651) };
			auto* accumulator = getter();
			if (accumulator && accumulator->camera == g_override.camera &&
				accumulator->GetRuntimeData() &&
				!accumulator->GetRuntimeData()->firstPerson)
				return &accumulator->GetRuntimeData()->firstPerson;
			return nullptr;
		}

		[[nodiscard]] bool* SkyRecenterFlag() noexcept
		{
			if (!g_skyRecenterInstalled.load(std::memory_order_acquire) ||
				g_override.phase != OverridePhase::kPatched || !g_override.camera ||
				!SecondView::IsInsidePrivateCapture())
				return nullptr;
			__try {
				if (auto* flag = ReadSkyRecenterFlagRaw())
					return flag;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			g_skyRecenterRejects.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}

		// The correction's third term reads the accumulator camera *node's* world
		// translation (`*(float *)(accumulator[0x10] + 0xa0)`, Ghidra Combined,
		// SE 1.5.97 RVA 0x12FABB0). A private capture borrows the engine's camera
		// node and patches only what is uploaded from it, so that translation is
		// still the main eye and the correction cancels itself out:
		// `sky - mainCamera + mainCamera`. The sky then stays on the main eye,
		// which from the reflected eye is an offset that changes as the player
		// walks -- the parallax the owner reported on 2026-09-15.
		//
		// NiAVObject::world.translate sits at +0xA0 on both supported runtimes;
		// the decompiled branch reads that exact offset on each of the three
		// vectors it touches, which is what the contract check above pins.
		inline constexpr std::ptrdiff_t kCameraWorldTranslateOffset = 0xA0;

		struct SkyCameraTranslationLease
		{
			float* translation{ nullptr };
			float saved[3]{};

			[[nodiscard]] bool Begin(const RE::NiCamera* camera, const DirectX::XMFLOAT3& origin) noexcept
			{
				if (!camera) return false;
				__try {
					auto* const target = reinterpret_cast<float*>(
						const_cast<std::byte*>(
							reinterpret_cast<const std::byte*>(camera)) +
						kCameraWorldTranslateOffset);
					for (int axis = 0; axis < 3; ++axis) {
						if (!std::isfinite(target[axis])) return false;
						saved[axis] = target[axis];
					}
					target[0] = origin.x;
					target[1] = origin.y;
					target[2] = origin.z;
					translation = target;
					return true;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					translation = nullptr;
					return false;
				}
			}

			void End() noexcept
			{
				if (!translation) return;
				__try {
					translation[0] = saved[0];
					translation[1] = saved[1];
					translation[2] = saved[2];
				} __except (EXCEPTION_EXECUTE_HANDLER) {
				}
				translation = nullptr;
			}
		};

		struct SkySetupHook
		{
			static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, std::uint32_t flags)
			{
				bool* const firstPerson = SkyRecenterFlag();
				if (!firstPerson) {
					func(shader, pass, flags);
					return;
				}
				// Lend the camera node the reflected origin for exactly this call.
				// posAdjust already carries it -- the upload patch proves every
				// block against expectedOrigin -- and without it the correction
				// below cancels itself out. A refused lease still recenters
				// nothing, which is the behaviour that shipped, never a fault.
				SkyCameraTranslationLease lease{};
				const bool lent = g_override.expectedOriginValid &&
					lease.Begin(g_override.camera, g_override.expectedOrigin);
				if (lent)
					g_skyCameraTranslationLeases.fetch_add(1, std::memory_order_relaxed);
				else
					g_skyCameraTranslationRefusals.fetch_add(1, std::memory_order_relaxed);
				__try {
					*firstPerson = true;
					func(shader, pass, flags);
					g_skyRecenters.fetch_add(1, std::memory_order_relaxed);
				} __finally {
					*firstPerson = false;
					lease.End();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func{};
		};

		// Per-runtime exact contract for the sky origin-correction branch. SE
		// 1.5.97 BSSkyShader::SetupGeometry (100640, RVA 0x12FABB0) carries the
		// byte-identical entry and `cmp byte ptr [rax+0x128], dil; jz` branch at
		// 0x12FACF2 (Ghidra Combined oracle, 2026-09-13); only the accumulator
		// getter's rel32 differs. The runtime firstPerson offset is 0x128 on both.
		struct SkyRecenterContract
		{
			const char* runtime;
			std::uint64_t setupID, getterID, vtableID;
			std::uintptr_t setupRVA, getterRVA, vtableRVA, branchRVA;
			std::uint8_t getEntry[8];
		};
		inline constexpr SkyRecenterContract kSkyRecenterAE17104{
			"AE17104", 107403, 105651, 255149, 0x15525B0, 0x14ECB90, 0x1B48078, 0x15526F2,
			{ 0x48, 0x8B, 0x05, 0x11, 0x72, 0xEE, 0x01, 0xC3 } };
		// Steam 1.6.1170 and GOG 1.6.1179 keep the AE IDs. The unique
		// SetupGeometry entry and +0x142 firstPerson branch were read from
		// those two executables; only the RVAs and getter rel32 move.
		inline constexpr SkyRecenterContract kSkyRecenterAE161170{
			"AE161170", 107403, 105651, 255149, 0x14E6030, 0x1480B20, 0x1ABF118, 0x14E6172,
			{ 0x48, 0x8B, 0x05, 0x81, 0x98, 0xEA, 0x01, 0xC3 } };
		inline constexpr SkyRecenterContract kSkyRecenterAE161179{
			"AE161179", 107403, 105651, 255149, 0x14E70A0, 0x1481BC0, 0x1AC00F8, 0x14E71E2,
			{ 0x48, 0x8B, 0x05, 0xB1, 0x9B, 0xEA, 0x01, 0xC3 } };
		inline constexpr SkyRecenterContract kSkyRecenterSE1597{
			"SE1597", 100640, 98997, 305390, 0x12FABB0, 0x12966A0, 0x1865C70, 0x12FACF2,
			{ 0x48, 0x8B, 0x05, 0x79, 0xA7, 0xF3, 0x01, 0xC3 } };

		[[nodiscard]] const SkyRecenterContract* SelectSkyRecenterContract() noexcept
		{
			if (SupportedRuntimePolicy::IsExactAE17104Runtime())
				return &kSkyRecenterAE17104;
			if (SupportedRuntimePolicy::IsExactAE161170Runtime())
				return &kSkyRecenterAE161170;
			if (SupportedRuntimePolicy::IsExactAE161179Runtime())
				return &kSkyRecenterAE161179;
			if (REL::Module::IsSE() && REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 })
				return &kSkyRecenterSE1597;
			return nullptr;
		}

		// Community Shaders re-vectors BSSkyShader's vtable slot 6 into its own
		// module (run 8, 2026-09-14: slot -> CommunityShaders.dll). Its thunk calls
		// the saved native SetupGeometry, which our inline detour still covers, so
		// under CS a slot pointing outside the game's text section is accepted;
		// the native entry/branch/getter bytes stay exact in every case.
		enum class SkyRecenterSlot { kNative, kForeignCS, kMismatch };
		[[nodiscard]] SkyRecenterSlot ClassifySkyRecenterSlot(std::uintptr_t slot, std::uintptr_t setup) noexcept
		{
			if (slot == setup)
				return SkyRecenterSlot::kNative;
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			const bool foreign = slot < text.address() || slot >= text.address() + text.size();
			return foreign && PeerDetection::CommunityShadersPresent() ? SkyRecenterSlot::kForeignCS : SkyRecenterSlot::kMismatch;
		}

		[[nodiscard]] const char* SkyRecenterSlotName(SkyRecenterSlot slot) noexcept
		{
			switch (slot) {
			case SkyRecenterSlot::kNative: return "native";
			case SkyRecenterSlot::kForeignCS: return "foreign-cs";
			default: return "mismatch";
			}
		}

		[[nodiscard]] bool VerifySkyRecenterContract(const SkyRecenterContract& c, SkyRecenterSlot& slotOut) noexcept
		{
			slotOut = SkyRecenterSlot::kMismatch;
			__try {
				const auto base = REL::Module::get().base();
				const auto setup = REL::ID(c.setupID).address();
				const auto getter = REL::ID(c.getterID).address();
				const auto vtable = REL::ID(c.vtableID).address();
				slotOut = ClassifySkyRecenterSlot(*reinterpret_cast<const std::uintptr_t*>(vtable + 6 * sizeof(void*)), setup);
				constexpr std::uint8_t entry[]{
					0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x55,
					0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };
				constexpr std::uint8_t branch[]{
					0x40, 0x38, 0xB8, 0x28, 0x01, 0x00, 0x00,
					0x74, 0x6D };
				return setup == base + c.setupRVA && getter == base + c.getterRVA &&
					vtable == base + c.vtableRVA &&
					slotOut != SkyRecenterSlot::kMismatch &&
					std::memcmp(reinterpret_cast<const void*>(setup), entry, sizeof(entry)) == 0 &&
					std::memcmp(reinterpret_cast<const void*>(base + c.branchRVA), branch, sizeof(branch)) == 0 &&
					std::memcmp(reinterpret_cast<const void*>(getter), c.getEntry, sizeof(c.getEntry)) == 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void InstallSkyRecenter()
		{
			if (!g_skyRecenterRequested.load(std::memory_order_acquire))
				return;
			const auto* contract = SelectSkyRecenterContract();
			auto slot = SkyRecenterSlot::kMismatch;
			const bool installed = contract && VerifySkyRecenterContract(*contract, slot) &&
				stl::detour_thunk<SkySetupHook>(RELOCATION_ID(100640, 107403));
			g_skyRecenterInstalled.store(installed, std::memory_order_release);
			logger::info("[MOS][SkyCamera] requested=true installed={} exactRuntime={} vtableSlot={} nativeSkyOriginCorrection=true",
				installed, contract ? contract->runtime : "unsupported", SkyRecenterSlotName(slot));
		}
	}

	void SetSkyRecenterRequested(bool requested) noexcept
	{
		g_skyRecenterRequested.store(requested, std::memory_order_release);
	}

	bool IsSupportedFlatRuntime() noexcept
	{
		const auto version = REL::Module::get().version();
		return (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
		       (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
		       SupportedRuntimePolicy::IsExactVRRuntime();
	}

	bool IsEnabled() noexcept
	{
		return g_runtimeEnabled.load(std::memory_order_acquire);
	}

	bool HookReady() noexcept
	{
		return g_hookInstalled.load(std::memory_order_acquire);
	}

	bool Requested() noexcept
	{
		return g_wallMirrorRequested.load(std::memory_order_acquire) ||
		       g_handMirrorHookRequested.load(std::memory_order_acquire);
	}

	void SetHandMirrorRuntimeEnabled(const bool enabled) noexcept
	{
		const bool ready = enabled && HookReady() &&
			g_handMirrorHookRequested.load(std::memory_order_acquire);
		const bool wasEnabled = g_handMirrorRuntimeEnabled.exchange(
			ready, std::memory_order_acq_rel);
		if (ready && !wasEnabled)
			g_handMirrorCaptureNativeFaulted = false;
		if (!ready) {
			g_handMirrorCaptureScope = false;
			g_handMirrorRasterProjection = {};
			g_handMirrorRasterProjectionValid = false;
		}
	}

	bool HandMirrorRuntimeReady() noexcept
	{
		return HookReady() &&
			g_handMirrorRuntimeEnabled.load(std::memory_order_acquire);
	}

	bool BeginHandMirrorCaptureScope(
		const DirectX::XMFLOAT4X4& frozenRasterProjection) noexcept
	{
		if (!HandMirrorRuntimeReady() || g_handMirrorCaptureScope) {
			if (g_handMirrorCaptureScope)
				g_handMirrorCaptureNativeFaulted = true;
			return false;
		}
		const float* projectionValues = &frozenRasterProjection._11;
		for (std::size_t index = 0; index < 16; ++index) {
			if (!std::isfinite(projectionValues[index])) {
				g_handMirrorCaptureNativeFaulted = true;
				return false;
			}
		}
		g_handMirrorRasterProjection = frozenRasterProjection;
		g_handMirrorRasterProjectionValid = true;
		g_handMirrorCaptureScope = true;
		return true;
	}

	bool HandMirrorCaptureProjectionMatches(
		const DirectX::XMFLOAT4X4& candidate) noexcept
	{
		if (!g_handMirrorCaptureScope || !g_handMirrorRasterProjectionValid)
			return false;
		const float* candidateValues = &candidate._11;
		for (std::size_t index = 0; index < 16; ++index) {
			if (!std::isfinite(candidateValues[index]))
				return false;
		}
		// The world and explicit-player cycles preserve one depth buffer. Require
		// the restored portrait camera to reproduce the exact projection bits before
		// the player cycle can be armed; a near/far divergence cleanly rejects the
		// frame instead of drawing the body through retained world depth.
		return std::memcmp(
			&candidate, &g_handMirrorRasterProjection, sizeof(candidate)) == 0;
	}

	void EndHandMirrorCaptureScope() noexcept
	{
		g_handMirrorCaptureScope = false;
		g_handMirrorRasterProjection = {};
		g_handMirrorRasterProjectionValid = false;
	}

	bool HandMirrorCaptureNativeFaulted() noexcept
	{
		return g_handMirrorCaptureNativeFaulted;
	}

	float PaneFitMarginScale() noexcept
	{
		return kPaneFitMarginScale;
	}

	bool ReadFlatCameraPose(const RE::NiCamera* camera, CameraPose& output) noexcept
	{
		output = {};
		return IsSupportedFlatRuntime() && ReadFlatCameraPoseSEH(camera, output);
	}

	void OnInputLoaded(
		bool secondViewHooksReady,
		bool wallMirrorRequested,
		bool handMirrorRequested)
	{
		g_wallMirrorRequested.store(wallMirrorRequested, std::memory_order_release);
		g_handMirrorHookRequested.store(handMirrorRequested, std::memory_order_release);
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		if (!IsSupportedFlatRuntime()) {
			logger::warn(
				"[MirrorsOfSkyrim][CameraOverride] unavailable outside exact SE 1.5.97 / AE 1.6.1170 / VR 1.4.15 gate");
			return;
		}
		if (!wallMirrorRequested && !handMirrorRequested) {
			logger::info(
				"[MirrorsOfSkyrim][CameraOverride] no mirror role requested the upload hook");
			return;
		}
		if (!secondViewHooksReady) {
			logger::warn(
				"[MirrorsOfSkyrim][CameraOverride] second-view hooks are not ready; upload hook not installed");
			return;
		}

		const bool exactSE1597 = REL::Module::IsSE() &&
			REL::Module::get().version() == REL::Version{ 1, 5, 97, 0 };
		const bool exactVR1415 = SupportedRuntimePolicy::IsExactVRRuntime();
		// 1.7.104 keeps ID 77258 and the AE entry window but moves the target to
		// 0x100AE80, so it selects its own contract instead of the AE one.
		const bool exactAE17104 = SupportedRuntimePolicy::IsExactAE17104Runtime();
		const bool exactAE161179 = SupportedRuntimePolicy::IsExactAE161179Runtime();
		const auto cameraRuntime = exactSE1597 ?
			MirrorCameraUpdateRuntimePolicy::Runtime::kSkyrimSE1597 :
			exactVR1415 ?
			MirrorCameraUpdateRuntimePolicy::Runtime::kSkyrimVR1415 :
			exactAE17104 ?
			MirrorCameraUpdateRuntimePolicy::Runtime::kSkyrimAE17104 :
			exactAE161179 ?
			MirrorCameraUpdateRuntimePolicy::Runtime::kSkyrimAE161179 :
			MirrorCameraUpdateRuntimePolicy::Runtime::kSkyrimAE161170;
		const auto* cameraContract =
			MirrorCameraUpdateRuntimePolicy::Select(cameraRuntime);
		// Flat and VR entry windows are both 36 bytes; MatchesEntryFor selects
		// the runtime's masked pattern.
		static_assert(MirrorCameraUpdateRuntimePolicy::kEntryWindow.size() ==
			MirrorCameraUpdateRuntimePolicy::kVREntryWindow.size());
		std::array<std::uint8_t,
			MirrorCameraUpdateRuntimePolicy::kEntryWindow.size()> cameraEntry{};
		// 75472 is in the VR address library (0xDBCE30), so REL::ID is exact on
		// every supported runtime.
		const auto cameraTarget = cameraContract ?
			REL::ID(cameraContract->updateCameraDataID).address() : 0;
		if (!cameraContract ||
			cameraTarget != REL::Module::get().base() +
				cameraContract->updateCameraDataRVA ||
			!ReadCameraUpdateEntrySEH(cameraTarget, cameraEntry) ||
			!MirrorCameraUpdateRuntimePolicy::MatchesEntryFor(
				cameraRuntime, cameraEntry)) {
			logger::error(
				"[MirrorsOfSkyrim][CameraOverride] exact-runtime UpdateCameraData ID/RVA/ABI signature mismatch");
			return;
		}
		const REL::RelocationID cameraRelocation{
			cameraContract->updateCameraDataID,
			cameraContract->updateCameraDataID };
		if (!stl::detour_thunk<UpdateCameraDataHook>(cameraRelocation)) {
			logger::error(
				"[MirrorsOfSkyrim][CameraOverride] failed to chain UpdateCameraData detour");
			return;
		}
		g_hookInstalled.store(true, std::memory_order_release);
		InstallSkyRecenter();
		logger::info(
			"[MirrorsOfSkyrim][CameraOverride] UpdateCameraData hook prepared (wallRequested={} handRequested={})",
			wallMirrorRequested, handMirrorRequested);
	}

	void OnActivationCommitted(bool wallMirrorEnabled) noexcept
	{
		if (!wallMirrorEnabled)
			return;
		if (!g_wallMirrorRequested.load(std::memory_order_acquire)) {
			logger::critical(
				"[MirrorsOfSkyrim][CameraOverride] wall-mirror activation committed without a prepared request");
			return;
		}
		if (!HookReady()) {
			logger::critical(
				"[MirrorsOfSkyrim][CameraOverride] committed wall-mirror activation has no prepared hook");
			return;
		}
		if (!g_runtimeEnabled.exchange(true, std::memory_order_acq_rel)) {
			logger::info(
				"[MirrorsOfSkyrim][CameraOverride] wall-mirror camera path activated");
		}
	}

	bool Begin(
		const RE::NiCamera* reflectedCamera,
		const PlanarMirrorMath::Plane& mirrorPlane,
		float clipBias,
		const PlanarMirrorMath::PaneFit* paneFit,
		bool handPhysicalRasterClip) noexcept
	{
		if ((!g_runtimeEnabled.load(std::memory_order_acquire) &&
			 !g_handMirrorCaptureScope) || !IsSupportedFlatRuntime() ||
			!reflectedCamera || !std::isfinite(clipBias) || clipBias < 0.0f)
			return false;
		if (g_override.phase != OverridePhase::kInactive) {
			g_counters.nestedBeginRejects.fetch_add(1, std::memory_order_relaxed);
			if (g_handMirrorCaptureScope)
				g_handMirrorCaptureNativeFaulted = true;
			return false;
		}
		if (handPhysicalRasterClip &&
			(!g_handMirrorCaptureScope || clipBias != 0.0F)) {
			if (g_handMirrorCaptureScope)
				g_handMirrorCaptureNativeFaulted = true;
			return false;
		}

		PlanarMirrorMath::Plane normalizedPlane = mirrorPlane;
		if (!PlanarMirrorMath::NormalizePlane(normalizedPlane))
			return false;
		CameraPose reflectedPose{};
		const bool poseReady = ReadFlatCameraPose(reflectedCamera, reflectedPose);
		if (!poseReady || !IsCameraBehindMirrorPlane(normalizedPlane, reflectedPose.origin)) {
			g_counters.cameraSideRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		g_override.camera = reflectedCamera;
		g_override.plane = normalizedPlane;
		g_override.clipBias = clipBias;
		g_override.handCaptureAtBegin = g_handMirrorCaptureScope;
		g_override.handPhysicalRasterClip = handPhysicalRasterClip;
		if (handPhysicalRasterClip &&
			!AllocateHandPhysicalDepthGeneration(
				g_override.handPhysicalDepthProjection.generation)) {
			g_override = {};
			g_handMirrorCaptureNativeFaulted = true;
			return false;
		}
		// A placed lease without a generation simply offers no soft depth; the
		// capture itself does not depend on it.
		if (!g_override.handCaptureAtBegin)
			(void)AllocateHandPhysicalDepthGeneration(
				g_override.placedDepthProjection.generation);
		g_override.expectedOrigin = reflectedPose.origin;
		g_override.expectedPose = reflectedPose;
		g_override.expectedOriginValid = true;
		if (paneFit) {
			g_override.paneFit = *paneFit;
			g_override.paneFitValid = true;
		} else {
			g_override.paneFit = {};
			g_override.paneFitValid = false;
		}
		g_override.phase = OverridePhase::kArmed;
		g_counters.arms.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void End(const RE::NiCamera* reflectedCamera) noexcept
	{
		if (g_override.phase == OverridePhase::kInactive)
			return;
		if (reflectedCamera && reflectedCamera != g_override.camera) {
			if (g_handMirrorCaptureScope)
				g_handMirrorCaptureNativeFaulted = true;
			return;
		}
		g_override = {};
	}

	bool Active(const RE::NiCamera* reflectedCamera) noexcept
	{
		return g_override.phase != OverridePhase::kInactive &&
		       (!reflectedCamera || reflectedCamera == g_override.camera);
	}

	bool GetPatchedViewProjection(
		const RE::NiCamera* reflectedCamera,
		DirectX::XMFLOAT4X4& output) noexcept
	{
		if (g_override.phase != OverridePhase::kPatched ||
			(reflectedCamera && reflectedCamera != g_override.camera))
			return false;
		output = g_override.patchedViewProjection;
		return true;
	}

	bool GetPatchedFrameData(
		const RE::NiCamera* reflectedCamera,
		DirectX::XMFLOAT4X4& viewProjection,
		DirectX::XMFLOAT3& origin) noexcept
	{
		if (g_override.phase != OverridePhase::kPatched ||
			(reflectedCamera && reflectedCamera != g_override.camera))
			return false;
		viewProjection = g_override.patchedViewProjection;
		origin = g_override.patchedOrigin;
		return true;
	}

	bool GetPlacedDepthProjection(
		const RE::NiCamera* reflectedCamera,
		HandPhysicalDepthProjection& output) noexcept
	{
		output = {};
		if (g_override.phase != OverridePhase::kPatched ||
			g_override.handCaptureAtBegin ||
			!g_override.placedDepthProjection.valid ||
			g_override.placedDepthProjection.generation == 0 ||
			(reflectedCamera && reflectedCamera != g_override.camera)) {
			return false;
		}
		output = g_override.placedDepthProjection;
		return true;
	}

	bool GetHandPhysicalDepthProjection(
		const RE::NiCamera* reflectedCamera,
		HandPhysicalDepthProjection& output) noexcept
	{
		output = {};
		if (g_override.phase != OverridePhase::kPatched ||
			!g_override.handCaptureAtBegin ||
			!g_override.handPhysicalRasterClip ||
			!g_override.handPhysicalDepthProjection.valid ||
			(reflectedCamera && reflectedCamera != g_override.camera)) {
			return false;
		}
		output = g_override.handPhysicalDepthProjection;
		return true;
	}

	Diagnostics GetDiagnostics() noexcept
	{
		return {
			.arms = g_counters.arms.load(std::memory_order_relaxed),
			.hookCalls = g_counters.hookCalls.load(std::memory_order_relaxed),
			.patchedUploads = g_counters.patchedUploads.load(std::memory_order_relaxed),
			.failedUploads = g_counters.failedUploads.load(std::memory_order_relaxed),
			.paneFitApplied = g_counters.paneFitApplied.load(std::memory_order_relaxed),
			.paneFitFallbacks = g_counters.paneFitFallbacks.load(std::memory_order_relaxed),
			.paneFitInvalidInputs =
				g_counters.paneFitInvalidInputs.load(std::memory_order_relaxed),
			.paneFitInvalidReferenceProjections =
				g_counters.paneFitInvalidReferenceProjections.load(
					std::memory_order_relaxed),
			.paneFitNotFullyInFront =
				g_counters.paneFitNotFullyInFront.load(std::memory_order_relaxed),
			.paneFitInvalidSlopes =
				g_counters.paneFitInvalidSlopes.load(std::memory_order_relaxed),
			.paneFitInvalidOutputs =
				g_counters.paneFitInvalidOutputs.load(std::memory_order_relaxed),
			.paneFitCornerClamps =
				g_counters.paneFitCornerClamps.load(std::memory_order_relaxed),
			.variantMismatchedUploads =
				g_counters.variantMismatchedUploads.load(std::memory_order_relaxed),
			.handPoseMismatchSkips =
				g_counters.handPoseMismatchSkips.load(std::memory_order_relaxed),
			.handStandardProjectionApplied =
				g_counters.handStandardProjectionApplied.load(
					std::memory_order_relaxed),
			.handPhysicalObliqueProjectionApplied =
				g_counters.handPhysicalObliqueProjectionApplied.load(
					std::memory_order_relaxed),
			.repeatedUploadAttempts =
				g_counters.repeatedUploadAttempts.load(std::memory_order_relaxed),
			.repeatedPatchedUploads =
				g_counters.repeatedPatchedUploads.load(std::memory_order_relaxed),
			.unownedUploadSkips =
				g_counters.unownedUploadSkips.load(std::memory_order_relaxed),
			.repeatedIdentityRejects =
				g_counters.repeatedIdentityRejects.load(std::memory_order_relaxed),
			.reentrantUploadRejects =
				g_counters.reentrantUploadRejects.load(std::memory_order_relaxed),
			.variantUploadDeltaMax = std::bit_cast<float>(
				g_counters.variantUploadDeltaMaxBits.load(std::memory_order_relaxed)),
			.lastFitHalfSlopeX = std::bit_cast<float>(
				g_counters.lastFitHalfSlopeXBits.load(std::memory_order_relaxed)),
			.lastFitHalfSlopeY = std::bit_cast<float>(
				g_counters.lastFitHalfSlopeYBits.load(std::memory_order_relaxed)),
			.phaseFitHalfSlopeX = {
				std::bit_cast<float>(
					g_counters.phaseFitHalfSlopeXBits[0].load(std::memory_order_relaxed)),
				std::bit_cast<float>(
					g_counters.phaseFitHalfSlopeXBits[1].load(std::memory_order_relaxed)) },
			.phaseOriginX = {
				std::bit_cast<float>(
					g_counters.phaseOriginXBits[0].load(std::memory_order_relaxed)),
				std::bit_cast<float>(
					g_counters.phaseOriginXBits[1].load(std::memory_order_relaxed)) },
			.phaseOriginY = {
				std::bit_cast<float>(
					g_counters.phaseOriginYBits[0].load(std::memory_order_relaxed)),
				std::bit_cast<float>(
					g_counters.phaseOriginYBits[1].load(std::memory_order_relaxed)) },
			.phaseOriginZ = {
				std::bit_cast<float>(
					g_counters.phaseOriginZBits[0].load(std::memory_order_relaxed)),
				std::bit_cast<float>(
					g_counters.phaseOriginZBits[1].load(std::memory_order_relaxed)) },
			.unexpectedFlags = g_counters.unexpectedFlags.load(std::memory_order_relaxed),
			.nestedBeginRejects = g_counters.nestedBeginRejects.load(std::memory_order_relaxed),
			.cameraSideRejects = g_counters.cameraSideRejects.load(std::memory_order_relaxed),
			.exceptions = g_counters.exceptions.load(std::memory_order_relaxed),
			.skyRecenters = g_skyRecenters.load(std::memory_order_relaxed),
			.skyRecenterRejects = g_skyRecenterRejects.load(std::memory_order_relaxed),
			.skyCameraTranslationLeases =
				g_skyCameraTranslationLeases.load(std::memory_order_relaxed),
			.skyCameraTranslationRefusals =
				g_skyCameraTranslationRefusals.load(std::memory_order_relaxed),
			.skyRecenterInstalled = g_skyRecenterInstalled.load(std::memory_order_acquire),
			.lastException = static_cast<std::uint32_t>(
				g_lastException.load(std::memory_order_relaxed)),
			.hookInstalled = g_hookInstalled.load(std::memory_order_acquire),
			.paneFitMarginScale = PaneFitMarginScale(),
			.active = g_override.phase != OverridePhase::kInactive,
			.projectionPatched = g_override.phase == OverridePhase::kPatched
		};
	}
}
