# Realistic Reflections - Mirrors: authoring your own mirrors

Install Realistic Reflections - Mirrors and enable **Realistic Reflections - Mirrors.esm** in the game's
plugin list. This single ESM contains both the workshop mirrors and the authoring
surface. Load it in Creation Kit when making your own mirrors.
Authors assign **MOF_MirrorSurface** to a flat model part, place the object and
save their plugin. Authored mirrors work automatically when the main mod, this
ESM and the author's plugin are enabled.

The **Realistic Reflections - Mirrors CK helper** adds an Alternate Textures picker to Static
objects' Model Data. It creates Fallout's normal Material Swap records for you.
The optional **Creators** download includes the helper, the standalone Fallout
**Mirror Creator**, and the same ESM, materials, textures and workshop meshes as
the main mod. Players need the main mod and your content files; these tools are
only needed by authors.

## Mirror Creator: select a surface in a NIF

Open **Mirror Creator/Start Mirror Creator.cmd** from the extracted Creators ZIP.
Select a Fallout NIF, click its flat pane, then **Save**. Hold **Shift** to combine
additional areas on the same plane, including separate model parts. Holes and
gaps retain their shape. Put the prepared NIF in your mod's Meshes directory,
assign it as a Static object's model in CK, place the object and save your plugin.

Do not also assign MOF_MirrorSurface in CK: the exported NIF is already prepared.
Load the shared ESM when authoring and keep it enabled in game. Use the matching
main mod package from this release, or a newer build with prepared-NIF support;
earlier DLLs do not recognise this authoring route. The standalone NIF tool has
no CK version restriction. See [Mirror Creator](MIRROR_CREATOR.md) for its guide.

## Creation Kit helper: assign an existing whole part

1. Copy **Data** and **MirrorAuthoring** from the optional **Creators** download
   into the Fallout 4 folder containing CreationKit.exe.
   Start MirrorAuthoring\MirrorsOfFalloutCK.exe. It opens or connects to your
   Fallout CK. With MO2, run this helper through MO2 so it sees the same Data files.
2. Load Realistic Reflections - Mirrors.esm and select your own ESP as the active file.
   Create or duplicate a Static object for your mirror.
3. Open a Static object's **Model Data → Alternate Textures...**. Select its
   flat pane, choose **MOF_MirrorSurface**, and click **Assign**. Confirm Model
   Data and the Static dialog with OK. Place the object and save your ESP.
4. Declare Realistic Reflections - Mirrors and the enabled Realistic Reflections - Mirrors.esm as player
   requirements. Material paths do not establish plugin dependencies by themselves;
   keep the shared ESM enabled even if a cleaning tool removes an unused master.

No material path entry or manual swap duplication is needed. The picker names
the actual mesh parts and explains why an unsupported part cannot be selected.
The pane needs a unique BGSM path, unused by the object's other mesh parts:
Fallout's material swaps apply by material path. Only one pane per object can
use the marker. Choosing a different pane replaces the previous assignment.
The former pane returns to its original NIF material. A replacement material
overwritten by an earlier mirror assignment is not recovered automatically.

The editor helper supports the verified **Fallout CK 1.11.240.0** executable.
The authored records use the same material format supported by the mod on .163,
.240 and VR. Players install the mod with its single ESM; the helper runs only
in CK. If you start the helper after opening Model Data, reopen that dialog.
Other CK versions are refused until their editor interfaces are verified.

Loose NIFs and Fallout BA2 versions 1, 7 and 8 are read without modifying them.
If different archives contain different versions of one mesh, extract the
version your CK uses as a loose NIF for authoring; the helper does not guess
archive precedence. Its material list must also match CK's loaded preview.

The original animated bathroom cupboard mesh is outside the static-pane
authoring support. Use a static workshop mesh or your own static NIF as a base.
The helper source includes original flat and round-hole test panes. Existing
built-in bathroom mirrors retain their separate runtime support.

## Manual workflow for another CK version

Duplicate MOF_MirrorSurfaceSwapTemplate into your ESP. Set Original Material to
the pane's unique BGSM path and keep Replacement Material as
MirrorsOfFallout\Authoring\MOF_MirrorSurface.bgsm. Paths are relative to Materials.
Assign that swap in the Static object's Model Data, then place and save normally.
The template's original path is the vanilla bathroom glass material; replace it
when your pane uses a different material. A Texture Set on a head part or an
unassigned TXST record alone does not tag a Fallout object.

## Test your mirror

Enable your saved plugin alongside Realistic Reflections - Mirrors.esm and start
the game through the matching F4SE. Visit the placed object to check its reflection.
No activation file or separate runtime authoring download is required.

The DLL recognises the assigned CK material or a prepared NIF, verifies the actual mesh is flat, and renders
the reflection. CK previews the fallback material, not a live reflection. Check
RealisticReflectionsMirrors.log for [MirrorAuthoring] initialization and accepted/rejected
reference messages. Check the result in game before publishing your plugin.

## Supported geometry and settings

One flat, unskinned, static reflective BSTriShape per STAT object. Mirror Creator
can combine several selected areas into that one part. Its triangles must face
consistently. Round outlines, disconnected coplanar islands and holes use the
actual triangles; they are not filled with a rectangular reflection. Curved,
animated, skinned, LOD and precombined geometry is unsupported. Keep the pane out
of precombined geometry so it retains its own loaded 3D. Repeated instances work.

MCM resolution controls capture quality and refresh rate controls each mirror's
update target. Flat Fallout supports multiple author mirrors alongside existing
mirrors. VR reserves the one largest physical mirror; smaller mirrors never
borrow its slot, and the original small bathroom mirrors remain excluded.

The shared ESM has no vanilla overrides, scripts or DLC/ESL dependency. The CK kit
includes its workshop meshes as well as the authoring material and textures. Use
it with the matching Realistic Reflections - Mirrors DLL on .163, .240 or VR.

## Updating earlier content plugins

The sole content file is now Realistic Reflections - Mirrors.esm. Replace the
previous mod installation and enable this master; do not leave
MirrorsOfFalloutWorkshop.esp or Realistic Reflections - Mirrors.esp installed.
Their workshop and authoring record IDs are unchanged, but existing saves and
author plugins still name their old master. Use a fresh game for testing, or
migrate those dependencies before loading existing placements. Renaming the
installed file alone does not update a save's master list.

The older two-record MirrorsOfFallout-Authoring.esm is also retired. Its local
IDs 000800/000801 now belong to workshop objects; its surface and swap references
must be remapped to 000B00/000B01 as well as changing the dependency. Reassigning
the pane with the current helper creates a custom swap in the author's ESP.
The marker material and texture paths have not changed.
