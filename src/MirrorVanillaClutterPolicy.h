#pragma once
#include <array>
#include <cstdint>

// Vanilla REFRs the core master overrides. An existing save can keep the old
// pose after the ESM changes, so the runtime writes each override until that
// live ref is enabled at the authored pose (XESP enable can restore the save).
namespace MirrorVanillaClutterPolicy
{
	enum class Action : std::uint8_t { kDisable, kRelocate };
	struct Record
	{
		std::uint32_t formId{};
		Action action{ Action::kDisable };
		float x{};
		float y{};
		float z{};
		float yaw{};
	};
	inline constexpr std::array kRecords{
		Record{ 0x000CB979, Action::kRelocate, -330.0F, -4134.0F, 226.6858673095703F, 0.0F },
		Record{ 0x000DF5CE, Action::kRelocate, 2576.265380859375F, -654.46630859375F, 397.396484375F, 4.71238899230957F },
		Record{ 0x000C4257, Action::kRelocate, -4352.3486328125F, -1378.42578125F, 362.6692199707031F, 0.0F },
		Record{ 0x000CA010, Action::kDisable },
		Record{ 0x000CA0B3, Action::kDisable },
	};
}
