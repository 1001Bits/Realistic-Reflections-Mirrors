#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace MirrorDefinitionRegistry
{
	struct Float3
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	enum class Family : std::uint8_t
	{
		kMirror01,
		kMirror02,
		kMirror03,
		kMirror04,
		kPlayerHouseBathroomCabinet,
		kPlayerHouseBathroomSink,
		kPlayerHouseBathroomShaveDrawer,
		kPlayerHouseRuinBathroomCabinet,
		kPlayerHouseRuinBathroomSink,
		kDLC04Operator,
		kWorkshopCabinet,
		kWorkshopRound,
		kWorkshopVanity,
		kWorkshopCracked,
		kAuthoring
	};

	enum class DefinitionId : std::uint8_t
	{
		kMirror01,
		kMirror01a,
		kMirror02,
		kMirror02a,
		kMirror03,
		kMirror03a,
		kMirror04,
		kMirror04a,
		kPlayerHouseBathroomMirror02,
		kPlayerHouseBathroomMirrorNoCol,
		kPlayerHouseBathroomSink01,
		kPlayerHouseBathroomSinkNoCol,
		kPlayerHouseBathroomShaveDrawer01,
		kPlayerHouseRuinBathroomMirror02,
		kPlayerHouseRuinBathroomSink01,
		kDLC04MirrorOperator01,
		kDLC04MirrorOperator01b,
		kDLC04MirrorOperator02,
		kDLC04MirrorOperator02B,
		kWorkshopCabinetWall,
		kWorkshopCabinetStanding,
		kWorkshopRoundWall,
		kWorkshopRoundStanding,
		kWorkshopVanityWall,
		kWorkshopVanityStanding,
		kWorkshopCabinetWallMedium,
		kWorkshopCabinetWallLarge,
		kWorkshopRoundWallMedium,
		kWorkshopRoundWallLarge,
		kWorkshopCabinetUnitWall,
		kWorkshopRoundMetalWall,
		kWorkshopCabinetWallVeryLarge,
		kWorkshopCabinetWallGigantic,
		kWorkshopRoundWallVeryLarge,
		kWorkshopRoundWallGigantic,
		kWorkshopCrackedMirror01,
		kWorkshopCrackedMirror01a,
		kWorkshopCrackedMirror02,
		kWorkshopCrackedMirror02a,
		kWorkshopCrackedMirror03,
		kWorkshopCrackedMirror03a,
		kCount,
		
		kConcordSpeakeasyFrame = 0xFD,
		
		kAuthoring = 0xFE
	};

	enum class MaskKind : std::uint8_t
	{
		kRectangle,
		kEllipse,
		kAuthoredTriangles
	};

	enum class AuthoredMask : std::uint8_t
	{
		kMirror01 = 0,
		kMirror03aStrips,
		kMirror04,
		kMirror04a,
		kMirror03aShards,
		kMirror01Shards,
		kMirror01aShards,
		kMirror02Shards,
		kMirror02aShards,
		kMirror03Shards,
		kDLC04Operator01Shards,
		kDLC04Operator02Shards,
		kNone = 0xFF
	};

	enum class TransformPolicy : std::uint8_t
	{

		kReferencePlacement,

		kAnimatedNodeThenPhysicsThenReference
	};

	enum class CaptureClass : std::uint8_t
	{
		kStandard1024,
		kHero4096
	};

	enum class VisualTwinPreference : std::uint8_t
	{
		kNoPreference,
		kFallbackToPreferredTwin,
		kPreferOverTwin
	};

	struct LocalOrientedBounds
	{

		Float3 center{};
		float halfThickness{ 0.0f };
		float halfWidth{ 0.0f };
		float halfHeight{ 0.0f };
	};

	struct LocalPane
	{

		Float3 center{};
		Float3 normal{ 0.0f, 1.0f, 0.0f };
		Float3 tangent{ 1.0f, 0.0f, 0.0f };
		Float3 bitangent{ 0.0f, 0.0f, -1.0f };
		float halfWidth{ 0.0f };
		float halfHeight{ 0.0f };

		LocalOrientedBounds bounds{};

	};

	struct TransformMetadata
	{
		TransformPolicy policy{ TransformPolicy::kReferencePlacement };
		std::string_view animatedNode{};
		Float3 inverseBindTranslation{};
	};

	struct BaseFormKey
	{
		std::string_view plugin{};
		std::uint32_t localFormID{ 0 };
	};

	inline constexpr std::size_t kMaximumBaseForms = 3;

	struct Definition
	{
		DefinitionId id{};
		Family family{};
		std::string_view name{};

		std::string_view modelPathSuffix{};
		LocalPane pane{};
		MaskKind mask{ MaskKind::kRectangle };
		AuthoredMask authoredMask{ AuthoredMask::kNone };
		TransformMetadata transform{};
		CaptureClass captureClass{ CaptureClass::kStandard1024 };
		std::uint8_t refreshIntervalFrames{ 1 };
		std::array<BaseFormKey, kMaximumBaseForms> baseForms{};
		std::uint8_t baseFormCount{ 0 };
		VisualTwinPreference visualTwinPreference{ VisualTwinPreference::kNoPreference };

		std::uint16_t captureSizeOverride{ 0 };
	};
	static_assert(std::is_standard_layout_v<Definition>);
	static_assert(std::is_trivially_copyable_v<Definition>);
	inline constexpr std::size_t kDefinitionCount = static_cast<std::size_t>(DefinitionId::kCount);

	constexpr std::uint32_t DefaultRefreshRate(const Definition& definition) noexcept
	{
		switch (definition.family) {
		case Family::kPlayerHouseBathroomSink:
		case Family::kPlayerHouseBathroomShaveDrawer:
		case Family::kPlayerHouseRuinBathroomSink:
			return 15u;
		default:
			return 30u;
		}
	}

	struct ReferenceOverride
	{
		std::uint32_t formID{};
		std::uint16_t captureSize{};   
		std::uint8_t refreshHz{};      
	};

	inline constexpr std::array<ReferenceOverride, 0> kReferenceOverrides{};
	constexpr const ReferenceOverride* ReferenceOverrideFor(std::uint32_t formID) noexcept
	{
		for (const auto& entry : kReferenceOverrides)
			if (entry.formID == formID)
				return &entry;
		return nullptr;
	}

	constexpr std::uint32_t RefreshRateFor(const Definition& definition, std::uint32_t formID) noexcept
	{
		const auto* override = ReferenceOverrideFor(formID);
		return override && override->refreshHz ? override->refreshHz : DefaultRefreshRate(definition);
	}

	template <class Scale>
	constexpr std::uint32_t CaptureExtent(const Definition* definition, std::uint32_t formID, std::uint32_t heroSize,
		std::uint32_t standardSize, Scale&& scale) noexcept
	{
		if (const auto* override = ReferenceOverrideFor(formID); override && override->captureSize)
			return override->captureSize;
		if (definition && definition->captureSizeOverride)
			return definition->captureSizeOverride;
		return scale(definition && definition->captureClass == CaptureClass::kHero4096 ? heroSize : standardSize);
	}

	std::span<const Definition> Definitions() noexcept;

	const Definition* Get(DefinitionId id) noexcept;

	bool MatchesModelPathSuffix(std::string_view modelPath, std::string_view canonicalSuffix) noexcept;

	const Definition* Find(std::string_view modelPath) noexcept;
	const Definition* FindForReference(std::string_view modelPath, std::uint32_t formID) noexcept;

	bool ReferenceScoped(std::uint32_t formID) noexcept;

	bool MatchesModelFileName(std::string_view modelPath, std::string_view canonicalSuffix) noexcept;

	bool DescribesGameAsset(const Definition& definition) noexcept;

	bool SameSurface(const Definition& first, const Definition& second) noexcept;
}
