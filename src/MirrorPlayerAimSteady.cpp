#include "PCH.h"

#include "MirrorPlayerAimSteady.h"
#include "MirrorHeadTiltMath.h"

#include <cstring>

namespace MirrorPlayerAimSteady
{
	namespace
	{
		using AimTargetFunction = void(void*, RE::hkbContext*, bool*, RE::hkVector4*, std::int32_t);
		using ApplyChannelsFunction = void(RE::BShkbAnimationGraph*, void*);
		using SetFloatFunction = bool(RE::BShkbAnimationGraph*, const RE::BSFixedString&, float);
		using SetIntFunction = bool(RE::BShkbAnimationGraph*, const RE::BSFixedString&, std::int32_t);
		using SendEventFunction = bool(RE::BShkbAnimationGraph*, const RE::BSFixedString*);
		using BoneNodes = RE::BSTArray<RE::BShkbAnimationGraph::BoneNodeEntry>;
		using WritePoseFunction = void(void*, BoneNodes*, std::uint32_t);
		using UpdateFunction = void(RE::NiAVObject*, RE::NiUpdateData*);

		// SE 1.5.97 Address Library IDs and the RVAs Ghidra Combined shows for
		// them (.runtime-review/core-release-20260916/ghidra/FINDINGS.md). An ID
		// that resolves anywhere else means a different library or build, and
		// then nothing is patched.
		struct NativeSite
		{
			std::uint64_t id;
			std::uintptr_t rva;
		};
		constexpr NativeSite kAimInterfaceVtable{ 263725, 0x1675C08 };
		constexpr NativeSite kAimTarget{ 42496, 0x72FAC0 };
		constexpr NativeSite kApplyLoop{ 62436, 0xAE3200 };
		constexpr NativeSite kApplyChannels{ 62645, 0xAEFD90 };
		constexpr NativeSite kSetFloat{ 62709, 0xAF7250 };
		// The per-graph int setter. CommonLib pairs 62708 with the bool setter
		// because its SE/AE ID columns are swapped for these three; the SE
		// function at this address stores all four bytes of its argument.
		constexpr NativeSite kSetInt{ 62708, 0xAF7190 };
		// Ghidra report 2 (.runtime-review/core-release-20260916/ghidra2):
		// BSAnimationGraphManager's event send loops its graphs under the
		// manager spin lock and calls the per-graph send once per graph.
		constexpr NativeSite kSendEvents{ 62420, 0xAE24A0 };
		constexpr NativeSite kGraphSendEvent{ 62646, 0xAEFEB0 };
		// `E8 A1 D9 00 00` at 0x140AE250A, the only call of the per-graph send.
		constexpr std::uintptr_t kSendEventCallOffset = 0x6A;
		// The per-graph generate step writes the animated pose into the bone
		// nodes: `LEA RDX,[RDI+0x160]; MOV RCX,RAX; CALL` at 0x140AEFD60.
		constexpr NativeSite kGeneratePose{ 62644, 0xAEFC50 };
		constexpr NativeSite kWritePose{ 62933, 0xB01480 };
		constexpr std::uintptr_t kWritePoseCallOffset = 0x110;
		// RDX of that call is &graph->boneNodes (CommonLib: boneNodes at 0x160).
		constexpr std::uintptr_t kBoneNodesOffset = 0x160;
		// Run 6 (2026-09-17): with the pose-write hook installed, headTilts stayed
		// 0 -- that call is skipped for the player's graph. The actor's 3D update
		// then updates the third-person root (`CALL NiAVObject::Update` at
		// 0x1406A4F34, Get3D1(false) in RCX): the fallback tilts just before it.
		constexpr NativeSite kActorUpdate3D{ 39446, 0x6A4EA0 };
		constexpr NativeSite kNiAVObjectUpdate{ 68900, 0xC56B50 };
		constexpr std::uintptr_t kWorldUpdateCallOffset = 0x94;
		// CommonLib's graph getters (read-only diagnostics) use these IDs.
		constexpr NativeSite kGetFloat{ 62695, 0xAF6090 };
		constexpr NativeSite kGetBool{ 62696, 0xAF6150 };
		// `E8 44 CB 00 00` -- the only call of the channel apply function.
		constexpr std::uintptr_t kApplyCallOffset = 0x47;
		constexpr std::size_t kAimTargetSlot = 1;

