#pragma once
#include <array>
#include <cstdint>

namespace MirrorFleetRuntime
{
	struct CapturePoolShapeDiagnostics
	{
		std::uint32_t width{}, height{};
		std::uint64_t allocations{}, reuseHits{}, evictions{}, failures{};
		std::uint64_t allocationMicroseconds{}, maximumAllocationMicroseconds{};
		bool operator==(const CapturePoolShapeDiagnostics&) const = default;
	};
	using CapturePoolDiagnostics = std::array<CapturePoolShapeDiagnostics, 64>;
}
