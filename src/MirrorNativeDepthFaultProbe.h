#pragma once

#include <cstdint>

namespace MirrorNativeDepthFaultProbe
{
	enum class DSVProvenance : std::uint8_t
	{
		kUnavailable,
		kPrivateTarget,
		kEngineTable,
		kPrivateEngineAlias,
		kOther
	};

	struct Capture
	{
		std::uintptr_t privateDSV{ 0 };
		std::uintptr_t engineTableDSV{ 0 };
		std::uint32_t depthIndex{ 0 };
		std::uint32_t depthSlice{ 0 };
		bool engineSlotCaptured{ false };
	};

	[[nodiscard]] constexpr bool IsEngineSlotBounded(
		const std::uint32_t depthIndex,
		const std::uint32_t depthSlice,
		const std::uint32_t depthTargetCount) noexcept
	{
		return depthTargetCount != 0 && depthIndex < depthTargetCount && depthSlice < 8;
	}

	[[nodiscard]] constexpr DSVProvenance Classify(
		const std::uintptr_t faultRDX,
		const Capture& capture) noexcept
	{
		if (faultRDX == 0 || capture.privateDSV == 0)
			return DSVProvenance::kUnavailable;

		const bool privateMatch = faultRDX == capture.privateDSV;
		const bool engineMatch = capture.engineSlotCaptured &&
			capture.engineTableDSV != 0 && faultRDX == capture.engineTableDSV;
		if (privateMatch && engineMatch)
			return DSVProvenance::kPrivateEngineAlias;
		if (privateMatch)
			return DSVProvenance::kPrivateTarget;
		if (engineMatch)
			return DSVProvenance::kEngineTable;
		return DSVProvenance::kOther;
	}

	[[nodiscard]] constexpr const char* Name(const DSVProvenance provenance) noexcept
	{
		switch (provenance) {
		case DSVProvenance::kUnavailable: return "unavailable";
		case DSVProvenance::kPrivateTarget: return "private-target";
		case DSVProvenance::kEngineTable: return "engine-table";
		case DSVProvenance::kPrivateEngineAlias: return "private-engine-alias";
		case DSVProvenance::kOther: return "other";
		default: return "unknown";
		}
	}
}
