#pragma once

#include <cstdint>

namespace RE
{
	class NiCamera;
}

namespace HandMirrorParticleCameraLease
{
	enum class InstallStatus : std::uint8_t
	{
		kNotRequested,
		kInstalled,
		kAlreadyInstalled,
		kUnsupportedRuntime,
		kSignatureMismatch,
		kPatchFailed,
		kFaulted
	};

	enum class BeginStatus : std::uint8_t
	{
		kBegan,
		kNotInstalled,
		kFaulted,
		kNested,
		kInvalidCamera,
		kHookOwnershipLost,
		kAnotherThreadActive
	};

	struct CaptureToken
	{
		std::uint32_t generation{ 0 };
		std::uint32_t threadID{ 0 };
		BeginStatus status{ BeginStatus::kNotInstalled };
		bool active{ false };
	};

	struct CaptureResult
	{
		std::uint64_t packCalls{ 0 };
		std::uint64_t correctedCalls{ 0 };
		std::uint64_t alreadyPrivateCalls{ 0 };
		std::uint64_t nativeCalls{ 0 };
		std::uint64_t nativeReturns{ 0 };
		std::uint64_t restoreAttempts{ 0 };
		std::uint64_t restoreSuccesses{ 0 };
		bool tokenMatched{ false };
		bool clean{ false };
		bool faulted{ false };
	};

	struct PrivateOrigin
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	struct Diagnostics
	{
		std::uint64_t installAttempts{ 0 };
		std::uint64_t installSuccesses{ 0 };
		std::uint64_t beginAttempts{ 0 };
		std::uint64_t begins{ 0 };
		std::uint64_t beginRejects{ 0 };
		std::uint64_t hookCalls{ 0 };
		std::uint64_t inactiveNativeCalls{ 0 };
		std::uint64_t activePackCalls{ 0 };
		std::uint64_t alreadyPrivateCalls{ 0 };
		std::uint64_t correctedCalls{ 0 };
		std::uint64_t nativeCalls{ 0 };
		std::uint64_t nativeReturns{ 0 };
		std::uint64_t restoreAttempts{ 0 };
		std::uint64_t restoreSuccesses{ 0 };
		std::uint64_t scopeCompletions{ 0 };
		std::uint64_t scopeRejects{ 0 };
		std::uint64_t crossThreadCalls{ 0 };
		std::uint64_t tokenMismatches{ 0 };
		std::uint32_t lastExceptionCode{ 0 };
		float maximumOriginDrift{ 0.0F };
		float maximumBasisDrift{ 0.0F };
		bool installed{ false };
		bool hookOwned{ false };
		bool faulted{ false };
	};

	/** Install the exact SE 1.5.97 NiParticles CPU-quad packing call-site hook. */
	[[nodiscard]] InstallStatus Install(bool requested) noexcept;

	/**
	 * Freeze the expected reflected camera origin/basis for one exact hand pass.
	 * The hook changes only RendererShadowState CPU values read synchronously by
	 * the native particle packer, restores them before returning, and never
	 * advances particle simulation or suppresses a draw.
	 */
	[[nodiscard]] CaptureToken BeginCapture(const RE::NiCamera* camera) noexcept;
	[[nodiscard]] CaptureResult EndCapture(CaptureToken& token) noexcept;

	/** Copy the exact private-camera origin retained by the active hand pass. */
	[[nodiscard]] bool TryGetExpectedPrivateOrigin(PrivateOrigin& output) noexcept;

	[[nodiscard]] bool CurrentCaptureHealthy() noexcept;
	[[nodiscard]] bool Installed() noexcept;
	[[nodiscard]] bool OwnsHook() noexcept;
	[[nodiscard]] bool Faulted() noexcept;
	[[nodiscard]] Diagnostics GetDiagnostics() noexcept;
}
