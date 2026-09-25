#include "Guard.h"

#include <cstddef>
#include <cstdint>

// The public SKSE loader ABI only. No CommonLib, Address Library, gameplay
// structures, runtime offsets, renderer or core DLL is needed by this guard.
// Layout: ianpatt/skse64, skse64/PluginAPI.h (SKSEPluginVersionData v1).
namespace
{
	struct QueryInterfacePrefix
	{
		std::uint32_t skseVersion;
		std::uint32_t runtimeVersion;
		std::uint32_t editorVersion;
		std::uint32_t isEditor;
	};
	struct PluginInfo
	{
		std::uint32_t infoVersion;
		const char* name;
		std::uint32_t version;
	};
	struct VersionData
	{
		std::uint32_t dataVersion{ 1 };
		std::uint32_t pluginVersion{ 1 };
		char name[256]{ "MirrorsOfSkyrimDependencyGuard" };
		char author[256]{ "Noud" };
		char supportEmail[252]{};
		std::uint32_t versionIndependenceEx{ 1 };  // No gameplay structures.
		std::uint32_t versionIndependence{ 2 };    // No hardcoded addresses.
		std::uint32_t compatibleVersions[16]{};
		std::uint32_t seVersionRequired{};
	};
	static_assert(sizeof(VersionData) == 0x350);
	static_assert(offsetof(VersionData, versionIndependenceEx) == 0x304);
}

extern "C"
{
	__declspec(dllexport) constinit VersionData SKSEPlugin_Version{};

	__declspec(dllexport) bool SKSEPlugin_Query(const QueryInterfacePrefix* a_skse, PluginInfo* a_info)
	{
		if (!a_skse || !a_info) {
			return false;
		}
		a_info->infoVersion = 1;
		a_info->name = "MirrorsOfSkyrimDependencyGuard";
		a_info->version = 1;
		return !a_skse->isEditor;
	}

	__declspec(dllexport) bool SKSEPlugin_Load(const QueryInterfacePrefix* a_skse)
	{
		if (!a_skse || a_skse->isEditor) {
			return false;
		}
		if (!MOS::DependencyGuard::Initialize()) {
			::MessageBoxW(nullptr,
				L"Mirrors of Skyrim could not safely check or disable its missing requirement.\n\n"
				L"Install Realistic Reflections: Mirrors, or disable Mirrors of Skyrim in your mod manager.\n\n"
				L"Click OK to close Skyrim before game data is loaded.",
				L"Mirrors of Skyrim - startup check failed", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
			::ExitProcess(ERROR_MOD_NOT_FOUND);
		}
		return true;
	}
}
