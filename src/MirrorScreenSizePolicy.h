#pragma once

#include "MirrorFleetPolicy.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>

// Owner, 2026-09-17: "Size captures to the mirror on screen". A placed pane shows
// its capture through the pane-fitted projection, whose rectangle spans the
// pane's bounding box widened by the 1.15 margin on each axis, and the pane
// shader samples a full mip chain, so texels beyond what the pane covers on
// screen are averaged away. Each capture axis is sized to the pane's projected
// extent on that axis times the margin and a supersample factor -- detail and
// antialiasing to spare, so the reflection stays as sharp and as steady while
// moving as the full-size capture -- rounded up to the pool's 512 steps and
// never above the configured resolution.
//
// Owner, core run 9 ("do all 5"): captures follow the pane's shape. A portrait
// pane gave its width as many texels as its height, 1.6x more than it can show.
// The reflected camera mirrors the main one, so the pane spans the same angles
// horizontally and vertically in both views, and its on-screen width and height
// size the capture's two axes directly.
namespace MirrorScreenSizePolicy
{
	inline constexpr float kMargin = 1.15F;
	inline constexpr float kSupersample = 1.5F;
	// Development A/B (F6): a lighter supersample the owner judges by eye.
	inline constexpr float kReducedSupersample = 1.25F;
	inline std::atomic_bool reducedSupersample{ false };
	[[nodiscard]] inline float Supersample() noexcept
	{
		return reducedSupersample.load(std::memory_order_relaxed) ? kReducedSupersample : kSupersample;
	}
	// A smaller size is taken only once the need has fit under it with this much
	// headroom for kLowerHoldMicroseconds, so walking along a size boundary never
	// flips targets back and forth. Larger sizes are taken at once.
	inline constexpr float kLowerHeadroom = 0.85F;
	inline constexpr std::uint64_t kLowerHoldMicroseconds = 1'000'000;

	struct Pane
	{
		DirectX::XMFLOAT3 center{}, tangent{}, bitangent{};
		float halfTangent{}, halfBitangent{};
	};
	// Screen pixels the whole pane spans on each screen axis; 0/0 when unknown.
	struct Pixels
	{
		float x{}, y{};
		[[nodiscard]] bool Known() const noexcept { return x > 0 && y > 0; }
	};
	struct Extent
	{
		int width{}, height{};
		constexpr bool operator==(const Extent&) const noexcept = default;
	};

	[[nodiscard]] constexpr bool Finite(float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
	}

