#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
namespace MirrorSourceEpoch {
inline std::atomic<std::uint64_t> sequence{1};
[[nodiscard]] inline std::uint64_t CurrentSourceSequence() noexcept {
    const auto value = sequence.load(std::memory_order_acquire);
    return value ? value : 1;
}
[[nodiscard]] inline std::uint64_t AdvanceSourceSequence() noexcept {
    auto value = sequence.load(std::memory_order_relaxed);
    for (;;) {
        const auto next = value == (std::numeric_limits<std::uint64_t>::max)() ? 1 : value + 1;
        if (sequence.compare_exchange_weak(value, next, std::memory_order_acq_rel,
                                          std::memory_order_relaxed)) return next;
    }
}
}
