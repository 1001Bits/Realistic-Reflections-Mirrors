#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Owner, 2026-09-17: one ordered Automatic ladder, best to cheapest:
// 2048/60 -> 1024/60 -> 1024/30 -> 512/30. Recovery reverses that order.
// One controller owns both values; room detail and lighting stay configured.
// It observes engine source frames, never frame-generated Presents.
namespace MirrorAdaptiveQuality
{
	struct Quality { int resolution{}, refreshHz{}; };
	// Ascending quality lets pressure take exactly one step down at a time.
	inline constexpr std::array<Quality, 4> kLevels{ Quality{512, 30}, {1024, 30}, {1024, 60}, {2048, 60} };
	inline constexpr unsigned kInitialLevel = 3;
	inline constexpr unsigned kDefaultTargetFPS = 60;
	inline constexpr double kSettlingMs = 10000;
	inline constexpr double kLastResortPressureMs = 10000;
	constexpr unsigned TargetFPS(int value) noexcept
	{
		return static_cast<unsigned>((std::clamp)(value, 30, 144));
	}
	constexpr int ResolutionAt(unsigned level) noexcept
	{
		return kLevels[(std::min)(level, kInitialLevel)].resolution;
	}
	constexpr int RefreshAt(unsigned level) noexcept
	{
		return kLevels[(std::min)(level, kInitialLevel)].refreshHz;
	}

	// One instance belongs to the source-frame observer; all storage is inline.
	class Controller
	{
		struct Window
		{
			double duration{};
			unsigned samples{}, late{};
			void Include(double interval, double budget) noexcept
			{
				duration += interval;
				++samples;
				if (interval > 1.15 * budget) ++late;
			}
		};
		struct State
		{
			unsigned quality{kInitialLevel}, slowWindows{};
			std::uint64_t publication{};
			double previous{-1}, latestCapture{-1}, settleUntil{kSettlingMs};
			double downStreak{}, upStreak{}, changeAfter{}, raiseAfter{};
			Window window{};
		} state_;
		double frameBudget_{1000.0 / kDefaultTargetFPS};

		void Suspend(double now) noexcept
		{
			state_.settleUntil = now + kSettlingMs;
			state_.window = {};
			state_.slowWindows = 0;
			state_.downStreak = state_.upStreak = 0;
		}
		void Evaluate(double now) noexcept
		{
			const auto batch = state_.window;
			state_.window = {};
			const double mean = batch.duration / batch.samples;
			const bool slow = mean > 1.10 * frameBudget_ && batch.late * 5 >= batch.samples;
			const bool fast = mean <= 1.05 * frameBudget_ && batch.late * 20 <= batch.samples;
			if (slow) {
				++state_.slowWindows;
				state_.downStreak += batch.duration;
			} else {
				state_.slowWindows = 0;
				state_.downStreak = 0;
			}
			state_.upStreak = fast ? state_.upStreak + batch.duration : 0;
			if (now < state_.changeAfter) return;
			const bool lowestFallbackReady = state_.quality != 1 || state_.downStreak >= kLastResortPressureMs;
			if (state_.quality > 0 && state_.slowWindows >= 2 && lowestFallbackReady) {
				state_.quality -= 1;
				state_.raiseAfter = now + 60000;
				state_.slowWindows = 0;
				state_.downStreak = 0;
			} else {
				if (state_.quality == kInitialLevel || state_.upStreak < 20000 || now < state_.raiseAfter) return;
				state_.quality += 1;
			}
			state_.upStreak = 0;
			state_.changeAfter = now + 2000;
		}
	public:
		void Reset(unsigned targetFPS) noexcept
		{
			state_ = State{};
			frameBudget_ = 1000.0 / TargetFPS(static_cast<int>(targetFPS));
		}
		unsigned Level() const noexcept { return state_.quality; }
		unsigned Observe(double nowMs, bool active, std::uint64_t publications) noexcept
		{
			if (std::isfinite(nowMs)) {
				const double interval = nowMs - state_.previous;
				state_.previous = nowMs;
				if (publications != state_.publication) state_.latestCapture = nowMs;
				state_.publication = publications;
				const bool freshCapture = state_.latestCapture >= 0 && nowMs - state_.latestCapture <= 500;
				if (!active || !freshCapture || interval <= 0 || interval > 250) {
					Suspend(nowMs);
				} else if (nowMs >= state_.settleUntil) {
					state_.window.Include(interval, frameBudget_);
					if (state_.window.duration >= 500 && state_.window.samples >= 8) Evaluate(nowMs);
				}
			}
			return Level();
		}
	};
}
