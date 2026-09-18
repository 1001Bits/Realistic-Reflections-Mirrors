#pragma once

#include <cstdint>

namespace RE
{
	class BSNavmesh;

	struct AStarNode
	{
		float         g_cost;                    // 00
		float         h_cost;                    // 04
		BSNavmesh*    navMesh;                   // 08
		std::uint16_t triangleIndex;             // 10
		std::uint16_t pad12;                     // 12
		std::uint32_t pad14;                     // 14
		float         pad18[(0x58 - 0x18) / 4];  // 18
		AStarNode*    next;                      // 58
	};
	static_assert(sizeof(AStarNode) == 0x60, "size");
}
