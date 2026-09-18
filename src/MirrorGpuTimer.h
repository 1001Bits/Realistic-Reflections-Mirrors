#pragma once
#include <array>
#include <cstdint>
#include <chrono>
#include <d3d11.h>
#include <wrl/client.h>
#include "EngineDeviceIdentity.h"
#include "MirrorFleetRuntime.h"

namespace MirrorFleetRuntime
{
	/** No flush, no wait, no reuse of pending queries, and no nested disjoint intervals. */
	class GpuTimer
	{
	public:
		struct PhaseResults {
			std::uint64_t samples{}, invalid{}, cpuSamples{};
			double totalMs{}, maxMs{}, cpuTotalMs{}, cpuMaxMs{};
		};
		struct Results {
			std::uint64_t samples{}, invalid{}, saturated{}, rejected{};
			double totalMs{}, maxMs{};
			std::array<PhaseResults, static_cast<std::size_t>(RenderPhase::kCount)> phases{};
			std::uint64_t phaseRejected{}, phaseSaturated{}, phaseQueryPairs{};
		};
		// Phase timestamps share this one disjoint interval. Disabled by default;
		// query creation and bounded recording never control mirror admission.
		bool Begin(ID3D11Device* device, ID3D11DeviceContext* context, bool phases = false) noexcept
		{
			if (!device || !context || active_ != kNone) return false;
			Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
			context->GetDevice(&contextDevice);
			const auto identity = EngineDeviceIdentity::Current(reinterpret_cast<std::uintptr_t>(device));
			if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
				!EngineDeviceIdentityPolicy::OwnedByEngineDevice(identity, reinterpret_cast<std::uintptr_t>(contextDevice.Get()))) {
				++results_.rejected; return false;
			}
			if (device_.Get() != device || context_.Get() != context) {
				Reset(); device_ = device; context_ = context;
			}
			Poll(context);
			const auto now = std::chrono::steady_clock::now();
			if (now < retryAfter_) return false;
			for (std::size_t i = 0; i < slots_.size(); ++i) {
				auto& s = slots_[i];
				if (s.pending) continue;
				if (!s.disjoint) {
					D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
					Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
					if (FAILED(device->CreateQuery(&desc, &disjoint))) {
						retryAfter_ = now + std::chrono::seconds(1); return false;
					}
					desc.Query = D3D11_QUERY_TIMESTAMP;
					if (FAILED(device->CreateQuery(&desc, &begin)) || FAILED(device->CreateQuery(&desc, &end))) {
						retryAfter_ = now + std::chrono::seconds(1); return false;
					}
					Microsoft::WRL::ComPtr<ID3D11Device> a, b, c;
					disjoint->GetDevice(&a); begin->GetDevice(&b); end->GetDevice(&c);
					if (!a || a != b || a != c || !EngineDeviceIdentityPolicy::OwnedByEngineDevice(
						identity, reinterpret_cast<std::uintptr_t>(a.Get()))) {
						++results_.rejected; retryAfter_ = now + std::chrono::seconds(1); return false;
					}
					s.disjoint = std::move(disjoint); s.begin = std::move(begin); s.end = std::move(end);
				}
				s.phaseEnabled = phases; s.used = 0; s.activePhase = kPhaseLimit;
				active_ = i;
				context->Begin(s.disjoint.Get()); context->End(s.begin.Get());
				return true;
			}
			++results_.saturated;
			return false;
		}
		PhaseToken BeginPhase(ID3D11DeviceContext* context, RenderPhase phase) noexcept
		{
			if (active_ == kNone || !slots_[active_].phaseEnabled) return 0;
			const auto index = static_cast<std::size_t>(phase);
			auto& s = slots_[active_];
			if (!context || context != context_.Get() || index >= results_.phases.size() ||
				s.activePhase != kPhaseLimit) { ++results_.phaseRejected; return 0; }
			if (s.used == kPhaseLimit) { ++results_.phaseSaturated; return 0; }
			const auto now = std::chrono::steady_clock::now();
			if (now < phaseRetryAfter_) return 0;
			auto& p = s.phases[s.used];
			if (!p.begin) {
				const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
				Microsoft::WRL::ComPtr<ID3D11Query> begin, end;
				if (FAILED(device_->CreateQuery(&desc, &begin)) || FAILED(device_->CreateQuery(&desc, &end))) {
					++results_.phaseRejected; phaseRetryAfter_ = now + std::chrono::seconds(1); return 0;
				}
				Microsoft::WRL::ComPtr<ID3D11Device> a, b;
				begin->GetDevice(&a); end->GetDevice(&b);
				const auto identity = EngineDeviceIdentity::Current(reinterpret_cast<std::uintptr_t>(device_.Get()));
				if (!a || a != b || !EngineDeviceIdentityPolicy::OwnedByEngineDevice(identity,
					reinterpret_cast<std::uintptr_t>(a.Get()))) {
					++results_.phaseRejected; phaseRetryAfter_ = now + std::chrono::seconds(1); return 0;
				}
				p.begin = std::move(begin); p.end = std::move(end); ++results_.phaseQueryPairs;
			}
			p.phase = index; p.closed = false; p.valid = true;
			if (++nextToken_ == 0) ++nextToken_;
			p.token = nextToken_; s.activePhase = s.used++;
			p.cpuStart = std::chrono::steady_clock::now();
			context->End(p.begin.Get());
			return p.token;
		}
		void EndPhase(ID3D11DeviceContext* context, PhaseToken token) noexcept
		{
			if (!token) return;
			if (active_ == kNone || !context || context != context_.Get()) {
				++results_.phaseRejected; return;
			}
			auto& s = slots_[active_];
			if (s.activePhase == kPhaseLimit || s.phases[s.activePhase].token != token) {
				++results_.phaseRejected; return;
			}
			auto& p = s.phases[s.activePhase];
			context->End(p.end.Get()); p.closed = true; s.activePhase = kPhaseLimit;
			const double ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - p.cpuStart).count();
			auto& result = results_.phases[p.phase];
			++result.cpuSamples; result.cpuTotalMs += ms;
			if (ms > result.cpuMaxMs) result.cpuMaxMs = ms;
		}
		void End(ID3D11DeviceContext* context) noexcept
		{
			if (active_ == kNone || !context) return;
			if (context != context_.Get()) { ++results_.rejected; return; }
			auto& s = slots_[active_];
			if (s.activePhase != kPhaseLimit) {
				// An abandoned/exceptional interval cannot masquerade as a complete
				// pass or leak into the next batch. Other phase samples remain valid.
				context->End(s.phases[s.activePhase].end.Get()); s.activePhase = kPhaseLimit;
			}
			context->End(s.end.Get()); context->End(s.disjoint.Get());
			s.pending = true; active_ = kNone;
		}
		void Poll(ID3D11DeviceContext* context) noexcept
		{
			if (!context || context != context_.Get()) return;
			for (auto& s : slots_) if (s.pending) {
				D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
				std::uint64_t start{}, end{};
				const auto a = context->GetData(s.disjoint.Get(), &d, sizeof(d), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (a == S_FALSE) continue;
				if (FAILED(a)) { Invalidate(s); continue; }
				const auto b = context->GetData(s.begin.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				const auto c = context->GetData(s.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
				if (FAILED(b) || FAILED(c)) { Invalidate(s); continue; }
				if (b == S_FALSE || c == S_FALSE) continue;
				if (d.Disjoint || !d.Frequency || end < start) { Invalidate(s); continue; }
				bool ready = true;
				for (std::size_t i = 0; i < s.used; ++i) {
					auto& p = s.phases[i];
					if (!p.closed || !p.valid) continue;
					const auto x = context->GetData(p.begin.Get(), &p.start, sizeof(p.start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					const auto y = context->GetData(p.end.Get(), &p.finish, sizeof(p.finish), D3D11_ASYNC_GETDATA_DONOTFLUSH);
					if (FAILED(x) || FAILED(y)) p.valid = false;
					else if (x == S_FALSE || y == S_FALSE) ready = false;
				}
				if (!ready) continue; // Commit all results once, never partially count a pending batch.
				s.pending = false;
				const double ms = 1000.0 * static_cast<double>(end - start) / static_cast<double>(d.Frequency);
				++results_.samples; results_.totalMs += ms;
				if (ms > results_.maxMs) results_.maxMs = ms;
				std::uint64_t priorEnd = start;
				for (std::size_t i = 0; i < s.used; ++i) {
					const auto& p = s.phases[i]; auto& result = results_.phases[p.phase];
					if (!p.closed || !p.valid || p.start < priorEnd || p.finish < p.start || p.finish > end) {
						++result.invalid; continue;
					}
					priorEnd = p.finish;
					const double phaseMs = 1000.0 * static_cast<double>(p.finish - p.start) / static_cast<double>(d.Frequency);
					++result.samples; result.totalMs += phaseMs;
					if (phaseMs > result.maxMs) result.maxMs = phaseMs;
				}
			}
		}
		Results Read() const noexcept { return results_; }
		void Reset() noexcept
		{
			// Close an outstanding interval on its owner before discarding it.
			End(context_.Get());
			for (auto& s : slots_) if (s.pending) Invalidate(s);
			slots_ = {}; context_.Reset(); device_.Reset(); active_ = kNone; retryAfter_ = {}; phaseRetryAfter_ = {};
		}
	private:
		static constexpr std::size_t kPhaseLimit = 128; // Recording bound, never a mirror/rendering bound.
		struct PhaseSample {
			Microsoft::WRL::ComPtr<ID3D11Query> begin, end;
			PhaseToken token{}; std::uint64_t start{}, finish{}; std::size_t phase{};
			std::chrono::steady_clock::time_point cpuStart{};
			bool closed{}, valid{};
		};
		struct Slot {
			Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
			std::array<PhaseSample, kPhaseLimit> phases{};
			std::size_t used{}, activePhase{kPhaseLimit}; bool pending{}, phaseEnabled{};
		};
		void Invalidate(Slot& s) noexcept {
			s.pending = false; ++results_.invalid;
			for (std::size_t i = 0; i < s.used; ++i) ++results_.phases[s.phases[i].phase].invalid;
		}
		static constexpr std::size_t kNone = 12;
		std::array<Slot, kNone> slots_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		std::size_t active_{ kNone };
		Results results_{};
		std::chrono::steady_clock::time_point retryAfter_{};
		std::chrono::steady_clock::time_point phaseRetryAfter_{};
		PhaseToken nextToken_{};
	};
}
