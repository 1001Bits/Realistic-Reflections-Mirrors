#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <limits>
#include <utility>

namespace HandMirrorVRRuntimePolicy
{
	// VR caches InventoryMenu after it leaves the stack. Instance existence
	// is an early transition proof only on the flat runtime.
	[[nodiscard]] constexpr bool InventoryBlocksCapture(const bool correctedVR,
		const bool itemMenuOpen, const bool inventoryOnStack,
		const bool inventoryInstanceExists) noexcept
	{
		return itemMenuOpen || inventoryOnStack ||
			(!correctedVR && inventoryInstanceExists);
	}

	// A VR equipment draw and the late world batch are different raster domains.
	// Never compare their views as duplicate observations of one immutable sample.
	[[nodiscard]] constexpr std::size_t ViewCohortIndex(
		const bool separatedVR, const bool handPane) noexcept
	{
		return separatedVR && handPane ? 1 : 0;
	}

	enum class ViewObservation { kStored, kRepeated, kRejectedFrame, kTerminal };

	template <class Snapshot>
	class CaptureViewCohorts
	{
	public:
		[[nodiscard]] Snapshot& Get(const std::size_t index) noexcept { return views[index]; }
		[[nodiscard]] const Snapshot& Get(const std::size_t index) const noexcept { return views[index]; }

		void Reset(const std::uint64_t frame = 0) noexcept
		{
			views = {};
			rejected = {};
			for (auto& view : views)
				view.mainWorldFrame = frame;
		}

		void Discard(const std::size_t index) noexcept
		{
			const auto frame = views[index].mainWorldFrame;
			views[index] = {};
			views[index].mainWorldFrame = frame;
			rejected[index] = true;
		}

		template <class SameView>
		[[nodiscard]] ViewObservation Observe(
			const std::size_t index, Snapshot candidate,
			const bool recoverableDrift, SameView sameView) noexcept
		{
			auto& view = views[index];
			if (rejected[index])
				return ViewObservation::kRejectedFrame;
			if (!candidate.valid || candidate.mainWorldFrame == 0 ||
				candidate.graphicsFrame == 0 || candidate.observationCount != 1 ||
				(view.mainWorldFrame != 0 && candidate.mainWorldFrame != view.mainWorldFrame)) {
				Discard(index);
				return ViewObservation::kTerminal;
			}
			if (view.valid) {
				if (view.observationCount == (std::numeric_limits<std::uint32_t>::max)()) {
					Discard(index);
					return ViewObservation::kTerminal;
				}
				if (!sameView(view, candidate)) {
					Discard(index);
					// Coherent VR camera/pass drift discards this domain for this
					// frame. Guarded reads, COM faults and overflow remain terminal.
					return recoverableDrift ? ViewObservation::kRejectedFrame : ViewObservation::kTerminal;
				}
				candidate.observationCount = view.observationCount + 1;
				view = std::move(candidate);
				return ViewObservation::kRepeated;
			}
			view = std::move(candidate);
			return ViewObservation::kStored;
		}

	private:
		std::array<Snapshot, 2> views{};
		std::array<bool, 2> rejected{};
	};

	// VR NiAVObject::UpdateWorldData (0xCA7000) reads the renderer frame and
	// stores it at +0x11C (0xCA7024). +0x104 is not the VR update stamp.
	inline constexpr std::size_t kFlatNodeFrameOffset = 0x104;
	inline constexpr std::size_t kVRNodeFrameOffset = 0x11C;

	[[nodiscard]] inline std::uint32_t ReadNodeFrame(
		const std::span<const std::byte> object, const bool correctedVR) noexcept
	{
		const auto offset = correctedVR ? kVRNodeFrameOffset : kFlatNodeFrameOffset;
		std::uint32_t frame = 0;
		if (object.size() >= offset + sizeof(frame))
			std::memcpy(&frame, object.data() + offset, sizeof(frame));
		return frame;
	}

	[[nodiscard]] constexpr bool AcceptPerspective(
		const bool correctedVR, const bool flatFirstPerson,
		const bool exactVRCameraState) noexcept
	{
		return correctedVR ? exactVRCameraState : flatFirstPerson;
	}

	// Only the hand caller may opt into the sole outer VR world scope. The
	// current-frame camera/target checks still follow; private and nested views
	// must never borrow it, even while the VR hand transaction remains active.
	[[nodiscard]] constexpr bool AcceptMainDrawScope(
		const bool allowVRWorld, const bool exactVRRuntime,
		const std::uint32_t worldDepth, const std::uint32_t privateDepth) noexcept
	{
		return privateDepth == 0 && (worldDepth == 0 ||
			(allowVRWorld && exactVRRuntime && worldDepth == 1));
	}

	// Early raster observation has a narrower lifetime than delayed delivery.
	// Its caller must additionally prove the exact equipped pane before reading
	// any target/camera data; outside-world and private draws cannot seed it.
	[[nodiscard]] constexpr bool AllowEarlyPaneObservation(
		const bool enabled, const bool exactVRRuntime,
		const std::uint32_t worldDepth, const std::uint32_t privateDepth) noexcept
	{
		return enabled && exactVRRuntime && worldDepth == 1 && privateDepth == 0;
	}
}
