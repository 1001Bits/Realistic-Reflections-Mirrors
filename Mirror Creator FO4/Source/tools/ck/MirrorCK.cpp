#include "Assets.h"
#include "Picker.h"
#include "Native.h"
#include <bit>
#include "Win32.h"
#include <CommCtrl.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace MirrorCK {
namespace {
constexpr UINT kAssign = 0x6d01, kStatus = 0x6d02;
constexpr UINT_PTR kSubclass = 0x4d4f4643;
HMODULE module{};
struct Context { HWND window{}; const std::byte* model{}; bool busy{}; };
struct Mapping { std::string original, replacement; float remap{}; };

void Log(const std::wstring& value) {
    auto path = ModulePath().parent_path() / L"MirrorsOfFalloutCK.log";
    auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return; auto line = Narrow(value+L"\r\n"); DWORD written{};
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr); CloseHandle(file);
}
template<class T> T Read(const void* address) {
    T value{}; SIZE_T count{};
    if (!ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &count) || count != sizeof(value))
        throw std::runtime_error("CK's model data is unavailable. Close and reopen Model Data.");
    return value;
}
std::string String(const char* pointer) {
    if (!pointer) return {};
    std::string value;
    for (unsigned i = 0; i < 1024; ++i) { auto c = Read<char>(pointer+i); if (!c) return value; value += c; }
    throw std::runtime_error("CK returned an invalid material path.");
}
std::string FixedString(const std::byte* entry) {
    // CK stores a string-cache entry, unlike the character pointer in its list
    // control. Follow bounded external aliases; never acquire/release CK refs.
    for (unsigned depth = 0; entry && depth < 16; ++depth) {
        const auto flags = Read<std::uint16_t>(entry+8);
        if (flags & 0x8000) throw std::runtime_error("Unsupported wide CK asset string.");
        if (flags & 0x4000) { entry = Read<const std::byte*>(entry+0x18); continue; }
        if (Read<std::uint32_t>(entry+0x10) > 1023) throw std::runtime_error("Oversized CK asset string.");
        return String(reinterpret_cast<const char*>(entry+0x18));
    }
    if (!entry) return {};
    throw std::runtime_error("Invalid CK string-cache alias.");
}
std::vector<Mapping> Mappings(const Context& context) {
    // Read-only ABI, exercised against native saved/reopened fixtures on the
    // exact CK fingerprint. CK creates the swap; Native inserts its entries.
    auto swap = Read<const std::byte*>(context.model+0x40); if (!swap) return {};
    auto capacity = Read<std::uint32_t>(swap+0x34), free = Read<std::uint32_t>(swap+0x38);
    auto entries = Read<const std::byte*>(swap+0x58);
    if (capacity > 4096 || free > capacity || (capacity && !entries)) throw std::runtime_error("Unsupported material swap layout.");
    std::vector<Mapping> result;
    for (unsigned i = 0; i < capacity; ++i) {
        auto entry = entries+i*0x20; auto original = Read<const std::byte*>(entry);
        if (!original || !Read<const void*>(entry+0x18)) continue;
        auto replacement = Read<const std::byte*>(entry+8);
        result.push_back({NormalizeMaterialPath(FixedString(original)), NormalizeMaterialPath(FixedString(replacement)), Read<float>(entry+0x10)});
    }
    if (result.size() != capacity-free) throw std::runtime_error("CK's material swap is not ready.");
    return result;
}
bool SameMappings(std::vector<Mapping> a, std::vector<Mapping> b) {
    auto less = [](const auto& x, const auto& y) { return x.original < y.original; };
    std::sort(a.begin(), a.end(), less); std::sort(b.begin(), b.end(), less);
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].original != b[i].original || a[i].replacement != b[i].replacement ||
            std::memcmp(&a[i].remap, &b[i].remap, sizeof(float))) return false;
    return true;
}
std::vector<std::string> Materials(HWND list) {
    auto count = ListView_GetItemCount(list); if (count < 1 || count > 4096) throw std::runtime_error("CK could not load the model's materials.");
    std::vector<std::string> paths;
    for (int i = 0; i < count; ++i) {
        LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = i;
        if (!SendMessageW(list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) throw std::runtime_error("CK's material list changed.");
        paths.push_back(NormalizeMaterialPath(String(reinterpret_cast<const char*>(item.lParam))));
    }
    return paths;
}
int ComboFind(HWND combo, const wchar_t* wanted) {
    auto n = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    if (n < 0 || n > 50000) return -1;
    for (int i = 0; i < n; ++i) {
        auto length = SendMessageW(combo, CB_GETLBTEXTLEN, i, 0); if (length < 0 || length > 1024) continue;
        std::wstring text(length+1, L'\0'); SendMessageW(combo, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        if (!_wcsicmp(text.c_str(), wanted)) return i;
    }
    return -1;
}
void SelectSwap(HWND parent, int index) {
    auto combo = GetDlgItem(parent, 5886); SendMessageW(combo, CB_SETCURSEL, index, 0);
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(5886, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
}
const std::byte* Library(HWND parent) {
    auto valid = GetDlgItem(parent, 5904); bool checked = SendMessageW(valid, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (checked) SendMessageW(valid, BM_CLICK, 0, 0);
    auto combo = GetDlgItem(parent, 5886); int index = ComboFind(combo, L"MOF_MirrorSurfaceSwapTemplate");
    auto found = index >= 0 ? reinterpret_cast<const std::byte*>(SendMessageW(combo, CB_GETITEMDATA, index, 0)) : nullptr;
    if (checked) SendMessageW(valid, BM_CLICK, 0, 0); return found;
}
void Assign(Context& context) {
    auto library = Library(context.window);
    if (!library) throw std::runtime_error("Load Realistic Reflections - Mirrors.esm in CK first.");
    auto name = Narrow(Text(GetDlgItem(context.window, 2421)));
    auto nativeName = FixedString(Read<const std::byte*>(context.model+8));
    Log(L"Model path: UI="+Wide(name)+L"; CK="+Wide(nativeName));
    // Cross-check the native init pointer before any read of the MSWP structure.
    if (NormalizePath(nativeName, "Meshes") != NormalizePath(name, "Meshes"))
        throw std::runtime_error("CK's model changed. Close and reopen Model Data.");
    auto data = ModulePath().parent_path()/L"Data";
    if (!std::filesystem::is_regular_file(data/kPluginW) || !std::filesystem::is_regular_file(data/L"Materials"/kMarkerW))
        throw std::runtime_error("Install the Realistic Reflections - Mirrors authoring library in this CK's Data folder.");
    auto cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    Model model;
    try { model = ReadNif(LoadMesh(data, name)); } catch (...) { SetCursor(cursor); throw; }
    SetCursor(cursor);
    auto mappings = Mappings(context);
    auto native = Materials(GetDlgItem(context.window, 5885)); std::sort(native.begin(), native.end()); native.erase(std::unique(native.begin(), native.end()), native.end());
    // CK also lists unused original paths from an assigned MSWP. Keep those
    // mappings, but do not mistake them for additional parts in the NIF.
    const bool allParts = std::includes(native.begin(), native.end(), model.materials.begin(), model.materials.end());
    const bool knownExtras = std::all_of(native.begin(), native.end(), [&](const auto& path) {
        return std::binary_search(model.materials.begin(), model.materials.end(), path) ||
            std::any_of(mappings.begin(), mappings.end(), [&](const auto& item) { return item.original == path; });
    });
    if (!allParts || !knownExtras) throw std::runtime_error("CK's preview uses different materials from the available NIF. Extract the NIF CK uses as a loose file, then reopen Model Data.");
    int chosen = PickPane(context.window, model);
    // The native Static editor can destroy Model Data while our modal picker
    // is open. Its subclass retains Context until this callback returns.
    if (chosen < 0 || !context.window) return;
    const auto pane = model.panes.at(chosen);
    Log(L"Assigning " + Wide(name)+L" / "+Wide(pane.name)+L"; existing substitutions="+std::to_wstring(mappings.size()));
    const auto marker = NormalizePath(kMarker);
    auto tagged = std::count_if(mappings.begin(), mappings.end(), [&](const auto& item) { return item.replacement == marker; });
    for (const auto& item : mappings) if (tagged == 1 && item.replacement == marker && item.original == pane.material) {
        SetDlgItemTextW(context.window, kStatus, L"This pane already uses MOF_MirrorSurface."); return;
    }
    auto api = Native::Bind();
    struct OwnedMapping { Native::Ref original, replacement; float remap; std::string path, target; };
    auto snapshot = [&](const std::byte* swap) {
        std::vector<OwnedMapping> entries;
        if (!swap) return entries;
        auto capacity = Read<std::uint32_t>(swap+0x34), free = Read<std::uint32_t>(swap+0x38);
        auto dataEntries = Read<const std::byte*>(swap+0x58);
        if (capacity > 4096 || free > capacity || (capacity && !dataEntries)) throw std::runtime_error("Invalid CK material table.");
        for (unsigned i = 0; i < capacity; ++i) {
            auto entry = dataEntries+i*0x20;
            if (!Read<const void*>(entry+0x18)) continue;
            auto key = Read<const std::byte*>(entry), value = Read<const std::byte*>(entry+8);
            auto path = NormalizeMaterialPath(FixedString(key)), target = NormalizeMaterialPath(FixedString(value));
            entries.push_back({Native::Ref(key, api), Native::Ref(value, api), Read<float>(entry+0x10), std::move(path), std::move(target)});
        }
        if (entries.size() != capacity-free) throw std::runtime_error("CK's material table changed.");
        return entries;
    };
    auto saved = snapshot(Read<const std::byte*>(context.model+0x40));
    auto templateEntries = snapshot(library);
    auto markerEntry = std::find_if(templateEntries.begin(), templateEntries.end(), [&](const auto& item) { return item.target == marker; });
    if (markerEntry == templateEntries.end()) throw std::runtime_error("The authoring master has no valid mirror marker material.");
    // The native material list stores BSStringCache::Entry::data(), not an
    // owning reference. Validate its header and pin it across model changes.
    auto list = GetDlgItem(context.window, 5885); const std::byte* paneEntry{};
    for (int i = 0, n = ListView_GetItemCount(list); i < n; ++i) {
        LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = i;
        if (!SendMessageW(list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) continue;
        auto text = String(reinterpret_cast<const char*>(item.lParam));
        if (NormalizeMaterialPath(text) != pane.material) continue;
        auto entry = reinterpret_cast<const std::byte*>(item.lParam)-0x18;
        if (Read<std::uint32_t>(entry+0x10) != text.size() || FixedString(entry) != text || !(Read<std::uint16_t>(entry+8)&0x3fff))
            throw std::runtime_error("CK's pane material is not a valid cached string.");
        paneEntry = entry; break;
    }
    if (!paneEntry) throw std::runtime_error("CK's selected pane material changed.");
    Native::Ref paneRef(paneEntry, api);
    auto combo = GetDlgItem(context.window, 5886); int previous = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    int custom = ComboFind(combo, L" Custom Material Swap"); if (custom < 0) throw std::runtime_error("CK's Custom Material Swap option is unavailable.");
    const float noRemap = std::bit_cast<float>(0x7f7fffffu);
    auto insert = [&](std::byte* destination, const Native::Ref& key, const Native::Ref& value, float remap) {
        if (!Native::Insert(api, destination, key, value, remap)) throw std::runtime_error("CK could not update its temporary material table.");
    };
    try {
        // The native selection owns creation/lifetime; only its entries change.
        // Never call TESForm::CopyFrom here: it also overwrites the form ID.
        SelectSwap(context.window, custom);
        if (!context.window) return;
        auto destination = Read<std::byte*>(context.model+0x40);
        if (!destination) throw std::runtime_error("CK did not create a custom swap.");
        auto identity = Read<std::uint32_t>(destination+0x14);
        for (const auto& item : saved)
            insert(destination, item.original, item.target == marker ? item.original : item.replacement, item.remap);
        insert(destination, paneRef, markerEntry->replacement, noRemap);
        auto after = Mappings(context); auto expected = mappings;
        for (auto& item : expected) if (item.replacement == marker) item.replacement = item.original;
        auto selected = std::find_if(expected.begin(), expected.end(), [&](const auto& item) { return item.original == pane.material; });
        if (selected == expected.end()) expected.push_back({pane.material, marker, noRemap});
        else { selected->replacement = marker; selected->remap = noRemap; }
        if (Read<std::uint32_t>(destination+0x14) != identity || !SameMappings(expected, after))
            throw std::runtime_error("CK's material assignment did not match the requested edit.");
        Log(L"Assigned pane; preserved all other substitutions and remap values. Custom ID="+std::to_wstring(identity));
    } catch (...) {
        if (!context.window) throw;
        if (previous != custom) SelectSwap(context.window, previous);
        else {
            SelectSwap(context.window, custom);
            auto original = Read<std::byte*>(context.model+0x40);
            for (const auto& item : saved) insert(original, item.original, item.replacement, item.remap);
            if (!SameMappings(mappings, Mappings(context))) throw std::runtime_error("The assignment failed. Cancel the Static dialog to discard this edit.");
        }
        throw;
    }
    SetDlgItemTextW(context.window, kStatus, (L"MOF_MirrorSurface assigned to "+Wide(pane.name)+L".\r\nConfirm Model Data and Static with OK, then place the object and save your ESP.").c_str());
}
LRESULT CALLBACK ModelProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto* context = reinterpret_cast<Context*>(ref);
    if (message == WM_COMMAND && LOWORD(w) == kAssign && !context->busy) {
        context->busy = true; EnableWindow(GetDlgItem(window, kAssign), FALSE);
        try { Assign(*context); } catch (const std::exception& error) {
            Log(L"Assignment declined: "+Wide(error.what()));
            if (context->window) MessageBoxW(window, Wide(error.what()).c_str(), L"Realistic Reflections - Mirrors authoring", MB_OK | MB_ICONINFORMATION);
        } catch (...) { Log(L"Unexpected authoring error."); }
        context->busy = false;
        if (context->window) EnableWindow(GetDlgItem(window, kAssign), TRUE);
        else delete context;
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, ModelProc, kSubclass); context->window = nullptr;
        if (!context->busy) delete context;
    }
    return DefSubclassProc(window, message, w, l);
}
void Attach(HWND window, LPARAM init) {
    if (Text(window) != L"Model Data" || !GetDlgItem(window, 5886) || !GetDlgItem(window, 5885) || GetDlgItem(window, kAssign)) return;
    auto owner = GetWindow(window, GW_OWNER);
    if (Text(owner) != L"Static" || !GetDlgItem(owner, 5500)) return;
    auto context = std::make_unique<Context>(); context->window = window; context->model = reinterpret_cast<const std::byte*>(init);
    if (!init) return;
    HMODULE pinned{}; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&Attach), &pinned);
    if (!SetWindowSubclass(window, ModelProc, kSubclass, reinterpret_cast<DWORD_PTR>(context.get()))) return;
    context.release();
    RECT preview{}; GetWindowRect(GetDlgItem(window, 2175), &preview); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&preview), 2);
    int left = preview.left, top = preview.bottom+52, width = preview.right-preview.left;
    auto button = CreateWindowExW(0, L"BUTTON", L"Alternate Textures...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, left, top, width, 30, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kAssign)), module, nullptr);
    auto label = CreateWindowExW(0, L"STATIC", L"Choose a flat mesh part and assign MOF_MirrorSurface.", WS_CHILD | WS_VISIBLE, left, top+40, width, 105, window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kStatus)), module, nullptr);
    auto font = SendMessageW(GetDlgItem(window, IDOK), WM_GETFONT, 0, 0); SendMessageW(button, WM_SETFONT, font, TRUE); SendMessageW(label, WM_SETFONT, font, TRUE);
    Log(L"Alternate Textures available for "+Text(GetDlgItem(owner, 5500)));
}
}
}
extern "C" __declspec(dllexport) LRESULT CALLBACK MirrorCKHook(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        static bool compatible = MirrorCK::SupportedEditor(MirrorCK::ModulePath());
        if (compatible) {
            const auto* message = reinterpret_cast<CWPRETSTRUCT*>(l);
            if (message && message->message == WM_INITDIALOG) try { MirrorCK::Attach(message->hwnd, message->lParam); } catch (...) { MirrorCK::Log(L"Unsupported CK dialog; helper skipped it."); }
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { MirrorCK::module = instance; DisableThreadLibraryCalls(instance); } return TRUE;
}
