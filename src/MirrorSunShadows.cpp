#include "MirrorSunShadows.h"
#include "MirrorShadowMaskPass.h"
#include "MirrorSunShadowD3DState.h"
#include "MirrorSunShadowReadback.h"
#include "MirrorSunShadowShader.h"
#include "MirrorPrivateShadow.h"
#include "MirrorNeutralShadowMap.h"
#include "MirrorCaptureScreenState.h"
#include "MirrorFeatures.h"
#include "MirrorShaderWorkPolicy.h"
#include "PCH.h"
#include "PeerDetection.h"
#include "SecondView.h"
#include "SKSELogDirectoryPolicy.h"
#include "SupportedRuntimePolicy.h"
#include "MirrorPlayerInclusion.h"
#include <array>
#include <condition_variable>
#include <d3d11.h>
#include <deque>
#include <vector>
#include <wrl/client.h>
#if defined(MIRRORS_OF_SKYRIM_STANDALONE)
#include "MirrorsOfSkyrimCameraOverride.h"
#else
#include "MirrorCameraOverride.h"
#endif

namespace MirrorSunShadows {
namespace {
using Microsoft::WRL::ComPtr;
// Owned by the original shader through D3D private-data lifetime. Neither
// the entry nor its private variant retains the original shader.
constexpr GUID kEntryId{0x7d795cf1,
                        0xe8b4,
                        0x4877,
                        {0x9f, 0x73, 0x8a, 0x1d, 0x9b, 0xa7, 0x29, 0x02}};
struct Entry final : IUnknown {
  std::atomic_ulong refs{1};
  std::atomic_bool ready{false};
  std::atomic_bool opaqueCaster{false};
  std::atomic_bool casterReady{false};
  ComPtr<ID3D11PixelShader> variant;
  ComPtr<ID3D11PixelShader> casterVariant;
  bool usesPrivateSampler{};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *out = static_cast<IUnknown *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
};
struct Work {
  ComPtr<Entry> entry;
  ComPtr<ID3D11Device> device;
  std::vector<std::uint8_t> bytes;
};
struct CapturePacket {
  nlohmann::json metadata;
  std::array<std::vector<std::uint8_t>, 4> bytes;
  unsigned number{};
};
struct Worker {
  std::mutex mutex;
  std::condition_variable available;
  std::deque<Work> queue;
  std::size_t pendingBytes{};
  std::deque<CapturePacket> captures;
  std::filesystem::path captureDirectory;
};
Worker &Jobs() {
  static auto *value = new Worker;
  return *value;
}
std::atomic_bool g_enabled{false};
std::atomic_bool g_privateGeneration{false};
std::atomic_bool g_enbGeneration{false};
std::atomic_bool g_communityShadersAtDevice{false};
std::atomic_uint64_t g_receiverBuildsSkipped{};
ComPtr<ID3D11DeviceContext> g_enbContext;
thread_local MirrorPrivateShadow::Publication g_privateMap;
thread_local MirrorPrivateShadow::Publication g_privateDetailMap;
struct CasterCamera {
  bool active{}, pending{};
  unsigned uploads{};
  RE::BSGraphics::RendererShadowState* owner{};
  RE::BSGraphics::ViewData saved{}, expected{};
  RE::NiPoint3 origin{}, previous{}, eye{};
};
thread_local CasterCamera g_casterCamera;
thread_local MirrorPrivateShadow::CasterDraw g_casterDraw;
thread_local bool g_cutoutDrawPending{};
std::atomic_uint64_t g_depthOnlyDraws{}, g_mapReuses{};
std::atomic_bool g_casterDepthShaders{};
std::atomic_uint64_t g_casterBuilt{}, g_casterRejected{}, g_cutoutDepthDraws{}, g_cutoutDepthRestores{};
std::atomic_bool g_captureEnabled{false};
std::atomic_uint64_t g_capturesWritten{}, g_captureFaults{};
std::atomic_uint64_t g_queued{}, g_built{}, g_rejected{}, g_binds{},
    g_missing{}, g_noMaps{}, g_restores{}, g_faults{}, g_neutralBinds{},
    g_unshadowedBinds{}, g_neutralMaskBinds{}, g_clusterGridBinds{}, g_screenSpaceShadowBinds{},
    g_csVariantSkips{}, g_maskBinds{}, g_maskRenders{}, g_maskRejects{};
std::atomic_bool g_maskRouteEnabled{false};
// Per capture: set when the mask for this capture is current, so the pass
// builder keeps the mask descriptor bits and every draw binds the same mask.
thread_local bool g_maskActive{false};
// Community Shaders' Lighting.hlsl samples the shadow mask at
//   uv = SV_Position.xy * DynamicResolutionParams2.xy * VPOSOffset.xy + VPOSOffset.zw
// and BSLightingShader::SetupTechnique (SE 1.5.97 0x1412F1990) builds VPOSOffset
// as (1/DAT_14302BB3C, 1/DAT_14302BB40, 0, 0) -- the MAIN framebuffer size, read
// live as 1600x900. A 2048 or 4096 private capture therefore sampled the mask at
// pixel/1600, which runs past the right edge above x=1600 and clamps: the mask
// squeezed into a corner of the reflection and smeared over the rest. That is the
// owner's run-27 report ("shadows ... don't appear where they should"), and the
// step the Phase 2 plan left pending. Present the capture's own size in those
// globals for exactly as long as the mask is bound, so the engine's own uv math
// resolves to pixel / captureSize.
// The contact-shadow neutral is Loaded at raster coordinates, so it must cover
// every pixel of the capture that binds it. The fetch is
//   Load(int3(int2(screenPosition.xy + 0.5f), 0))
// and SV_Position is the pixel centre, so pixel n reads texel n+1: a neutral
// sized exactly to the capture still loses its last row and column, and the 1x1
// this used to bind was out of bounds for every pixel including the first.
// 4096 is the largest resolution either slider offers, so the neutral is one
// texel larger than that. Proved on the GPU in
// tests/MirrorShadowMaskLookupWarpTests.cpp.
constexpr std::uint32_t kMaximumNeutralCoveredCapture = 4096;
constexpr std::uint32_t kNeutralScreenSpaceShadowExtent =
    kMaximumNeutralCoveredCapture + 1;
constexpr std::uintptr_t kScreenWidthGlobalOffset = 0x302BB3C;
constexpr std::uintptr_t kScreenHeightGlobalOffset = 0x302BB40;
thread_local bool g_screenSizePatched{false};
thread_local MirrorCaptureScreenState::Lease g_captureScreenState;

// These were pinned to SE 1.5.97 by absolute RVA, which meant the patch below
// was inert on AE and VR -- silently. The owner spent 2026-09-15/16 on AE
// chasing horizontal bands across reflected water and a vertical line down a
// reflected mirror, which are the two faces of exactly this: a uv that reaches
// 1.0 at the MAIN framebuffer's width or height and clamps to a repeated column
// or row for the rest of an oversized capture. Clamped in x it is a vertical
// seam, in y a horizontal one.
//
// CommonLibSSE resolves the same two fields per runtime, and asserts their
// offsets for SE, AE and VR alike, so nothing needs pinning here at all.
[[nodiscard]] RE::BSGraphics::State* ScreenSizeState() noexcept {
  __try {
    return RE::BSGraphics::State::GetSingleton();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}
[[nodiscard]] std::uint32_t* ScreenSizeGlobal(std::uintptr_t offset) noexcept {
  auto* const state = ScreenSizeState();
  if (!state) return nullptr;
  // Offsets kept as the selector so both call sites stay unchanged; the base is
  // now the resolved singleton rather than a per-runtime module RVA.
  if (offset == kScreenWidthGlobalOffset) return &state->screenWidth;
  if (offset == kScreenHeightGlobalOffset) return &state->screenHeight;
  return nullptr;
}
[[nodiscard]] __declspec(noinline) bool PatchScreenSizeGlobalsSEH(
    std::uint32_t* widthGlobal, std::uint32_t* heightGlobal,
    std::uint32_t width, std::uint32_t height) noexcept {
  __try {
    std::array<float*,4> ratios{};
    if (PeerDetection::CommunityShadersPresent()) {
      auto* state = ScreenSizeState();
      if (!state) return false;
      auto& runtime = state->GetRuntimeData();
      ratios = {&runtime.dynamicResolutionWidthRatio, &runtime.dynamicResolutionHeightRatio,
        &runtime.dynamicResolutionPreviousWidthRatio, &runtime.dynamicResolutionPreviousHeightRatio};
    }
    // Private targets rasterise their complete extent. Native per-frame b12
    // uploads must therefore see unit scale, including previous-frame ratios.
    // Retaining the main upscale ratio makes CS shadow/water UVs run off-target.
    return g_captureScreenState.Begin(*widthGlobal, *heightGlobal, width, height, ratios);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Begin saves the complete restore record before its first mutation.
    // A partial write must also unwind when the caller never received success.
    __try { g_captureScreenState.Restore(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
    return false;
  }
}
bool PatchScreenSizeGlobals(std::uint32_t width, std::uint32_t height) noexcept {
  if (width == 0 || height == 0) return false;
  if (g_screenSizePatched) {
    __try { return g_captureScreenState.Matches(width, height); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
  }
  auto* const widthGlobal = ScreenSizeGlobal(kScreenWidthGlobalOffset);
  auto* const heightGlobal = ScreenSizeGlobal(kScreenHeightGlobalOffset);
  if (!widthGlobal || !heightGlobal) return false;
  g_screenSizePatched =
      PatchScreenSizeGlobalsSEH(widthGlobal, heightGlobal, width, height);
  return g_screenSizePatched;
}
__declspec(noinline) void RestoreScreenSizeGlobalsSEH(
    std::uint32_t* widthGlobal, std::uint32_t* heightGlobal) noexcept {
  __try {
    (void)widthGlobal; (void)heightGlobal;
    g_captureScreenState.Restore();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}
void RestoreScreenSizeGlobals() noexcept {
  if (!g_screenSizePatched) return;
  RestoreScreenSizeGlobalsSEH(ScreenSizeGlobal(kScreenWidthGlobalOffset),
                              ScreenSizeGlobal(kScreenHeightGlobalOffset));
  g_screenSizePatched = false;
}
// Private CPU attribution only: no GPU query, binding, allocation or logging in a
// native draw. Sample one of every 128 calls and keep each draw class separate.
struct NativeDrawTiming {
  std::atomic_uint64_t calls{}, samples{}, preparationTicks{}, submissionTicks{},
      restorationTicks{}, completedSamples{}, skippedSamples{}, invalidSamples{};
};
NativeDrawTiming g_casterTiming, g_receiverTiming;
std::atomic_bool g_drawTimingEnabled{};
std::uint64_t g_drawTimingFrequency{};
struct NativeDrawSample {
  NativeDrawTiming* totals{};
  std::uint64_t begin{}, prepared{}, submitted{};
};
std::uint64_t DrawClock() noexcept {
  LARGE_INTEGER value{};
  return QueryPerformanceCounter(&value) && value.QuadPart > 0 ?
      static_cast<std::uint64_t>(value.QuadPart) : 0;
}
NativeDrawSample BeginDrawSample(bool caster) noexcept {
  if (!g_drawTimingEnabled.load(std::memory_order_relaxed)) return {};
  auto& totals = caster ? g_casterTiming : g_receiverTiming;
  if ((totals.calls.fetch_add(1, std::memory_order_relaxed) & 127u) != 0) return {};
  return {&totals, DrawClock()};
}
void EndDrawSample(const NativeDrawSample& sample, bool completed) noexcept {
  if (!sample.totals) return;
  const auto end = DrawClock();
  auto& totals = *sample.totals;
  if (!sample.begin || !sample.prepared || !sample.submitted ||
      sample.prepared < sample.begin || sample.submitted < sample.prepared ||
      end < sample.submitted) {
    totals.invalidSamples.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  totals.preparationTicks.fetch_add(sample.prepared - sample.begin, std::memory_order_relaxed);
  totals.submissionTicks.fetch_add(sample.submitted - sample.prepared, std::memory_order_relaxed);
  totals.restorationTicks.fetch_add(end - sample.submitted, std::memory_order_relaxed);
  (completed ? totals.completedSamples : totals.skippedSamples).fetch_add(1, std::memory_order_relaxed);
  totals.samples.fetch_add(1, std::memory_order_release);
}
thread_local bool g_creatingVariant = false;
using CreatePS = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const void *,
                                              SIZE_T, ID3D11ClassLinkage *,
                                              ID3D11PixelShader **);
CreatePS g_createPS{};
decltype(&D3D11CreateDeviceAndSwapChain) g_createDevice{};
ComPtr<Entry> Find(ID3D11PixelShader *shader) {
  ComPtr<Entry> entry;
  UINT size = sizeof(Entry *);
  if (shader &&
      FAILED(shader->GetPrivateData(kEntryId, &size, entry.GetAddressOf())))
    entry.Reset();
  return entry;
}
void RunWorker() {
  for (;;) {
    Work work;
    CapturePacket capture;
    {
      auto &jobs = Jobs();
      std::unique_lock lock(jobs.mutex);
      jobs.available.wait(
          lock, [&] { return !jobs.queue.empty() || !jobs.captures.empty(); });
      if (!jobs.captures.empty()) {
        capture = std::move(jobs.captures.front());
        jobs.captures.pop_front();
      } else {
        work = std::move(jobs.queue.front());
        jobs.queue.pop_front();
        jobs.pendingBytes -= work.bytes.size();
      }
    }
    if (capture.number) {
      try {
        const auto directory = Jobs().captureDirectory;
        std::filesystem::create_directories(directory);
        const auto stem = std::format("shadow-{:03}", capture.number);
        constexpr const char *names[]{"far.rg16", "near.rg16", "sun.f32",
                                      "vs-frame.f32"};
        for (unsigned i = 0; i < capture.bytes.size(); ++i) {
          const auto name = stem + "-" + names[i];
          std::ofstream file(directory / name,
                             std::ios::binary | std::ios::trunc);
          file.exceptions(std::ios::failbit | std::ios::badbit);
          file.write(reinterpret_cast<const char *>(capture.bytes[i].data()),
                     static_cast<std::streamsize>(capture.bytes[i].size()));
          file.close();
          capture.metadata["files"][names[i]] = {
              {"name", name}, {"bytes", capture.bytes[i].size()}};
        }
        // The receipt is written last. No receipt means an incomplete sample.
        std::ofstream receipt(directory / (stem + ".json"), std::ios::trunc);
        receipt.exceptions(std::ios::failbit | std::ios::badbit);
        receipt << capture.metadata.dump(2) << '\n';
        receipt.close();
        ++g_capturesWritten;
        logger::info("[MOS][SunShadows][Capture] saved {}",
                     (directory / (stem + ".json")).string());
      } catch (const std::exception &e) {
        ++g_captureFaults;
        g_captureEnabled.store(false);
        logger::warn(
            "[MOS][SunShadows][Capture] disabled after write failure: {}",
            e.what());
      }
      continue;
    }
    try {
      if (g_privateGeneration.load()) work.entry->opaqueCaster.store(
          MirrorSunShadowShader::CanRenderDepthOnly(work.bytes),std::memory_order_release);
      if (g_casterDepthShaders.load() && !work.entry->opaqueCaster.load()) {
        const auto caster = MirrorSunShadowShader::BuildCaster(work.bytes);
        g_creatingVariant = true;
        const auto casterHR = caster.bytecode.empty() ? E_FAIL : work.device->CreatePixelShader(
            caster.bytecode.data(), caster.bytecode.size(), nullptr, &work.entry->casterVariant);
        g_creatingVariant = false;
        if (SUCCEEDED(casterHR)) {
          ++g_casterBuilt;
          work.entry->casterReady.store(true, std::memory_order_release);
        } else ++g_casterRejected; // Native program stays available immediately.
      }
      // Run 9 built 8,615 receiver variants and bound zero under CS. Keep
      // caster work above, but never compile a receiver nobody can consume.
      if (!MirrorShaderWorkPolicy::BuildReceiver(
              g_communityShadersAtDevice.load(std::memory_order_relaxed),
              g_enbGeneration.load(std::memory_order_relaxed))) {
        ++g_receiverBuildsSkipped;
        continue;
      }
      auto result = g_enbGeneration.load() ? MirrorSunShadowShader::BuildENB(work.bytes) :
          MirrorSunShadowShader::Build(work.bytes, g_privateGeneration.load(),g_privateGeneration.load());
      g_creatingVariant = true;
      const auto hr = result.bytecode.empty()
                          ? E_FAIL
                          : work.device->CreatePixelShader(
                                result.bytecode.data(), result.bytecode.size(),
                                nullptr, &work.entry->variant);
      g_creatingVariant = false;
      if (SUCCEEDED(hr)) {
        ++g_built;
        work.entry->usesPrivateSampler = result.usesPrivateSampler;
        work.entry->ready.store(true, std::memory_order_release);
      } else {
        ++g_rejected;
        // Vanilla lighting programs that do not declare the private-sun
        // constant layout are left unchanged on purpose. Logging those skips
        // made expected interior work look like a shader fault.
        if (result.reason.find("missing light declaration") != 0) {
          static std::atomic_uint64_t unexpected{};
          if (++unexpected <= 8)
            logger::info("[MOS][SunShadows] shader kept unchanged: {} hr={:08X}",
                         result.reason, static_cast<unsigned>(hr));
        }
      }
    } catch (...) {
      g_creatingVariant = false;
      ++g_rejected;
    }
  }
}
HRESULT STDMETHODCALLTYPE CreatePixelShader(ID3D11Device *device,
                                            const void *bytes, SIZE_T size,
                                            ID3D11ClassLinkage *linkage,
                                            ID3D11PixelShader **output) {
  const auto hr = g_createPS(device, bytes, size, linkage, output);
  if (g_creatingVariant || !g_enabled.load(std::memory_order_relaxed) ||
      FAILED(hr) || !output || !*output || linkage || size > 4 * 1024 * 1024)
    return hr;
  try {
    // Cheap prefilter only. Full reflection/slot/operand validation runs
    // on the worker, never during rendering or shader creation.
    constexpr char needle[] = "\0DirLightColor";
    const auto *first = static_cast<const char *>(bytes);
    if ((!g_privateGeneration.load(std::memory_order_relaxed) &&
         std::search(first, first + size, needle, needle + sizeof(needle)) ==
            first + size) ||
        Find(*output))
      return hr;
    auto &jobs = Jobs();
    std::lock_guard lock(jobs.mutex);
    if (jobs.pendingBytes + size > 64 * 1024 * 1024) {
      ++g_rejected;
      return hr;
    }
    Work work;
    work.entry.Attach(new Entry);
    work.device = device;
    work.bytes.assign(static_cast<const std::uint8_t *>(bytes),
                      static_cast<const std::uint8_t *>(bytes) + size);
    if (FAILED((*output)->SetPrivateDataInterface(kEntryId, work.entry.Get())))
      return hr;
    jobs.pendingBytes += size;
    jobs.queue.push_back(std::move(work));
    ++g_queued;
    jobs.available.notify_one();
  } catch (...) {
    ++g_rejected;
  }
  return hr;
}
HRESULT WINAPI CreateDevice(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type,
                            HMODULE software, UINT flags,
                            const D3D_FEATURE_LEVEL *levels, UINT count,
                            UINT version, const DXGI_SWAP_CHAIN_DESC *desc,
                            IDXGISwapChain **swap, ID3D11Device **device,
                            D3D_FEATURE_LEVEL *level,
                            ID3D11DeviceContext **context) {
  const auto hr = g_createDevice(adapter, type, software, flags, levels, count,
                                 version, desc, swap, device, level, context);
  if (flags & D3D11_CREATE_DEVICE_SINGLETHREADED) {
    g_enabled.store(false);
    return hr;
  }
  if (SUCCEEDED(hr) && device && *device && !g_createPS &&
      g_enabled.load(std::memory_order_relaxed)) {
    // ID3D11Device public COM ABI, slot 15; retain whichever peer is
    // already there. Later peers likewise chain this observer.
    auto **table = *reinterpret_cast<void ***>(*device);
    HMODULE owner{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(table[15]), &owner) &&
        GetProcAddress(owner, "ENBGetSDKVersion")) {
      // OnCommit and PrivateGenerationEnabled already exclude ENB. Building
      // vanilla variants on its wrapper allocated thousands of shader objects
      // that could never be bound. Test the actual device here, after creation,
      // without freezing the process-wide peer cache during SKSE plugin load.
      g_enabled.store(false);
      g_privateGeneration.store(false);
      logger::info("[MOS][SunShadows] ENB device: unsupported vanilla shader worker skipped; no variants queued");
      return hr;
    }
    // Device creation follows SKSE DLL loading. Read the module directly here:
    // PeerDetection's process-wide cache must not freeze during plugin load.
    g_communityShadersAtDevice.store(GetModuleHandleW(L"CommunityShaders.dll") != nullptr,
                                     std::memory_order_relaxed);
    try {
      // Start only when a compatible device can supply work. An ENB session
      // needs neither this idle thread nor its compiler/resource queue.
      std::thread(RunWorker).detach();
    } catch (const std::exception& error) {
      g_enabled.store(false);
      logger::warn("[MOS][SunShadows] shader worker unavailable: {}", error.what());
      return hr;
    }
    g_createPS = reinterpret_cast<CreatePS>(table[15]);
    REL::safe_write(reinterpret_cast<std::uintptr_t>(&table[15]),
                    reinterpret_cast<std::uintptr_t>(&CreatePixelShader));
  }
  return hr;
}
struct Projection {
  DirectX::XMFLOAT4X4 inverse;
  DirectX::XMFLOAT4 origin;
  DirectX::XMFLOAT4 viewport;
};
struct Resources {
  ComPtr<ID3D11Device> device;
  // Only the CS neutral route uses this fast path. It reads none of the
  // projection constants; its four fixed resources are independent of camera,
  // target size (up to 4096), and viewport. Retaining the context prevents an
  // address reuse from carrying resources across devices.
  ComPtr<ID3D11DeviceContext> neutralContext;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11ShaderResourceView> neutralMoments;
  // Vanilla: 1x1 white shadow mask (t14) for draws whose program has no
  // rebuilt private variant, so they render unshadowed instead of sampling the
  // main camera's screen-space mask (camera-dependent contact shadows).
  ComPtr<ID3D11ShaderResourceView> neutralMask;
  // Community Shaders Light Limit Fix: all-zero LightGrid (t37) so a mirror
  // draw takes no cluster lights from the main camera's screen-space grid.
  ComPtr<ID3D11ShaderResourceView> neutralLightGrid;
  // Community Shaders Screen Space Shadows: capture-covering white texture
  // (t45, Texture2D<unorm float>) so no main-screen contact shadow reaches a
  // private draw.
  ComPtr<ID3D11ShaderResourceView> neutralScreenSpaceShadow;
  MirrorShadowSettings::NeutralMap noShadows;
  ComPtr<ID3D11ShaderResourceView> validatedMoments, validatedMatrices;
  DirectX::XMFLOAT4X4 vp{};
  DirectX::XMFLOAT3 origin{};
  D3D11_VIEWPORT viewport{};
  Projection projection{};
  bool uploaded{};
};
thread_local Resources g_resources;
struct Lease : MirrorSunShadowD3DState {
  bool armed{};
  bool playerDetail{};
};
thread_local Lease g_lease;
bool EmptyMarker(const wchar_t *path) {
  if (MirrorFeatures::Enabled(path)) return true;
  const HANDLE file = CreateFileW(
      path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  FILE_ATTRIBUTE_TAG_INFO tag{};
  FILE_STANDARD_INFO standard{};
  const bool valid = GetFileType(file) == FILE_TYPE_DISK &&
                     GetFileInformationByHandleEx(file, FileAttributeTagInfo,
                                                  &tag, sizeof(tag)) &&
                     GetFileInformationByHandleEx(
                         file, FileStandardInfo, &standard, sizeof(standard)) &&
                     (tag.FileAttributes &
                      (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
                       FILE_ATTRIBUTE_DEVICE)) == 0 &&
                     !standard.Directory && standard.EndOfFile.QuadPart == 0;
  CloseHandle(file);
  return valid;
}

// Separate default-off evidence gate. A request file starts 24 samples; a
// second request may start another 24. No readback or file polling in normal
// builds/runs.
void CaptureDraw(ID3D11DeviceContext *context,
                 ID3D11ShaderResourceView *moments,
                 ID3D11ShaderResourceView *matrices) {
  if (!g_captureEnabled.load(std::memory_order_relaxed))
    return;
  struct Pending {
    std::array<MirrorSunShadowReadback::Item, 3> items;
    CapturePacket packet;
    ULONGLONG started{};
  };
  struct State {
    Pending pending;
    std::uint64_t lastFrame{};
    ULONGLONG requestPoll{}, lastCapture{};
    unsigned remaining{}, total{};
    bool requestWasPresent{};
  };
  static thread_local State state;
  const auto frame = SecondView::CurrentMainWorldSourceSequence();
  if (!frame || state.lastFrame == frame)
    return;
  state.lastFrame = frame;
  const auto now = GetTickCount64();
  try {
    if (state.pending.started) {
      bool ready = true;
      for (auto &item : state.pending.items) {
        const auto result = item.Poll(context);
        if (result == MirrorSunShadowReadback::Result::failed ||
            now - state.pending.started > 4000)
          throw std::runtime_error("nonblocking readback failed or expired");
        ready &= result == MirrorSunShadowReadback::Result::ready;
      }
      if (!ready)
        return;
      auto &pending = state.pending;
      pending.packet.bytes = {std::move(pending.items[0].bytes[0]),
                              std::move(pending.items[0].bytes[1]),
                              std::move(pending.items[1].bytes[0]),
                              std::move(pending.items[2].bytes[0])};
      auto &jobs = Jobs();
      {
        std::lock_guard lock(jobs.mutex);
        if (jobs.captures.size() >= 2)
          throw std::runtime_error("bounded file-writer queue full");
        jobs.captures.push_back(std::move(pending.packet));
      }
      jobs.available.notify_one();
      pending = {};
    }
    if (now - state.requestPoll >= 1000) {
      state.requestPoll = now;
      const bool requested =
          EmptyMarker(L"Data\\MirrorsOfSkyrim_ShadowCapture.request");
      if (requested && !state.requestWasPresent && !state.remaining &&
          state.total < 48) {
        state.remaining = 24;
        logger::info("[MOS][SunShadows][Capture] requested 24 samples, 500ms "
                     "minimum spacing");
      }
      state.requestWasPresent = requested;
    }
    if (!state.remaining || now - state.lastCapture < 500)
      return;
    ComPtr<ID3D11Resource> mr, sr;
    moments->GetResource(&mr);
    matrices->GetResource(&sr);
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11Buffer> sun, vsFrame;
    context->VSGetConstantBuffers(12, 1, &vsFrame);
    if (FAILED(mr.As(&texture)) || FAILED(sr.As(&sun)) || !vsFrame)
      throw std::runtime_error("draw resources unavailable");
    auto &pending = state.pending;
    if (!pending.items[0].Moments(context, texture.Get()) ||
        !pending.items[1].Buffer(context, sun.Get()) ||
        !pending.items[2].Buffer(context, vsFrame.Get()))
      throw std::runtime_error("staging resources rejected");
    pending.started = state.lastCapture = now;
    pending.packet.number = ++state.total;
    --state.remaining;
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    const auto floats = [](const float *data, unsigned count) {
      std::vector<float> result(count);
      std::memcpy(result.data(), data, count * sizeof(float));
      return result;
    };
    const auto &r = g_resources;
    pending.packet.metadata = {
        {"schema", 1},
        {"number", pending.packet.number},
        {"sourceFrame", frame},
        {"pass", SecondView::MirrorPrimaryPassSequence()},
        {"tickMs", now},
        {"utcFiletime",
         (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
             time.dwLowDateTime},
        {"raisedPhysicalHand",
         SecondView::IsInsideExactHandPhysicalRasterClip()},
        {"viewProjectionRowMajor", floats(&r.vp._11, 16)},
        {"inverseViewProjectionRowMajor",
         floats(&r.projection.inverse._11, 16)},
        {"origin", floats(&r.projection.origin.x, 4)},
        {"viewport", floats(&r.projection.viewport.x, 4)},
        {"readback", "Public D3D staging, DO_NOT_WAIT, no Flush or GPU wait; "
                     "original bindings unchanged"}};
    if (const auto *player = RE::PlayerCharacter::GetSingleton()) {
      const auto position = player->GetPosition();
      pending.packet.metadata["playerPosition"] = {position.x, position.y,
                                                   position.z};
    }
  } catch (const std::exception &e) {
    state.pending = {};
    ++g_captureFaults;
    g_captureEnabled.store(false);
    logger::warn(
        "[MOS][SunShadows][Capture] disabled, reflection unchanged: {}",
        e.what());
  }
}
bool ValidMaps(ID3D11ShaderResourceView *moments,
               ID3D11ShaderResourceView *matrices) {
  if (!moments || !matrices)
    return false;
  if (g_resources.validatedMoments.Get() == moments &&
      g_resources.validatedMatrices.Get() == matrices)
    return true;
  D3D11_SHADER_RESOURCE_VIEW_DESC m{}, d{};
  moments->GetDesc(&m);
  matrices->GetDesc(&d);
  if (m.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
      m.Format != DXGI_FORMAT_R16G16_UNORM ||
      m.Texture2D.MostDetailedMip != 0 || m.Texture2D.MipLevels != 2 ||
      d.ViewDimension != D3D11_SRV_DIMENSION_BUFFER ||
      d.Format != DXGI_FORMAT_UNKNOWN || d.Buffer.FirstElement != 0 ||
      d.Buffer.NumElements != 1)
    return false;
  ComPtr<ID3D11Resource> mr, dr;
  moments->GetResource(&mr);
  matrices->GetResource(&dr);
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11Buffer> data;
  if (FAILED(mr.As(&tex)) || FAILED(dr.As(&data)))
    return false;
  D3D11_TEXTURE2D_DESC td{};
  D3D11_BUFFER_DESC bd{};
  tex->GetDesc(&td);
  data->GetDesc(&bd);
  // Published GPU contract from stock CS 1.8.4, no private C++ object.
  const bool valid = td.Width == 512 && td.Height == 512 && td.MipLevels == 2 &&
                     td.ArraySize == 1 && td.SampleDesc.Count == 1 &&
                     bd.StructureByteStride == 272 && bd.ByteWidth == 272;
  if (valid) {
    g_resources.validatedMoments = moments;
    g_resources.validatedMatrices = matrices;
  }
  return valid;
}
bool Prepare(ID3D11DeviceContext *context) {
  const bool neutralOnly = PeerDetection::CommunityShadersPresent();
  if (neutralOnly && context && g_resources.neutralContext.Get() == context &&
      g_resources.neutralMoments && g_resources.neutralMask &&
      g_resources.neutralLightGrid && g_resources.neutralScreenSpaceShadow)
    return true;
  DirectX::XMFLOAT4X4 vp;
  DirectX::XMFLOAT3 origin;
  if (!MirrorCameraOverride::GetPatchedFrameData(nullptr, vp, origin))
    return false;
  UINT count = 1;
  D3D11_VIEWPORT viewport{};
  context->RSGetViewports(&count, &viewport);
  if (count != 1 || !(viewport.Width >= 1 && viewport.Height >= 1) ||
      viewport.MinDepth != 0 || viewport.MaxDepth != 1)
    return false;
  auto &r = g_resources;
  ComPtr<ID3D11Device> device;
  context->GetDevice(&device);
  if (r.device.Get() != device.Get())
    r = {};
  if (r.uploaded && std::memcmp(&vp, &r.vp, sizeof(vp)) == 0 &&
      std::memcmp(&origin, &r.origin, sizeof(origin)) == 0 &&
      std::memcmp(&viewport, &r.viewport, sizeof(viewport)) == 0)
    return true;
  Projection p{};
  DirectX::XMVECTOR determinant;
  const auto inverse =
      DirectX::XMMatrixInverse(&determinant, DirectX::XMLoadFloat4x4(&vp));
  const auto det = DirectX::XMVectorGetX(determinant);
  if (!std::isfinite(det) || std::abs(det) < 1e-12f ||
      DirectX::XMMatrixIsNaN(inverse) || DirectX::XMMatrixIsInfinite(inverse))
    return false;
  DirectX::XMStoreFloat4x4(&p.inverse, inverse);
  p.origin = {origin.x, origin.y, origin.z, 0};
  p.viewport = {viewport.TopLeftX, viewport.TopLeftY, 1 / viewport.Width,
                1 / viewport.Height};
  if (!r.constants) {
    r.device = device;
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = sizeof(Projection);
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&d, nullptr, &r.constants)))
      return false;
    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&s, &r.sampler))) {
      r = {};
      return false;
    }
    const std::uint16_t white[8]{0xffff, 0xffff, 0xffff, 0xffff,
                                 0xffff, 0xffff, 0xffff, 0xffff};
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 2;
    td.MipLevels = 2;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial[2]{{white, 8, 0}, {white, 4, 0}};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&td, initial, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr,
                                                &r.neutralMoments))) {
      r = {};
      return false;
    }
  }
  if (!r.neutralMask) {
    const std::uint32_t white = 0xffffffffu;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 1;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{&white, 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&td, &initial, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &r.neutralMask))) {
      r = {};
      return false;
    }
  }
  if (!r.neutralLightGrid && PeerDetection::CommunityShadersPresent()) {
    // CS 1.8.x LightLimitFix/Common.hlsli: LightGrid { uint offset; uint lightCount; uint pad0[2]; }
    // at register t37. 4096 zero cells; out-of-range structured reads return
    // zero as well, so any cluster count is covered.
    constexpr UINT kCells = 4096, kStride = 16;
    std::vector<std::uint8_t> zero(kCells * kStride, 0);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = kCells * kStride;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = kStride;
    D3D11_SUBRESOURCE_DATA initial{zero.data(), 0, 0};
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.FirstElement = 0;
    sd.Buffer.NumElements = kCells;
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device->CreateBuffer(&bd, &initial, &buffer)) ||
        FAILED(device->CreateShaderResourceView(buffer.Get(), &sd, &r.neutralLightGrid))) {
      r = {};
      return false;
    }
  }
  if (!r.neutralScreenSpaceShadow && PeerDetection::CommunityShadersPresent()) {
    // Community Shaders reads the contact-shadow texture with a Load at the
    // raster position:
    //   ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition.xy + 0.5f), 0)).x
    // (ScreenSpaceShadows.hlsli), under `SCREEN_SPACE_SHADOWS && DEFERRED` and
    // `!InInterior && dirLightAngle >= 0` -- outdoors with the sun up, which is
    // where the owner's reflections are judged. A Load outside the texture
    // returns 0, so the 1x1 neutral this used to bind made every capture pixel
    // except (0,0) read zero and multiplied the reflected sunlight away. The
    // neutral has to cover the whole capture plus the one texel the rounding
    // reaches, so it is allocated once at 4097 square (about 16 MiB at R8).
    const std::vector<std::uint8_t> white(
        static_cast<std::size_t>(kNeutralScreenSpaceShadowExtent) *
            kNeutralScreenSpaceShadowExtent,
        0xff);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kNeutralScreenSpaceShadowExtent;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{white.data(),
                                   kNeutralScreenSpaceShadowExtent, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&td, &initial, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &r.neutralScreenSpaceShadow))) {
      r = {};
      return false;
    }
  }
  if (!r.uploaded || std::memcmp(&p, &r.projection, sizeof(p)) != 0) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(r.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped)))
      return false;
    std::memcpy(mapped.pData, &p, sizeof(p));
    context->Unmap(r.constants.Get(), 0);
    r.projection = p;
    r.uploaded = true;
  }
  r.vp = vp;
  r.origin = origin;
  r.viewport = viewport;
  if (neutralOnly) r.neutralContext = context;
  return true;
}
} // namespace

