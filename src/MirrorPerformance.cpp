#include "PCH.h"
#include "MirrorPerformance.h"
#include "MirrorPerformancePolicy.h"
#include "MirrorPerformanceOverlay.h"
#include "MirrorFleetRuntime.h"
#include "SupportedRuntimePolicy.h"
#include "MirrorBenchmark.h"
#include "MirrorCaptureProfile.h"
#include "MirrorCaptureOptimizations.h"
#include "SecondView.h"
#include "MirrorScreenSizePolicy.h"
#include <cstdio>

namespace MirrorPerformance
{
namespace
{
Hotkeys keys;
std::atomic<HWND> gameWindow{};
std::atomic_bool inputRegistered{};
std::atomic_bool detailedTiming{};
std::atomic_bool developmentPanelOpen{};
bool visible{}, eligible{}, focused{}, currentMode{true};
bool overlayFailureLogged{}, controlsLogged{};
Meter meter;
MirrorOverlay::Renderer overlay;
std::uint64_t nextLog{};

bool WindowFocused(HWND window) noexcept
{
    return window && GetForegroundWindow()==window && !IsIconic(window);
}
class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
        RE::BSTEventSource<RE::InputEvent*>*) override
    {
        if(!events) return RE::BSEventNotifyControl::kContinue;
        const bool active=WindowFocused(gameWindow.load(std::memory_order_acquire));
        if(!active) {keys.Clear();return RE::BSEventNotifyControl::kContinue;}
        auto* manager=RE::BSInputDeviceManager::GetSingleton();
        auto* keyboard=manager?manager->GetKeyboard():nullptr;
        using Key=RE::BSKeyboardDevice::Key;
        // Use the vendored runtime accessor and the same DirectInput high-bit
        // test as IsPressed. Calling that out-of-line helper pulls in its
        // unimplemented device constructor/vtable from CommonLib's archive.
        const auto pressed=[keyboard](std::uint32_t key) {
            if(!keyboard) return false;
            const auto& state=keyboard->GetRuntimeData().curState;
            return key<sizeof(state) && (state[key]&0x80)!=0;
        };
        const bool modified=pressed(Key::kLeftAlt) || pressed(Key::kRightAlt) ||
            pressed(Key::kLeftControl) || pressed(Key::kRightControl) ||
            pressed(Key::kLeftShift) || pressed(Key::kRightShift);
        if(!DebugHotkeysEnabled()) return RE::BSEventNotifyControl::kContinue;
        for(auto* event=*events;event;event=event->next) {
            auto* button=event->AsButtonEvent();
            if(!button || !keys.Button(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard,
                button->GetIDCode(),button->IsDown(),active,modified,
                developmentPanelOpen.load(std::memory_order_acquire))) continue;
            // Only neutralize our key; preserve the native batch and all
            // movement, blocking, placement and menu releases for other sinks.
            button->SetUserEvent(RE::BSFixedString{});
            button->SetIDCode(RE::ControlMap::kInvalid);
            button->GetRuntimeData().value=0.0F;
            button->GetRuntimeData().heldDownSecs=0.0F;
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
InputSink inputSink;
static_assert(Hotkeys::kMenuScanCode==RE::BSKeyboardDevice::Key::kF8);
static_assert(Hotkeys::kRenderingScanCode==RE::BSKeyboardDevice::Key::kF11);
static_assert(Hotkeys::kCutsScanCode==RE::BSKeyboardDevice::Key::kF7);
static_assert(Hotkeys::kSharpnessScanCode==RE::BSKeyboardDevice::Key::kF6);
static_assert(Hotkeys::kBenchmarkScanCode==RE::BSKeyboardDevice::Key::kF4);
// F7: every capture cut off, then back to what it was (side-by-side checks).
struct Cuts { bool tight{}, lights{}, roster{}, screen{}, rectangle{}, listReuse{}, lightChoice{}, roomCull{}; };
Cuts savedCuts{};
bool cutsOff{};
Cuts CurrentCuts() noexcept
{
    using namespace MirrorCaptureOptimizations;
    return { tightPaneCull.load(), lightCandidateCache.load(), replayRosterFilter.load(), screenSizedCapture.load(),
        rectangularCapture.load(), rosterListReuse.load(), lightChoiceCache.load(), roomCull.load() };
}
void SetCuts(const Cuts& cuts) noexcept
{
    using namespace MirrorCaptureOptimizations;
    tightPaneCull.store(cuts.tight);
    lightCandidateCache.store(cuts.lights);
    replayRosterFilter.store(cuts.roster);
    screenSizedCapture.store(cuts.screen);
    rectangularCapture.store(cuts.rectangle);
    rosterListReuse.store(cuts.listReuse);
    lightChoiceCache.store(cuts.lightChoice);
    roomCull.store(cuts.roomCull);
}
void ToggleCuts() noexcept
{
    if (!cutsOff) {
        savedCuts = CurrentCuts();
        SetCuts({});
    } else {
        SetCuts(savedCuts);
    }
    cutsOff = !cutsOff;
}
bool WorldActive() noexcept
{
    __try {
        auto* ui=RE::UI::GetSingleton();auto* player=RE::PlayerCharacter::GetSingleton();
        return ui && player && player->GetParentCell() && player->Is3DLoaded() && !ui->GameIsPaused() &&
            !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) && !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) &&
            !ui->IsMenuOpen(RE::Console::MENU_NAME) && !ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Draw(IDXGISwapChain* chain)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> target;
    if(FAILED(chain->GetDevice(IID_PPV_ARGS(&device))) || FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) return;
    device->GetImmediateContext(&context);
    if(!context || FAILED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target))) return;
    D3D11_TEXTURE2D_DESC description{};buffer->GetDesc(&description);
    std::array<std::array<char,112>,24> text{};
    std::size_t count=0;
    const auto nowUs=MirrorFleetRuntime::Now();
    if(MirrorBenchmark::Showing()) {
        count=MirrorBenchmark::Lines(text,nowUs);
    } else {
    const auto live=meter.Live(),on=meter.Mode(true),off=meter.Mode(false);
    std::snprintf(text[0].data(),text[0].size(),"MIRRORS OF SKYRIM - PERFORMANCE");
    if(eligible && live.frames)
        std::snprintf(text[1].data(),text[1].size(),"FPS: %.1f  |  %.2f ms/frame",live.FPS(),live.Milliseconds());
    else std::snprintf(text[1].data(),text[1].size(),"%s",eligible?"Measuring...":"Paused / loading - samples excluded");
    std::snprintf(text[2].data(),text[2].size(),"Mirror rendering: %s",currentMode?"ON":"OFF");
    for(unsigned i=0;i<2;++i) {
        const auto average=meter.Mode(i==0);
        if(average.frames) std::snprintf(text[3+i].data(),text[3+i].size(),"%s: %6.1f FPS | %6.2f ms | P95 %6.2f | P99 %6.2f (%4.1fs)",
            i==0?" ON":"OFF",average.FPS(),average.Milliseconds(),meter.PercentileMs(i==0,.95),meter.PercentileMs(i==0,.99),average.Seconds());
        else std::snprintf(text[3+i].data(),text[3+i].size(),"%s average: waiting for settled frames",i==0?" ON":"OFF");
    }
    if(on.Seconds()>=2 && off.Seconds()>=2)
        std::snprintf(text[5].data(),text[5].size(),"ON minus OFF: %+.2f ms/frame  |  %+.1f FPS",on.Milliseconds()-off.Milliseconds(),on.FPS()-off.FPS());
    else std::snprintf(text[5].data(),text[5].size(),"Measure both modes for at least 2 seconds");
    // Where one standing capture spends its time (owner, 2026-09-17).
    const auto capture=MirrorCaptureProfile::Read(nowUs);
    using Part=MirrorCaptureProfile::Part;
    const auto part=[&capture](Part p) { return capture.partMs[static_cast<std::size_t>(p)]; };
    if(capture.captures) {
        std::snprintf(text[6].data(),text[6].size(),"Capture %dx%d: %.1f/s | CPU %.2f ms (P95 %.2f) | GPU %.2f ms",
            SecondView::LastPlacedCaptureResolution(),SecondView::LastPlacedCaptureHeight(),
            capture.seconds>0?double(capture.captures)/capture.seconds:0.0,capture.captureMs,capture.captureP95Ms,capture.gpuMs);
        std::snprintf(text[7].data(),text[7].size(),"  cull %.2f | engine draw %.2f | mod per-draw %.2f (lights %.2f)",
            part(Part::kCull),(std::max)(0.0,part(Part::kDraw)-part(Part::kHooks)),part(Part::kHooks),part(Part::kLights));
        std::snprintf(text[8].data(),text[8].size(),"  depth pass %.2f | object list %.2f | setup, mips and handover %.2f",
            part(Part::kDepth),part(Part::kRoster),capture.otherMs);
    } else {
        std::snprintf(text[6].data(),text[6].size(),"Capture: none yet (face a mirror with mirrors ON)");
    }
    std::snprintf(text[9].data(),text[9].size(),"F8 menu  |  F11 mirrors ON/OFF  |  F7 cuts %s  |  F6 sharp %s",cutsOff?"OFF":"ON",
        MirrorScreenSizePolicy::reducedSupersample.load()?"1.25x":"1.5x");
    std::snprintf(text[10].data(),text[10].size(),"Keep the same view; opening menu resets averages.");
    std::snprintf(text[11].data(),text[11].size(),"VSync / FPS caps can hide the rendering cost.");
    std::snprintf(text[12].data(),text[12].size(),"F4 Run benchmark - stand still facing a mirror.");
    count=13;
    }
    std::array<std::string_view,24> lines{};
    for(unsigned i=0;i<count;++i) lines[i]=text[i].data();
    if(!overlay.Draw(context.Get(),target.Get(),description.Width,description.Height,std::span<const std::string_view>(lines.data(),count)) && !overlayFailureLogged) {
        overlayFailureLogged=true;logger::warn("[MOS][Performance] overlay unavailable; FPS logging and F11 remain active");
    }
    // All swap-chain buffer references die here, before the chained Present.
}
}

