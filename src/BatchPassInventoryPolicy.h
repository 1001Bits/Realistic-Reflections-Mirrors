#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace BatchPassInventoryPolicy
{
	/** Native flat BSBatchRenderer stores these records inline, not pointers to
	 * records. AE 1.7.104 RegisterPass at RVA 0x155EFA0 indexes +8 by index*0x30.
	 * The five pass identities are opaque; inventory must never dereference them.
	 */
	struct PassGroup
	{
		std::array<std::uintptr_t, 5> passes{};
		std::uint32_t validPassBits{};
		std::uint32_t padding{};
	};
	static_assert(sizeof(PassGroup) == 0x30);
	static_assert(offsetof(PassGroup, validPassBits) == 0x28);

	struct Counts
	{
		std::uint32_t groups{};
		std::uint32_t slots{};
	};

	[[nodiscard]] constexpr Counts Inspect(std::span<const PassGroup> groups) noexcept
	{
		Counts result;
		for (const auto& group : groups) {
			if (group.validPassBits == 0)
				continue;
			++result.groups;
			for (const auto pass : group.passes)
				result.slots += pass != 0 ? 1u : 0u;
		}
		return result;
	}
}