void Install() {
  const bool se = REL::Module::IsSE() && REL::Module::get().version() == REL::Version{1, 5, 97, 0};
  const bool ae = REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(REL::Module::get().version());
  if (!se && !ae)
    return;
  const bool privateRequested = EmptyMarker(L"Data\\MirrorsOfSkyrim_PrivateSunShadows.enable");
  // Owner 2026-09-14 ("we need something that works with all settings", then
  // "turn it on ... make them internal switches"): the private mask is the
  // engine's own shadow mechanism, so it reaches Community Shaders' lighting
  // programs too. It is an internal default now; no file is placed or read.
  g_maskRouteEnabled.store(privateRequested &&
      MirrorFeatures::Enabled(L"MirrorsOfSkyrim_ShadowMask.enable"), std::memory_order_release);
  if (!se && !privateRequested) return; // legacy CS-map reuse remains SE-only
  const HANDLE file = CreateFileW(
      L"Data\\RealisticReflections_MirrorSunShadows.enable", GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE && !privateRequested)
    return;
  FILE_ATTRIBUTE_TAG_INFO tag{};
  FILE_STANDARD_INFO standard{};
  const bool requested =
      GetFileType(file) == FILE_TYPE_DISK &&
      GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag,
                                   sizeof(tag)) &&
      GetFileInformationByHandleEx(file, FileStandardInfo, &standard,
                                   sizeof(standard)) &&
      (tag.FileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_DEVICE)) == 0 &&
      !standard.Directory && standard.EndOfFile.QuadPart == 0;
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (!requested && !privateRequested)
    return;
  g_createDevice = reinterpret_cast<decltype(g_createDevice)>(SKSE::PatchIAT(
      &CreateDevice, "d3d11.dll", "D3D11CreateDeviceAndSwapChain"));
  if (!g_createDevice) {
    logger::warn(
        "[MOS][SunShadows] device observer unavailable; unchanged lighting");
    return;
  }
  g_privateGeneration.store(privateRequested);
  g_casterDepthShaders.store(se && privateRequested &&
      MirrorFeatures::Enabled(L"MirrorsOfSkyrim_CasterDepthShaders.enable"));
  g_enabled.store(true);
  if (EmptyMarker(L"Data\\MirrorsOfSkyrim_ShadowCapture.enable")) {
    if (const auto directory = SKSELogDirectoryPolicy::Resolve(
            logger::log_directory(),
            REL::Module::IsVR(),
            REL::Module::get().version())) {
      Jobs().captureDirectory =
          *directory / std::format("MirrorsOfSkyrim-SunShadow-{}-{}",
                                   GetCurrentProcessId(), GetTickCount64());
      g_captureEnabled.store(true);
      logger::info("[MOS][SunShadows][Capture] prepared; exact empty "
                   "Data/MirrorsOfSkyrim_ShadowCapture.request starts bounded "
                   "samples; output={}",
                   Jobs().captureDirectory.string());
    }
  }
  logger::info("[MOS][SunShadows] opt-in flat sunlight prepared; privateGeneration={} "
               "resolution={} receiverRadius={} detailExtent=256 cameraRelatedCasterUpdates=false; ENB uses its separately verified native route; VR excluded", privateRequested,
               MirrorPrivateShadow::Resolution(false), MirrorPrivateShadow::kDefaultReceiverRadius);
}
void EnableENB(ID3D11DeviceContext* nativeContext) noexcept {
  const bool flat = (REL::Module::IsSE() && REL::Module::get().version()==REL::Version{1,5,97,0}) ||
      (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(REL::Module::get().version()));
  if (g_enbGeneration.load() || g_createPS || !nativeContext ||
      !flat ||
      !PeerDetection::ENBPresent() || PeerDetection::CommunityShadersPresent() ||
      !EmptyMarker(L"Data\\MirrorsOfSkyrim_ENBSunShadows.enable") ||
      !EmptyMarker(L"Data\\MirrorsOfSkyrim_PrivateSunShadows.enable")) return;
  try {
    ComPtr<ID3D11Device> device;nativeContext->GetDevice(&device);
    if (!device || device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) return;
    auto** table=*reinterpret_cast<void***>(device.Get());
    // Verify the public native CreatePixelShader entry beneath ENB, before
    // chaining it. Never compile our private copy through ENB's device wrapper.
    HMODULE owner{};wchar_t path[MAX_PATH]{},system[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(table[15]),&owner) || !GetModuleFileNameW(owner,path,MAX_PATH)) return;
    const auto n=GetSystemDirectoryW(system,MAX_PATH);if (!n || n+11>=MAX_PATH) return;
    wcscat_s(system,L"\\d3d11.dll");if (_wcsicmp(path,system)!=0) return;
    g_enbContext=nativeContext;
    g_createPS=reinterpret_cast<CreatePS>(table[15]);
    auto error=DetourTransactionBegin();
    if (error==NO_ERROR) {
      error=DetourUpdateThread(GetCurrentThread());
      if (error==NO_ERROR) error=DetourAttach(reinterpret_cast<PVOID*>(&g_createPS),reinterpret_cast<PVOID>(&CreatePixelShader));
      if (error==NO_ERROR) error=DetourTransactionCommit();else DetourTransactionAbort();
    }
    if (error!=NO_ERROR) {g_createPS=nullptr;g_enbContext.Reset();return;}
    std::thread(RunWorker).detach();
    if (EmptyMarker(L"Data\\MirrorsOfSkyrim_ENBDrawTiming.enable")) {
      LARGE_INTEGER frequency{};
      if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        g_drawTimingFrequency = static_cast<std::uint64_t>(frequency.QuadPart);
        g_drawTimingEnabled.store(true, std::memory_order_release);
        logger::info("[MOS][ENBDrawTiming] CPU-only sample stride=128 frequency={}", g_drawTimingFrequency);
      }
    }
    g_privateGeneration.store(true);
    g_casterDepthShaders.store(MirrorFeatures::Enabled(L"MirrorsOfSkyrim_CasterDepthShaders.enable"));
    g_enbGeneration.store(true,std::memory_order_release);
    g_enabled.store(true,std::memory_order_release);
    logger::info("[MOS][ENBSun] native shader observer enabled; private world/detail maps and per-draw native restore");
  } catch (const std::exception& error) {
    g_enabled.store(false);
    logger::warn("[MOS][ENBSun] setup unavailable: {}",error.what());
  }
}
bool ENBEnabled() noexcept {
  return g_enabled.load(std::memory_order_relaxed) && g_enbGeneration.load(std::memory_order_relaxed);
}
void OnSetupGeometry(const RE::BSRenderPass* pass) noexcept {
  if (!g_enabled.load(std::memory_order_relaxed))
    return;
  if (!Restore())
    return;
  // The exterior auxiliary cycle draws distant LOD into the same reflected
  // view and needs the same sun treatment as the primary cycle.
  g_lease.armed = SecondView::IsInsideMirrorPrimaryCapture() ||
      SecondView::IsInsideMirrorAuxiliaryCapture();
  if (g_lease.armed && g_privateGeneration.load(std::memory_order_relaxed) &&
      g_privateDetailMap.Current(SecondView::CurrentMainWorldSourceSequence())) {
    // This moving detail volume exists for the player's face. Blending it into
    // scenery makes a visible boundary follow the player whenever the fine and
    // coarse caster silhouettes differ. Static scenery uses the wide map only.
    g_lease.playerDetail = PlayerDrawPassProbe::ClassifyExactMirrorPlayerDraw(pass) ==
        PlayerDrawPassProbe::ExactHandDrawClassification::kPlayer;
  }
}
static void Commit(ID3D11DeviceContext *context, bool nativeENB) {
  if (!g_enabled.load(std::memory_order_relaxed) || !g_lease.armed ||
      !context || !(SecondView::IsInsideMirrorPrimaryCapture() ||
                    SecondView::IsInsideMirrorAuxiliaryCapture()) ||
      (!g_privateGeneration.load() && !PeerDetection::CommunityShadersPresent()) ||
      (g_enbGeneration.load() ? (!nativeENB || context!=g_enbContext.Get()) :
                              (nativeENB || PeerDetection::ENBPresent())))
    return;
  ComPtr<ID3D11PixelShader> shader;
  UINT classes = 0;
  context->PSGetShader(&shader, nullptr, &classes);
  if (!shader || classes)
    return;
  if (g_lease.bound) {
    // Every dirty commit may have replaced the shader. Restore the prior
    // private state before starting another lease, even within one pass.
    if (shader.Get() == g_lease.replacement.Get())
      shader = g_lease.original;
    const bool playerDetail = g_lease.playerDetail;
    if (!Restore())
      return;
    g_lease.armed = true;
    g_lease.playerDetail = playerDetail;
    context->PSSetShader(shader.Get(), nullptr, 0);
  }
  const bool communityShaders = PeerDetection::CommunityShadersPresent();
  // CS never consumes a replacement: skip its per-draw private-data lookup.
  auto entry = communityShaders ? ComPtr<Entry>{} : Find(shader.Get());
  // Community Shaders (owner 2026-09-14, run 24): the private maps are not
  // generated, and a rebuilt vanilla-bytecode variant without them rendered the
  // draws it covered (player skin/face/hair) uniformly dark while CS's own
  // programs stayed lit. Every private draw takes the neutral route under CS.
  if (communityShaders) ++g_csVariantSkips;
  if (!entry || !entry->ready.load(std::memory_order_acquire) || communityShaders) {
    ++g_missing;
    // Community Shaders evaluates its own sun cascades (SharedShadowMap, t18)
    // inside every lighting program, fitted to the main camera. In the
    // reflected view that produces hard-edged shadow bands the main view does
    // not show. A program we could not rebuild keeps its code; only the
    // borrowed moments are neutralized for this draw and restored afterwards.
    if (communityShaders && Prepare(context) && g_resources.neutralMoments &&
        g_lease.BindNeutralOnly(context, g_resources.neutralMoments.Get())) {
      ++g_neutralBinds;
      if (g_resources.neutralLightGrid &&
          g_lease.BindClusterGridNeutral(context, g_resources.neutralLightGrid.Get()))
        ++g_clusterGridBinds;
      if (g_resources.neutralScreenSpaceShadow &&
          g_lease.BindScreenSpaceShadowNeutral(context, g_resources.neutralScreenSpaceShadow.Get()))
        ++g_screenSpaceShadowBinds;
      // With the mask route the descriptor keeps bits 13/14, so t14 must always
      // hold a private texture: this capture's mask, or white when it is absent.
      auto* mask = g_maskActive ? MirrorShadowMaskPass::Mask() : nullptr;
      if (!mask) mask = g_resources.neutralMask.Get();
      if (mask && g_lease.BindShadowMask(context, mask))
        ++g_maskBinds;
    }
    // Vanilla: the unpatched program samples the main camera's screen-space
    // shadow mask (t14) at reflected-view coordinates, so its shadows move with
    // the main camera (owner, run 6: contact shadows appearing/disappearing
    // while panning). Owner rule: stable or none. Give this draw a white mask.
    else if (!communityShaders && g_privateGeneration.load() &&
             SecondView::MirrorPrimaryShadowsEnabled() && Prepare(context) &&
             g_resources.neutralMask &&
             g_lease.BindNeutralOnly(context, g_resources.neutralMask.Get(), 14))
      ++g_neutralMaskBinds;
    return;
  }
  ComPtr<ID3D11ShaderResourceView> moments, matrices;
  const bool ownMap = g_privateGeneration.load();
  const bool mapCurrent = ownMap &&
      g_privateMap.Current(SecondView::CurrentMainWorldSourceSequence());
  // Interiors, night, and the frames before the first outdoor generation never
  // publish a private sun map. Use the unshadowed private path instead of
  // treating that as a missing-map search.
  const bool noShadows = !SecondView::MirrorPrimaryShadowsEnabled() ||
      (ownMap && !mapCurrent);
  if (ownMap && !mapCurrent && SecondView::MirrorPrimaryShadowsEnabled())
    ++g_unshadowedBinds;
  if (noShadows) {
    if (!Prepare(context) || !g_resources.noShadows.Prepare(g_resources.device.Get())) {
      ++g_noMaps; return;
    }
    moments=g_resources.noShadows.Depth(); matrices=g_resources.noShadows.Matrices();
  } else if (ownMap) {
    moments = g_privateMap.Depth(); matrices = g_privateMap.Matrices();
  } else {
    context->PSGetShaderResources(18, 1, &moments);
    context->PSGetShaderResources(98, 1, &matrices);
  }
  if ((!noShadows && !ownMap && !ValidMaps(moments.Get(), matrices.Get())) || !Prepare(context)) {
    ++g_noMaps;
    return;
  }
  if (g_lease.Bind(context, entry->variant.Get(), g_resources.constants.Get(),
                   moments.Get(), matrices.Get(), g_resources.sampler.Get(),
                   communityShaders ? g_resources.neutralMoments.Get() : nullptr,
                   entry->usesPrivateSampler, ownMap,
                   noShadows ? moments.Get() : ownMap && g_lease.playerDetail && g_privateDetailMap.Current(SecondView::CurrentMainWorldSourceSequence()) ? g_privateDetailMap.Depth() : nullptr,
                   noShadows ? matrices.Get() : ownMap && g_lease.playerDetail && g_privateDetailMap.Current(SecondView::CurrentMainWorldSourceSequence()) ? g_privateDetailMap.Matrices() : nullptr)) {
    if (!ownMap && !noShadows) CaptureDraw(context, moments.Get(), matrices.Get());
    if (communityShaders && g_resources.neutralLightGrid &&
        g_lease.BindClusterGridNeutral(context, g_resources.neutralLightGrid.Get()))
      ++g_clusterGridBinds;
    if (++g_binds <= 4)
      logger::info("[MOS][SunShadows] private draw corrected: sourceFrame={} "
                   "viewport={}x{} filteredSampler={}",
                   SecondView::CurrentMainWorldSourceSequence(),
                   g_resources.viewport.Width, g_resources.viewport.Height,
                   entry->usesPrivateSampler);
  }
}