		// Skyrim AE 1.7.104, decoded from the owner's exact executable. These
		// are direct RVAs; every mutable call/vtable site is still checked
		// against its recorded native target before it is patched.
		constexpr NativeSite kAEAimInterfaceVtable{ 0, 0x193BB38 };
		constexpr NativeSite kAEAimTarget{ 0, 0x7DB5E0 };
		constexpr NativeSite kAEApplyLoop{ 0, 0xBC1A80 };
		constexpr NativeSite kAEApplyChannels{ 0, 0xBCF570 };
		constexpr NativeSite kAESetFloat{ 0, 0xBD3100 };
		constexpr NativeSite kAESetInt{ 0, 0xBD31D0 };
		constexpr NativeSite kAESendEvents{ 0, 0xBC0C90 };
		constexpr NativeSite kAEGraphSendEvent{ 0, 0xBCF690 };
		constexpr NativeSite kAEGeneratePose{ 0, 0xBCF480 };
		constexpr NativeSite kAEWritePose{ 0, 0xBDFA20 };
		constexpr NativeSite kAEActorUpdate3D{ 0, 0x74C080 };
		constexpr NativeSite kAENiAVObjectUpdate{ 0, 0xEE0930 };
		constexpr NativeSite kAEGetFloat{ 0, 0xBD3420 };
		constexpr NativeSite kAEGetBool{ 0, 0xBD3350 };
		constexpr std::uintptr_t kAEApplyCallOffset = 0x49;
		constexpr std::uintptr_t kAESendEventCallOffset = 0x59;
		constexpr std::uintptr_t kAEWritePoseCallOffset = 0xC1;
		constexpr std::uintptr_t kAEWorldUpdateCallOffset = 0x94;

		struct RuntimeContract
		{
			NativeSite aimInterfaceVtable, aimTarget, applyLoop, applyChannels;
			NativeSite setFloat, setInt, sendEvents, graphSendEvent;
			NativeSite generatePose, writePose, actorUpdate3D, niAVObjectUpdate;
			NativeSite getFloat, getBool;
			std::uintptr_t applyCallOffset, sendEventCallOffset;
			std::uintptr_t writePoseCallOffset, worldUpdateCallOffset;
			const char* runtime;
		};

		constexpr RuntimeContract kSEContract{
			kAimInterfaceVtable, kAimTarget, kApplyLoop, kApplyChannels,
			kSetFloat, kSetInt, kSendEvents, kGraphSendEvent,
			kGeneratePose, kWritePose, kActorUpdate3D, kNiAVObjectUpdate,
			kGetFloat, kGetBool, kApplyCallOffset, kSendEventCallOffset,
			kWritePoseCallOffset, kWorldUpdateCallOffset, "SE 1.5.97"
		};
		constexpr RuntimeContract kAEContract{
			kAEAimInterfaceVtable, kAEAimTarget, kAEApplyLoop, kAEApplyChannels,
			kAESetFloat, kAESetInt, kAESendEvents, kAEGraphSendEvent,
			kAEGeneratePose, kAEWritePose, kAEActorUpdate3D, kAENiAVObjectUpdate,
			kAEGetFloat, kAEGetBool, kAEApplyCallOffset, kAESendEventCallOffset,
			kAEWritePoseCallOffset, kAEWorldUpdateCallOffset, "AE 1.7.104"
		};

		// Steam AE 1.6.1170 and GOG AE 1.6.1179, matched from the 1.7.104
		// sites against those two executables. Intra-function E8 offsets are
		// the 1.7.104 ones. SE Address Library IDs (263725, 62436, …) resolve
		// to different AE functions and must not be reused here.
		constexpr NativeSite kAE1170AimInterfaceVtable{ 0, 0x18BD598 };
		constexpr NativeSite kAE1170AimTarget{ 0, 0x7C6AD0 };
		constexpr NativeSite kAE1170ApplyLoop{ 0, 0xBA5050 };
		constexpr NativeSite kAE1170ApplyChannels{ 0, 0xBB2B30 };
		constexpr NativeSite kAE1170SetFloat{ 0, 0xBB66C0 };
		constexpr NativeSite kAE1170SetInt{ 0, 0xBB6600 };
		constexpr NativeSite kAE1170SendEvents{ 0, 0xBA4260 };
		constexpr NativeSite kAE1170GraphSendEvent{ 0, 0xBB2C50 };
		constexpr NativeSite kAE1170GeneratePose{ 0, 0xBB2A40 };
		constexpr NativeSite kAE1170WritePose{ 0, 0xBC2FE0 };
		constexpr NativeSite kAE1170ActorUpdate3D{ 0, 0x7394B0 };
		constexpr NativeSite kAE1170NiAVObjectUpdate{ 0, 0xD1BF70 };
		constexpr NativeSite kAE1170GetFloat{ 0, 0xBB69E0 };
		constexpr NativeSite kAE1170GetBool{ 0, 0xBB6910 };

		constexpr NativeSite kAE1179AimInterfaceVtable{ 0, 0x18BE948 };
		constexpr NativeSite kAE1179AimTarget{ 0, 0x7C8D00 };
		constexpr NativeSite kAE1179ApplyLoop{ 0, 0xBA6B10 };
		constexpr NativeSite kAE1179ApplyChannels{ 0, 0xBB45F0 };
		constexpr NativeSite kAE1179SetFloat{ 0, 0xBB8180 };
		constexpr NativeSite kAE1179SetInt{ 0, 0xBB80C0 };
		constexpr NativeSite kAE1179SendEvents{ 0, 0xBA5D20 };
		constexpr NativeSite kAE1179GraphSendEvent{ 0, 0xBB4710 };
		constexpr NativeSite kAE1179GeneratePose{ 0, 0xBB4500 };
		constexpr NativeSite kAE1179WritePose{ 0, 0xBC4AA0 };
		constexpr NativeSite kAE1179ActorUpdate3D{ 0, 0x73B6E0 };
		constexpr NativeSite kAE1179NiAVObjectUpdate{ 0, 0xD1D990 };
		constexpr NativeSite kAE1179GetFloat{ 0, 0xBB84A0 };
		constexpr NativeSite kAE1179GetBool{ 0, 0xBB83D0 };