bool RuntimeCapable() noexcept
{
    return (REL::Module::IsSE() && REL::Module::get().version()==REL::Version{1,5,97,0}) ||
        (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(REL::Module::get().version()));
}

bool Requested() noexcept
{
    // Owner 2026-09-18: F8/F11 are the development menu, off unless MCM
    // Development menu is on. A leftover Data enable file must not arm them.
    return RuntimeCapable() && DebugHotkeysEnabled();
}

bool DetailedTimingEnabled() noexcept
{
    return detailedTiming.load(std::memory_order_acquire);
}

void OnInputLoaded()
{
    if(!RuntimeCapable()) return;
    MirrorCaptureProfile::SetGpuSource(&MirrorFleetRuntime::StandingGpu);
    if(auto* manager=RE::BSInputDeviceManager::GetSingleton();manager && !inputRegistered.exchange(true)) {
        manager->PrependEventSink(&inputSink);
        logger::info("[MOS][Performance] native Skyrim keyboard input registered: F8 scan=0x42, F11 scan=0x57, F7 scan=0x41, F6 scan=0x40");
    }
}

void BeforePresent(IDXGISwapChain* chain) noexcept
{
    if(!Requested() || !chain) return;
    try {
        DXGI_SWAP_CHAIN_DESC desc{};
        const HWND window=SUCCEEDED(chain->GetDesc(&desc))?desc.OutputWindow:nullptr;
        gameWindow.store(window,std::memory_order_release);
        focused=WindowFocused(window);
        if(!focused) keys.Clear();
        if(window && !controlsLogged) {
            controlsLogged=true;
            logger::info("[MOS][Performance] ready: F8 performance panel, F11 mirror rendering, F7 capture cuts, F6 capture sharpness; nativeInput={} debugHotkeys={} (application Present cadence)",inputRegistered.load(),DebugHotkeysEnabled());
        }
        eligible=focused && WorldActive();currentMode=RenderingEnabled();
        if(visible && focused) Draw(chain);
    } catch(...) {eligible=false;}
}

