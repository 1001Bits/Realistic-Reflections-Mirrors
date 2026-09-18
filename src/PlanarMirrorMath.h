#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include <DirectXMath.h>

namespace PlanarMirrorMath
{
	/** A normalized world-space plane expressed as dot(normal, position) = distance. */
	struct Plane
	{
		DirectX::XMFLOAT3 normal{ 0.0f, 0.0f, 1.0f };
		float distance{ 0.0f };
	};

	/** Inputs used to recover a mirror plane from an authored mesh transform. */
	struct PlaneSelectionInput
	{
		DirectX::XMFLOAT3 center{};
		std::array<DirectX::XMFLOAT3, 3> worldAxes{};
		std::array<float, 3> localHalfExtents{};
		DirectX::XMFLOAT3 eye{};
		std::int32_t normalAxisHint{ -1 };
		float maximumThicknessRatio{ 0.35f };
		float minimumFacingCosine{ 0.02f };
		bool requireThinAxis{ false };
	};

	/** Result of plane selection, including a stable surface basis for later mirror-UV work. */
	struct PlaneSelection
	{
		Plane plane{};
		DirectX::XMFLOAT3 center{};
		/** Canonical authored normal. Unlike plane.normal, this never flips with the viewer. */
		DirectX::XMFLOAT3 authoredNormal{};
		DirectX::XMFLOAT3 tangent{};
		DirectX::XMFLOAT3 bitangent{};
		std::int32_t normalAxis{ -1 };
		float facingCosine{ 0.0f };
		float confidence{ 0.0f };
		bool viewOriented{ false };
		bool renderable{ false };
	};

	/**
	 * Result of the last fail-closed source-eye clearance check performed before
	 * a reflected mirror pass.  The reflecting plane is authored through the
	 * center of a slab. The legacy gate added the oblique clip bias to the native
	 * near-plane/slab clearance even though that bias moves the replacement clip
	 * plane away from the reflected eye; the corrected gate does not.
	 */
	struct MirrorCaptureClearance
	{
		float sourcePlaneDistance{ 0.0f };
		float requiredDistance{ 0.0f };
		bool safe{ false };
	};

	enum class MirrorCaptureClearancePolicy : std::uint8_t
	{
		/** Historical conservative boundary retained for default-off A/B safety. */
		kLegacyNearPlaneSlabAndClipBias,
		/** Keep the native near plane outside the slab; oblique bias is not additive. */
		kNativeNearPlaneOutsideSlab
	};

	/** The signed-short local bounds stored by a Bethesda OBND record. */
	struct SignedShortBounds
	{
		std::array<std::int16_t, 3> minimum{};
		std::array<std::int16_t, 3> maximum{};
	};

	/** A uniform-scale local-to-world transform expressed with its rotation columns. */
	struct UniformWorldTransform
	{
		DirectX::XMFLOAT3 translation{};
		std::array<DirectX::XMFLOAT3, 3> rotationColumns{};
		float scale{ 1.0f };
	};

	/** Validated oriented bounds recovered from OBND and a scene-graph world transform. */
	struct OrientedBounds
	{
		DirectX::XMFLOAT3 localCenter{};
		DirectX::XMFLOAT3 worldCenter{};
		std::array<DirectX::XMFLOAT3, 3> worldAxes{};
		std::array<float, 3> localHalfExtents{};
		std::array<float, 3> worldHalfExtents{};
	};

	/** Normalize a plane in place. Returns false for non-finite or degenerate input. */
	bool NormalizePlane(Plane& plane) noexcept;

