# Mirror Creator NIF authoring preview

Load a NIF, click a surface to see its CK part name and 3D Index, optionally Shift-click more areas, then Save under the same filename; CK assigns MOS_MirrorSurface to one whole flat part in the loaded NIF.

The source preview now also has [eight vanilla selector samples and verified
preview fixes](MIRROR_CREATOR_VANILLA_SAMPLES_20260907.md). Plates/trays open from
above, and the selected pane is rendered separately from its original triangles
to eliminate false gaps in the highlight. The existing portable 0.3 ZIP predates
these two fixes and Shift-selection. The separate 0.4 portable ZIP includes them;
the old 0.3 archive has not been replaced. Share the complete ZIP, then extract
it and run **Start Mirror Creator.cmd**. A localhost link is local to your PC.

## Use

Double-click **Start Mirror Creator.cmd**. The portable Windows x64 ZIP includes
Python and the local 3D viewer; no installation or internet connection is needed.
From source, Python 3.10 or later and a WebGL 2 browser are required.

1. **Select NIF**.
2. Click the pane from the side that should reflect.
   Optionally **Shift-click** other flat areas to add or remove them.
3. **Save**. The download uses the same filename as the file you opened.

The model fits automatically. Drag to rotate, right-drag to pan, and scroll to
zoom. Each click selects a connected flat region. Shift-click combines areas
on the same plane, including separate NIF parts, into one continuous reflection;
gaps stay empty. This supports broken glass whose shards remain aligned.
Ordinary click replaces the selection. Clicking from the opposite side reverses
its facing. See [Shift-selection details and tests](MIRROR_CREATOR_SHIFT_SELECTION.md).
Save downloads a prepared `.nif` under the same filename as the file you
opened; that original file stays intact. Load the saved NIF with Select NIF
to edit it again. There are no type, project, example, parts-list,
selection-mode, flip, or extra export controls. Selection shows the CK part name
and 3D Index; partial or multiple-part selections explain when to use Save.

The export writes the world profile. Equipped-hand integration and item records
remain separate work. World reflection needs Realistic Reflections installed.
No enable file is required. The internal world profile uses the compatible
`placeable` API value.

Input and output are `.nif` model files; reflective metadata is embedded in the
saved copy. The tool is a portable Windows application with a local browser UI,
bundled Python and bundled Three.js. No Python installation, internet connection
or Skyrim installation is required for the selector itself. Fallout 4 import,
export and metadata recognition are not implemented. Its mirror plugin currently
uses an explicit model catalogue, so Skyrim exports cannot be used there directly.

## Supported input and preservation

The first importer accepts little-endian Skyrim SE/VR stream-100 NIFs with
static, unskinned `BSTriShape` geometry under `NiNode`/`BSFadeNode` scene roots.
Parent translation, row-major rotation, uniform scale and inherited hidden flags
are applied when presenting geometry. Unknown non-scene blocks remain in the
original file; unsupported scene geometry is rejected rather than silently
omitted. LE/other-game streams, skinned/animated meshes, particles, cyclic or
instanced scene nodes and sheared transforms are not supported by this preview.

Limits: 64 MB per NIF, 4,096 blocks, hierarchy depth below 96, 250,000 vertices
and 500,000 triangles per model. The local session retains at most four models
and 128 MB of original NIF data. Reopen a NIF if it has been evicted.
Textures, skin animation and collision are not displayed. Import success does
not prove a model is suitable for either in-game mirror profile.

Selection follows shared edges, welding positional UV seams, while validating
against one plane and consistent triangle winding. It preserves selected
triangle IDs, nonrectangular boundaries and holes. Shift-click explicitly allows
disconnected regions on the same plane and facing side. Curvature, offset or
tilted additions and non-manifold edges are rejected when encountered.
This is not a general mesh repair or self-intersection validator.

Prepared NIF downloads preserve unrelated blocks, geometry and existing collision.
Multi-area export bakes the selected static geometry into one common pane;
its new vertex attributes replace the selected originals. Source collision owners
remain in place, including as empty nodes when all their triangles were selected.
The backend still supports the old deterministic selection-project endpoints for
compatibility and offline tests; these are not part of the simplified interface.
No ESP, registration catalog or game installation is written by the browser tool.

