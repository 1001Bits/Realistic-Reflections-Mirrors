#include "PCH.h"
#include "MirrorENBScreenMask.h"
#include "MirrorENBScreenMaskD3D.h"
#include "HandMirrorSafetySettings.h"
#include "PeerDetection.h"
#include "SecondView.h"
#include "MirrorSunShadows.h"
#include "SupportedRuntimePolicy.h"
#include <d3d11_1.h>
#include <wrl/client.h>

namespace MirrorENBScreenMask
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11DeviceContext1> g_context;
        ComPtr<ID3D11ShaderResourceView> g_neutral;
        MirrorENBScreenMaskD3D::DrawIndexed g_original{};
        std::atomic_bool g_enabled{};
        std::atomic_uint64_t g_draws{}, g_restores{}, g_targetMisses{}, g_faults{};

        void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* context, UINT count, UINT start, INT base)
        {
            // No COM calls, allocations or counters on ordinary game draws.
            if (context != g_context.Get() || !g_enabled.load(std::memory_order_relaxed)) {
                g_original(context, count, start, base);
                return;
            }
            if (MirrorSunShadows::ENBEnabled()) {
                if (auto* caster=SecondView::MirrorPrivateShadowColorTarget(); caster &&
                    MirrorSunShadows::DrawENB(context,caster,g_original,count,start,base,true)) return;
            }
            auto* expected = SecondView::MirrorPrimaryColorTarget(MirrorENBScreenMaskD3D::kExtent);
            if (!expected) {
                g_original(context, count, start, base);
                return;
            }
            if (MirrorSunShadows::DrawENB(context,expected,g_original,count,start,base,false)) return;
            MirrorENBScreenMaskD3D::Result result{};
            __try {
                MirrorENBScreenMaskD3D::Draw(context, expected, g_neutral.Get(), g_original,
                    count, start, base, result);
            } __finally {
                if (result.maskBound) ++g_draws;
                if (result.restored) ++g_restores;
                if (!result.targetMatched) ++g_targetMisses;
                if (!result.drawCompleted || (result.maskBound && !result.restored)) {
                    ++g_faults;
                    g_enabled.store(false, std::memory_order_release);
                }
            }
        }

        bool IsSystemD3D(const void* address) noexcept
        {
            HMODULE module{};
            wchar_t modulePath[MAX_PATH]{}, systemPath[MAX_PATH]{};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &module) ||
                !GetModuleFileNameW(module, modulePath, MAX_PATH)) return false;
            const auto length = GetSystemDirectoryW(systemPath, MAX_PATH);
            if (!length || length + 11 >= MAX_PATH) return false;
            wcscat_s(systemPath, L"\\d3d11.dll");
            return _wcsicmp(modulePath, systemPath) == 0;
        }

        bool IsENBWrapper(ID3D11DeviceContext* context) noexcept
        {
            HMODULE module{};
            auto** table = *reinterpret_cast<void***>(context);
            return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(table[12]), &module) &&
                GetProcAddress(module, "ENBGetSDKVersion") != nullptr;
        }
    }

    void OnInputLoaded() noexcept
    {
        if (g_enabled.load() || !HandMirrorSafetySettings::EmptyMarker(
                L"Data\\MirrorsOfSkyrim_ENBScreenMask.enable")) return;
        // The correction uses public D3D11 interfaces, not SE engine offsets.
        // AE uses the same ENB screen-coordinate mask and square mirror targets;
        // verify its actual wrapper/native context below just as on SE.
        const auto version = REL::Module::get().version();
        const bool supportedFlat =
            (REL::Module::IsSE() && version == REL::Version{1, 5, 97, 0}) ||
            (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version));
        if (!supportedFlat ||
            !PeerDetection::ENBPresent() || PeerDetection::CommunityShadersPresent() ||
            !SecondView::HooksReady()) return;
        try {
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            auto* wrapped = renderer ? reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context) : nullptr;
            // A helper DLL alone is not evidence of ENB's renderer or t37 contract.
            if (!wrapped || !IsENBWrapper(wrapped) || FAILED(wrapped->QueryInterface(IID_PPV_ARGS(&g_context)))) {
                logger::warn("[MOS][ENBMask] requested but the ENB wrapper/native context is unavailable");
                return;
            }
            ComPtr<ID3D11Device> device;
            g_context->GetDevice(&device);
            auto** table = *reinterpret_cast<void***>(g_context.Get());
            // ID3D11DeviceContext::DrawIndexed is public COM slot 12, not an engine RVA.
            if (!IsSystemD3D(table[12]) || FAILED(MirrorENBScreenMaskD3D::CreateNeutral(device.Get(), &g_neutral))) {
                g_context.Reset();
                return;
            }
            g_original = reinterpret_cast<MirrorENBScreenMaskD3D::DrawIndexed>(table[12]);
            auto error = DetourTransactionBegin();
            if (error == NO_ERROR) {
                error = DetourUpdateThread(GetCurrentThread());
                if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&g_original), reinterpret_cast<PVOID>(&Draw));
                if (error == NO_ERROR) error = DetourTransactionCommit();
                else DetourTransactionAbort();
            }
            if (error != NO_ERROR) {
                g_neutral.Reset(); g_context.Reset(); g_original = nullptr;
                logger::warn("[MOS][ENBMask] native hook unavailable error={}", error);
                return;
            }
            g_enabled.store(true, std::memory_order_release);
            logger::info("[MOS][ENBMask] enabled: native t37 neutral mask 4096x4096 (64 MiB), exact primary mirror targets, per-draw restore");
            MirrorSunShadows::EnableENB(g_context.Get());
        } catch (const std::exception& error) {
            g_neutral.Reset(); g_context.Reset();
            logger::warn("[MOS][ENBMask] setup failed: {}", error.what());
        }
    }

    bool Ready() noexcept { return g_enabled.load(std::memory_order_acquire); }

    void LogDiagnostics(const char* reason) noexcept
    {
        if (!g_original) return;
        logger::info("[MOS][ENBMask] {} enabled={} draws={} restores={} targetMisses={} faults={}",
            reason, g_enabled.load(), g_draws.load(), g_restores.load(), g_targetMisses.load(), g_faults.load());
    }
}
