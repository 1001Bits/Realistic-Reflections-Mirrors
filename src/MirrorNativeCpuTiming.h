#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <type_traits>

#include "MirrorCaptureProfile.h"

// Opt-in CPU attribution around existing native calls. No engine/D3D mutation,
// resource retention, GPU query, allocation, wait or per-call logging.
namespace MirrorNativeCpuTiming
{
enum class Kind { kWorldShadow, kDetailShadow, kStanding, kHand, kOther };
struct Counter {
    std::atomic_uint64_t calls{}, ticks{}, completed{}, failed{}, invalid{};
};
inline Counter g_worldCull, g_detailCull, g_standingCull, g_handCull, g_otherCull;
inline Counter g_worldRender, g_detailRender, g_standingRender, g_handRender, g_otherRender;
inline std::atomic_bool g_enabled{};
inline std::uint64_t g_frequency{};

inline void Enable(bool requested) noexcept {
    LARGE_INTEGER frequency{};
    if (requested && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        g_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
        g_enabled.store(true, std::memory_order_release);
    }
}
inline Counter& Select(Kind kind, bool render) noexcept {
    switch (kind) {
    case Kind::kWorldShadow: return render ? g_worldRender : g_worldCull;
    case Kind::kDetailShadow: return render ? g_detailRender : g_detailCull;
    case Kind::kStanding: return render ? g_standingRender : g_standingCull;
    case Kind::kHand: return render ? g_handRender : g_handCull;
    default: return render ? g_otherRender : g_otherCull;
    }
}
inline std::uint64_t Clock() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) && value.QuadPart > 0 ?
        static_cast<std::uint64_t>(value.QuadPart) : 0;
}
inline void Finish(Counter& counter, std::uint64_t start, bool returned, bool profiled, bool render) noexcept {
    const auto end = Clock();
    if (!start || end < start) {
        counter.invalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    counter.ticks.fetch_add(end - start, std::memory_order_relaxed);
    (returned ? counter.completed : counter.failed).fetch_add(1, std::memory_order_release);
    // The F8 panel / benchmark breakdown of the standing capture in progress.
    if (profiled)
        MirrorCaptureProfile::AddTicks(render ? MirrorCaptureProfile::Part::kDraw :
            MirrorCaptureProfile::Part::kCull, end - start);
}
template<class Function, class... Args>
inline void Call(Kind kind, bool render, Function function, Args... arguments) {
    static_assert(std::is_pointer_v<Function> &&
                  (std::is_trivially_destructible_v<Args> && ...));
    const bool profiled = kind == Kind::kStanding && MirrorCaptureProfile::Active() &&
        MirrorCaptureProfile::InCapture();
    if (!profiled && !g_enabled.load(std::memory_order_acquire)) {
        function(arguments...);
        return;
    }
    auto& counter = Select(kind, render);
    counter.calls.fetch_add(1, std::memory_order_relaxed);
    const auto start = Clock();
    bool returned = false;
    __try {
        function(arguments...);
        returned = true;
    } __finally {
        Finish(counter, start, returned, profiled, render);
    }
}
}
