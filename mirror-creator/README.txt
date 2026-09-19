Mirror Creator 0.5 preview — Realistic Reflections

1. Extract this entire ZIP to a folder you can write to.
2. Double-click Start Mirror Creator.cmd. A local browser preview opens.
3. Select NIF.
4. Click the pane from the side that should reflect.
   Shift-click to add or remove other areas on that same flat plane.
   The selection shows its CK part name and 3D Index for Alternate Textures.
5. Save downloads a prepared NIF with the same filename as the file you opened.

Drag to rotate, right-drag to pan, scroll to zoom. The checker outline shows
your selected surface. To change the reflective side, rotate the model and
click the other side. Open a saved NIF using Select NIF to edit it again.

The original NIF is never overwritten. All processing stays on your computer.
Python and the 3D viewer are included. No Skyrim installation, Python install,
Node, extra packages or internet connection is needed to use the tool.
Requires Windows 10/11 x64 and a WebGL 2 browser such as Edge or Chrome.
Share this complete ZIP; the localhost browser address only works on your PC.

IN-GAME STATUS
The exported NIF has its reflective surface, plane, front and exact triangle
outline embedded as extra data. It has an opaque black fallback pane.
World objects using that NIF reflect when Realistic Reflections is installed.
Do not also assign MOS_MirrorSurface on the same object. No enable file is
required. This ZIP is the authoring tool, not the game mod.

Use the NIF as an ordinary object model in your own Skyrim mod. No per-design
registration in our plugin is needed for the marked world surface. This tool
does not create ESP/item records, placement scripts, collision or equipment
fits. See docs/MIRROR_NIF_AUTHORING.md for exact status.

SUPPORTED INPUT
Static, unskinned Skyrim SE/VR stream-100 BSTriShape NIFs. Pick one flat region
or Shift-click several coplanar areas for one reflection. Gaps, round contours
and holes are preserved. LE, skinned/animated and
curved reflective surfaces are not supported. Materials/textures/collision
are preserved where unrelated to the selected pane but are not previewed.
Fallout 4 NIF import/export and its runtime recognition are not implemented.

If another preview is using port 8765, run in PowerShell:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Start-MirrorCreator.ps1 -Port 8766

LICENSES AND SOURCE
Tool source is included under GPL-3.0 (LICENSE) when that file is present.
Three.js is MIT with license and provenance in tools/mirror_creator_web/vendor.
The included Python 3.14.7 embeddable runtime carries its PSF license in
runtime/LICENSE.txt. The examples are first-party Realistic Reflections assets.
