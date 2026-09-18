#include "PCH.h"

#include "MirrorPlayerDrawPassProbe.h"

#include "MirrorsOfSkyrimShadowMapBypass.h"
#include "PlayerDrawProbeLifetime.h"
#include "PlayerPassAncestry.h"
#include "SecondView.h"

#include <bit>
#include <cstring>
#include <memory>
#include <type_traits>

namespace PlayerDrawPassProbe
{
	namespace
	{
		constexpr std::size_t kMaximumParentDepth = 256;
		constexpr std::size_t kRestoreGeometrySlot = 0x7;

		struct AggregateCounters
		{
			std::atomic<std::uint64_t> scopes{ 0 };
			std::atomic<std::uint64_t> scopesWithPlayer{ 0 };
			std::atomic<std::uint64_t> scopesWithSkinnedPlayer{ 0 };
			std::atomic<std::uint64_t> scopesWithoutPlayer{ 0 };
			std::atomic<std::uint64_t> scopesWithTargetFault{ 0 };
			std::atomic<std::uint64_t> lightingPostDrawCalls{ 0 };
			std::atomic<std::uint64_t> lightingPostDrawReturns{ 0 };
			std::atomic<std::uint64_t> targetMatchedCalls{ 0 };
			std::atomic<std::uint64_t> targetMismatches{ 0 };
			std::atomic<std::uint64_t> playerPostDrawCalls{ 0 };
			std::atomic<std::uint64_t> playerPostDrawReturns{ 0 };
			std::atomic<std::uint64_t> skinnedPlayerPostDrawCalls{ 0 };
			std::atomic<std::uint64_t> skinnedPlayerPostDrawReturns{ 0 };
			std::atomic<std::uint64_t> truncatedParentWalks{ 0 };
			std::atomic<std::uint64_t> malformedParentWalks{ 0 };
			std::atomic<std::uint64_t> classificationFaults{ 0 };
			std::atomic<std::uint64_t> targetQueryFaults{ 0 };
		};

		struct ActiveObservation
		{
			Result result{};
			RE::NiAVObject* retainedRoot{ nullptr };
			ID3D11RenderTargetView* retainedRenderTarget{ nullptr };
			CaptureKind kind{ CaptureKind::kMirror };
			std::uint64_t token{ 0 };
			std::uint32_t threadID{ 0 };
			bool active{ false };
		};
		static_assert(std::is_trivially_destructible_v<ActiveObservation>,
			"quarantined TLS references must not release during thread teardown");

		AggregateCounters g_mirrorCounters{};
		AggregateCounters g_handMirrorCounters{};
		std::atomic<std::uint64_t> g_nextToken{ 1 };
		std::atomic<std::uint64_t> g_beginRejects{ 0 };
		std::atomic<std::uint64_t> g_faultedBeginRejects{ 0 };
		std::atomic<std::uint64_t> g_endMismatches{ 0 };
		std::atomic<std::uint64_t> g_quarantinedSessions{ 0 };
		std::atomic<std::uint64_t> g_rootRetainFaults{ 0 };
		std::atomic<std::uint64_t> g_targetRetainFaults{ 0 };
		std::atomic<std::uint64_t> g_rootReleaseFaults{ 0 };
		std::atomic<std::uint64_t> g_targetReleaseFaults{ 0 };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_installAttempted{ false };
		std::atomic_bool g_installed{ false };
		std::uintptr_t g_vtableSlotAddress{ 0 };
		SRWLOCK g_installLock = SRWLOCK_INIT;
		thread_local ActiveObservation g_active{};

		enum class TargetStatus : std::uint8_t
		{
			kMatch,
			kMismatch,
			kFault
		};

		void EnterFailStop() noexcept
		{
			g_faulted.store(true, std::memory_order_release);
		}

