# 0044 — Profile sweep frames and validation

Status: immutable kernel verified locally; document/native integration pending.

R053 sweeps one planar face along a bounded polyline without changing the source
surface or its allocator. The path starts in the profile plane; its start point
is the profile anchor. The shortest rotation aligns the profile normal with the
first segment. An antiparallel first segment rotates around the first profile
edge. Subsequent segments use minimal-rotation parallel transport, with no
user-supplied roll or scale. Curved paths are explicitly faceted polylines.

At a corner, parallel profile rails intersect the tangent-bisector plane, creating
one shared miter ring. Miters above four times the profile radius and direction
reversals reject with `SWEEP_SHARP_TURN`. Every longitudinal rail must retain
positive length above the native tolerance; otherwise `SWEEP_SELF_INTERSECTION`
explains that the profile consumes the segment. Open paths have two caps. Closed
paths have no seam cap and must return the transported frame within the specified
angular/vector and profile-radius-scaled tolerance; otherwise `SWEEP_TWIST`
requests a split or planar path. An exact repeated closing station is removed.

Output includes both cap IDs, generated side faces keyed by each canonical source
boundary vertex pair, and faces ordered by path segment. Hole winding is normalized
against the outer face. Open holed profiles produce connected hollow solids.
Closed holed profiles currently reject with `SWEEP_CLOSED_HOLES`: they require
multiple-shell containment validation that the current native solid classifier
does not provide. No volume is claimed by summing unvalidated shells.

The native surface/topology and solid validators remain authoritative. Crossing
surfaces reject before any document mutation; complexity, profile validity,
coordinate range and degenerate results have separate actionable codes. Admission
allows 128 stations, 256 profile vertices, 32 loops and 4,096 output faces, subject
also to the existing solid-analysis budget. Arithmetic is floating point at the
native 100 nm tolerance, with quantized tessellation; this is not exact geometry.

Valid coplanar miter faces exposed independent tessellation rounding at a shared
edge. The solid classifier now applies its existing four-tolerance contact rule
to positive-area triangle overlap only when the entire convex overlap lies in
one authoritative shared-edge tube (including covered collinear edge chains) or
one shared-vertex ball. It still rejects overlap elsewhere. This does not merge
identities, increase tolerance, or accept unrelated coincident surfaces.

No new dependency, persistence format, provider behavior or user-facing command
is introduced by the kernel slice. R053 remains incomplete until shared-command,
selection, preview/cancel, Undo/Redo and native workflow acceptance are verified.
