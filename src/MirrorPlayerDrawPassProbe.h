#pragma once

#include <cstdint>

namespace RE
{
	class BSRenderPass;
	class NiAVObject;
}

struct ID3D11RenderTargetView;

namespace PlayerDrawPassProbe
{
	enum class CaptureKind : std::uint8_t
	{
		kMirror = 0,
		// Preserve the established value while exposing only the equipped role.
		kHandMirror = 5
	};

	struct Result
	{
		std::uint64_t lightingPostDrawCalls{ 0 };
		std::uint64_t lightingPostDrawReturns{ 0 };
		std::uint64_t targetMatchedCalls{ 0 };
		std::uint64_t targetMismatches{ 0 };
		std::uint64_t playerPostDrawCalls{ 0 };
		std::uint64_t playerPostDrawReturns{ 0 };
		std::uint64_t skinnedPlayerPostDrawCalls{ 0 };
		std::uint64_t skinnedPlayerPostDrawReturns{ 0 };
		std::uint64_t truncatedParentWalks{ 0 };
		std::uint64_t malformedParentWalks{ 0 };
		std::uint64_t classificationFaults{ 0 };
		std::uint64_t targetQueryFaults{ 0 };
		// End owns the independently retained player root and private RTV through
		// the last native draw. A guarded DecRef/Release failure is evidence about
		// this frame, not merely a reason to reject the next Begin.
		bool lifetimeCleanupFault{ false };
		bool valid{ false };
	};

	enum class ExactHandDrawClassification : std::uint8_t
	{
		kInactive,
		kNonPlayer,
		kPlayer,
		kFault
	};

	/**
	 * Apply the exact End-time root/RTV release outcome before returning evidence.
	 * Kept constexpr so the fault-injection regression exercises the same rule as
	 * the native adapter without constructing engine or D3D objects.
	 */
	[[nodiscard]] constexpr Result ApplyOwnedReferenceReleaseOutcome(
		Result result,
		bool targetReleased,
		bool rootReleased) noexcept
	{
		if (!targetReleased || !rootReleased) {
			result.lifetimeCleanupFault = true;
			result.valid = false;
		}
		return result;
	}

	/** A hand frame may not survive any guarded probe/native cleanup fault. */
	[[nodiscard]] constexpr bool CurrentFrameNativeFaulted(
		const Result& result,
		CaptureKind kind) noexcept
	{
		return kind == CaptureKind::kHandMirror &&
		       (result.lifetimeCleanupFault || result.classificationFaults != 0 ||
			   result.targetQueryFaults != 0);
	}

	/** POD session: consumers explicitly End it inside their existing cleanup boundaries. */
	struct Session
	{
		Result result{};
		std::uint64_t token{ 0 };
		std::uint32_t threadID{ 0 };
		bool active{ false };
	};

	/** Install the verified BSLightingShader post-draw vfunc observer once. Call only behind an existing default-off marker. */
	[[nodiscard]] bool EnsureInstalled() noexcept;
	[[nodiscard]] bool IsInstalled() noexcept;

	/**
	 * Begin observing draws whose geometry is below the supplied live player root.
	 * The probe takes independent root/RTV references until a matching End.  An
	 * End identity mismatch fail-stops the probe and quarantines those references.
	 */
	[[nodiscard]] bool Begin(
		Session& session,
		CaptureKind kind,
		RE::NiAVObject* retainedPlayerRoot,
		ID3D11RenderTargetView* expectedRenderTarget) noexcept;

	/** Read current same-thread evidence without ending the observation. */
	[[nodiscard]] Result Snapshot(const Session& session) noexcept;

	/** Classify a generic batch pass below the exact retained hand-mirror player root. */
	[[nodiscard]] ExactHandDrawClassification ClassifyExactHandPlayerDraw(
		RE::BSRenderPass* pass) noexcept;
	/** Same retained-root/target proof for mirror or hand-mirror receivers. */
	[[nodiscard]] ExactHandDrawClassification ClassifyExactMirrorPlayerDraw(
		const RE::BSRenderPass* pass) noexcept;

	/** End and aggregate the observation. Idempotent. */
	[[nodiscard]] Result End(Session& session) noexcept;

	void LogDiagnostics(const char* reason);
}
