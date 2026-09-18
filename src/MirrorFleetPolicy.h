#pragma once

#include "MultiMirrorPolicy.h"
#include "MirrorCaptureAdmission.h"
#include <array>
#include <atomic>
#include <cstring>
#include <unordered_map>

namespace MirrorFleetPolicy
{
	// InputLoaded only: new render behaviour remains available for a controlled A/B run.
	inline std::atomic_bool enabled{ false };
	inline constexpr std::array<int, 8> kResolutions{ 512, 1024, 1536, 2048, 2560, 3072, 3584, 4096 };
	inline std::atomic<int> resolution{ 4096 }, refreshHz{ 0 };
	// Hand mirror captures per second (0 = every visible source frame). Run 21
	// (2026-09-14): the hand captured on 41,681 of 42,470 frames at 23-32 ms CPU
	// each, the largest single per-frame cost; the placed refresh never applied.
	inline std::atomic<int> handRefreshHz{ 30 };
	// Owner 2026-09-14: separate cap while the hand mirror is lowered (0 = every frame).
	inline std::atomic<int> handLoweredRefreshHz{ 30 };
	inline std::atomic_bool dynamicResolution{ true };
	// Automatic quality (MirrorAdaptiveQuality) writes here, never into
	// `resolution`, so the manual value in the INI survives a spell in Automatic
	// mode; 0 means no adaptive override is active. While it is active the legacy
	// visible-count dynamic resolution stands down too: two controllers on one
	// dial would fight.
	inline std::atomic<int> adaptiveResolution{ 0 };
	[[nodiscard]] inline int EffectiveResolution() noexcept
	{
		const int adaptive = adaptiveResolution.load(std::memory_order_relaxed);
		return adaptive ? adaptive : resolution.load(std::memory_order_relaxed);
	}
	// Automatic mode's paired update rate (MirrorAdaptiveQuality), kept
	// apart from `refreshHz` for the same reason; 0 means no override.
	inline std::atomic<int> adaptiveRefreshHz{ 0 };
	[[nodiscard]] inline int EffectiveRefresh() noexcept
	{
		const int adaptive = adaptiveRefreshHz.load(std::memory_order_relaxed);
		return adaptive ? adaptive : refreshHz.load(std::memory_order_relaxed);
	}
	[[nodiscard]] inline bool EffectiveDynamic() noexcept
	{
		return dynamicResolution.load(std::memory_order_relaxed) &&
		       adaptiveResolution.load(std::memory_order_relaxed) == 0;
	}
	inline std::atomic_bool paneAlignedCamera{ false };
	[[nodiscard]] constexpr int Resolution(int value) noexcept
	{
		return value >= 512 && value <= 4096 && value % 512 == 0 ? value : 4096;
	}
	// Count visible world mirrors before refresh/admission filtering; the hand
	// mirror has separate targets. One selection is frozen for the whole batch.
	struct FrameQuality
	{
		std::size_t visibleMirrors{};
		int manualResolution{ 4096 };
		int resolution{ 4096 };
		bool dynamic{ true };
	};
	[[nodiscard]] constexpr FrameQuality SelectQuality(
		std::size_t count, bool dynamic, int manual) noexcept
	{
		const int normalized = Resolution(manual);
		const int automatic = count <= 2 ? 4096 : count <= 4 ? 2048 : count <= 8 ? 1024 : 512;
		return { count, normalized, dynamic ? automatic : normalized, dynamic };
	}
	[[nodiscard]] constexpr int Refresh(int value) noexcept
	{
		// Owner rule (2026-09-16): a configured cadence is never below 30 Hz. A
		// stored/manual value under the floor normalizes to 30; zero keeps its
		// meaning of every source frame.
		return value == 0 ? 0 : (std::clamp)(value, 30, 120);
	}
	[[nodiscard]] constexpr std::uint64_t Interval(int hz) noexcept
	{
		return hz == 0 ? 0 : (1'000'000ULL + Refresh(hz) / 2) / Refresh(hz);
	}
	[[nodiscard]] constexpr std::uint64_t ColorBytes(std::uint32_t width, std::uint32_t height) noexcept
	{
		std::uint64_t result = 0;
		while (width && height) {
			result += std::uint64_t{width} * height * 8;
			if (width==1 && height==1) break;
			width=(std::max)(1u,width/2); height=(std::max)(1u,height/2);
		}
		return result;
	}
	[[nodiscard]] constexpr std::uint64_t ColorBytes(std::uint32_t size) noexcept { return ColorBytes(size,size); }
	[[nodiscard]] inline MirrorCaptureAdmission::Policy AdmissionPolicy() noexcept
	{
		MirrorCaptureAdmission::Policy policy;
		// Geometry is rechecked on every scheduled sample. The old 125 ms
		// timing threshold permanently restarted warmup at low FPS or 5 Hz.
		policy.maximumSampleGapMilliseconds = (std::numeric_limits<std::int64_t>::max)();
		policy.maximumSequenceGap = 1; // reference-local scheduled sample serial
		return policy;
	}

	struct Clock
	{
		std::uint64_t next{}, last{}, interval{};
		bool started{};
		[[nodiscard]] bool Due(std::uint64_t now, std::uint64_t requested) const noexcept
		{
			return !started || requested != interval || now < last || requested == 0 || now >= next;
		}
		void Attempt(std::uint64_t now, std::uint64_t requested) noexcept
		{
			if (!started || requested != interval || now < last || requested == 0)
				next = now + requested;
			else if (now >= next)
				next += ((now - next) / requested + 1) * requested;
			last = now; interval = requested; started = true;
		}
		void Wake() noexcept { started = false; }
	};

