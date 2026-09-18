#include "PCH.h"
#include "MirrorBenchmark.h"

#include "HandMirrorSettings.h"
#include "MirrorCaptureOptimizations.h"
#include "MirrorCaptureProfile.h"
#include "MirrorCaptureSizing.h"
#include "MirrorFleetPolicy.h"
#include "MirrorPerformance.h"
#include "MirrorPerformancePolicy.h"
#include "MirrorQualityPreset.h"
#include "MirrorRenderDistance.h"
#include "MirrorShadowSettings.h"
#include "MirrorScreenSizePolicy.h"
#include "SecondView.h"

#include <cstdio>

namespace MirrorBenchmark
{
	namespace
	{
		using MirrorPerformance::kBenchmarkSteps;
		using MirrorPerformance::StepKind;
		using Event = MirrorPerformance::BenchmarkClock::Event;

		// Everything a step may change, captured before the first step and put
		// back exactly afterwards. Nothing here is written to the INI.
		struct Saved
		{
			std::uint32_t mode{}, level{};
			int resolution{}, refreshHz{}, handRefreshHz{}, handLoweredRefreshHz{};
			bool dynamicResolution{};
			bool handShadows{}, placedShadows{};
			int handDistance{}, placedDistance{}, handShadowDistance{}, placedShadowDistance{};
			bool distanceControls{};
			int raisedResolution{}, loweredResolution{};
			bool rendering{};
			bool tightCull{}, lightCache{}, rosterFilter{}, screenSize{}, rectangle{}, listReuse{},
				lightChoice{}, roomCull{}, lightSharpness{};
		};

		struct Result
		{
			bool valid{};
			bool playerComplete{};
			bool rendering{};
			int resolution{};
			int captureSize{}, captureHeight{};  // the last placed capture actually rendered
			int refreshHz{};
			std::uint64_t frames{};
			double frameMs{}, p50Ms{}, p95Ms{}, p99Ms{};
			double correctedFrameMs{}, correctedP95Ms{}, correctedP99Ms{};
			double correctedCaptureMs{}, correctedGpuMs{};
			std::uint64_t sourceFrames{};
			double sourceFrameMs{}, sourceP95Ms{}, sourceP99Ms{};
			MirrorCaptureProfile::Summary capture{};
		};

		std::atomic_bool requested{ false };
		MirrorPerformance::BenchmarkClock clock;
		MirrorPerformance::Meter meter, sourceMeter;
		Saved saved{};
		bool haveSaved{};
		bool showing{};
		bool finished{};
		const char* abortReason{};
		std::uint64_t lastNow{}, lastSource{}, measurementUs{};
		bool measurementEligible{}, previousEligible{};
		std::array<Result, kBenchmarkSteps.size()> results{};
		double driftMs{};
		bool driftCorrected{};

		void Save() noexcept
		{
			saved.mode = MirrorQualityPreset::mode.load();
			saved.level = MirrorQualityPreset::level.load();
			saved.resolution = MirrorFleetPolicy::resolution.load();
			saved.refreshHz = MirrorFleetPolicy::refreshHz.load();
			saved.handRefreshHz = MirrorFleetPolicy::handRefreshHz.load();
			saved.handLoweredRefreshHz = MirrorFleetPolicy::handLoweredRefreshHz.load();
			saved.dynamicResolution = MirrorFleetPolicy::dynamicResolution.load();
			saved.handShadows = MirrorShadowSettings::hand.load();
			saved.placedShadows = MirrorShadowSettings::placed.load();
			saved.handDistance = MirrorRenderDistance::hand.load();
			saved.placedDistance = MirrorRenderDistance::placed.load();
			saved.handShadowDistance = MirrorRenderDistance::handShadow.load();
			saved.placedShadowDistance = MirrorRenderDistance::placedShadow.load();
			saved.distanceControls = MirrorRenderDistance::controls.load();
			saved.raisedResolution = HandMirrorSettings::RaisedResolution();
			saved.loweredResolution = HandMirrorSettings::LoweredResolution();
			saved.rendering = MirrorPerformance::rendering.load();
			saved.tightCull = MirrorCaptureOptimizations::tightPaneCull.load();
			saved.lightCache = MirrorCaptureOptimizations::lightCandidateCache.load();
			saved.rosterFilter = MirrorCaptureOptimizations::replayRosterFilter.load();
			saved.screenSize = MirrorCaptureOptimizations::screenSizedCapture.load();
			saved.rectangle = MirrorCaptureOptimizations::rectangularCapture.load();
			saved.listReuse = MirrorCaptureOptimizations::rosterListReuse.load();
			saved.lightChoice = MirrorCaptureOptimizations::lightChoiceCache.load();
			saved.roomCull = MirrorCaptureOptimizations::roomCull.load();
			saved.lightSharpness = MirrorScreenSizePolicy::reducedSupersample.load();
			haveSaved = true;
		}