	// Projected extent of the whole pane in output pixels, unclipped: the capture
	// covers the entire pane at one density even when part of it is off screen.
	// Unknown when a corner is at or behind the main camera or any input is
	// unusable; the caller then renders at the configured size.
	[[nodiscard]] inline Pixels ProjectedPixels(const Pane& pane,
		const DirectX::XMFLOAT4X4& viewProjection, const DirectX::XMFLOAT3& origin,
		float width, float height) noexcept
	{
		if (!Finite(width) || !Finite(height) || width < 1 || height < 1 ||
			!Finite(pane.halfTangent) || !Finite(pane.halfBitangent) ||
			!(pane.halfTangent > 0) || !(pane.halfBitangent > 0))
			return {};
		for (const auto& row : viewProjection.m)
			for (const float value : row)
				if (!Finite(value)) return {};
		float left = 0, right = 0, bottom = 0, top = 0;
		bool first = true;
		for (const float t : { -1.0F, 1.0F }) {
			for (const float b : { -1.0F, 1.0F }) {
				const float x = pane.center.x + t * pane.halfTangent * pane.tangent.x +
					b * pane.halfBitangent * pane.bitangent.x - origin.x;
				const float y = pane.center.y + t * pane.halfTangent * pane.tangent.y +
					b * pane.halfBitangent * pane.bitangent.y - origin.y;
				const float z = pane.center.z + t * pane.halfTangent * pane.tangent.z +
					b * pane.halfBitangent * pane.bitangent.z - origin.z;
				DirectX::XMFLOAT4 clip{};
				DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
					DirectX::XMVectorSet(x, y, z, 1), DirectX::XMLoadFloat4x4(&viewProjection)));
				if (!Finite(clip.x) || !Finite(clip.y) || !(clip.w > 1.0e-3F) || !Finite(clip.w))
					return {};
				const float nx = clip.x / clip.w, ny = clip.y / clip.w;
				if (!Finite(nx) || !Finite(ny)) return {};
				left = first ? nx : (std::min)(left, nx);
				right = first ? nx : (std::max)(right, nx);
				bottom = first ? ny : (std::min)(bottom, ny);
				top = first ? ny : (std::max)(top, ny);
				first = false;
			}
		}
		const Pixels result{ (right - left) * 0.5F * width, (top - bottom) * 0.5F * height };
		return Finite(result.x) && Finite(result.y) && result.Known() ? result : Pixels{};
	}

	// Smallest pool size that holds `pixels` with the margin and supersample,
	// never above the (normalized) cap. Unknown size renders at the cap.
	[[nodiscard]] constexpr int SizeFor(float pixels, int cap, float supersample = kSupersample) noexcept
	{
		cap = MirrorFleetPolicy::Resolution(cap);
		if (!Finite(pixels) || !(pixels > 0)) return cap;
		const float need = pixels * kMargin * supersample;
		for (const int size : MirrorFleetPolicy::kResolutions) {
			if (size >= cap) return cap;
			if (static_cast<float>(size) >= need) return size;
		}
		return cap;
	}

	// Both axes; a square capture takes the larger need on both.
	[[nodiscard]] constexpr Extent ExtentFor(Pixels pixels, int cap, bool rectangular,
		float supersample = kSupersample) noexcept
	{
		if (!rectangular) {
			const int size = SizeFor((std::max)(pixels.x, pixels.y), cap, supersample);
			return { size, size };
		}
		return { SizeFor(pixels.x, cap, supersample), SizeFor(pixels.y, cap, supersample) };
	}

	// Per-mirror hysteresis, render thread only. Each axis rises at once and
	// falls only after holding under a smaller size with headroom.
	class Sizer
	{
	public:
		struct Counters
		{
			std::uint64_t choices{}, raises{}, lowers{}, unknown{};
			Extent last{}, lastNeed{};
		};
		Extent Choose(std::uint32_t mirror, int cap, Pixels pixels, std::uint64_t nowUs,
			bool rectangular = true, float supersample = kSupersample) noexcept
		{
			cap = MirrorFleetPolicy::Resolution(cap);
			const bool known = pixels.Known() && Finite(pixels.x) && Finite(pixels.y);
			if (!known) pixels = {};
			const Extent wanted = ExtentFor(pixels, cap, rectangular, supersample);
			++counters_.choices;
			const auto need = [&](float value) {
				if (!known) return 0;
				const double result = double(value) * kMargin * supersample + 0.5;
				return static_cast<int>((std::min)(result, double((std::numeric_limits<int>::max)())));
			};
			counters_.lastNeed = rectangular ? Extent{ need(pixels.x), need(pixels.y) } :
				Extent{ need((std::max)(pixels.x, pixels.y)), need((std::max)(pixels.x, pixels.y)) };
			// A new mirror starts at its need (the cap when unknown); a known one
			// keeps its size through a frame without a usable main view. A changed
			// cap, shape mode or supersample applies at once.
			auto* entry = Find(mirror, cap, rectangular, supersample, wanted, nowUs);
			if (!known) {
				++counters_.unknown;
				entry->lowerSince = {};
				entry->lowerTarget = {};
			} else {
				const Extent lower = ExtentFor({ pixels.x / kLowerHeadroom, pixels.y / kLowerHeadroom },
					cap, rectangular, supersample);
				bool raised = false, lowered = false;
				const auto axis = [&](int wantedAxis, int lowerAxis, int& current, std::size_t index) {
					auto& since = entry->lowerSince[index];
					auto& candidate = entry->lowerTarget[index];
					if (wantedAxis > current) {
						current = wantedAxis; since = 0; candidate = 0; raised = true;
					} else {
						const int target = (std::min)(current, lowerAxis);
						if (target == current) { since = 0; candidate = 0; }
						else if (!since || candidate != target || nowUs < since) {
							since = nowUs ? nowUs : 1; candidate = target;
						} else if (nowUs - since >= kLowerHoldMicroseconds) {
							current = target; since = 0; candidate = 0; lowered = true;
						}
					}
				};
				// One axis changing must not mature (or cancel) the other's timer.
				axis(wanted.width, lower.width, entry->current.width, 0);
				axis(wanted.height, lower.height, entry->current.height, 1);
				counters_.raises += raised;
				counters_.lowers += lowered;
			}
			entry->used = nowUs;
			counters_.last = entry->current;
			return entry->current;
		}
		[[nodiscard]] const Counters& Read() const noexcept { return counters_; }
	private:
		struct Entry
		{
			std::uint32_t mirror{};
			int cap{};
			bool rectangular{};
			float supersample{};
			Extent current{};
			std::array<std::uint64_t, 2> lowerSince{};
			std::array<int, 2> lowerTarget{};
			std::uint64_t used{};
		};
		Entry* Find(std::uint32_t mirror, int cap, bool rectangular, float supersample,
			Extent wanted, std::uint64_t nowUs) noexcept
		{
			Entry* oldest = &entries_[0];
			for (auto& entry : entries_) {
				if (entry.current.width != 0 && entry.mirror == mirror) {
					if (entry.cap != cap || entry.rectangular != rectangular || entry.supersample != supersample)
						entry = { mirror, cap, rectangular, supersample, wanted, {}, {}, nowUs };
					return &entry;
				}
				if (entry.current.width == 0 || entry.used < oldest->used) oldest = &entry;
			}
			*oldest = { mirror, cap, rectangular, supersample, wanted, {}, {}, nowUs };
			return oldest;
		}
		std::array<Entry, 8> entries_{};
		Counters counters_{};
	};
}