	struct Entry
	{
		MultiMirrorPolicy::Identity identity{};
		Clock clock{};
		std::uint64_t planned{}, attempts{}, successes{}, failures{}, consecutiveFailures{};
		std::uint64_t lastAttemptSource{}, lastSuccessSource{}, firstFailureSource{}, lastFailureSource{};
		std::uint64_t cpuMicroseconds{}, lastSuccessMicroseconds{}, lastExpiredCapture{};
		const char* firstFailure{ "none" };
		const char* lastFailure{ "none" };
		std::uint32_t width{};
		std::uint64_t admissionSource{}, admissionSequence{}, seenEpoch{};
		std::uint64_t resumeSource{}, resumeCapture{}, resumePresentations{};
		bool visible{};
	};

	/** One clock and failure history per reference lifetime, never per scratch worker. */
	class Fleet
	{
	public:
		bool Reconcile(std::span<const MultiMirrorPolicy::Identity> identities) noexcept
		{
			try {
				++epoch_;
				if (!epoch_) { for (auto& [id, e] : entries_) e.seenEpoch = 0; ++epoch_; }
				// Complete allocations before pruning any old owner.
				for (const auto id : identities) if (id.Valid()) {
					auto& entry = entries_[id.formID];
					if (entry.identity != id) entry = Entry{ .identity = id };
					entry.seenEpoch = epoch_;
				}
				std::erase_if(entries_, [&](const auto& item) {
					return item.second.seenEpoch != epoch_;
				});
				return true;
			} catch (...) { return false; }
		}
		Entry* Find(MultiMirrorPolicy::Identity id) noexcept
		{
			const auto it = entries_.find(id.formID);
			return it != entries_.end() && it->second.identity == id ? &it->second : nullptr;
		}
		bool Request(MultiMirrorPolicy::Identity id, bool visible, std::uint64_t now, int hz) noexcept
		{
			auto* entry = Find(id);
			if (!entry) return visible; // Allocation failure must not suppress a visible owner.
			if (visible && !entry->visible) {
				entry->clock.Wake();
				// Visibility is not a new identity. Preserve the sample serial so
				// a returning, geometrically revalidated mirror need not warm up again.
			}
			entry->visible = visible;
			return visible && entry->clock.Due(now, Interval(hz));
		}
		void Planned(MultiMirrorPolicy::Identity id, std::uint64_t now, int hz) noexcept
		{
			if (auto* e = Find(id)) { e->clock.Attempt(now, Interval(hz)); ++e->planned; }
		}
		void Result(MultiMirrorPolicy::Identity id, std::uint64_t source, std::uint64_t now,
			bool attempted, bool success, const char* failure, std::uint64_t cpu, std::uint32_t width) noexcept
		{
			if (auto* e = Find(id)) {
				e->attempts += attempted; e->lastAttemptSource = source; e->cpuMicroseconds += cpu;
				if (success) {
					++e->successes; e->lastSuccessSource = source; e->lastSuccessMicroseconds = now;
					e->consecutiveFailures = 0; e->width = width;
				} else if (failure && std::strcmp(failure, "mirrorAdmissionWarmups") == 0) {
					// Three-sample admission settling is expected, not a capture fault.
				} else {
					++e->failures; ++e->consecutiveFailures;
					if (e->consecutiveFailures == 1) { e->firstFailureSource = source; e->firstFailure = failure; }
					e->lastFailure = failure; e->lastFailureSource = source;
				}
			}
		}
		void Expired(MultiMirrorPolicy::Identity id, std::uint64_t capture) noexcept
		{
			if (auto* e = Find(id); e && capture != e->lastExpiredCapture) {
				e->lastExpiredCapture = capture; e->clock.Wake();
			}
		}
		const auto& Entries() const noexcept { return entries_; }
		bool ResumeImage(MultiMirrorPolicy::Identity id, std::uint64_t source,
			std::uint64_t captureSource, std::uint64_t capture) noexcept
		{
			auto* e = Find(id);
			if (!e || !source || !captureSource || captureSource >= source || !capture) return false;
			// Delivery runs before this frame's capture planning. An idle owner can
			// reuse its exact image for at most three returning frames, once per
			// capture. Repeated failed draws cannot keep extending the grace period.
			if (!e->visible && e->resumeCapture != capture) {
				e->resumeSource = source; e->resumeCapture = capture; e->clock.Wake();
			}
			const bool allowed = e->resumeCapture == capture && e->resumeSource &&
				captureSource < e->resumeSource && source >= e->resumeSource && source - e->resumeSource < 3;
			e->resumePresentations += allowed;
			return allowed;
		}
		std::uint64_t AdmissionSequence(MultiMirrorPolicy::Identity id, std::uint64_t source) noexcept
		{
			auto* e = Find(id);
			if (!e || !source) return 0;
			if (e->admissionSource != source) {
				e->admissionSource = source;
				if (++e->admissionSequence == 0) ++e->admissionSequence;
			}
			return e->admissionSequence;
		}
		void Reset() noexcept { entries_.clear(); }
	private:
		std::unordered_map<std::uint32_t, Entry> entries_;
		std::uint64_t epoch_{};
	};
}
