#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace MirrorAuthoringContract
{
	// This file is an engine-independent specification and validation policy.
	// No runtime recognizer, pane resolver, or delivery path consumes it yet.
	inline constexpr bool kRuntimeIntegrationWired = false;
	inline constexpr bool kAuthorsControlCaptureResolution = false;
	inline constexpr bool kAuthorsControlCaptureCadence = false;

	inline constexpr std::string_view kPaneName = "TrueMirror:0";
	inline constexpr std::string_view kSchemaTagName = "TrueMirror";
	inline constexpr std::string_view kApertureTagName = "RRMirrorAperture";

	inline constexpr std::size_t kMaximumPanesPerReference = 1;
	inline constexpr std::size_t kMaximumRegisteredBases = 256;
	inline constexpr std::size_t kMaximumTrackedReferences = 32;
	inline constexpr std::size_t kRectangularApertureValueCount = 4;

	// Proposed schema-v2 v0 policy limits.  They are compatibility policy, not
	// claims about an engine limit, and may only change through a schema/policy
	// revision backed by new evidence.
	inline constexpr float kMinimumApertureHalfExtent = 4.0F;
	inline constexpr float kMaximumApertureHalfExtent = 256.0F;
	inline constexpr float kMaximumApertureAspectRatio = 8.0F;
	inline constexpr float kMinimumClearance = 0.0F;
	inline constexpr float kMaximumClearance = 128.0F;
	inline constexpr float kMinimumUniformScale = 0.05F;
	inline constexpr float kMaximumUniformScale = 20.0F;
	inline constexpr float kMaximumWorldHalfExtent = 1024.0F;
	inline constexpr float kMinimumAxisLengthSquared = 0.99F * 0.99F;
	inline constexpr float kMaximumAxisLengthSquared = 1.01F * 1.01F;
	inline constexpr float kMaximumAxisDotMagnitude = 0.001F;
	inline constexpr float kMinimumRightHandedDeterminant = 0.999F;

	enum class SchemaVersion : std::uint8_t
	{
		kUnknown = 0,
		kFrozenV1 = 1,
		kRectangularV2 = 2
	};

	enum class SourcePolicy : std::uint8_t
	{
		kNone,
		kFrozenV1Allowlist,
		kExplicitRegistration,
		kOwnedKeyword,
		kEditorID,
		kModelPath,
		kObjectName,
		kPaneMetadataOnly
	};

	enum class PlaneSource : std::uint8_t
	{
		kNone,
		kBaseObjectBoundsThinAxis,
		kPaneOriginAndRectangularMetadata
	};

	enum class GeometryType : std::uint8_t
	{
		kUnknown,
		kBSTriShape,
		kOther
	};

	enum class ExtraDataType : std::uint8_t
	{
		kMissing,
		kNiIntegerExtraData,
		kNiFloatsExtraData,
		kOther
	};

	enum class RejectionReason : std::uint8_t
	{
		kNone,
		kFrozenV1BaseNotAllowlisted,
		kSourcePolicyIneligible,
		kSourceOwnershipUnverified,
		kSourceRegistrationMissing,
		kSourceRegistrationCapacityExceeded,
		kSourceMetadataMismatch,
		kPaneMissing,
		kMultiplePanes,
		kPaneNotDirect,
		kPaneGeometryTypeMismatch,
		kPaneNameMismatch,
		kSchemaTagMissing,
		kSchemaTagNotDirect,
		kSchemaTagTypeMismatch,
		kSchemaTagNameMismatch,
		kSchemaTagValueMismatch,
		kApertureMetadataUnexpected,
		kApertureMetadataMissing,
		kApertureMetadataNotDirect,
		kApertureMetadataTypeMismatch,
		kApertureMetadataNameMismatch,
		kApertureMetadataCountMismatch,
		kApertureOriginMismatch,
		kApertureValueNotFinite,
		kApertureWidthOutOfRange,
		kApertureHeightOutOfRange,
		kApertureAspectRatioOutOfRange,
		kFrontClearanceOutOfRange,
		kBackClearanceOutOfRange,
		kScaleNotFinite,
		kScaleOutOfRange,
		kAxisNotFinite,
		kAxisLengthOutOfRange,
		kAxesNotOrthogonal,
		kAxesNotRightHanded,
		kWorldExtentNotFinite,
		kWorldExtentOutOfRange
	};

	struct Vector3
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	struct Basis
	{
		Vector3 normal{};
		Vector3 tangent{};
		Vector3 bitangent{};
	};

	inline constexpr Basis kRequiredLocalBasis{
		.normal = { 1.0F, 0.0F, 0.0F },
		.tangent = { 0.0F, 1.0F, 0.0F },
		.bitangent = { 0.0F, 0.0F, 1.0F }
	};

	struct Aperture
	{
		float halfWidth{ 0.0F };
		float halfHeight{ 0.0F };
		float frontClearance{ 0.0F };
		float backClearance{ 0.0F };
	};

	struct IntegerExtraDataObservation
	{
		ExtraDataType type{ ExtraDataType::kMissing };
		bool direct{ false };
		std::string_view name{};
		std::int32_t value{ 0 };
	};

	struct FloatsExtraDataObservation
	{
		ExtraDataType type{ ExtraDataType::kMissing };
		bool direct{ false };
		std::string_view name{};
		std::size_t valueCount{ 0 };
		std::array<float, kRectangularApertureValueCount> values{};
	};

	struct PaneObservation
	{
		std::size_t paneCount{ 0 };
		bool directGeometry{ false };
		GeometryType geometryType{ GeometryType::kUnknown };
		std::string_view name{};
		IntegerExtraDataObservation schemaTag{};
		FloatsExtraDataObservation apertureTag{};
		bool paneOriginIsApertureCenter{ false };
	};

	struct FrozenV1Input
	{
		std::string_view pluginName{};
		std::uint32_t localFormID{ 0 };
		PaneObservation pane{};
	};

	struct SourceObservation
	{
		SourcePolicy policy{ SourcePolicy::kNone };
		bool ownershipVerified{ false };
		SchemaVersion declaredSchema{ SchemaVersion::kUnknown };
		std::size_t explicitRegisteredBaseCount{ 0 };
	};

	struct InstanceObservation
	{
		Basis worldAxes{};
		float uniformScale{ 0.0F };
	};

	struct RectangularV2Input
	{
		SourceObservation source{};
		PaneObservation pane{};
		InstanceObservation instance{};
	};

	struct ValidatedContract
	{
		SchemaVersion schema{ SchemaVersion::kUnknown };
		SourcePolicy sourcePolicy{ SourcePolicy::kNone };
		PlaneSource planeSource{ PlaneSource::kNone };
		Vector3 localPlaneAnchor{};
		Vector3 localApertureCenter{};
		Basis localAxes{};
		Aperture aperture{};
		// Ordered as normal, tangent, bitangent.  V1 preserves its authored
		// OBND source extents here; v2 derives these from aperture metadata.
		std::array<float, 3> localSourceHalfExtents{};
	};

	struct ValidatedInstance
	{
		Basis worldAxes{};
		float uniformScale{ 0.0F };
		// Ordered as normal, tangent, bitangent.
		std::array<float, 3> worldHalfExtents{};
	};

	struct ValidationResult
	{
		RejectionReason rejection{ RejectionReason::kNone };
		ValidatedContract contract{};
		ValidatedInstance instance{};
		bool instanceValidated{ false };

		[[nodiscard]] constexpr bool Accepted() const noexcept
		{
			return rejection == RejectionReason::kNone;
		}
	};

	struct FrozenV1Base
	{
		std::string_view pluginName{};
		std::uint32_t localFormID{ 0 };
	};

	inline constexpr std::array kFrozenV1Bases{
		FrozenV1Base{ "RealisticReflections.esp", 0x800 },
		FrozenV1Base{ "RealisticReflectionsMirrors.esm", 0x800 },
		FrozenV1Base{ "RealisticReflectionsMirrors.esm", 0x801 },
		FrozenV1Base{ "RealisticReflectionsMirrors.esm", 0x802 },
		FrozenV1Base{ "RealisticReflectionsMirrors.esm", 0x803 },
		FrozenV1Base{ "RealisticReflectionsMirrors.esm", 0x804 }
	};

	inline constexpr Vector3 kFrozenV1LocalPlaneAnchor{ 0.0F, 0.0F, 0.0F };
	inline constexpr Vector3 kFrozenV1LocalApertureCenter{ 0.0F, 0.0F, 95.0F };
	inline constexpr Aperture kFrozenV1Aperture{
		.halfWidth = 52.0F,
		.halfHeight = 87.0F,
		.frontClearance = 8.0F,
		.backClearance = 8.0F
	};
	inline constexpr std::array kFrozenV1OBNDHalfExtents{ 8.0F, 60.0F, 95.0F };

	[[nodiscard]] constexpr char LowerASCII(const char value) noexcept
	{
		return value >= 'A' && value <= 'Z' ?
			static_cast<char>(value + ('a' - 'A')) : value;
	}

	[[nodiscard]] constexpr bool FilenameEquals(
		const std::string_view left,
		const std::string_view right) noexcept
	{
		if (left.size() != right.size())
			return false;
		for (std::size_t index = 0; index < left.size(); ++index) {
			if (LowerASCII(left[index]) != LowerASCII(right[index]))
				return false;
		}
		return true;
	}

	[[nodiscard]] constexpr bool IsFrozenV1Base(
		const std::string_view pluginName,
		const std::uint32_t localFormID) noexcept
	{
		for (const auto& base : kFrozenV1Bases) {
			if (base.localFormID == localFormID &&
				FilenameEquals(base.pluginName, pluginName)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] constexpr bool IsEligibleV2SourcePolicy(
		const SourcePolicy policy) noexcept
	{
		return policy == SourcePolicy::kExplicitRegistration ||
		       policy == SourcePolicy::kOwnedKeyword;
	}

	[[nodiscard]] constexpr bool IsFinite(const float value) noexcept
	{
		return (std::bit_cast<std::uint32_t>(value) & 0x7F800000U) !=
		       0x7F800000U;
	}

	[[nodiscard]] constexpr bool IsFinite(const Vector3 value) noexcept
	{
		return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
	}

	[[nodiscard]] constexpr float Abs(const float value) noexcept
	{
		return value < 0.0F ? -value : value;
	}

	[[nodiscard]] constexpr float Dot(
		const Vector3 left,
		const Vector3 right) noexcept
	{
		return left.x * right.x + left.y * right.y + left.z * right.z;
	}

	[[nodiscard]] constexpr Vector3 Cross(
		const Vector3 left,
		const Vector3 right) noexcept
	{
		return {
			left.y * right.z - left.z * right.y,
			left.z * right.x - left.x * right.z,
			left.x * right.y - left.y * right.x
		};
	}

	[[nodiscard]] constexpr float Determinant(const Basis& basis) noexcept
	{
		return Dot(basis.normal, Cross(basis.tangent, basis.bitangent));
	}

	[[nodiscard]] constexpr bool ApertureMetadataAbsent(
		const FloatsExtraDataObservation& metadata) noexcept
	{
		return metadata.type == ExtraDataType::kMissing && !metadata.direct &&
		       metadata.name.empty() && metadata.valueCount == 0;
	}

	[[nodiscard]] constexpr RejectionReason ValidatePaneShell(
		const PaneObservation& pane) noexcept
	{
		if (pane.paneCount == 0)
			return RejectionReason::kPaneMissing;
		if (pane.paneCount > kMaximumPanesPerReference)
			return RejectionReason::kMultiplePanes;
		if (!pane.directGeometry)
			return RejectionReason::kPaneNotDirect;
		if (pane.geometryType != GeometryType::kBSTriShape)
			return RejectionReason::kPaneGeometryTypeMismatch;
		if (pane.name != kPaneName)
			return RejectionReason::kPaneNameMismatch;
		return RejectionReason::kNone;
	}

	[[nodiscard]] constexpr RejectionReason ValidateSchemaTag(
		const IntegerExtraDataObservation& tag,
		const SchemaVersion expected) noexcept
	{
		if (tag.type == ExtraDataType::kMissing)
			return RejectionReason::kSchemaTagMissing;
		if (!tag.direct)
			return RejectionReason::kSchemaTagNotDirect;
		if (tag.type != ExtraDataType::kNiIntegerExtraData)
			return RejectionReason::kSchemaTagTypeMismatch;
		if (tag.name != kSchemaTagName)
			return RejectionReason::kSchemaTagNameMismatch;
		if (tag.value != static_cast<std::int32_t>(expected))
			return RejectionReason::kSchemaTagValueMismatch;
		return RejectionReason::kNone;
	}

	[[nodiscard]] constexpr ValidationResult Reject(
		const RejectionReason reason) noexcept
	{
		return { .rejection = reason };
	}

	/**
	 * Adapt only the exact first-party schema-v1 bases and their exact direct
	 * pane/tag structure to a normalized rectangular description.  This does not
	 * generalize v1 to third-party assets and does not validate a placed instance.
	 */
	[[nodiscard]] constexpr ValidationResult AdaptFrozenV1(
		const FrozenV1Input& input) noexcept
	{
		if (!IsFrozenV1Base(input.pluginName, input.localFormID))
			return Reject(RejectionReason::kFrozenV1BaseNotAllowlisted);
		if (const auto reason = ValidatePaneShell(input.pane);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}
		if (const auto reason =
				ValidateSchemaTag(input.pane.schemaTag, SchemaVersion::kFrozenV1);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}
		if (!ApertureMetadataAbsent(input.pane.apertureTag))
			return Reject(RejectionReason::kApertureMetadataUnexpected);

		ValidationResult result{};
		result.contract.schema = SchemaVersion::kFrozenV1;
		result.contract.sourcePolicy = SourcePolicy::kFrozenV1Allowlist;
		result.contract.planeSource = PlaneSource::kBaseObjectBoundsThinAxis;
		result.contract.localPlaneAnchor = kFrozenV1LocalPlaneAnchor;
		result.contract.localApertureCenter = kFrozenV1LocalApertureCenter;
		result.contract.localAxes = kRequiredLocalBasis;
		result.contract.aperture = kFrozenV1Aperture;
		result.contract.localSourceHalfExtents = kFrozenV1OBNDHalfExtents;
		return result;
	}

	[[nodiscard]] constexpr RejectionReason ValidateV2Source(
		const SourceObservation& source) noexcept
	{
		if (!IsEligibleV2SourcePolicy(source.policy))
			return RejectionReason::kSourcePolicyIneligible;
		if (!source.ownershipVerified)
			return RejectionReason::kSourceOwnershipUnverified;
		if (source.declaredSchema != SchemaVersion::kRectangularV2)
			return RejectionReason::kSourceMetadataMismatch;
		if (source.policy == SourcePolicy::kExplicitRegistration) {
			if (source.explicitRegisteredBaseCount == 0)
				return RejectionReason::kSourceRegistrationMissing;
			if (source.explicitRegisteredBaseCount > kMaximumRegisteredBases)
				return RejectionReason::kSourceRegistrationCapacityExceeded;
		}
		return RejectionReason::kNone;
	}

	[[nodiscard]] constexpr RejectionReason ValidateV2ApertureMetadata(
		const PaneObservation& pane,
		Aperture& aperture) noexcept
	{
		const auto& metadata = pane.apertureTag;
		if (metadata.type == ExtraDataType::kMissing)
			return RejectionReason::kApertureMetadataMissing;
		if (!metadata.direct)
			return RejectionReason::kApertureMetadataNotDirect;
		if (metadata.type != ExtraDataType::kNiFloatsExtraData)
			return RejectionReason::kApertureMetadataTypeMismatch;
		if (metadata.name != kApertureTagName)
			return RejectionReason::kApertureMetadataNameMismatch;
		if (metadata.valueCount != kRectangularApertureValueCount)
			return RejectionReason::kApertureMetadataCountMismatch;
		if (!pane.paneOriginIsApertureCenter)
			return RejectionReason::kApertureOriginMismatch;
		for (const auto value : metadata.values) {
			if (!IsFinite(value))
				return RejectionReason::kApertureValueNotFinite;
		}

		aperture = {
			.halfWidth = metadata.values[0],
			.halfHeight = metadata.values[1],
			.frontClearance = metadata.values[2],
			.backClearance = metadata.values[3]
		};
		if (aperture.halfWidth < kMinimumApertureHalfExtent ||
			aperture.halfWidth > kMaximumApertureHalfExtent) {
			return RejectionReason::kApertureWidthOutOfRange;
		}
		if (aperture.halfHeight < kMinimumApertureHalfExtent ||
			aperture.halfHeight > kMaximumApertureHalfExtent) {
			return RejectionReason::kApertureHeightOutOfRange;
		}
		if (aperture.halfWidth >
				aperture.halfHeight * kMaximumApertureAspectRatio ||
			aperture.halfHeight >
				aperture.halfWidth * kMaximumApertureAspectRatio) {
			return RejectionReason::kApertureAspectRatioOutOfRange;
		}
		if (aperture.frontClearance < kMinimumClearance ||
			aperture.frontClearance > kMaximumClearance) {
			return RejectionReason::kFrontClearanceOutOfRange;
		}
		if (aperture.backClearance < kMinimumClearance ||
			aperture.backClearance > kMaximumClearance) {
			return RejectionReason::kBackClearanceOutOfRange;
		}
		return RejectionReason::kNone;
	}

	[[nodiscard]] constexpr RejectionReason ValidateV2Instance(
		const InstanceObservation& observation,
		const Aperture& aperture,
		ValidatedInstance& instance) noexcept
	{
		if (!IsFinite(observation.uniformScale))
			return RejectionReason::kScaleNotFinite;
		if (observation.uniformScale < kMinimumUniformScale ||
			observation.uniformScale > kMaximumUniformScale) {
			return RejectionReason::kScaleOutOfRange;
		}

		const std::array axes{
			observation.worldAxes.normal,
			observation.worldAxes.tangent,
			observation.worldAxes.bitangent
		};
		for (const auto axis : axes) {
			if (!IsFinite(axis))
				return RejectionReason::kAxisNotFinite;
			const auto lengthSquared = Dot(axis, axis);
			if (lengthSquared < kMinimumAxisLengthSquared ||
				lengthSquared > kMaximumAxisLengthSquared) {
				return RejectionReason::kAxisLengthOutOfRange;
			}
		}
		if (Abs(Dot(axes[0], axes[1])) > kMaximumAxisDotMagnitude ||
			Abs(Dot(axes[0], axes[2])) > kMaximumAxisDotMagnitude ||
			Abs(Dot(axes[1], axes[2])) > kMaximumAxisDotMagnitude) {
			return RejectionReason::kAxesNotOrthogonal;
		}
		if (Determinant(observation.worldAxes) <
			kMinimumRightHandedDeterminant) {
			return RejectionReason::kAxesNotRightHanded;
		}

		const auto normalHalfExtent =
			(aperture.frontClearance > aperture.backClearance ?
				aperture.frontClearance : aperture.backClearance) *
			observation.uniformScale;
		const std::array worldHalfExtents{
			normalHalfExtent,
			aperture.halfWidth * observation.uniformScale,
			aperture.halfHeight * observation.uniformScale
		};
		for (const auto extent : worldHalfExtents) {
			if (!IsFinite(extent))
				return RejectionReason::kWorldExtentNotFinite;
			if (extent > kMaximumWorldHalfExtent)
				return RejectionReason::kWorldExtentOutOfRange;
		}

		instance.worldAxes = observation.worldAxes;
		instance.uniformScale = observation.uniformScale;
		instance.worldHalfExtents = worldHalfExtents;
		return RejectionReason::kNone;
	}

	/**
	 * Validate proposed schema v2 without consulting Skyrim state.  The caller
	 * must supply already-observed source authority, direct pane metadata, and
	 * value-only world axes/scale; validation never repairs malformed input.
	 */
	[[nodiscard]] constexpr ValidationResult ValidateRectangularV2(
		const RectangularV2Input& input) noexcept
	{
		if (const auto reason = ValidateV2Source(input.source);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}
		if (const auto reason = ValidatePaneShell(input.pane);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}
		if (const auto reason = ValidateSchemaTag(
				input.pane.schemaTag, SchemaVersion::kRectangularV2);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}

		Aperture aperture{};
		if (const auto reason = ValidateV2ApertureMetadata(input.pane, aperture);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}
		ValidatedInstance instance{};
		if (const auto reason = ValidateV2Instance(
				input.instance, aperture, instance);
			reason != RejectionReason::kNone) {
			return Reject(reason);
		}

		const auto normalHalfExtent =
			aperture.frontClearance > aperture.backClearance ?
				aperture.frontClearance : aperture.backClearance;
		ValidationResult result{};
		result.contract.schema = SchemaVersion::kRectangularV2;
		result.contract.sourcePolicy = input.source.policy;
		result.contract.planeSource =
			PlaneSource::kPaneOriginAndRectangularMetadata;
		result.contract.localPlaneAnchor = {};
		result.contract.localApertureCenter = {};
		result.contract.localAxes = kRequiredLocalBasis;
		result.contract.aperture = aperture;
		result.contract.localSourceHalfExtents = {
			normalHalfExtent, aperture.halfWidth, aperture.halfHeight };
		result.instance = instance;
		result.instanceValidated = true;
		return result;
	}

	[[nodiscard]] constexpr std::string_view RejectionName(
		const RejectionReason reason) noexcept
	{
		switch (reason) {
		case RejectionReason::kNone: return "none";
		case RejectionReason::kFrozenV1BaseNotAllowlisted: return "frozen-v1-base-not-allowlisted";
		case RejectionReason::kSourcePolicyIneligible: return "source-policy-ineligible";
		case RejectionReason::kSourceOwnershipUnverified: return "source-ownership-unverified";
		case RejectionReason::kSourceRegistrationMissing: return "source-registration-missing";
		case RejectionReason::kSourceRegistrationCapacityExceeded: return "source-registration-capacity-exceeded";
		case RejectionReason::kSourceMetadataMismatch: return "source-metadata-mismatch";
		case RejectionReason::kPaneMissing: return "pane-missing";
		case RejectionReason::kMultiplePanes: return "multiple-panes";
		case RejectionReason::kPaneNotDirect: return "pane-not-direct";
		case RejectionReason::kPaneGeometryTypeMismatch: return "pane-geometry-type-mismatch";
		case RejectionReason::kPaneNameMismatch: return "pane-name-mismatch";
		case RejectionReason::kSchemaTagMissing: return "schema-tag-missing";
		case RejectionReason::kSchemaTagNotDirect: return "schema-tag-not-direct";
		case RejectionReason::kSchemaTagTypeMismatch: return "schema-tag-type-mismatch";
		case RejectionReason::kSchemaTagNameMismatch: return "schema-tag-name-mismatch";
		case RejectionReason::kSchemaTagValueMismatch: return "schema-tag-value-mismatch";
		case RejectionReason::kApertureMetadataUnexpected: return "aperture-metadata-unexpected";
		case RejectionReason::kApertureMetadataMissing: return "aperture-metadata-missing";
		case RejectionReason::kApertureMetadataNotDirect: return "aperture-metadata-not-direct";
		case RejectionReason::kApertureMetadataTypeMismatch: return "aperture-metadata-type-mismatch";
		case RejectionReason::kApertureMetadataNameMismatch: return "aperture-metadata-name-mismatch";
		case RejectionReason::kApertureMetadataCountMismatch: return "aperture-metadata-count-mismatch";
		case RejectionReason::kApertureOriginMismatch: return "aperture-origin-mismatch";
		case RejectionReason::kApertureValueNotFinite: return "aperture-value-not-finite";
		case RejectionReason::kApertureWidthOutOfRange: return "aperture-width-out-of-range";
		case RejectionReason::kApertureHeightOutOfRange: return "aperture-height-out-of-range";
		case RejectionReason::kApertureAspectRatioOutOfRange: return "aperture-aspect-ratio-out-of-range";
		case RejectionReason::kFrontClearanceOutOfRange: return "front-clearance-out-of-range";
		case RejectionReason::kBackClearanceOutOfRange: return "back-clearance-out-of-range";
		case RejectionReason::kScaleNotFinite: return "scale-not-finite";
		case RejectionReason::kScaleOutOfRange: return "scale-out-of-range";
		case RejectionReason::kAxisNotFinite: return "axis-not-finite";
		case RejectionReason::kAxisLengthOutOfRange: return "axis-length-out-of-range";
		case RejectionReason::kAxesNotOrthogonal: return "axes-not-orthogonal";
		case RejectionReason::kAxesNotRightHanded: return "axes-not-right-handed";
		case RejectionReason::kWorldExtentNotFinite: return "world-extent-not-finite";
		case RejectionReason::kWorldExtentOutOfRange: return "world-extent-out-of-range";
		}
		return "unknown";
	}
}
