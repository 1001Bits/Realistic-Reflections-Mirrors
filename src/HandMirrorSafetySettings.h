#pragma once

#include <Windows.h>
#include "MirrorFeatures.h"

// Shipped defaults are internal. Other test opt-ins retain the legacy empty
// regular-file check; configuration is still latched before rendering.
namespace HandMirrorSafetySettings
{
	inline bool EmptyMarker(const wchar_t* path) noexcept
	{
		if (MirrorFeatures::Enabled(path)) return true;
		WIN32_FILE_ATTRIBUTE_DATA data{};
		return GetFileAttributesExW(path, GetFileExInfoStandard, &data) &&
			(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
				FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) == 0 &&
			data.nFileSizeHigh == 0 && data.nFileSizeLow == 0;
	}

	inline bool StandaloneSE() noexcept
	{
		static const bool enabled = EmptyMarker(
			L"Data\\MirrorsOfSkyrim_HandMirrorStandaloneSE.enable");
		return enabled;
	}

	inline bool HeadClearance() noexcept
	{
		static const bool enabled = EmptyMarker(
			L"Data\\MirrorsOfSkyrim_HandMirrorHeadClearance.enable");
		return enabled;
	}

	inline bool FaceCenter() noexcept
	{
		static const bool enabled = EmptyMarker(
			L"Data\\MirrorsOfSkyrim_HandFaceCenter.enable");
		return enabled;
	}

	inline bool PortraitClipPlane() noexcept
	{
		static const bool enabled = EmptyMarker(
			L"Data\\MirrorsOfSkyrim_HandPortraitClipPlane.enable");
		return enabled;
	}

	inline bool SceneryShadows() noexcept
	{
		static const bool enabled = EmptyMarker(
			L"Data\\MirrorsOfSkyrim_SceneryShadows.enable");
		return enabled;
	}

}
