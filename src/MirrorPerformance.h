#pragma once
#include <atomic>
#include <cstdint>
struct IDXGISwapChain;

namespace MirrorPerformance
{
// Changes only after the outer Present returns, never during a capture/lease.
inline std::atomic_bool rendering{true};
inline bool RenderingEnabled() noexcept { return rendering.load(std::memory_order_acquire); }
// Owner, 2026-09-16: the F8/F11 hotkeys are development aids and must never
// collide with Community Shaders / ENB keys. Off unless the MCM switch is on;
// with it off the input sink passes every key through untouched.
// Owner, 2026-09-18: the F8/F11 overlay is not a compiled default.
inline std::atomic_bool debugHotkeys{false};
inline bool DebugHotkeysEnabled() noexcept { return debugHotkeys.load(std::memory_order_acquire); }
bool Requested() noexcept;
// Detailed timestamps are collected only while the owner opens the panel.
bool DetailedTimingEnabled() noexcept;
void OnInputLoaded();
void BeforePresent(IDXGISwapChain* chain) noexcept;
void AfterPresent(bool successful, std::uint64_t now) noexcept;
}