		constexpr RuntimeContract kAE1170Contract{
			kAE1170AimInterfaceVtable, kAE1170AimTarget, kAE1170ApplyLoop, kAE1170ApplyChannels,
			kAE1170SetFloat, kAE1170SetInt, kAE1170SendEvents, kAE1170GraphSendEvent,
			kAE1170GeneratePose, kAE1170WritePose, kAE1170ActorUpdate3D, kAE1170NiAVObjectUpdate,
			kAE1170GetFloat, kAE1170GetBool, kAEApplyCallOffset, kAESendEventCallOffset,
			kAEWritePoseCallOffset, kAEWorldUpdateCallOffset, "AE 1.6.1170"
		};
		constexpr RuntimeContract kAE1179Contract{
			kAE1179AimInterfaceVtable, kAE1179AimTarget, kAE1179ApplyLoop, kAE1179ApplyChannels,
			kAE1179SetFloat, kAE1179SetInt, kAE1179SendEvents, kAE1179GraphSendEvent,
			kAE1179GeneratePose, kAE1179WritePose, kAE1179ActorUpdate3D, kAE1179NiAVObjectUpdate,
			kAE1179GetFloat, kAE1179GetBool, kAEApplyCallOffset, kAESendEventCallOffset,
			kAEWritePoseCallOffset, kAEWorldUpdateCallOffset, "GOG AE 1.6.1179"
		};

		std::atomic<AimTargetFunction*> g_originalAim{ nullptr };
		std::atomic<ApplyChannelsFunction*> g_originalApply{ nullptr };
		std::atomic<SetFloatFunction*> g_setFloat{ nullptr };
		std::atomic<SetIntFunction*> g_setInt{ nullptr };
		std::atomic<SendEventFunction*> g_originalSend{ nullptr };
		std::atomic<WritePoseFunction*> g_originalWritePose{ nullptr };
		std::atomic<UpdateFunction*> g_originalWorldUpdate{ nullptr };
		std::atomic<bool> g_worldUpdateHook{ false };
		std::atomic<std::uint64_t> g_hiddenPoseWrites{ 0 };
		std::atomic<std::uint64_t> g_hiddenWorldUpdates{ 0 };
		std::atomic<bool> g_aimHook{ false };
		std::atomic<bool> g_channelHook{ false };
		std::atomic<bool> g_turnEventHook{ false };
		std::atomic<bool> g_poseHook{ false };
		std::atomic<std::uint64_t> g_aimTargetsWithdrawn{ 0 };
		std::atomic<std::uint64_t> g_channelZeroes{ 0 };
		std::atomic<std::uint64_t> g_turnEventsSteadied{ 0 };
		std::atomic<std::uint64_t> g_headTilts{ 0 };
		std::atomic<std::uint64_t> g_headBoneMisses{ 0 };
		std::atomic<float> g_lastTilt{ 0.0f };
		std::atomic<std::uint64_t> g_faults{ 0 };
		std::atomic<std::uint64_t> g_lastSampleMs{ 0 };
		std::atomic<std::uint32_t> g_samplesLogged{ 0 };

		struct Names
		{
			RE::BSFixedString firstPerson{ "FirstPerson" };
			RE::BSFixedString pitch{ "Pitch" };
			RE::BSFixedString pitchDelta{ "PitchDelta" };
			RE::BSFixedString turnDelta{ "TurnDelta" };
			RE::BSFixedString aimActive{ "bAimActive" };
			RE::BSFixedString aimPitch{ "AimPitchCurrent" };
			RE::BSFixedString isNPC{ "IsNPC" };
			RE::BSFixedString turnDeltaDamped{ "TurnDeltaDamped" };
			RE::BSFixedString syncTurnState{ "iSyncTurnState" };
			RE::BSFixedString turnLeft{ "turnLeft" };
			RE::BSFixedString turnRight{ "turnRight" };
			RE::BSFixedString turnStop{ "turnStop" };
			RE::BSFixedString neckBone{ "NPC Neck [Neck]" };
			RE::BSFixedString headBone{ "NPC Head [Head]" };
		};
		// Never destroyed: the engine's string pool is gone before static
		// destructors run.
		std::atomic<const Names*> g_names{ nullptr };

		[[nodiscard]] bool Resolve(const NativeSite& a_site, std::uintptr_t& a_address) noexcept
		{
			try {
				if (!a_site.id) {
					a_address = REL::Module::get().base() + a_site.rva;
					return a_address != 0;
				}
				const REL::ID id{ a_site.id };
				if (id.offset() != a_site.rva)
					return false;
				a_address = id.address();
				return a_address != 0;
			} catch (...) {
				return false;
			}
		}

