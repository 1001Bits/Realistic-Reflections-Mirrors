#pragma once

namespace RE
{
	class BSTimer
	{
	public:
		[[nodiscard]] static BSTimer* GetSingleton() noexcept
		{
			static REL::Relocation<BSTimer*> singleton{ RELOCATION_ID(523657, 410196) };
			return singleton.get();
		}

		static float QGlobalTimeMultiplier()
		{
			static REL::Relocation<float*> value{ RELOCATION_ID(511882, 388442) };
			return *value;
		}

		static float QGlobalTimeMultiplierTarget()
		{
			static REL::Relocation<float*> value{ RELOCATION_ID(511883, 388443) };
			return *value;
		}

		void SetGlobalTimeMultiplier(float a_multiplier, bool a_arg2)
		{
			using func_t = decltype(&BSTimer::SetGlobalTimeMultiplier);
			static REL::Relocation<func_t> func{ RELOCATION_ID(66988, 68245) };
			return func(this, a_multiplier, a_arg2);
		}

		// members
		std::uint64_t unk00;                 // 00
		std::uint32_t lastPerformanceCount;  // 08
#if !defined(ENABLE_SKYRIM_VR) || defined(ENABLE_SKYRIM_SE) || defined(ENABLE_SKYRIM_AE)
		std::uint32_t pad0C;  // 0C - not present in VR-only builds
#endif
		float         clamp;                          // 10 (0C in VR)
		float         clampRemainder;                 // 14 (10 in VR)
		float         delta;                          // 18 (14 in VR)
		float         realTimeDelta;                  // 1C (18 in VR)
		std::uint32_t unk20;                          // 20 (1C in VR)
		std::uint32_t unk24;                          // 24 (20 in VR)
		float         unk28;                          // 28 (24 in VR)
		std::uint32_t unk2C;                          // 2C (28 in VR)
		std::uint32_t unk30;                          // 30 (2C in VR)
		std::uint32_t unk34;                          // 34 (30 in VR)
		std::uint8_t  unk38;                          // 38 (34 in VR)
		std::uint8_t  unk39;                          // 39 (35 in VR)
		bool          useGlobalTimeMultiplierTarget;  // 3A (36 in VR)
		std::uint8_t  pad3B;                          // 3B (37 in VR)
		std::uint32_t pad3C;                          // 3C (38 in VR)
	};
	static_assert(sizeof(BSTimer) == 0x40);
}
