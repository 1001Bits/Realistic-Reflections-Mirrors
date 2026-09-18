#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace MirrorMerchantStock
{
	enum class Result { kUnchanged, kAdded, kInvalid, kAllocationFailed, kRemoved };

	// Used only at DataLoaded. Prepare every allocation before publishing either
	// pointer or count; existing entries and their ownership data are retained.
	template <class Entry, class Object, class Allocator>
	Result EnsurePair(Entry**& entries, std::uint32_t& count,
		const std::array<Object*, 2>& wanted, Allocator& allocator,
		std::array<Entry*, 2>* owned = nullptr) noexcept
	{
		if ((count && !entries) || !wanted[0] || !wanted[1] || wanted[0] == wanted[1] ||
			count > std::numeric_limits<std::uint32_t>::max() - 2)
			return Result::kInvalid;
		std::array<Object*, 2> missing{};
		std::uint32_t added = 0;
		for (auto* object : wanted) {
			bool found = false;
			for (std::uint32_t i = 0; i < count; ++i) {
				if (entries[i] && entries[i]->obj == object) {
					found = true;
					break;
				}
			}
			// Even a zero-count entry may be intentional in the winning override.
			if (!found)
				missing[added++] = object;
		}
		if (!added)
			return Result::kUnchanged;
		auto** replacement = allocator.AllocateList(count + added);
		if (!replacement)
			return Result::kAllocationFailed;
		for (std::uint32_t i = 0; i < count; ++i)
			replacement[i] = entries[i];
		for (std::uint32_t i = 0; i < added; ++i) {
			replacement[count + i] = allocator.MakeEntry(missing[i]);
			if (!replacement[count + i]) {
				for (std::uint32_t j = 0; j < i; ++j)
					allocator.FreeEntry(replacement[count + j]);
				allocator.FreeList(replacement);
				return Result::kAllocationFailed;
			}
		}
		auto** previous = entries;
		if (owned) for (std::uint32_t i = 0; i < added; ++i)
			for (std::size_t j = 0; j < wanted.size(); ++j)
				if (missing[i] == wanted[j]) (*owned)[j] = replacement[count + i];
		entries = replacement;
		count += added;
		allocator.FreeList(previous);
		return Result::kAdded;
	}

	// Remove only our exact, unchanged contributions. A foreign replacement or
	// edit becomes foreign ownership; never subtract from that mod's entry.
	template <class Entry, class Allocator>
	Result RemoveOwned(Entry**& entries, std::uint32_t& count,
		std::array<Entry*, 2>& owned, Allocator& allocator) noexcept
	{
		if (count && !entries) return Result::kInvalid;
		std::array<Entry*, 2> remove{};
		std::uint32_t removed = 0;
		for (auto* candidate : owned) {
			if (!candidate) continue;
			for (std::uint32_t i = 0; i < count; ++i) {
				if (entries[i] == candidate && allocator.IsUnmodified(candidate)) {
					remove[removed++] = candidate; break;
				}
			}
		}
		if (!removed) { owned = {}; return Result::kUnchanged; }
		auto** replacement = count == removed ? nullptr : allocator.AllocateList(count - removed);
		if (count != removed && !replacement) return Result::kAllocationFailed;
		std::uint32_t next = 0;
		for (std::uint32_t i = 0; i < count; ++i)
			if (entries[i] != remove[0] && (removed < 2 || entries[i] != remove[1]))
				replacement[next++] = entries[i];
		auto** previous = entries;
		entries = replacement; count = next; owned = {};
		allocator.FreeList(previous);
		for (std::uint32_t i = 0; i < removed; ++i) allocator.FreeEntry(remove[i]);
		return Result::kRemoved;
	}
}