		void Restore() noexcept
		{
			if (!haveSaved)
				return;
			haveSaved = false;
			MirrorQualityPreset::mode.store(saved.mode);
			MirrorQualityPreset::level.store(saved.level);
			MirrorFleetPolicy::resolution.store(saved.resolution);
			MirrorFleetPolicy::refreshHz.store(saved.refreshHz);
			MirrorFleetPolicy::handRefreshHz.store(saved.handRefreshHz);
			MirrorFleetPolicy::handLoweredRefreshHz.store(saved.handLoweredRefreshHz);
			MirrorFleetPolicy::dynamicResolution.store(saved.dynamicResolution);
			MirrorShadowSettings::hand.store(saved.handShadows);
			MirrorShadowSettings::placed.store(saved.placedShadows);
			MirrorRenderDistance::hand.store(saved.handDistance);
			MirrorRenderDistance::placed.store(saved.placedDistance);
			MirrorRenderDistance::handShadow.store(saved.handShadowDistance);
			MirrorRenderDistance::placedShadow.store(saved.placedShadowDistance);
			MirrorRenderDistance::controls.store(saved.distanceControls);
			(void)HandMirrorSettings::SetRaisedResolution(saved.raisedResolution);
			(void)HandMirrorSettings::SetLoweredResolution(saved.loweredResolution);
			MirrorPerformance::rendering.store(saved.rendering, std::memory_order_release);
			MirrorCaptureOptimizations::tightPaneCull.store(saved.tightCull);
			MirrorCaptureOptimizations::lightCandidateCache.store(saved.lightCache);
			MirrorCaptureOptimizations::replayRosterFilter.store(saved.rosterFilter);
			MirrorCaptureOptimizations::screenSizedCapture.store(saved.screenSize);
			MirrorCaptureOptimizations::rectangularCapture.store(saved.rectangle);
			MirrorCaptureOptimizations::rosterListReuse.store(saved.listReuse);
			MirrorCaptureOptimizations::lightChoiceCache.store(saved.lightChoice);
			MirrorCaptureOptimizations::roomCull.store(saved.roomCull);
			MirrorScreenSizePolicy::reducedSupersample.store(saved.lightSharpness);
		}

