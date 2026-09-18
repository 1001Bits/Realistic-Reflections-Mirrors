#pragma once

namespace MirrorShadowMapBypassGate
{
	enum class Decision
	{
		kIgnore,
		kNoShadowBinding,
		kBindNeutral,
		kAlreadyNeutral,
		kFailStop
	};

	enum class RestoreDisposition
	{
		kClearPass,
		kRetainOriginalForRetry
	};

	struct Input
	{
		bool enabled{ false };
		bool faulted{ false };
		bool passActive{ false };
		bool mirrorPrimary{ false };
		bool graphicsCommit{ false };
		bool sameThread{ false };
		bool sameContext{ false };
		bool hasShadowBinding{ false };
		bool neutralAlreadyBound{ false };
		bool neutralWasBoundByUs{ false };
	};

	[[nodiscard]] constexpr Decision Decide(const Input& input) noexcept
	{
		if (!input.enabled || input.faulted || !input.passActive)
			return Decision::kIgnore;
		if (!input.mirrorPrimary || !input.graphicsCommit || !input.sameThread ||
			!input.sameContext)
			return Decision::kFailStop;
		if (input.neutralAlreadyBound)
			return input.neutralWasBoundByUs ? Decision::kAlreadyNeutral :
				Decision::kFailStop;
		if (!input.hasShadowBinding)
			return Decision::kNoShadowBinding;
		return Decision::kBindNeutral;
	}

	[[nodiscard]] constexpr RestoreDisposition ClassifyRestore(
		bool neutralWasBoundByUs,
		bool restoreVerified) noexcept
	{
		return neutralWasBoundByUs && !restoreVerified ?
			RestoreDisposition::kRetainOriginalForRetry :
			RestoreDisposition::kClearPass;
	}
}