		[[nodiscard]] bool ReadPointerSEH(std::uintptr_t a_address, std::uintptr_t& a_value) noexcept
		{
			__try {
				a_value = *reinterpret_cast<const std::uintptr_t*>(a_address);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool ReadCallTargetSEH(std::uintptr_t a_site, std::uintptr_t& a_target) noexcept
		{
			__try {
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_site);
				if (bytes[0] != 0xE8)
					return false;
				std::int32_t displacement = 0;
				std::memcpy(&displacement, bytes + 1, sizeof(displacement));
				a_target = a_site + 5 + static_cast<std::intptr_t>(displacement);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// The player's third-person graph while the player is in first person.
		// The graph's own project name is the engine's test for the first-person
		// graph (PlayerCharacter graph-variable init, SE 0x1406B8C20), so the
		// graph index never matters.
		[[nodiscard]] bool HiddenBodyGraphSEH(RE::BShkbAnimationGraph* a_graph, const Names* a_names) noexcept
		{
			__try {
				if (!a_graph || !a_names)
					return false;
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_graph->holder != static_cast<RE::Actor*>(player))
					return false;
				if (player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode)
					return false;
				return a_graph->projectName.c_str() != a_names->firstPerson.c_str();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// hkbContext::character -> behaviorGraph (+0x58) -> userData (+0x30) is
		// the BShkbAnimationGraph, exactly as the native callback reads it.
		[[nodiscard]] RE::BShkbAnimationGraph* GraphFromContextSEH(RE::hkbContext* a_context) noexcept
		{
			__try {
				if (!a_context || !a_context->character)
					return nullptr;
				auto* behavior = a_context->character->behaviorGraph.get();
				if (!behavior)
					return nullptr;
				return reinterpret_cast<RE::BShkbAnimationGraph*>(
					static_cast<std::uintptr_t>(behavior->userData));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		void WithdrawAimSEH(RE::hkbContext* a_context, bool* a_valid, const Names* a_names) noexcept
		{
			__try {
				if (!*a_valid)
					return;
				if (HiddenBodyGraphSEH(GraphFromContextSEH(a_context), a_names)) {
					*a_valid = false;
					g_aimTargetsWithdrawn.fetch_add(1, std::memory_order_relaxed);
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_faults.fetch_add(1, std::memory_order_relaxed);
			}
		}

		// Job-thread safe: no allocation, no locks, reads and one flag write.
		void AimTargetThunk(void* a_self, RE::hkbContext* a_context, bool* a_valid,
			RE::hkVector4* a_target, std::int32_t a_userInfo)
		{
			const auto* original = g_originalAim.load(std::memory_order_acquire);
			if (!original)
				return;
			original(a_self, a_context, a_valid, a_target, a_userInfo);
			// 1 is head tracking (already declined natively); 2 and 3 are the
			// view-direction and dragon-ride targets.
			if ((a_userInfo != 2 && a_userInfo != 3) || !a_valid)
				return;
			if (const auto* names = g_names.load(std::memory_order_acquire))
				WithdrawAimSEH(a_context, a_valid, names);
		}

		struct Sample
		{
			float pitch{ 0.0f };
			float pitchDelta{ 0.0f };
			float turnDelta{ 0.0f };
			float aimPitch{ 0.0f };
			float turnDeltaDamped{ 0.0f };
			int syncTurnState{ 0 };
			bool aimActive{ false };
			bool isNPC{ false };
			std::uint8_t read{ 0 };
		};

		[[nodiscard]] bool ReadSampleSEH(RE::BShkbAnimationGraph* a_graph, const Names* a_names, Sample& a_sample) noexcept
		{
			__try {
				unsigned read = 0;
				if (a_graph->GetGraphVariableFloat(a_names->pitch, a_sample.pitch))
					read |= 0x01u;
				if (a_graph->GetGraphVariableFloat(a_names->pitchDelta, a_sample.pitchDelta))
					read |= 0x02u;
				if (a_graph->GetGraphVariableFloat(a_names->turnDelta, a_sample.turnDelta))
					read |= 0x04u;
				if (a_graph->GetGraphVariableFloat(a_names->aimPitch, a_sample.aimPitch))
					read |= 0x08u;
				if (a_graph->GetGraphVariableBool(a_names->aimActive, a_sample.aimActive))
					read |= 0x10u;
				if (a_graph->GetGraphVariableBool(a_names->isNPC, a_sample.isNPC))
					read |= 0x20u;
				if (a_graph->GetGraphVariableFloat(a_names->turnDeltaDamped, a_sample.turnDeltaDamped))
					read |= 0x40u;
				if (a_graph->GetGraphVariableInt(a_names->syncTurnState, a_sample.syncTurnState))
					read |= 0x80u;
				a_sample.read = static_cast<std::uint8_t>(read);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// The runtime check the findings ask for: what the hidden body's graph is
		// fed, and whether the zeroing actually lands in the graph it updates next.
		// Owner, run 5: "when i turn left and right the arms/hand still goes up and
		// down" -- with TurnDelta live before zeroing, the after column decides
		// whether the setter missed or something else drives the turn.
		[[nodiscard]] bool SampleDue() noexcept
		{
			constexpr std::uint32_t kMaximumSamples = 40;
			constexpr std::uint64_t kSampleIntervalMs = 15000;
			const auto logged = g_samplesLogged.load(std::memory_order_relaxed);
			if (logged >= kMaximumSamples)
				return false;
			const auto now = static_cast<std::uint64_t>(GetTickCount64());
			auto last = g_lastSampleMs.load(std::memory_order_relaxed);
			if (logged >= 3 && now - last < kSampleIntervalMs)
				return false;
			if (!g_lastSampleMs.compare_exchange_strong(last, now, std::memory_order_relaxed))
				return false;
			g_samplesLogged.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void LogSample(const Sample& a_before, const Sample& a_after) noexcept
		{
			try {
				logger::info(
					"[MOS][SteadyArms] hidden-body graph before/after zeroing: Pitch={:.2f}/{:.2f} PitchDelta={:.2f}/{:.2f} TurnDelta={:.3f}/{:.3f} TurnDeltaDamped={:.3f}/{:.3f} iSyncTurnState={}/{} AimPitchCurrent={:.2f} bAimActive={} IsNPC={} read=0x{:02X}/0x{:02X} aimWithdrawn={} channelZeroes={} turnEventsSteadied={} headTilt={:.3f} headTilts={} headBoneMisses={}",
					a_before.pitch, a_after.pitch, a_before.pitchDelta, a_after.pitchDelta,
					a_before.turnDelta, a_after.turnDelta, a_before.turnDeltaDamped, a_after.turnDeltaDamped,
					a_before.syncTurnState, a_after.syncTurnState, a_before.aimPitch,
					a_before.aimActive, a_before.isNPC, a_before.read, a_after.read,
					g_aimTargetsWithdrawn.load(std::memory_order_relaxed),
					g_channelZeroes.load(std::memory_order_relaxed),
					g_turnEventsSteadied.load(std::memory_order_relaxed),
					g_lastTilt.load(std::memory_order_relaxed),
					g_headTilts.load(std::memory_order_relaxed),
					g_headBoneMisses.load(std::memory_order_relaxed));
			} catch (...) {
			}
		}

		[[nodiscard]] bool ZeroChannelsSEH(RE::BShkbAnimationGraph* a_graph, const Names* a_names,
			SetFloatFunction* a_setFloat, SetIntFunction* a_setInt) noexcept
		{
			__try {
				(void)a_setFloat(a_graph, a_names->pitch, 0.0f);
				(void)a_setFloat(a_graph, a_names->pitchDelta, 0.0f);
				(void)a_setFloat(a_graph, a_names->turnDelta, 0.0f);
				// The turn action writes iSyncTurnState (right 0, stop 1, left 2)
				// to every graph; a magic state entered mid-turn starts from it.
				if (a_setInt)
					(void)a_setInt(a_graph, a_names->syncTurnState, 1);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void ApplyChannelsThunk(RE::BShkbAnimationGraph* a_graph, void* a_channels)
		{
			const auto* original = g_originalApply.load(std::memory_order_acquire);
			if (!original)
				return;
			original(a_graph, a_channels);
			const auto* names = g_names.load(std::memory_order_acquire);
			auto* setFloat = g_setFloat.load(std::memory_order_acquire);
			if (!names || !setFloat || !HiddenBodyGraphSEH(a_graph, names))
				return;
			const bool sample = SampleDue();
			Sample before{};
			if (sample && !ReadSampleSEH(a_graph, names, before))
				before = {};
			if (ZeroChannelsSEH(a_graph, names, setFloat, g_setInt.load(std::memory_order_acquire)))
				g_channelZeroes.fetch_add(1, std::memory_order_relaxed);
			else
				g_faults.fetch_add(1, std::memory_order_relaxed);
			if (sample) {
				Sample after{};
				if (!ReadSampleSEH(a_graph, names, after))
					after = {};
				LogSample(before, after);
			}
		}

		// Owner, run 5: "when i turn left and right the arms/hand still goes up
		// and down". The turn steps are not a channel: the engine's turn action
		// sends turnLeft/turnRight to every graph of the player, and the magic
		// ready/cast behaviours switch into their Mag_Turn clips on those events
		// with no condition. The hidden body gets turnStop instead, so it turns
		// with the actor's heading and never plays a turn step.
		[[nodiscard]] bool IsTurnStepSEH(const RE::BSFixedString* a_event, const Names* a_names) noexcept
		{
			__try {
				const auto* name = a_event->c_str();
				return name == a_names->turnLeft.c_str() || name == a_names->turnRight.c_str();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// Runs under the manager spin lock: reads only, then the native send.
		bool SendEventThunk(RE::BShkbAnimationGraph* a_graph, const RE::BSFixedString* a_event)
		{
			const auto* original = g_originalSend.load(std::memory_order_acquire);
			if (!original)
				return false;
			const auto* names = g_names.load(std::memory_order_acquire);
			if (names && a_event && IsTurnStepSEH(a_event, names) && HiddenBodyGraphSEH(a_graph, names)) {
				g_turnEventsSteadied.fetch_add(1, std::memory_order_relaxed);
				return original(a_graph, &names->turnStop);
			}
			return original(a_graph, a_event);
		}

		// Last local rotation this module wrote per bone. A pose write that did
		// not reach a bone leaves that value in place; it is then kept as it is
		// rather than tilted a second time.
		struct Written
		{
			RE::NiAVObject* node{ nullptr };
			MirrorHeadTiltMath::Mat3 rotate{};
		};
		Written g_neckWritten{};
		Written g_headWritten{};
		std::atomic<std::int32_t> g_neckIndex{ -1 };
		std::atomic<std::int32_t> g_headIndex{ -1 };
		std::atomic_flag g_tiltBusy = ATOMIC_FLAG_INIT;

		[[nodiscard]] MirrorHeadTiltMath::Mat3 ToMat3(const RE::NiMatrix3& a_matrix) noexcept
		{
			MirrorHeadTiltMath::Mat3 result{};
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					result[i][j] = a_matrix.entry[i][j];
			return result;
		}

		void FromMat3(const MirrorHeadTiltMath::Mat3& a_matrix, RE::NiMatrix3& a_out) noexcept
		{
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					a_out.entry[i][j] = a_matrix[i][j];
		}

		// Bone lookup by name (XPMSE and other skeletons reorder bones); the
		// index is cached and re-checked on every use.
		[[nodiscard]] RE::NiAVObject* FindBone(BoneNodes& a_bones, std::uint32_t a_count,
			const RE::BSFixedString& a_name, std::atomic<std::int32_t>& a_cache) noexcept
		{
			const std::uint32_t count = (std::min)(a_count, a_bones.size());
			const auto* wanted = a_name.c_str();
			const auto cached = a_cache.load(std::memory_order_relaxed);
			if (cached >= 0 && static_cast<std::uint32_t>(cached) < count) {
				auto* node = a_bones[static_cast<std::uint32_t>(cached)].node;
				if (node && node->name.c_str() == wanted)
					return node;
			}
			for (std::uint32_t i = 0; i < count; ++i) {
				auto* node = a_bones[i].node;
				if (node && node->name.c_str() == wanted) {
					a_cache.store(static_cast<std::int32_t>(i), std::memory_order_relaxed);
					return node;
				}
			}
			a_cache.store(-1, std::memory_order_relaxed);
			return nullptr;
		}

		[[nodiscard]] bool TiltBone(RE::NiAVObject* a_node, float a_heading, float a_tilt, Written& a_written) noexcept
		{
			if (!a_node || !a_node->parent)
				return false;
			const auto current = ToMat3(a_node->local.rotate);
			if (a_written.node == a_node && current == a_written.rotate)
				return false;  // not rewritten since our last tilt
			a_written.node = nullptr;
			if (std::fabs(a_tilt) < MirrorHeadTiltMath::kMinimumTilt)
				return false;
			const auto parent = ToMat3(a_node->parent->world.rotate);
			if (!MirrorHeadTiltMath::IsRotation(parent) || !MirrorHeadTiltMath::IsRotation(current))
				return false;
			FromMat3(MirrorHeadTiltMath::TiltLocal(parent, current, a_heading, a_tilt), a_node->local.rotate);
			a_written = { a_node, ToMat3(a_node->local.rotate) };
			return true;
		}

		// Both routes end here. A bone the other route already tilted since its
		// pose was last written is left as it is (TiltBone's Written record).
		void TiltBones(RE::NiAVObject* a_neck, RE::NiAVObject* a_head) noexcept
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player)
				return;
			const float tilt = MirrorHeadTiltMath::TiltFor(player->data.angle.x);
			const float heading = player->data.angle.z;
			g_lastTilt.store(tilt, std::memory_order_relaxed);
			if (!a_head) {
				g_headBoneMisses.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			const float neckTilt = a_neck ? tilt * MirrorHeadTiltMath::kNeckShare : 0.0f;
			bool tilted = TiltBone(a_neck, heading, neckTilt, g_neckWritten);
			tilted = TiltBone(a_head, heading, tilt - neckTilt, g_headWritten) || tilted;
			if (tilted)
				g_headTilts.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] bool TiltHeadSEH(RE::BShkbAnimationGraph* a_graph, const Names* a_names, std::uint32_t a_count) noexcept
		{
			__try {
				TiltBones(FindBone(a_graph->boneNodes, a_count, a_names->neckBone, g_neckIndex),
					FindBone(a_graph->boneNodes, a_count, a_names->headBone, g_headIndex));
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// The hidden body's third-person root, and its neck and head by name.
		[[nodiscard]] bool TiltHiddenRootSEH(RE::NiAVObject* a_root, const Names* a_names) noexcept
		{
			__try {
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || !a_root || player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode ||
					a_root != player->Get3D1(false))
					return true;
				g_hiddenWorldUpdates.fetch_add(1, std::memory_order_relaxed);
				TiltBones(a_root->GetObjectByName(a_names->neckBone), a_root->GetObjectByName(a_names->headBone));
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// Before the actor's 3D update of its third-person root; main thread.
		void WorldUpdateThunk(RE::NiAVObject* a_root, RE::NiUpdateData* a_data)
		{
			const auto* original = g_originalWorldUpdate.load(std::memory_order_acquire);
			if (!original)
				return;
			if (const auto* names = g_names.load(std::memory_order_acquire); names && a_root &&
				!g_tiltBusy.test_and_set(std::memory_order_acquire)) {
				if (!TiltHiddenRootSEH(a_root, names))
					g_faults.fetch_add(1, std::memory_order_relaxed);
				g_tiltBusy.clear(std::memory_order_release);
			}
			original(a_root, a_data);
		}

		// After the native pose write, and only for the hidden body: the actor's
		// 3D update that follows (SE 0x1406A4F34) carries the tilt from the neck
		// and head to the face and everything the head holds.
		void WritePoseThunk(void* a_pose, BoneNodes* a_bones, std::uint32_t a_count)
		{
			const auto* original = g_originalWritePose.load(std::memory_order_acquire);
			if (!original)
				return;
			original(a_pose, a_bones, a_count);
			const auto* names = g_names.load(std::memory_order_acquire);
			if (!names || !a_bones)
				return;
			auto* graph = reinterpret_cast<RE::BShkbAnimationGraph*>(
				reinterpret_cast<std::uintptr_t>(a_bones) - kBoneNodesOffset);
			if (!HiddenBodyGraphSEH(graph, names))
				return;
			g_hiddenPoseWrites.fetch_add(1, std::memory_order_relaxed);
			if (g_tiltBusy.test_and_set(std::memory_order_acquire))
				return;
			if (!TiltHeadSEH(graph, names, a_count))
				g_faults.fetch_add(1, std::memory_order_relaxed);
			g_tiltBusy.clear(std::memory_order_release);
		}

		// Patches one call site when it still calls the recorded target.
		template <class Thunk, class Original>
		[[nodiscard]] bool PatchCall(std::uintptr_t a_site, std::uintptr_t a_expected,
			std::atomic<Original*>& a_original, Thunk a_thunk, const char* a_what)
		{
			std::uintptr_t target = 0;
			if (!ReadCallTargetSEH(a_site, target) || target != a_expected) {
				logger::warn("[MOS][SteadyArms] {} call is not native (target 0x{:X}); left alone", a_what, target);
				return false;
			}
			// Published before the call changes, so a thread entering the thunk
			// mid-install still chains.
			a_original.store(reinterpret_cast<Original*>(a_expected), std::memory_order_release);
			const auto captured = SKSE::GetTrampoline().write_call<5>(a_site, a_thunk);
			return captured == a_expected;
		}
	}

	void Install() noexcept
	{
		try {
			if (g_aimHook.load() || g_channelHook.load())
				return;
			const auto version = REL::Module::get().version();
			const RuntimeContract* contract = nullptr;
			if (REL::Module::IsSE() && version == REL::Version{ 1, 5, 97, 0 })
				contract = &kSEContract;
			else if (REL::Module::IsAE() && version == REL::Version{ 1, 7, 104, 0 })
				contract = &kAEContract;
			else if (REL::Module::IsAE() && version == REL::Version{ 1, 6, 1170, 0 })
				contract = &kAE1170Contract;
			else if (REL::Module::IsAE() &&
				(version == REL::Version{ 1, 6, 1179, 0 } || version == REL::Version{ 1, 6, 1179, 1 }))
				contract = &kAE1179Contract;
			if (!contract) {
				logger::info("[MOS][SteadyArms] not installed: supported hook contracts are SE 1.5.97, AE 1.6.1170, GOG AE 1.6.1179, and AE 1.7.104");
				return;
			}
			std::uintptr_t vtable = 0, aimTarget = 0, applyLoop = 0, applyChannels = 0,
						   setFloat = 0, getFloat = 0, getBool = 0;
			if (!Resolve(contract->aimInterfaceVtable, vtable) || !Resolve(contract->aimTarget, aimTarget) ||
				!Resolve(contract->applyLoop, applyLoop) || !Resolve(contract->applyChannels, applyChannels) ||
				!Resolve(contract->setFloat, setFloat) || !Resolve(contract->getFloat, getFloat) ||
				!Resolve(contract->getBool, getBool)) {
				logger::warn("[MOS][SteadyArms] not installed: {} native sites did not resolve", contract->runtime);
				return;
			}
			// The turn filter and head tilt resolve separately: a mismatch there
			// leaves the two hooks above working.
			std::uintptr_t setInt = 0, sendEvents = 0, graphSendEvent = 0, generatePose = 0, writePose = 0;
			const bool turnSites = Resolve(contract->setInt, setInt) && Resolve(contract->sendEvents, sendEvents) &&
			                       Resolve(contract->graphSendEvent, graphSendEvent);
			const bool poseSites = Resolve(contract->generatePose, generatePose) && Resolve(contract->writePose, writePose);
			std::uintptr_t actorUpdate3D = 0, niUpdate = 0;
			const bool worldSites = Resolve(contract->actorUpdate3D, actorUpdate3D) && Resolve(contract->niAVObjectUpdate, niUpdate);
			g_setFloat.store(reinterpret_cast<SetFloatFunction*>(setFloat), std::memory_order_release);
			if (turnSites)
				g_setInt.store(reinterpret_cast<SetIntFunction*>(setInt), std::memory_order_release);
			g_names.store(new Names{}, std::memory_order_release);

			const auto slotAddress = vtable + kAimTargetSlot * sizeof(std::uintptr_t);
			const auto aimThunk = reinterpret_cast<std::uintptr_t>(&AimTargetThunk);
			std::uintptr_t slotValue = 0;
			if (ReadPointerSEH(slotAddress, slotValue) && slotValue == aimTarget) {
				// The original is published before the slot changes, so a job
				// thread entering the thunk mid-install still chains.
				g_originalAim.store(reinterpret_cast<AimTargetFunction*>(aimTarget), std::memory_order_release);
				REL::Relocation<std::uintptr_t> table{ vtable };
				const auto captured = table.write_vfunc(kAimTargetSlot, aimThunk);
				std::uintptr_t installed = 0;
				g_aimHook.store(captured == aimTarget && ReadPointerSEH(slotAddress, installed) &&
									installed == aimThunk,
					std::memory_order_release);
			} else {
				logger::warn("[MOS][SteadyArms] aim target slot is not native (0x{:X}); another plugin owns it, left alone", slotValue);
			}

			g_channelHook.store(PatchCall(applyLoop + contract->applyCallOffset, applyChannels, g_originalApply,
									&ApplyChannelsThunk, "channel apply"),
				std::memory_order_release);
			if (turnSites) {
				g_turnEventHook.store(PatchCall(sendEvents + contract->sendEventCallOffset, graphSendEvent, g_originalSend,
										  &SendEventThunk, "graph event send"),
					std::memory_order_release);
			} else {
				logger::warn("[MOS][SteadyArms] turn-step filter not installed: {} sites did not resolve", contract->runtime);
			}
			if (poseSites) {
				g_poseHook.store(PatchCall(generatePose + contract->writePoseCallOffset, writePose, g_originalWritePose,
									 &WritePoseThunk, "pose write"),
					std::memory_order_release);
			} else {
				logger::warn("[MOS][SteadyArms] head tilt not installed: {} sites did not resolve", contract->runtime);
			}
			if (worldSites) {
				g_worldUpdateHook.store(PatchCall(actorUpdate3D + contract->worldUpdateCallOffset, niUpdate, g_originalWorldUpdate,
											&WorldUpdateThunk, "actor 3D update"),
					std::memory_order_release);
			} else {
				logger::warn("[MOS][SteadyArms] head tilt fallback not installed: {} sites did not resolve", contract->runtime);
			}
			logger::info(
				"[MOS][SteadyArms] runtime={} aimTargetHook={} channelHook={} turnStepFilter={} headTilt(poseWrite/worldUpdate)={}/{} (first person: the reflected body gets no aim target, no turn steps and no pitch, and its head follows the view pitch; the first-person graph is untouched)",
				contract->runtime, g_aimHook.load(), g_channelHook.load(), g_turnEventHook.load(), g_poseHook.load(), g_worldUpdateHook.load());
		} catch (...) {
			logger::warn("[MOS][SteadyArms] install threw; hooks may be partial (aimTargetHook={} channelHook={} turnStepFilter={} headTilt(poseWrite/worldUpdate)={}/{})",
				g_aimHook.load(), g_channelHook.load(), g_turnEventHook.load(), g_poseHook.load(), g_worldUpdateHook.load());
		}
	}

	Diagnostics Snapshot() noexcept
	{
		return {
			.aimTargetHook = g_aimHook.load(std::memory_order_relaxed),
			.channelHook = g_channelHook.load(std::memory_order_relaxed),
			.turnStepFilter = g_turnEventHook.load(std::memory_order_relaxed),
			.headTiltHook = g_poseHook.load(std::memory_order_relaxed),
			.headTiltFallbackHook = g_worldUpdateHook.load(std::memory_order_relaxed),
			.aimTargetsWithdrawn = g_aimTargetsWithdrawn.load(std::memory_order_relaxed),
			.channelZeroes = g_channelZeroes.load(std::memory_order_relaxed),
			.turnStepsSteadied = g_turnEventsSteadied.load(std::memory_order_relaxed),
			.headTilts = g_headTilts.load(std::memory_order_relaxed),
			.headBoneMisses = g_headBoneMisses.load(std::memory_order_relaxed),
			.hiddenPoseWrites = g_hiddenPoseWrites.load(std::memory_order_relaxed),
			.hiddenWorldUpdates = g_hiddenWorldUpdates.load(std::memory_order_relaxed),
			.faults = g_faults.load(std::memory_order_relaxed)
		};
	}
}