		void Apply(const MirrorPerformance::BenchmarkStep& step) noexcept
		{
			if (step.kind != StepKind::kOff) {
				// A preset first, so every setting a step does not name has one
				// known value; manual steps then override resolution and cadence.
				MirrorQualityPreset::mode.store(static_cast<std::uint32_t>(MirrorQualityPreset::Mode::kPreset));
				MirrorQualityPreset::level.store(step.level);
				MirrorQualityPreset::Apply();
				if (step.kind == StepKind::kManual) {
					MirrorQualityPreset::mode.store(static_cast<std::uint32_t>(MirrorQualityPreset::Mode::kManual));
					MirrorFleetPolicy::resolution.store(MirrorFleetPolicy::Resolution(step.resolution));
					MirrorFleetPolicy::refreshHz.store(MirrorFleetPolicy::Refresh(step.refreshHz));
				}
				MirrorShadowSettings::hand.store(step.shadows);
				MirrorShadowSettings::placed.store(step.shadows);
				if (step.objectDistance > 0) {
					MirrorRenderDistance::controls.store(true);
					(void)MirrorRenderDistance::Set(true, step.objectDistance);
					(void)MirrorRenderDistance::Set(false, step.objectDistance);
				} else {
					MirrorRenderDistance::controls.store(false);
				}
			}
			MirrorCaptureOptimizations::tightPaneCull.store(step.tightCull);
			MirrorCaptureOptimizations::lightCandidateCache.store(step.lightCache);
			MirrorCaptureOptimizations::replayRosterFilter.store(step.rosterFilter);
			MirrorCaptureOptimizations::screenSizedCapture.store(step.screenSize);
			MirrorCaptureOptimizations::rectangularCapture.store(step.rectangle);
			MirrorCaptureOptimizations::rosterListReuse.store(step.listReuse);
			MirrorCaptureOptimizations::lightChoiceCache.store(step.lightChoice);
			MirrorCaptureOptimizations::roomCull.store(step.roomCull);
			MirrorScreenSizePolicy::reducedSupersample.store(step.lightSharpness);
			MirrorPerformance::rendering.store(step.kind != StepKind::kOff, std::memory_order_release);
		}

		void CorrectDrift() noexcept
		{
			driftMs = 0;
			driftCorrected = false;
			std::size_t first = kBenchmarkSteps.size();
			std::size_t last = 0;
			unsigned anchors = 0;
			for (std::size_t i = 0; i < results.size(); ++i) {
				if (!results[i].valid || !results[i].playerComplete || !kBenchmarkSteps[i].driftAnchor)
					continue;
				if (anchors == 0)
					first = i;
				last = i;
				++anchors;
			}
			const bool correct = anchors >= 2 && last > first;
			if (correct) {
				driftMs = results[last].frameMs - results[first].frameMs;
				driftCorrected = true;
			}
			for (std::size_t i = 0; i < results.size(); ++i) {
				auto& r = results[i];
				if (!r.valid)
					continue;
				if (!correct) {
					r.correctedFrameMs = r.frameMs;
					r.correctedP95Ms = r.p95Ms;
					r.correctedP99Ms = r.p99Ms;
					r.correctedCaptureMs = r.capture.captureMs;
					r.correctedGpuMs = r.capture.gpuMs;
					continue;
				}
				const auto t = MirrorPerformance::DriftT(i, first, last);
				r.correctedFrameMs = MirrorPerformance::DriftCorrected(r.frameMs, results[first].frameMs, results[last].frameMs, t);
				r.correctedP95Ms = MirrorPerformance::DriftCorrected(r.p95Ms, results[first].p95Ms, results[last].p95Ms, t);
				r.correctedP99Ms = MirrorPerformance::DriftCorrected(r.p99Ms, results[first].p99Ms, results[last].p99Ms, t);
				r.correctedCaptureMs = MirrorPerformance::DriftCorrected(r.capture.captureMs, results[first].capture.captureMs, results[last].capture.captureMs, t);
				r.correctedGpuMs = MirrorPerformance::DriftCorrected(r.capture.gpuMs, results[first].capture.gpuMs, results[last].capture.gpuMs, t);
			}
		}

