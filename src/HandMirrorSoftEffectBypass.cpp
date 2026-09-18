#include "PCH.h"

#include "HandMirrorSoftEffectBypass.h"

#include "HandMirrorSoftEffectHookChainPolicy.h"
#include "HandMirrorSoftEffectPolicy.h"
#include "HandMirrorSafetySettings.h"
#include "NoDestructor.h"
#if defined(MIRRORS_OF_SKYRIM_STANDALONE)
#	include "MirrorPlayerDrawPassProbe.h"
#else
#	include "PlayerDrawPassProbe.h"
#endif
#include "SecondView.h"
#include "MirrorSurfaceLights.h"
#include "MirrorCaptureProfile.h"
#include "MirrorDrawAlphaPolicy.h"
#include "MirrorWaterCapturePolicy.h"

#include <array>
#include <bcrypt.h>
#include <cstring>
#include <limits>
#include <type_traits>
#include <wrl/client.h>

namespace HandMirrorSoftEffectBypass
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr REL::RelocationID kSetupAndDrawPass{ 100854, 107644 };
		constexpr REL::Version kSupportedSE{ 1, 5, 97, 0 };
		constexpr REL::Version kSupportedAE{ 1, 6, 1170, 0 };
		constexpr std::uint64_t kCommunityShaders184Length = 20898816;
		constexpr std::array<std::uint8_t, 32> kCommunityShaders184SHA256{
			0xBD, 0xF6, 0x55, 0xFB, 0x2C, 0xCA, 0x15, 0x7C,
			0x3B, 0xD8, 0xDE, 0x93, 0x06, 0xC9, 0xB7, 0x0F,
			0x25, 0x12, 0x0B, 0xA1, 0xE2, 0x77, 0x7F, 0x60,
			0x84, 0x3C, 0x63, 0x89, 0xBF, 0x2B, 0xB8, 0x8F
		};
		constexpr std::uint64_t kEngineFixes7020Length = 2453504;
		constexpr std::array<std::uint8_t, 32> kEngineFixes7020SHA256{
			0xFB, 0x7E, 0x73, 0x17, 0x86, 0xB6, 0x48, 0x52,
			0xA2, 0xAC, 0x4E, 0x05, 0x72, 0x84, 0x5D, 0x97,
			0xA8, 0xAF, 0x4C, 0xE1, 0xFA, 0x69, 0xB0, 0xBA,
			0xA5, 0x8A, 0x43, 0xCE, 0x34, 0x42, 0x81, 0x25
		};
		constexpr std::uintptr_t kEngineFixes7020OwnerRVA = 0x2CF80;
		constexpr std::array<std::uint8_t, 16> kEngineFixes7020OwnerPrologue{
			0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
			0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83
		};
		constexpr std::uint64_t kAE161170EngineFixesLength = 2456064;
		constexpr std::array<std::uint8_t, 32> kAE161170EngineFixesSHA256{
			0x5D, 0x13, 0x84, 0xAC, 0xFB, 0x52, 0x3A, 0xBD,
			0x13, 0x33, 0xF5, 0xAF, 0x71, 0xAF, 0x0B, 0x7D,
			0x13, 0x1B, 0x6E, 0xBB, 0x1A, 0x0E, 0xE6, 0xB3,
			0xED, 0xFF, 0x86, 0xFB, 0x4C, 0x93, 0xAD, 0xF3
		};
		constexpr std::uintptr_t kAE161170EngineFixesOwnerRVA = 0x2CF90;
		constexpr std::array<std::uint8_t, 16>
			kAE161170EngineFixesOwnerPrologue{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
				0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83
			};
		constexpr std::size_t kEntryInspectionBytes = 16;
		constexpr std::size_t kSafetyHookRelayBytes = 14;
		constexpr GUID kD3DDebugObjectName{
			0x429b8c22, 0x9188, 0x4b0c,
			{ 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 }
		};

		struct RuntimeContract
		{
			HandMirrorSoftEffectHookChainPolicy::Runtime runtime{
				HandMirrorSoftEffectHookChainPolicy::Runtime::kUnsupported };
			std::uintptr_t dispatcherRVA{ 0 };
			std::uint64_t engineFixesLength{ 0 };
			const std::array<std::uint8_t, 32>* engineFixesSHA256{ nullptr };
			std::uintptr_t engineFixesOwnerRVA{ 0 };
			const std::array<std::uint8_t, 16>* engineFixesOwnerPrologue{ nullptr };
			bool requiresPinnedCommunityShaders{ false };
			const char* name{ "unsupported" };
		};

		constexpr RuntimeContract kSE1597Contract{
			.runtime = HandMirrorSoftEffectHookChainPolicy::Runtime::kSkyrimSE1597,
			.dispatcherRVA = 0x1308440,
			.engineFixesLength = kEngineFixes7020Length,
			.engineFixesSHA256 = &kEngineFixes7020SHA256,
			.engineFixesOwnerRVA = kEngineFixes7020OwnerRVA,
			.engineFixesOwnerPrologue = &kEngineFixes7020OwnerPrologue,
			.requiresPinnedCommunityShaders = true,
			.name = "SE 1.5.97"
		};
		constexpr RuntimeContract kAE161170Contract{
			.runtime = HandMirrorSoftEffectHookChainPolicy::Runtime::kSkyrimAE161170,
			.dispatcherRVA = 0x14F3DC0,
			.engineFixesLength = kAE161170EngineFixesLength,
			.engineFixesSHA256 = &kAE161170EngineFixesSHA256,
			.engineFixesOwnerRVA = kAE161170EngineFixesOwnerRVA,
			.engineFixesOwnerPrologue = &kAE161170EngineFixesOwnerPrologue,
			.requiresPinnedCommunityShaders = false,
			.name = "AE 1.6.1170"
		};
		// Skyrim 1.7.104: BSBatchRenderer::SetupAndDrawPass (ID 107644) resolves to
		// 0x1560340 and its entry equals kNativeAE161170Entry exactly, so only the
		// dispatcher RVA differs from the AE contract.  The Engine Fixes identity
		// fields describe the AE build of EngineFixes.dll and are unchanged.
		constexpr RuntimeContract kAE17104Contract{
			.runtime = HandMirrorSoftEffectHookChainPolicy::Runtime::kSkyrimAE17104,
			.dispatcherRVA = 0x1560340,
			.engineFixesLength = kAE161170EngineFixesLength,
			.engineFixesSHA256 = &kAE161170EngineFixesSHA256,
			.engineFixesOwnerRVA = kAE161170EngineFixesOwnerRVA,
			.engineFixesOwnerPrologue = &kAE161170EngineFixesOwnerPrologue,
			.requiresPinnedCommunityShaders = false,
			.name = "AE 1.7.104"
		};
		// GOG 1.6.1179: BSBatchRenderer::SetupAndDrawPass (ID 107644) resolves to
		// 0x14F4E30.  The Engine Fixes identity is the Steam AE build and must
		// not be applied here, so only the native AE entry is accepted (VR-like).
		constexpr RuntimeContract kAE161179Contract{
			.runtime = HandMirrorSoftEffectHookChainPolicy::Runtime::kSkyrimAE161179,
			.dispatcherRVA = 0x14F4E30,
			.engineFixesLength = 0,
			.engineFixesSHA256 = nullptr,
			.engineFixesOwnerRVA = 0,
			.engineFixesOwnerPrologue = nullptr,
			.requiresPinnedCommunityShaders = false,
			.name = "GOG AE 1.6.1179"
		};
		// Skyrim VR 1.4.15: SetupAndDrawPass (ID 100854 -> VR CSV 0x1349680) has
		// SE-identical entry bytes.  No Engine Fixes SafetyHook chain is pinned on
		// VR (null identity fields), so only the native entry is accepted there.
		constexpr RuntimeContract kVR1415Contract{
			.runtime = HandMirrorSoftEffectHookChainPolicy::Runtime::kSkyrimVR1415,
			.dispatcherRVA = 0x1349680,
			.engineFixesLength = 0,
			.engineFixesSHA256 = nullptr,
			.engineFixesOwnerRVA = 0,
			.engineFixesOwnerPrologue = nullptr,
			.requiresPinnedCommunityShaders = false,
			.name = "VR 1.4.15"
		};

		[[nodiscard]] const RuntimeContract* ExactRuntimeContract() noexcept
		{
			const auto version = REL::Module::get().version();
			if (REL::Module::IsSE() && version == kSupportedSE)
				return &kSE1597Contract;
			if (SupportedRuntimePolicy::IsExactAE17104Runtime())
				return &kAE17104Contract;
			if (SupportedRuntimePolicy::IsExactAE161179Runtime())
				return &kAE161179Contract;
			if (SupportedRuntimePolicy::IsExactAE161170Runtime())
				return &kAE161170Contract;
			if (SupportedRuntimePolicy::IsExactVRRuntime())
				return &kVR1415Contract;
			return nullptr;
		}

		std::atomic_bool g_installAttempted{ false };
		std::atomic_bool g_installed{ false };
		std::atomic_bool g_faulted{ false };
		std::atomic_bool g_alphaDrawEnabled{ false };
		std::atomic<std::uint64_t> g_alphaDrawCorrections{ 0 };
		std::atomic<std::uint64_t> g_alphaDrawReadFaults{ 0 };
		std::atomic<std::uint64_t> g_placedEffectDrawsSeen{ 0 };
		std::atomic<std::uint64_t> g_placedParticleDrawsSeen{ 0 };
		std::atomic<std::uint64_t> g_placedEffectParticleSkipped{ 0 };
		std::atomic<std::uint64_t> g_placedEffectPrivateDepth{ 0 };
		std::atomic<std::uint64_t> g_placedFirstPersonEffectSkips{ 0 };
		std::atomic<std::uint64_t> g_nullShaderPassSkips{ 0 };
		std::atomic<bool> g_nullShaderPassLogged{ false };
		// First 48 distinct Effect/Particle geometry names drawn inside placed
		// captures, logged once each with their owner reference, so a missing
		// torch flame or campfire can be told apart from world fog/glow cards.
		SRWLOCK g_placedEffectNameLock = SRWLOCK_INIT;
		std::atomic<bool> g_placedEffectNamesFull{ false };
		std::size_t g_placedEffectNameCount = 0;
		std::array<std::uint64_t, 48> g_placedEffectNameHashes{};
		void NotePlacedEffectName(const RE::BSRenderPass* pass, bool particle) noexcept
		{
			// This is a bounded startup census. Once full, avoid reading/hashing
			// names and taking a lock on every reflected effect draw.
			if (g_placedEffectNamesFull.load(std::memory_order_relaxed))
				return;
			__try {
				auto* geometry = pass ? pass->geometry : nullptr;
				const char* name = geometry ? geometry->name.c_str() : nullptr;
				if (!name)
					return;
				// Run 8: sky cloud cards (CloudDistant*/CloudShape*/INV_Cloud*) filled
				// every slot before any flame could be recorded. Skip them here; the
				// counters still include them.
				if (std::strstr(name, "Cloud") != nullptr)
					return;
				std::uint64_t hash = 1469598103934665603ull;
				for (const char* p = name; *p; ++p) hash = (hash ^ static_cast<unsigned char>(*p)) * 1099511628211ull;
				AcquireSRWLockExclusive(&g_placedEffectNameLock);
				bool seen = false;
				for (std::size_t i = 0; i < g_placedEffectNameCount; ++i)
					if (g_placedEffectNameHashes[i] == hash) { seen = true; break; }
				const bool room = g_placedEffectNameCount < g_placedEffectNameHashes.size();
				if (!seen && room)
					g_placedEffectNameHashes[g_placedEffectNameCount++] = hash;
				if (g_placedEffectNameCount == g_placedEffectNameHashes.size())
					g_placedEffectNamesFull.store(true, std::memory_order_relaxed);
				ReleaseSRWLockExclusive(&g_placedEffectNameLock);
				if (seen || !room)
					return;
				auto* reference = geometry->GetUserData();
				auto* base = reference ? reference->GetBaseObject() : nullptr;
				const auto& bound = geometry->worldBound;
				logger::info("[MOS][PlacedEffects] geometry name=\"{}\" kind={} ref=0x{:08X} base=0x{:08X} bound=({:.0f},{:.0f},{:.0f}) r={:.1f} alpha={}",
					name, particle ? "particle" : "effect",
					reference ? reference->GetFormID() : 0u, base ? base->GetFormID() : 0u,
					bound.center.x, bound.center.y, bound.center.z, bound.radius,
					geometry->GetGeometryRuntimeData().alphaProperty.get() != nullptr);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}
		std::atomic<std::uint64_t> g_arms{ 0 };
		std::atomic<std::uint64_t> g_commits{ 0 };
		std::atomic<std::uint64_t> g_reapplies{ 0 };
		std::atomic<std::uint64_t> g_restores{ 0 };
		std::atomic<std::uint64_t> g_playerMatches{ 0 };
		std::atomic<std::uint64_t> g_playerArms{ 0 };
		std::atomic<std::uint64_t> g_playerCommits{ 0 };
		std::atomic<std::uint64_t> g_playerReapplies{ 0 };
		std::atomic<std::uint64_t> g_playerRestores{ 0 };
		std::atomic<std::uint64_t> g_playerRejects{ 0 };
		std::atomic<std::uint64_t> g_faults{ 0 };

		struct ActiveDraw
		{
			ID3D11DeviceContext* context{ nullptr };
			ID3D11RenderTargetView* colorRTV{ nullptr };
			ID3D11DepthStencilView* writableDepthDSV{ nullptr };
			ID3D11DepthStencilView* readOnlyDepthDSV{ nullptr };
			ID3D11ShaderResourceView* privateDepthSRV{ nullptr };
			ID3D11ShaderResourceView* retainedNativeT3{ nullptr };
			std::uintptr_t targetIdentity{ 0 };
			std::uint64_t dispatcherDepth{ 0 };
			DWORD threadID{ 0 };
			std::uint32_t width{ 0 };
			std::uint32_t height{ 0 };
			bool active{ false };
			bool privateDepthRequired{ false };
			bool nativeT3Captured{ false };
			bool mutationMayHaveOccurred{ false };
			bool privateBindingVerified{ false };
			// Placed-mirror draw: failures are counted and fall back; they never
			// latch the session (a black mirror is worse than one hard edge).
			bool placed{ false };
		};
		thread_local ActiveDraw g_active{};
		std::atomic<std::uint64_t> g_placedSoftDepthFallbacks{ 0 };

		struct ActivePlayerDepthClipDraw
		{
			ID3D11DeviceContext* context{ nullptr };
			ID3D11RasterizerState* savedState{ nullptr };
			ID3D11RasterizerState* appliedState{ nullptr };
			std::uint64_t dispatcherDepth{ 0 };
			DWORD threadID{ 0 };
			bool active{ false };
			bool commitObserved{ false };
			bool mutationMayHaveOccurred{ false };
		};
		static_assert(std::is_trivially_destructible_v<ActivePlayerDepthClipDraw>);
		thread_local ActivePlayerDepthClipDraw g_activePlayerDepthClip{};
		thread_local std::uint64_t g_setupAndDrawPassDepth{ 0 };

		constexpr std::size_t kPlayerDepthClipCacheSize = 16;
		struct PlayerDepthClipCacheEntry
		{
			D3D11_RASTERIZER_DESC description{};
			ComPtr<ID3D11RasterizerState> state;
		};
		struct PlayerDepthClipCache
		{
			ComPtr<ID3D11Device> device;
			std::array<PlayerDepthClipCacheEntry, kPlayerDepthClipCacheSize> entries{};
			std::size_t nextReplacement{ 0 };
		};
		stl::no_destructor<PlayerDepthClipCache> g_playerDepthClipCache{};

		void EnterFailStop() noexcept
		{
			if (!g_faulted.exchange(true, std::memory_order_acq_rel))
				g_faults.fetch_add(1, std::memory_order_relaxed);
			SecondView::FailStopExactHandSoftDepth();
		}

		[[nodiscard]] bool CopyPrologueSEH(
			const std::uintptr_t address,
			std::uint8_t* output,
			const std::size_t size) noexcept
		{
			if (!address || !output || size == 0)
				return false;
#if defined(_MSC_VER)
			__try {
				std::memcpy(output, reinterpret_cast<const void*>(address), size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#else
			std::memcpy(output, reinterpret_cast<const void*>(address), size);
			return true;
#endif
		}

		[[nodiscard]] bool HashFileExact(
			const wchar_t* path,
			const std::uint64_t expectedLength,
			const std::array<std::uint8_t, 32>& expectedDigest) noexcept
		{
			if (!path || !*path)
				return false;
			const HANDLE file = CreateFileW(
				path, GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			PUCHAR hashObject = nullptr;
			bool matched = false;
			do {
				LARGE_INTEGER length{};
				if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
					static_cast<std::uint64_t>(length.QuadPart) != expectedLength) {
					break;
				}
				if (BCryptOpenAlgorithmProvider(
						&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
					break;
				}
				DWORD copied = 0;
				DWORD objectBytes = 0;
				DWORD digestBytes = 0;
				if (BCryptGetProperty(
						algorithm, BCRYPT_OBJECT_LENGTH,
						reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
						&copied, 0) < 0 || copied != sizeof(objectBytes) ||
					objectBytes == 0 ||
					BCryptGetProperty(
						algorithm, BCRYPT_HASH_LENGTH,
						reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes),
						&copied, 0) < 0 || copied != sizeof(digestBytes) ||
					digestBytes != expectedDigest.size()) {
					break;
				}
				hashObject = static_cast<PUCHAR>(
					HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, objectBytes));
				if (!hashObject || BCryptCreateHash(
						algorithm, &hash, hashObject, objectBytes, nullptr, 0, 0) < 0) {
					break;
				}

				std::array<std::uint8_t, 16384> buffer{};
				std::uint64_t total = 0;
				bool complete = false;
				for (;;) {
					DWORD bytesRead = 0;
					if (!ReadFile(
							file, buffer.data(), static_cast<DWORD>(buffer.size()),
							&bytesRead, nullptr)) {
						break;
					}
					if (bytesRead == 0) {
						complete = total == expectedLength;
						break;
					}
					if (total > expectedLength - bytesRead ||
						BCryptHashData(hash, buffer.data(), bytesRead, 0) < 0) {
						break;
					}
					total += bytesRead;
				}
				if (!complete)
					break;
				std::array<std::uint8_t, 32> digest{};
				if (BCryptFinishHash(
						hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
					break;
				}
				matched = digest == expectedDigest;
			} while (false);

			if (hash)
				BCryptDestroyHash(hash);
			if (hashObject)
				HeapFree(GetProcessHeap(), 0, hashObject);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
			CloseHandle(file);
			return matched;
		}

		[[nodiscard]] bool VerifyLoadedModuleFileIdentity(
			const wchar_t* moduleName,
			const std::uint64_t expectedLength,
			const std::array<std::uint8_t, 32>& expectedDigest,
			HMODULE& module) noexcept
		{
			module = GetModuleHandleW(moduleName);
			if (!module)
				return false;
			std::array<wchar_t, 32768> path{};
			const DWORD length = GetModuleFileNameW(
				module, path.data(), static_cast<DWORD>(path.size()));
			return length != 0 && length < path.size() &&
				HashFileExact(path.data(), expectedLength, expectedDigest);
		}

		[[nodiscard]] bool IsPinnedSafetyHookRelayMemory(
			const std::uintptr_t relayAddress) noexcept
		{
			MEMORY_BASIC_INFORMATION memory{};
			if (!relayAddress || VirtualQuery(
					reinterpret_cast<const void*>(relayAddress), &memory,
					sizeof(memory)) != sizeof(memory)) {
				return false;
			}
			if (memory.State != MEM_COMMIT || memory.Type != MEM_PRIVATE ||
				memory.Protect != PAGE_EXECUTE_READWRITE) {
				return false;
			}
			const auto regionBegin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
			if (regionBegin > std::numeric_limits<std::uintptr_t>::max() -
					memory.RegionSize ||
				relayAddress < regionBegin) {
				return false;
			}
			const auto regionEnd = regionBegin + memory.RegionSize;
			return relayAddress <= regionEnd &&
				regionEnd - relayAddress >= kSafetyHookRelayBytes;
		}

		[[nodiscard]] bool IsPinnedEngineFixesChain(
			const RuntimeContract& contract,
			const std::uintptr_t entryAddress,
			const std::array<std::uint8_t, kEntryInspectionBytes>& entry) noexcept
		{
			// Engine Fixes SafetyHooks this dispatcher for
			// bBSLightingShaderForceAlphaTest. Accept a pre-existing E9 only after
			// proving every byte owner from Skyrim through the relay to the exact
			// runtime-specific Engine Fixes file/RVA. Engine Fixes owns this hook
			// with or without CS. The SE standalone opt-in removes only the unrelated
			// CS-presence requirement; all hook-owner evidence remains mandatory.
			std::uintptr_t relayAddress = 0;
			if (!HandMirrorSoftEffectHookChainPolicy::HasNativeTail(
					contract.runtime, entry) ||
				!HandMirrorSoftEffectHookChainPolicy::DecodeRelativeJump(
					entryAddress, entry, relayAddress) ||
				!IsPinnedSafetyHookRelayMemory(relayAddress)) {
				return false;
			}

			std::array<std::uint8_t, kSafetyHookRelayBytes> relay{};
			if (!CopyPrologueSEH(relayAddress, relay.data(), relay.size()))
				return false;
			std::uintptr_t ownerAddress = 0;
			if (!HandMirrorSoftEffectHookChainPolicy::DecodeSafetyHookAbsoluteRelay(
					relay, ownerAddress)) {
				return false;
			}

			HMODULE communityShaders = nullptr;
			HMODULE engineFixes = nullptr;
			if (contract.requiresPinnedCommunityShaders &&
				!HandMirrorSoftEffectHookChainPolicy::AcceptSEPeerConfiguration(
					HandMirrorSafetySettings::StandaloneSE(),
					GetModuleHandleW(L"CommunityShaders.dll") != nullptr,
					VerifyLoadedModuleFileIdentity(
						L"CommunityShaders.dll", kCommunityShaders184Length,
						kCommunityShaders184SHA256, communityShaders))) {
				return false;
			}
			if (!contract.engineFixesSHA256 ||
				!contract.engineFixesOwnerPrologue ||
				!VerifyLoadedModuleFileIdentity(
					L"EngineFixes.dll", contract.engineFixesLength,
					*contract.engineFixesSHA256, engineFixes)) {
				return false;
			}
			(void)communityShaders;
			const auto engineFixesBase = reinterpret_cast<std::uintptr_t>(engineFixes);
			if (engineFixesBase > std::numeric_limits<std::uintptr_t>::max() -
					contract.engineFixesOwnerRVA) {
				return false;
			}
			const auto expectedOwnerAddress =
				engineFixesBase + contract.engineFixesOwnerRVA;
			if (!HandMirrorSoftEffectHookChainPolicy::IsPinnedSafetyHookEntryShape(
					contract.runtime, entryAddress, entry, relay, relayAddress,
					expectedOwnerAddress) ||
				ownerAddress != expectedOwnerAddress) {
				return false;
			}

			std::array<std::uint8_t, 16> owner{};
			return CopyPrologueSEH(
					expectedOwnerAddress, owner.data(), owner.size()) &&
				owner == *contract.engineFixesOwnerPrologue;
		}

		[[nodiscard]] bool ReleaseGuarded(IUnknown* value) noexcept
		{
			if (!value)
				return true;
#if defined(_MSC_VER)
			__try {
				value->Release();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#else
			value->Release();
			return true;
#endif
		}

		[[nodiscard]] bool QueryRasterizerStateSEH(
			ID3D11DeviceContext* context,
			ID3D11RasterizerState*& output) noexcept
		{
			output = nullptr;
			if (!context)
				return false;
			bool queried = false;
#if defined(_MSC_VER)
			__try {
#endif
				context->RSGetState(&output);
				queried = true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				queried = false;
			}
#endif
			if (!queried) {
				auto* uncertain = output;
				output = nullptr;
				(void)ReleaseGuarded(uncertain);
			}
			return queried;
		}

		[[nodiscard]] bool QueryContextDeviceSEH(
			ID3D11DeviceContext* context,
			ID3D11Device*& output) noexcept
		{
			output = nullptr;
			if (!context)
				return false;
			bool queried = false;
#if defined(_MSC_VER)
			__try {
#endif
				context->GetDevice(&output);
				queried = output != nullptr;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				queried = false;
			}
#endif
			if (!queried) {
				auto* uncertain = output;
				output = nullptr;
				(void)ReleaseGuarded(uncertain);
			}
			return queried;
		}

		[[nodiscard]] bool ReadRasterizerDescriptionSEH(
			ID3D11RasterizerState* state,
			D3D11_RASTERIZER_DESC& output) noexcept
		{
			if (!state)
				return true;
			bool read = false;
#if defined(_MSC_VER)
			__try {
#endif
				state->GetDesc(&output);
				read = true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				read = false;
			}
#endif
			return read;
		}

		[[nodiscard]] D3D11_RASTERIZER_DESC DefaultRasterizerDescription() noexcept
		{
			D3D11_RASTERIZER_DESC description{};
			description.FillMode = D3D11_FILL_SOLID;
			description.CullMode = D3D11_CULL_BACK;
			description.FrontCounterClockwise = FALSE;
			description.DepthClipEnable = TRUE;
			return description;
		}

		[[nodiscard]] bool EqualRasterizerDescriptions(
			const D3D11_RASTERIZER_DESC& left,
			const D3D11_RASTERIZER_DESC& right) noexcept
		{
			return left.FillMode == right.FillMode &&
				left.CullMode == right.CullMode &&
				left.FrontCounterClockwise == right.FrontCounterClockwise &&
				left.DepthBias == right.DepthBias &&
				left.DepthBiasClamp == right.DepthBiasClamp &&
				left.SlopeScaledDepthBias == right.SlopeScaledDepthBias &&
				left.DepthClipEnable == right.DepthClipEnable &&
				left.ScissorEnable == right.ScissorEnable &&
				left.MultisampleEnable == right.MultisampleEnable &&
				left.AntialiasedLineEnable == right.AntialiasedLineEnable;
		}

		void ClearPlayerDepthClipCache() noexcept
		{
			auto& cache = g_playerDepthClipCache.get();
			for (auto& entry : cache.entries) {
				entry.state.Reset();
				entry.description = {};
			}
			cache.device.Reset();
			cache.nextReplacement = 0;
		}

		[[nodiscard]] bool AcquirePlayerDepthClipState(
			ID3D11Device* device,
			const D3D11_RASTERIZER_DESC& description,
			ID3D11RasterizerState*& output) noexcept
		{
			output = nullptr;
			if (!device || description.DepthClipEnable != FALSE)
				return false;
			auto& cache = g_playerDepthClipCache.get();
			if (cache.device.Get() != device) {
				ClearPlayerDepthClipCache();
				cache.device = device;
			}
			for (const auto& entry : cache.entries) {
				if (entry.state &&
					EqualRasterizerDescriptions(entry.description, description)) {
					output = entry.state.Get();
					return true;
				}
			}

			ComPtr<ID3D11RasterizerState> created;
			if (FAILED(device->CreateRasterizerState(
					&description, created.GetAddressOf())) || !created) {
				return false;
			}
			constexpr char debugName[] =
				"RealisticReflections.HandMirror.PlayerNoDepthClip";
			(void)created->SetPrivateData(
				kD3DDebugObjectName,
				static_cast<UINT>(sizeof(debugName) - 1), debugName);

			std::size_t slot = cache.entries.size();
			for (std::size_t index = 0; index < cache.entries.size(); ++index) {
				if (!cache.entries[index].state) {
					slot = index;
					break;
				}
			}
			if (slot == cache.entries.size()) {
				slot = cache.nextReplacement;
				cache.nextReplacement =
					(cache.nextReplacement + 1) % cache.entries.size();
			}
			cache.entries[slot].description = description;
			cache.entries[slot].state = created;
			output = cache.entries[slot].state.Get();
			return output != nullptr;
		}

		[[nodiscard]] bool AcquirePlayerDepthClipStateForContextSEH(
			ID3D11DeviceContext* context,
			const D3D11_RASTERIZER_DESC& description,
			ID3D11RasterizerState*& output) noexcept
		{
			output = nullptr;
			ID3D11Device* device = nullptr;
			bool acquired = false;
#if defined(_MSC_VER)
			__try {
#endif
				acquired = QueryContextDeviceSEH(context, device) &&
					AcquirePlayerDepthClipState(device, description, output);
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				acquired = false;
			}
#endif
			const bool released = ReleaseGuarded(device);
			if (!acquired || !released)
				output = nullptr;
			return acquired && released;
		}

		[[nodiscard]] bool CurrentRasterizerStateIs(
			ID3D11DeviceContext* context,
			ID3D11RasterizerState* expected,
			const bool requireDepthClipDisabled) noexcept
		{
			if (!context)
				return false;
			ID3D11RasterizerState* current = nullptr;
			if (!QueryRasterizerStateSEH(context, current))
				return false;
			bool matched = current == expected;
			if (matched && requireDepthClipDisabled) {
				auto description = DefaultRasterizerDescription();
				matched = ReadRasterizerDescriptionSEH(current, description) &&
					description.DepthClipEnable == FALSE;
			}
			return ReleaseGuarded(current) && matched;
		}

		[[nodiscard]] bool ReplaceSavedPlayerRasterizer(
			ID3D11RasterizerState* acquired) noexcept
		{
			auto* previous = g_activePlayerDepthClip.savedState;
			g_activePlayerDepthClip.savedState = acquired;
			return ReleaseGuarded(previous);
		}

		[[nodiscard]] bool BeginPlayerDepthClipDraw() noexcept
		{
			if (g_activePlayerDepthClip.active ||
				g_faulted.load(std::memory_order_acquire) ||
				g_setupAndDrawPassDepth == 0) {
				return false;
			}
			g_activePlayerDepthClip = {
				.dispatcherDepth = g_setupAndDrawPassDepth,
				.threadID = GetCurrentThreadId(),
				.active = true
			};
			g_playerArms.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] CommitResult CommitPlayerDepthClipRaw(
			ID3D11DeviceContext* context) noexcept
		{
			if (!g_activePlayerDepthClip.active)
				return CommitResult::kInactive;
			if (!context ||
				g_setupAndDrawPassDepth !=
					g_activePlayerDepthClip.dispatcherDepth ||
				GetCurrentThreadId() != g_activePlayerDepthClip.threadID ||
				(g_activePlayerDepthClip.context &&
				 g_activePlayerDepthClip.context != context)) {
				return CommitResult::kFailed;
			}

			ID3D11RasterizerState* current = nullptr;
			if (!QueryRasterizerStateSEH(context, current))
				return CommitResult::kFailed;
			if (g_activePlayerDepthClip.mutationMayHaveOccurred &&
				current == g_activePlayerDepthClip.appliedState) {
				const bool currentReleased = ReleaseGuarded(current);
				context->RSSetState(g_activePlayerDepthClip.appliedState);
				const bool verified = CurrentRasterizerStateIs(
					context, g_activePlayerDepthClip.appliedState, true);
				if (!currentReleased || !verified)
					return CommitResult::kFailed;
				g_activePlayerDepthClip.commitObserved = true;
				g_playerReapplies.fetch_add(1, std::memory_order_relaxed);
				return CommitResult::kApplied;
			}

			auto description = DefaultRasterizerDescription();
			if (!ReadRasterizerDescriptionSEH(current, description)) {
				(void)ReleaseGuarded(current);
				return CommitResult::kFailed;
			}
			const bool nativeDepthClipEnabled =
				description.DepthClipEnable != FALSE;
			// Native SetDirtyStates has superseded any prior override. Transfer the
			// exact new native state before deriving its replacement so a failed
			// derivation leaves B bound instead of restoring stale A.
			g_activePlayerDepthClip.mutationMayHaveOccurred = false;
			g_activePlayerDepthClip.appliedState = nullptr;
			if (!ReplaceSavedPlayerRasterizer(current))
				return CommitResult::kFailed;
			current = nullptr;
			g_activePlayerDepthClip.context = context;
			ID3D11RasterizerState* disabledState =
				g_activePlayerDepthClip.savedState;
			if (nativeDepthClipEnabled) {
				description.DepthClipEnable = FALSE;
				if (!AcquirePlayerDepthClipStateForContextSEH(
						context, description, disabledState)) {
					return CommitResult::kFailed;
				}
			}
			const bool reapply = g_activePlayerDepthClip.commitObserved;
			if (!nativeDepthClipEnabled) {
				g_activePlayerDepthClip.commitObserved = true;
				(reapply ? g_playerReapplies : g_playerCommits).fetch_add(
					1, std::memory_order_relaxed);
				return CommitResult::kApplied;
			}
			g_activePlayerDepthClip.appliedState = disabledState;
			g_activePlayerDepthClip.mutationMayHaveOccurred = true;
			context->RSSetState(disabledState);
			if (!CurrentRasterizerStateIs(context, disabledState, true))
				return CommitResult::kFailed;
			g_activePlayerDepthClip.commitObserved = true;
			(reapply ? g_playerReapplies : g_playerCommits).fetch_add(
				1, std::memory_order_relaxed);
			return CommitResult::kApplied;
		}

		[[nodiscard]] bool ClosePlayerDepthClipDrawRaw() noexcept
		{
			if (!g_activePlayerDepthClip.active)
				return true;
			const bool commitProven = g_activePlayerDepthClip.commitObserved;
			const bool sameOwner =
				GetCurrentThreadId() == g_activePlayerDepthClip.threadID &&
				g_setupAndDrawPassDepth ==
					g_activePlayerDepthClip.dispatcherDepth;
			bool restored = sameOwner;
			if (g_activePlayerDepthClip.mutationMayHaveOccurred &&
				g_activePlayerDepthClip.context && sameOwner) {
				ID3D11RasterizerState* current = nullptr;
				if (!QueryRasterizerStateSEH(
						g_activePlayerDepthClip.context, current)) {
					restored = false;
				} else if (current == g_activePlayerDepthClip.appliedState) {
					const bool currentReleased = ReleaseGuarded(current);
					g_activePlayerDepthClip.context->RSSetState(
						g_activePlayerDepthClip.savedState);
					restored = currentReleased && CurrentRasterizerStateIs(
						g_activePlayerDepthClip.context,
						g_activePlayerDepthClip.savedState, false);
				} else {
					// Native code has already superseded the plugin state. Preserve that
					// newer exact state; restoring savedState would roll it backwards.
					restored = ReleaseGuarded(current);
				}
			} else if (g_activePlayerDepthClip.mutationMayHaveOccurred) {
				restored = false;
			}

			auto* savedState = g_activePlayerDepthClip.savedState;
			g_activePlayerDepthClip = {};
			const bool released = ReleaseGuarded(savedState);
			if (commitProven && restored && released)
				g_playerRestores.fetch_add(1, std::memory_order_relaxed);
			return commitProven && restored && released;
		}

		[[nodiscard]] bool ClosePlayerDepthClipDraw() noexcept
		{
			bool restored = false;
#if defined(_MSC_VER)
			__try {
				restored = ClosePlayerDepthClipDrawRaw();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				auto* savedState = g_activePlayerDepthClip.savedState;
				g_activePlayerDepthClip = {};
				(void)ReleaseGuarded(savedState);
				restored = false;
			}
#else
			restored = ClosePlayerDepthClipDrawRaw();
#endif
			if (!restored) {
				g_playerRejects.fetch_add(1, std::memory_order_relaxed);
				EnterFailStop();
			}
			return restored;
		}

		template <class T>
		[[nodiscard]] bool ReplaceRetained(
			T*& destination, T* acquired) noexcept
		{
			auto* previous = destination;
			destination = acquired;
			return ReleaseGuarded(previous);
		}

		[[nodiscard]] bool SameBindings(
			const ActiveDraw& active,
			const SecondView::ExactHandSoftDepthBindings& current) noexcept
		{
			return active.context == current.context &&
				active.colorRTV == current.colorRTV &&
				active.writableDepthDSV == current.writableDepthDSV &&
				active.readOnlyDepthDSV == current.readOnlyDepthDSV &&
				active.privateDepthSRV == current.depthSRV &&
				active.targetIdentity == current.targetIdentity &&
				active.width == current.width && active.height == current.height;
		}

		[[nodiscard]] bool ViewsAlias(
			ID3D11ShaderResourceView* shaderView,
			ID3D11DepthStencilView* depthView,
			bool& inspected) noexcept
		{
			inspected = false;
			if (!shaderView || !depthView)
				return false;
			ID3D11Resource* shaderResource = nullptr;
			ID3D11Resource* depthResource = nullptr;
			bool alias = false;
#if defined(_MSC_VER)
			__try {
#endif
				shaderView->GetResource(&shaderResource);
				depthView->GetResource(&depthResource);
				inspected = shaderResource && depthResource;
				alias = inspected && shaderResource == depthResource;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				inspected = false;
				alias = false;
			}
#endif
			const bool shaderReleased = ReleaseGuarded(shaderResource);
			const bool depthReleased = ReleaseGuarded(depthResource);
			inspected = inspected && shaderReleased && depthReleased;
			return alias;
		}

		[[nodiscard]] bool VerifyShaderResource(
			ID3D11DeviceContext* context,
			ID3D11ShaderResourceView* expected) noexcept
		{
			if (!context)
				return false;
			ID3D11ShaderResourceView* actual = nullptr;
			bool queried = false;
#if defined(_MSC_VER)
			__try {
#endif
				context->PSGetShaderResources(
					HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1, &actual);
				queried = true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				queried = false;
			}
#endif
			const bool matched = queried && actual == expected;
			return ReleaseGuarded(actual) && matched;
		}

		[[nodiscard]] bool RefreshRetainedNativeDepthBinding(
			ID3D11DeviceContext* context) noexcept
		{
			if (!context || context != g_active.context ||
				!g_active.nativeT3Captured) {
				return false;
			}
			ID3D11ShaderResourceView* current = nullptr;
			context->PSGetShaderResources(
				HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1, &current);
			if (current == g_active.privateDepthSRV)
				return ReleaseGuarded(current);
			if (current) {
				bool inspected = false;
				const bool aliasesPrivateDepth = ViewsAlias(
					current, g_active.writableDepthDSV, inspected);
				if (!inspected || aliasesPrivateDepth) {
					(void)ReleaseGuarded(current);
					return false;
				}
			}
			return ReplaceRetained(g_active.retainedNativeT3, current);
		}

		[[nodiscard]] bool VerifyOutputMerger(
			ID3D11DeviceContext* context,
			ID3D11RenderTargetView* expectedColor,
			ID3D11DepthStencilView* expectedDepth) noexcept
		{
			if (!context || !expectedColor || !expectedDepth)
				return false;
			ID3D11RenderTargetView* actualColor = nullptr;
			ID3D11DepthStencilView* actualDepth = nullptr;
			bool queried = false;
#if defined(_MSC_VER)
			__try {
#endif
				context->OMGetRenderTargets(1, &actualColor, &actualDepth);
				queried = true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				queried = false;
			}
#endif
			const bool matched = queried && actualColor == expectedColor &&
				actualDepth == expectedDepth;
			const bool colorReleased = ReleaseGuarded(actualColor);
			const bool depthReleased = ReleaseGuarded(actualDepth);
			return colorReleased && depthReleased && matched;
		}

		void SetPrivateViewport(const ActiveDraw& active) noexcept
		{
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(active.width);
			viewport.Height = static_cast<float>(active.height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			active.context->RSSetViewports(1, &viewport);
		}

		[[nodiscard]] bool BeginEffectDrawRaw(
			const SecondView::ExactHandEffectDraw& described) noexcept
		{
			if (g_active.active || g_faulted.load(std::memory_order_acquire) ||
				g_setupAndDrawPassDepth == 0)
				return false;
			if (!described.pass || !described.pass->geometry ||
				described.technique < HandMirrorSoftEffectPolicy::kEffectTechniqueBase ||
				!described.privateDepthRequired) {
				return false;
			}
			SecondView::ExactHandSoftDepthBindings bindings{};
			if (!SecondView::TryGetExactHandSoftDepthBindings(bindings))
				return false;
			g_active = {
				.context = bindings.context,
				.colorRTV = bindings.colorRTV,
				.writableDepthDSV = bindings.writableDepthDSV,
				.readOnlyDepthDSV = bindings.readOnlyDepthDSV,
				.privateDepthSRV = bindings.depthSRV,
				.targetIdentity = bindings.targetIdentity,
				.dispatcherDepth = g_setupAndDrawPassDepth,
				.threadID = GetCurrentThreadId(),
				.width = bindings.width,
				.height = bindings.height,
				.active = true,
				.privateDepthRequired = described.privateDepthRequired,
				.placed = described.placed
			};
			g_arms.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] bool BeginEffectDraw(
			const SecondView::ExactHandEffectDraw& described) noexcept
		{
			bool began = false;
#if defined(_MSC_VER)
			__try {
				began = BeginEffectDrawRaw(described);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				began = false;
			}
#else
			began = BeginEffectDrawRaw(described);
#endif
			return began;
		}

		[[nodiscard]] CommitResult CommitPrivateDepthRaw(
			ID3D11DeviceContext* context) noexcept
		{
			if (!g_active.active || !g_active.privateDepthRequired)
				return CommitResult::kInactive;
			SecondView::ExactHandSoftDepthBindings current{};
			if (!context || context != g_active.context ||
				g_setupAndDrawPassDepth != g_active.dispatcherDepth ||
				GetCurrentThreadId() != g_active.threadID ||
				!SecondView::TryGetExactHandSoftDepthBindings(current) ||
				!SameBindings(g_active, current)) {
				return CommitResult::kFailed;
			}

			if (!g_active.nativeT3Captured) {
				context->PSGetShaderResources(
					HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1,
					&g_active.retainedNativeT3);
				g_active.nativeT3Captured = true;
				if (g_active.retainedNativeT3) {
					bool inspected = false;
					const bool aliasesPrivateDepth = ViewsAlias(
						g_active.retainedNativeT3, g_active.writableDepthDSV,
						inspected);
					if (!inspected || aliasesPrivateDepth)
						return CommitResult::kFailed;
				}
			}

			const bool reapply = g_active.mutationMayHaveOccurred;
			// Ownership begins before the first D3D mutation. Cleanup therefore
			// restores writable depth even if a driver call faults midway through.
			g_active.mutationMayHaveOccurred = true;
			ID3D11RenderTargetView* color = g_active.colorRTV;
			context->OMSetRenderTargets(1, &color, g_active.readOnlyDepthDSV);
			SetPrivateViewport(g_active);
			ID3D11ShaderResourceView* privateDepth = g_active.privateDepthSRV;
			context->PSSetShaderResources(
				HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1, &privateDepth);
			g_active.privateBindingVerified =
				VerifyOutputMerger(
					context, g_active.colorRTV, g_active.readOnlyDepthDSV) &&
				VerifyShaderResource(context, g_active.privateDepthSRV);
			if (!g_active.privateBindingVerified)
				return CommitResult::kFailed;
			(reapply ? g_reapplies : g_commits).fetch_add(
				1, std::memory_order_relaxed);
			return CommitResult::kApplied;
		}

		[[nodiscard]] CommitResult CommitEffectDrawRaw(
			ID3D11DeviceContext* context) noexcept
		{
			if (!g_active.active)
				return CommitResult::kInactive;
			return CommitPrivateDepthRaw(context);
		}

		[[nodiscard]] bool CloseEffectDrawRaw() noexcept
		{
			if (!g_active.active)
				return true;
			// An armed SoftEffect draw is successful only if SetDirtyStates reached the
			// exact private-depth commit and its D3D readback proved the binding. Native
			// return without this receipt rendered with an unowned depth path.
			const bool commitProven =
				HandMirrorSoftEffectPolicy::VerifiedPrivateDepthCommit(
					g_active.nativeT3Captured, g_active.mutationMayHaveOccurred,
					g_active.privateBindingVerified);
			bool restored = commitProven;
			SecondView::ExactHandSoftDepthBindings current{};
			if (!SecondView::TryGetExactHandSoftDepthBindings(current) ||
				!SameBindings(g_active, current) ||
				g_setupAndDrawPassDepth != g_active.dispatcherDepth ||
				GetCurrentThreadId() != g_active.threadID) {
				restored = false;
			}

			if (g_active.mutationMayHaveOccurred && g_active.context) {
				// Remove the private depth SRV before returning the same resource to a
				// writable DSV. Restoring in the opposite order triggers D3D11's alias
				// hazard and silently nulls one side of the transaction.
				ID3D11ShaderResourceView* nullDepth = nullptr;
				g_active.context->PSSetShaderResources(
					HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1, &nullDepth);
				ID3D11RenderTargetView* color = g_active.colorRTV;
				g_active.context->OMSetRenderTargets(
					1, &color, g_active.writableDepthDSV);
				SetPrivateViewport(g_active);
				ID3D11ShaderResourceView* nativeDepth =
					g_active.retainedNativeT3;
				g_active.context->PSSetShaderResources(
					HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1, &nativeDepth);
				restored = VerifyShaderResource(
					g_active.context, g_active.retainedNativeT3) &&
					VerifyOutputMerger(
						g_active.context, g_active.colorRTV,
						g_active.writableDepthDSV) && restored;
				if (restored)
					g_restores.fetch_add(1, std::memory_order_relaxed);
			}

			ID3D11ShaderResourceView* retainedNativeT3 =
				g_active.retainedNativeT3;
			g_active = {};
			const bool t3Released = ReleaseGuarded(retainedNativeT3);
			return restored && t3Released;
		}

		[[nodiscard]] bool CloseEffectDraw() noexcept
		{
			const bool placed = g_active.placed;
			bool restored = false;
#if defined(_MSC_VER)
			__try {
				restored = CloseEffectDrawRaw();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				// Tombstone logical ownership before any later callback can mistake
				// this failed transaction for an active draw.
				ID3D11ShaderResourceView* retainedNativeT3 =
					g_active.retainedNativeT3;
				g_active = {};
				(void)ReleaseGuarded(retainedNativeT3);
				restored = false;
			}
#else
			restored = CloseEffectDrawRaw();
#endif
			if (!restored) {
				// The raw close restores writable depth and native t3 whenever it
				// may have mutated them; a placed draw that could not prove its
				// commit was one draw without the fade, not a broken renderer.
				if (placed)
					g_placedSoftDepthFallbacks.fetch_add(1, std::memory_order_relaxed);
				else
					EnterFailStop();
			}
			return restored;
		}

		// Vanilla water's screen-space reflection, switched off inside private
		// captures. Owner, 2026-09-15: "we just turn SSR off for the mirror
		// reflection."
		//
		// Correct SSR is not available to a mirror: it reflects the screen, and the
		// only screen-space colour the engine holds while we capture is the main
		// view's. Data/Shaders/Water.hlsl (the vanilla source CS 1.8.4 ships) puts
		// the switch in reach --
		//
		//   Texture2D<float4> SSRReflectionTex    : register(t10);
		//   Texture2D<float4> RawSSRReflectionTex : register(t11);
		//   float ssrFraction = saturate(ssrReflectionColor.w * distanceFactor * ssrAmount);
		//   reflectionColor = lerp(reflectionColor, finalSsrReflectionColor, ssrFraction);
		//
		// -- the blend weight is the SSR textures' own alpha. A zero-alpha texture
		// makes ssrFraction exactly 0 and leaves reflectionColor as the cubemap and
		// planar terms: ordinary water, no constant edited and no weight guessed.
		//
		// The same lines explain the banding. That sample's uv comes from
		// DynamicResolutionParams2 * HPosition, the *main* framebuffer's scale, so a
		// 4096 capture runs past uv 1 and the sampler clamps to one edge texel --
		// a repeated run that slides with the camera.
		constexpr UINT kWaterSSRFirstSlot = 10;
		constexpr UINT kWaterSSRSlotCount = 2;
		// ReflectionTex : register(t0) -- the planar water reflection, rendered
		// for the MAIN camera and sampled in screen space.
		//
		// Run 5 proved the SSR neutral alone is not enough: it bound 1,466,702
		// times with zero failures and the owner still saw horizontal streaks
		// across the water (ScreenShot291). Streaks are the signature of a uv
		// past 1 clamping to one repeated row, which is addressing, not the SSR
		// blend weight -- so the offender is a different screen-space sample, and
		// t0 is the one that carries a whole reflected image.
		//
		// Neutralised the water keeps its cubemap, base colour and depth fade:
		// plainer than ENB or vanilla at their best, but stable and showing this
		// scene rather than a smeared copy of the main view.
		constexpr UINT kWaterPlanarReflectionSlot = 0;
		// Snow/terrain draws seen inside a private capture, and how many the
		// alpha-test correction now deliberately leaves alone (2026-09-15).
		std::atomic<std::uint64_t> g_captureSnowDraws{ 0 };
		std::atomic<std::uint64_t> g_captureTerrainDraws{ 0 };
		std::atomic<std::uint64_t> g_captureSnowExcluded{ 0 };
		std::atomic<std::uint64_t> g_waterSSRNeutralBinds{ 0 };
		std::atomic<std::uint64_t> g_waterSSRNeutralFailures{ 0 };
		std::atomic<std::uint64_t> g_waterPrivateTechniqueRewrites{ 0 };

		[[nodiscard]] ID3D11ShaderResourceView* WaterSSRNeutral(
			ID3D11DeviceContext* context) noexcept
		{
			static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> neutral;
			static ID3D11Device* owner = nullptr;
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(device.GetAddressOf());
			if (!device)
				return nullptr;
			if (neutral && owner == device.Get())
				return neutral.Get();
			neutral.Reset();
			owner = nullptr;
			constexpr std::uint8_t zero[4]{ 0, 0, 0, 0 };  // alpha 0 == no SSR
			D3D11_TEXTURE2D_DESC texture{};
			texture.Width = texture.Height = texture.MipLevels = texture.ArraySize = 1;
			texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			texture.SampleDesc.Count = 1;
			texture.Usage = D3D11_USAGE_IMMUTABLE;
			texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const D3D11_SUBRESOURCE_DATA pixels{ zero, sizeof(zero), 0 };
			Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
			if (FAILED(device->CreateTexture2D(&texture, &pixels, image.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(
					image.Get(), nullptr, neutral.GetAddressOf()))) {
				neutral.Reset();
				return nullptr;
			}
			owner = device.Get();
			return neutral.Get();
		}

		struct WaterSSRNeutralLease
		{
			ID3D11DeviceContext* context{ nullptr };
			ID3D11ShaderResourceView* saved[kWaterSSRSlotCount]{};
			ID3D11ShaderResourceView* savedPlanar{ nullptr };
			bool active{ false };

			[[nodiscard]] bool Begin(const RE::BSRenderPass* pass) noexcept
			{
				// A lease left open by a RestoreGeometry that never arrived would
				// otherwise hold a neutral texture into the next draw.
				if (active)
					End();
				if (!pass || !pass->shader ||
					pass->shader->shaderType.underlying() != RE::BSShader::Type::Water ||
					!SecondView::IsInsidePrivateCapture()) {
					return false;
				}
				auto* const current = SecondView::ActiveCaptureContext();
				if (!current)
					return false;
				__try {
					auto* const neutral = WaterSSRNeutral(current);
					if (!neutral) {
						g_waterSSRNeutralFailures.fetch_add(1, std::memory_order_relaxed);
						return false;
					}
					current->PSGetShaderResources(
						kWaterSSRFirstSlot, kWaterSSRSlotCount, saved);
					ID3D11ShaderResourceView* const neutrals[kWaterSSRSlotCount]{
						neutral, neutral };
					current->PSSetShaderResources(
						kWaterSSRFirstSlot, kWaterSSRSlotCount, neutrals);
					current->PSGetShaderResources(
						kWaterPlanarReflectionSlot, 1, &savedPlanar);
					ID3D11ShaderResourceView* const planarNeutral[1]{ neutral };
					current->PSSetShaderResources(
						kWaterPlanarReflectionSlot, 1, planarNeutral);
					context = current;
					active = true;
					g_waterSSRNeutralBinds.fetch_add(1, std::memory_order_relaxed);
					return true;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					g_waterSSRNeutralFailures.fetch_add(1, std::memory_order_relaxed);
					context = nullptr;
					active = false;
					return false;
				}
			}

			void End() noexcept
			{
				if (!active || !context)
					return;
				__try {
					context->PSSetShaderResources(
						kWaterSSRFirstSlot, kWaterSSRSlotCount, saved);
					context->PSSetShaderResources(
						kWaterPlanarReflectionSlot, 1, &savedPlanar);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					g_waterSSRNeutralFailures.fetch_add(1, std::memory_order_relaxed);
				}
				for (auto*& view : saved) {
					if (view)
						(void)ReleaseGuarded(view);
					view = nullptr;
				}
				if (savedPlanar)
					(void)ReleaseGuarded(savedPlanar);
				savedPlanar = nullptr;
				context = nullptr;
				active = false;
			}
		};

		// Which material families a capture is actually being handed. The feature
		// index is the lighting technique's descriptor byte, the same arithmetic
		// MirrorDrawAlphaPolicy::Select uses, so the two always agree about what a
		// draw is.
		void NoteCaptureMaterialFamily(std::uint32_t technique, bool alphaTest) noexcept
		{
			if (technique < MirrorDrawAlphaPolicy::kLightingBase ||
				technique - MirrorDrawAlphaPolicy::kLightingBase >= 0x14000000u)
				return;
			const auto feature = (technique - MirrorDrawAlphaPolicy::kLightingBase) >> 24;
			switch (feature) {
			case 10: case 14:  // snow coverage
				g_captureSnowDraws.fetch_add(1, std::memory_order_relaxed);
				if (alphaTest)
					g_captureSnowExcluded.fetch_add(1, std::memory_order_relaxed);
				break;
			case 8: case 9: case 13: case 15: case 18: case 19:  // land and object LOD
				g_captureTerrainDraws.fetch_add(1, std::memory_order_relaxed);
				break;
			default:
				break;
			}
		}

		bool PrepareAlphaDraw(const RE::BSRenderPass* pass, std::uint32_t technique,
			bool alphaTest, RE::BSRenderPass& copy) noexcept
		{
			static_assert(MirrorDrawAlphaPolicy::kLightingBase == RE::BSLightingShader::kTechniqueIDBase);
			static_assert(MirrorDrawAlphaPolicy::kAlphaTest ==
				static_cast<std::uint32_t>(RE::BSLightingShader::TechniqueFlag::kDoAlphaTest));
			if (!g_alphaDrawEnabled.load(std::memory_order_relaxed) || !alphaTest ||
				!SecondView::IsInsideMirrorPrimaryCapture()) return false;
			__try {
				if (!pass || !pass->shader) return false;
				const bool lighting = pass->shader->shaderType.underlying() == RE::BSShader::Type::Lighting;
				if (!MirrorDrawAlphaPolicy::Prepare(*pass, technique, true, lighting, alphaTest, copy))
					return false;
				g_alphaDrawCorrections.fetch_add(1, std::memory_order_relaxed);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_alphaDrawReadFaults.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}

		// The lease has to outlive SetupGeometry, so it cannot be a local.
		//
		// Owner, 2026-09-16: "so the water still shows wrong SSR reflections",
		// with waterSSRNeutral(binds/failures)=407401/0 in the same log. Both were
		// true. The lease was taken and released inside the SetupGeometry thunk,
		// and SetupGeometry is where the shader binds its *own* textures and the
		// draw is issued afterwards, between slots 6 and 7. So every one of those
		// 407,401 binds was overwritten by the call it wrapped and then restored
		// before anything was drawn with it: the counter measured the bind, not
		// the effect, and the water sampled the main view exactly as before.
		//
		// It now brackets the draw the way the engine brackets it: apply after
		// SetupGeometry has bound the real textures, release in RestoreGeometry.
		thread_local WaterSSRNeutralLease g_waterLease{};

		// BSWaterShader::SetupGeometry. Slot 6 is SetupGeometry and slot 7 is
		// RestoreGeometry on every BSShader, which is the slot the sky recenter
		// already leases on BSSkyShader; VTABLE_BSWaterShader carries the
		// per-runtime address, so no offset of our own is pinned here.
		struct WaterSetupHook
		{
			static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass,
				std::uint32_t flags)
			{
				func(shader, pass, flags);
				(void)g_waterLease.Begin(pass);
			}
			static inline REL::Relocation<decltype(thunk)> func{};
		};

		// BSWaterShader::RestoreGeometry -- the engine's own end of the draw.
		struct WaterRestoreHook
		{
			static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass,
				std::uint32_t flags)
			{
				g_waterLease.End();
				func(shader, pass, flags);
			}
			static inline REL::Relocation<decltype(thunk)> func{};
		};

		std::atomic_bool g_waterHookInstalled{ false };

		void EnsureWaterSSRHook() noexcept
		{
			if (g_waterHookInstalled.load(std::memory_order_acquire))
				return;
			try {
				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BSWaterShader[0] };
				WaterSetupHook::func = vtable.write_vfunc(6, WaterSetupHook::thunk);
				WaterRestoreHook::func = vtable.write_vfunc(7, WaterRestoreHook::thunk);
				g_waterHookInstalled.store(true, std::memory_order_release);
				logger::info(
					"[RR][HandMirror][water-ssr] BSWaterShader Setup/RestoreGeometry lease "
					"installed; the screen-space water samples are neutral for the whole "
					"draw inside private captures (t0 planar, t10/t11 alpha 0)");
			} catch (...) {
				logger::warn(
					"[RR][HandMirror][water-ssr] could not install the BSWaterShader lease; "
					"water keeps its main-view screen-space reflection inside captures");
			}
		}

		// F8 breakdown: native draw time inside the outermost dispatch of a
		// profiled standing capture; the rest of the dispatch is this mod's work.
		thread_local bool g_profileDispatch = false;
		thread_local std::uint64_t g_profileNativeTicks = 0;

		struct SetupAndDrawPassHook
		{
			static void TimedNative(
				RE::BSRenderPass* pass,
				std::uint32_t technique,
				std::uint8_t geometryMode,
				std::uint32_t renderFlags)
			{
				if (!g_profileDispatch) {
					func(pass, technique, geometryMode, renderFlags);
					return;
				}
				const auto start = MirrorCaptureProfile::Ticks();
#if defined(_MSC_VER)
				__try {
					func(pass, technique, geometryMode, renderFlags);
				} __finally {
					g_profileNativeTicks += MirrorCaptureProfile::Ticks() - start;
				}
#else
				func(pass, technique, geometryMode, renderFlags);
				g_profileNativeTicks += MirrorCaptureProfile::Ticks() - start;
#endif
			}

			static void CallNativeWithTransactionsFinally(
				RE::BSRenderPass* pass,
				std::uint32_t technique,
				std::uint8_t geometryMode,
				std::uint32_t renderFlags,
				const bool effectActive,
				const bool playerDepthClipActive)
			{
#if defined(_MSC_VER)
				__try {
					TimedNative(pass, technique, geometryMode, renderFlags);
				} __finally {
					if (effectActive)
						(void)CloseEffectDraw();
					if (playerDepthClipActive)
						(void)ClosePlayerDepthClipDraw();
				}
#else
				TimedNative(pass, technique, geometryMode, renderFlags);
				if (effectActive)
					(void)CloseEffectDraw();
				if (playerDepthClipActive)
					(void)ClosePlayerDepthClipDraw();
#endif
			}

			static void DispatchBody(
				RE::BSRenderPass* pass,
				std::uint32_t technique,
				std::uint8_t geometryMode,
				std::uint32_t renderFlags)
			{
				// Owner (run 6, 2026-09-14): fire/particles missing in placed mirrors.
				// Count Effect/Particle passes reaching this dispatcher inside a
				// placed-mirror capture and how many the skip chain removes.
				bool placedEffectOrParticle = false;
				if (pass && pass->shader && SecondView::IsInsideMirrorPrimaryCapture()) {
					const auto shaderType = pass->shader->shaderType.underlying();
					if (shaderType == RE::BSShader::Type::Effect) {
						placedEffectOrParticle = true;
						g_placedEffectDrawsSeen.fetch_add(1, std::memory_order_relaxed);
						NotePlacedEffectName(pass, false);
					} else if (shaderType == RE::BSShader::Type::Particle) {
						placedEffectOrParticle = true;
						g_placedParticleDrawsSeen.fetch_add(1, std::memory_order_relaxed);
						NotePlacedEffectName(pass, true);
					}
				}
				// Core run 1 (2026-09-16, SE + CS + DLSS/FG, main-menu coc into the
				// Sleeping Giant Inn): the first depth pre-pass handed a pass without a
				// shader to the native draw. BSBatchRenderer::RenderPassImmediately --
				// and Engine Fixes' alpha-test wrapper around it (EngineFixes.dll+0x2CFA1)
				// -- read pass->shader first, so the access violation latched the whole
				// session and every mirror went black. Nothing downstream can draw such
				// a pass: skip it inside private captures and count it, so the periodic
				// line says how often it happens instead of the session ending.
				if (SecondView::IsInsidePrivateCapture() && (!pass || !pass->shader)) {
					g_nullShaderPassSkips.fetch_add(1, std::memory_order_relaxed);
					if (!g_nullShaderPassLogged.exchange(true, std::memory_order_acq_rel))
						logger::warn("[MOS][PlacedEffects] pass without shader skipped inside a private capture (pass={} technique=0x{:X} geometryMode={} primary={} auxiliary={}); counted as nullShaderSkips",
							pass != nullptr, technique, geometryMode,
							SecondView::IsInsideMirrorPrimaryCapture(), SecondView::IsInsideMirrorAuxiliaryCapture());
					return;
				}
				if (SecondView::ShouldSkipDeviceLostDraw()) {
					if (placedEffectOrParticle)
						g_placedEffectParticleSkipped.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				if (placedEffectOrParticle && !SecondView::IsInsideExactHandPhysicalRasterClip() &&
					SecondView::IsFirstPersonPlayerGeometry(pass)) {
					g_placedFirstPersonEffectSkips.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				// V177: native shader setup dereferences each point light's NiLight
				// without a null check. A queued pass can outlive that light. Reject
				// before setup or any local lease, leaving normal batch retirement
					// to the caller. A separate exact-AE marker also protects the main
					// world from the same retired-light dereference after capture stops.
				if (SecondView::ShouldSkipExactHandRetiredLightDraw(pass, technique) ||
					SecondView::ShouldSkipMirrorDistanceDraw(pass) ||
					SecondView::ShouldSkipPaneCrossingEffectDraw(pass) ||
					SecondView::ShouldSkipSceneryShadowDraw(pass) ||
					SecondView::ShouldSkipDuplicateReplayDraw(pass, technique, geometryMode)) {
					if (placedEffectOrParticle)
						g_placedEffectParticleSkipped.fetch_add(1, std::memory_order_relaxed);
					return;
				}

				// Cached main-view passes can bypass CalculateActiveLightsForSurface.
				// Correct the actual synchronous draw without mutating/freeing the
				// queued native pass or leaving stack pointers in its cache.
				const auto* coveragePass = pass;
				const auto coverageTechnique = technique;
				MirrorSurfaceLights::Draw surfaceDraw{};
				if (MirrorSurfaceLights::PrepareDraw(pass, technique, surfaceDraw)) {
					pass = &surfaceDraw.pass;
					technique = surfaceDraw.pass.passEnum;
				}
				// Native argument 3 is the alpha-test boolean. Keep both synchronous
				// draw arguments consistent without changing the engine's cached pass.
				if (SecondView::IsInsidePrivateCapture())
					NoteCaptureMaterialFamily(technique, geometryMode != 0);
				RE::BSRenderPass alphaPass;
				if (PrepareAlphaDraw(pass, technique, geometryMode != 0, alphaPass)) {
					pass = &alphaPass;
					technique = alphaPass.passEnum;
				}
				RE::BSRenderPass waterPass;
				const auto waterTechnique = MirrorWaterCapturePolicy::Select(
					SecondView::IsInsideMirrorPrimaryCapture() || SecondView::IsInsideMirrorAuxiliaryCapture(),
					pass && pass->shader && pass->shader->shaderType.underlying() == RE::BSShader::Type::Water,
					technique);
				if (waterTechnique != technique) {
					waterPass = *pass;
					waterPass.passEnum = waterTechnique;
					pass = &waterPass;
					technique = waterTechnique;
					g_waterPrivateTechniqueRewrites.fetch_add(1, std::memory_order_relaxed);
				}
				bool playerDepthClipActive = false;
				if (SecondView::IsInsideExactHandPhysicalRasterClip()) {
					const auto classification =
						PlayerDrawPassProbe::ClassifyExactHandPlayerDraw(pass);
					if (classification ==
						PlayerDrawPassProbe::ExactHandDrawClassification::kPlayer) {
						g_playerMatches.fetch_add(1, std::memory_order_relaxed);
						playerDepthClipActive = BeginPlayerDepthClipDraw();
						if (!playerDepthClipActive) {
							g_playerRejects.fetch_add(1, std::memory_order_relaxed);
							EnterFailStop();
						}
					} else if (classification ==
						PlayerDrawPassProbe::ExactHandDrawClassification::kFault) {
						g_playerRejects.fetch_add(1, std::memory_order_relaxed);
						EnterFailStop();
					}
				}

				SecondView::ExactHandEffectDraw described{};
				const bool effectDescribed =
					SecondView::TryDescribeExactHandEffectDraw(
						pass, technique, described);
				const bool effectRequired = effectDescribed && described.privateDepthRequired;
				// Keep the synchronous pass descriptor consistent with the selected
				// technique. Shader/peer geometry setup can also inspect passEnum.
				// The native cached pass remains untouched.
				RE::BSRenderPass effectPass;
				if (effectDescribed && !described.privateDepthRequired &&
					described.technique != 0 && described.technique != technique) {
					effectPass = *pass;
					effectPass.passEnum = described.technique;
					pass = &effectPass;
					technique = described.technique;
				}
				bool effectActive = false;
				bool effectFellBack = false;
				if (effectRequired) {
					effectActive = BeginEffectDraw(described);
					if (effectActive) {
						if (placedEffectOrParticle)
							g_placedEffectPrivateDepth.fetch_add(1, std::memory_order_relaxed);
					} else if (described.placed && described.fallbackTechnique != 0) {
						// Placed capture: draw this one without the SoftEffect fade.
						effectPass = *pass;
						effectPass.passEnum = described.fallbackTechnique;
						pass = &effectPass;
						technique = described.fallbackTechnique;
						effectFellBack = true;
						g_placedSoftDepthFallbacks.fetch_add(1, std::memory_order_relaxed);
					} else {
						EnterFailStop();
					}
				}

				if (!effectActive && !playerDepthClipActive) {
					TimedNative(pass, technique, geometryMode, renderFlags);
					SecondView::NoteMirrorCoverageDrawReturned(coveragePass, coverageTechnique, geometryMode);
					return;
				}
				CallNativeWithTransactionsFinally(
					pass, effectRequired && !effectFellBack ? described.technique : technique,
					geometryMode, renderFlags, effectActive,
					playerDepthClipActive);
				SecondView::NoteMirrorCoverageDrawReturned(coveragePass, coverageTechnique, geometryMode);
			}

			static void FinishProfiledDispatch(std::uint64_t a_start) noexcept
			{
				g_profileDispatch = false;
				const auto total = MirrorCaptureProfile::Ticks() - a_start;
				MirrorCaptureProfile::AddTicks(MirrorCaptureProfile::Part::kHooks,
					total > g_profileNativeTicks ? total - g_profileNativeTicks : 0);
			}

			static __declspec(noinline) void DispatchAtOwnedDepth(
				RE::BSRenderPass* pass,
				std::uint32_t technique,
				std::uint8_t geometryMode,
				std::uint32_t renderFlags)
			{
				if (g_setupAndDrawPassDepth == UINT64_MAX) {
					EnterFailStop();
					func(pass, technique, geometryMode, renderFlags);
					return;
				}
				++g_setupAndDrawPassDepth;
				const bool profile = g_setupAndDrawPassDepth == 1 && MirrorCaptureProfile::Active() &&
					MirrorCaptureProfile::InCapture() &&
					(SecondView::IsInsideMirrorPrimaryCapture() || SecondView::IsInsideMirrorAuxiliaryCapture());
				const auto profileStart = profile ? MirrorCaptureProfile::Ticks() : 0;
				if (profile) {
					g_profileDispatch = true;
					g_profileNativeTicks = 0;
				}
#if defined(_MSC_VER)
				__try {
					DispatchBody(pass, technique, geometryMode, renderFlags);
				} __finally {
					--g_setupAndDrawPassDepth;
					// A light lent for the reflected body goes back before any
					// other draw -- or the main view -- can read it.
					if (g_setupAndDrawPassDepth == 0)
						MirrorSurfaceLights::EndDrawLeases();
					if (profile)
						FinishProfiledDispatch(profileStart);
				}
#else
				DispatchBody(pass, technique, geometryMode, renderFlags);
				--g_setupAndDrawPassDepth;
				if (g_setupAndDrawPassDepth == 0)
					MirrorSurfaceLights::EndDrawLeases();
				if (profile)
					FinishProfiledDispatch(profileStart);
#endif
			}

			static void thunk(
				RE::BSRenderPass* pass,
				std::uint32_t technique,
				std::uint8_t geometryMode,
				std::uint32_t renderFlags)
			{
				DispatchAtOwnedDepth(pass, technique, geometryMode, renderFlags);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void EndWaterSSRNeutralLease() noexcept
	{
		g_waterLease.End();
	}

	bool EnsureInstalled() noexcept
	{
		// Water's SSR neutral rides its own shader vtable, not the batch
		// dispatcher: run 4 measured zero binds through the dispatcher.
		EnsureWaterSSRHook();
		if (g_installed.load(std::memory_order_acquire))
			return true;
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return g_installed.load(std::memory_order_acquire);

		const auto* contract = ExactRuntimeContract();
		if (!contract) {
			logger::info(
				"[RR][HandMirror][soft-effect-depth] exact batch argument flow is unsupported on this runtime; hand runtime remains fail-closed");
			return false;
		}
		logger::info("[MOS][HandSafety] standaloneSE={} headClearance={} (test opt-ins; VR excluded)",
			HandMirrorSafetySettings::StandaloneSE() && REL::Module::IsSE(),
			HandMirrorSafetySettings::HeadClearance() && !REL::Module::IsVR());

		REL::Relocation<std::uintptr_t> target{ kSetupAndDrawPass };
		std::array<std::uint8_t, kEntryInspectionBytes> actual{};
		if (target.address() !=
				REL::Module::get().base() + contract->dispatcherRVA ||
			!CopyPrologueSEH(
				target.address(), actual.data(), actual.size())) {
			logger::critical(
				"[RR][HandMirror][soft-effect-depth] exact batch dispatcher entry was unreadable; private SoftEffect depth transaction not installed");
			return false;
		}
		const bool nativeEntry =
			HandMirrorSoftEffectHookChainPolicy::IsNativeEntry(
				contract->runtime, actual);
		const bool pinnedPeerChain = !nativeEntry &&
			IsPinnedEngineFixesChain(*contract, target.address(), actual);
		if (!nativeEntry && !pinnedPeerChain) {
			logger::critical(
				"[RR][HandMirror][soft-effect-depth] exact {} batch dispatcher is neither native nor its pinned Engine Fixes SafetyHook chain; private SoftEffect depth transaction not installed",
				contract->name);
			return false;
		}
		if (pinnedPeerChain) {
			logger::info(
				"[RR][HandMirror][soft-effect-depth] exact {} Engine Fixes SafetyHook chain accepted at owner RVA 0x{:X}; attaching outward without replacing the peer or native continuation",
				contract->name, contract->engineFixesOwnerRVA);
		}

		// Detours copies the already-proven E9 into our trampoline. Calling func
		// therefore reaches the SafetyHook relay, Engine Fixes' wrapper, its own
		// native trampoline, and finally Skyrim in that order.
		const bool installed =
			stl::detour_thunk<SetupAndDrawPassHook>(kSetupAndDrawPass);
		g_installed.store(installed, std::memory_order_release);
		g_alphaDrawEnabled.store(installed && !REL::Module::IsVR() &&
			HandMirrorSafetySettings::EmptyMarker(L"Data\\MirrorsOfSkyrim_MirrorDrawAlphaTest.enable"),
			std::memory_order_release);
		if (installed) {
			logger::info(
				"[RR][HandMirror][soft-effect-depth] exact {} batch dispatcher installed (peerChained={}); only native SoftEffect draws sample private depth at PS t3; VS constant buffers remain native",
				contract->name, pinnedPeerChain);
		} else {
			logger::critical(
				"[RR][HandMirror][soft-effect-depth] batch dispatcher detour failed");
		}
		return installed;
	}

	bool TryGetPinnedCommunityShaders184Module(
		std::uintptr_t& moduleBase) noexcept
	{
		moduleBase = 0;
		HMODULE module = nullptr;
		if (!VerifyLoadedModuleFileIdentity(
				L"CommunityShaders.dll", kCommunityShaders184Length,
				kCommunityShaders184SHA256, module) ||
			!module) {
			return false;
		}
		moduleBase = reinterpret_cast<std::uintptr_t>(module);
		return moduleBase != 0;
	}

	bool IsInstalled() noexcept
	{
		return g_installed.load(std::memory_order_acquire);
	}

	bool Faulted() noexcept
	{
		return g_faulted.load(std::memory_order_acquire);
	}

	bool PrepareForWritableRebind(ID3D11DeviceContext* context) noexcept
	{
		const bool effectInvocationMismatch = g_active.active &&
			(g_setupAndDrawPassDepth == 0 ||
			 g_active.dispatcherDepth != g_setupAndDrawPassDepth);
		const bool playerInvocationMismatch = g_activePlayerDepthClip.active &&
			(g_setupAndDrawPassDepth == 0 ||
			 g_activePlayerDepthClip.dispatcherDepth !=
				g_setupAndDrawPassDepth);
		if (effectInvocationMismatch || playerInvocationMismatch) {
			// A nested dispatcher must never consume or overwrite the outer draw's
			// private depth/raster transaction. Abort before target rebind.
			EnterFailStop();
			return false;
		}
		// A prior read-only-depth commit leaves private t3 live behind Skyrim's
		// shadow state; remove that alias before the target becomes writable again.
		if (!g_active.active || !g_active.mutationMayHaveOccurred)
			return true;
		bool prepared = false;
#if defined(_MSC_VER)
		__try {
#endif
			if (context == g_active.context &&
				GetCurrentThreadId() == g_active.threadID) {
				prepared = RefreshRetainedNativeDepthBinding(context);
				if (prepared) {
					ID3D11ShaderResourceView* nullDepth = nullptr;
					context->PSSetShaderResources(
						HandMirrorSoftEffectPolicy::kDepthResourceSlot, 1, &nullDepth);
					prepared = VerifyShaderResource(context, nullptr);
				}
			}
#if defined(_MSC_VER)
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			prepared = false;
		}
#endif
		if (!prepared)
			EnterFailStop();
		return prepared;
	}

	CommitResult OnSetDirtyStatesCommitted(
		ID3D11DeviceContext* context) noexcept
	{
		const bool effectOwned = g_active.active &&
			g_active.dispatcherDepth == g_setupAndDrawPassDepth;
		const bool playerOwned = g_activePlayerDepthClip.active &&
			g_activePlayerDepthClip.dispatcherDepth ==
				g_setupAndDrawPassDepth;
		if (!effectOwned && !playerOwned)
			return CommitResult::kInactive;
		CommitResult effectResult = CommitResult::kInactive;
		CommitResult playerResult = CommitResult::kInactive;
#if defined(_MSC_VER)
		__try {
			if (effectOwned)
				effectResult = CommitEffectDrawRaw(context);
			if (playerOwned)
				playerResult = CommitPlayerDepthClipRaw(context);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (effectOwned)
				effectResult = CommitResult::kFailed;
			if (playerOwned)
				playerResult = CommitResult::kFailed;
		}
#else
		if (effectOwned)
			effectResult = CommitEffectDrawRaw(context);
		if (playerOwned)
			playerResult = CommitPlayerDepthClipRaw(context);
#endif
		if (effectResult == CommitResult::kFailed &&
			playerResult != CommitResult::kFailed && g_active.placed) {
			// A placed draw whose private depth could not be proven renders with
			// whatever depth is bound; its close restores writable depth.
			g_placedSoftDepthFallbacks.fetch_add(1, std::memory_order_relaxed);
			return playerResult == CommitResult::kApplied ?
				CommitResult::kApplied : CommitResult::kInactive;
		}
		if (effectResult == CommitResult::kFailed ||
			playerResult == CommitResult::kFailed) {
			if (playerResult == CommitResult::kFailed)
				g_playerRejects.fetch_add(1, std::memory_order_relaxed);
			EnterFailStop();
			return CommitResult::kFailed;
		}
		return effectResult == CommitResult::kApplied ||
			playerResult == CommitResult::kApplied ?
			CommitResult::kApplied : CommitResult::kInactive;
	}

	Diagnostics ReadDiagnostics() noexcept
	{
		return {
			.arms = g_arms.load(std::memory_order_relaxed),
			.commits = g_commits.load(std::memory_order_relaxed),
			.reapplies = g_reapplies.load(std::memory_order_relaxed),
			.restores = g_restores.load(std::memory_order_relaxed),
			.playerMatches = g_playerMatches.load(std::memory_order_relaxed),
			.playerArms = g_playerArms.load(std::memory_order_relaxed),
			.playerCommits = g_playerCommits.load(std::memory_order_relaxed),
			.playerReapplies = g_playerReapplies.load(std::memory_order_relaxed),
			.playerRestores = g_playerRestores.load(std::memory_order_relaxed),
			.playerRejects = g_playerRejects.load(std::memory_order_relaxed),
			.faults = g_faults.load(std::memory_order_relaxed),
			.alphaDrawCorrections = g_alphaDrawCorrections.load(std::memory_order_relaxed),
			.alphaDrawReadFaults = g_alphaDrawReadFaults.load(std::memory_order_relaxed),
			.captureSnowDraws = g_captureSnowDraws.load(std::memory_order_relaxed),
			.captureSnowAlphaTest = g_captureSnowExcluded.load(std::memory_order_relaxed),
			.captureTerrainDraws = g_captureTerrainDraws.load(std::memory_order_relaxed),
			.waterSSRNeutralBinds = g_waterSSRNeutralBinds.load(std::memory_order_relaxed),
			.waterSSRNeutralFailures = g_waterSSRNeutralFailures.load(std::memory_order_relaxed),
			.waterPrivateTechniqueRewrites = g_waterPrivateTechniqueRewrites.load(std::memory_order_relaxed),
			.placedEffectDrawsSeen = g_placedEffectDrawsSeen.load(std::memory_order_relaxed),
			.placedParticleDrawsSeen = g_placedParticleDrawsSeen.load(std::memory_order_relaxed),
			.placedEffectParticleSkipped = g_placedEffectParticleSkipped.load(std::memory_order_relaxed),
			.placedEffectPrivateDepth = g_placedEffectPrivateDepth.load(std::memory_order_relaxed),
			.placedSoftDepthFallbacks = g_placedSoftDepthFallbacks.load(std::memory_order_relaxed),
			.placedFirstPersonEffectSkips = g_placedFirstPersonEffectSkips.load(std::memory_order_relaxed),
			.nullShaderPassSkips = g_nullShaderPassSkips.load(std::memory_order_relaxed),
			.alphaDrawEnabled = g_alphaDrawEnabled.load(std::memory_order_relaxed)
		};
	}
}