## Implementation and verification

The Python core is [mirror_creator_core.py](../tools/mirror_creator_core.py);
[mirror_creator.py](../tools/mirror_creator.py) serves the loopback-only UI and
bounded import/selection/project endpoints. The existing lossless block reader
is reused. Static SE vertex data follows the
[Niftools stream definition](https://github.com/niftools/nifxml/blob/develop/nif.xml)
and the vendored engine vertex descriptor. Browser ray picking and orbit use
[Three.js](https://github.com/mrdoob/three.js/tree/r180), pinned to 0.180.0 with
MIT license and per-file hashes in `tools/mirror_creator_web/vendor/`. The
single edited vendor import points to the bundled core. There are no CDN calls
or other external requests during operation. Host/origin checks and a custom
request header prevent another website from using the local file-processing
endpoints. HTTP handling is independent of the hidden launcher's stdout.

Offline verification covers importer failures, transformed planes, connected
selection within combined geometry, seams, opposite faces, holes, malformed
topology, both project profiles, tampering and local HTTP boundaries. Browser
checks exercise real file inputs, 3D clicks, orbit/zoom, facing, downloads,
saved-NIF reopening, failed-import recovery and compact/narrow layouts.
The project corpus includes all 26 current distribution/editable sample NIFs.
No Skyrim process is needed or launched by these checks.

```powershell
python -B -m unittest discover -s tests -p 'test_mirror_creator*.py' -v
python -B tools/mirror_creator.py --port 8765
# In another shell, with the optional Playwright test dependency available:
python -B tests/mirror_creator_browser_smoke.py --output D:\MirrorCreator-tests
```

Test results and screenshots for this development run are retained in
`D:/RealisticReflections-build/reviews/mirror-creator-preview-20260907/`.
The optional Playwright test dependency is isolated on D:; it is not a runtime
dependency of the builder. Automated browser rendering uses Edge's headless
SwiftShader path, not an in-game performance measurement.

Source version 0.4 verification: **48 Python checks**, **9 Shift workflows**,
**11 existing browser workflows**, **16 vanilla workflows** and **45 exported
declarations checked by the native runtime policy** pass. Evidence is in
`D:/RealisticReflections-build/reviews/mirror-creator-shift-areas-20260907/`.
The source preview is `http://127.0.0.1:60522/`. The portable 0.4 ZIP is under
`D:/RealisticReflections-build/mirror-creator/releases/0.4-preview-20260907/`;
its packaging and bundled-runtime verification record is under
`D:/RealisticReflections-build/reviews/mirror-creator-0.4-share-20260907/`.

Historical version 0.3 verification: **39 Python checks** and **11 browser workflows** pass.
The latter cover the four-control interface, both profiles, automatic facing,
round and partial-shape export, unchanged input, error recovery, saved-NIF
reopening, orbit/zoom and both desktop/narrow layouts. Evidence and screenshots:
`D:/RealisticReflections-build/reviews/mirror-creator-simple-20260907/`.

Historical version 0.2 verification: **39 Python checks**, **15 browser workflows** from
both the source server and the extracted portable tool, **6 targeted native
CTests**, and **42 real exported declarations** checked by the native schema
validator pass. The declaration corpus covers both facings of 21 pane-bearing
NIFs; five inventory-only models have no pane. All 26 import successfully.
The portable tool also passes its 39 checks with bundled Python and system
Python removed from PATH. Both runtime products build without warnings/errors;
27 release-boundary checks pass. Runtime installation and in-game acceptance
remain pending. Evidence is in
`D:/RealisticReflections-build/reviews/v185-nif-mirror-authoring-20260907/`.

Remaining full-product work includes in-game acceptance of NIF world recognition,
equipped-hand integration, both item/record profiles, hand fitting and installable
add-on packaging. The author-tool ZIP can be shared for NIF preparation tests.

Historical 0.1 verification: **31/31 Python checks** (0.709 seconds), **12/12 browser
workflows**, and **26/26 sample NIF imports** passed. The browser reported no
script errors or external runtime requests. The owner-facing preview was opened
through the tested launcher; owner acceptance of their own asset remains pending.
