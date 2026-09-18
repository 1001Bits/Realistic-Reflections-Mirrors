#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

/**
 * Placed/hand captures render two native cycles into one target: the
 * supplemental cycle (actors plus every near reference around the pane,
 * submitted all-pass) and then the borrowed main-scene list replay. A near
 * reference that also sits in the borrowed lists was drawn twice. Opaque
 * duplicates land at equal depth and are harmless; alpha-blended duplicates are
 * not: the second copy blends over the first (double glass tint) and any
 * blended part behind a depth-writing blended surface of the first copy fails
 * the depth test (alchemy liquid behind its outer glass, owner runs 10-20,
 * 2026-09-14). Whether a reference enters the near roster depends on the pane
 * position, which is why the hand mirror flapped as it moved.
 *
 * The replay's draw of any geometry whose ancestor root is in the supplemental
 * roster is therefore skipped at the synchronous draw seam; the supplemental
 * copy (complete, all-pass, natively ordered) is the one that survives.
 */
namespace MirrorReplayDuplicatePolicy
{
	inline constexpr std::size_t kMaximumAncestorDepth = 32;

	/** Roster membership by pointer identity over a sorted, unique roster. */
	[[nodiscard]] inline bool Contains(
		std::span<const std::uintptr_t> a_sortedRoster, std::uintptr_t a_root) noexcept
	{
		if (a_root == 0 || a_sortedRoster.empty())
			return false;
		return std::binary_search(a_sortedRoster.begin(), a_sortedRoster.end(), a_root);
	}

	/** Sort in place and drop duplicates/nulls; returns the retained count. */
	[[nodiscard]] inline std::size_t Normalize(std::span<std::uintptr_t> a_roster) noexcept
	{
		std::sort(a_roster.begin(), a_roster.end());
		auto last = std::unique(a_roster.begin(), a_roster.end());
		auto first = a_roster.begin();
		while (first != last && *first == 0)
			++first;
		if (first != a_roster.begin()) {
			last = std::copy(first, last, a_roster.begin());
		}
		return static_cast<std::size_t>(last - a_roster.begin());
	}

	/**
	 * Walk a_node and its ancestors (a_parentOf(node) -> parent or null) up to
	 * kMaximumAncestorDepth levels; true when any ancestor (including a_node
	 * itself) is a roster root. A malformed or cyclic chain is bounded by the
	 * depth limit and reports no membership.
	 */
	template <class Node, class ParentOf>
	[[nodiscard]] bool AncestorInRoster(
		const Node* a_node, std::span<const std::uintptr_t> a_sortedRoster,
		ParentOf a_parentOf) noexcept
	{
		if (a_sortedRoster.empty())
			return false;
		const Node* node = a_node;
		for (std::size_t depth = 0; node && depth < kMaximumAncestorDepth; ++depth) {
			if (Contains(a_sortedRoster, reinterpret_cast<std::uintptr_t>(node)))
				return true;
			node = a_parentOf(node);
		}
		return false;
	}
}
