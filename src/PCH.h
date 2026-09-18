#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "NoDestructor.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <detours/detours.h>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include "SupportedRuntimePolicy.h"

namespace logger = SKSE::log;
namespace fs = std::filesystem;

namespace stl
{
	// Microsoft Detours function-start hook; T provides
	//   static <ret> thunk(args...)  and  static inline REL::Relocation<decltype(thunk)> func;
	template <class T>
	[[nodiscard]] bool detour_thunk(REL::RelocationID a_relId)
	{
		T::func = a_relId.address();
		if (DetourTransactionBegin() != NO_ERROR)
			return false;
		if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) {
			DetourTransactionAbort();
			return false;
		}
		if (DetourAttach(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk)) != NO_ERROR) {
			DetourTransactionAbort();
			return false;
		}
		return DetourTransactionCommit() == NO_ERROR;
	}
}

#ifndef RR_VERSION_STR
#	define RR_VERSION_STR "1.0.0"
#endif
