#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <limits>

namespace MirrorSettingsFile
{
// Write beside the destination and replace only after the whole file is durable.
// Disk exhaustion, denied replacement, or partial writes preserve the old INI.
[[nodiscard]] inline bool Write(const std::filesystem::path& destination,
    const std::vector<std::string>& lines) noexcept
{
    struct Temporary {
        wchar_t path[MAX_PATH]{};
        HANDLE handle{INVALID_HANDLE_VALUE};
        ~Temporary() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); if (path[0]) DeleteFileW(path); }
    } temporary;
    try {
        std::string bytes;
        for (const auto& line : lines) { bytes += line; bytes += '\n'; }
        if (bytes.size() > (std::numeric_limits<DWORD>::max)()) return false;
        const auto target = std::filesystem::absolute(destination);
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error || !GetTempFileNameW(target.parent_path().c_str(), L"MOS", 0, temporary.path)) return false;
        temporary.handle = CreateFileW(temporary.path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (temporary.handle == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        if (!WriteFile(temporary.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(temporary.handle)) return false;
        const bool closed = CloseHandle(temporary.handle) != FALSE;
        temporary.handle = INVALID_HANDLE_VALUE;
        if (!closed || !MoveFileExW(temporary.path, target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
        temporary.path[0] = 0;
        return true;
    } catch (...) { return false; }
}
}
