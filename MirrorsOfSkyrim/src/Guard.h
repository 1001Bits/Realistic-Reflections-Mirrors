#pragma once

#include <Windows.h>

namespace MOS::DependencyGuard
{
	using Notify = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);

	// Called by SKSEPlugin_Load, before WinMain and TESFile discovery. The
	// injectable notification is also used by the offline Windows test host.
	// Returns false only if disabling could not be established; the entry point
	// must then stop startup instead of continuing into a missing-master crash.
	[[nodiscard]] bool Initialize(Notify a_notify = ::MessageBoxW) noexcept;
}
