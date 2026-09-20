# Mirror Creator for Fallout 4

Mirror Creator lets you open a Fallout 4 NIF, click a flat surface and save a
prepared mirror model. Shift-click combines additional areas on the same plane,
including areas in separate model parts. The tool preserves the selected outline,
holes and gaps, and leaves the original file untouched.

The optional **Creators** download contains both this standalone tool and the
Creation Kit helper. They are two ways to author a mirror; you do not need to use
both on the same pane. Use the matching main mod package from this release, or a
newer build with prepared-NIF support. Older player DLLs do not discover these
prepared meshes automatically.

## Select and save

1. Extract the complete Creators ZIP. Open **Mirror Creator** and double-click
   **Start Mirror Creator.cmd**. The viewer opens in your default browser.
2. Click **Select NIF** and choose a loose Fallout 4 model. Extract it from its
   BA2 first if necessary.
3. Click the flat area that should reflect, from the side that should show the
   reflection. The connected flat area receives a checker pattern.
4. Hold **Shift** and click to add other areas on the same plane. Shift-click a
   selected area to remove it. Aligned shards can share one continuous reflection
   while the gaps remain empty. Areas at different angles or depths cannot share
   one reflection.
5. Click **Save**. Your browser downloads a prepared NIF using the original
   filename; it may append a number when a download with that name already exists.
   Choose a separate destination and retain the original as a backup.

Drag to rotate the preview, right-drag to pan, and scroll to zoom. The selection
shows the source model part name and its NIF block number. This block number is
not a Skyrim Alternate Textures 3D Index.

To change the reflective side, rotate around the model and click from the other
side. The selected front is lighter than its reverse in the checker preview.
You can reopen an exported NIF. Include the whole previous reflective surface
when changing or extending it; otherwise start again from the original model.

**Close** stops the local tool. Closing only the browser tab leaves it running
for the next launch. The viewer and Python are bundled; no internet connection,
Python installation or game installation is needed to use the tool. It requires
Windows 10/11 x64 and a browser with WebGL 2, such as Edge or Chrome. Files are
processed locally and are not uploaded.

## Use the model in your mod

1. Install the matching main Realistic Reflections - Mirrors mod and enable
   **Realistic Reflections - Mirrors.esm**.
2. Put your prepared NIF under your mod's **Meshes** directory.
3. In Fallout Creation Kit, load the shared ESM and your own plugin, with your
   plugin active. Create or duplicate a **Static** object and set its model to
   the prepared NIF.
4. Place the object, save your plugin and test it in game through F4SE.

No additional material assignment or activation file is required. Do not also
assign **MOF_MirrorSurface**, or replace the prepared pane's material with another
swap. The NIF already carries the mirror surface and its recognition marker.
The standalone Creator does not depend on a specific CK version. The separate
CK helper's 1.11.240.0 restriction applies only to its editor integration.

Players need the main mod, its enabled ESM, your plugin and your prepared meshes.
Keep the shared ESM enabled even if a cleaning tool removes an unused master:
NIF material paths alone do not establish a plugin dependency. CK and the Creator
preview do not display a live reflection. Without the player mod, the prepared
pane has a black fallback material.

## Supported models

- Static Fallout 4 NIFs using BSTriShape, NiNode and BSFadeNode, with standard
  half- or full-precision vertices. Skyrim NIFs require the Skyrim Creator.
- One flat reflective surface per Static object. Several coplanar regions are
  combined into one surface on export, preserving their actual triangles.
- Unselected geometry, materials, transforms, opaque extra data and existing
  collision are retained. Collision is not generated or reshaped.
- Curved reflective regions, animated or skinned models, LOD and precombined
  geometry are unsupported. Keep the placed pane out of precombined geometry so
  it retains its own loaded 3D. Apply negative scales in your model editor first.
- The preview shows geometry rather than game textures, physics or animations.
  Hidden parts remain hidden and cannot be selected.

The tool does not create plugins, workshop recipes or collision. It rejects
unsupported input instead of converting it to a different game format. If you
edit or optimise the exported NIF afterwards, retain its mirror material and
root extra-data marker, and check the result again in game.

## Validation and source

The file tests cover half- and full-precision input, transforms, reversed facing,
holes, disconnected regions, collision preservation, invalid files and re-export.
Exported panes also pass the native Fallout CK asset reader. Browser tests cover
actual picking, Shift selection, download, reopen, error handling and shutdown.
The universal player DLL includes prepared-mesh recognition, with native tests
for valid and invalid root markers. In-game visual confirmation of this new
authoring path remains pending.

The package includes the source, original test fixtures and GNU GPL v3 license.
Three.js retains its MIT license and the bundled Python runtime its PSF license.
The tool is adapted from the first-party Skyrim Mirror Creator.