		[[nodiscard]] bool RetainRoot(RE::NiAVObject* root) noexcept
		{
			if (!root)
				return false;
			__try {
				root->IncRefCount();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_rootRetainFaults.fetch_add(1, std::memory_order_relaxed);
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
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_rootReleaseFaults.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		[[nodiscard]] bool RetainTarget(ID3D11RenderTargetView* target) noexcept
		{
			if (!target)
				return false;
			__try {
				target->AddRef();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_targetRetainFaults.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		[[nodiscard]] bool ReleaseTarget(ID3D11RenderTargetView* target) noexcept
		{
			if (!target)
				return true;
			__try {
				target->Release();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_targetReleaseFaults.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		struct OwnedReferenceReleaseOutcome
		{
			bool targetReleased{ false };
			bool rootReleased{ false };
		};

		[[nodiscard]] OwnedReferenceReleaseOutcome ReleaseOwnedReferences(
			RE::NiAVObject* root,
			ID3D11RenderTargetView* target) noexcept
		{
			// The TLS observation is cleared before entering here, so the hook can
			// never race a final Release on the owning render thread.
			const OwnedReferenceReleaseOutcome outcome{
				.targetReleased = ReleaseTarget(target),
				.rootReleased = ReleaseRoot(root)
			};
			if (!outcome.targetReleased || !outcome.rootReleased)
				EnterFailStop();
			return outcome;
		}

		[[nodiscard]] bool IsSupportedRuntime() noexcept
		{
			const auto version = REL::Module::get().version();
			return (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 }) ||
			       (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) ||
			       SupportedRuntimePolicy::IsExactVRRuntime();
		}

		[[nodiscard]] bool IsExecutableAddress(const void* address) noexcept
		{
			if (!address)
				return false;
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0 ||
				(info.Protect & PAGE_NOACCESS) != 0) {
				return false;
			}
			const DWORD protection = info.Protect & 0xFF;
			return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
			       protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool ReadPointer(const void* address, std::uintptr_t& output) noexcept
		{
			if (!address)
				return false;
			__try {
				std::memcpy(std::addressof(output), address, sizeof(output));
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				output = 0;
				return false;
			}
		}

		[[nodiscard]] AggregateCounters& CountersFor(CaptureKind kind) noexcept
		{
			switch (kind) {
			case CaptureKind::kHandMirror:
				return g_handMirrorCounters;
			case CaptureKind::kMirror:
			default:
				return g_mirrorCounters;
			}
		}

		void Aggregate(CaptureKind kind, const Result& result) noexcept
		{
			auto& counters = CountersFor(kind);
			counters.scopes.fetch_add(1, std::memory_order_relaxed);
			counters.lightingPostDrawCalls.fetch_add(
				result.lightingPostDrawCalls, std::memory_order_relaxed);
			counters.lightingPostDrawReturns.fetch_add(
				result.lightingPostDrawReturns, std::memory_order_relaxed);
			counters.targetMatchedCalls.fetch_add(
				result.targetMatchedCalls, std::memory_order_relaxed);
			counters.targetMismatches.fetch_add(
				result.targetMismatches, std::memory_order_relaxed);
			counters.playerPostDrawCalls.fetch_add(
				result.playerPostDrawCalls, std::memory_order_relaxed);
			counters.playerPostDrawReturns.fetch_add(
				result.playerPostDrawReturns, std::memory_order_relaxed);
			counters.skinnedPlayerPostDrawCalls.fetch_add(
				result.skinnedPlayerPostDrawCalls, std::memory_order_relaxed);
			counters.skinnedPlayerPostDrawReturns.fetch_add(
				result.skinnedPlayerPostDrawReturns, std::memory_order_relaxed);
			counters.truncatedParentWalks.fetch_add(
				result.truncatedParentWalks, std::memory_order_relaxed);
			counters.malformedParentWalks.fetch_add(
				result.malformedParentWalks, std::memory_order_relaxed);
			counters.classificationFaults.fetch_add(
				result.classificationFaults, std::memory_order_relaxed);
			counters.targetQueryFaults.fetch_add(
				result.targetQueryFaults, std::memory_order_relaxed);
			if (result.targetQueryFaults != 0)
				counters.scopesWithTargetFault.fetch_add(1, std::memory_order_relaxed);
			if (result.playerPostDrawReturns != 0)
				counters.scopesWithPlayer.fetch_add(1, std::memory_order_relaxed);
			else if (result.targetQueryFaults == 0)
				counters.scopesWithoutPlayer.fetch_add(1, std::memory_order_relaxed);
			if (result.skinnedPlayerPostDrawReturns != 0)
				counters.scopesWithSkinnedPlayer.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] TargetStatus QueryTargetStatus(
			ID3D11RenderTargetView* expected) noexcept
		{
			if (!expected) {
				++g_active.result.targetQueryFaults;
				return TargetStatus::kFault;
			}
			ID3D11RenderTargetView* current = nullptr;
			__try {
				auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
				if (!renderer) {
					++g_active.result.targetQueryFaults;
					return TargetStatus::kFault;
				}
				auto* context = reinterpret_cast<ID3D11DeviceContext*>(
					renderer->GetRuntimeData().context);
				if (!context) {
					++g_active.result.targetQueryFaults;
					return TargetStatus::kFault;
				}
				context->OMGetRenderTargets(1, &current, nullptr);
				const bool matches = current == expected;
				if (current) {
					auto* release = current;
					current = nullptr;
					release->Release();
				}
				return matches ? TargetStatus::kMatch : TargetStatus::kMismatch;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				if (current) {
					__try {
						current->Release();
					} __except (EXCEPTION_EXECUTE_HANDLER) {
					}
				}
				++g_active.result.targetQueryFaults;
				return TargetStatus::kFault;
			}
		}

		[[nodiscard]] bool ClassifyPlayerGeometry(
			const RE::BSRenderPass* pass,
			RE::NiAVObject* root,
			bool& skinned) noexcept
		{
			skinned = false;
			if (!pass || !root)
				return false;
			__try {
				auto* geometry = pass->geometry;
				if (!geometry)
					return false;
				const auto ancestry = PlayerPassAncestry::Classify(
					static_cast<const RE::NiAVObject*>(geometry), root, kMaximumParentDepth,
					[](const RE::NiAVObject* object) noexcept { return object->parent; });
				switch (ancestry) {
				case PlayerPassAncestry::Result::kMatch:
					skinned = geometry->GetGeometryRuntimeData().skinInstance.get() != nullptr;
					return true;
				case PlayerPassAncestry::Result::kTruncated:
					++g_active.result.truncatedParentWalks;
					break;
				case PlayerPassAncestry::Result::kMalformed:
					++g_active.result.malformedParentWalks;
					break;
				case PlayerPassAncestry::Result::kNoMatch:
				default:
					break;
				}
				return false;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				++g_active.result.classificationFaults;
				return false;
			}
		}

		struct RestoreGeometryHook
		{
			using Function = void(RE::BSShader*, RE::BSRenderPass*, std::uint32_t);

			static __declspec(noinline) void InvokeDownstreamObserved(
				Function* downstream,
				RE::BSShader* shader,
				RE::BSRenderPass* pass,
				std::uint32_t renderFlags)
			{
				MirrorShadowMapBypass::OnBeforeRestoreGeometry();
#if defined(_MSC_VER)
				__try {
					downstream(shader, pass, renderFlags);
				} __finally {
					MirrorShadowMapBypass::OnAfterRestoreGeometry();
				}
#else
				downstream(shader, pass, renderFlags);
				MirrorShadowMapBypass::OnAfterRestoreGeometry();
#endif
			}

			static void thunk(
				RE::BSShader* shader,
				RE::BSRenderPass* pass,
				std::uint32_t renderFlags)
			{
				auto* downstream = original.load(std::memory_order_acquire);
				if (!downstream)
					return;
				if (g_faulted.load(std::memory_order_acquire) || !g_active.active) {
					InvokeDownstreamObserved(downstream, shader, pass, renderFlags);
					return;
				}

				const std::uint64_t token = g_active.token;
				++g_active.result.lightingPostDrawCalls;
				const auto targetStatus = QueryTargetStatus(g_active.retainedRenderTarget);
				if (targetStatus == TargetStatus::kMatch)
					++g_active.result.targetMatchedCalls;
				else if (targetStatus == TargetStatus::kMismatch)
					++g_active.result.targetMismatches;
				bool skinned = false;
				const bool player = targetStatus == TargetStatus::kMatch &&
					ClassifyPlayerGeometry(pass, g_active.retainedRoot, skinned);
				if (player) {
					++g_active.result.playerPostDrawCalls;
					if (skinned)
						++g_active.result.skinnedPlayerPostDrawCalls;
					SecondView::ObserveLightingEvidence(pass, skinned ? 3u : 1u);
				}

				InvokeDownstreamObserved(downstream, shader, pass, renderFlags);
				if (g_faulted.load(std::memory_order_acquire) ||
					!g_active.active || g_active.token != token)
					return;
				++g_active.result.lightingPostDrawReturns;
				if (player) {
					++g_active.result.playerPostDrawReturns;
					if (skinned)
						++g_active.result.skinnedPlayerPostDrawReturns;
				}
			}

			static inline std::atomic<Function*> original{ nullptr };
		};

		[[nodiscard]] bool HookStillInstalled() noexcept
		{
			if (!g_installed.load(std::memory_order_acquire) || !g_vtableSlotAddress)
				return false;
			std::uintptr_t current = 0;
			return ReadPointer(reinterpret_cast<const void*>(g_vtableSlotAddress), current) &&
			       current == std::bit_cast<std::uintptr_t>(&RestoreGeometryHook::thunk);
		}

		void LogCounterFamily(const char* label, const AggregateCounters& counters)
		{
			logger::info(
				"[MirrorsOfSkyrim][PlayerDrawProbe] {} scopes={} withPlayer={} withSkinned={} noPlayer={} targetFaultScopes={} "
				"postDrawCalls={} postDrawReturns={} targetMatched={} targetMismatch={} "
				"playerCalls={} playerReturns={} skinnedCalls={} skinnedReturns={} "
				"truncatedParents={} malformedParents={} classifyFaults={} targetFaults={}",
				label,
				counters.scopes.load(std::memory_order_relaxed),
				counters.scopesWithPlayer.load(std::memory_order_relaxed),
				counters.scopesWithSkinnedPlayer.load(std::memory_order_relaxed),
				counters.scopesWithoutPlayer.load(std::memory_order_relaxed),
				counters.scopesWithTargetFault.load(std::memory_order_relaxed),
				counters.lightingPostDrawCalls.load(std::memory_order_relaxed),
				counters.lightingPostDrawReturns.load(std::memory_order_relaxed),
				counters.targetMatchedCalls.load(std::memory_order_relaxed),
				counters.targetMismatches.load(std::memory_order_relaxed),
				counters.playerPostDrawCalls.load(std::memory_order_relaxed),
				counters.playerPostDrawReturns.load(std::memory_order_relaxed),
				counters.skinnedPlayerPostDrawCalls.load(std::memory_order_relaxed),
				counters.skinnedPlayerPostDrawReturns.load(std::memory_order_relaxed),
				counters.truncatedParentWalks.load(std::memory_order_relaxed),
				counters.malformedParentWalks.load(std::memory_order_relaxed),
				counters.classificationFaults.load(std::memory_order_relaxed),
				counters.targetQueryFaults.load(std::memory_order_relaxed));
		}
	}

	namespace
	{
		// BSLightingShader::SetupGeometry (vtable slot 6) runs after the material
		// textures are bound and before the draw: the seam for the nested hand
		// pane diffuse swap. Installed beside the post-draw observer.
		struct SetupGeometryHook
		{
			using Function = void(RE::BSShader*, RE::BSRenderPass*, std::uint32_t);
			static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, std::uint32_t renderFlags)
			{
				auto* downstream = original.load(std::memory_order_acquire);
				if (!downstream)
					return;
				downstream(shader, pass, renderFlags);
				// 2026-09-14: the nested hand-pane diffuse swap is retired; nested pane
				// content is drawn by the pane renderer before capture mip generation.
			}
			static inline std::atomic<Function*> original{ nullptr };
		};
		constexpr std::size_t kSetupGeometrySlot = 0x6;
		std::atomic_bool g_setupGeometryInstalled{ false };

		void InstallSetupGeometryHookLocked() noexcept
		{
			if (g_setupGeometryInstalled.load(std::memory_order_acquire))
				return;
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSLightingShader[0] };
			const auto slot = vtable.address() + kSetupGeometrySlot * sizeof(void*);
			std::uintptr_t current = 0;
			const auto thunkAddress = std::bit_cast<std::uintptr_t>(&SetupGeometryHook::thunk);
			if (!ReadPointer(reinterpret_cast<const void*>(slot), current) || current == thunkAddress ||
				!IsExecutableAddress(reinterpret_cast<const void*>(current))) {
				logger::critical("[MirrorsOfSkyrim][PlayerDrawProbe] BSLightingShader::SetupGeometry slot unreadable; nested hand pane content stays off");
				return;
			}
			SetupGeometryHook::original.store(std::bit_cast<SetupGeometryHook::Function*>(current), std::memory_order_release);
			const auto captured = vtable.write_vfunc(kSetupGeometrySlot, SetupGeometryHook::thunk);
			if (captured != 0 && captured != thunkAddress && IsExecutableAddress(reinterpret_cast<const void*>(captured)))
				SetupGeometryHook::original.store(std::bit_cast<SetupGeometryHook::Function*>(captured), std::memory_order_release);
			g_setupGeometryInstalled.store(true, std::memory_order_release);
			logger::info("[MirrorsOfSkyrim][PlayerDrawProbe] BSLightingShader::SetupGeometry hook installed (nested hand pane content)");
		}
	}

	bool EnsureInstalled() noexcept
	{
		if (HookStillInstalled())
			return true;
		if (!IsSupportedRuntime())
			return false;

		AcquireSRWLockExclusive(&g_installLock);
		if (HookStillInstalled()) {
			ReleaseSRWLockExclusive(&g_installLock);
			return true;
		}
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel)) {
			ReleaseSRWLockExclusive(&g_installLock);
			return false;
		}

		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSLightingShader[0] };
		g_vtableSlotAddress = vtable.address() + kRestoreGeometrySlot * sizeof(void*);
		std::uintptr_t current = 0;
		const auto thunkAddress = std::bit_cast<std::uintptr_t>(&RestoreGeometryHook::thunk);
		bool installed = ReadPointer(
			reinterpret_cast<const void*>(g_vtableSlotAddress), current) &&
			current != thunkAddress && IsExecutableAddress(reinterpret_cast<const void*>(current));
		if (installed) {
			RestoreGeometryHook::original.store(
				std::bit_cast<RestoreGeometryHook::Function*>(current), std::memory_order_release);
			const auto captured = vtable.write_vfunc(kRestoreGeometrySlot, RestoreGeometryHook::thunk);
			if (captured != 0 && captured != thunkAddress &&
				IsExecutableAddress(reinterpret_cast<const void*>(captured))) {
				RestoreGeometryHook::original.store(
					std::bit_cast<RestoreGeometryHook::Function*>(captured), std::memory_order_release);
			}
			g_installed.store(true, std::memory_order_release);
			installed = HookStillInstalled();
			if (installed)
				InstallSetupGeometryHookLocked();
		}
		g_installed.store(installed, std::memory_order_release);
		ReleaseSRWLockExclusive(&g_installLock);
		if (installed) {
			logger::info(
				"[MirrorsOfSkyrim][PlayerDrawProbe] dormant BSLightingShader post-draw observer installed behind an exact marker-gated mirror capture");
		} else {
			logger::critical(
				"[MirrorsOfSkyrim][PlayerDrawProbe] failed to install BSLightingShader post-draw observer");
		}
		return installed;
	}

