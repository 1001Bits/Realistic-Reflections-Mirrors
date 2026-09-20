#include "Assets.h"
#include <array>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_STATIC
#pragma warning(disable: 4505) // stb's unused image API; only bounded zlib decode is used.
#include <stb_image.h>

namespace MirrorCK {
namespace {
template<class T> T At(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size()-offset) throw std::runtime_error("Truncated BA2 table.");
    T value{}; std::memcpy(&value, bytes.data()+offset, sizeof(T)); return value;
}
std::vector<std::byte> Range(std::ifstream& file, std::uint64_t total, std::uint64_t offset, std::uint64_t count) {
    if (count > kMaxMeshBytes || offset > total || count > total-offset) throw std::runtime_error("Invalid BA2 file range or oversized mesh.");
    std::vector<std::byte> value(static_cast<std::size_t>(count));
    file.clear(); file.seekg(offset); file.read(reinterpret_cast<char*>(value.data()), value.size());
    if (!file) throw std::runtime_error("Unable to read the asset file."); return value;
}
std::optional<std::vector<std::byte>> FromArchive(const std::filesystem::path& path, const std::string& wanted) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 24) return {};
    const auto total = static_cast<std::uint64_t>(file.tellg());
    auto header = Range(file, total, 0, 24);
    if (std::memcmp(header.data(), "BTDX", 4) || std::memcmp(header.data()+8, "GNRL", 4)) return {};
    auto version = At<std::uint32_t>(header, 4);
    if (version != 1 && version != 7 && version != 8) throw std::runtime_error("Unsupported BA2 version. Extract the model as a loose NIF for authoring.");
    auto count = At<std::uint32_t>(header, 12); auto offset = At<std::uint64_t>(header, 16);
    if (count > 1000000 || offset > total || offset < 24ull + count*36ull) throw std::runtime_error("Invalid BA2 index.");
    auto names = Range(file, total, offset, total-offset);
    std::size_t cursor{}; std::optional<std::uint32_t> found;
    for (unsigned i = 0; i < count; ++i) {
        auto length = At<std::uint16_t>(names, cursor); cursor += 2;
        if (!length || length > 1024 || length > names.size()-cursor) throw std::runtime_error("Invalid BA2 filename.");
        std::string name(reinterpret_cast<const char*>(names.data()+cursor), length); cursor += length;
        // Most entries are unrelated to this mesh; normalization is bounded.
        for (auto& c : name) { if (c == '/') c = '\\'; if (c >= 'A' && c <= 'Z') c += 'a'-'A'; }
        if (name == wanted) { if (found) throw std::runtime_error("Duplicate mesh paths in one BA2. Extract the winning NIF for authoring."); found = i; }
    }
    if (!found) return {};
    auto entry = Range(file, total, 24ull+*found*36ull, 36);
    auto dataOffset = At<std::uint64_t>(entry, 16); auto packed = At<std::uint32_t>(entry, 24); auto unpacked = At<std::uint32_t>(entry, 28);
    if (!unpacked || unpacked > kMaxMeshBytes) throw std::runtime_error("The BA2 mesh exceeds the authoring size limit.");
    auto bytes = Range(file, total, dataOffset, packed ? packed : unpacked);
    if (!packed) return bytes;
    std::vector<std::byte> decoded(unpacked);
    int size = stbi_zlib_decode_buffer(reinterpret_cast<char*>(decoded.data()), static_cast<int>(unpacked), reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
    if (size != static_cast<int>(unpacked)) throw std::runtime_error("The BA2 mesh could not be decompressed.");
    return decoded;
}
}
std::vector<std::byte> ReadFile(const std::filesystem::path& path, std::size_t maximum) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || static_cast<std::uint64_t>(file.tellg()) > maximum) throw std::runtime_error("Missing or oversized asset: " + path.filename().string());
    auto size = static_cast<std::size_t>(file.tellg()); std::vector<std::byte> bytes(size);
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) throw std::runtime_error("Unable to read the asset."); return bytes;
}
std::vector<std::byte> LoadMesh(const std::filesystem::path& data, const std::string& model) {
    auto normalized = NormalizePath(model, "Meshes");
    if (!normalized.ends_with(".nif")) throw std::runtime_error("Select a NIF model first.");
    auto loose = data / "Meshes" / normalized;
    if (std::filesystem::exists(loose)) return ReadFile(loose, kMaxMeshBytes);
    std::optional<std::vector<std::byte>> result; unsigned archives{};
    for (const auto& item : std::filesystem::directory_iterator(data)) {
        auto extension = item.path().extension().wstring(); for (auto& c : extension) c = std::towlower(c);
        if (extension != L".ba2" || !item.is_regular_file()) continue;
        if (++archives > 1024) throw std::runtime_error("Too many archives to inspect. Extract the model as a loose NIF for authoring.");
        auto candidate = FromArchive(item.path(), "meshes\\" + normalized);
        if (!candidate) continue;
        // Never guess mod/archive precedence. The native Model Data materials are
        // checked again by the picker before it can apply the result.
        if (result && *result != *candidate) throw std::runtime_error("Multiple archives contain different versions of this mesh. Extract the version CK uses as a loose NIF for authoring.");
        result = std::move(candidate);
    }
    if (!result) throw std::runtime_error("The model was not found in Data/Meshes or a Fallout 4 BA2 archive.");
    return std::move(*result);
}
}
