#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace MirrorPerformance
{
class Hotkeys
{
public:
    // Owner 2026-09-17: F8 panel, F11 mirrors. Community Shaders' default F10
    // event is never touched.
    static constexpr std::uint32_t kMenuScanCode = 0x42, kRenderingScanCode = 0x57;
    // Owner 2026-09-17: "add a development key that switches all cuts off F7".
    static constexpr std::uint32_t kCutsScanCode = 0x41;
    // Core run 9: F6 switches captures to the lighter 1.25x supersample, for an
    // A/B the owner judges by eye.
    static constexpr std::uint32_t kSharpnessScanCode = 0x40;
    static constexpr std::uint32_t kBenchmarkScanCode = 0x3E;
    static constexpr unsigned kMenu = 1, kRendering = 2, kCuts = 4, kSharpness = 8, kBenchmark = 16;
    // Skyrim ButtonEvent IDs are DirectInput scan codes, not Win32 virtual keys.
    // The engine supplies IsDown only on the first pressed frame; held/release
    // events are consumed without queuing a second action.
    bool Button(bool keyboard, std::uint32_t scanCode, bool initialDown,
        bool focused, bool modified, bool panelOpen = false) noexcept
    {
        if (!focused) { Clear(); return false; }
        if (!keyboard || modified) return false;
        const auto bit = scanCode == kMenuScanCode ? kMenu :
            scanCode == kRenderingScanCode ? kRendering :
            scanCode == kCutsScanCode ? kCuts :
            scanCode == kSharpnessScanCode ? kSharpness :
            scanCode == kBenchmarkScanCode && panelOpen ? kBenchmark : 0;
        if (!bit) return false;
        if (initialDown) pending_.fetch_xor(bit);
        return true;
    }
    void Clear() noexcept { pending_.store(0); }
    unsigned Take() noexcept { return pending_.exchange(0); }
private:
    std::atomic_uint pending_{};
};

// Built-in F8 benchmark (2026-09-18): mirrors off first, then Low / Medium /
// High. Quality steps keep reflected sun shadows. No extra resolutions or
// draw-distance steps. Internal capture cuts stay on F7/F6.
enum class StepKind : std::uint8_t { kOff, kPreset, kManual };
struct BenchmarkStep
{
    std::string_view name;
    StepKind kind{};
    std::uint8_t level{};         // preset: 0 Low, 1 Medium, 2 High
    int resolution{};             // manual only
    int refreshHz{};              // manual only; 0 = every frame
    int objectDistance{};         // 0 = product default (no prune); else enable the slider
    bool shadows{ true };         // reflected sun shadows for this step
    bool tightCull{ true };       // cull only what the pane can show
    bool lightCache{ true };      // per-capture light candidates
    bool rosterFilter{ true };    // replay without references already drawn
    bool screenSize{ true };      // capture at the mirror's on-screen size
    bool rectangle{ true };       // ... shaped like the pane
    bool listReuse{ true };       // nearby-reference list reused between captures
    bool lightChoice{ true };     // light choice reused between captures
    bool roomCull{ true };        // skip rooms the mirror cannot see into
    bool lightSharpness{ false }; // 1.25x supersample instead of 1.5x
    bool driftAnchor{ false };    // repeated Medium used to correct scene drift
};
[[nodiscard]] constexpr BenchmarkStep OffStep(std::string_view name) noexcept
{
    return { .name = name, .kind = StepKind::kOff, .shadows = false };
}
[[nodiscard]] constexpr BenchmarkStep PresetStep(
    std::string_view name, std::uint8_t level, bool driftAnchor = false, bool shadows = true,
    int objectDistance = 0) noexcept
{
    return { .name = name, .kind = StepKind::kPreset, .level = level,
        .objectDistance = objectDistance, .shadows = shadows, .driftAnchor = driftAnchor };
}
inline constexpr int kReducedObjectDistance = 2000; // kept for a later distance pass; unused this schedule
inline constexpr std::size_t kBenchmarkStepCount = 4;
[[nodiscard]] consteval std::array<BenchmarkStep, kBenchmarkStepCount> MakeBenchmarkSteps() noexcept
{
    std::array<BenchmarkStep, kBenchmarkStepCount> steps{};
    steps[0] = OffStep("Mirrors off");
    steps[1] = PresetStep("Low", 0);
    steps[2] = PresetStep("Medium", 1);
    steps[3] = PresetStep("High", 2);
    return steps;
}
inline constexpr auto kBenchmarkSteps = MakeBenchmarkSteps();
static_assert(kBenchmarkSteps.size() == 4);
static_assert(kBenchmarkSteps.front().name == "Mirrors off");
static_assert(kBenchmarkSteps.front().kind == StepKind::kOff);
static_assert(kBenchmarkSteps[1].name == "Low" && kBenchmarkSteps[1].shadows && kBenchmarkSteps[1].objectDistance == 0);
static_assert(kBenchmarkSteps[2].name == "Medium" && kBenchmarkSteps[2].shadows);
static_assert(kBenchmarkSteps.back().name == "High" && kBenchmarkSteps.back().shadows);
static_assert(kBenchmarkSteps[1].objectDistance == 0 && kBenchmarkSteps[2].objectDistance == 0 &&
    kBenchmarkSteps[3].objectDistance == 0);
[[nodiscard]] constexpr double DriftT(std::size_t index, std::size_t first, std::size_t last) noexcept
{
    if (last <= first || index <= first)
        return 0.0;
    if (index >= last)
        return 1.0;
    return double(index - first) / double(last - first);
}
[[nodiscard]] constexpr double DriftCorrected(double value, double first, double last, double t) noexcept
{
    return value - (last - first) * t;
}

// Countdown, then settle + measure per step. Time only advances while the
// world is eligible (unpaused, focused, not loading), so a menu pauses it.
class BenchmarkClock
{
public:
    static constexpr std::uint64_t kCountdown = 4'000'000, kSettle = 3'000'000, kMeasure = 8'000'000;
    enum class Event : std::uint8_t { kNone, kApplyStep, kBeginMeasure, kEndMeasure, kFinished };

    void Start() noexcept
    {
        cursor_ = 0;
        remaining_ = kCountdown;
        enabled_ = true;
    }
    void Stop() noexcept { enabled_ = false; }
    bool Running() const noexcept { return enabled_; }
    std::size_t Step() const noexcept
    {
        return cursor_ == 0 ? 0 : (cursor_ - 1) / 3;
    }
    bool Measuring() const noexcept { return enabled_ && cursor_ % 3 == 2; }
    bool CountingDown() const noexcept { return enabled_ && cursor_ == 0; }
    std::uint64_t Remaining() const noexcept { return remaining_; }

    Event Advance(std::uint64_t delta, bool eligible) noexcept
    {
        if (!enabled_) return Event::kNone;
        if (eligible) remaining_ -= delta < remaining_ ? delta : remaining_;
        if (remaining_ != 0) return Event::kNone;
        if (cursor_ == 3 * kBenchmarkSteps.size()) {
            Stop();
            return Event::kFinished;
        }
        // Each row has three boundaries: apply, begin measurement, end measurement.
        // Countdowns consume eligible time; the end boundary consumes none.
        constexpr std::array<Event, 3> events{
            Event::kEndMeasure, Event::kApplyStep, Event::kBeginMeasure };
        constexpr std::array<std::uint64_t, 3> waits{0, kSettle, kMeasure};
        const auto boundary = (++cursor_) % events.size();
        remaining_ = waits[boundary];
        return events[boundary];
    }
private:
    std::uint64_t remaining_{};
    std::size_t cursor_{};
    bool enabled_{};
};

struct Average
{
    std::uint64_t frames{}, microseconds{};
    double FPS() const noexcept { return microseconds ? double(frames) * 1'000'000 / double(microseconds) : 0; }
    double Milliseconds() const noexcept { return frames ? double(microseconds) / double(frames) / 1000 : 0; }
    double Seconds() const noexcept { return double(microseconds) / 1'000'000; }
    void Add(std::uint64_t interval) noexcept { ++frames; microseconds += interval; }
};

// Frame-time distribution (owner, 2026-09-17: the average hides the one slow
// capture frame in three). 0.1 ms buckets to 200 ms; longer frames share the
// last bucket, so a percentile there reads "200 ms or more".
class Histogram
{
public:
    static constexpr std::uint32_t kBucketMicroseconds = 100;
    static constexpr std::size_t kBuckets = 2000;
    void Add(std::uint64_t microseconds) noexcept
    {
        const auto bucket = microseconds / kBucketMicroseconds;
        ++buckets_[bucket < kBuckets ? static_cast<std::size_t>(bucket) : kBuckets - 1];
        ++count_;
    }
    // Upper edge of the bucket holding the p-th sample, in milliseconds.
    double PercentileMs(double p) const noexcept
    {
        if (!count_ || !(p >= 0.0) || p > 1.0) return 0.0;
        const auto rank = static_cast<std::uint64_t>(p * double(count_ - 1) + 0.5) + 1;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            seen += buckets_[i];
            if (seen >= rank) return double((i + 1) * kBucketMicroseconds) / 1000.0;
        }
        return double(kBuckets * kBucketMicroseconds) / 1000.0;
    }
    std::uint64_t Count() const noexcept { return count_; }
private:
    std::array<std::uint32_t, kBuckets> buckets_{};
    std::uint64_t count_{};
};

