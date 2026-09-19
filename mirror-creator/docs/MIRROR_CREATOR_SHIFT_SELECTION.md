# One reflection across several selected areas

September 7, 2026. Implemented in the source Mirror Creator 0.4 preview at
`http://127.0.0.1:60522/`. The existing portable 0.3 ZIP is unchanged.

Click one flat region, then **Shift-click** others to combine them. Shift-click
an already selected region to remove it; removing the last disables Save.
Ordinary click replaces the selection. The toolbar shows **hold shift to select multiple parts**.

The areas can belong to different static NIF parts. They must lie on one plane
and face the same side. Angled or offset additions show an error and preserve
the current selection. Clicking from the front handles opposite source winding.
The checker uses one common coordinate system, so its pattern continues across
the selected areas while gaps remain empty.

This supports a broken mirror with aligned shards: the exported NIF declares
one pane, one plane and the exact combined triangles. Missing glass stays
missing. Shards angled independently would need separate reflections, which
this selection workflow does not create.

## Export and preservation

The exporter recomputes selections from source triangle IDs and facing; it does
not trust submitted plane metadata. It bakes selected vertices through their
static parent transforms into the first selected part's local coordinate space,
then checks the actual float32 output against the original world positions.
The new pane uses standard SSE position/UV/normal/tangent attributes and the
existing opaque black fallback material. Unselected source triangles and their
original attributes remain unchanged.

A fully consumed source shape becomes an empty NiNode at its existing block
index, preserving its transform, unrelated extra data and collision owner.
Partially consumed shapes retain their remaining triangles and collision.
This creates no new collision. The existing static/unskinned input restrictions
apply; animated shards are unsupported.

One combined pane retains schema 1 and the existing format limits of 65,535
triangles and 65,535 indexed vertices. These are geometry limits, not a limit
on the number of mirrors in a scene. One capture covers the combined bounds;
spreading pieces farther apart can reduce pixels per unit at fixed resolution.
The legacy internal selection-project endpoints retain their connected-region
contract; the simple interface saves and reopens prepared NIFs.

## Verification and manual test

48 creator Python tests, 9 Shift browser workflows, 11 existing browser workflows
and 16 vanilla browser workflows pass. The existing native schema validator
accepts 45 declarations decoded from actual exports, including transformed and
negative-scale parts. Browser checks cover both profiles, shared checker/gaps,
add/remove/clear, ordinary replacement, saved-NIF reopening, opposite winding
and invalid additions preserving the exact savable selection. Browser suites
run serially because the local server intentionally retains at most four models.
No browser errors or external requests occurred in the final runs.

Evidence, source before-images and the scoped change patch are in
`D:/RealisticReflections-build/reviews/mirror-creator-shift-areas-20260907/`.
Two purpose-made selector fixtures and reference screenshots are in
`D:/RealisticReflections-build/mirror-creator/vanilla-selector-samples-20260907/Shift selection examples/`.
They are synthetic test geometry, separate from the eight original vanilla NIFs.
Click the left area, Shift-click the right, Save, and reopen the result.

This is offline author-tool verification. No runtime DLL, installed asset,
marker or existing ZIP changed. No game was launched or stopped. Generic world
NIF reflections still require in-game acceptance of the existing default-off
runtime; arbitrary equipped hand mirrors remain unfinished. The acquisition
proposal remains awaiting owner approval.