		void Log(std::size_t index, const Result& r) noexcept
		{
			try {
				const auto& step = kBenchmarkSteps[index];
				const auto& c = r.capture;
				using Part = MirrorCaptureProfile::Part;
				const auto part = [&c](Part p) { return c.partMs[static_cast<std::size_t>(p)]; };
				logger::info(
					"[MOS][Benchmark] step={} name=\"{}\" rendering={} resolution={} captureSize={}x{} refreshHz={} shadows={} objectDistance={} distanceControls={} framebufferSized={} tightCull={} lightCache={} rosterFilter={} screenSized={} rectangle={} listReuse={} lightChoice={} roomCull={} lightSharpness={} frames={} frameMs={:.2f} p50Ms={:.2f} p95Ms={:.2f} p99Ms={:.2f} captures={} capturesPerSecond={:.1f} captureMs={:.2f} captureP95Ms={:.2f} cullMs={:.2f} drawMs={:.2f} engineDrawMs={:.2f} hooksMs={:.2f} lightsMs={:.2f} depthMs={:.2f} rosterMs={:.2f} otherMs={:.2f} gpuMs={:.2f} gpuSamples={} captureP99Ms={:.2f} captureMaxMs={:.2f} sourceFrames={} sourceFrameMs={:.2f} sourceP95Ms={:.2f} sourceP99Ms={:.2f} playerDrawFrames={} playerMissingFrames={} playerComplete={}",
					index + 1, step.name, r.rendering, r.resolution, r.captureSize, r.captureHeight, r.refreshHz, step.shadows, step.objectDistance, MirrorRenderDistance::controls.load(), MirrorCaptureSizing::UsesFramebuffer(),
					step.tightCull, step.lightCache, step.rosterFilter, step.screenSize, step.rectangle, step.listReuse,
					step.lightChoice, step.roomCull, step.lightSharpness, r.frames, r.frameMs, r.p50Ms, r.p95Ms, r.p99Ms,
					c.captures, c.seconds > 0 ? static_cast<double>(c.captures) / c.seconds : 0.0,
					c.captureMs, c.captureP95Ms, part(Part::kCull), part(Part::kDraw),
					(std::max)(0.0, part(Part::kDraw) - part(Part::kHooks)), part(Part::kHooks), part(Part::kLights),
					part(Part::kDepth), part(Part::kRoster), c.otherMs, c.gpuMs, c.gpuSamples,
					c.captureP99Ms, c.captureMaxMs, r.sourceFrames, r.sourceFrameMs, r.sourceP95Ms, r.sourceP99Ms, c.playerDrawFrames, c.playerMissingFrames, r.playerComplete);
			} catch (...) {
			}
		}

		void Handle(Event event, std::uint64_t now) noexcept
		{
			const auto index = clock.Step();
			switch (event) {
			case Event::kApplyStep:
				Apply(kBenchmarkSteps[index]);
				break;
			case Event::kBeginMeasure:
				meter = MirrorPerformance::Meter{};
				meter.BeginMeasurement(MirrorPerformance::rendering.load(), now);
				sourceMeter.BeginMeasurement(MirrorPerformance::rendering.load(), now, false);
				measurementUs = 0;
				MirrorCaptureProfile::Reset(now);
				break;
			case Event::kEndMeasure: {
				auto& r = results[index];
				const bool rendering = MirrorPerformance::rendering.load();
				const auto average = meter.Mode(rendering);
				r.valid = true;
				r.rendering = rendering;
				r.resolution = MirrorFleetPolicy::EffectiveResolution();
				r.captureSize = SecondView::LastPlacedCaptureResolution();
				r.captureHeight = SecondView::LastPlacedCaptureHeight();
				r.refreshHz = MirrorFleetPolicy::refreshHz.load();
				r.frames = average.frames;
				r.frameMs = average.Milliseconds();
				r.p50Ms = meter.PercentileMs(rendering, 0.50);
				r.p95Ms = meter.PercentileMs(rendering, 0.95);
				r.p99Ms = meter.PercentileMs(rendering, 0.99);
				r.capture = MirrorCaptureProfile::Read(now);
                // Standing at the mirror is the benchmark contract. Never claim a
                // cheaper step with an unobserved/missing reflected player is valid.
                r.playerComplete = !rendering || (r.capture.captures != 0 &&
                    r.capture.playerMissingFrames == 0 &&
                    r.capture.playerDrawFrames == r.capture.captures);
				r.capture.seconds = double(measurementUs) / 1'000'000.0;
				const auto sourceAverage = sourceMeter.Mode(rendering);
				r.sourceFrames = sourceAverage.frames;
				r.sourceFrameMs = sourceAverage.Milliseconds();
				r.sourceP95Ms = sourceMeter.PercentileMs(rendering, 0.95);
				r.sourceP99Ms = sourceMeter.PercentileMs(rendering, 0.99);
				Log(index, r);
				CorrectDrift();
				break;
			}
			case Event::kFinished:
				CorrectDrift();
				Restore();
				finished = true;
				try {
					if (driftCorrected) {
						logger::info("[MOS][Benchmark] finished; every setting restored ({} steps); driftMs={:.2f}; overlay times are drift-corrected",
							kBenchmarkSteps.size(), driftMs);
						for (std::size_t i = 0; i < results.size(); ++i) {
							const auto& r = results[i];
							if (!r.valid)
								continue;
							logger::info(
								"[MOS][Benchmark] corrected step={} name=\"{}\" frameMs={:.2f} p95Ms={:.2f} p99Ms={:.2f} captureMs={:.2f} gpuMs={:.2f}",
								i + 1, kBenchmarkSteps[i].name, r.correctedFrameMs, r.correctedP95Ms, r.correctedP99Ms,
								r.correctedCaptureMs, r.correctedGpuMs);
						}
					} else {
						logger::info("[MOS][Benchmark] finished; every setting restored ({} steps); drift correction skipped",
							kBenchmarkSteps.size());
					}
				} catch (...) {
				}
				break;
			default:
				break;
			}
		}

