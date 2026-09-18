#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Head tilt for the reflected player in first person (owner, 2026-09-17:
// "when moving up and down i want the player head to tilt up and down a bit in
// the reflection").
//
// The hidden third-person body gets no Pitch channel (MirrorPlayerAimSteady
// zeroes it so the arms stay still), so its head would never follow the view.
// After the engine writes the animated pose, the neck and head are turned about
// the body's own left-right axis by a fraction of the view pitch. Skyrim's
// look direction is (sin h cos p, cos h cos p, -sin p): positive pitch looks
// down, so a positive tilt lowers the chin.
//
// Matrices follow NiMatrix3: column vectors, world = parentWorld * local.
namespace MirrorHeadTiltMath
{
	using Mat3 = std::array<std::array<float, 3>, 3>;

	// Owner, run 13: the same angle up and down when moving the mouse.
	// Both directions follow 0.9 of the view pitch, capped at 45 degrees.
	// Split 40/60 between neck and head so the bend reads as a nod.
	inline constexpr float kTiltPerPitch = 0.9F;
	inline constexpr float kMaximumTilt = 0.785398F;
	inline constexpr float kTiltPerPitchUp = kTiltPerPitch;
	inline constexpr float kMaximumTiltUp = kMaximumTilt;
	inline constexpr float kNeckShare = 0.4F;
	inline constexpr float kMinimumTilt = 1.0E-4F;

	[[nodiscard]] inline float TiltFor(float a_pitch) noexcept
	{
		if (!std::isfinite(a_pitch))
			return 0.0F;
		if (a_pitch < 0.0F)
			return (std::max)(a_pitch * kTiltPerPitchUp, -kMaximumTiltUp);
		return (std::min)(a_pitch * kTiltPerPitch, kMaximumTilt);
	}

	[[nodiscard]] inline Mat3 Multiply(const Mat3& a, const Mat3& b) noexcept
	{
		Mat3 r{};
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		return r;
	}

	[[nodiscard]] inline Mat3 Transpose(const Mat3& a) noexcept
	{
		return { { { a[0][0], a[1][0], a[2][0] }, { a[0][1], a[1][1], a[2][1] }, { a[0][2], a[1][2], a[2][2] } } };
	}

	[[nodiscard]] inline std::array<float, 3> Apply(const Mat3& m, const std::array<float, 3>& v) noexcept
	{
		return { m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
			m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
			m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] };
	}

	// A rotation, not a scaled or collapsed matrix: a parent whose world
	// transform has never been updated is left alone.
	[[nodiscard]] inline bool IsRotation(const Mat3& m) noexcept
	{
		const Mat3 identity = Multiply(Transpose(m), m);
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (!std::isfinite(identity[i][j]) || std::fabs(identity[i][j] - (i == j ? 1.0F : 0.0F)) > 0.02F)
					return false;
		const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
		                  m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		                  m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
		return det > 0.98F && det < 1.02F;
	}

	// World rotation lowering the chin by `a_tilt` for a body facing `a_heading`:
	// A(h) takes body axes (x right, y forward, z up) to world, B(t) pitches the
	// body's forward axis down about x.
	[[nodiscard]] inline Mat3 WorldTilt(float a_heading, float a_tilt) noexcept
	{
		const float c = std::cos(a_heading), s = std::sin(a_heading);
		const float ct = std::cos(a_tilt), st = std::sin(a_tilt);
		const Mat3 bodyToWorld{ { { c, s, 0.0F }, { -s, c, 0.0F }, { 0.0F, 0.0F, 1.0F } } };
		const Mat3 pitchDown{ { { 1.0F, 0.0F, 0.0F }, { 0.0F, ct, st }, { 0.0F, -st, ct } } };
		return Multiply(Multiply(bodyToWorld, pitchDown), Transpose(bodyToWorld));
	}

	// New local rotation so that parentWorld * local' == WorldTilt * parentWorld * local:
	// the bone turns about the world axis through its own pivot and its children follow.
	[[nodiscard]] inline Mat3 TiltLocal(const Mat3& a_parentWorld, const Mat3& a_local, float a_heading, float a_tilt) noexcept
	{
		const Mat3 world = WorldTilt(a_heading, a_tilt);
		return Multiply(Multiply(Multiply(Transpose(a_parentWorld), world), a_parentWorld), a_local);
	}
}
