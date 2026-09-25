#include "Guard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MOS::DependencyGuard
{
	namespace
	{
		constexpr char kAddon[] = "MirrorsOfSkyrim.esp";
		constexpr wchar_t kMasterSuffix[] = L"Data\\RealisticReflectionsMirrors.esm";
		constexpr wchar_t kAddonSuffix[] = L"Data\\MirrorsOfSkyrim.esp";
		constexpr wchar_t kTitle[] = L"Mirrors of Skyrim - missing requirement";
		constexpr wchar_t kWarning[] =
			L"Mirrors of Skyrim requires Realistic Reflections: Mirrors.\n\n"
			L"RealisticReflectionsMirrors.esm could not be found.\n\n"
			L"MirrorsOfSkyrim.esp has been disabled for this session.\n"
			L"Click OK to continue without Mirrors of Skyrim.\n\n"
			L"Install and enable Realistic Reflections: Mirrors before your next launch.";

		decltype(&::FindFirstFileA) g_firstA{};
		decltype(&::FindNextFileA) g_nextA{};
		decltype(&::FindFirstFileW) g_firstW{};
		decltype(&::FindNextFileW) g_nextW{};
		decltype(&::FindClose) g_close{};

		template <class Char>
		bool IsAddon(const Char* a_name, DWORD a_attributes) noexcept
		{
			if (a_attributes & FILE_ATTRIBUTE_DIRECTORY) {
				return false;
			}
			for (std::size_t i = 0; i < sizeof(kAddon); ++i) {
				auto actual = a_name[i];
				auto wanted = kAddon[i];
				if (actual >= 'A' && actual <= 'Z') {
					actual += 'a' - 'A';
				}
				if (wanted >= 'A' && wanted <= 'Z') {
					wanted += 'a' - 'A';
				}
				if (actual != wanted) {
					return false;
				}
			}
			return true;
		}

		template <class Data, class Next>
		BOOL NextVisible(HANDLE a_handle, Data* a_data, Next a_next) noexcept
		{
			while (a_next(a_handle, a_data)) {
				if (!IsAddon(a_data->cFileName, a_data->dwFileAttributes)) {
					return TRUE;
				}
			}
			return FALSE;  // Preserve the original function's last error.
		}

		template <class Char, class Data, class First, class Next>
		HANDLE FirstVisible(const Char* a_pattern, Data* a_data, First a_first, Next a_next) noexcept
		{
			auto handle = a_first(a_pattern, a_data);
			if (handle != INVALID_HANDLE_VALUE && IsAddon(a_data->cFileName, a_data->dwFileAttributes)) {
				if (!NextVisible(handle, a_data, a_next)) {
					g_close(handle);
					::SetLastError(ERROR_FILE_NOT_FOUND);
					return INVALID_HANDLE_VALUE;
				}
			}
			return handle;
		}

		HANDLE WINAPI FindFirstA(LPCSTR a_pattern, LPWIN32_FIND_DATAA a_data) noexcept
		{
			return FirstVisible(a_pattern, a_data, g_firstA, g_nextA);
		}
		BOOL WINAPI FindNextA(HANDLE a_handle, LPWIN32_FIND_DATAA a_data) noexcept
		{
			return NextVisible(a_handle, a_data, g_nextA);
		}
		HANDLE WINAPI FindFirstW(LPCWSTR a_pattern, LPWIN32_FIND_DATAW a_data) noexcept
		{
			return FirstVisible(a_pattern, a_data, g_firstW, g_nextW);
		}
		BOOL WINAPI FindNextW(HANDLE a_handle, LPWIN32_FIND_DATAW a_data) noexcept
		{
			return NextVisible(a_handle, a_data, g_nextW);
		}

		struct Patch
		{
			void** slot{};
			void* before{};
			void* after{};
		};

		bool Exchange(const Patch& a_patch, bool a_install) noexcept
		{
			DWORD protection{};
			if (!::VirtualProtect(a_patch.slot, sizeof(void*), PAGE_READWRITE, &protection)) {
				return false;
			}
			auto expected = a_install ? a_patch.before : a_patch.after;
			auto desired = a_install ? a_patch.after : a_patch.before;
			const auto previous = ::InterlockedCompareExchangePointer(a_patch.slot, desired, expected);
			DWORD unused{};
			const bool restored = ::VirtualProtect(a_patch.slot, sizeof(void*), protection, &unused) != FALSE;
			return previous == expected && restored;
		}

		bool Install(HMODULE a_module) noexcept
		{
			const auto base = reinterpret_cast<std::byte*>(a_module);
			const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) {
				return false;
			}
			const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
				return false;
			}
			const auto size = nt->OptionalHeader.SizeOfImage;
			const auto fits = [size](std::size_t rva, std::size_t bytes) { return rva < size && bytes <= size - rva; };
			const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!directory.VirtualAddress || !fits(directory.VirtualAddress, directory.Size)) {
				return false;
			}
			std::array<Patch, 32> patches{};
			std::size_t count{};
			std::array<void*, 5> originals{};
			constexpr std::array names{ "FindFirstFileA", "FindNextFileA", "FindFirstFileW", "FindNextFileW", "FindClose" };
			const std::array<void*, 4> replacements{
				reinterpret_cast<void*>(&FindFirstA), reinterpret_cast<void*>(&FindNextA),
				reinterpret_cast<void*>(&FindFirstW), reinterpret_cast<void*>(&FindNextW)
			};
			const auto imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
			bool terminated{};
			for (std::size_t d = 0; d < directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++d) {
				const auto& descriptor = imports[d];
				if (!descriptor.Name) {
					terminated = true;
					break;
				}
				if (!descriptor.OriginalFirstThunk || !descriptor.FirstThunk) {
					return false;
				}
				for (std::size_t i = 0;; ++i) {
					const auto nameRva = descriptor.OriginalFirstThunk + i * sizeof(IMAGE_THUNK_DATA64);
					const auto slotRva = descriptor.FirstThunk + i * sizeof(IMAGE_THUNK_DATA64);
					if (!fits(nameRva, sizeof(IMAGE_THUNK_DATA64)) || !fits(slotRva, sizeof(void*))) {
						return false;
					}
					const auto thunk = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + nameRva);
					if (!thunk->u1.AddressOfData) {
						break;
					}
					if (IMAGE_SNAP_BY_ORDINAL64(thunk->u1.Ordinal)) {
						continue;
					}
					const auto rva = thunk->u1.AddressOfData;
					if (!fits(rva, sizeof(IMAGE_IMPORT_BY_NAME))) {
						return false;
					}
					const auto name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + rva)->Name;
					if (!std::memchr(name, 0, size - rva - offsetof(IMAGE_IMPORT_BY_NAME, Name))) {
						return false;
					}
					for (std::size_t j = 0; j < names.size(); ++j) {
						if (std::strcmp(name, names[j]) != 0) {
							continue;
						}
						auto slot = reinterpret_cast<void**>(base + slotRva);
						if (!*slot || (originals[j] && originals[j] != *slot)) {
							return false;
						}
						originals[j] = *slot;
						if (j < replacements.size()) {
							if (count == patches.size()) {
								return false;
							}
							patches[count++] = { slot, *slot, replacements[j] };
						}
					}
				}
			}
			// The reviewed SE/AE discovery path requires the ANSI pair. Also wrap
			// a complete Unicode pair if a future host imports one. Never substitute
			// raw Kernel32 addresses: the originals may belong to a mod-manager VFS.
			if (!terminated || !originals[0] || !originals[1] || !originals[4] ||
				(static_cast<bool>(originals[2]) != static_cast<bool>(originals[3]))) {
				return false;
			}
			g_firstA = reinterpret_cast<decltype(g_firstA)>(originals[0]);
			g_nextA = reinterpret_cast<decltype(g_nextA)>(originals[1]);
			g_firstW = reinterpret_cast<decltype(g_firstW)>(originals[2]);
			g_nextW = reinterpret_cast<decltype(g_nextW)>(originals[3]);
			g_close = reinterpret_cast<decltype(g_close)>(originals[4]);
			for (std::size_t i = 0; i < count; ++i) {
				if (!Exchange(patches[i], true)) {
					// Startup will be terminated by the caller. Roll back any owned
					// slots we can, including one whose protection restore failed.
					for (std::size_t j = 0; j <= i; ++j) {
						(void)Exchange(patches[j], false);
					}
					return false;
				}
			}
			return true;
		}
	}

	bool Initialize(Notify a_notify) noexcept
	{
		wchar_t path[32768]{};
		const auto length = ::GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
		if (length == 0 || length >= std::size(path)) {
			return false;
		}
		std::size_t end = length;
		while (end && path[end - 1] != L'\\' && path[end - 1] != L'/') {
			--end;
		}
		if (!end || end + std::size(kMasterSuffix) > std::size(path)) {
			return false;
		}
		std::memcpy(path + end, kAddonSuffix, sizeof(kAddonSuffix));
		const auto addonAttributes = ::GetFileAttributesW(path);
		if (addonAttributes == INVALID_FILE_ATTRIBUTES || (addonAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			return true;  // An orphan guard DLL has no add-on to disable.
		}
		std::memcpy(path + end, kMasterSuffix, sizeof(kMasterSuffix));
		const auto masterAttributes = ::GetFileAttributesW(path);
		if (masterAttributes != INVALID_FILE_ATTRIBUTES && !(masterAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			return true;  // Normal installation: no hooks or dialog.
		}
		if (!Install(::GetModuleHandleW(nullptr))) {
			return false;
		}
		::OutputDebugStringW(L"[MirrorsOfSkyrimDependencyGuard] Missing core ESM; addon hidden for this session.\n");
		a_notify(nullptr, kWarning, kTitle, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
		return true;
	}
}
