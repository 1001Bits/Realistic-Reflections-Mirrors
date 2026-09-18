#include "PCH.h"

#include "LeaseRegistry.h"
#include "MirrorsOfSkyrimPlayerInclusion.h"
#include "MirrorPlayerRollbackOwnershipPolicy.h"
#include "MirrorActivation.h"

#include <cstring>

namespace MirrorPlayerInclusion
{
	namespace
	{
		constexpr std::size_t kMaximumParentDepth = 256;
		// Flat SE/AE BSFadeNode alpha (verified in the Load3D audit:
		// SE_First_Person_Player_Visibility.md initializes it to 1.0). Skyrim's
		// near-camera actor fade writes it below 1.0, which leaked a see-through
		// body into the reflection (2026-08-07 owner report). The private pass
		// leases it to fully opaque and restores the exact prior value.
		constexpr std::ptrdiff_t kFadeNodeAlphaOffset = 0x130;

		struct Counters
		{
			std::atomic<std::uint64_t> attempts{ 0 };
			std::atomic<std::uint64_t> missingRoots{ 0 };
			std::atomic<std::uint64_t> coveredByWorldRoot{ 0 };
			std::atomic<std::uint64_t> separateRoots{ 0 };
			std::atomic<std::uint64_t> handMirrorForcedSeparateRoots{ 0 };
			std::atomic<std::uint64_t> handWorldCullExclusionBegins{ 0 };
			std::atomic<std::uint64_t> mirrorWorldCullExclusionBegins{ 0 };
			std::atomic<std::uint64_t> handWorldCullExclusionRestores{ 0 };
			std::atomic<std::uint64_t> mirrorWorldCullExclusionRestores{ 0 };
			std::atomic<std::uint64_t> handWorldCullExclusionFailures{ 0 };
			std::atomic<std::uint64_t> mirrorWorldCullExclusionFailures{ 0 };
			std::atomic<std::uint64_t> priorCulled{ 0 };
			std::atomic<std::uint64_t> priorVisible{ 0 };
			std::atomic<std::uint64_t> preparationFailures{ 0 };
			std::atomic<std::uint64_t> restores{ 0 };
			std::atomic<std::uint64_t> restoreFailures{ 0 };
			std::atomic<std::uint64_t> rollbackQueued{ 0 };
			std::atomic<std::uint64_t> rollbackRecovered{ 0 };
			std::atomic<std::uint64_t> drawEvidenceFrames{ 0 };
			std::atomic<std::uint64_t> drawEvidenceWithPlayer{ 0 };
			std::atomic<std::uint64_t> drawEvidenceWithSkinnedPlayer{ 0 };
			std::atomic<std::uint64_t> drawEvidenceZeroSkinned{ 0 };
			std::atomic<std::uint64_t> drawEvidenceTargetFaults{ 0 };
			std::atomic<std::uint64_t> drawEvidenceNoDispatch{ 0 };
			std::atomic<std::uint64_t> drawProbeInstallFailures{ 0 };
			std::atomic<std::uint64_t> poseSamples{ 0 };
			std::atomic<std::uint64_t> poseChangedSamples{ 0 };
			std::atomic<std::uint64_t> poseFirstPersonSamples{ 0 };
			std::atomic<std::uint64_t> poseFirstPersonChanged{ 0 };
			std::atomic<std::uint64_t> poseThirdPersonSamples{ 0 };
			std::atomic<std::uint64_t> poseThirdPersonChanged{ 0 };
			std::atomic<std::uint64_t> poseSampleFaults{ 0 };
			std::atomic<std::uint64_t> fadeLeases{ 0 };
			std::atomic<std::uint64_t> fadeRestoreFailures{ 0 };
			std::atomic<std::uint64_t> fadeReapplies{ 0 };
			std::atomic<std::uint64_t> fadeReapplyFailures{ 0 };
			std::atomic<std::uint64_t> exceptions{ 0 };
		};

		struct PendingRollback
		{
			RE::NiAVObject* root{ nullptr };
			bool priorAppCulled{ false };
			bool appCullPending{ false };
			bool fadePending{ false };
			float priorFadeAlpha{ 1.0f };
			bool pending{ false };
			bool ownsReference{ false };
		};

		Counters g_counters{};
		std::atomic<unsigned long> g_lastException{ 0 };
		std::atomic_bool g_loggedCoverage{ false };
		// F4-POSEPROOF-style motion telemetry: F4's stiff-doll reflection was only
		// diagnosable once pose hashes existed (its clone republished a constant
		// pose while the live tree animated). Skyrim is expected to differ — the
		// hidden 3P body should stay animated in first person because UpdateAnimation
		// visits both bipeds — but that is exactly the claim this proves or refutes
		// on the first player-included run.
		std::atomic<std::uint64_t> g_lastPoseHash{ 0 };
		std::atomic_bool g_lastPoseValid{ false };
		std::atomic<std::uint32_t> g_poseStillStreak{ 0 };
		std::atomic<std::uint32_t> g_poseMaxStillStreak{ 0 };
		std::atomic_bool g_loggedPoseLive{ false };
		std::atomic_bool g_loggedPoseFrozen{ false };
		constexpr std::uint32_t kMaxPoseNodes = 96;
		constexpr std::uint32_t kFrozenPoseStreakWarning = 30;
		std::atomic_bool g_requested{ false };
		std::atomic_bool g_enabled{ false };
		std::atomic_bool g_handMirrorRequested{ false };
		std::atomic_bool g_handMirrorEnabled{ false };
		std::atomic_bool g_playerLeaseArmed{ false };
		SRWLOCK g_rollbackLock = SRWLOCK_INIT;
		PendingRollback g_pendingRollback{};
		MirrorPlayerRollbackOwnershipPolicy::State g_rollbackOwnership{};