void AfterPresent(bool successful,std::uint64_t now) noexcept
{
    if(!Requested()) return;
    focused=WindowFocused(gameWindow.load(std::memory_order_acquire));
    meter.Observe(now,successful && eligible && focused);
    MirrorBenchmark::OnPresent(now,successful && eligible && focused);
    if(MirrorBenchmark::Showing() && !visible) {visible=true;}
    auto actions=keys.Take();
    if(!DebugHotkeysEnabled()) {
        actions=0;
        if(visible) {visible=false;MirrorBenchmark::Dismiss();logger::info("[MOS][Performance] F8 panel closed: debug hotkeys switched off");}
    }
    const bool menu=(actions&Hotkeys::kMenu)!=0,toggle=(actions&Hotkeys::kRendering)!=0,cuts=(actions&Hotkeys::kCuts)!=0,
        sharpness=(actions&Hotkeys::kSharpness)!=0;
    try {
        if(focused && successful && visible && eligible &&
            (actions&Hotkeys::kBenchmark)!=0 && !MirrorBenchmark::Running()) {
            MirrorBenchmark::Request();
            logger::info("[MOS][Benchmark] requested from the F8 development panel");
        }
        if(focused && successful && menu) {
            visible=!visible;
            if(visible) {meter=Meter{};meter.Switch(RenderingEnabled(),now);MirrorCaptureProfile::Reset(now);}
            else MirrorBenchmark::Dismiss();
            logger::info("[MOS][Performance] F8 panel={}",visible?"OPEN":"CLOSED");
        }
        if(focused && successful && toggle) {
            // F11 during a benchmark stops it: the benchmark owns the switch.
            if(MirrorBenchmark::Running()) MirrorBenchmark::Abort("F11 pressed");
            else {
            if(!visible) {visible=true;meter=Meter{};}
            MirrorBenchmark::Dismiss();
            currentMode=!RenderingEnabled();rendering.store(currentMode,std::memory_order_release);
            meter.Switch(currentMode,now);
            MirrorCaptureProfile::Reset(now);
            logger::info("[MOS][Performance] F11 mirror rendering={}; captures/shadows/pane delivery switched at Present boundary",currentMode?"ON":"OFF");
            }
        }
        if(focused && successful && cuts) {
            // The benchmark owns the cut switches while it runs.
            if(MirrorBenchmark::Running()) MirrorBenchmark::Abort("F7 pressed");
            ToggleCuts();
            MirrorCaptureProfile::Reset(now);
            if(visible) {meter=Meter{};meter.Switch(RenderingEnabled(),now);}
            logger::info("[MOS][Performance] F7 capture cuts={} (pane cull, light caches, roster filter, screen size and shape, list reuse, room cull)",cutsOff?"OFF":"ON");
        }
        if(focused && successful && sharpness) {
            // The benchmark owns the sharpness while it runs.
            if(MirrorBenchmark::Running()) MirrorBenchmark::Abort("F6 pressed");
            const bool lighter=!MirrorScreenSizePolicy::reducedSupersample.load();
            MirrorScreenSizePolicy::reducedSupersample.store(lighter);
            MirrorCaptureProfile::Reset(now);
            if(visible) {meter=Meter{};meter.Switch(RenderingEnabled(),now);}
            logger::info("[MOS][Performance] F6 capture sharpness={}x",lighter?"1.25":"1.5");
        }
        developmentPanelOpen.store(visible, std::memory_order_release);
        detailedTiming.store(visible, std::memory_order_release);
        MirrorCaptureProfile::active.store(visible || MirrorBenchmark::Running(), std::memory_order_relaxed);
        if(visible && now>=nextLog && meter.Valid()) {
            nextLog=now+5'000'000;
            const auto on=meter.Mode(true),off=meter.Mode(false);
            logger::info("[MOS][Performance] rendering={} onFrames={} onSeconds={:.2f} onFPS={:.2f} onMs={:.3f} offFrames={} offSeconds={:.2f} offFPS={:.2f} offMs={:.3f} (application Present cadence)",
                RenderingEnabled(),on.frames,on.Seconds(),on.FPS(),on.Milliseconds(),off.frames,off.Seconds(),off.FPS(),off.Milliseconds());
        }
    } catch(...) {}
}
}
