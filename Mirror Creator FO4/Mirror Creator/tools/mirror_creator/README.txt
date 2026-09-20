Mirror Creator for Fallout 4 1.0 - Realistic Reflections - Mirrors

1. Extract the complete Creators ZIP.
2. Open the Mirror Creator folder and double-click Start Mirror Creator.cmd.
3. Select a Fallout 4 NIF, then click the flat area that should reflect.
   Click from the side that should show the reflection. Shift-click to add or
   remove other areas on the same plane, including separate model parts.
4. Save downloads a prepared NIF. Keep your original as a backup.
5. Put the saved NIF under your mod's Meshes folder. In Creation Kit, use it as
   a Static object's model, place the object, and save your plugin.

Drag to rotate, right-drag to pan, and scroll to zoom. The checker pattern shows
the selected surface. Holes and gaps remain empty. Close stops the tool's local
server; you can then close its browser tab. Closing the tab alone leaves the
server available for the next launch.

The download keeps the input filename; your browser may add a number when that
name already exists in Downloads. Opening a file never overwrites it. You can
reopen a prepared NIF to change the selection; include the complete previous
reflective surface or start again from the original NIF.

Use the matching player mod supplied with this Creator release, or a newer build
with prepared-NIF support. Players need Realistic Reflections - Mirrors, its
enabled ESM, your prepared NIF and your plugin. Load the shared ESM when authoring
your plugin and keep it enabled. Do not also assign MOF_MirrorSurface or another
material swap to the prepared pane. No activation file is needed.

Windows 10/11 x64 and a WebGL 2 browser such as Edge or Chrome are required.
Python and the viewer are included. No Python installation, game installation,
additional packages or internet connection is needed to run Mirror Creator.
Everything is processed on your computer. This tool does not install game files.

Supports static Fallout 4 stream-130 BSTriShape geometry, including half- and
full-precision vertices. Select one flat area or combine several areas on the
same plane into one reflection. Unselected geometry, transforms and existing
collision are preserved; collision is not created or reshaped. The preview shows
geometry, not game materials or a live reflection. Animated, skinned, curved
reflective surfaces, LOD and precombined objects are unsupported. Extract NIFs
from BA2 archives before selecting them. Skyrim NIFs need the Skyrim tool.

See docs/MIRROR_CREATOR.md for the guide and current validation status.

If port 8766 is occupied, open PowerShell in this folder and run:
  .\tools\mirror_creator\Start-MirrorCreator.ps1 -Port 8767

LICENSES AND SOURCE
The tool is adapted from the first-party Skyrim Mirror Creator and is supplied
under GNU GPL v3 (COPYING). Python source, browser source and file tests are
included. Run tests with:
  .\runtime\python.exe -m unittest discover -s tests/mirror_creator -p test_creator.py
Native validator integration requires MIRROR_CK_VALIDATOR to name the built CK
asset-test executable; it is skipped when that optional executable is absent.

Three.js 0.180.0 is MIT; its license and provenance are in
tools/mirror_creator/mirror_creator_web/vendor. The bundled Python 3.14.7 runtime
has its PSF license in runtime/LICENSE.txt. The small example NIFs and test meshes
are original Realistic Reflections fixtures. The tool creates no ESP records or
settlement recipes; create those in your authoring tools and test the result in game.