bool Restore() noexcept {
  if (!RestoreCasterDraw()) return false;
  if (!g_lease.bound) {
    g_lease = {};
    return true;
  }
  if (!g_lease.MirrorSunShadowD3DState::Restore()) {
    ++g_faults;
    g_enabled.store(false);
    return false;
  }
  g_lease = {};
  ++g_restores;
  return true;
}
void LogDiagnostics() {
  logger::info(
      "[MOS][SunShadows] enabled={} queued/built/rejected={}/{}/{} binds={} "
      "pendingOrUnsupported={} missingMaps={} restores={} faults={} "
      "csNeutralOnlyBinds={} csUnshadowedBinds={} vanillaNeutralMaskBinds={} csClusterGridNeutralBinds={} "
      "csScreenSpaceShadowNeutralBinds={} csVariantLookupsSkipped={} shadowMask(route/renders/rejects/binds/faults)={}/{}/{}/{}/{} receiverBuildsSkipped={}",
      g_enabled.load(), g_queued.load(), g_built.load(), g_rejected.load(),
      g_binds.load(), g_missing.load(), g_noMaps.load(), g_restores.load(),
      g_faults.load(), g_neutralBinds.load(), g_unshadowedBinds.load(),
      g_neutralMaskBinds.load(), g_clusterGridBinds.load(), g_screenSpaceShadowBinds.load(),
      g_csVariantSkips.load(), ShadowMaskRouteEnabled(), g_maskRenders.load(),
      g_maskRejects.load(), g_maskBinds.load(), MirrorShadowMaskPass::Counters().faults, g_receiverBuildsSkipped.load());
  if (g_privateGeneration.load()) logger::info("[MOS][PrivateSun] depthOnlyDraws={} mapReuses={}",
                                              g_depthOnlyDraws.load(),g_mapReuses.load());
  if (g_casterDepthShaders.load()) logger::info("[MOS][CasterDepth] built={} nativeFallbacks={} draws={} restores={}",
      g_casterBuilt.load(),g_casterRejected.load(),g_cutoutDepthDraws.load(),g_cutoutDepthRestores.load());
}
void OnCommit(ID3D11DeviceContext* context) { Commit(context,false); }

