#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace MirrorCK {
inline std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!n) n = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring value(n, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), n))
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), value.data(), n);
    return value;
}
inline std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string value(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), n, nullptr, nullptr); return value;
}
inline std::wstring Text(HWND window) {
    int n = GetWindowTextLengthW(window); if (n < 0 || n > 32767) return {};
    std::wstring value(n+1, L'\0'); n = GetWindowTextW(window, value.data(), static_cast<int>(value.size())); value.resize(n); return value;
}
inline std::filesystem::path ModulePath(HMODULE module = nullptr) {
    std::wstring value(32768, L'\0'); auto n = GetModuleFileNameW(module, value.data(), static_cast<DWORD>(value.size()));
    if (!n || n == value.size()) return {}; value.resize(n); return value;
}
inline bool SupportedEditor(const std::filesystem::path& path) {
    // UI/resource and read-only model/MSWP layouts are verified for this editor.
    // Never attach these assumptions to Skyrim, a game, or a different CK build.
    constexpr char expected[] = "90fde1624fec6c66cf74652a21480ed32c49ebcd4cb231e9c4804d974a3fcc08";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{}; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        std::array<UCHAR, 65536> block{}; DWORD count{}; bool readOK = true;
        while (::ReadFile(file, block.data(), static_cast<DWORD>(block.size()), &count, nullptr)) {
            if (!count) { std::array<UCHAR, 32> digest{}; if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
                std::string hex; const char* digits = "0123456789abcdef";
                for (auto c : digest) { hex += digits[c>>4]; hex += digits[c&15]; } ok = hex == expected;
            } break; }
            if (BCryptHashData(hash, block.data(), count, 0) < 0) { readOK = false; break; }
        }
        ok &= readOK;
    }
    if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); CloseHandle(file); return ok;
}
}