	bool IsInstalled() noexcept
	{
		return HookStillInstalled();
	}

	bool Begin(
		Session& session,
		CaptureKind kind,
		RE::NiAVObject* retainedPlayerRoot,
		ID3D11RenderTargetView* expectedRenderTarget) noexcept
	{
		if (kind != CaptureKind::kMirror && kind != CaptureKind::kHandMirror) {
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (g_faulted.load(std::memory_order_acquire)) {
			g_faultedBeginRejects.fetch_add(1, std::memory_order_relaxed);
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (session.active || !retainedPlayerRoot || !expectedRenderTarget ||
			!IsInstalled() || g_active.active) {
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		if (!RetainRoot(retainedPlayerRoot)) {
			// IncRefCount may have faulted after changing ownership.  Retrying or
			// balancing an uncertain reference is unsafe, so fail-stop globally.
			EnterFailStop();
			g_quarantinedSessions.fetch_add(1, std::memory_order_relaxed);
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (!RetainTarget(expectedRenderTarget)) {
			// The target's reference count is uncertain after a fault.  Never call
			// Release on it; the independently retained root can still be balanced.
			EnterFailStop();
			g_quarantinedSessions.fetch_add(1, std::memory_order_relaxed);
			(void) ReleaseRoot(retainedPlayerRoot);
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (g_faulted.load(std::memory_order_acquire)) {
			(void)ReleaseOwnedReferences(retainedPlayerRoot, expectedRenderTarget);
			g_faultedBeginRejects.fetch_add(1, std::memory_order_relaxed);
			g_beginRejects.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		std::uint64_t token = g_nextToken.fetch_add(1, std::memory_order_relaxed);
		if (token == 0)
			token = g_nextToken.fetch_add(1, std::memory_order_relaxed);
		session = {};
		session.token = token;
		session.threadID = GetCurrentThreadId();
		session.active = true;
		g_active = {};
		g_active.retainedRoot = retainedPlayerRoot;
		g_active.retainedRenderTarget = expectedRenderTarget;
		g_active.kind = kind;
		g_active.token = token;
		g_active.threadID = session.threadID;
		g_active.active = true;
		return true;
	}

	Result Snapshot(const Session& session) noexcept
	{
		if (!g_faulted.load(std::memory_order_acquire) && session.active &&
			session.threadID == GetCurrentThreadId() &&
			g_active.active && g_active.token == session.token) {
			auto result = g_active.result;
			result.valid = true;
			return result;
		}
		return session.result;
	}

	ExactHandDrawClassification ClassifyExactHandPlayerDraw(
		RE::BSRenderPass* pass) noexcept
	{
		if (g_active.kind != CaptureKind::kHandMirror)
			return ExactHandDrawClassification::kInactive;
		return ClassifyExactMirrorPlayerDraw(pass);
	}

	ExactHandDrawClassification ClassifyExactMirrorPlayerDraw(
		const RE::BSRenderPass* pass) noexcept
	{
		if (g_faulted.load(std::memory_order_acquire) || !g_active.active ||
			(g_active.kind != CaptureKind::kHandMirror && g_active.kind != CaptureKind::kMirror)) {
			return ExactHandDrawClassification::kInactive;
		}
		if (g_active.threadID == 0 ||
			g_active.threadID != GetCurrentThreadId()) {
			++g_active.result.classificationFaults;
			return ExactHandDrawClassification::kFault;
		}
		__try {
			if (!pass || !pass->geometry) {
				++g_active.result.classificationFaults;
				return ExactHandDrawClassification::kFault;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			++g_active.result.classificationFaults;
			return ExactHandDrawClassification::kFault;
		}

		const auto targetStatus = QueryTargetStatus(g_active.retainedRenderTarget);
		if (targetStatus == TargetStatus::kFault)
			return ExactHandDrawClassification::kFault;
		if (targetStatus != TargetStatus::kMatch)
			return ExactHandDrawClassification::kNonPlayer;

		const auto faultsBefore = g_active.result.classificationFaults;
		const auto truncatedBefore = g_active.result.truncatedParentWalks;
		const auto malformedBefore = g_active.result.malformedParentWalks;
		bool skinned = false;
		if (ClassifyPlayerGeometry(pass, g_active.retainedRoot, skinned))
			return ExactHandDrawClassification::kPlayer;
		return g_active.result.classificationFaults != faultsBefore ||
			g_active.result.truncatedParentWalks != truncatedBefore ||
			g_active.result.malformedParentWalks != malformedBefore ?
			ExactHandDrawClassification::kFault :
			ExactHandDrawClassification::kNonPlayer;
	}

	Result End(Session& session) noexcept
	{
		const auto disposition = PlayerDrawProbeLifetime::ClassifyEnd(
			session.active, session.threadID, session.token, GetCurrentThreadId(),
			g_active.active, g_active.token);
		if (disposition == PlayerDrawProbeLifetime::EndDisposition::kAlreadyEnded)
			return session.result;
		if (disposition == PlayerDrawProbeLifetime::EndDisposition::kQuarantine) {
			// TLS is per-thread: this caller cannot clear or release the original
			// observation.  Fail-stop makes every hook chain without inspection,
			// while its independent root/RTV references keep stale pointers alive.
			EnterFailStop();
			g_endMismatches.fetch_add(1, std::memory_order_relaxed);
			g_quarantinedSessions.fetch_add(1, std::memory_order_relaxed);
			session.active = false;
			session.result = {};
			return session.result;
		}

		session.result = g_active.result;
		session.result.valid = true;
		const auto kind = g_active.kind;
		auto* retainedRoot = g_active.retainedRoot;
		auto* retainedTarget = g_active.retainedRenderTarget;
		g_active = {};
		session.active = false;
		const auto released = ReleaseOwnedReferences(retainedRoot, retainedTarget);
		session.result = ApplyOwnedReferenceReleaseOutcome(
			session.result, released.targetReleased, released.rootReleased);
		Aggregate(kind, session.result);
		return session.result;
	}

	void LogDiagnostics(const char* reason)
	{
		logger::info(
			"[MirrorsOfSkyrim][PlayerDrawProbe] diagnostics ({}) installed={} faulted={} beginRejects={} "
			"faultedBeginRejects={} endMismatches={} quarantined={} "
			"rootRefFaults(retain/release)={}/{} targetRefFaults(retain/release)={}/{}",
			reason ? reason : "unspecified", IsInstalled(),
			g_faulted.load(std::memory_order_relaxed),
			g_beginRejects.load(std::memory_order_relaxed),
			g_faultedBeginRejects.load(std::memory_order_relaxed),
			g_endMismatches.load(std::memory_order_relaxed),
			g_quarantinedSessions.load(std::memory_order_relaxed),
			g_rootRetainFaults.load(std::memory_order_relaxed),
			g_rootReleaseFaults.load(std::memory_order_relaxed),
			g_targetRetainFaults.load(std::memory_order_relaxed),
			g_targetReleaseFaults.load(std::memory_order_relaxed));
		LogCounterFamily("mirror", g_mirrorCounters);
		LogCounterFamily("hand-mirror", g_handMirrorCounters);
	}
}