bool DrawENB(ID3D11DeviceContext* context, ID3D11RenderTargetView* expected,
             NativeDraw draw, unsigned count, unsigned start, int base, bool caster) {
  if (!ENBEnabled() || context!=g_enbContext.Get() || !expected) return false;
  ID3D11RenderTargetView* actual{};
  bool attempted=false,dispatched=false,completed=false;
  const bool armed=g_lease.armed, playerDetail=g_lease.playerDetail;
  auto timing = BeginDrawSample(caster);
  __try {
    context->OMGetRenderTargets(1,&actual,nullptr);
    if (actual!=expected) __leave;
    attempted=true; // cleanup also owns a partially acquired shader lease
    if (caster) OnCasterCommit(context);else Commit(context,true);
    if (timing.totals) timing.prepared = DrawClock();
    if (!caster && !g_lease.bound) __leave;
    dispatched=true;
    draw(context,count,start,base);
    completed=true;
  } __finally {
    if (timing.totals) {
      timing.submitted = DrawClock();
      if (!timing.prepared) timing.prepared = timing.submitted;
    }
    __try {
      if (attempted) {
        if (!Restore()) {++g_faults;g_enabled.store(false);}
        if (dispatched && !completed) {++g_faults;g_enabled.store(false);}
        // One material pass may issue several native draws. Keep its geometry
        // admission and player-detail classification until the next SetupGeometry.
        g_lease.armed=armed;g_lease.playerDetail=playerDetail;
      }
    } __finally {
      __try {if (actual) actual->Release();}
      __finally {EndDrawSample(timing, completed);}
    }
  }
  return completed;
}
bool PrivateGenerationEnabled() noexcept {
  // Community Shaders (owner 2026-09-14): its lighting programs cannot be
  // rebuilt, so with no mask route the reflection gets neutral moments only and
  // the private maps (two scene passes per capture) are never visible. The mask
  // route consumes them again, through the engine's own screen-space mask.
  return g_enabled.load(std::memory_order_relaxed) && g_privateGeneration.load(std::memory_order_relaxed) &&
         (!PeerDetection::CommunityShadersPresent() || ShadowMaskRouteEnabled()) &&
         (g_enbGeneration.load(std::memory_order_relaxed) || !PeerDetection::ENBPresent());
}
bool ShadowMaskRouteEnabled() noexcept {
  // Vanilla keeps the rebuilt private-shadow variant, which computes the same
  // visibility inside the lighting program; binding the mask there as well
  // would multiply the two. The mask serves the programs that have no variant,
  // which is every one of them while Community Shaders owns the lighting.
  return g_maskRouteEnabled.load(std::memory_order_acquire) &&
         PeerDetection::CommunityShadersPresent();
}
bool ShadowMaskActive() noexcept { return g_maskActive; }
bool PrivateSunMapsCurrent() noexcept {
  const auto source = SecondView::CurrentMainWorldSourceSequence();
  return g_privateMap.Current(source) && g_privateDetailMap.Current(source);
}
void PresentCaptureScreenSize(std::uint32_t width, std::uint32_t height) noexcept {
  // The outer capture owns the restore record; mask attempts can only verify
  // this same extent. An unavailable mask must not restore main-view ratios.
  (void)PatchScreenSizeGlobals(width, height);
}
void EndCaptureScreenSize() noexcept { RestoreScreenSizeGlobals(); }
void EndShadowMask() noexcept {
  g_maskActive = false;
  MirrorShadowMaskPass::Invalidate();
}
bool RenderShadowMask(ID3D11DeviceContext* context, ID3D11ShaderResourceView* captureDepth,
                      std::uint32_t width, std::uint32_t height, std::uint64_t sequence) noexcept {
  EndShadowMask();
  // Only the outer capture restores dimensions. Interiors, night and rejected
  // mask attempts still render water/effects with private raster coordinates.
  if (!ShadowMaskRouteEnabled() || !context || !captureDepth || sequence == 0 ||
      !SecondView::MirrorPrimaryShadowsEnabled() || g_faults.load() != 0)
    return false;
  const auto source = SecondView::CurrentMainWorldSourceSequence();
  // Prepare() snapshots the bound viewport into the projection constants the
  // mask shader reconstructs world positions with, and MirrorShadowMaskPass
  // rasterises at the capture's own size. Those two must agree or every
  // reconstructed position is scaled wrong and the shadows land in the wrong
  // places -- the same failure the mask uv had. What is bound here is
  // whatever the native depth pre-pass render left behind, so impose the
  // capture viewport across both and put the caller's back afterwards.
  std::array<D3D11_VIEWPORT,
             D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
      savedViewports{};
  UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  context->RSGetViewports(&savedViewportCount, savedViewports.data());
  const D3D11_VIEWPORT captureViewport{
      0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
  context->RSSetViewports(1, &captureViewport);
  const auto restoreViewports = [&]() noexcept {
    context->RSSetViewports(savedViewportCount, savedViewports.data());
  };
  if (!g_privateMap.Current(source) || !g_privateDetailMap.Current(source) || !Prepare(context)) {
    restoreViewports();
    ++g_maskRejects;
    return false;
  }
  const MirrorShadowMaskPass::Inputs inputs{
      .captureDepth = captureDepth,
      .shadowDepth = g_privateMap.Depth(),
      .matrices = g_privateMap.Matrices(),
      .detailDepth = g_privateDetailMap.Depth(),
      .detailMatrices = g_privateDetailMap.Matrices(),
      .projection = g_resources.constants.Get(),
      .width = width,
      .height = height};
  const bool rendered = MirrorShadowMaskPass::Render(context, inputs);
  restoreViewports();
  if (!rendered) {
    ++g_maskRejects;
    return false;
  }
  MirrorShadowMaskPass::Publish(sequence);
  g_maskActive = MirrorShadowMaskPass::Current(sequence);
  // The mask is worthless unless the lighting programs sample it at the
  // capture's own resolution; refuse the route rather than draw shadows in
  // the wrong place when the screen-size globals cannot be presented.
  // A capture the neutral cannot cover would read zeros out of bounds in CS's
  // contact-shadow Load and lose its sunlight; refuse the route instead.
  if (g_maskActive && (width > kMaximumNeutralCoveredCapture ||
                       height > kMaximumNeutralCoveredCapture)) {
    g_maskActive = false;
    MirrorShadowMaskPass::Invalidate();
    ++g_maskRejects;
    return false;
  }
  if (g_maskActive && !PatchScreenSizeGlobals(width, height)) {
    g_maskActive = false;
    MirrorShadowMaskPass::Invalidate();
    ++g_maskRejects;
    return false;
  }
  if (g_maskActive) ++g_maskRenders;
  return g_maskActive;
}
bool PrivateMapMatches(const MirrorPrivateShadow::Plan& plan,const MirrorShadowCasterVolume* receiver) noexcept {
  const auto& publication=MirrorPrivateShadow::Detail(plan) ? g_privateDetailMap : g_privateMap;
  const bool matches=PrivateGenerationEnabled() &&
         publication.Matches(SecondView::CurrentMainWorldSourceSequence(), plan, receiver);
  if (matches) ++g_mapReuses;
  return matches;
}
void InvalidatePrivateMap(bool detail) noexcept { (detail ? g_privateDetailMap : g_privateMap).Invalidate(); }
bool PublishPrivateMap(ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth,
                       const MirrorPrivateShadow::Plan& plan,const MirrorShadowCasterSet* coverage) {
  auto& publication=MirrorPrivateShadow::Detail(plan) ? g_privateDetailMap : g_privateMap;
  return PrivateGenerationEnabled() && publication.Publish(
      g_enbGeneration.load() ? g_enbContext.Get() : context,
      depth, SecondView::CurrentMainWorldSourceSequence(), plan, coverage);
}
bool BeginPrivateCamera(const MirrorPrivateShadow::Plan& plan) noexcept {
  if (!PrivateGenerationEnabled() || g_casterCamera.active || g_casterCamera.pending) return false;
  __try {
    auto* owner=RE::BSGraphics::RendererShadowState::GetSingleton();
    if (!owner) return false;
    auto& state=owner->GetRuntimeData();
    auto& c=g_casterCamera;
    static_assert(sizeof(state.cameraData)==sizeof(c.saved));
    static_assert(sizeof(state.posAdjust)==sizeof(c.origin));
    c={}; c.owner=owner;
    std::memcpy(&c.saved,&state.cameraData,sizeof(c.saved));
    std::memcpy(&c.origin,&state.posAdjust,sizeof(c.origin));
    std::memcpy(&c.previous,&state.previousPosAdjust,sizeof(c.previous));
    c.eye={plan.eye.x,plan.eye.y,plan.eye.z};
    c.expected=c.saved;
    auto& view=c.expected;
    view.viewUp={plan.up.x,plan.up.y,plan.up.z,0};
    view.viewRight={plan.right.x,plan.right.y,plan.right.z,0};
    view.viewForward={plan.forward.x,plan.forward.y,plan.forward.z,0};
    const DirectX::XMMATRIX rotation{
      plan.right.x,plan.up.x,plan.forward.x,0,
      plan.right.y,plan.up.y,plan.forward.y,0,
      plan.right.z,plan.up.z,plan.forward.z,0, 0,0,0,1};
    const auto projection=DirectX::XMMatrixOrthographicLH(2*plan.extent,
      2*plan.extent,MirrorPrivateShadow::kNear,MirrorPrivateShadow::kDepth);
    DirectX::XMStoreFloat4x4(&view.viewMat,rotation);
    DirectX::XMStoreFloat4x4(&view.projMat,projection);
    DirectX::XMStoreFloat4x4(&view.viewProjMat,rotation*projection);
    view.viewProjMatrixUnjittered=view.previousViewProjMatrixUnjittered=view.viewProjMat;
    view.projMatrixUnjittered=view.projMat;
    view.viewPort[0]=0;view.viewPort[1]=1;view.viewPort[2]=1;view.viewPort[3]=0;
    view.viewDepthRange={0,1};
    c.active=true;
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool OnCameraUpload() noexcept {
  auto& c=g_casterCamera;
  if (!c.active) return true;
  __try {
    if (RE::BSGraphics::RendererShadowState::GetSingleton()!=c.owner) return false;
    auto& state=c.owner->GetRuntimeData();
    c.pending=true; // restoration ownership precedes the first shared write
    std::memcpy(&state.cameraData,&c.expected,sizeof(c.expected));
    std::memcpy(&state.posAdjust,&c.eye,sizeof(c.eye));
    std::memcpy(&state.previousPosAdjust,&c.eye,sizeof(c.eye));
    if (std::memcmp(&state.cameraData,&c.expected,sizeof(c.expected)) ||
        std::memcmp(&state.posAdjust,&c.eye,sizeof(c.eye))) return false;
    ++c.uploads; return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool PrivateCameraUploaded() noexcept { return g_casterCamera.active && g_casterCamera.uploads>0; }
bool EndPrivateCamera() noexcept {
  const bool shaderRestored=RestoreCasterDraw();
  auto& c=g_casterCamera;
  c.active=false;
  if (!c.pending) {c={};return shaderRestored;}
  __try {
    auto& state=c.owner->GetRuntimeData();
    std::memcpy(&state.cameraData,&c.saved,sizeof(c.saved));
    std::memcpy(&state.posAdjust,&c.origin,sizeof(c.origin));
    std::memcpy(&state.previousPosAdjust,&c.previous,sizeof(c.previous));
    if (std::memcmp(&state.cameraData,&c.saved,sizeof(c.saved)) ||
        std::memcmp(&state.posAdjust,&c.origin,sizeof(c.origin)) ||
        std::memcmp(&state.previousPosAdjust,&c.previous,sizeof(c.previous))) return false;
    c={};return shaderRestored;
  } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool RestoreCasterDraw() noexcept {
  if (!g_casterDraw.Restore()) return false;
  if (g_cutoutDrawPending) { ++g_cutoutDepthRestores; g_cutoutDrawPending=false; }
  return true;
}
void OnCasterCommit(ID3D11DeviceContext* context) {
  if (!g_casterCamera.active || !context ||
      (g_enbGeneration.load() && context!=g_enbContext.Get())) return;
  auto* shadow=RE::BSGraphics::RendererShadowState::GetSingleton();
  if (!shadow || shadow->GetRuntimeData().alphaBlendAlphaToCoverage) return;
  const bool reducedCasters = g_casterDepthShaders.load(std::memory_order_relaxed);
  if (!reducedCasters && shadow->GetRuntimeData().alphaTestEnabled) return;
  ComPtr<ID3D11PixelShader> shader; UINT classes=0;
  context->PSGetShader(&shader,nullptr,&classes);
  if (!shader || classes) return;
  const auto entry=Find(shader.Get());
  if (!entry) return;
  if (reducedCasters && entry->casterReady.load(std::memory_order_acquire)) {
    if (g_casterDraw.Begin(context,entry->casterVariant.Get())) {
      g_cutoutDrawPending=true; ++g_cutoutDepthDraws;
    }
  } else if (!shadow->GetRuntimeData().alphaTestEnabled &&
             entry->opaqueCaster.load(std::memory_order_acquire) && g_casterDraw.Begin(context)) ++g_depthOnlyDraws;
}
} // namespace MirrorSunShadows