// Present-return cadence, not GPU duration or monitor scan-out. Keep the latest
// settled visit to each mode, so repeated comparisons do not mix old locations.
class Meter
{
public:
    void Switch(bool mirrors, std::uint64_t now) noexcept
    {
        mode_ = mirrors; modes_[mirrors] = {}; histograms_[mirrors] = {}; live_ = {}; window_ = {};
        settleDelay_ = 1'000'000; last_ = 0; settleUntil_ = now + settleDelay_;
    }
    // The benchmark has already settled for three seconds. Start counting at
    // this exact boundary rather than silently dropping another second.
    void BeginMeasurement(bool mirrors, std::uint64_t now, bool prime = true) noexcept
    {
        Switch(mirrors, now);
        settleDelay_ = 0; settleUntil_ = now; last_ = prime ? now : 0;
    }
    void Observe(std::uint64_t now, bool eligible) noexcept
    {
        valid_ = eligible;
        if (!eligible || (last_ && now <= last_)) {
            last_ = 0; live_ = {}; window_ = {}; settleUntil_ = now + settleDelay_; return;
        }
        const auto previous = last_; last_ = now;
        if (!previous || previous < settleUntil_) return;
        const auto elapsed = now - previous;
        modes_[mode_].Add(elapsed); histograms_[mode_].Add(elapsed); window_.Add(elapsed);
        if (window_.microseconds >= 500'000) { live_ = window_; window_ = {}; }
    }
    Average Mode(bool mirrors) const noexcept { return modes_[mirrors]; }
    double PercentileMs(bool mirrors, double p) const noexcept { return histograms_[mirrors].PercentileMs(p); }
    Average Live() const noexcept { return live_; }
    bool Valid() const noexcept { return valid_; }
    bool Mode() const noexcept { return mode_; }
private:
    Average modes_[2]{}, live_{}, window_{};
    Histogram histograms_[2]{};
    std::uint64_t last_{}, settleUntil_{}, settleDelay_{1'000'000};
    bool mode_{true}, valid_{};
};
}
