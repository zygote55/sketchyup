# 0059: Explicit site placement

Status: implemented for the R060.e site-placement subset.

`assembly.site_place` places an existing body and its complete descendant assembly
without rebuilding geometry. Every request must provide the stable body ID, a
three-number `position`, `positionUnit` (`m`, `mm`, `cm`, `in` or `ft`), `frame`
(`world` or `parent`) and `yawDeltaRadians` (−2π…2π). There are no inferred units,
coordinate systems or bearings. `parent` means the parent’s local frame, not the
selected body's frame. Coordinates convert to canonical metres and must remain
within the model's ±1,000,000 m bounds, including every transformed vertex.

The command rotates the existing basis about the chosen frame's positive Z axis
by the explicit yaw delta, then sets its origin to the requested position in that
frame. Existing scale and shear are preserved in that frame. World placement under
a transformed parent is converted back to the original parent-local matrix; the
parent identity never changes. Rotation is a delta, not an absolute compass heading.
This is engineering-coordinate placement, not geographic CRS conversion, terrain
following or geolocation.

## Preservation and atomic publication

One ordinary `scene.transform` command and three completion assertions execute in
a private batch. Assertions check the independently predicted world minimum/maximum
and unchanged local dimensions, with 1e-6 m absolute tolerance. The resulting frame
origin is checked to 1e-6 m and basis components to relative 1e-10. The receipt records
input and converted coordinates, both origins, before/after parent matrices,
actual completion assertions and the selected record count.

Every body identity, local geometry, property, material, tag, asset, component
definition/instance and hosted relationship must remain exact, except for the
selected root's transform. A host and every attached component must move together
through a common ancestor. Host-only and attachment-only placement reject, as do
locked targets/descendants and edits crossing canonical component ownership.
An empty target with no finite vertex geometry rejects. Existing ordinary transform
and no-change behavior apply; no separate site metadata or persistent constraints
are added. The document schema remains version 15.

Measurement work is bounded by the shared assertion limits: 256 records, 20,000
vertices, 40,000 edges, 2,000 faces and 64,000 loop corners. Unsupported or oversized
targets reject before publication. A failure rolls back preceding outer-batch
commands. A successful change is one Undo/Redo task and supports private preview,
transaction staging, headless execution and the assistant's routine-edit policy.

## Executable study

Prompt: “Place the complete study assembly at [100000125, 200000250, 12500] mm
in world coordinates and rotate its existing orientation by π/6 radians around
world Z. Preserve its scale, local geometry, materials, shared components and
hosted windows.”

The [eleven-step recipe](../../examples/site-recipe-v1.json) creates a room, roof,
stairs, table and cabinet, groups their returned roots, inspects the sole new group
for its stable reference, places it, verifies staged and committed measurements,
and saves the entire study as one transaction. Cumulative transaction receipts do
not make their first created ID the most recently created group; the explicit
inspection step avoids that assumption.

The [native starting fixture](../../examples/m6-site-before.sketchyup) contains an
explicitly adopted room with general hosted windows, the other assemblies and an
unrelated triangle. The [placed fixture](../../examples/m6-site-after.sketchyup)
moves only the study. Every actual placed vertex is checked against an independent
world delta to 1e-7 m; the unrelated triangle stays exact. Unit conversion and both
coordinate frames are tested under mirrored, nonuniform, rotated and sheared
parents. The native workflow performs a one-millimetre Move at the distant
coordinates, undoes it and reopens the container exactly.

## Native rendering follow-up

The model and numeric edits retain double precision at the distant test coordinates.
The existing viewport converts world positions and camera matrices to floats before
projection. Controlled near/far captures show visible triangle artifacts at the
large coordinates. R060.e does not claim translation-invariant rasterization;
[R060.f camera-relative rendering](0060-camera-relative-rendering.md) fixes this
viewport precision defect before the integrated M6 gate.