		[[nodiscard]] int RecordException(unsigned long code) noexcept
		{
			g_lastException.store(code, std::memory_order_relaxed);
			g_counters.exceptions.fetch_add(1, std::memory_order_relaxed);
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[nodiscard]] bool IsExactSupportedRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			return (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
			       (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			       SupportedRuntimePolicy::IsExactVRRuntime();
		}

		[[nodiscard]] bool EnsurePlayerLeaseArmed() noexcept
		{
			if (g_playerLeaseArmed.load(std::memory_order_acquire))
				return true;
			if (!LeaseRegistry::TryArm({ .name = "M3PlayerInclusion",
					.family = LeaseRegistry::Family::kPlayerRoot,
					.maturity = LeaseRegistry::Maturity::kRuntimeProven })) {
				return false;
			}
			g_playerLeaseArmed.store(true, std::memory_order_release);
			return true;
		}

		[[nodiscard]] bool TryGetBodyRoot(RE::NiAVObject*& output) noexcept
		{
			output = nullptr;
			__try {
				if (auto* player = RE::PlayerCharacter::GetSingleton())
					output = player->Get3D1(false);
				return true;
			} __except (RecordException(GetExceptionCode())) {
				output = nullptr;
				return false;
			}
		}

		[[nodiscard]] bool RetainRoot(RE::NiAVObject* root) noexcept
		{
			if (!root)
				return false;
			__try {
				root->IncRefCount();
				return true;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool ReleaseRoot(RE::NiAVObject* root) noexcept
		{
			if (!root)
				return true;
			__try {
				root->DecRefCount();
				return true;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool ReadAppCull(
			RE::NiAVObject* root,
			bool& output) noexcept
		{
			__try {
				output = root->GetAppCulled();
				return true;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool SetAndVerifyAppCull(
			RE::NiAVObject* root,
			bool value) noexcept
		{
			__try {
				root->SetAppCulled(value);
				return root->GetAppCulled() == value;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool ReadFadeAlpha(
			RE::NiAVObject* root,
			float& output) noexcept
		{
			__try {
				std::memcpy(
					&output,
					reinterpret_cast<const std::byte*>(root) + kFadeNodeAlphaOffset,
					sizeof(output));
				return true;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool WriteAndVerifyFadeAlpha(
			RE::NiAVObject* root,
			float value) noexcept
		{
			__try {
				std::memcpy(
					reinterpret_cast<std::byte*>(root) + kFadeNodeAlphaOffset,
					&value, sizeof(value));
				float readBack = 0.0f;
				std::memcpy(
					&readBack,
					reinterpret_cast<const std::byte*>(root) + kFadeNodeAlphaOffset,
					sizeof(readBack));
				return readBack == value;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool TryRecoverPendingRollback() noexcept
		{
			AcquireSRWLockExclusive(std::addressof(g_rollbackLock));
			if (!g_pendingRollback.pending) {
				const bool coherent =
					!g_rollbackOwnership.recoveryPending &&
					!g_rollbackOwnership.ownsReference &&
					g_rollbackOwnership.retainedRootIdentity == 0 &&
					!g_rollbackOwnership.terminal;
				ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
				return coherent;
			}
			if (!g_pendingRollback.root || !g_pendingRollback.ownsReference ||
				(!g_pendingRollback.appCullPending &&
					!g_pendingRollback.fadePending) ||
				(g_pendingRollback.fadePending &&
					!std::isfinite(g_pendingRollback.priorFadeAlpha)) ||
				!MirrorPlayerRollbackOwnershipPolicy::IsExactPendingRecovery(
					g_rollbackOwnership,
					reinterpret_cast<std::uintptr_t>(g_pendingRollback.root),
					g_pendingRollback.priorAppCulled)) {
				ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
				g_counters.restoreFailures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			bool restored = true;
			if (g_pendingRollback.fadePending) {
				if (WriteAndVerifyFadeAlpha(
						g_pendingRollback.root,
						g_pendingRollback.priorFadeAlpha)) {
					g_pendingRollback.fadePending = false;
				} else {
					g_counters.fadeRestoreFailures.fetch_add(
						1, std::memory_order_relaxed);
					restored = false;
				}
			}
			if (g_pendingRollback.appCullPending) {
				if (SetAndVerifyAppCull(
						g_pendingRollback.root,
						g_pendingRollback.priorAppCulled)) {
					g_pendingRollback.appCullPending = false;
				} else {
					g_counters.restoreFailures.fetch_add(
						1, std::memory_order_relaxed);
					restored = false;
				}
			}
			if (!restored || g_pendingRollback.fadePending ||
				g_pendingRollback.appCullPending) {
				ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
				return false;
			}

			// Tombstone every value identity before invoking the guarded native
			// DecRef. Both engine mutations have already read back exactly; a DecRef
			// fault is therefore terminal reference-management failure, not a reason
			// to replay either rollback write or double-release the root.
			auto* recoveredRoot = g_pendingRollback.root;
			const bool recoveredPrior = g_pendingRollback.priorAppCulled;
			const auto tombstone =
				MirrorPlayerRollbackOwnershipPolicy::TombstoneAfterExactReadback(
					g_rollbackOwnership,
					reinterpret_cast<std::uintptr_t>(recoveredRoot), recoveredPrior);
			if (tombstone != MirrorPlayerRollbackOwnershipPolicy::Status::kAccepted) {
				ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
				return false;
			}
			g_pendingRollback = {};
			ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));

			const bool released = ReleaseRoot(recoveredRoot);
			AcquireSRWLockExclusive(std::addressof(g_rollbackLock));
			const auto releaseStatus =
				MirrorPlayerRollbackOwnershipPolicy::CompleteReferenceRelease(
					g_rollbackOwnership, released);
			ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
			g_counters.rollbackRecovered.fetch_add(1, std::memory_order_relaxed);
			try {
				logger::critical(
					"[MirrorsOfSkyrim][Player] recovered a previously failed player-state rollback; "
					"the exact retained root identity was tombstoned after readback");
			} catch (...) {
				// Recovery and ownership are already resolved; diagnostics are optional.
			}
			return released &&
				releaseStatus == MirrorPlayerRollbackOwnershipPolicy::Status::kAccepted;
		}

		[[nodiscard]] bool QueueFailedRollback(
			RE::NiAVObject* root,
			bool priorAppCulled,
			bool appCullPending,
			bool fadePending,
			float priorFadeAlpha,
			bool ownsRollbackLease) noexcept
		{
			if (!root || !ownsRollbackLease ||
				(!appCullPending && !fadePending) ||
				(fadePending && !std::isfinite(priorFadeAlpha))) {
				return false;
			}

			AcquireSRWLockExclusive(std::addressof(g_rollbackLock));
			const auto transfer =
				MirrorPlayerRollbackOwnershipPolicy::TransferFailedRollback(
					g_rollbackOwnership, reinterpret_cast<std::uintptr_t>(root),
					priorAppCulled);
			const bool accepted =
				transfer == MirrorPlayerRollbackOwnershipPolicy::Status::kAccepted;
			if (accepted) {
				g_pendingRollback.root = root;
				g_pendingRollback.priorAppCulled = priorAppCulled;
				g_pendingRollback.appCullPending = appCullPending;
				g_pendingRollback.fadePending = fadePending;
				g_pendingRollback.priorFadeAlpha = priorFadeAlpha;
				g_pendingRollback.pending = true;
				g_pendingRollback.ownsReference = true;
			}
			ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
			if (accepted) {
				g_counters.rollbackQueued.fetch_add(1, std::memory_order_relaxed);
			}
			return accepted;
		}

		[[nodiscard]] bool DetermineCoverage(
			RE::NiAVObject* playerRoot,
			RE::NiAVObject* worldSceneRoot,
			bool& covered) noexcept
		{
			covered = false;
			if (!playerRoot || !worldSceneRoot)
				return false;

			__try {
				auto* cursor = playerRoot;
				for (std::size_t depth = 0; depth < kMaximumParentDepth; ++depth) {
					if (cursor == worldSceneRoot) {
						covered = true;
						return true;
					}
					auto* parent = cursor->parent;
					if (!parent)
						return true;
					if (parent == cursor)
						return false;
					cursor = parent;
				}
				// A cyclic/corrupt parent chain must never cause a guessed duplicate cull.
				return false;
			} __except (RecordException(GetExceptionCode())) {
				return false;
			}
		}

		[[nodiscard]] bool HashBodyPoseRaw(
			RE::NiAVObject* root,
			std::uint64_t& hash,
			std::uint32_t& visited)
		{
			constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
			constexpr std::uint64_t kFnvPrime = 0x100000001B3ull;
			hash = kFnvOffset;
			visited = 0;
			if (!root)
				return false;

			RE::NiAVObject* queue[kMaxPoseNodes] = {};
			std::uint32_t head = 0;
			std::uint32_t tail = 0;
			queue[tail++] = root;
			const auto mix = [&hash](const void* data, std::size_t size) noexcept {
				const auto* bytes = static_cast<const unsigned char*>(data);
				for (std::size_t i = 0; i < size; ++i) {
					hash ^= bytes[i];
					hash *= kFnvPrime;
				}
			};
			while (head < tail) {
				auto* object = queue[head++];
				if (!object)
					continue;
				++visited;
				// Hash LOCAL transforms and skip the root's own transform: world
				// transforms (and the root's local) embed actor root motion, which
				// would report a frozen skeleton as "live" whenever the player
				// merely walks or turns. Bone locals change only when the
				// animation graph actually writes a pose — the discriminator this
				// telemetry exists for.
				if (object != root) {
					mix(&object->local.rotate, sizeof(object->local.rotate));
					mix(&object->local.translate, sizeof(object->local.translate));
				}
				if (auto* node = object->AsNode()) {
					auto& children = node->GetChildren();
					for (auto& child : children) {
						if (tail >= kMaxPoseNodes)
							break;
						if (child)
							queue[tail++] = child.get();
					}
				}
			}
			mix(&visited, sizeof(visited));
			return visited != 0;
		}

		[[nodiscard]] bool ReadThirdPersonFlagRaw(bool& thirdPerson)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return false;
			thirdPerson =
				player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode;
			return true;
		}

		[[nodiscard]] bool SamplePoseMotionSEH(RE::NiAVObject* root) noexcept
		{
			std::uint64_t hash = 0;
			std::uint32_t visited = 0;
			bool thirdPerson = false;
			bool sampled = false;
			bool perspectiveKnown = false;
			__try {
				sampled = HashBodyPoseRaw(root, hash, visited);
				perspectiveKnown = ReadThirdPersonFlagRaw(thirdPerson);
			} __except (RecordException(GetExceptionCode())) {
				sampled = false;
			}
			if (!sampled || !perspectiveKnown) {
				g_counters.poseSampleFaults.fetch_add(1, std::memory_order_relaxed);
				g_lastPoseValid.store(false, std::memory_order_release);
				return false;
			}

			g_counters.poseSamples.fetch_add(1, std::memory_order_relaxed);
			if (thirdPerson)
				g_counters.poseThirdPersonSamples.fetch_add(
					1, std::memory_order_relaxed);
			else
				g_counters.poseFirstPersonSamples.fetch_add(
					1, std::memory_order_relaxed);

			const bool hadPrior = g_lastPoseValid.load(std::memory_order_acquire);
			const std::uint64_t prior =
				g_lastPoseHash.exchange(hash, std::memory_order_acq_rel);
			g_lastPoseValid.store(true, std::memory_order_release);
			if (!hadPrior)
				return true;

			if (prior != hash) {
				g_counters.poseChangedSamples.fetch_add(1, std::memory_order_relaxed);
				if (thirdPerson)
					g_counters.poseThirdPersonChanged.fetch_add(
						1, std::memory_order_relaxed);
				else
					g_counters.poseFirstPersonChanged.fetch_add(
						1, std::memory_order_relaxed);
				g_poseStillStreak.store(0, std::memory_order_release);
				if (!g_loggedPoseLive.exchange(true, std::memory_order_relaxed)) {
					logger::info(
						"[MirrorsOfSkyrim][Player] submitted body pose is live (changed between "
						"captures; perspective={})",
						thirdPerson ? "third-person" : "first-person");
				}
				return true;
			}

			const std::uint32_t streak =
				g_poseStillStreak.fetch_add(1, std::memory_order_acq_rel) + 1;
			std::uint32_t maxStreak =
				g_poseMaxStillStreak.load(std::memory_order_relaxed);
			while (streak > maxStreak &&
			       !g_poseMaxStillStreak.compare_exchange_weak(
					   maxStreak, streak, std::memory_order_relaxed)) {
			}
			if (streak >= kFrozenPoseStreakWarning && !thirdPerson &&
				!g_loggedPoseFrozen.exchange(true, std::memory_order_relaxed)) {
				logger::warn(
					"[MirrorsOfSkyrim][Player] submitted body pose has been IDENTICAL for {} "
					"consecutive captures in first person — F4-dormant-graph "
					"signature; the mirror body would look like a frozen doll. "
					"Reverse-engineer Skyrim's own 1P pose path before porting any "
					"F4 clone/subgraph mechanism",
					streak);
			}
			return true;
		}
	}

	Scope::Scope(
		RE::NiAVObject* worldSceneRoot,
		ID3D11RenderTargetView* expectedRenderTarget,
		PlayerDrawPassProbe::CaptureKind a_captureKind,
		RE::NiAVObject* expectedBodyRoot) noexcept :
		captureKind(a_captureKind)
	{
		const bool enabled = g_enabled.load(std::memory_order_acquire) ||
			(captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror &&
				g_handMirrorEnabled.load(std::memory_order_acquire));
		if (!enabled) {
			coverage = Coverage::kDisabled;
			return;
		}
		g_counters.attempts.fetch_add(1, std::memory_order_relaxed);
		if (!TryRecoverPendingRollback()) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!IsExactSupportedRuntime()) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kUnsupported;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!worldSceneRoot) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		// The equipped-hand biped root proves the mirror item's identity, but it is
		// not Skyrim's renderable player body root.  V25 live evidence showed that it
		// emitted only two non-skinned item descendants and zero face/body draws.
		// Always capture the documented third-person actor/body root.  The exact hand
		// path can additionally supply a synchronously retained expected render root;
		// a clean identity change rejects that frame before any root mutation.
		RE::NiAVObject* root = nullptr;
		if (!TryGetBodyRoot(root)) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!root) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kNoPlayerRoot;
			g_counters.missingRoots.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror &&
			(!expectedBodyRoot || root != expectedBodyRoot)) {
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		AcquireSRWLockExclusive(std::addressof(g_rollbackLock));
		const auto rollbackOwnerStatus =
			MirrorPlayerRollbackOwnershipPolicy::TryBeginScope(
				g_rollbackOwnership);
		ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
		if (rollbackOwnerStatus !=
			MirrorPlayerRollbackOwnershipPolicy::Status::kAccepted) {
			// A live scope or unresolved capacity-one recovery already owns the only
			// player-root mutation domain.  Never begin another mutation beside it.
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		rollbackOwnerLease = true;
		if (!RetainRoot(root)) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		playerRoot = root;
		retained = true;
		retainedRootIdentity = reinterpret_cast<std::uintptr_t>(root);
		expectedRenderTargetIdentity =
			reinterpret_cast<std::uintptr_t>(expectedRenderTarget);

		if (!ReadAppCull(playerRoot, priorAppCulled)) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!SetAndVerifyAppCull(playerRoot, false)) {
			nativeFaulted =
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			// The prior value is known. Treat this as scoped until an exact rollback
			// succeeds, even when the failed operation changed the bit before faulting.
			appCullScoped = true;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			(void) Restore();
			return;
		}
		appCullScoped = true;
		if (priorAppCulled)
			g_counters.priorCulled.fetch_add(1, std::memory_order_relaxed);
		else
			g_counters.priorVisible.fetch_add(1, std::memory_order_relaxed);

		// Near-camera actor fade lease: force the body fully opaque for the
		// private pass only. A failed read/write is nonfatal (cosmetic fade may
		// leak into the reflection); an applied lease must restore exactly.
		float currentFade = 1.0f;
		if (ReadFadeAlpha(playerRoot, currentFade) &&
			std::isfinite(currentFade) && currentFade >= 0.0f &&
			currentFade < 1.0f) {
			if (WriteAndVerifyFadeAlpha(playerRoot, 1.0f)) {
				priorFadeAlpha = currentFade;
				fadeScoped = true;
				g_counters.fadeLeases.fetch_add(1, std::memory_order_relaxed);
			}
		}

		bool covered = false;
		if (captureKind != PlayerDrawPassProbe::CaptureKind::kHandMirror &&
			!DetermineCoverage(playerRoot, worldSceneRoot, covered)) {
			nativeFaulted =
				nativeFaulted ||
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
			coverage = Coverage::kFailed;
			g_counters.preparationFailures.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		// The equipped mirror is rendered in a separate direct cull, and so is the
		// wall route now.
		//
		// This used to read "the wall route uses the proven world-root ancestry
		// when available". Run 3 proves it was never ancestry that was in
		// question: being a descendant of the world scene root is not evidence
		// that *this* capture's descriptor traversed the player branch. Owner,
		// 2026-09-16: "it was reflecting the player and then suddenly stopped",
		// and the [MirrorsOfSkyrim][Player] counters name this branch 1:1 across
		// fourteen consecutive periods --
		//
		//     covered+536 -> zeroSkinned+536    separate+0   -> skinned+0
		//     covered+504 -> zeroSkinned+504    separate+504 -> skinned+504
		//
		// every capture taking the shortcut drew zero skinned geometry, and for
		// three periods around 05:17 every frame took it, so the player left the
		// mirror entirely. The equipped route already culls the retained root for
		// the same reason; the wall route was the last one trusting ancestry.
		//
		// DetermineCoverage still runs and still fails the capture when the graph
		// cannot be read: that is an unreadable scene, not a relocated player.
		// Only its shortcut is no longer trusted.
		const bool forceSeparateHandMirrorCull =
			captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror;
		if (forceSeparateHandMirrorCull) {
			g_counters.handMirrorForcedSeparateRoots.fetch_add(
				1, std::memory_order_relaxed);
		}
		// Unconditional: no capture kind takes the ancestry shortcut any more.
		coverage = Coverage::kSeparateRoot;
		g_counters.separateRoots.fetch_add(1, std::memory_order_relaxed);

		if (!g_loggedCoverage.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[MirrorsOfSkyrim][Player] body root retained and un-culled; coverage={}",
				forceSeparateHandMirrorCull ? "hand-mirror-forced-separate-native-cull" :
				(covered ? "mirror-forced-separate-native-cull (world-scene "
					"descendant, but descent is not proof of traversal)" :
					"separate-native-cull"));
		}
		// The full first-person-hidden actor graph is the render root, not a bounded
		// telemetry tree. V26 froze before entering the private pass while the
		// diagnostic pose hash walked this graph; hand rendering does not consume it.
		if (captureKind != PlayerDrawPassProbe::CaptureKind::kHandMirror)
			(void)SamplePoseMotionSEH(playerRoot);
		drawProbeArmed = PlayerDrawPassProbe::Begin(
				drawProbe, captureKind,
				playerRoot, expectedRenderTarget);
		if (!drawProbeArmed) {
			if (captureKind != PlayerDrawPassProbe::CaptureKind::kHandMirror) {
				coverage = Coverage::kFailed;
				g_counters.preparationFailures.fetch_add(
					1, std::memory_order_relaxed);
			}
		}
	}

	Scope::~Scope()
	{
		(void) Close();
	}

	bool Scope::Ready() const noexcept
	{
		return coverage == Coverage::kDisabled ||
		       coverage == Coverage::kCoveredByWorldRoot ||
		       coverage == Coverage::kSeparateRoot;
	}

	void OnInputLoaded(
		bool secondViewHooksReady,
		bool mirrorCaptureRequested,
		bool handMirrorCaptureRequested)
	{
		g_requested.store(false, std::memory_order_release);
		g_enabled.store(false, std::memory_order_release);
		g_handMirrorRequested.store(false, std::memory_order_release);
		g_handMirrorEnabled.store(false, std::memory_order_release);
		if (!secondViewHooksReady || !IsExactSupportedRuntime())
			return;

		bool activationRequested = false;
		const auto prepared = MirrorActivation::GlobalLifecycle().Prepared();
		if (prepared && !prepared->IsConflicted()) {
			activationRequested = MirrorActivation::Contains(
				prepared->Masks().HookRequest(),
				MirrorActivation::Feature::kMirrorPlayerInclusion);
		}
		const bool mirrorRequested =
			mirrorCaptureRequested || activationRequested;
		if (!mirrorRequested && !handMirrorCaptureRequested) {
			logger::info(
				"[MirrorsOfSkyrim][Player] dormant because neither wall nor hand mirror requested player inclusion");
			return;
		}

		g_requested.store(mirrorRequested, std::memory_order_release);
		g_handMirrorRequested.store(
			handMirrorCaptureRequested, std::memory_order_release);
		if (!PlayerDrawPassProbe::EnsureInstalled()) {
			g_requested.store(false, std::memory_order_release);
			g_handMirrorRequested.store(false, std::memory_order_release);
			g_counters.drawProbeInstallFailures.fetch_add(
				1, std::memory_order_relaxed);
			logger::critical(
				"[MirrorsOfSkyrim][Player] inclusion request rejected because the exact-target draw observer could not be installed");
			return;
		}
		logger::info(
			"[MirrorsOfSkyrim][Player] inclusion hook prepared (wall={} hand={}); activation waits for readiness",
			mirrorRequested, handMirrorCaptureRequested);
	}

	void OnDataLoaded(
		bool secondViewEnabled,
		bool handMirrorCaptureEnabled)
	{
		const bool mirrorRequested =
			g_requested.load(std::memory_order_acquire);
		const bool handMirrorRequested = handMirrorCaptureEnabled &&
			g_handMirrorRequested.load(std::memory_order_acquire);
		if (!secondViewEnabled || (!mirrorRequested && !handMirrorRequested))
			return;
		if (!PlayerDrawPassProbe::IsInstalled()) {
			g_requested.store(false, std::memory_order_release);
			g_handMirrorRequested.store(false, std::memory_order_release);
			g_enabled.store(false, std::memory_order_release);
			g_handMirrorEnabled.store(false, std::memory_order_release);
			g_counters.drawProbeInstallFailures.fetch_add(
				1, std::memory_order_relaxed);
			logger::critical(
				"[MirrorsOfSkyrim][Player] draw-observer ownership was lost before readiness; player inclusion remains disabled");
			return;
		}
		// This proven lease mutates only the player body root's app-cull bit and
		// fade alpha, and blocks an incompatible root lease from joining it.
		if (!EnsurePlayerLeaseArmed()) {
			logger::critical(
				"[MirrorsOfSkyrim][Player] lease registry refused the player-root lease; inclusion stays off");
			return;
		}
		g_enabled.store(mirrorRequested, std::memory_order_release);
		g_handMirrorEnabled.store(
			handMirrorRequested, std::memory_order_release);
		logger::warn(
			"[MirrorsOfSkyrim][Player] ENABLED: scoped body-root visibility lease armed (wall={} hand={})",
			mirrorRequested, handMirrorRequested);
	}

	void OnActivationCommitted() noexcept
	{
		const auto committed = MirrorActivation::GlobalLifecycle().Committed();
		if (!committed || committed->IsConflicted())
			return;
		const bool mirror = MirrorActivation::Contains(
			committed->Masks().RuntimeEnable(),
			MirrorActivation::Feature::kMirrorPlayerInclusion);
		if (!mirror)
			return;
		if (!HookReady()) {
			logger::critical(
				"[MirrorsOfSkyrim][Player] committed inclusion has no prepared draw observer");
			return;
		}
		if (!EnsurePlayerLeaseArmed()) {
			logger::critical(
				"[MirrorsOfSkyrim][Player] player-root lease refused; committed wall scope remains dormant");
			return;
		}
		g_enabled.store(
			g_enabled.load(std::memory_order_acquire) || mirror,
			std::memory_order_release);
		logger::info(
			"[MirrorsOfSkyrim][Player] committed wall-mirror scope promoted");
	}

	void OnGameLoaded() noexcept
	{
		g_lastPoseValid.store(false, std::memory_order_release);
		g_poseStillStreak.store(0, std::memory_order_release);
		if (!TryRecoverPendingRollback()) {
			logger::critical(
				"[MirrorsOfSkyrim][Player] retained app-cull rollback is still pending after game load");
		}
	}

	bool IsEnabled() noexcept
	{
		return g_enabled.load(std::memory_order_acquire) ||
		       g_handMirrorEnabled.load(std::memory_order_acquire);
	}

	bool HookReady() noexcept
	{
		return PlayerDrawPassProbe::IsInstalled();
	}

	bool IsEnabledFor(PlayerDrawPassProbe::CaptureKind captureKind) noexcept
	{
		switch (captureKind) {
		case PlayerDrawPassProbe::CaptureKind::kHandMirror:
			return g_handMirrorEnabled.load(std::memory_order_acquire);
		case PlayerDrawPassProbe::CaptureKind::kMirror:
			return g_enabled.load(std::memory_order_acquire);
		default:
			return false;
		}
	}

	bool TrySnapshotBodyBounds(BodyBoundsSnapshot& output) noexcept
	{
		output = {};
		RE::NiAVObject* root = nullptr;
		DirectX::XMFLOAT3 playerPosition{};
		__try {
			auto* const player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return false;
			const auto position = player->GetPosition();
			playerPosition = { position.x, position.y, position.z };
			root = player->Get3D1(false);
		} __except (RecordException(GetExceptionCode())) {
			return false;
		}
		if (!root || !RetainRoot(root))
			return false;

		bool copied = false;
		DirectX::XMFLOAT3 boundCenter{};
		float boundRadius = 0.0f;
		__try {
			boundCenter = {
				root->worldBound.center.x,
				root->worldBound.center.y,
				root->worldBound.center.z };
			boundRadius = root->worldBound.radius;
			copied = true;
		} __except (RecordException(GetExceptionCode())) {
			copied = false;
		}

		const bool released = ReleaseRoot(root);
		if (!copied || !released || !std::isfinite(playerPosition.x) ||
			!std::isfinite(playerPosition.y) || !std::isfinite(playerPosition.z) ||
			!std::isfinite(boundCenter.x) || !std::isfinite(boundCenter.y) ||
			!std::isfinite(boundCenter.z) || !std::isfinite(boundRadius) ||
			boundRadius < 0.0f) {
			return false;
		}

		output.rootAddress = reinterpret_cast<std::uintptr_t>(root);
		output.playerPosition = playerPosition;
		output.boundCenter = boundCenter;
		output.boundRadius = boundRadius;
		return output.rootAddress != 0;
	}

	RE::NiAVObject* Scope::AdditionalCullRoot() const noexcept
	{
		return coverage == Coverage::kSeparateRoot ? playerRoot : nullptr;
	}

	Coverage Scope::GetCoverage() const noexcept
	{
		return coverage;
	}

	bool Scope::BeginWorldCullExclusion(
		RE::NiAVObject* expectedPlayerRoot, WorldCullExclusionMode mode) noexcept
	{
		const bool exactScope =
			((mode == WorldCullExclusionMode::kHandComposition &&
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror) ||
			 (mode == WorldCullExclusionMode::kMirrorComposition &&
				captureKind == PlayerDrawPassProbe::CaptureKind::kMirror)) &&
			coverage == Coverage::kSeparateRoot && appCullScoped && retained &&
			!closed && playerRoot && expectedPlayerRoot == playerRoot &&
			!worldCullExclusionActive;
		bool currentlyCulled = true;
		if (!exactScope || !ReadAppCull(playerRoot, currentlyCulled) ||
			currentlyCulled || !SetAndVerifyAppCull(playerRoot, true)) {
			nativeFaulted = true;
			(mode == WorldCullExclusionMode::kMirrorComposition ?
			g_counters.mirrorWorldCullExclusionFailures :
			g_counters.handWorldCullExclusionFailures).fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}

		worldCullExclusionMode = mode;
		worldCullExclusionActive = true;
		(mode == WorldCullExclusionMode::kMirrorComposition ?
			g_counters.mirrorWorldCullExclusionBegins :
			g_counters.handWorldCullExclusionBegins).fetch_add(
			1, std::memory_order_relaxed);
		return true;
	}

	bool Scope::RestoreForExplicitCull(
		RE::NiAVObject* expectedPlayerRoot, WorldCullExclusionMode mode) noexcept
	{
		const bool exactScope =
			((mode == WorldCullExclusionMode::kHandComposition &&
				captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror) ||
			 (mode == WorldCullExclusionMode::kMirrorComposition &&
				captureKind == PlayerDrawPassProbe::CaptureKind::kMirror)) &&
			coverage == Coverage::kSeparateRoot && appCullScoped && retained &&
			!closed && playerRoot && expectedPlayerRoot == playerRoot &&
			worldCullExclusionActive && worldCullExclusionMode == mode;
		bool currentlyCulled = false;
		if (!exactScope || !ReadAppCull(playerRoot, currentlyCulled) ||
			!currentlyCulled || !SetAndVerifyAppCull(playerRoot, false)) {
			nativeFaulted = true;
			(mode == WorldCullExclusionMode::kMirrorComposition ?
			g_counters.mirrorWorldCullExclusionFailures :
			g_counters.handWorldCullExclusionFailures).fetch_add(
				1, std::memory_order_relaxed);
			return false;
		}

		worldCullExclusionActive = false;
		(mode == WorldCullExclusionMode::kMirrorComposition ?
			g_counters.mirrorWorldCullExclusionRestores :
			g_counters.handWorldCullExclusionRestores).fetch_add(
			1, std::memory_order_relaxed);
		return true;
	}

	bool Scope::FinishDrawEvidence(bool nativeDrawDispatched) noexcept
	{
		if (drawEvidenceFinalized)
			return drawEvidenceReady;
		drawEvidenceFinalized = true;
		drawEvidenceNativeDrawDispatched = nativeDrawDispatched;
		if (coverage == Coverage::kDisabled) {
			// Disabled is absence of an observation, never positive evidence.  This
			// keeps every strict caller fail-closed if activation state drifts.
			drawEvidenceReady = false;
			return false;
		}

		const auto result = PlayerDrawPassProbe::End(drawProbe);
		drawEvidenceResult = result;
		if (PlayerDrawPassProbe::CurrentFrameNativeFaulted(result, captureKind)) {
			nativeFaulted = true;
			return false;
		}
		if (!result.valid) {
			if (captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror)
				nativeFaulted = true;
			return false;
		}
		if (!nativeDrawDispatched) {
			// The observation scope begins before descriptor culling so every exit can
			// close it uniformly.  Do not call a pre-dispatch rejection a rendered,
			// playerless frame.
			g_counters.drawEvidenceNoDispatch.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (result.targetQueryFaults != 0 && result.playerPostDrawReturns == 0) {
			// The tri-state target query made no negative observation: an RTV query
			// fault must not be reported as a valid zero-player mirror frame.
			g_counters.drawEvidenceTargetFaults.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		g_counters.drawEvidenceFrames.fetch_add(1, std::memory_order_relaxed);
		if (result.playerPostDrawReturns != 0)
			g_counters.drawEvidenceWithPlayer.fetch_add(1, std::memory_order_relaxed);
		if (result.skinnedPlayerPostDrawReturns != 0)
			g_counters.drawEvidenceWithSkinnedPlayer.fetch_add(1, std::memory_order_relaxed);
		drawEvidenceReady = result.playerPostDrawReturns != 0 &&
			result.skinnedPlayerPostDrawReturns != 0;
		if (!drawEvidenceReady)
			g_counters.drawEvidenceZeroSkinned.fetch_add(1, std::memory_order_relaxed);
		return drawEvidenceReady;
	}

	bool Scope::PlayerDrawEvidenceConfirmed() const noexcept
	{
		return drawEvidenceFinalized && drawEvidenceReady;
	}

	FinalizedDrawEvidence Scope::ReadFinalizedDrawEvidence() const noexcept
	{
		return {
			.probe = drawEvidenceResult,
			.coverage = coverage,
			.retainedRootIdentity = retainedRootIdentity,
			.expectedRenderTargetIdentity = expectedRenderTargetIdentity,
			.probeArmed = drawProbeArmed,
			.finalized = drawEvidenceFinalized,
			.nativeDrawDispatched = drawEvidenceNativeDrawDispatched
		};
	}

	bool Scope::NativeFaulted() const noexcept
	{
		return nativeFaulted;
	}

	void Scope::ReapplyFade() noexcept
	{
		if (!fadeScoped || !playerRoot)
			return;
		if (WriteAndVerifyFadeAlpha(playerRoot, 1.0f))
			g_counters.fadeReapplies.fetch_add(1, std::memory_order_relaxed);
		else
			g_counters.fadeReapplyFailures.fetch_add(1, std::memory_order_relaxed);
	}

	bool Scope::Restore() noexcept
	{
		bool restored = true;
		if (drawProbe.active) {
			const auto result = PlayerDrawPassProbe::End(drawProbe);
			if (PlayerDrawPassProbe::CurrentFrameNativeFaulted(
					result, captureKind) ||
				(!result.valid &&
					captureKind == PlayerDrawPassProbe::CaptureKind::kHandMirror)) {
				nativeFaulted = true;
			}
		}
		if (fadeScoped) {
			if (playerRoot && WriteAndVerifyFadeAlpha(playerRoot, priorFadeAlpha)) {
				fadeScoped = false;
			} else {
				g_counters.fadeRestoreFailures.fetch_add(
					1, std::memory_order_relaxed);
				restored = false;
			}
		}
		if (appCullScoped) {
			if (!playerRoot || !SetAndVerifyAppCull(playerRoot, priorAppCulled)) {
				g_counters.restoreFailures.fetch_add(1, std::memory_order_relaxed);
				restored = false;
			} else {
				appCullScoped = false;
				worldCullExclusionActive = false;
				g_counters.restores.fetch_add(1, std::memory_order_relaxed);
			}
		}
		return restored && !fadeScoped && !appCullScoped;
	}

	bool Scope::Close() noexcept
	{
		if (closed)
			return closeSucceeded;

		closeSucceeded = Restore();
		if (!closeSucceeded)
			closeSucceeded = Restore();
		if (!closeSucceeded) {
			if (retained && playerRoot) {
				const bool transferred = QueueFailedRollback(
					playerRoot, priorAppCulled, appCullScoped, fadeScoped,
					priorFadeAlpha, rollbackOwnerLease);
				try {
					if (transferred) {
						logger::critical(
							"[MirrorsOfSkyrim][Player] player-state rollback failed twice; "
							"exact retained root ownership transferred to lifecycle recovery");
					} else {
						logger::critical(
							"[MirrorsOfSkyrim][Player] player-state rollback recovery-slot transfer failed; "
							"scope retains the exact root and remains terminally unclosed");
					}
				} catch (...) {
					// Cleanup is more important than diagnostics on the render thread.
				}
				if (transferred) {
					// Transfer the already-held manual reference and singleton lease only
					// after the fixed recovery slot acknowledges exact ownership.
					retained = false;
					playerRoot = nullptr;
					rollbackOwnerLease = false;
					appCullScoped = false;
					fadeScoped = false;
					closed = true;
				}
			}
			nativeFaulted = true;
			return false;
		}

		closeSucceeded = ReleaseRetainedRoot();
		closeSucceeded = ReleaseRollbackOwnerLease(closeSucceeded) && closeSucceeded;
		closed = true;
		return closeSucceeded;
	}

	bool Scope::ReleaseRetainedRoot() noexcept
	{
		if (!retained)
			return true;
		const bool released = ReleaseRoot(playerRoot);
		// DecRefCount's completion point is ambiguous after an AV.  Clear local
		// ownership so neither explicit Close nor the destructor can decrement twice,
		// but report failure so the enclosing private pass fault-latches.
		retained = false;
		playerRoot = nullptr;
		return released;
	}

	bool Scope::ReleaseRollbackOwnerLease(
		const bool retainedReferenceReleased) noexcept
	{
		if (!rollbackOwnerLease)
			return true;
		AcquireSRWLockExclusive(std::addressof(g_rollbackLock));
		const auto status =
			MirrorPlayerRollbackOwnershipPolicy::CompleteReferenceRelease(
				g_rollbackOwnership, retainedReferenceReleased);
		ReleaseSRWLockExclusive(std::addressof(g_rollbackLock));
		const bool released =
			status == MirrorPlayerRollbackOwnershipPolicy::Status::kAccepted;
		if (released || !retainedReferenceReleased)
			rollbackOwnerLease = false;
		return released;
	}

	void LogDiagnostics(const char* reason)
	{
		logger::info("[MOS][PlayerCull] mirrorExclusions={} restores={} failures={}",
			g_counters.mirrorWorldCullExclusionBegins.load(std::memory_order_relaxed),
			g_counters.mirrorWorldCullExclusionRestores.load(std::memory_order_relaxed),
			g_counters.mirrorWorldCullExclusionFailures.load(std::memory_order_relaxed));
		logger::info(
			"[MirrorsOfSkyrim][Player] diagnostics ({}) requested={} enabled={} handRequested={} handEnabled={} attempts={} "
			"missing={} covered={} separate={} handForcedSeparate={} handUnifiedCull(exclude/restore/fail)={}/{}/{} "
			"priorCulled={} priorVisible={} prepFailures={} restores={} restoreFailures={} "
			"rollbackQueued={} rollbackRecovered={} rollbackPending={} "
			"drawFrames={} drawWithPlayer={} drawWithSkinned={} drawZeroSkinned={} drawTargetFaults={} drawNoDispatch={} probeInstallFailures={} "
			"pose(samples/changed/still/maxStill/faults)={}/{}/{}/{}/{} "
			"pose1P(samples/changed)={}/{} pose3P(samples/changed)={}/{} "
			"fade(leases/restoreFailures/reapplies/reapplyFailures)={}/{}/{}/{} "
			"exceptions={} lastSEH=0x{:08X}",
			reason ? reason : "unspecified",
			g_requested.load(std::memory_order_relaxed),
			g_enabled.load(std::memory_order_relaxed),
			g_handMirrorRequested.load(std::memory_order_relaxed),
			g_handMirrorEnabled.load(std::memory_order_relaxed),
			g_counters.attempts.load(std::memory_order_relaxed),
			g_counters.missingRoots.load(std::memory_order_relaxed),
			g_counters.coveredByWorldRoot.load(std::memory_order_relaxed),
			g_counters.separateRoots.load(std::memory_order_relaxed),
			g_counters.handMirrorForcedSeparateRoots.load(std::memory_order_relaxed),
			g_counters.handWorldCullExclusionBegins.load(std::memory_order_relaxed),
			g_counters.handWorldCullExclusionRestores.load(std::memory_order_relaxed),
			g_counters.handWorldCullExclusionFailures.load(std::memory_order_relaxed),
			g_counters.priorCulled.load(std::memory_order_relaxed),
			g_counters.priorVisible.load(std::memory_order_relaxed),
			g_counters.preparationFailures.load(std::memory_order_relaxed),
			g_counters.restores.load(std::memory_order_relaxed),
			g_counters.restoreFailures.load(std::memory_order_relaxed),
			g_counters.rollbackQueued.load(std::memory_order_relaxed),
			g_counters.rollbackRecovered.load(std::memory_order_relaxed),
			[]() noexcept {
				AcquireSRWLockShared(std::addressof(g_rollbackLock));
				const bool pending = g_pendingRollback.pending;
				ReleaseSRWLockShared(std::addressof(g_rollbackLock));
				return pending;
			}(),
			g_counters.drawEvidenceFrames.load(std::memory_order_relaxed),
			g_counters.drawEvidenceWithPlayer.load(std::memory_order_relaxed),
			g_counters.drawEvidenceWithSkinnedPlayer.load(std::memory_order_relaxed),
			g_counters.drawEvidenceZeroSkinned.load(std::memory_order_relaxed),
			g_counters.drawEvidenceTargetFaults.load(std::memory_order_relaxed),
			g_counters.drawEvidenceNoDispatch.load(std::memory_order_relaxed),
			g_counters.drawProbeInstallFailures.load(std::memory_order_relaxed),
			g_counters.poseSamples.load(std::memory_order_relaxed),
			g_counters.poseChangedSamples.load(std::memory_order_relaxed),
			g_poseStillStreak.load(std::memory_order_relaxed),
			g_poseMaxStillStreak.load(std::memory_order_relaxed),
			g_counters.poseSampleFaults.load(std::memory_order_relaxed),
			g_counters.poseFirstPersonSamples.load(std::memory_order_relaxed),
			g_counters.poseFirstPersonChanged.load(std::memory_order_relaxed),
			g_counters.poseThirdPersonSamples.load(std::memory_order_relaxed),
			g_counters.poseThirdPersonChanged.load(std::memory_order_relaxed),
			g_counters.fadeLeases.load(std::memory_order_relaxed),
			g_counters.fadeRestoreFailures.load(std::memory_order_relaxed),
			g_counters.fadeReapplies.load(std::memory_order_relaxed),
			g_counters.fadeReapplyFailures.load(std::memory_order_relaxed),
			g_counters.exceptions.load(std::memory_order_relaxed),
			g_lastException.load(std::memory_order_relaxed));
	}

}
