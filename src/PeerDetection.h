#pragma once

namespace PeerDetection
{
	/**
	 * Which reflection-adjacent peers are present in this process.
	 *
	 * WHY THIS EXISTS: Realistic Reflections does not own the reflection pipeline,
	 * it feeds the engine's existing reflection consumers. Two peers rewrite those
	 * consumers, and each changes what our writes are worth:
	 *
	 *   Community Shaders - replaces the water pixel shader. Its Water.hlsl scales
	 *     the t3 cube term by saturate(distance / 1024) and forces it to zero when
	 *     HideSky is set, so our t3 override contributes nothing in interiors and
	 *     nearly nothing in the near field. The seam still installs correctly; the
	 *     consumer discards it. Under CS the t10/t11 resolve path is primary.
	 *
	 *   ENB - closed source, therefore unverifiable. We can detect it and we can
	 *     say what we do not know, but we cannot claim compatibility from source
	 *     reading the way we can for CS.
	 *
	 * Detection is by exported symbol and module handle, never by scanning for
	 * file names on disk: users rename freely, other wrappers share the same file
	 * names, and a mod being installed is not the same as it being loaded.
	 */
	struct Peers
	{
		bool communityShaders{ false };
		bool enb{ false };
	};

	/**
	 * Probe the loaded modules once and cache the result for process lifetime.
	 * Safe to call from any thread and at any point after DLL load; call it after
	 * peer plugins have loaded (InputLoaded or later) for an accurate answer.
	 */
	[[nodiscard]] const Peers& Detect() noexcept;

	/**
	 * Emit a one-time summary of what was found and what it implies for our
	 * routes. Reporting, never silent degradation - if a peer defeats one of our
	 * paths the log must say so, or the next person debugging it will look for a
	 * defect on our side that does not exist.
	 */
	void LogOnce() noexcept;

	/**
	 * True when ENB is present. Callers that own D3D resources must treat this as
	 * a warning, not a feature gate.
	 *
	 * UNRESOLVED DESIGN CONFLICT - do not claim ENB compatibility until closed:
	 * ENB drives its own device/resource reset lifecycle. Our D3D objects live in
	 * `stl::no_destructor` process-lifetime storage, which by construction never
	 * releases. If ENB resets the device beneath us, every cached view becomes a
	 * stale handle with no release path. Closing this needs an explicit
	 * invalidate-and-recreate hook, not a detection flag.
	 */
	[[nodiscard]] bool ENBPresent() noexcept;

	/** True when Community Shaders is loaded. */
	[[nodiscard]] bool CommunityShadersPresent() noexcept;
}
