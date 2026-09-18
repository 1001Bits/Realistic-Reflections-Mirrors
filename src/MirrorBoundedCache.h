#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

// Render-thread value cache. One lazy allocation, four bounded probes, and one
// victim per insertion: reaching capacity must never destroy/rebuild the entire
// working set on a capture frame. Keys and values confer no engine ownership.
template <class Value, std::size_t Capacity = 16384, std::size_t Ways = 4>
class MirrorBoundedCache
{
    static_assert(Ways > 0 && Capacity >= Ways && Capacity % Ways == 0);
    static constexpr std::size_t kSets = Capacity / Ways;
    static_assert((kSets & (kSets - 1)) == 0);
    struct Slot
    {
        std::uintptr_t key{};
        std::uint64_t epoch{};
        Value value{};
    };
    struct Set
    {
        std::array<Slot, Ways> slots{};
        std::size_t next{};
    };
public:
    [[nodiscard]] const Value* Find(std::uintptr_t key) const noexcept
    {
        if (!sets_) return nullptr;
        for (const auto& slot : sets_[Index(key)].slots)
            if (slot.epoch == epoch_ && slot.key == key) return &slot.value;
        return nullptr;
    }
    bool Store(std::uintptr_t key, const Value& value) noexcept
    {
        // A failed allocation disables this optional cache for its lifetime;
        // every miss still takes the complete light-selection path.
        if (!attempted_) {
            attempted_ = true;
            sets_.reset(new (std::nothrow) Set[kSets]);
        }
        if (!sets_) return false;
        auto& set = sets_[Index(key)];
        Slot* destination = nullptr;
        for (auto& slot : set.slots) {
            if (slot.epoch == epoch_ && slot.key == key) { destination = &slot; break; }
            if (!destination && slot.epoch != epoch_) destination = &slot;
        }
        if (!destination) {
            destination = &set.slots[set.next];
            set.next = (set.next + 1) % Ways;
            ++evictions_;
        }
        *destination = Slot{ key, epoch_, value };
        return true;
    }
    void Clear() noexcept
    {
        // Scene changes invalidate values without freeing thousands of nodes.
        if (++epoch_ == 0) {
            if (sets_) for (std::size_t i = 0; i < kSets; ++i)
                for (auto& slot : sets_[i].slots) slot.epoch = 0;
            epoch_ = 1;
        }
    }
    [[nodiscard]] std::uint64_t Evictions() const noexcept { return evictions_; }
    [[nodiscard]] bool Allocated() const noexcept { return sets_ != nullptr; }
private:
    static std::size_t Index(std::uintptr_t key) noexcept
    {
        // Geometry addresses are aligned; mix high bits before the set mask.
        auto hash = static_cast<std::uint64_t>(key);
        hash ^= hash >> 30; hash *= 0xbf58476d1ce4e5b9ULL;
        hash ^= hash >> 27; hash *= 0x94d049bb133111ebULL;
        hash ^= hash >> 31;
        return static_cast<std::size_t>(hash) & (kSets - 1);
    }
    std::unique_ptr<Set[]> sets_;
    std::uint64_t epoch_{ 1 }, evictions_{};
    bool attempted_{};
};
