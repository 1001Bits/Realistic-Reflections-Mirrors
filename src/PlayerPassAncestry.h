#pragma once

#include <cstddef>

namespace PlayerPassAncestry
{
	enum class Result
	{
		kMatch,
		kNoMatch,
		kTruncated,
		kMalformed
	};

	/**
	 * Bounded parent-chain classification used by the render-pass probe. The
	 * caller owns pointer validation/SEH because engine nodes can disappear on
	 * crash-sensitive render paths; this helper only guarantees finite walking
	 * and rejects an immediate self-cycle.
	 */
	template <class Node, class ParentAccessor>
	[[nodiscard]] Result Classify(
		const Node* candidate,
		const Node* root,
		std::size_t maximumDepth,
		ParentAccessor&& parentAccessor) noexcept
	{
		if (!candidate || !root || maximumDepth == 0)
			return Result::kNoMatch;

		auto* cursor = candidate;
		for (std::size_t depth = 0; depth < maximumDepth; ++depth) {
			if (cursor == root)
				return Result::kMatch;

			auto* parent = parentAccessor(cursor);
			if (!parent)
				return Result::kNoMatch;
			if (parent == cursor)
				return Result::kMalformed;
			cursor = parent;
		}
		return Result::kTruncated;
	}
}