	/** Signed distance from a point to a normalized plane. */
	float SignedDistance(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept;

	/**
	 * Evaluate whether a source eye is far enough in front of an oriented mirror
	 * plane to start the private reflected pass.  Returning false means the input
	 * itself was invalid; a valid but insufficient/opposite-side distance returns
	 * true with output.safe == false.
	 */
	bool EvaluateMirrorCaptureClearance(
		const Plane& orientedPlane,
		const DirectX::XMFLOAT3& sourceEye,
		float normalHalfThickness,
		float cameraNearPlane,
		float clipBias,
		MirrorCaptureClearance& output,
		MirrorCaptureClearancePolicy policy =
			MirrorCaptureClearancePolicy::kLegacyNearPlaneSlabAndClipBias) noexcept;

	/** Reflect an absolute position about a normalized plane. */
	DirectX::XMFLOAT3 ReflectPoint(const Plane& plane, const DirectX::XMFLOAT3& point) noexcept;

	/** Reflect a direction about a normalized plane. */
	DirectX::XMFLOAT3 ReflectVector(const Plane& plane, const DirectX::XMFLOAT3& direction) noexcept;

	/**
	 * Build oriented world bounds from a signed-short OBND and the column-basis transform used by NiTransform.
	 * Negative or zero scale, invalid bounds, and non-orthogonal bases are rejected.
	 */
	bool BuildOrientedBounds(
		const SignedShortBounds& bounds,
		const UniformWorldTransform& transform,
		OrientedBounds& output) noexcept;

	/**
	 * Select the thin axis when usable local extents are supplied, otherwise select the authored axis most
	 * nearly facing the eye. A valid geometric selection is preserved at grazing incidence or when the eye is
	 * exactly at the center; callers must inspect renderable before attempting a reflected pass.
	 */
	bool SelectPlaneFromBasis(const PlaneSelectionInput& input, PlaneSelection& output) noexcept;

	/**
	 * Transform a plane into camera space. Matrices use the engine's row-vector convention
	 * (viewProjection = view * projection).
	 */
	bool TransformPlaneToView(
		const Plane& plane,
		const DirectX::XMFLOAT4X4& view,
		DirectX::XMFLOAT4& cameraPlane) noexcept;

	/**
	 * Replace the D3D 0..1 projection's near clip equation with cameraPlane. cameraPlane is expressed as
	 * ax + by + cz + d >= 0 for retained geometry. This is the row-vector D3D form of oblique near clipping.
	 */
	bool BuildObliqueNearProjection(
		const DirectX::XMFLOAT4X4& projection,
		const DirectX::XMFLOAT4& cameraPlane,
		DirectX::XMFLOAT4X4& output) noexcept;

	/** World-space pane rectangle used to fit the reflected capture frustum. */
	struct PaneFit
	{
		DirectX::XMFLOAT3 center{};
		DirectX::XMFLOAT3 tangent{};
		DirectX::XMFLOAT3 bitangent{};
		float halfTangent{ 0.0f };
		float halfBitangent{ 0.0f };
	};

	/**
	 * Compare every value that defines the pane-fitted capture rectangle.  This
	 * is deliberately stricter than a plane comparison: a same-generation pane
	 * may translate in-plane, rotate about its normal, or change scale without
	 * changing its clipping plane.
	 */
	[[nodiscard]] inline bool SamePaneFit(
		const PaneFit& left,
		const PaneFit& right,
		float tolerance = 1.0e-4f) noexcept
	{
		if (!std::isfinite(tolerance) || tolerance < 0.0f)
			return false;
		const auto sameValue = [tolerance](float a, float b) noexcept {
			return std::isfinite(a) && std::isfinite(b) &&
			       std::abs(a - b) <= tolerance;
		};
		const auto sameVector = [&sameValue](
			const DirectX::XMFLOAT3& a,
			const DirectX::XMFLOAT3& b) noexcept {
			return sameValue(a.x, b.x) && sameValue(a.y, b.y) &&
			       sameValue(a.z, b.z);
		};
		return left.halfTangent > 0.0f && left.halfBitangent > 0.0f &&
		       right.halfTangent > 0.0f && right.halfBitangent > 0.0f &&
		       sameVector(left.center, right.center) &&
		       sameVector(left.tangent, right.tangent) &&
		       sameVector(left.bitangent, right.bitangent) &&
		       sameValue(left.halfTangent, right.halfTangent) &&
		       sameValue(left.halfBitangent, right.halfBitangent);
	}

	/** Derive the exact pane rectangle used by capture and delivery. */
	bool BuildPaneFit(
		const PlaneSelection& plane,
		const std::array<float, 3>& worldHalfExtents,
		const std::array<DirectX::XMFLOAT3, 3>& worldAxes,
		PaneFit& output) noexcept;

	/** Exact reason a pane-fitted projection could not be built. */
	enum class PaneFitFailure
	{
		kNone,
		kInvalidInput,
		kInvalidReferenceProjection,
		kPaneNotFullyInFront,
		kInvalidSlope,
		kInvalidOutput
	};

	/** Corners nearer than this view depth clamp to it instead of failing the
	 *  pane fit. The 2026-08-19 ordinal-attributed runtime evidence proved the
	 *  all-or-nothing kPaneNotFullyInFront rejection was the missing-content
	 *  mechanism: oblique camera states push one near corner across the
	 *  epsilon, every frame falls back to the unfitted reference projection,
	 *  and pane-edge content (the watched fern) collapses to 0-4 samples. The
	 *  slope clamps below bound the widened frustum, mirroring the
	 *  kMaximumCullSlope precedent. */
	inline constexpr float kMinimumPaneFitCornerDepth = 1.0f;

	/**
	 * Build an off-center row-vector D3D projection whose frustum tightly covers
	 * the pane rectangle as seen from the camera, reusing the reference
	 * projection's exact near/far depth mapping. The view matrix follows the
	 * engine convention (rotation with the absolute origin supplied separately),
	 * so pane corners are made camera-relative before transformation. Corners
	 * nearer than kMinimumPaneFitCornerDepth clamp to it (cornerClamped reports
	 * this); the fit returns false — for caller fallback to the reference
	 * projection — only when every corner is at or behind the camera plane or
	 * the inputs degenerate.
	 */
	bool BuildPaneFittedProjection(
		const PaneFit& pane,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& referenceProjection,
		float marginScale,
		DirectX::XMFLOAT4X4& output,
		PaneFitFailure* failure = nullptr,
		bool* cornerClamped = nullptr) noexcept;

	/** Saturation cap for pane cull slopes (~88 degrees off-axis). Corners at or
	 *  behind the camera's forward plane clamp here instead of failing, so the
	 *  widened cull stays continuous across grazing/close poses. */
	inline constexpr float kMaximumCullSlope = 32.0f;

	/** Signed view-space slope interval (offset per unit forward distance) that
	 *  bounds the pane rectangle as seen from a camera basis. */
	struct PaneCullSlopes
	{
		float minRight{ 0.0f };
		float maxRight{ 0.0f };
		float minUp{ 0.0f };
		float maxUp{ 0.0f };
	};

	/**
	 * Bound the pane rectangle's four corners as signed slopes in the camera
	 * basis, expanded by marginScale about each interval's centre. Used to
	 * widen a private cull frustum so frustum culling never rejects content
	 * the pane-fitted projection can display: a frustum copied from the source
	 * camera swings with the view direction while the pane framing stays
	 * anchored to the mirror, so pane-edge content (an off-axis NPC, geometry
	 * behind the player) was culled while remaining visible in the pane.
	 * Fails — for caller fallback to the unmodified frustum — when a corner
	 * reaches the camera's forward plane or any input degenerates.
	 */
	bool BuildPaneCullSlopes(
		const PaneFit& pane,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT3& forward,
		const DirectX::XMFLOAT3& up,
		const DirectX::XMFLOAT3& right,
		float marginScale,
		PaneCullSlopes& output) noexcept;

	/** Viewing cone of a pane from the reflected eye: four side planes through
	 *  the pane edges plus the pane plane itself, all oriented so positive
	 *  signed distance is inside. Content is pane-visible only when its bound
	 *  intersects this cone, which makes it the correct-by-construction cull
	 *  test for supplemental submissions (the engine's native rejection
	 *  heuristics drop pane-visible content; all-pass needs a cap lottery). */
	struct PaneVisibilityCone
	{
		std::array<DirectX::XMFLOAT4, 5> planes{};
		std::size_t planeCount{ 0 };
	};

	/** Fails (for caller fallback to no filtering) when the eye lies on the
	 *  pane plane or any input degenerates. */
	bool BuildPaneVisibilityCone(
		const PaneFit& pane,
		const DirectX::XMFLOAT3& reflectedEye,
		PaneVisibilityCone& output) noexcept;

	/** True when the sphere (plus slack) is not fully outside any cone plane.
	 *  On rejection, optional outputs receive the failing plane index and the
	 *  sphere centre's signed distance to it (negative = outside). */
	bool SphereIntersectsPaneCone(
		const PaneVisibilityCone& cone,
		const DirectX::XMFLOAT3& center,
		float radius,
		float slack,
		std::size_t* rejectPlane = nullptr,
		float* rejectDistance = nullptr) noexcept;

	/**
	 * Build an oblique projection and view-projection from an absolute world plane. Fallout 4's camera view
	 * matrix contains rotation while CameraStateData::posAdjust supplies the absolute origin, so cameraOrigin
	 * is removed from the plane before it is transformed into view space.
	 */
	bool BuildObliqueViewProjection(
		const Plane& absoluteWorldPlane,
		float clipBias,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection,
		DirectX::XMFLOAT4X4& outputProjection,
		DirectX::XMFLOAT4X4& outputViewProjection,
		DirectX::XMFLOAT4* outputCameraPlane = nullptr) noexcept;
}
