#pragma once

/**
 * Skyrim VR only: an owner-tunable local offset for the equipped hand mirror's
 * RRMirrorItem node, so the handle sits in the VRIK hand instead of through it.
 *
 * Read from the [VRHandGrip] section of Data\SKSE\Plugins\MirrorsOfSkyrim.ini:
 * fOffsetX / fOffsetY / fOffsetZ in game units and fRotateX / fRotateY /
 * fRotateZ in degrees, in the RRMirrorItem parent frame. Translation axes
 * follow that attachment frame, not the rotated pane. With the default-off
 * VRPresentationFix marker, the VRIK body clone additionally receives its
 * measured +5 X grip correction. Otherwise all-zero settings leave the mesh
 * untouched. Flat runtimes ignore the section entirely.
 */
namespace HandMirrorVRGripOffset
{
	void OnDataLoaded() noexcept;
	void OnGameLoaded() noexcept;
}
