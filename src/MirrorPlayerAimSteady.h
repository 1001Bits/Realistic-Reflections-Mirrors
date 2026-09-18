#pragma once

#include <cstdint>

// Steady arms for the reflected player in first person (owner, core run 4:
// "the player should stop moving their arms when looking left or right or up
// and down").
//
// In first person the mirror shows the hidden third-person body, and that
// body's graph receives the same per-frame animation channels as the
// first-person graph: TurnDelta starts turn-in-place steps when the camera
// yaws, Pitch drives the bow spine twist, and a BSDirectAtModifier (when a
// behaviour enables it) bends the spine toward the view target. None of that
// belongs to a body that is only ever seen standing in front of a mirror.
//
// Exact SE 1.5.97, AE 1.6.1170, GOG AE 1.6.1179, and AE 1.7.104 hooks, all
// limited to the player's non-"FirstPerson" graph while the player is in first
// person. Every call and vtable target is checked before patching; a changed
// executable fails closed. 1170/1179 use the same intra-function call offsets
// as 1.7.104; their RVAs are taken from those executables, not SE Address
// Library IDs (those IDs name different AE functions).
// The SE evidence names below identify the same native operations on AE:
//  - BSDirectAtModifierInterface slot 1 (SE 0x14072FAC0, vtable ID 263725):
//    the aim target is withdrawn, so the modifier fades its offsets out.
//  - the channel apply call in BSAnimationGraphManager (ID 62436 + 0x47, calls
//    ID 62645): after the channels are written, Pitch, PitchDelta and
//    TurnDelta are zeroed in that one graph through the per-graph setter
//    (ID 62709) -- never the holder setter, which writes every graph --
//    and iSyncTurnState is held at "stopped" (per-graph int setter, ID 62708).
//
// Owner, run 5: "when i turn left and right the arms/hand still goes up and
// down"; and "when moving up and down i want the player head to tilt up and
// down a bit in the reflection". Two more verified call sites (SE Ghidra report 2,
// .runtime-review/core-release-20260916/ghidra2):
//  - the per-graph event send in BSAnimationGraphManager (ID 62420 + 0x6A,
//    calls ID 62646): turnLeft/turnRight become turnStop for the hidden body,
//    because the magic ready/cast behaviours play their turn clips on those
//    events whatever TurnDelta says.
//  - the pose write in the per-graph generate step (ID 62644 + 0x110, calls
//    ID 62933): after the animated pose lands, the neck and head of the hidden
//    body turn about its left-right axis by a fraction of the view pitch
//    (MirrorHeadTiltMath).
//  - run 6 showed that pose write never reaches the player's graph, so the
//    actor's 3D update of the third-person root (ID 39446 + 0x94, calls
//    ID 68900) tilts the same bones just before the world transforms are
//    computed; a bone already tilted since its last pose write is left alone.
// Gameplay aim reads the active first-person graph, which no hook touches.
namespace MirrorPlayerAimSteady
{
	struct Diagnostics
	{
		bool aimTargetHook{ false };
		bool channelHook{ false };
		bool turnStepFilter{ false };
		bool headTiltHook{ false };
		bool headTiltFallbackHook{ false };
		std::uint64_t aimTargetsWithdrawn{ 0 };
		std::uint64_t channelZeroes{ 0 };
		std::uint64_t turnStepsSteadied{ 0 };
		std::uint64_t headTilts{ 0 };
		std::uint64_t headBoneMisses{ 0 };
		std::uint64_t hiddenPoseWrites{ 0 };
		std::uint64_t hiddenWorldUpdates{ 0 };
		std::uint64_t faults{ 0 };
	};

	// Call once at kDataLoaded. Installs nothing off the four contracted
	// runtimes or when the native bytes differ from the recorded ones.
	void Install() noexcept;
	[[nodiscard]] Diagnostics Snapshot() noexcept;
}
