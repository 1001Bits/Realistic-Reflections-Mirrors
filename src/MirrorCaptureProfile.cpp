#include "PCH.h"
#include "MirrorCaptureProfile.h"

#include <algorithm>
#include <mutex>
#include "MirrorPerformancePolicy.h"

namespace MirrorCaptureProfile
{
	namespace
	{
		constexpr std::size_t kParts = static_cast<std::size_t>(Part::kCount);
		constexpr auto kBuckets = MirrorPerformance::Histogram::kBuckets;
		constexpr auto kBucketMicroseconds = MirrorPerformance::Histogram::kBucketMicroseconds;

		[[nodiscard]] std::uint64_t Frequency() noexcept
		{
			static const std::uint64_t frequency = [] {
				LARGE_INTEGER value{};
				return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ?
					static_cast<std::uint64_t>(value.QuadPart) : 0;
			}();
			return frequency;
		}

		std::atomic<std::uint64_t> captures{}, captureMicroseconds{};
		std::atomic<std::uint64_t> playerDrawFrames{}, playerMissingFrames{};
		std::array<std::atomic<std::uint64_t>, kParts> partMicroseconds{};
		std::array<std::atomic<std::uint64_t>, kBuckets> histogram{};
		std::atomic<std::uint64_t> maximumMicroseconds{};
		std::atomic<std::uint64_t> windowStart{};
		std::atomic<bool (*)(double&, std::uint64_t&) noexcept> gpuSource{ nullptr };
		std::mutex baseMutex;
		double gpuBaseMs{};
		std::uint64_t gpuBaseSamples{};

		thread_local bool inCapture{};
		thread_local std::array<std::uint64_t, kParts> captureTicks{};

		void ReadGpu(double& totalMs, std::uint64_t& samples) noexcept
		{
			totalMs = 0.0;
			samples = 0;
			if (auto* source = gpuSource.load(std::memory_order_acquire))
				(void)source(totalMs, samples);
		}
	}

	std::uint64_t Ticks() noexcept
	{
		LARGE_INTEGER value{};
		return QueryPerformanceCounter(&value) && value.QuadPart > 0 ?
			static_cast<std::uint64_t>(value.QuadPart) : 0;
	}

	std::uint64_t Microseconds(std::uint64_t ticks) noexcept
	{
		const auto frequency = Frequency();
		return frequency ? ticks * 1'000'000 / frequency : 0;
	}

	void BeginCapture() noexcept
	{
		if (!Active())
			return;
		inCapture = true;
		captureTicks = {};
	}

	bool InCapture() noexcept
	{
		return inCapture;
	}

	void RecordPlayerDraw(bool confirmed) noexcept
	{
		if (inCapture) (confirmed ? playerDrawFrames : playerMissingFrames).fetch_add(1, std::memory_order_relaxed);
	}

	void AddTicks(Part part, std::uint64_t ticks) noexcept
	{
		const auto index = static_cast<std::size_t>(part);
		if (inCapture && index < kParts)
			captureTicks[index] += ticks;
	}

	void EndCapture(std::uint64_t microseconds) noexcept
	{
		if (!inCapture)
			return;
		inCapture = false;
		if (!Active())
			return;
		captures.fetch_add(1, std::memory_order_relaxed);
		captureMicroseconds.fetch_add(microseconds, std::memory_order_relaxed);
		for (std::size_t i = 0; i < kParts; ++i)
			partMicroseconds[i].fetch_add(Microseconds(captureTicks[i]), std::memory_order_relaxed);
		const auto bucket = (std::min)(microseconds / kBucketMicroseconds, std::uint64_t(kBuckets - 1));
		histogram[bucket].fetch_add(1, std::memory_order_relaxed);
		// Capture completion and Reset both run at render/Present boundaries.
		if (microseconds > maximumMicroseconds.load(std::memory_order_relaxed))
			maximumMicroseconds.store(microseconds, std::memory_order_relaxed);
	}

	void Reset(std::uint64_t nowMicroseconds) noexcept
	{
		captures.store(0, std::memory_order_relaxed);
		playerDrawFrames.store(0, std::memory_order_relaxed);
		playerMissingFrames.store(0, std::memory_order_relaxed);
		captureMicroseconds.store(0, std::memory_order_relaxed);
		for (auto& part : partMicroseconds)
			part.store(0, std::memory_order_relaxed);
		for (auto& bucket : histogram) bucket.store(0, std::memory_order_relaxed);
		maximumMicroseconds.store(0, std::memory_order_relaxed);
		windowStart.store(nowMicroseconds, std::memory_order_relaxed);
		double totalMs{};
		std::uint64_t samples{};
		ReadGpu(totalMs, samples);
		std::scoped_lock lock(baseMutex);
		gpuBaseMs = totalMs;
		gpuBaseSamples = samples;
	}

	Summary Read(std::uint64_t nowMicroseconds) noexcept
	{
		Summary summary{};
		summary.captures = captures.load(std::memory_order_relaxed);
		summary.playerDrawFrames = playerDrawFrames.load(std::memory_order_relaxed);
		summary.playerMissingFrames = playerMissingFrames.load(std::memory_order_relaxed);
		const auto start = windowStart.load(std::memory_order_relaxed);
		summary.seconds = nowMicroseconds > start ? static_cast<double>(nowMicroseconds - start) / 1'000'000.0 : 0.0;
		if (summary.captures) {
			const double count = static_cast<double>(summary.captures);
			summary.captureMs = static_cast<double>(captureMicroseconds.load(std::memory_order_relaxed)) / count / 1000.0;
			double parts = 0.0;
			for (std::size_t i = 0; i < kParts; ++i)
				summary.partMs[i] = static_cast<double>(partMicroseconds[i].load(std::memory_order_relaxed)) / count / 1000.0;
			parts = summary.partMs[static_cast<std::size_t>(Part::kCull)] +
				summary.partMs[static_cast<std::size_t>(Part::kDraw)] +
				summary.partMs[static_cast<std::size_t>(Part::kDepth)] +
				summary.partMs[static_cast<std::size_t>(Part::kRoster)];
			summary.otherMs = (std::max)(0.0, summary.captureMs - parts);
			// Whole measurement window, no allocation/sort on every overlay frame.
			const auto rank95 = static_cast<std::uint64_t>((count - 1.0) * 0.95 + 0.5) + 1;
			const auto rank99 = static_cast<std::uint64_t>((count - 1.0) * 0.99 + 0.5) + 1;
			std::uint64_t seen = 0;
			for (std::size_t i = 0; i < kBuckets; ++i) {
				seen += histogram[i].load(std::memory_order_relaxed);
				const double edge = double((i + 1) * kBucketMicroseconds) / 1000.0;
				if (!summary.captureP95Ms && seen >= rank95) summary.captureP95Ms = edge;
				if (seen >= rank99) { summary.captureP99Ms = edge; break; }
			}
			summary.captureMaxMs = double(maximumMicroseconds.load(std::memory_order_relaxed)) / 1000.0;
		}
		double totalMs{};
		std::uint64_t samples{};
		ReadGpu(totalMs, samples);
		std::scoped_lock lock(baseMutex);
		if (samples > gpuBaseSamples) {
			summary.gpuSamples = samples - gpuBaseSamples;
			summary.gpuMs = (totalMs - gpuBaseMs) / static_cast<double>(summary.gpuSamples);
		}
		return summary;
	}

	void SetGpuSource(bool (*source)(double& totalMs, std::uint64_t& samples) noexcept) noexcept
	{
		gpuSource.store(source, std::memory_order_release);
	}
}