		void Put(Line& line, const char* format, auto... arguments) noexcept
		{
			std::snprintf(line.data(), line.size(), format, arguments...);
		}
	}

	void Request() noexcept
	{
		requested.store(true, std::memory_order_release);
	}

	bool Running() noexcept
	{
		return clock.Running();
	}

	bool Showing() noexcept
	{
		return showing;
	}

	void Abort(const char* reason) noexcept
	{
		if (!clock.Running())
			return;
		clock.Stop();
		Restore();
		abortReason = reason;
		finished = true;
		try {
			logger::info("[MOS][Benchmark] stopped ({}); every setting restored", reason ? reason : "unknown");
		} catch (...) {
		}
	}

	void Dismiss() noexcept
	{
		if (clock.Running())
			Abort("panel closed");
		showing = false;
	}

	// The scene-list fence is an engine source-frame seam; generated Presents cannot add
	// samples here. A new window discards its first crossing interval.
	void OnSourceFrame(std::uint64_t source, std::uint64_t now) noexcept
	{
		if (source == lastSource) return;
		lastSource = source;
		if (clock.Measuring()) sourceMeter.Observe(now, measurementEligible);
	}

	void OnPresent(std::uint64_t now, bool eligible) noexcept
	{
		const auto delta = lastNow && now > lastNow && previousEligible && eligible ? now - lastNow : 0;
		lastNow = now;
		previousEligible = eligible;
		measurementEligible = eligible;
		if (!eligible && clock.Measuring()) sourceMeter.Observe(now, false);
		if (requested.exchange(false, std::memory_order_acq_rel) && !clock.Running()) {
			if (!MirrorPerformance::DebugHotkeysEnabled())
				return;
			Save();
			results = {};
			driftMs = 0;
			driftCorrected = false;
			finished = false;
			abortReason = nullptr;
			showing = true;
			clock.Start();
			try {
				logger::info("[MOS][Benchmark] started: {} steps, about {} s; stand still facing a mirror",
					kBenchmarkSteps.size(),
					(MirrorPerformance::BenchmarkClock::kCountdown + kBenchmarkSteps.size() *
						(MirrorPerformance::BenchmarkClock::kSettle + MirrorPerformance::BenchmarkClock::kMeasure)) / 1'000'000);
			} catch (...) {
			}
		}
		if (!clock.Running())
			return;
		if (!MirrorPerformance::DebugHotkeysEnabled()) {
			Abort("Development menu switched off");
			return;
		}
		meter.Observe(now, eligible);
		if (clock.Measuring()) measurementUs += delta;
		auto event = clock.Advance(delta, eligible);
		for (unsigned guard = 0; event != Event::kNone && guard < 8; ++guard) {
			Handle(event, now);
			event = clock.Advance(0, eligible);
		}
	}

