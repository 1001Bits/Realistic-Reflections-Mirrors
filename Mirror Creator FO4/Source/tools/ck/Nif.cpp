#include "Assets.h"
#include "MirrorAuthoringGeometry.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string_view>

namespace MirrorCK {
namespace {
class Reader {
public:
    std::span<const std::byte> bytes;
    std::size_t position{};
    explicit Reader(std::span<const std::byte> value) : bytes(value) {}
    std::span<const std::byte> take(std::size_t n) {
        if (n > bytes.size() - position) throw std::runtime_error("The NIF is truncated.");
        auto result = bytes.subspan(position, n); position += n; return result;
    }
    template<class T> T get() { T value{}; auto raw = take(sizeof(T)); std::memcpy(&value, raw.data(), sizeof(T)); return value; }
    std::string string(std::size_t n) {
        if (n > 4096) throw std::runtime_error("The NIF contains an oversized string.");
        auto raw = take(n); return {reinterpret_cast<const char*>(raw.data()), raw.size()};
    }
    std::string sized() { return string(get<std::uint32_t>()); }
    std::uint32_t count(std::uint32_t max = 65535) {
        auto n = get<std::uint32_t>(); if (n > max) throw std::runtime_error("The NIF exceeds the supported block/count limit."); return n;
    }
};
struct Block { std::string type; std::span<const std::byte> bytes; };
struct Object { std::string name; bool animated{}, hidden{}, singular{}; std::vector<std::uint32_t> children; };
std::string Name(Reader& r, const std::vector<std::string>& strings) {
    auto index = r.get<std::uint32_t>();
    if (index == 0xffffffff) return {};
    if (index >= strings.size()) throw std::runtime_error("Invalid NIF string reference.");
    return strings[index];
}
Object ObjectNET(Reader& r, const std::vector<std::string>& strings) {
    Object obj; obj.name = Name(r, strings); r.take(r.count() * 4);
    obj.animated = r.get<std::int32_t>() != -1; return obj;
}
Object AVObject(Reader& r, const std::vector<std::string>& strings) {
    auto obj = ObjectNET(r, strings); obj.hidden = (r.get<std::uint32_t>() & 1) != 0;
    for (int i = 0; i < 12; ++i) if (!MirrorAuthoringGeometry::Finite(r.get<float>())) obj.singular = true;
    float scale = r.get<float>(); obj.singular |= !MirrorAuthoringGeometry::Finite(scale) || scale <= 0.00001f;
    r.take(4); return obj;
}
}

std::string NormalizePath(std::string path, const char* root) {
    if (path.empty() || path.size() > 1024) throw std::runtime_error("An asset path is empty or too long.");
    for (auto& c : path) {
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (static_cast<unsigned char>(c) < 32 || c == ':' || c == '"') throw std::runtime_error("Invalid relative asset path.");
    }
    std::string prefix(root); for (auto& c : prefix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; prefix += '\\';
    if (path.starts_with(prefix)) path.erase(0, prefix.size());
    if (path.empty() || path.front() == '\\' || path.back() == '\\') throw std::runtime_error("Invalid relative asset path.");
    std::size_t offset{};
    while (offset < path.size()) {
        auto end = path.find('\\', offset); if (end == std::string::npos) end = path.size();
        auto part = path.substr(offset, end-offset);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            throw std::runtime_error("Invalid relative asset path.");
        offset = end + 1;
    }
    return path;
}

std::string NormalizeMaterialPath(std::string path) {
    if (path.empty() || path.size() > 1024) throw std::runtime_error("An asset path is empty or too long.");
    // Vanilla NIF shader names can retain C:\Projects\Fallout4\Build\PC\Data\materials\...
    // while CK's material list uses the Data-relative asset name. The prefix
    // describes the exporter, not a filesystem location to access on this PC.
    auto folded = path;
    for (auto& c : folded) {
        if (static_cast<unsigned char>(c) < 32 || c == '"') throw std::runtime_error("Invalid material asset path.");
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    }
    if (folded.starts_with("data\\materials\\")) path.erase(0, 5);
    else if (folded.size() >= 3 && folded[0] >= 'a' && folded[0] <= 'z' && folded[1] == ':' && folded[2] == '\\') {
        constexpr std::string_view anchor = "\\data\\materials\\";
        const auto at = folded.find(anchor, 2);
        if (at == std::string::npos) throw std::runtime_error("An exported material path must point inside Data\\Materials.");
        path.erase(0, at+anchor.size());
    }
    // Reuse the strict relative-path checks on the remaining asset name, so a
    // traversal, drive prefix, empty component or trailing dot still rejects.
    return NormalizePath(std::move(path));
}

Model ReadNif(std::span<const std::byte> bytes) {
    if (bytes.size() > kMaxMeshBytes) throw std::runtime_error("The mesh exceeds the 64 MiB authoring limit.");
    Reader r(bytes);
    if (r.string(39) != "Gamebryo File Format, Version 20.2.0.7\n" ||
        r.get<std::uint32_t>() != 0x14020007 || r.get<std::uint8_t>() != 1 || r.get<std::uint32_t>() != 12)
        throw std::runtime_error("Use a Fallout 4 NIF (20.2.0.7 / 12 / 130).");
    const auto count = r.count();
    if (!count || r.get<std::uint32_t>() != 130) throw std::runtime_error("Use a Fallout 4 NIF (BS version 130).");
    for (int i = 0; i < 4; ++i) r.take(r.get<std::uint8_t>());
    const auto typeCount = r.get<std::uint16_t>();
    if (!typeCount || typeCount > count) throw std::runtime_error("Invalid NIF block type table.");
    std::vector<std::string> types; for (unsigned i = 0; i < typeCount; ++i) types.push_back(r.sized());
    std::vector<std::uint16_t> typeIndices;
    for (unsigned i = 0; i < count; ++i) { auto t = r.get<std::uint16_t>(); if (t >= typeCount) throw std::runtime_error("Invalid NIF type index."); typeIndices.push_back(t); }
    std::vector<std::uint32_t> sizes; for (unsigned i = 0; i < count; ++i) sizes.push_back(r.get<std::uint32_t>());
    auto stringCount = r.count(); r.take(4);
    std::vector<std::string> strings; for (unsigned i = 0; i < stringCount; ++i) strings.push_back(r.sized());
    r.take(r.count() * 4);
    std::vector<Block> blocks;
    for (unsigned i = 0; i < count; ++i) blocks.push_back({types[typeIndices[i]], r.take(sizes[i])});
    auto rootCount = r.count(); std::vector<std::uint32_t> roots;
    for (unsigned i = 0; i < rootCount; ++i) roots.push_back(r.get<std::uint32_t>());
    if (r.position != bytes.size() || roots.empty()) throw std::runtime_error("Invalid NIF root/footer data.");

    Model model;
    std::vector<Object> objects(count);
    std::vector<bool> isObject(count), isNode(count), visited(count);
    std::map<std::string, unsigned> materialUses;
    for (unsigned index = 0; index < count; ++index) {
        const auto& block = blocks[index]; Reader b(block.bytes);
        const bool node = block.type == "NiNode" || block.type == "BSFadeNode" || block.type == "BSLeafAnimNode" || block.type == "NiSwitchNode" || block.type == "NiLODNode";
        if (node) {
            objects[index] = AVObject(b, strings); isObject[index] = isNode[index] = true;
            auto n = b.count(); for (unsigned i = 0; i < n; ++i) objects[index].children.push_back(b.get<std::uint32_t>());
            objects[index].animated |= block.type != "NiNode" && block.type != "BSFadeNode";
            continue;
        }
        if (block.type.find("TriShape") == std::string::npos && block.type != "BSMeshLODTriShape" && block.type != "NiTriStrips") continue;
        // Other triangle classes cannot be safely mapped through the static runtime.
        if (!block.type.starts_with("BS") || block.type == "BSLODTriShape")
            throw std::runtime_error("This mesh contains an unsupported geometry class: " + block.type);
        auto obj = AVObject(b, strings); objects[index] = obj; isObject[index] = true;
        Pane pane; pane.block = index; pane.name = obj.name.empty() ? "Unnamed part" : obj.name;
        b.take(16); auto skin = b.get<std::int32_t>(); auto shader = b.get<std::int32_t>(); b.take(4);
        if (shader < 0 || static_cast<unsigned>(shader) >= count) throw std::runtime_error("Invalid NIF shader reference.");
        Reader s(blocks[shader].bytes);
        if (blocks[shader].type == "BSLightingShaderProperty") s.take(4);
        else if (blocks[shader].type != "BSEffectShaderProperty") throw std::runtime_error("Unsupported geometry shader.");
        auto prop = ObjectNET(s, strings);
        if (prop.name.empty()) pane.reason = "Assign a unique BGSM to this part in the NIF first.";
        else {
            pane.material = NormalizeMaterialPath(prop.name);
            ++materialUses[pane.material]; model.materials.push_back(pane.material);
        }
        if (block.type != "BSTriShape") pane.reason = "Only a static BSTriShape is supported.";
        else if (skin != -1 || obj.animated || prop.animated) pane.reason = "Animated or skinned parts are unsupported.";
        else if (blocks[shader].type != "BSLightingShaderProperty" || !pane.material.ends_with(".bgsm")) pane.reason = "The pane needs a lighting BGSM material.";
        else if (pane.reason.empty()) {
            auto desc = b.get<std::uint64_t>(); auto triangles = b.count(65535); auto vertices = b.get<std::uint16_t>(); auto dataSize = b.get<std::uint32_t>();
            auto stride = (desc & 15) * 4;
            if (dataSize != vertices*stride + triangles*6) throw std::runtime_error("Invalid NIF triangle stream size.");
            auto vertexBytes = b.take(vertices * stride); auto triangleBytes = b.take(triangles * 6);
            std::vector<std::uint16_t> indices(triangles * 3); std::memcpy(indices.data(), triangleBytes.data(), triangleBytes.size());
            std::vector<MirrorAuthoringGeometry::Vec3> points; MirrorAuthoringGeometry::Surface surface;
            if (!MirrorAuthoringGeometry::Decode(vertexBytes, indices, vertices, desc, points) || !MirrorAuthoringGeometry::Build(points, surface))
                pane.reason = "The part must be flat, with consistently facing triangles.";
            else { pane.width = surface.pane.halfWidth * 2; pane.height = surface.pane.halfHeight * 2; }
        }
        model.panes.push_back(std::move(pane));
    }
    std::function<void(std::uint32_t, bool, unsigned)> visit = [&](std::uint32_t index, bool blocked, unsigned depth) {
        if (index == 0xffffffff) return;
        if (index >= count || !isObject[index] || depth > 128 || visited[index]) throw std::runtime_error("Unsupported or cyclic NIF scene hierarchy.");
        visited[index] = true; const auto& obj = objects[index]; blocked |= obj.animated || obj.hidden || obj.singular;
        if (blocked) for (auto& pane : model.panes) if (pane.block == index) pane.reason = "The part or a parent is hidden, animated or has an invalid transform.";
        if (isNode[index]) for (auto child : obj.children) visit(child, blocked, depth+1);
    };
    for (auto root : roots) visit(root, false, 0);
    for (auto& pane : model.panes) {
        if (!visited[pane.block]) pane.reason = "The part is not attached to the model root.";
        if (!pane.material.empty() && materialUses[pane.material] > 1) pane.reason = "This material is shared by multiple parts; give the pane a unique BGSM first.";
    }
    if (model.panes.empty()) throw std::runtime_error("This model has no supported mesh parts.");
    std::sort(model.materials.begin(), model.materials.end()); model.materials.erase(std::unique(model.materials.begin(), model.materials.end()), model.materials.end());
    return model;
}
}
