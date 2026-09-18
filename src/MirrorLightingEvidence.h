#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Internal, value-only diagnostic ABI for an unsuspended PROCESS_VM_READ reader.
// No engine pointer in this record owns or extends the lifetime of an object.
namespace MirrorLightingEvidence
{
    inline constexpr std::size_t kCapacity = 1024;
    inline constexpr std::uint32_t kPerViewBudget = 192;

    struct Light
    {
        std::uint64_t wrapper{}, object{};
        float position[3]{}, radius[3]{}, diffuse[3]{};
        float fade{}, lodDimmer{};
        std::uint32_t frustumCull{}, appCulled{}, valid{};
    };
    static_assert(sizeof(Light) == 72);

    struct Sample
    {
        std::uint64_t source{}, passSequence{}, milliseconds{}, target{};
        std::uint64_t geometry{}, property{}, propertyFlags{};
        std::uint32_t kind{}, technique{}, lightCount{}, shadowCount{};
        std::uint32_t appCulled{}, ancestorCulled{}, activeLightMask{}, lightListFence{};
        float camera[3]{}, bound[4]{}, position[3]{}, scale{}, alpha{};
        char name[64]{};
        std::uint64_t siblingPane{};
        std::uint32_t siblingPaneCulled{}, siblingParentCulled{};
        float siblingPaneAlpha{}, siblingPaneRadius{};
        float ambient[13]{}; // NiTransform: rotation, translation, scale
        Light lights[16]{};
        // reserved bit 0: exact private-target player ancestry; bit 1: skinned.
        // Zero preserves the ordinary scene sample interpretation and ABI.
        std::uint32_t valid{}, reserved{};
    };
    static_assert(std::is_trivially_copyable_v<Sample>);
    static_assert(sizeof(Sample) == 1440);

    struct Slot
    {
        std::atomic<std::uint64_t> stamp{};
        Sample sample{};
    };
    static_assert(offsetof(Slot, sample) == 8 && sizeof(Slot) == 1448);

    // Render-thread-only writer. The external reader reads each slot twice and
    // accepts identical bytes with a nonzero even stamp; this is not a C++ API
    // for concurrent unsynchronised readers of sample.
    struct Ring
    {
        std::array<Slot, kCapacity> slots{};
        std::uint64_t next{};
        void Publish(const Sample& sample) noexcept
        {
            const auto serial = ++next;
            auto& slot = slots[(serial - 1) % kCapacity];
            slot.stamp.exchange(serial * 2 - 1, std::memory_order_acq_rel);
            slot.sample = sample;
            slot.stamp.store(serial * 2, std::memory_order_release);
        }
    };
}
