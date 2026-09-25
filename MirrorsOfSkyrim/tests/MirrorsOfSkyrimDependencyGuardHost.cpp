// Offline host for the actual shipping DLL. No Skyrim executable is launched.
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace
{
	void Require(bool ok, const char* what)
	{
		if (!ok) {
			std::fprintf(stderr, "FAIL: %s (error %lu)\n", what, ::GetLastError());
			::ExitProcess(1);
		}
	}
	void** ImportSlot(HMODULE module, const char* name, char** importedName = nullptr)
	{
		auto base = reinterpret_cast<std::byte*>(module);
		auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
		for (; descriptor->Name; ++descriptor) {
			auto thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
			auto slots = reinterpret_cast<void**>(base + descriptor->FirstThunk);
			for (std::size_t i = 0; thunk[i].u1.AddressOfData; ++i) {
				if (IMAGE_SNAP_BY_ORDINAL64(thunk[i].u1.Ordinal)) continue;
				auto text = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk[i].u1.AddressOfData)->Name;
				if (std::strcmp(text, name) == 0) {
					if (importedName) *importedName = text;
					return slots + i;
				}
			}
		}
		return nullptr;
	}
	void Replace(HMODULE module, const char* name, void* replacement)
	{
		auto slot = ImportSlot(module, name);
		Require(slot != nullptr, name);
		DWORD protection{}, unused{};
		Require(::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection) != FALSE, "test IAT protection");
		*slot = replacement;
		Require(::VirtualProtect(slot, sizeof(void*), protection, &unused) != FALSE, "test IAT restore");
	}
	int warnings{};
	int WINAPI Notify(HWND, LPCWSTR text, LPCWSTR, UINT flags)
	{
		++warnings;
		Require((flags & MB_TYPEMASK) == MB_OK, "OK-only warning");
		Require(std::wcsstr(text, L"Mirrors of Skyrim") != nullptr, "warning names addon");
		if (std::wcsstr(text, L"Click OK to close Skyrim")) {
			std::puts("CLOSE_STARTUP");
		} else {
			Require(std::wcsstr(text, L"RealisticReflectionsMirrors.esm") != nullptr, "warning names missing ESM");
			Require(std::wcsstr(text, L"disabled for this session") != nullptr, "warning explains disable scope");
			Require(std::wcsstr(text, L"OK to continue") != nullptr, "warning offers continuation");
			std::puts("OK_CONTINUE");
		}
		std::fflush(stdout);
		return IDOK;
	}

	struct VirtualEntry { const char* name; DWORD attributes{ FILE_ATTRIBUTE_NORMAL }; };
	std::vector<VirtualEntry> virtualFiles;
	std::size_t position{};
	unsigned closes{};
	const auto fakeHandle = reinterpret_cast<HANDLE>(std::uintptr_t{ 0x12345 });
	BOOL Emit(WIN32_FIND_DATAA* data)
	{
		if (position == virtualFiles.size()) { ::SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
		*data = {};
		const auto entry = virtualFiles[position++];
		strcpy_s(data->cFileName, entry.name);
		data->dwFileAttributes = entry.attributes;
		return TRUE;
	}
	BOOL Emit(WIN32_FIND_DATAW* data)
	{
		WIN32_FIND_DATAA narrow{};
		if (!Emit(&narrow)) return FALSE;
		*data = {};
		::MultiByteToWideChar(CP_UTF8, 0, narrow.cFileName, -1, data->cFileName, MAX_PATH);
		data->dwFileAttributes = narrow.dwFileAttributes;
		return TRUE;
	}
	HANDLE WINAPI VirtualFirstA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
	{
		if (std::strcmp(pattern, "no-such-directory/*") == 0) { ::SetLastError(ERROR_PATH_NOT_FOUND); return INVALID_HANDLE_VALUE; }
		position = 0;
		return Emit(data) ? fakeHandle : INVALID_HANDLE_VALUE;
	}
	HANDLE WINAPI VirtualFirstW(LPCWSTR, LPWIN32_FIND_DATAW data) { position = 0; return Emit(data) ? fakeHandle : INVALID_HANDLE_VALUE; }
	BOOL WINAPI VirtualNextA(HANDLE handle, LPWIN32_FIND_DATAA data) { Require(handle == fakeHandle, "VFS ANSI handle"); return Emit(data); }
	BOOL WINAPI VirtualNextW(HANDLE handle, LPWIN32_FIND_DATAW data) { Require(handle == fakeHandle, "VFS Unicode handle"); return Emit(data); }
	BOOL WINAPI VirtualClose(HANDLE handle) { Require(handle == fakeHandle, "VFS close handle"); ++closes; return TRUE; }

	std::set<std::string> EnumerateA(const std::string& pattern)
	{
		std::set<std::string> found;
		WIN32_FIND_DATAA data{};
		auto handle = ::FindFirstFileA(pattern.c_str(), &data);
		if (handle != INVALID_HANDLE_VALUE) {
			do { found.emplace(data.cFileName); } while (::FindNextFileA(handle, &data));
			Require(::GetLastError() == ERROR_NO_MORE_FILES, "ANSI enumeration terminal error");
			Require(::FindClose(handle) != FALSE, "ANSI handle closed");
		}
		return found;
	}
	std::set<std::wstring> EnumerateW(const std::wstring& pattern)
	{
		std::set<std::wstring> found;
		WIN32_FIND_DATAW data{};
		auto handle = ::FindFirstFileW(pattern.c_str(), &data);
		if (handle != INVALID_HANDLE_VALUE) {
			do { found.emplace(data.cFileName); } while (::FindNextFileW(handle, &data));
			Require(::GetLastError() == ERROR_NO_MORE_FILES, "Unicode enumeration terminal error");
			Require(::FindClose(handle) != FALSE, "Unicode handle closed");
		}
		return found;
	}
}

