#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace MirrorCoverageDrawRecord
{
	// One geometry can have several material passes. Only an identical returned
	// dispatch is evidence that the replay's copy can be omitted.
	struct Key
	{
		std::uintptr_t geometry{};
		std::uintptr_t shader{};
		std::uint32_t technique{};
		std::uint8_t geometryMode{};
		constexpr bool operator==(const Key&) const noexcept = default;
	};

	template <std::size_t Capacity = 16384, class Generation = std::uint32_t>
	class Record
	{
		static_assert(Capacity >= 4 && (Capacity & (Capacity - 1)) == 0);
		static_assert(std::is_unsigned_v<Generation> && !std::is_same_v<Generation, bool>);
	public:
		enum class InsertResult { kIgnored, kInserted, kSaturated };
		// Capture cleanup runs even when no world coverage was recorded. Advancing
		// a generation avoids clearing 384 KiB of keys every time. On wrap, erase
		// all tags before reusing generation one; stale draws must never suppress
		// current geometry. Keys are values only and own no engine resources.
		void Clear() noexcept
		{
			if (++generation_ == 0) { generations_ = {}; generation_ = 1; }
			count_ = 0;
			saturated_ = false;
		}
		[[nodiscard]] bool Contains(Key key) const noexcept
		{
			if (!key.geometry || !key.shader) return false;
			auto slot = Slot(key);
			for (std::size_t i = 0; i < Capacity; ++i) {
				if (generations_[slot] != generation_) return false;
				if (entries_[slot] == key) return true;
				slot = (slot + 1) & (Capacity - 1);
			}
			return false;
		}
		InsertResult Insert(Key key) noexcept
		{
			if (!key.geometry || !key.shader || saturated_) return InsertResult::kIgnored;
			auto slot = Slot(key);
			for (std::size_t i = 0; i < Capacity; ++i) {
				if (generations_[slot] == generation_ && entries_[slot] == key) return InsertResult::kIgnored;
				if (generations_[slot] != generation_) {
					if (count_ >= Capacity * 3 / 4) {
						saturated_ = true;
						return InsertResult::kSaturated;
					}
					entries_[slot] = key;
					generations_[slot] = generation_;
					++count_;
					return InsertResult::kInserted;
				}
				slot = (slot + 1) & (Capacity - 1);
			}
			return InsertResult::kIgnored;
		}
	private:
		static std::size_t Slot(Key key) noexcept
		{
			return ((key.geometry >> 4) ^ (key.geometry >> 21) ^
				(key.shader >> 4) ^ key.technique ^ key.geometryMode) & (Capacity - 1);
		}
		std::array<Key, Capacity> entries_{};
		std::array<Generation, Capacity> generations_{};
		Generation generation_{1};
		std::size_t count_{};
		bool saturated_{};
	};
}
