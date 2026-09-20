#include "Assets.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace MirrorCK;
namespace {
unsigned checks{};
void Check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
void Throws(const std::function<void()>& action, const char* label) { bool threw = false; try { action(); } catch (const std::exception&) { threw = true; } Check(threw, label); }
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--model") {
            auto model = ReadNif(LoadMesh(argv[2], argv[3]));
            for (const auto& part : model.panes) std::cout << part.block << " | " << part.name << " | " << part.material << " | " << (part.eligible() ? "AVAILABLE" : part.reason) << '\n';
            return 0;
        }
        if (argc != 2) throw std::runtime_error("Expected test fixture directory.");
        std::filesystem::path root(argv[1]);
        auto read = [&](const char* name) { return ReadNif(ReadFile(root/name, kMaxMeshBytes)); };
        auto flat = read("Flat.nif"); Check(flat.panes.size() == 1 && flat.panes[0].eligible(), "Original flat quad should be selectable");
        Check(flat.panes[0].name == "MOFCKPane" && flat.panes[0].width == 64 && flat.panes[0].height == 96, "Mesh part name and dimensions");
        auto exported = read("ExportedMaterial.nif");
        Check(exported.panes.size() == 1 && exported.panes[0].eligible() && exported.materials == flat.materials,
            "A vanilla-style absolute export material must match the relative CK asset");
        auto sharedExported = read("SharedExportedMaterial.nif");
        Check(sharedExported.panes.size() == 2 && sharedExported.materials.size() == 1 &&
            !sharedExported.panes[0].eligible() && !sharedExported.panes[1].eligible(),
            "Absolute and relative aliases of a shared material must reject both parts");
        auto hole = read("Hole.nif"); Check(hole.panes.size() == 1 && hole.panes[0].eligible(), "Coplanar pane with a hole");
        for (const char* name : {"CurvedReject.nif", "Animated.nif", "Skinned.nif", "HiddenParent.nif", "BadIndex.nif"})
            Check(!read(name).panes[0].eligible(), name);
        auto shared = read("Shared.nif"); Check(shared.panes.size() == 2 && !shared.panes[0].eligible() && !shared.panes[1].eligible(), "Shared materials must reject both parts");
        auto separate = read("TwoParts.nif"); Check(separate.panes.size() == 2 && separate.panes[0].eligible() && separate.panes[1].eligible(), "Distinct flat parts can be selected independently");
        Throws([&] { read("Cycle.nif"); }, "Scene cycle must not recurse indefinitely");
        auto bytes = ReadFile(root/"Flat.nif", kMaxMeshBytes);
        for (std::size_t size = 0; size < bytes.size(); ++size) Throws([&] { ReadNif(std::span(bytes).first(size)); }, "Truncated NIF must reject");
        for (const char* path : {"../a.bgsm", "Materials/../../a.bgsm", "C:/a.bgsm", "/a.bgsm", "a//b.bgsm", "a/./b.bgsm", "a./b.bgsm"})
            Throws([&] { NormalizePath(path); }, "Unsafe asset path must reject");
        Check(NormalizePath("Materials/Example/A.BGSM") == "example\\a.bgsm", "Case and prefix normalization");
        for (const char* path : {"example/a.bgsm", "Materials/Example/A.BGSM", "Data/MATERIALS/example/a.bgsm",
            "C:\\Projects\\Fallout4\\Build\\PC\\Data\\materials\\Example\\A.BGSM", "E:/Export Folder/Data/Materials/Example/A.BGSM"})
            Check(NormalizeMaterialPath(path) == "example\\a.bgsm", "Embedded material aliases resolve to one asset name");
        for (const char* path : {"C:/a.bgsm", "C:a.bgsm", "C:/Materials/a.bgsm", "//host/Data/Materials/a.bgsm",
            "C:/Export/NotData/Materials/a.bgsm", "C:/Export/Data/Materials/../a.bgsm", "C:/Export/Data/Materials/C:/a.bgsm",
            "C:/Export/Data/Materials/a//b.bgsm", "Data/Materials/../../a.bgsm", "C:/Export/Data/Materials/"})
            Throws([&] { NormalizeMaterialPath(path); }, "Malformed material asset paths must still reject");
        for (int version : {1, 7, 8}) for (int compressed : {0, 1}) {
            auto folder = root/("archive"+std::to_string(version)+"-"+std::to_string(compressed));
            Check(LoadMesh(folder, "test.nif") == bytes, "BA2 stored/compressed byte equality");
        }
        Throws([&] { LoadMesh(root/"conflict", "test.nif"); }, "Conflicting archive providers must reject");
        Throws([&] { LoadMesh(root/"unknown", "test.nif"); }, "Unknown archive layout must reject");
        Check(LoadMesh(root/"Data", "MirrorsOfFallout/HelperTest/Flat.nif") == bytes, "Loose NIF path");
        std::cout << "PASS: " << checks << " CK asset checks\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