int wmain(int argc, wchar_t** argv)
{
	Require(argc == 3, "host arguments");
	const std::wstring mode = argv[2];
	const bool chain = mode.starts_with(L"chain-");
	const bool normal = mode == L"present" || mode == L"orphan";
	const auto host = ::GetModuleHandleW(nullptr);
	if (chain) {
		virtualFiles = {{"A.esp"}, {"Z.esp"}, {"MirrorsOfSkyrim.esp.bak"}, {"MirrorsOfSkyrim.espX"}, {"MirrorsOfSkyrim.esp", FILE_ATTRIBUTE_DIRECTORY}};
		if (mode == L"chain-only") virtualFiles = {{"mIrRoRsOfSkYrIm.EsP"}};
		else if (mode == L"chain-first") virtualFiles.insert(virtualFiles.begin(), {"mIrRoRsOfSkYrIm.EsP"});
		else if (mode == L"chain-last") virtualFiles.push_back({"mIrRoRsOfSkYrIm.EsP"});
		else virtualFiles.insert(virtualFiles.begin() + 1, {"mIrRoRsOfSkYrIm.EsP"});
		Replace(host, "FindFirstFileA", reinterpret_cast<void*>(&VirtualFirstA));
		Replace(host, "FindNextFileA", reinterpret_cast<void*>(&VirtualNextA));
		Replace(host, "FindFirstFileW", reinterpret_cast<void*>(&VirtualFirstW));
		Replace(host, "FindNextFileW", reinterpret_cast<void*>(&VirtualNextW));
		Replace(host, "FindClose", reinterpret_cast<void*>(&VirtualClose));
	}
	if (mode == L"fail-import") {
		char* text{};
		Require(ImportSlot(host, "FindNextFileA", &text) != nullptr, "negative import exists");
		DWORD old{}, unused{};
		Require(::VirtualProtect(text, 1, PAGE_READWRITE, &old) != FALSE, "negative import writable");
		*text = 'X';
		Require(::VirtualProtect(text, 1, old, &unused) != FALSE, "negative import protection restored");
	}
	const auto before = *ImportSlot(host, "FindFirstFileA");
	auto dll = ::LoadLibraryW(argv[1]);
	Require(dll != nullptr, "shipping DLL loaded");
	Replace(dll, "MessageBoxW", reinterpret_cast<void*>(&Notify));
	struct Prefix { std::uint32_t skseVersion, runtimeVersion, editorVersion, isEditor; } prefix{ 0x02000000, 0x01050610, 0, 0 };
	struct Info { std::uint32_t version; const char* name; std::uint32_t pluginVersion; } info{};
	const auto query = reinterpret_cast<bool(*)(const Prefix*, Info*)>(::GetProcAddress(dll, "SKSEPlugin_Query"));
	const auto load = reinterpret_cast<bool(*)(const Prefix*)>(::GetProcAddress(dll, "SKSEPlugin_Load"));
	Require(query && load && ::GetProcAddress(dll, "SKSEPlugin_Version"), "SE and AE exports");
	Require(query(&prefix, &info) && info.version == 1 && info.pluginVersion == 1, "SE query metadata");
	prefix.isEditor = 1;
	Require(!query(&prefix, &info) && !load(&prefix), "editor rejected");
	prefix.isEditor = 0;
	Require(load(&prefix), "startup guard returned to SKSE");
	Require(warnings == (normal ? 0 : 1), "warning count");
	Require((before == *ImportSlot(host, "FindFirstFileA")) == normal, "hooks only when required");
	if (chain) {
		std::set<std::string> expected;
		std::set<std::wstring> expectedW;
		for (auto entry : virtualFiles) {
			if (std::strcmp(entry.name, "mIrRoRsOfSkYrIm.EsP") == 0) continue;
			expected.emplace(entry.name);
			expectedW.emplace(entry.name, entry.name + std::strlen(entry.name));
		}
		Require(EnumerateA("virtual/*") == expected, "VFS ANSI results preserved except addon");
		if (expected.empty()) Require(::GetLastError() == ERROR_FILE_NOT_FOUND, "first-only error");
		Require(EnumerateW(L"virtual/*") == expectedW, "VFS Unicode results preserved except addon");
		Require(closes == 2, "VFS handles closed exactly once");
		WIN32_FIND_DATAA data{};
		Require(::FindFirstFileA("no-such-directory/*", &data) == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_PATH_NOT_FOUND, "original first error preserved");
	} else {
		const auto data = std::filesystem::path(argv[0]).parent_path() / L"Data";
		auto found = EnumerateA((data / L"*.esp").string());
		auto foundW = EnumerateW((data / L"*.esp").wstring());
		std::set<std::string> expected{ "A.esp", "Z.esp" };
		std::set<std::wstring> expectedW{ L"A.esp", L"Z.esp" };
		if (mode == L"present") { expected.emplace("MirrorsOfSkyrim.esp"); expectedW.emplace(L"MirrorsOfSkyrim.esp"); }
		Require(found == expected && foundW == expectedW, "real loose plugin enumeration");
		if (!normal) {
			WIN32_FIND_DATAA item{};
			Require(::FindFirstFileA((data / L"MirrorsOfSkyrim.esp").string().c_str(), &item) == INVALID_HANDLE_VALUE, "direct addon discovery hidden");
			Require(::GetLastError() == ERROR_FILE_NOT_FOUND, "direct hidden-file error");
			Require(::GetFileAttributesW((data / L"MirrorsOfSkyrim.esp").c_str()) != INVALID_FILE_ATTRIBUTES, "ESP remains on disk");
		}
	}
	std::puts("PASS");
	return 0;
}