	std::size_t Lines(std::span<Line> out, std::uint64_t) noexcept
	{
		if (out.size() < 4)
			return 0;
		std::size_t n = 0;
		Put(out[n++], "MIRRORS OF SKYRIM - BENCHMARK");
		const auto seconds = static_cast<unsigned>((clock.Remaining() + 999'999) / 1'000'000);
		if (clock.Running()) {
			const auto index = clock.Step();
			if (clock.CountingDown())
				Put(out[n++], "Starting in %u s: stand still, about %u min", seconds,
					static_cast<unsigned>((MirrorPerformance::BenchmarkClock::kCountdown + kBenchmarkSteps.size() *
						(MirrorPerformance::BenchmarkClock::kSettle + MirrorPerformance::BenchmarkClock::kMeasure) +
						59'999'999) / 60'000'000));
			else
				Put(out[n++], "Step %u/%u %.*s: %s, %u s", static_cast<unsigned>(index + 1),
					static_cast<unsigned>(kBenchmarkSteps.size()),
					static_cast<int>(kBenchmarkSteps[index].name.size()), kBenchmarkSteps[index].name.data(),
					clock.Measuring() ? "measuring" : "settling", seconds);
		} else if (abortReason) {
			Put(out[n++], "Stopped: %s (settings restored)", abortReason);
		} else if (finished) {
			if (driftCorrected)
				Put(out[n++], "Done - drift %+0.2f ms; times corrected (F8 closes)", driftMs);
			else
				Put(out[n++], "Done - settings restored; full table in the log (F8 closes)");
		}
		if (driftCorrected && n < out.size())
			Put(out[n++], "Times below are drift-corrected against Medium");
		if (n < out.size())
			Put(out[n++], "%u steps; full table in the log",
				static_cast<unsigned>(kBenchmarkSteps.size()));
		if (n >= out.size())
			return n;
		Put(out[n++], "%-36s %6s %6s %6s | %6s %6s", "Setting", "frame", "p95", "p99", "cpu", "gpu");
		auto emit = [&](std::size_t i) {
			if (n >= out.size() || i >= results.size() || !results[i].valid)
				return;
			const auto& r = results[i];
			const auto& name = kBenchmarkSteps[i].name;
			if (!r.playerComplete)
				Put(out[n++], "%-36.*s  player draw unverified; see log", static_cast<int>(name.size()), name.data());
			else if (r.capture.captures)
				Put(out[n++], "%-36.*s %6.2f %6.2f %6.2f | %6.2f %6.2f", static_cast<int>(name.size()), name.data(),
					r.correctedFrameMs, r.correctedP95Ms, r.correctedP99Ms, r.correctedCaptureMs, r.correctedGpuMs);
			else
				Put(out[n++], "%-36.*s %6.2f %6.2f %6.2f | %6s %6s", static_cast<int>(name.size()), name.data(),
					r.correctedFrameMs, r.correctedP95Ms, r.correctedP99Ms, "-", "-");
		};
		for (std::size_t i = 0; i < results.size() && n < out.size(); ++i)
			if (kBenchmarkSteps[i].kind == StepKind::kOff)
				emit(i);
		for (std::size_t i = 0; i < results.size() && n < out.size(); ++i)
			if (kBenchmarkSteps[i].kind == StepKind::kPreset)
				emit(i);
		for (std::size_t i = results.size(); i-- > 0 && n < out.size();)
			if (kBenchmarkSteps[i].kind == StepKind::kManual)
				emit(i);
		return n;
	}
}
