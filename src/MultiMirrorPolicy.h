#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace MultiMirrorPolicy
{
	// Sentinels are independent of the number of standing mirrors.
	inline constexpr std::size_t kNone = (std::numeric_limits<std::size_t>::max)();
	inline constexpr std::size_t kHand = kNone - 1;

	[[nodiscard]] constexpr bool WallBridgeLeaseSatisfied(bool independentSlots,
		bool handEnabled, std::uint64_t legacyLease) noexcept
	{
		return independentSlots || !handEnabled || legacyLease != 0;
	}

	struct Identity
	{
		std::uint32_t formID{};
		std::uint64_t generation{};
		constexpr bool operator==(const Identity&) const noexcept = default;
		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return formID != 0 && generation != 0;
		}
	};

	struct FrameCapture
	{
		std::size_t slot{ kNone };
		Identity mirror{};
	};

	struct FramePlan
	{
		std::uint64_t source{};
		std::vector<FrameCapture> captures{};
		std::size_t count{};
	};

	/** Stable slots grow with the visible set; ranking never exchanges images. */
	class Scheduler
	{
	public:
		/** Retention and current rendering demand have distinct lifetimes. An OOM
		 * disables filtering, keeping visible mirrors eligible for this source. */
		bool SetCaptureDemand(std::span<const Identity> demand, bool enabled) noexcept
		{
			demandFiltering_ = false;
			if (!enabled) return true;
			try {
				demand_.assign(demand.begin(), demand.end());
				std::sort(demand_.begin(), demand_.end(), [](Identity a, Identity b) {
					return a.formID < b.formID || (a.formID == b.formID && a.generation < b.generation);
				});
				demandFiltering_ = true;
				return true;
			} catch (...) { return false; }
		}

		[[nodiscard]] bool HasCaptureDemand(Identity identity) const noexcept
		{
			if (!demandFiltering_) return true;
			return std::binary_search(demand_.begin(), demand_.end(), identity, [](Identity a, Identity b) {
				return a.formID < b.formID || (a.formID == b.formID && a.generation < b.generation);
			});
		}
		/** Allocation failure leaves every prior identity intact. Scratch capacity
		 * is retained so an unchanged scene requires no per-frame allocation.
		 */
		bool Reconcile(std::span<const Identity> ranked, bool /*handEquipped*/) noexcept
		{
			try {
				admitted_.clear();
				admitted_.reserve(ranked.size());
				for (const auto candidate : ranked)
					if (candidate.Valid())
						admitted_.push_back(candidate);
				std::sort(admitted_.begin(), admitted_.end(), [](Identity a, Identity b) {
					return a.formID < b.formID || (a.formID == b.formID && a.generation > b.generation);
				});
				admitted_.erase(std::unique(admitted_.begin(), admitted_.end(), [](Identity a, Identity b) {
					return a.formID == b.formID;
				}), admitted_.end());
				nextSlots_ = slots_;
				nextSlots_.resize((std::max)(slots_.size(), admitted_.size()));
				// Mark retained identities in scratch, leaving only new ones to insert.
				for (auto& slot : nextSlots_) {
					const auto found = std::lower_bound(admitted_.begin(), admitted_.end(), slot.formID,
						[](Identity a, std::uint32_t formID) { return a.formID < formID; });
					if (slot.Valid() && found != admitted_.end() && *found == slot)
						found->generation = 0;
					else
						slot = {};
				}
				auto empty = nextSlots_.begin();
				for (const auto candidate : admitted_) {
					if (!candidate.Valid())
						continue;
					empty = std::find_if(empty, nextSlots_.end(), [](Identity a) { return !a.Valid(); });
					*empty++ = candidate;
				}
				while (!nextSlots_.empty() && !nextSlots_.back().Valid())
					nextSlots_.pop_back();
				slots_.swap(nextSlots_);
				return true;
			} catch (...) {
				return false;
			}
		}

		[[nodiscard]] std::size_t Choose(std::uint64_t source, bool handEligible) noexcept
		{
			if (source == 0 || source <= lastSource_)
				return kNone;
			lastSource_ = source;
			const auto choices = slots_.size() + 1;
			next_ %= choices;
			for (std::size_t step = 0; step < choices; ++step) {
				const auto index = next_;
				next_ = (next_ + 1) % choices;
				if (index == slots_.size()) {
					if (handEligible)
						return kHand;
				} else if (slots_[index].Valid() && HasCaptureDemand(slots_[index])) {
					return index;
				}
			}
			return kNone;
		}

		/** One attempt per admitted mirror per source. Hand finalization runs last.
		 * The source guard also prevents a second attempt after allocation failure.
		 */
		bool ChooseEveryFrame(std::uint64_t source, bool handEligible, FramePlan& plan) noexcept
		{
			plan.source = 0;
			plan.count = 0;
			plan.captures.clear();
			if (source == 0 || source <= lastSource_)
				return false;
			lastSource_ = source;
			try {
				plan.captures.reserve(slots_.size() + (handEligible ? 1 : 0));
				for (std::size_t slot = 0; slot < slots_.size(); ++slot)
					if (slots_[slot].Valid() && HasCaptureDemand(slots_[slot]))
						plan.captures.push_back({ slot, slots_[slot] });
				if (handEligible)
					plan.captures.push_back({ kHand, {} });
				plan.source = source;
				plan.count = plan.captures.size();
				return true;
			} catch (...) {
				plan.captures.clear();
				return false;
			}
		}

		[[nodiscard]] FramePlan ChooseEveryFrame(std::uint64_t source, bool handEligible) noexcept
		{
			FramePlan plan{};
			ChooseEveryFrame(source, handEligible, plan);
			return plan;
		}

		[[nodiscard]] std::size_t Find(Identity identity) const noexcept
		{
			if (identity.Valid()) {
				for (std::size_t i = 0; i < slots_.size(); ++i)
					if (slots_[i] == identity)
						return i;
			}
			return kNone;
		}
		[[nodiscard]] const auto& Slots() const noexcept { return slots_; }
		void Reset() noexcept
		{
			slots_.clear();
			nextSlots_.clear();
			admitted_.clear();
			demand_.clear();
			demandFiltering_ = false;
			next_ = 0;
			lastSource_ = 0;
		}

	private:
		std::vector<Identity> slots_{}, nextSlots_{}, admitted_{};
		std::vector<Identity> demand_{};
		bool demandFiltering_{ false };
		std::size_t next_{};
		std::uint64_t lastSource_{};
	};
}
