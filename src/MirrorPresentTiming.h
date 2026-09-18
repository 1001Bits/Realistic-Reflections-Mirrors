#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace MirrorFleetRuntime
{
	/** Fixed-cost collection; an overflow never masquerades as whole-window quantiles. */
	template <std::size_t Capacity = 4096>
	class PresentTiming
	{
	public:
		struct Summary
		{
			std::uint64_t intervals{};
			double average{}, maximum{}, p50{}, p95{}, p99{};
			bool percentilesAvailable{};
		};
		void Observe(std::uint64_t now, bool successful = true) noexcept
		{
			if (!successful) { last_ = 0; return; }
			if (last_ && now > last_) {
				const double ms = static_cast<double>(now - last_) / 1000.0;
				if (count_ < times_.size()) times_[count_] = ms;
				++count_; sum_ += ms; maximum_ = (std::max)(maximum_, ms);
			}
			last_ = now;
		}
		Summary Take() noexcept
		{
			Summary result{count_, count_ ? sum_ / count_ : 0.0, maximum_};
			if (count_ && count_ <= times_.size()) {
				std::sort(times_.begin(), times_.begin() + count_);
				// Nearest rank, including the maximum for single-sample windows.
				const auto percentile = [&](std::uint64_t p) {
					return times_[(count_ * p + 99) / 100 - 1];
				};
				result.p50 = percentile(50); result.p95 = percentile(95); result.p99 = percentile(99);
				result.percentilesAvailable = true;
			}
			count_ = 0; sum_ = maximum_ = 0.0;
			return result;
		}
	private:
		std::array<double, Capacity> times_{};
		std::uint64_t last_{}, count_{};
		double sum_{}, maximum_{};
	};
}
