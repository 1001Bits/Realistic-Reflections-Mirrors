#pragma once
#include "MirrorFleetPolicy.h"
#include "MirrorCapturePoolDiagnostics.h"
#include <d3d11.h>

namespace MirrorFleetRuntime
{
	enum class RenderPhase : std::uint8_t { kWorldShadow, kPlayerShadow, kStanding, kHand, kOther, kCount };
	// Zero means no diagnostic interval was opened; tokens never own render state.
	using PhaseToken = std::uint64_t;
	std::uint64_t Now() noexcept;
	void PreparePresentHook() noexcept;
	// Swap-chain size and the refresh rate of the monitor showing the game,
	// read at most every two seconds on the frame thread; 0 until known.
	bool OutputSize(std::uint32_t& width, std::uint32_t& height) noexcept;
	std::uint32_t DisplayHz() noexcept;
	void Reconcile(std::span<const MultiMirrorPolicy::Identity> identities) noexcept;
	bool Request(MultiMirrorPolicy::Identity id, bool visible, std::uint64_t now) noexcept;
	void Planned(MultiMirrorPolicy::Identity id, std::uint64_t now) noexcept;
	/** Hand-mirror refresh clock: handRefreshHz raised, handLoweredRefreshHz lowered; 0 Hz is always due. */
	[[nodiscard]] bool HandDue(std::uint64_t now, bool raised) noexcept;
	void HandPlanned(std::uint64_t now, bool raised) noexcept;
	std::uint64_t AdmissionSequence(MultiMirrorPolicy::Identity id, std::uint64_t source) noexcept;
	void Result(MultiMirrorPolicy::Identity id, std::uint64_t source, bool attempted,
		bool success, const char* failure, std::uint64_t cpu, std::uint32_t width) noexcept;
	void Expired(MultiMirrorPolicy::Identity id, std::uint64_t capture) noexcept;
	bool ResumeImage(MultiMirrorPolicy::Identity id, std::uint64_t source,
		std::uint64_t captureSource, std::uint64_t capture) noexcept;
	void BeginFrame(ID3D11Device* device, ID3D11DeviceContext* context, std::uint64_t source) noexcept;
	void BeginGPU(ID3D11Device* device, ID3D11DeviceContext* context) noexcept;
	void EndGPU(ID3D11DeviceContext* context) noexcept;
	PhaseToken BeginPhase(ID3D11DeviceContext* context, RenderPhase phase) noexcept;
	void EndPhase(ID3D11DeviceContext* context, PhaseToken token) noexcept;
	// Cumulative GPU time of standing-mirror captures (phase timing, F8 panel).
	bool StandingGpu(double& totalMs, std::uint64_t& samples) noexcept;
	void Allocated(bool depth) noexcept;
	void SetFrameQuality(MirrorFleetPolicy::FrameQuality quality) noexcept;
	void Report(std::uint64_t source, std::uint64_t logicalBytes,
		const CapturePoolDiagnostics& targetPool) noexcept;
}
