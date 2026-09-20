#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace MirrorCK {
inline constexpr char kMarker[] = "MirrorsOfFallout\\Authoring\\MOF_MirrorSurface.bgsm";
inline constexpr wchar_t kMarkerW[] = L"MirrorsOfFallout\\Authoring\\MOF_MirrorSurface.bgsm";
inline constexpr wchar_t kPluginW[] = L"Realistic Reflections - Mirrors.esm";
inline constexpr std::size_t kMaxMeshBytes = 64 * 1024 * 1024;

struct Pane {
    std::uint32_t block{};
    std::string name;
    std::string material;
    std::string reason;
    float width{}, height{};
    bool eligible() const noexcept { return reason.empty(); }
};
struct Model {
    std::vector<Pane> panes;
    std::vector<std::string> materials;
};

// Throws a descriptive exception for malformed/unsupported files. No NIF writes.
Model ReadNif(std::span<const std::byte> bytes);
std::string NormalizePath(std::string value, const char* root = "Materials");
// Embedded material names may retain an export workstation's Data\Materials
// prefix. Resolve the asset name only; never open that workstation path.
std::string NormalizeMaterialPath(std::string value);
std::vector<std::byte> LoadMesh(const std::filesystem::path& data, const std::string& model);
std::vector<std::byte> ReadFile(const std::filesystem::path& file, std::size_t maximum);
}
