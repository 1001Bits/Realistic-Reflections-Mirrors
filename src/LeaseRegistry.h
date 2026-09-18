#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

/**
 * Mutually exclusive engine-state lease families.
 *
 * A "lease" here is any marker-gated behaviour that temporarily mutates state
 * the engine owns - an app-cull bit, a fade alpha, a scene-root array, a
 * renderer shadow-state field. Two leases that touch the SAME engine object are
 * not independently testable: if a run misbehaves, neither can be blamed.
 *
 * On 2026-08-07 three player-root leases were armed in one session
 * (M3PlayerInclusion, WaterPlayerUncull, WaterPlayerRootInject), two of them
 * never runtime-proven. The session hung, and the resulting evidence could not
 * attribute the failure to any one of them - the post-mortem eventually showed
 * the hang was not even in that family. The rule "one unproven lease per run"
 * had been written down in HANDOVER.md and was still violated, because prose
 * cannot enforce anything.
 *
 * This registry makes it structural: a lease declares its family at arm time,
 * and the second arm in a family is REFUSED. Refusal is deliberately
 * conservative - the earlier lease stays armed and the later one is skipped -
 * so a mis-staged marker set degrades into a valid single-lease run instead of
 * an ambiguous one.
 */
namespace LeaseRegistry
{
	enum class Family : std::uint8_t
	{
		/** PlayerCharacter 3D roots: app-cull bits, fade alpha, root injection. */
		kPlayerRoot,
		/** The private BSShaderAccumulator's batch renderer and its groups. */
		kPrivateBatch,
		/** The authored pane's own geometry and its app-cull ownership. */
		kPane,
		/** Renderer-wide shadow state (cull mode, targets, viewports). */
		kRendererState,
		kCount
	};

	[[nodiscard]] constexpr std::string_view FamilyName(Family a_family) noexcept
	{
		switch (a_family) {
		case Family::kPlayerRoot:
			return "kPlayerRoot";
		case Family::kPrivateBatch:
			return "kPrivateBatch";
		case Family::kPane:
			return "kPane";
		case Family::kRendererState:
			return "kRendererState";
		default:
			return "kUnknown";
		}
	}

	/**
	 * Whether a lease has already been proven at runtime. Proven leases are the
	 * control baseline: they may stay armed alongside one unproven lease, and
	 * they never conflict with each other.
	 */
	enum class Maturity : std::uint8_t
	{
		kRuntimeProven,
		kUnproven
	};

	struct Registration
	{
		std::string_view name{};
		Family family{ Family::kCount };
		Maturity maturity{ Maturity::kUnproven };
	};

	/**
	 * Attempt to arm a lease. Returns false when an UNPROVEN lease would join a
	 * family that already holds an unproven lease this session. Proven leases
	 * do not conflict, but invalid registrations or bookkeeping failures refuse
	 * every lease. Names must have process lifetime (use string literals).
	 *
	 * Thread-safe and idempotent for identical registrations; a reused name may
	 * not change family or maturity. Safe to call before logging exists.
	 */
	[[nodiscard]] bool TryArm(const Registration& a_registration) noexcept;

	/** True when the named lease successfully armed. */
	[[nodiscard]] bool IsArmed(std::string_view a_name) noexcept;

	struct Diagnostics
	{
		std::uint32_t arms{ 0 };
		std::uint32_t refusals{ 0 };
		std::uint32_t familyConflicts{ 0 };
		std::array<std::string_view, static_cast<std::size_t>(Family::kCount)>
			unprovenHolder{};
	};

	[[nodiscard]] Diagnostics GetDiagnostics() noexcept;

	/** Clear all arm state. Only for tests and process init. */
	void Reset() noexcept;
}
