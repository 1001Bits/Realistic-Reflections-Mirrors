#include "PCH.h"

#include "StandingMirrorPlacement.h"
#include "StandingMirrorPlacementPolicy.h"

#ifdef MIRRORS_OF_SKYRIM_STANDALONE
#	include "MirrorsOfSkyrimRecognition.h"
#else
#	include "MirrorRecognition.h"
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace StandingMirrorPlacement
{
	namespace
	{
		// Counter dump for callers that already hold g_stateLock (ConfirmLocked,
		// CancelLocked).  V135 called the public LogDiagnostics from there, which
		// re-locked the non-recursive mutex and killed the game at confirm.
		void LogDiagnosticsUnlocked(const char* reason, bool active) noexcept;
		struct PlacementState;
		void SetLookLease(PlacementState& state, bool hold) noexcept;
		constexpr std::string_view kPluginName = "MirrorsOfSkyrim.esp";
		constexpr std::uint32_t kKeyboardEnter = 0x1C;
		constexpr std::uint32_t kKeyboardEscape = 0x01;
		constexpr std::uint32_t kKeyboardLeft = 0xCB;
		constexpr std::uint32_t kKeyboardRight = 0xCD;
		constexpr std::uint32_t kMouseWheelUp = 8;
		constexpr std::uint32_t kMouseWheelDown = 9;
		constexpr std::uint32_t kKeyboardUp = 0xC8;
		constexpr std::uint32_t kKeyboardDown = 0xD0;
		constexpr std::uint32_t kMouseButtonLeft = 0;
		constexpr std::uint32_t kMouseButtonRight = 1;
		// Skyrim VR drives its controllers through the gamepad column of its
		// own controlmap (Interface BSA, 2026-09-05 extract): the triggers are
		// 0x0009/0x000A ("Left/Right Attack/Block") and the sticks 0x000B/0x000C.
		constexpr std::uint32_t kVRLeftTrigger = 0x0009;
		constexpr std::uint32_t kVRRightTrigger = 0x000A;
		// Raw OpenVR ids in case the VR device reports the controller button
		// itself: k_EButton_Axis1 (SteamVR trigger) and k_EButton_Axis0.
		constexpr std::uint32_t kOpenVRTrigger = 33;
		constexpr std::uint32_t kOpenVRAxis0 = 32;
		// Button events logged per live VR placement (first N), to pin the
		// controller's real device/id/user-event for the trigger.
		constexpr std::uint32_t kVRButtonLogBudget = 40;
		// Radians of mirror yaw per mouse X unit while the right button is held.
		constexpr float kMouseRotateRadiansPerUnit = 0.004F;
		// Input batches (~frames) to wait for the InventoryMenu to honour the
		// close request before escalating to a forced hide.
		constexpr std::uint32_t kForceHideAfterBatches = 30;
		// Window after a kit activation during which the vanilla "cannot equip"
		// notification belongs to that activation and is suppressed.
		constexpr std::uint64_t kEquipMessageSuppressWindowMs = 1500;

		struct PlacementState
		{
			struct ControlLease
			{
				bool acquired{ false };
				bool waitLogged{ false };
			};

			std::size_t style{ 0 };
			RE::TESObjectMISC* inventoryItem{ nullptr };
			RE::TESBoundObject* placedBase{ nullptr };
			RE::NiPointer<RE::TESObjectREFR> preview{};
			RE::TESObjectCELL* originCell{ nullptr };
			RE::TESWorldSpace* originWorldspace{ nullptr };
			// Cell the preview currently stands in (exteriors may cross cells).
			RE::TESObjectCELL* currentCell{ nullptr };
			std::uint32_t menuCloseWaitBatches{ 0 };
			bool forceHideSent{ false };
			float rotationOffsetRadians{ 0.0F };
			// Absolute mirror yaw: faces the player once at begin, then changes only
			// through Left/Right or right-button mouse drag (the mirror does not
			// keep turning to face the camera while it is walked around).
			float mirrorYaw{ 0.0F };
			bool mirrorYawInitialized{ false };
			// Right mouse button held: mouse X rotates the mirror, position frozen,
			// camera look leased off so the view does not turn with it.
			bool rotating{ false };
			bool lookLeaseHeld{ false };
			float previewDistance{ StandingMirrorPlacementPolicy::kPreviewDistance };
			// VR stick placement: sideways offset of the preview across the
			// bearing (positive = player's right), driven by the left stick's X;
			// the right stick turns the preview and changes previewDistance.
			// Each stick integrates over the time since its previous sample.
			float previewLateral{ 0.0F };
			// VR: the follow bearing is frozen on the first live update, so head
			// turns never move the preview; walking carries it along at the same
			// offset and the sticks adjust that offset.
			float frozenHeadingYaw{ 0.0F };
			bool headingFrozen{ false };
			// VR trigger is analog: confirm on the rising edge through
			// kVRTriggerPressValue instead of the digital IsDown() edge.
			StandingMirrorPlacementPolicy::VRConfirmationGate vrConfirmation{};
			std::uint32_t vrButtonLogBudget{ kVRButtonLogBudget };
			std::optional<std::chrono::steady_clock::time_point> lastLeftStickSample{};
			std::optional<std::chrono::steady_clock::time_point> lastRightStickSample{};
			RE::NiPoint3 smoothedPosition{};
			bool smoothedValid{ false };
			bool awaitingMenuClose{ true };
			ControlLease controls{};
		};

		struct Counters
		{
			std::atomic_uint64_t papyrusRequests{ 0 };
			std::atomic_uint64_t dropRequests{ 0 };
			std::atomic_uint64_t previewsStarted{ 0 };
			std::atomic_uint64_t previewUpdates{ 0 };
			std::atomic_uint64_t floorProbes{ 0 };
			std::atomic_uint64_t floorProbeHits{ 0 };
			std::atomic_uint64_t distanceChanges{ 0 };
			std::atomic_uint64_t cameraYawReads{ 0 };
			std::atomic_uint64_t cameraYawFallbacks{ 0 };
			std::atomic_uint64_t menuCloseEvents{ 0 };
			std::atomic_uint64_t forceHides{ 0 };
			std::atomic_uint64_t equipMessagesSuppressed{ 0 };
			std::atomic_uint64_t rotations{ 0 };
			std::atomic_uint64_t stickSamples{ 0 };
			std::atomic_uint64_t confirms{ 0 };
			std::atomic_uint64_t confirmFailures{ 0 };
			std::atomic_uint64_t cancels{ 0 };
			std::atomic_uint64_t pickups{ 0 };
			std::atomic_uint64_t controlWaits{ 0 };
			std::atomic_uint64_t controlLeases{ 0 };
			std::atomic_uint64_t controlRestores{ 0 };
			std::atomic_uint64_t failures{ 0 };
		};

		enum class ControlLeaseAcquireResult : std::uint8_t
		{
			kPending,
			kAcquired,
			kFailed
		};

		std::array<RE::TESBoundObject*, 5> g_placedBases{};
		std::array<RE::TESObjectSTAT*, 5> g_previewBases{};
		std::array<RE::TESObjectMISC*, 5> g_inventoryItems{};
		std::mutex g_stateLock{};
		StandingMirrorPlacementPolicy::VRConfirmationGate g_vrTriggerHistory{};
		stl::no_destructor<std::optional<PlacementState>> g_state{};
		std::atomic_bool g_ready{ false };
		std::atomic_bool g_inputSinkRegistered{ false };
		std::atomic_bool g_containerSinkRegistered{ false };
		std::atomic_bool g_menuSinkRegistered{ false };
		std::atomic_bool g_hudHookInstalled{ false };
		std::atomic_uint64_t g_lastActivationTick{ 0 };
		std::atomic_bool g_internalInventoryMutation{ false };
		std::atomic_bool g_suspended{ true };
		std::atomic_uint64_t g_lifecycleGeneration{ 1 };
		Counters g_counters{};

		[[nodiscard]] std::optional<PlacementState>& State() noexcept
		{
			return g_state.get();
		}

		[[nodiscard]] bool Finite(const RE::NiPoint3& point) noexcept
		{
			return std::isfinite(point.x) && std::isfinite(point.y) &&
			       std::isfinite(point.z);
		}

		// Texts this module emits.  The HUD filter runs when the UI queue drains,
		// not inside ShowHUDMessage, so ownership is proven by text, not by a
		// thread-local marker (V133 suppressed its own prompt).
		constexpr std::array<std::string_view, 12> kOwnNotifications{
			"A standing mirror is required to begin placement.",
			"Standing mirror placement cancelled; the kit remains in your inventory.",
			"Standing mirror placement could not start.",
			"The dropped mirror kit was left in the world; placement did not start.",
			"The mirror kit was returned, but placement could not start.",
			"Standing mirror placement could not acquire player controls.",
			"The standing mirror kit is no longer in your inventory.",
			"Standing mirror placed. Aim at it and press Activate to pick it up.",
			"Standing mirror picked up.",
			"Store Mirror (press E)",
			"That mirror position could not be confirmed; placement was cancelled and the kit remains in your inventory.",
			""
		};

		[[nodiscard]] bool IsOwnNotification(std::string_view text) noexcept
		{
			for (const auto own : kOwnNotifications) {
				if (!own.empty() && own == text)
					return true;
			}
			return false;
		}

		// A vanilla "cannot equip" notification held back while a kit activation
		// may follow it; re-emitted unchanged if none does.
		std::atomic_uint64_t g_heldEquipMessageTick{ 0 };
		std::atomic_bool g_reemitEquipMessage{ false };
		constexpr std::uint64_t kHeldEquipMessageMs = 250;

		void Notify(const char* message) noexcept
		{
			try {
				RE::SendHUDMessage::ShowHUDMessage(message);
			} catch (...) {
			}
		}

		void DisposeReference(RE::TESObjectREFR* reference) noexcept
		{
			if (!reference)
				return;
			try {
				reference->SetActivationBlocked(true);
				reference->SetCollision(false);
				reference->Disable();
				reference->SetDelete(true);
			} catch (...) {
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] std::optional<std::size_t> StyleForInventoryItem(
			const RE::TESForm* item) noexcept
		{
			if (!item)
				return std::nullopt;
			for (std::size_t style = 0; style < g_inventoryItems.size(); ++style) {
				if (g_inventoryItems[style] == item)
					return style;
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::size_t> StyleForPlacedMirrorBase(
			const RE::TESBoundObject* base) noexcept
		{
			if (!base)
				return std::nullopt;
			for (std::size_t style = 0; style < g_placedBases.size(); ++style) {
				if (g_placedBases[style] == base)
					return style;
			}
			return std::nullopt;
		}

		[[nodiscard]] RE::ObjectRefHandle ReadFlatCrosshairTarget() noexcept
		{
			auto* const pick = RE::CrosshairPickData::GetSingleton();
			if (!pick)
				return {};
			try {
				if (REL::Module::IsVR()) {
					// VR keeps one target per pointing device at the same 0x04 the
					// flat layout uses for its single handle.  Prefer whichever
					// controller is actually pointing at something, then the
					// headset, so the mirror answers the hand the player aimed
					// with rather than only the head.
					auto* const bytes = reinterpret_cast<std::byte*>(pick);
					for (std::uint32_t device = 0;
						device < RE::VR_DEVICE::kTotal; ++device) {
						RE::ObjectRefHandle candidate{};
						std::memcpy(
							&candidate,
							bytes + 0x04 + device * sizeof(RE::ObjectRefHandle),
							sizeof(candidate));
						if (candidate)
							return candidate;
					}
					return {};
				}
				return REL::RelocateMember<RE::ObjectRefHandle>(pick, 0x04, 0x04);
			} catch (...) {
				return {};
			}
		}

		class InternalInventoryMutationGuard
		{
		public:
			InternalInventoryMutationGuard() noexcept
			{
				g_internalInventoryMutation.store(true, std::memory_order_release);
			}

			~InternalInventoryMutationGuard()
			{
				g_internalInventoryMutation.store(false, std::memory_order_release);
			}

			InternalInventoryMutationGuard(
				const InternalInventoryMutationGuard&) = delete;
			InternalInventoryMutationGuard& operator=(
				const InternalInventoryMutationGuard&) = delete;
		};

		[[nodiscard]] RE::TESObjectMISC* ResolveInventoryItem(
			std::size_t style) noexcept
		{
			if (style >= g_inventoryItems.size())
				return nullptr;
			if (g_inventoryItems[style])
				return g_inventoryItems[style];
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				return data->LookupForm<RE::TESObjectMISC>(
					StandingMirrorPlacementPolicy::kInventoryLocalFormIDs[style],
					kPluginName);
			}
			return nullptr;
		}

		[[nodiscard]] StandingMirrorPlacementPolicy::RequiredControlState
		ReadRequiredControlState(
			const RE::ControlMap& controls) noexcept
		{
			using Flag = RE::ControlMap::UEFlag;
			return {
				.activate = controls.AreControlsEnabled(Flag::kActivate),
				.menu = controls.AreControlsEnabled(Flag::kMenu),
				.fighting = controls.AreControlsEnabled(Flag::kFighting),
				.wheelZoom = controls.AreControlsEnabled(Flag::kWheelZoom)
			};
		}

		[[nodiscard]] std::uint32_t ReadRequiredControlMask(
			const RE::ControlMap& controls) noexcept
		{
			return StandingMirrorPlacementPolicy::RequiredControlMask(
				ReadRequiredControlState(controls));
		}

		void RestoreControlLease(PlacementState& state) noexcept
		{
			SetLookLease(state, false);
			state.rotating = false;
			if (!state.controls.acquired)
				return;
			if (auto* controls = RE::ControlMap::GetSingleton()) {
				using Flag = RE::ControlMap::UEFlag;
				try {
					const auto before = ReadRequiredControlMask(*controls);
					controls->ToggleControls(Flag::kActivate, true, false);
					controls->ToggleControls(Flag::kMenu, true, false);
					controls->ToggleControls(Flag::kFighting, true, false);
					controls->ToggleControls(Flag::kWheelZoom, true, false);
					const auto after = ReadRequiredControlMask(*controls);
					if (after == StandingMirrorPlacementPolicy::kAllRequiredControlBits) {
						g_counters.controlRestores.fetch_add(
							1, std::memory_order_relaxed);
						logger::info(
							"[MOS][Placement] controls restored style={} mask=0x{:X}->0x{:X}",
							state.style, before, after);
					} else {
						g_counters.failures.fetch_add(1, std::memory_order_relaxed);
						logger::critical(
							"[MOS][Placement] control restore incomplete style={} mask=0x{:X}->0x{:X}",
							state.style, before, after);
					}
				} catch (...) {
					g_counters.failures.fetch_add(1, std::memory_order_relaxed);
					logger::critical(
						"[MOS][Placement] control restore faulted style={}", state.style);
				}
			} else {
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
				logger::critical(
					"[MOS][Placement] control restore unavailable style={}", state.style);
			}
			state.controls = {};
		}

		void SetLookLease(PlacementState& state, const bool hold) noexcept
		{
			if (state.lookLeaseHeld == hold)
				return;
			if (auto* controls = RE::ControlMap::GetSingleton()) {
				try {
					controls->ToggleControls(
						RE::ControlMap::UEFlag::kLooking, !hold, false);
					state.lookLeaseHeld = hold;
				} catch (...) {
					g_counters.failures.fetch_add(1, std::memory_order_relaxed);
				}
			}
		}

		[[nodiscard]] ControlLeaseAcquireResult AcquireControlLease(
			PlacementState& state) noexcept
		{
			if (state.controls.acquired)
				return ControlLeaseAcquireResult::kAcquired;
			auto* const controls = RE::ControlMap::GetSingleton();
			if (!controls)
				return ControlLeaseAcquireResult::kFailed;
			using Flag = RE::ControlMap::UEFlag;
			try {
				const auto controlState = ReadRequiredControlState(*controls);
				const auto before =
					StandingMirrorPlacementPolicy::RequiredControlMask(controlState);
				if (!StandingMirrorPlacementPolicy::AllRequiredControlsEnabled(
						controlState)) {
					g_counters.controlWaits.fetch_add(1, std::memory_order_relaxed);
					if (!state.controls.waitLogged) {
						state.controls.waitLogged = true;
						logger::info(
							"[MOS][Placement] waiting for vanilla controls style={} mask=0x{:X}",
							state.style, before);
					}
					return ControlLeaseAcquireResult::kPending;
				}
				state.controls.acquired = true;
				state.controls.waitLogged = false;
				controls->ToggleControls(Flag::kActivate, false, false);
				controls->ToggleControls(Flag::kMenu, false, false);
				controls->ToggleControls(Flag::kFighting, false, false);
				controls->ToggleControls(Flag::kWheelZoom, false, false);
				const auto after = ReadRequiredControlMask(*controls);
				if (after == 0) {
					g_counters.controlLeases.fetch_add(1, std::memory_order_relaxed);
					logger::info(
						"[MOS][Placement] controls acquired style={} mask=0x{:X}->0x{:X}",
						state.style, before, after);
					return ControlLeaseAcquireResult::kAcquired;
				}
			} catch (...) {
			}
			RestoreControlLease(state);
			g_counters.failures.fetch_add(1, std::memory_order_relaxed);
			return ControlLeaseAcquireResult::kFailed;
		}

		[[nodiscard]] bool PickFloorSEH(
			RE::bhkWorld* world, RE::bhkPickData* pick) noexcept
		{
			__try {
				return world->PickObject(*pick);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		/**
		 * Vertical Havok probe at (x, y) from kFloorProbeRise above the player's
		 * feet to kFloorProbeDrop below.  Uses the line-of-sight layer so floors,
		 * stairs and furniture count while the collisionless preview itself, the
		 * player and other actors do not.
		 */
		[[nodiscard]] bool ProbeFloorHeight(
			RE::TESObjectCELL& cell, float x, float y, float referenceZ,
			float& output) noexcept
		{
			namespace Policy = StandingMirrorPlacementPolicy;
			auto* const world = cell.GetbhkWorld();
			if (!world || !std::isfinite(x) || !std::isfinite(y) ||
				!std::isfinite(referenceZ)) {
				return false;
			}
			g_counters.floorProbes.fetch_add(1, std::memory_order_relaxed);
			const float scale = RE::bhkWorld::GetWorldScale();
			const float fromZ = referenceZ + Policy::kFloorProbeRise;
			const float toZ = referenceZ - Policy::kFloorProbeDrop;
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(
				x * scale, y * scale, fromZ * scale, 0.0F);
			pick.rayInput.to = RE::hkVector4(
				x * scale, y * scale, toZ * scale, 0.0F);
			pick.rayInput.enableShapeCollectionFilter = false;
			std::uint32_t group = 0;
			if (auto* filter = RE::bhkCollisionFilter::GetSingleton())
				group = filter->GetNewSystemGroup();
			pick.rayInput.filterInfo.filter =
				(group << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			bool hit = false;
			{
				RE::BSReadLockGuard lock{ world->worldLock };
				hit = PickFloorSEH(world, &pick) && pick.rayOutput.HasHit();
			}
			if (!hit ||
				!Policy::FloorHeightFromProbe(
					referenceZ, pick.rayOutput.hitFraction, output)) {
				return false;
			}
			g_counters.floorProbeHits.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] __declspec(noinline) bool TryReadCameraPoseSEH(
			RE::NiPoint3& origin, RE::NiPoint3& forward) noexcept
		{
			__try {
				auto* const camera = RE::Main::WorldRootCamera();
				if (!camera)
					return false;
				origin = camera->world.translate;
				const auto& rotate = camera->world.rotate;
				forward = { rotate.entry[0][0], rotate.entry[1][0], rotate.entry[2][0] };
				return std::isfinite(origin.x) && std::isfinite(origin.y) &&
					std::isfinite(origin.z) && std::isfinite(forward.x) &&
					std::isfinite(forward.y) && std::isfinite(forward.z);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] __declspec(noinline) RE::TESObjectREFR* CollidableRefSEH(
			const RE::hkpCollidable* collidable) noexcept
		{
			__try {
				return collidable ?
					RE::TESHavokUtilities::FindCollidableRef(*collidable) : nullptr;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		// Reach of the store prompt/pickup ray from the camera, world units.
		constexpr float kStoreMirrorReach = 260.0F;

		/**
		 * The engine's crosshair activation pick never targets a static, so the
		 * "Store Mirror" prompt and pickup use their own forward Havok ray from
		 * the rendered camera (the placed frame carries a static Havok box since
		 * V150).  Returns the dynamic placed-mirror reference in reach, if any.
		 */
		[[nodiscard]] RE::TESObjectREFR* FindPlacedMirrorAhead() noexcept
		{
			auto* const player = RE::PlayerCharacter::GetSingleton();
			auto* const cell = player ? player->GetParentCell() : nullptr;
			auto* const world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world)
				return nullptr;
			RE::NiPoint3 origin{};
			RE::NiPoint3 forward{};
			if (!TryReadCameraPoseSEH(origin, forward))
				return nullptr;
			const float scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(
				origin.x * scale, origin.y * scale, origin.z * scale, 0.0F);
			pick.rayInput.to = RE::hkVector4(
				(origin.x + forward.x * kStoreMirrorReach) * scale,
				(origin.y + forward.y * kStoreMirrorReach) * scale,
				(origin.z + forward.z * kStoreMirrorReach) * scale, 0.0F);
			pick.rayInput.enableShapeCollectionFilter = false;
			std::uint32_t group = 0;
			if (auto* filter = RE::bhkCollisionFilter::GetSingleton())
				group = filter->GetNewSystemGroup();
			pick.rayInput.filterInfo.filter =
				(group << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			bool hit = false;
			{
				RE::BSReadLockGuard lock{ world->worldLock };
				hit = PickFloorSEH(world, &pick) && pick.rayOutput.HasHit();
			}
			if (!hit)
				return nullptr;
			auto* const ref = CollidableRefSEH(pick.rayOutput.rootCollidable);
			if (!ref || !ref->IsDynamicForm() ||
				!StyleForPlacedMirrorBase(ref->GetObjectReference())) {
				return nullptr;
			}
			return ref;
		}

		/**
		 * Heading of the rendered camera (NiCamera forward is world-rotate
		 * column 0).  Mouse-look turns this in both first and third person,
		 * whereas the actor's own yaw only follows the camera while moving.
		 */
		[[nodiscard]] __declspec(noinline) bool TryReadCameraYawSEH(
			float& output) noexcept
		{
			__try {
				auto* const camera = RE::Main::WorldRootCamera();
				if (!camera)
					return false;
				const auto& rotate = camera->world.rotate;
				const float forwardX = rotate.entry[0][0];
				const float forwardY = rotate.entry[1][0];
				if (!std::isfinite(forwardX) || !std::isfinite(forwardY) ||
					forwardX * forwardX + forwardY * forwardY < 1.0e-6F) {
					return false;
				}
				output = std::atan2(forwardX, forwardY);
				return std::isfinite(output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] RE::TESObjectCELL* ResolvePreviewCell(
			const PlacementState& state, const RE::NiPoint3& position) noexcept
		{
			auto* const world = RE::TES::GetSingleton();
			if (!world)
				return nullptr;
			auto* const cell = world->GetCell(position);
			if (!cell)
				return nullptr;
			if (state.originWorldspace) {
				// Exterior: the follow point may cross a cell boundary; any cell
				// of the origin worldspace is a valid stand.
				return cell->IsExteriorCell() &&
						cell->GetRuntimeData().worldSpace == state.originWorldspace ?
					cell : nullptr;
			}
			return cell == state.originCell ? cell : nullptr;
		}

		[[nodiscard]] bool UpdatePreviewLocked() noexcept
		{
			namespace Policy = StandingMirrorPlacementPolicy;
			if (!State())
				return false;
			auto* const player = RE::PlayerCharacter::GetSingleton();
			auto& state = *State();
			if (!player || !state.preview || !Policy::SamePlacementLocation(
					reinterpret_cast<std::uintptr_t>(state.originCell),
					reinterpret_cast<std::uintptr_t>(state.originWorldspace),
					reinterpret_cast<std::uintptr_t>(player->GetParentCell()),
					reinterpret_cast<std::uintptr_t>(player->GetWorldspace()))) {
				return false;
			}

			const auto playerPosition = player->GetPosition();
			if (state.rotating && state.smoothedValid && state.mirrorYawInitialized) {
				// Rotate-only mode: keep the stand, apply the current yaw.
				try {
					state.preview->SetAngle(RE::NiPoint3{ 0.0F, 0.0F, state.mirrorYaw });
					state.preview->Update3DPosition(true);
					return true;
				} catch (...) {
					return false;
				}
			}
			float headingYaw = 0.0F;
			if (REL::Module::IsVR() && state.headingFrozen) {
				// VR live placement: the bearing froze as placement went live, so
				// head turns leave the preview alone; walking carries it along at
				// the same offset and the sticks slide, push and turn it.
				headingYaw = state.frozenHeadingYaw;
			} else {
				if (TryReadCameraYawSEH(headingYaw)) {
					g_counters.cameraYawReads.fetch_add(1, std::memory_order_relaxed);
				} else {
					headingYaw = player->GetAngleZ();
					g_counters.cameraYawFallbacks.fetch_add(
						1, std::memory_order_relaxed);
				}
				if (REL::Module::IsVR() && !state.awaitingMenuClose) {
					state.frozenHeadingYaw = headingYaw;
					state.headingFrozen = true;
					logger::info(
						"[MOS][Placement] VR bearing frozen yaw={:.2f}", headingYaw);
				}
			}
			const auto target = Policy::FollowTarget(
				playerPosition.x, playerPosition.y, playerPosition.z,
				headingYaw, state.previewDistance, state.previewLateral);
			Policy::Point3 goal = target;
			float floorZ = 0.0F;
			if (ProbeFloorHeight(
					*player->GetParentCell(), target.x, target.y, playerPosition.z,
					floorZ)) {
				goal.z = floorZ;
			}
			if (!std::isfinite(goal.x) || !std::isfinite(goal.y) ||
				!std::isfinite(goal.z)) {
				return false;
			}
			const Policy::Point3 current{
				state.smoothedPosition.x, state.smoothedPosition.y,
				state.smoothedPosition.z };
			const auto smoothed = state.smoothedValid ?
				Policy::SmoothTowards(current, goal, Policy::kPreviewFollowSmoothing) :
				goal;
			const RE::NiPoint3 position{ smoothed.x, smoothed.y, smoothed.z };
			if (!Finite(position))
				return false;
			auto* const cell = ResolvePreviewCell(state, position);
			if (!cell)
				return false;
			state.currentCell = cell;

			if (!state.mirrorYawInitialized) {
				state.mirrorYaw = Policy::FacingYawRadians(
					position.x, position.y, playerPosition.x, playerPosition.y,
					state.rotationOffsetRadians);
				state.mirrorYawInitialized = true;
			}
			const float yaw = state.mirrorYaw;
			try {
				state.preview->SetPosition(position);
				state.preview->SetAngle(RE::NiPoint3{ 0.0F, 0.0F, yaw });
				// SetPosition/SetAngle only rewrite the reference data; the loaded
				// 3D follows on an explicit warp (03:35 run: ghost never moved).
				state.preview->Update3DPosition(true);
				state.smoothedPosition = position;
				state.smoothedValid = true;
				const auto updates =
					g_counters.previewUpdates.fetch_add(1, std::memory_order_relaxed) + 1;
				if (updates <= 3 || updates % 120 == 0) {
					logger::info(
						// Both angles, named. The line used to print headingYaw as
						// plain "yaw", and on 2026-09-16 that is what got copied into
						// a placement manifest: the camera bearing instead of the
						// mirror's own frozen facing, 68 degrees out, which turned
						// the pane toward the wall and left it black because a pane
						// facing away from the capture eye fails the facing gate.
						// mirrorYaw carries four decimals because it is the value a
						// placement is written from.
						"[MOS][Placement] follow #{} preview=({:.0f},{:.0f},{:.0f}) player=({:.0f},{:.0f},{:.0f}) cameraYaw={:.2f} mirrorYaw={:.4f} distance={:.0f} lateral={:.0f}",
						updates, position.x, position.y, position.z,
						playerPosition.x, playerPosition.y, playerPosition.z,
						headingYaw, yaw, state.previewDistance, state.previewLateral);
				}
				return true;
			} catch (...) {
				return false;
			}
		}

		void CancelLocked(bool notifyPlayer, const char* reason) noexcept
		{
			if (!State())
				return;
			auto& state = *State();
			logger::info(
				"[MOS][Placement] cancel style={} reason={} preview={:08X} lease={}",
				state.style, reason ? reason : "unspecified",
				state.preview ? state.preview->GetFormID() : 0,
				state.controls.acquired);
			DisposeReference(state.preview.get());
			state.preview.reset();
			RestoreControlLease(state);
			State().reset();
			g_counters.cancels.fetch_add(1, std::memory_order_relaxed);
			LogDiagnosticsUnlocked("cancel", false);
			if (notifyPlayer)
				Notify("Standing mirror placement cancelled; the kit remains in your inventory.");
		}

		[[nodiscard]] bool ConsumeInventoryItem(
			RE::PlayerCharacter& player, RE::TESObjectMISC& item) noexcept
		{
			std::int32_t before = 0;
			try {
				before = player.GetItemCount(&item);
				if (before < 1)
					return false;
				InternalInventoryMutationGuard mutation{};
				(void)player.RemoveItem(
					&item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			} catch (...) {
			}
			try {
				const auto after = player.GetItemCount(&item);
				if (after == before - 1)
					return true;
				if (after < before) {
					InternalInventoryMutationGuard mutation{};
					player.AddObjectToContainer(&item, nullptr, before - after, nullptr);
					if (player.GetItemCount(&item) != before) {
						logger::critical(
							"[MOS][Placement] inventory removal rollback failed for item {:08X}",
							item.GetFormID());
					}
				}
			} catch (...) {
				logger::critical(
					"[MOS][Placement] inventory removal verification faulted for item {:08X}",
					item.GetFormID());
			}
			return false;
		}

		[[nodiscard]] bool TransferDroppedItemToInventory(
			std::size_t style, const RE::ObjectRefHandle& handle) noexcept
		{
			auto dropped = handle.get();
			auto* const player = RE::PlayerCharacter::GetSingleton();
			auto* const item = ResolveInventoryItem(style);
			if (!dropped || !player || !item || !dropped->IsDynamicForm() ||
				dropped->GetObjectReference() != item) {
				return false;
			}
			try {
				const auto before = player->GetItemCount(item);
				{
					InternalInventoryMutationGuard mutation{};
					// Create and verify the replacement inventory unit before touching
					// the exact world reference reported by the drop event.  Passing the
					// dropped reference as a_fromRefr can consume it before verification.
					player->AddObjectToContainer(item, nullptr, 1, nullptr);
				}
				if (player->GetItemCount(item) != before + 1)
					return false;
				DisposeReference(dropped.get());
				if (dropped->IsDeleted() || dropped->IsMarkedForDeletion())
					return true;
				// Deleting the source failed.  Roll back only the replacement unit;
				// the original dropped kit remains in the world.
				if (!ConsumeInventoryItem(*player, *item) ||
					player->GetItemCount(item) != before) {
					logger::critical(
						"[MOS][Placement] dropped-kit rollback failed for ref {:08X}",
						dropped->GetFormID());
				}
				return false;
			} catch (...) {
				return false;
			}
		}

		[[nodiscard]] bool BeginPlacementMainThread(std::size_t style) noexcept
		{
			std::scoped_lock lock{ g_stateLock };
			const auto fail = [&]() noexcept {
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			};
			// Placement is driven by the camera pose and a confirm button, not by
			// the mouse, so VR can run the same flow: the headset supplies the
			// aim and the controller's activate binding supplies the confirm.
			if (!g_ready.load(std::memory_order_acquire) ||
				g_suspended.load(std::memory_order_acquire) ||
				style >= g_placedBases.size() || State()) {
				return fail();
			}

			auto* const player = RE::PlayerCharacter::GetSingleton();
			auto* const item = g_inventoryItems[style];
			auto* const placedBase = g_placedBases[style];
			if (!player || !player->GetParentCell() || !item || !placedBase)
				return fail();
			if (player->GetItemCount(item) < 1) {
				Notify("A standing mirror is required to begin placement.");
				return false;
			}

			auto* const previewBase = g_previewBases[style];
			if (!previewBase)
				return fail();
			auto preview = player->PlaceObjectAtMe(previewBase, false);
			if (!preview) {
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			try {
				preview->SetTemporary();
				preview->SetActivationBlocked(true);
				preview->SetCollision(false);
				(void)preview->SetMotionType(
					RE::hkpMotion::MotionType::kKeyframed, false);
			} catch (...) {
				DisposeReference(preview.get());
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			State() = PlacementState{
				.style = style,
				.inventoryItem = item,
				.placedBase = placedBase,
				.preview = std::move(preview),
				.originCell = player->GetParentCell(),
				.originWorldspace = player->GetWorldspace(),
				.rotationOffsetRadians = 0.0F,
				.previewDistance = StandingMirrorPlacementPolicy::kPreviewDistance,
				.vrConfirmation = g_vrTriggerHistory
			};
			(void)UpdatePreviewLocked();
			g_counters.previewsStarted.fetch_add(1, std::memory_order_relaxed);
			logger::info(
				"[MOS][Placement] begin style={} preview={:08X} awaitingMenuClose=true",
				style, State()->preview ? State()->preview->GetFormID() : 0);
			if (REL::Module::IsVR()) {
				Notify("Walk with the left stick; left/right slides the mirror, right stick turns it, trigger places it");
			}

			auto* const ui = RE::UI::GetSingleton();
			const bool inventoryOpen =
				ui && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
			const bool tweenOpen = ui && ui->IsMenuOpen(RE::TweenMenu::MENU_NAME);
			if (auto* queue = RE::UIMessageQueue::GetSingleton(); queue) {
				if (inventoryOpen) {
					queue->AddMessage(
						RE::InventoryMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide,
						nullptr);
				}
				// The InventoryMenu opens on top of the TweenMenu; hiding only the
				// former leaves the TweenMenu (and the game pause) in place — the
				// 03:23 owner run: placement live, world visible, player frozen.
				if (tweenOpen) {
					queue->AddMessage(
						RE::TweenMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
			}
			logger::info(
				"[MOS][Placement] menu close requested inventory={} tween={} itemMenus={} paused={}",
				inventoryOpen, tweenOpen, ui ? ui->IsItemMenuOpen() : false,
				ui ? ui->GameIsPaused() : false);
			return true;
		}

		void QueueBeginPlacement(std::size_t style) noexcept
		{
			if (g_suspended.load(std::memory_order_acquire) ||
				!g_ready.load(std::memory_order_acquire)) {
				return;
			}
			const auto generation =
				g_lifecycleGeneration.load(std::memory_order_acquire);
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([style, generation] {
					if (generation !=
						g_lifecycleGeneration.load(std::memory_order_acquire) ||
						g_suspended.load(std::memory_order_acquire)) {
						return;
					}
					if (!BeginPlacementMainThread(style))
						Notify("Standing mirror placement could not start.");
				});
			} else {
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void QueueDroppedPlacement(
			std::size_t style, RE::ObjectRefHandle droppedHandle) noexcept
		{
			if (g_suspended.load(std::memory_order_acquire) ||
				!g_ready.load(std::memory_order_acquire)) {
				return;
			}
			const auto generation =
				g_lifecycleGeneration.load(std::memory_order_acquire);
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([style, droppedHandle, generation] {
					if (generation !=
						g_lifecycleGeneration.load(std::memory_order_acquire) ||
						g_suspended.load(std::memory_order_acquire)) {
						return;
					}
					if (!TransferDroppedItemToInventory(style, droppedHandle)) {
						Notify("The dropped mirror kit was left in the world; placement did not start.");
						g_counters.failures.fetch_add(1, std::memory_order_relaxed);
						return;
					}
					g_counters.dropRequests.fetch_add(1, std::memory_order_relaxed);
					if (!BeginPlacementMainThread(style))
						Notify("The mirror kit was returned, but placement could not start.");
				});
			} else {
				g_counters.failures.fetch_add(1, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] bool ConfirmLocked() noexcept
		{
			if (!State() || !UpdatePreviewLocked())
				return false;
			auto& state = *State();
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player || !state.preview || !state.placedBase ||
				!state.inventoryItem || !StandingMirrorPlacementPolicy::SamePlacementLocation(
					reinterpret_cast<std::uintptr_t>(state.originCell),
					reinterpret_cast<std::uintptr_t>(state.originWorldspace),
					reinterpret_cast<std::uintptr_t>(player->GetParentCell()),
					reinterpret_cast<std::uintptr_t>(player->GetWorldspace()))) {
				return false;
			}

			const RE::NiPoint3 position = state.preview->GetPosition();
			const RE::NiPoint3 angle = state.preview->GetAngle();
			auto* const data = RE::TESDataHandler::GetSingleton();
			auto* const standCell = state.currentCell ? state.currentCell :
				state.originCell;
			if (!data || !standCell)
				return false;

			// Retire the translucent ghost before the real mirror exists so its
			// alpha-blended passes never share the mirror's first private capture
			// (owner run 2026-09-04 01:57: leftover passes on that first pass).
			DisposeReference(state.preview.get());
			state.preview.reset();

			// Stage and fully configure the persistent reference before consuming
			// the kit.  This makes inventory removal the transaction's final commit
			// point: every earlier failure leaves the player's inventory untouched.
			const auto placedHandle = data->CreateReferenceAtLocation(
				state.placedBase, position, angle, standCell,
				state.originWorldspace, nullptr, nullptr, RE::ObjectRefHandle{}, true,
				true);
			auto placed = placedHandle.get();
			if (!placed)
				return false;
			try {
				// Never clear collision on the committed reference. The identity
				// checks below are synchronous, so no frame boundary separates a
				// disable from its re-enable, but Skyrim latches the disabled state
				// on a reference whose 3D has not loaded yet: the later
				// SetCollision(true) is then a no-op and the mirror stays walk-
				// through for the rest of the session. Activation blocking alone
				// covers the validation window.
				placed->SetActivationBlocked(true);
				if (!placed->IsDynamicForm() ||
					placed->GetObjectReference() != state.placedBase ||
					placed->GetParentCell() != standCell) {
					DisposeReference(placed.get());
					return false;
				}
				placed->SetCollision(true);
				placed->SetActivationBlocked(false);
			} catch (...) {
				DisposeReference(placed.get());
				return false;
			}
			if (!ConsumeInventoryItem(*player, *state.inventoryItem)) {
				DisposeReference(placed.get());
				Notify("The standing mirror kit is no longer in your inventory.");
				return false;
			}

			if (!MirrorRecognition::ObserveCreatedReference(placed.get())) {
				logger::warn(
					"[MOS][Placement] placed ref {:08X} retained but immediate recognition is pending",
					placed->GetFormID());
			}
			const auto committedStyle = state.style;
			const auto committedFormID = placed->GetFormID();
			RestoreControlLease(state);
			State().reset();
			g_counters.confirms.fetch_add(1, std::memory_order_relaxed);
			logger::info(
				"[MOS][Placement] confirm committed style={} placed={:08X}",
				committedStyle, committedFormID);
			LogDiagnosticsUnlocked("confirm", false);
			return true;
		}

		std::atomic<std::uint32_t> g_pickupLogBudget{ 40 };

		void LogPickupOutcome(const char* outcome, const std::uint32_t formID) noexcept
		{
			auto budget = g_pickupLogBudget.load(std::memory_order_acquire);
			if (budget == 0 || !g_pickupLogBudget.compare_exchange_strong(
					budget, budget - 1, std::memory_order_acq_rel)) {
				return;
			}
			try {
				logger::info(
					"[MOS][Placement] activate: crosshair {} target={:08X}", outcome, formID);
			} catch (...) {
			}
		}

		[[nodiscard]] bool PickupPlacedMirrorReference(
			RE::NiPointer<RE::TESObjectREFR> target) noexcept
		{
			if (!target) {
				LogPickupOutcome("no-target", 0);
				return false;
			}
			// Fixed house/catalog references are world content, never inventory kits.
			// Only the persistent dynamic references created by ConfirmLocked may be
			// picked up and deleted through this route.
			if (!target->IsDynamicForm()) {
				LogPickupOutcome("not-dynamic", target->GetFormID());
				return false;
			}
			const auto style = StyleForPlacedMirrorBase(target->GetObjectReference());
			if (!style || *style >= g_inventoryItems.size()) {
				LogPickupOutcome("not-placed-mirror", target->GetFormID());
				return false;
			}
			auto* const player = RE::PlayerCharacter::GetSingleton();
			auto* const item = g_inventoryItems[*style];
			if (!player || !item)
				return false;
			auto* const controls = RE::ControlMap::GetSingleton();
			if (!controls || !controls->IsActivateControlsEnabled() ||
				!controls->IsMenuControlsEnabled()) {
				LogPickupOutcome("controls-disabled", target->GetFormID());
				return false;
			}
			try {
				const auto before = player->GetItemCount(item);
				{
					InternalInventoryMutationGuard mutation{};
					player->AddObjectToContainer(item, nullptr, 1, nullptr);
				}
				if (player->GetItemCount(item) != before + 1)
					return false;
				DisposeReference(target.get());
				if (!target->IsDeleted() && !target->IsMarkedForDeletion()) {
					const bool inventoryRolledBack =
						ConsumeInventoryItem(*player, *item);
					if (!inventoryRolledBack) {
						logger::critical(
							"[MOS][Placement] pickup rollback failed for ref {:08X}",
							target->GetFormID());
					}
					try {
						target->Enable(false);
						target->SetCollision(true);
						target->SetActivationBlocked(false);
					} catch (...) {
						logger::critical(
							"[MOS][Placement] pickup world-reference restore failed for ref {:08X}",
							target->GetFormID());
					}
					return false;
				}
				g_counters.pickups.fetch_add(1, std::memory_order_relaxed);
				Notify("Standing mirror picked up.");
				return true;
			} catch (...) {
				return false;
			}
		}

		[[nodiscard]] bool PickupCrosshairMirror() noexcept
		{
			// The engine never fills its crosshair activation target for a static
			// (V154 run: "no-target" on every activate over a placed mirror), so
			// fall back to the forward Havok ray.
			auto target = ReadFlatCrosshairTarget().get();
			if (!target) {
				if (auto* ahead = FindPlacedMirrorAhead())
					target = RE::NiPointer<RE::TESObjectREFR>{ ahead };
			}
			return PickupPlacedMirrorReference(target);
		}

		[[nodiscard]] bool SameEvent(
			const RE::BSFixedString& left, const RE::BSFixedString& right) noexcept
		{
			return left == right;
		}

		[[nodiscard]] bool MatchesMappedEvent(
			const RE::ButtonEvent& button,
			const RE::BSFixedString& expectedEvent) noexcept
		{
			if (SameEvent(button.GetUserEvent(), expectedEvent))
				return true;
			auto* const controls = RE::ControlMap::GetSingleton();
			if (!controls)
				return false;
			const auto device = button.GetDevice();
			if (device != RE::INPUT_DEVICE::kKeyboard &&
				device != RE::INPUT_DEVICE::kMouse &&
				device != RE::INPUT_DEVICE::kGamepad) {
				return false;
			}
			try {
				const auto mapped = controls->GetMappedKey(
					static_cast<std::string_view>(expectedEvent), device,
					RE::ControlMap::InputContextID::kGameplay);
				return StandingMirrorPlacementPolicy::MatchesMappedInput(
					button.GetIDCode(), mapped, RE::ControlMap::kInvalid);
			} catch (...) {
				return false;
			}
		}

		/**
		 * Either VR trigger confirms placement.  Matched by the controlmap's
		 * gamepad-column codes and, in case a custom map moved them, by the
		 * attack user events those codes carry (Fighting is leased off during
		 * placement, so the pull never reaches the attack handler).
		 */
		[[nodiscard]] bool IsVRTrigger(
			const RE::ButtonEvent& button, const RE::UserEvents* userEvents) noexcept
		{
			if (!REL::Module::IsVR())
				return false;
			const auto device = button.GetDevice();
			if (device == RE::INPUT_DEVICE::kKeyboard ||
				device == RE::INPUT_DEVICE::kMouse) {
				return false;
			}
			const auto id = button.GetIDCode();
			if (id == kVRLeftTrigger || id == kVRRightTrigger ||
				id == kOpenVRTrigger || id == kOpenVRAxis0) {
				return true;
			}
			return userEvents &&
				(SameEvent(button.GetUserEvent(), userEvents->leftAttack) ||
				 SameEvent(button.GetUserEvent(), userEvents->rightAttack));
		}

		/**
		 * VR trigger edge.  The trigger is analog and its first event can carry
		 * a non-zero hold time, so IsDown() is not a reliable edge; latch on the
		 * pull crossing kVRTriggerPressValue and release when it drops back.
		 * Returns true exactly once per pull.
		 */
		[[nodiscard]] bool VRTriggerPressedEdge(
			StandingMirrorPlacementPolicy::VRConfirmationGate& gate, const RE::ButtonEvent& button,
			const RE::UserEvents* userEvents, bool confirmationAllowed = true) noexcept
		{
			const bool controller = REL::Module::IsVR() &&
				button.GetDevice() != RE::INPUT_DEVICE::kKeyboard &&
				button.GetDevice() != RE::INPUT_DEVICE::kMouse;
			if (!controller || !(IsVRTrigger(button, userEvents) ||
				(userEvents && MatchesMappedEvent(button, userEvents->activate))))
				return false;
			return gate.Observe(
				static_cast<std::uint32_t>(button.GetDevice()), button.GetIDCode(),
				button.Value(), confirmationAllowed);
		}

		// First kVRButtonLogBudget button events of a live VR placement, so the
		// log shows what the controller actually sends for the trigger.
		void LogVRButtonEvent(
			PlacementState& state, const RE::ButtonEvent& button) noexcept
		{
			if (!REL::Module::IsVR() || state.vrButtonLogBudget == 0)
				return;
			--state.vrButtonLogBudget;
			try {
				logger::info(
					"[MOS][Placement] VR button device={} id={} event=\"{}\" value={:.2f} held={:.2f}",
					static_cast<int>(button.GetDevice()), button.GetIDCode(),
					button.GetUserEvent().c_str() ? button.GetUserEvent().c_str() : "",
					button.Value(), button.HeldDuration());
			} catch (...) {
			}
		}

		/**
		 * VR stick placement on top of follow mode.  Left stick: Y keeps
		 * walking the player (the preview keeps its offset on the bearing
		 * frozen at go-live) while X slides the preview across that bearing;
		 * X is cleared on the event before the
		 * game's movement handler sees it (this sink is prepended, so it runs
		 * first) so the player does not strafe with it.  Right stick: X turns
		 * the preview and Y changes the follow distance; both are cleared so
		 * the game neither snap-turns nor acts on the stick.  A sample
		 * integrates over the time since the previous one on the same stick,
		 * so the policy rates hold at any frame rate; a stick back at rest
		 * drops its clock so the next push starts from zero instead of a gap.
		 */
		void HandleStickLocked(
			PlacementState& state, RE::ThumbstickEvent& stick) noexcept
		{
			namespace Policy = StandingMirrorPlacementPolicy;
			const bool left = stick.IsLeft();
			if (!left && !stick.IsRight())
				return;
			const float x = Policy::StickDeflection(stick.xValue);
			const float y = left ? 0.0F : Policy::StickDeflection(stick.yValue);
			stick.xValue = 0.0F;
			if (!left)
				stick.yValue = 0.0F;
			auto& last = left ? state.lastLeftStickSample : state.lastRightStickSample;
			if (x == 0.0F && y == 0.0F) {
				last.reset();
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			const float seconds = last ?
				Policy::ClampStickDeltaSeconds(
					std::chrono::duration<float>(now - *last).count()) :
				0.0F;
			last = now;
			if (seconds <= 0.0F)
				return;
			if (left) {
				state.previewLateral = Policy::ClampPreviewLateral(
					state.previewLateral +
					x * Policy::kStickSlideUnitsPerSecond * seconds);
			} else {
				if (state.mirrorYawInitialized && x != 0.0F) {
					state.mirrorYaw = Policy::NormalizeRadians(
						state.mirrorYaw +
						x * Policy::kStickRotateRadiansPerSecond * seconds);
				}
				if (y != 0.0F) {
					state.previewDistance = Policy::ClampPreviewDistance(
						state.previewDistance +
						y * Policy::kStickDistanceUnitsPerSecond * seconds);
				}
			}
			const auto samples =
				g_counters.stickSamples.fetch_add(1, std::memory_order_relaxed) + 1;
			if (samples <= 3 || samples % 300 == 0) {
				logger::info(
					"[MOS][Placement] stick #{} {} x={:.2f} y={:.2f} dt={:.3f} distance={:.0f} lateral={:.0f} yaw={:.2f}",
					samples, left ? "left" : "right", x, y, seconds,
					state.previewDistance, state.previewLateral, state.mirrorYaw);
			}
			(void)UpdatePreviewLocked();
		}

		void ConsumePlacementButton(RE::ButtonEvent& button)
		{
			// kStop consumes the entire engine-owned linked batch, including unrelated
			// movement/block releases. Neutralize only the button placement owns.
			button.SetUserEvent(RE::BSFixedString{});
			button.SetIDCode(RE::ControlMap::kInvalid);
			button.GetRuntimeData().value = 0.0F;
			button.GetRuntimeData().heldDownSecs = 0.0F;
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink& GetSingleton()
			{
				static InputSink singleton{};
				return singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const* events,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!events || g_suspended.load(std::memory_order_acquire))
					return RE::BSEventNotifyControl::kContinue;

				if (const auto held = g_heldEquipMessageTick.load(std::memory_order_acquire);
					held != 0) {
					const auto now = static_cast<std::uint64_t>(GetTickCount64());
					const auto activation =
						g_lastActivationTick.load(std::memory_order_acquire);
					if (activation >= held) {
						g_heldEquipMessageTick.store(0, std::memory_order_release);
					} else if (now >= held + kHeldEquipMessageMs) {
						g_heldEquipMessageTick.store(0, std::memory_order_release);
						if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
							if (auto* setting = settings->GetSetting("sCantEquipGeneric")) {
								g_reemitEquipMessage.store(true, std::memory_order_release);
								Notify(setting->GetString());
							}
						}
					}
				}

				std::scoped_lock lock{ g_stateLock };
				// Remember inventory-selection presses even before Papyrus starts the
				// preview. This prevents auto-placement without swallowing the first
				// later pull from either controller. Read before consuming any event.
				for (auto* event = *events; event; event = event->next) {
					if (auto* button = event->AsButtonEvent())
						(void)VRTriggerPressedEdge(g_vrTriggerHistory, *button,
							RE::UserEvents::GetSingleton(), false);
				}
				auto* const ui = RE::UI::GetSingleton();
				if (State()) {
					const bool menuOpen = !ui || ui->GameIsPaused() ||
						ui->IsItemMenuOpen();
					if (State()->awaitingMenuClose || menuOpen) {
						// Observe releases even in batches consumed by menu/control
						// restoration, but never accept a press from that transition.
						for (auto* event = *events; event; event = event->next) {
							if (auto* button = event->AsButtonEvent())
								(void)VRTriggerPressedEdge(State()->vrConfirmation, *button,
									RE::UserEvents::GetSingleton(), false);
						}
					}
					if (menuOpen && State()->awaitingMenuClose) {
						// The plain hide request can leave SkyUI's InventoryMenu
						// open but invisible (owner 2026-09-04: inventory keys still
						// worked, the ghost appeared only after a manual close).
						auto& waiting = *State();
						if (++waiting.menuCloseWaitBatches >= kForceHideAfterBatches &&
							!waiting.forceHideSent) {
							waiting.forceHideSent = true;
							if (auto* queue = RE::UIMessageQueue::GetSingleton();
								queue && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
								queue->AddMessage(
									RE::InventoryMenu::MENU_NAME,
									RE::UI_MESSAGE_TYPE::kForceHide, nullptr);
								g_counters.forceHides.fetch_add(
									1, std::memory_order_relaxed);
								logger::info(
									"[MOS][Placement] InventoryMenu still open after {} batches; forced hide sent",
									waiting.menuCloseWaitBatches);
							}
						}
						return RE::BSEventNotifyControl::kContinue;
					}
					if (menuOpen) {
						// Live placement while a pausing menu is up: idle, never cancel
						// from here (re-opening cancels through MenuSink).  If it is our
						// own inventory/tween pair still lingering, push it closed.
						auto& live = *State();
						if (++live.menuCloseWaitBatches >= kForceHideAfterBatches &&
							!live.forceHideSent) {
							live.forceHideSent = true;
							if (auto* queue = RE::UIMessageQueue::GetSingleton(); queue) {
								for (const auto name : { RE::InventoryMenu::MENU_NAME,
										 RE::TweenMenu::MENU_NAME }) {
									if (ui->IsMenuOpen(name)) {
										queue->AddMessage(
											name, RE::UI_MESSAGE_TYPE::kForceHide, nullptr);
										g_counters.forceHides.fetch_add(
											1, std::memory_order_relaxed);
										logger::info(
											"[MOS][Placement] {} still open while live; forced hide sent",
											name);
									}
								}
							}
						}
						return RE::BSEventNotifyControl::kContinue;
					}
					if (State()->awaitingMenuClose) {
						// InventoryMenu restores vanilla control flags as it closes.  Lease
						// them only after all four flags are observably restored.  A closed
						// menu can precede that restoration by an input batch, so keep waiting
						// without taking ownership of a false flag.
						const auto lease = AcquireControlLease(*State());
						if (lease == ControlLeaseAcquireResult::kPending)
							return RE::BSEventNotifyControl::kContinue;
						if (lease == ControlLeaseAcquireResult::kFailed ||
							!UpdatePreviewLocked()) {
							CancelLocked(false, "control-acquire-failed");
							Notify("Standing mirror placement could not acquire player controls.");
						} else {
							State()->awaitingMenuClose = false;
						}
						return RE::BSEventNotifyControl::kContinue;
					}
					if (!UpdatePreviewLocked()) {
						CancelLocked(true, "preview-update-failed");
						return RE::BSEventNotifyControl::kContinue;
					}
				} else if (!ui || ui->GameIsPaused() || ui->IsItemMenuOpen()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto* const userEvents = RE::UserEvents::GetSingleton();
				for (auto* event = *events; event; event = event->next) {
					if (State() && !State()->awaitingMenuClose) {
						if (auto* stick = event->AsThumbstickEvent();
							stick && REL::Module::IsVR()) {
							HandleStickLocked(*State(), *stick);
							continue;
						}
						if (auto* move = event->AsMouseMoveEvent();
							move && State()->rotating && State()->mirrorYawInitialized) {
							State()->mirrorYaw =
								StandingMirrorPlacementPolicy::NormalizeRadians(
									State()->mirrorYaw +
									static_cast<float>(move->mouseInputX) *
										kMouseRotateRadiansPerUnit);
							(void)UpdatePreviewLocked();
							continue;
						}
						if (auto* mouseButton = event->AsButtonEvent();
							mouseButton && mouseButton->device == RE::INPUT_DEVICE::kMouse &&
							mouseButton->GetIDCode() == kMouseButtonRight) {
							if (mouseButton->IsDown()) {
								State()->rotating = true;
								SetLookLease(*State(), true);
							} else if (mouseButton->IsUp()) {
								State()->rotating = false;
								SetLookLease(*State(), false);
							}
							continue;
						}
					}
					auto* const button = event->AsButtonEvent();
					if (!button)
						continue;
					bool vrTriggerEdge = false;
					if (State() && !State()->awaitingMenuClose) {
						LogVRButtonEvent(*State(), *button);
						vrTriggerEdge = VRTriggerPressedEdge(State()->vrConfirmation, *button, userEvents);
					}
					if (!vrTriggerEdge && !button->IsDown())
						continue;
					const auto device = button->device;
					const auto id = button->GetIDCode();
					if (State()) {
						const bool confirm = vrTriggerEdge ||
							(device == RE::INPUT_DEVICE::kKeyboard &&
							 id == kKeyboardEnter) ||
							(device == RE::INPUT_DEVICE::kMouse &&
							 id == kMouseButtonLeft) ||
							(userEvents && MatchesMappedEvent(*button, userEvents->activate) &&
								(!REL::Module::IsVR() || device == RE::INPUT_DEVICE::kKeyboard ||
								 device == RE::INPUT_DEVICE::kMouse));
						const bool cancel =
							(device == RE::INPUT_DEVICE::kKeyboard &&
							 id == kKeyboardEscape) ||
							(userEvents &&
							 (MatchesMappedEvent(*button, userEvents->cancel) ||
							  MatchesMappedEvent(*button, userEvents->tweenMenu)));
						const bool rotateLeft =
							device == RE::INPUT_DEVICE::kKeyboard &&
							id == kKeyboardLeft;
						const bool rotateRight =
							device == RE::INPUT_DEVICE::kKeyboard &&
							id == kKeyboardRight;
						const bool nearer =
							(device == RE::INPUT_DEVICE::kKeyboard &&
							 id == kKeyboardDown) ||
							(device == RE::INPUT_DEVICE::kMouse &&
							 id == kMouseWheelDown);
						const bool farther =
							(device == RE::INPUT_DEVICE::kKeyboard &&
							 id == kKeyboardUp) ||
							(device == RE::INPUT_DEVICE::kMouse &&
							 id == kMouseWheelUp);

						if (confirm) {
							ConsumePlacementButton(*button);
							if (!ConfirmLocked()) {
								g_counters.confirmFailures.fetch_add(
									1, std::memory_order_relaxed);
								CancelLocked(false, "confirm-failed");
								Notify("That mirror position could not be confirmed; placement was cancelled and the kit remains in your inventory.");
							}
							return RE::BSEventNotifyControl::kContinue;
						}
						if (cancel) {
							ConsumePlacementButton(*button);
							CancelLocked(true, "player-cancelled");
							return RE::BSEventNotifyControl::kContinue;
						}
						if (rotateLeft || rotateRight) {
							ConsumePlacementButton(*button);
							State()->mirrorYaw =
								StandingMirrorPlacementPolicy::NormalizeRadians(
									State()->mirrorYaw +
									(rotateRight ?
										 StandingMirrorPlacementPolicy::kRotationStepRadians :
										-StandingMirrorPlacementPolicy::kRotationStepRadians));
							(void)UpdatePreviewLocked();
							g_counters.rotations.fetch_add(
								1, std::memory_order_relaxed);
							continue;
						}
						if (nearer || farther) {
							ConsumePlacementButton(*button);
							State()->previewDistance =
								StandingMirrorPlacementPolicy::ClampPreviewDistance(
									State()->previewDistance +
									(farther ?
										 StandingMirrorPlacementPolicy::kPreviewDistanceStep :
										-StandingMirrorPlacementPolicy::kPreviewDistanceStep));
							(void)UpdatePreviewLocked();
							g_counters.distanceChanges.fetch_add(
								1, std::memory_order_relaxed);
							continue;
						}
					} else if (userEvents &&
						MatchesMappedEvent(*button, userEvents->activate)) {
						if (PickupCrosshairMirror()) {
							ConsumePlacementButton(*button);
							return RE::BSEventNotifyControl::kContinue;
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// Vanilla activation of a placed mirror (E over the "Store Mirror" prompt)
		// stores it, independent of the input sink's key mapping.
		class ActivateSink final :
			public RE::BSTEventSink<RE::TESActivateEvent>
		{
		public:
			static ActivateSink& GetSingleton()
			{
				static ActivateSink singleton{};
				return singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESActivateEvent* event,
				RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (!event || g_suspended.load(std::memory_order_acquire) ||
					!g_ready.load(std::memory_order_acquire)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* const player = RE::PlayerCharacter::GetSingleton();
				if (!player || !event->actionRef || event->actionRef.get() != player ||
					!event->objectActivated) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto target = event->objectActivated;
				if (!target->IsDynamicForm() ||
					!StyleForPlacedMirrorBase(target->GetObjectReference())) {
					return RE::BSEventNotifyControl::kContinue;
				}
				LogPickupOutcome("activate-event", target->GetFormID());
				(void)PickupPlacedMirrorReference(target);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class ContainerSink final :
			public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			static ContainerSink& GetSingleton()
			{
				static ContainerSink singleton{};
				return singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESContainerChangedEvent* event,
				RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (!event || g_internalInventoryMutation.load(
						std::memory_order_acquire) ||
					g_suspended.load(std::memory_order_acquire) ||
					!g_ready.load(std::memory_order_acquire)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* const player = RE::PlayerCharacter::GetSingleton();
				if (!player || event->oldContainer != player->GetFormID() ||
					event->newContainer != 0 || event->itemCount != 1 ||
					!event->reference) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* const form = RE::TESForm::LookupByID(event->baseObj);
				const auto style = StyleForInventoryItem(form);
				if (!style)
					return RE::BSEventNotifyControl::kContinue;

				auto dropped = event->reference.get();
				if (!dropped || !dropped->IsDynamicForm() ||
					dropped->GetObjectReference() != form) {
					return RE::BSEventNotifyControl::kContinue;
				}
				QueueDroppedPlacement(*style, event->reference);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		/**
		 * InventoryMenu close event: start the placement lease the moment the
		 * menu really closes instead of waiting for the next input batch.
		 */
		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink& GetSingleton()
			{
				static MenuSink singleton{};
				return singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!event || g_suspended.load(std::memory_order_acquire))
					return RE::BSEventNotifyControl::kContinue;
				const bool inventory =
					event->menuName == RE::InventoryMenu::MENU_NAME;
				const bool tween = event->menuName == RE::TweenMenu::MENU_NAME;
				if (!inventory && !tween)
					return RE::BSEventNotifyControl::kContinue;
				std::scoped_lock lock{ g_stateLock };
				if (!State())
					return RE::BSEventNotifyControl::kContinue;
				if (event->opening) {
					if (!State()->awaitingMenuClose) {
						CancelLocked(true, "menu-reopened");
						return RE::BSEventNotifyControl::kContinue;
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				if (!inventory || !State()->awaitingMenuClose)
					return RE::BSEventNotifyControl::kContinue;
				g_counters.menuCloseEvents.fetch_add(1, std::memory_order_relaxed);
				const auto lease = AcquireControlLease(*State());
				if (lease == ControlLeaseAcquireResult::kAcquired &&
					UpdatePreviewLocked()) {
					State()->awaitingMenuClose = false;
					logger::info(
						"[MOS][Placement] inventory closed; placement live style={} waitBatches={}",
						State()->style, State()->menuCloseWaitBatches);
				} else if (lease == ControlLeaseAcquireResult::kFailed) {
					CancelLocked(false, "control-acquire-failed-on-close");
					Notify("Standing mirror placement could not acquire player controls.");
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		/**
		 * Using a MISC kit goes through Skyrim's equip path, which queues the
		 * "You cannot equip this item." notification before the kit script can
		 * run.  Drop exactly that notification when it belongs to a kit: either
		 * a kit activation was just requested or a kit is the selected
		 * InventoryMenu item.
		 */
		[[nodiscard]] bool SelectedInventoryItemIsKit() noexcept
		{
			try {
				auto* const ui = RE::UI::GetSingleton();
				if (!ui)
					return false;
				const auto menu = ui->GetMenu<RE::InventoryMenu>();
				if (!menu)
					return false;
				auto* const list = menu->GetRuntimeData().itemList;
				if (!list)
					return false;
				auto* const item = list->GetSelectedItem();
				if (!item || !item->data.objDesc)
					return false;
				return StyleForInventoryItem(item->data.objDesc->object)
					.has_value();
			} catch (...) {
				return false;
			}
		}

		std::atomic<std::uint32_t> g_hudFilterLogBudget{ 24 };

		[[nodiscard]] bool PlayerCarriesKit() noexcept
		{
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return false;
			try {
				for (auto* item : g_inventoryItems) {
					if (item && player->GetItemCount(item) > 0)
						return true;
				}
			} catch (...) {
			}
			return false;
		}

		[[nodiscard]] bool ShouldSuppressHudMessage(RE::UIMessage& message) noexcept
		{
			if (message.type != RE::UI_MESSAGE_TYPE::kUpdate || !message.data)
				return false;
			auto* const hud = static_cast<RE::HUDData*>(message.data);
			if (hud->type != RE::HUD_MESSAGE_TYPE::kNotification)
				return false;
			const char* text = hud->text.c_str();
			if (!text || !*text)
				return false;
			if (IsOwnNotification(text))
				return false;
			const char* cannotEquip = nullptr;
			if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
				if (auto* setting = settings->GetSetting("sCantEquipGeneric"))
					cannotEquip = setting->GetString();
			}
			const bool equipText =
				cannotEquip && std::string_view{ text } == cannotEquip;
			if (equipText && g_reemitEquipMessage.exchange(false, std::memory_order_acq_rel))
				return false;  // our deferred re-emit of a held vanilla message
			const auto now = static_cast<std::uint64_t>(GetTickCount64());
			const auto last = g_lastActivationTick.load(std::memory_order_acquire);
			const bool recentActivation =
				last != 0 && now >= last && now - last <= kEquipMessageSuppressWindowMs;
			const bool kitSelected = equipText && SelectedInventoryItemIsKit();
			// The vanilla equip refusal is queued ~70 ms before the kit script can
			// request placement, so with a kit in the inventory and the
			// InventoryMenu open it is held back; the input sink re-emits it after
			// kHeldEquipMessageMs if no activation follows.
			bool held = false;
			if (equipText && !recentActivation && !kitSelected) {
				auto* const ui = RE::UI::GetSingleton();
				if (ui && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) && PlayerCarriesKit()) {
					g_heldEquipMessageTick.store(now, std::memory_order_release);
					held = true;
				}
			}
			const bool suppress = (equipText && recentActivation) || kitSelected || held;
			auto budget = g_hudFilterLogBudget.load(std::memory_order_acquire);
			if ((recentActivation || equipText) && budget != 0 &&
				g_hudFilterLogBudget.compare_exchange_strong(
					budget, budget - 1, std::memory_order_acq_rel)) {
				logger::info(
					"[MOS][Placement] HUD notification {} text=\"{}\" equipText={} sinceActivationMs={} kitSelected={} held={}",
					suppress ? "suppressed" : "passed", text, equipText,
					last != 0 && now >= last ? now - last : 0, kitSelected, held);
			}
			return suppress;
		}

		struct HudMenuHook
		{
			static RE::UI_MESSAGE_RESULTS Thunk(
				RE::HUDMenu* menu, RE::UIMessage& message)
			{
				if (ShouldSuppressHudMessage(message)) {
					g_counters.equipMessagesSuppressed.fetch_add(
						1, std::memory_order_relaxed);
					return RE::UI_MESSAGE_RESULTS::kHandled;
				}
				return original(menu, message);
			}
			static inline REL::Relocation<decltype(&Thunk)> original;
		};

		void InstallHudHook() noexcept
		{
			if (g_hudHookInstalled.exchange(true, std::memory_order_acq_rel))
				return;
			try {
				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_HUDMenu[0] };
				HudMenuHook::original = vtable.write_vfunc(0x4, &HudMenuHook::Thunk);
				logger::info("[MOS][Placement] HUD notification filter installed");
			} catch (...) {
				g_hudHookInstalled.store(false, std::memory_order_release);
				logger::error("[MOS][Placement] HUD notification filter install failed");
			}
		}

		bool BeginPlacementNative(
			RE::StaticFunctionTag*, std::uint32_t inventoryItemFormID)
		{
			// V172 proved that both Form (VM type 0) and MiscObject (32) fail
			// CommonLib's type lookup in this runtime. A Papyrus Int preserves the
			// full FormID bits without native object-type lookup or unmarshalling.
			static_assert(RE::BSScript::GetRawType<std::uint32_t>{}() ==
				RE::BSScript::TypeInfo::RawType::kInt);
			if (!inventoryItemFormID || !g_ready.load(std::memory_order_acquire) ||
				g_suspended.load(std::memory_order_acquire)) {
				logger::warn(
					"[MOS][Placement] Papyrus BeginPlacement refused: item={:08X} ready={} suspended={}",
					inventoryItemFormID, g_ready.load(std::memory_order_acquire),
					g_suspended.load(std::memory_order_acquire));
				return false;
			}
			// Compare against the five already-resolved kit forms, including their
			// load-order bits. No arbitrary incoming form is looked up or trusted.
			std::optional<std::size_t> style;
			for (std::size_t index = 0; index < g_inventoryItems.size(); ++index) {
				const auto* item = g_inventoryItems[index];
				if (item && item->GetFormID() == inventoryItemFormID) {
					style = index;
					break;
				}
			}
			if (!style) {
				logger::warn(
					"[MOS][Placement] Papyrus BeginPlacement refused: item={:08X} is not one of the {} resolved kits",
					inventoryItemFormID, g_inventoryItems.size());
				return false;
			}
			logger::info(
				"[MOS][Placement] Papyrus BeginPlacement accepted: item={:08X} style={}",
				inventoryItemFormID, *style);
			g_lastActivationTick.store(
				static_cast<std::uint64_t>(GetTickCount64()),
				std::memory_order_release);
			g_counters.papyrusRequests.fetch_add(1, std::memory_order_relaxed);
			QueueBeginPlacement(*style);
			return true;
		}
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm)
	{
		if (!vm)
			return false;
		vm->RegisterFunction(
			"BeginPlacement", "MOSStandingMirrorNative", BeginPlacementNative);
		logger::info("[MOS][Placement] Papyrus MOSStandingMirrorNative.BeginPlacement registered");
		return true;
	}

	void OnInputLoaded()
	{
		if (g_inputSinkRegistered.exchange(true, std::memory_order_acq_rel))
			return;
		auto* const manager = RE::BSInputDeviceManager::GetSingleton();
		if (!manager) {
			g_inputSinkRegistered.store(false, std::memory_order_release);
			logger::error("[MOS][Placement] input manager unavailable");
			return;
		}
		manager->PrependEventSink(&InputSink::GetSingleton());
		logger::info("[MOS][Placement] input sink registered");
	}

	namespace
	{
		// Placed standing mirrors are statics, which show no crosshair prompt.
		// The activate-text virtual on the shared TESObjectSTAT vtable is
		// redirected for exactly the five placed bases so the HUD offers
		// "Store Mirror"; every other static keeps the original behaviour.
		struct PlacedMirrorActivateTextHook
		{
			static bool GetActivateText(
				RE::TESBoundObject* a_self,
				RE::TESObjectREFR* a_activator,
				RE::BSString& a_dst)
			{
				static std::atomic<std::uint64_t> calls{ 0 };
				static std::atomic<std::uint32_t> logBudget{ 12 };
				const auto call = calls.fetch_add(1, std::memory_order_relaxed) + 1;
				for (auto* placed : g_placedBases) {
					if (placed && placed == a_self) {
						auto budget = logBudget.load(std::memory_order_acquire);
						if (budget != 0 && logBudget.compare_exchange_strong(
								budget, budget - 1, std::memory_order_acq_rel)) {
							try {
								logger::info(
									"[MOS][Placement] activate text requested for placed base {:08X} (call {})",
									placed->GetFormID(), call);
							} catch (...) {
							}
						}
						a_dst = "Store Mirror";
						return true;
					}
				}
				if (call == 1 || (call % 5000) == 0) {
					try {
						logger::info(
							"[MOS][Placement] static activate text hook alive: calls={}", call);
					} catch (...) {
					}
				}
				return original(a_self, a_activator, a_dst);
			}
			static inline REL::Relocation<decltype(GetActivateText)> original{};
		};
		std::atomic_bool g_activateTextHookInstalled{ false };

		void InstallPlacedMirrorActivateTextHook() noexcept
		{
			if (g_activateTextHookInstalled.exchange(true, std::memory_order_acq_rel))
				return;
			try {
				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_TESObjectSTAT[0] };
				PlacedMirrorActivateTextHook::original = vtable.write_vfunc(
					0x4C, PlacedMirrorActivateTextHook::GetActivateText);
				logger::info("[MOS][Placement] placed-mirror activate text hook installed");
			} catch (...) {
				g_activateTextHookInstalled.store(false, std::memory_order_release);
				logger::error("[MOS][Placement] placed-mirror activate text hook failed");
			}
		}
	}

	void OnDataLoaded()
	{
		auto* const data = RE::TESDataHandler::GetSingleton();
		bool ready = data != nullptr;
		if (data) {
			for (std::size_t style = 0; style < g_placedBases.size(); ++style) {
				// V157: placed mirrors are activators so the engine shows the
				// native "E) Store Mirror" prompt and raises TESActivateEvent.
				g_placedBases[style] = data->LookupForm<RE::TESObjectACTI>(
					StandingMirrorPlacementPolicy::kPlacedStaticLocalFormIDs[style],
					kPluginName);
				g_inventoryItems[style] = data->LookupForm<RE::TESObjectMISC>(
					StandingMirrorPlacementPolicy::kInventoryLocalFormIDs[style],
					kPluginName);
				g_previewBases[style] = data->LookupForm<RE::TESObjectSTAT>(
					StandingMirrorPlacementPolicy::kPreviewStaticLocalFormIDs[style],
					kPluginName);
				ready = ready && g_placedBases[style] && g_inventoryItems[style] &&
					g_previewBases[style] &&
					MirrorRecognition::IsOwnedMirrorBase(g_placedBases[style]) &&
					!MirrorRecognition::IsOwnedMirrorBase(g_previewBases[style]);
			}
		}
		// The STAT activate-text hook is superseded by the ACTI records; keep
		// the code but never install it.
		(void)&InstallPlacedMirrorActivateTextHook;
		if (!g_containerSinkRegistered.exchange(true, std::memory_order_acq_rel)) {
			if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
				holder->AddEventSink(&ContainerSink::GetSingleton());
				holder->AddEventSink(&ActivateSink::GetSingleton());
			} else {
				g_containerSinkRegistered.store(false, std::memory_order_release);
			}
		}
		if (!g_menuSinkRegistered.exchange(true, std::memory_order_acq_rel)) {
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->AddEventSink(&MenuSink::GetSingleton());
			} else {
				g_menuSinkRegistered.store(false, std::memory_order_release);
			}
		}
		InstallHudHook();
		ready = ready && g_inputSinkRegistered.load(std::memory_order_acquire) &&
			g_containerSinkRegistered.load(std::memory_order_acquire);
		g_ready.store(ready, std::memory_order_release);
		logger::info(
			"[MOS][Placement] data ready={} forms={} inputSink={} containerSink={} externalDependencies=false",
			ready, g_placedBases.size(),
			g_inputSinkRegistered.load(std::memory_order_acquire),
			g_containerSinkRegistered.load(std::memory_order_acquire));
	}

	void OnGameLoaded() noexcept
	{
		g_lifecycleGeneration.fetch_add(1, std::memory_order_acq_rel);
		{
			std::scoped_lock lock{ g_stateLock };
			if (State()) {
				DisposeReference(State()->preview.get());
				RestoreControlLease(*State());
				State().reset();
			}
		}
		g_suspended.store(false, std::memory_order_release);
	}

	void OnPreLoadGame() noexcept
	{
		g_suspended.store(true, std::memory_order_release);
		g_lifecycleGeneration.fetch_add(1, std::memory_order_acq_rel);
		std::scoped_lock lock{ g_stateLock };
		if (State())
			CancelLocked(false, "pre-load");
	}

	namespace
	{
		void LogDiagnosticsUnlocked(const char* reason, bool active) noexcept
		{
		try {
		logger::info(
			"[MOS][Placement] {} ready={} suspended={} active={} requests(papyrus/drop)={}/{} previews(start/update)={}/{} floor(probes/hits)={}/{} distance={} cameraYaw(reads/fallbacks)={}/{} menu(closeEvents/forceHides/equipMessagesSuppressed)={}/{}/{} rotate={} stick={} confirm(success/failure)={}/{} cancel={} pickup={} controls(wait/acquire/restore)={}/{}/{} failures={} externalDependencies=false",
			reason ? reason : "diagnostics",
			g_ready.load(std::memory_order_acquire),
			g_suspended.load(std::memory_order_acquire), active,
			g_counters.papyrusRequests.load(std::memory_order_relaxed),
			g_counters.dropRequests.load(std::memory_order_relaxed),
			g_counters.previewsStarted.load(std::memory_order_relaxed),
			g_counters.previewUpdates.load(std::memory_order_relaxed),
			g_counters.floorProbes.load(std::memory_order_relaxed),
			g_counters.floorProbeHits.load(std::memory_order_relaxed),
			g_counters.distanceChanges.load(std::memory_order_relaxed),
			g_counters.cameraYawReads.load(std::memory_order_relaxed),
			g_counters.cameraYawFallbacks.load(std::memory_order_relaxed),
			g_counters.menuCloseEvents.load(std::memory_order_relaxed),
			g_counters.forceHides.load(std::memory_order_relaxed),
			g_counters.equipMessagesSuppressed.load(std::memory_order_relaxed),
			g_counters.rotations.load(std::memory_order_relaxed),
			g_counters.stickSamples.load(std::memory_order_relaxed),
			g_counters.confirms.load(std::memory_order_relaxed),
			g_counters.confirmFailures.load(std::memory_order_relaxed),
			g_counters.cancels.load(std::memory_order_relaxed),
			g_counters.pickups.load(std::memory_order_relaxed),
			g_counters.controlWaits.load(std::memory_order_relaxed),
			g_counters.controlLeases.load(std::memory_order_relaxed),
			g_counters.controlRestores.load(std::memory_order_relaxed),
			g_counters.failures.load(std::memory_order_relaxed));
		} catch (...) {
		}
		}
	}

	void LogDiagnostics(const char* reason)
	{
		bool active = false;
		{
			std::scoped_lock lock{ g_stateLock };
			active = State().has_value();
		}
		LogDiagnosticsUnlocked(reason, active);
	}
}
