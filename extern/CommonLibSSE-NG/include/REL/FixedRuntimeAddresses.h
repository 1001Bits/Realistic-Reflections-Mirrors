#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace REL::FixedRuntimeAddresses
{
    struct Entry { std::uint64_t id; std::uint64_t offset; };
    using Version = std::array<std::uint16_t, 4>;
    // Exact executable versions only. Empty means unsupported; no disk fallback.
    [[nodiscard]] std::span<const Entry> Select(Version version) noexcept;
    [[nodiscard]] std::uint64_t Find(std::span<const Entry> entries, std::uint64_t id) noexcept;
}
