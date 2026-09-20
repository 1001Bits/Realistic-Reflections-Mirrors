#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace MirrorCK::Native {
// CK 1.11.240.0 only, additionally protected by the whole-executable SHA-256.
// Verified from BGSMaterialSwap::CopyFrom's insertion loop and the called
// string-cache routines; evidence is in the CK helper review's reference/.
struct Functions {
    using Insert = bool(*)(void*, const void*, const void*);
    using CopyRef = void*(*)(void*, const void*);
    using ReleaseRef = void(*)(void*);
    Insert insert{};
    CopyRef copyRef{};
    ReleaseRef releaseRef{};
};
inline Functions Bind() {
    auto base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    auto checked = [&](std::size_t rva, std::initializer_list<unsigned char> expected) -> const void* {
        std::array<unsigned char, 32> bytes{}; SIZE_T read{};
        if (expected.size() > bytes.size() || !ReadProcessMemory(GetCurrentProcess(), base+rva, bytes.data(), expected.size(), &read) ||
            read != expected.size() || std::memcmp(bytes.data(), expected.begin(), expected.size()))
            throw std::runtime_error("A required CK material function is different or patched. This editor session is not supported by the mirror helper.");
        return base+rva;
    };
    return {
        reinterpret_cast<Functions::Insert>(const_cast<void*>(checked(0x442ed0, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57}))),
        reinterpret_cast<Functions::CopyRef>(const_cast<void*>(checked(0x2429790, {0x4c,0x8b,0x02,0x4c,0x8b,0xc9,0x4d,0x85,0xc0,0x74,0x2d,0x41,0x8b,0x40,0x08}))),
        reinterpret_cast<Functions::ReleaseRef>(const_cast<void*>(checked(0x2429890, {0x48,0x83,0xec,0x38,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff})))
    };
}
class Ref {
    const std::byte* entry_{};
    Functions::ReleaseRef release_{};
public:
    Ref(const std::byte* entry, const Functions& api) : release_(api.releaseRef) { api.copyRef(&entry_, &entry); }
    ~Ref() { if (entry_) release_(&entry_); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : entry_(std::exchange(other.entry_, nullptr)), release_(other.release_) {}
    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) { if (entry_) release_(&entry_); entry_ = std::exchange(other.entry_, nullptr); release_ = other.release_; } return *this;
    }
    const std::byte* get() const noexcept { return entry_; }
};
struct Value { const std::byte* replacement; float remap; std::uint32_t padding{}; };
inline bool Insert(const Functions& api, std::byte* swap, const Ref& original, const Ref& replacement, float remap) noexcept {
    if (!swap || !original.get() || !replacement.get()) return false;
    auto key = original.get(); Value value{replacement.get(), remap};
    __try { return api.insert(swap+0x30, &key, &value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
