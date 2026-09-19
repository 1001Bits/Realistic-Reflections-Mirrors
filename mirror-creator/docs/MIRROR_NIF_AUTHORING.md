# Making a reflective NIF

Mirror Creator prepares a surface declaration inside the NIF itself. The
**Realistic Reflections** runtime reads that declaration from a loaded world
object. Authors do not submit a FormID, filename, asset hash or per-design
registration list to our plugin.

**Development status:** prepared NIF export and the world-reference runtime
connection are compiled in. No enable file is required. Older runtime builds
do not understand this schema. The Creators ZIP contains the builder, not the
game plugin.

## Author workflow

1. **Select NIF**.
2. Pick the flat surface that should reflect. A pane may be part of a larger
   mesh. Round outlines, concave outlines and holes retain their selected faces.
   In source version 0.4, **Shift-click** adds or removes other coplanar areas,
   including separate NIF parts. They form one reflection with gaps intact.
3. Click from the side that should reflect; facing is automatic.
4. Click **Save**. The download keeps the original filename. The file you opened is not overwritten unless you save on top of it.
5. Use that NIF as the model of an ordinary object record in your Skyrim mod.
   Place the object through the Creation Kit or your mod's usual mechanism.
   Do not also assign `MOS_MirrorSurface` on the same object; the runtime
   refuses a mesh that carries both a prepared pane and a CK texture-set swap.
   With Realistic Reflections installed, the loaded surface can enter the
   existing world-mirror capture/delivery path.

The builder changes the selected pane's material to an opaque black fallback.
Single-area export preserves other geometry, transforms, original vertex attributes
and existing collision. It does not create collision where none exists. If only part of a
shape was selected, it separates those triangle indices into a sibling shape
with exactly the same transform and retains the remaining indices on the source.

Multi-area export bakes selected vertices into one common static pane, generates
its vertex attributes and retains unselected source geometry and collision
owners. Fully consumed source shapes become empty nodes at the same indices,
with their transforms, unrelated extra data and collision references retained.
Aligned broken shards share one plane and one capture; tilted or offset shards
are rejected. See [Shift-selection and verification](MIRROR_CREATOR_SHIFT_SELECTION.md).

Reflection and item behavior are separate. Skyrim still needs item records for
inventory/equipment, and a placement interaction needs records/scripts. An
author can use this prepared NIF in an existing mod without the builder writing
those records. **The preview always writes the world/placeable profile.
Arbitrary equipped hand mirrors are not integrated yet.** Their dropped/world
models can use this same declaration, but their equipped first/third-person
clones require the hand pipeline's identity, pose and render-timing integration.
Neither a marker nor a renamed node bypasses that existing contract.

## NIF surface schema 1

One static, unskinned `BSTriShape` is named `MOSReflectiveSurface:0`. Its triangles
may form disconnected islands on the same plane. It carries
these direct extra-data blocks:

| Name | NIF type | Value |
|---|---|---|
| `MOSMirrorSurface` | `NiIntegerExtraData` | `1`, schema version |
| `MOSMirrorProfile` | `NiIntegerExtraData` | `1` world/placeable intent, `2` hand intent |
| `MOSMirrorPlane` | `NiFloatsExtraData` | 14 floats: local center XYZ, normal XYZ, right XYZ, up XYZ, half width, half height |
| `MOSMirrorTriangles` | `NiFloatsExtraData` | Six floats per triangle: `(u0,v0,u1,v1,u2,v2)` |

The plane is in the pane shape's **local** coordinate space. Its center is the
selected outline's bounding-box center in the right/up basis, not the shape's
origin or the surface's area centroid. Right and up are unit perpendicular
vectors; `right × up = normal`. All triangle coordinates are normalized to
`[-1,+1]`, with counter-clockwise winding viewed from the front. A local point is
`center + right * u * halfWidth + up * v * halfHeight`. The extra-data triangle
count equals the pane shape's native triangle count.

The runtime applies the live pane transform, including negative uniform scale,
to this plane and outline. It validates finite values, orthonormal axes, extent
and triangle limits, one declared pane in the reference's current scene graph,
native geometry type/count and the actual reference owner. Actor/equipped roots
are excluded from this world-reference route. Metadata must remain immutable
for a given loaded asset. Changed geometry identity, metadata or pose invalidates
its existing candidate generation and publication before delivery.

Supported world base types are STAT, MSTT, ACTI, MISC, ARMO, WEAP, CONT and FURN.
The existing mod still owns its normal activation, visibility, cell/lifetime,
capture-resource and cleanup gates. Merely naming an unrelated or malformed
shape does not grant rendering access. No extra hooks or engine addresses were
introduced for this feature.

## Rendering and current test boundary

Recognition uses the authored plane and extents rather than fixed standing-mirror
dimensions or an object's OBND center. The custom pane draw uses the selected
triangles, preserving holes and irregular edges. It borrows those values for a
synchronous draw and uses a reusable dynamic vertex buffer. Projection coverage
checks the selected triangles; a bounding rectangle cannot fill or authorize a
hole. The native fallback is suppressed only after successful delivery through
the existing lifetime/cleanup path. The global mirror-count policy is unchanged.

The old explicit registration/schema-2 foundations in `MirrorAuthoringContract.h`
remain unwired. This NIF-local surface schema is a separate author-facing path;
it does not turn those earlier readiness flags on.

The owner-run acceptance test, after a fresh launch request, is an unrelated
STAT record using an exported NIF, followed by a partial-mesh or round example.
Check front/back facing, exact alignment, frame/holes, multiple references,
movement, unload/reload and removal of the test switch. Existing hand and built-in
standing mirrors must also retain their prior behavior. No live performance or
VR acceptance is inferred from offline math and WARP rendering.

Offline verification on September 7: both serialized product builds pass without
warnings/errors; 6/6 targeted native tests, 39/39 builder Python tests and 27/27
release-boundary tests pass. Real D3D11 WARP draws check a triangle, a contour
with a hole, hole-only visibility, buffer growth, state restoration and rejection
of malformed coordinates/equipped-hand misuse. The native schema validator
accepts all 42 declarations decoded from exported corpus NIFs (21 models, both
facings). Fifteen browser workflows pass for the source and portable servers.
The DLL-validated public release DryRun retains 80 blockers and writes nothing.
The NIF feature marker remains absent in SE, AE and VR. No candidate was deployed.
