# 0044 — Profile sweep frames and validation

Status: immutable kernel, shared command and native tool implemented; acceptance below.

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

The shared `geometry.sweep` command takes `body`, `face`, `path`, optional `closed`
(false), and `space` (`local` by default, or `world`). It preserves the source and
creates a separate editable sibling body, or a child when the profile belongs
directly to a group. Local paths use the source coordinate frame; world paths
sweep the transformed profile, then convert output back to that frame. Thus
nonuniform and mirrored placement does not distort a world-space sweep. The
result inherits the selected face's color and front/back materials and source tag.
Locked sources reject. Component edits use the existing explicit shared or unique
instance scope, including scene/canonical member translation.

The response's `sweeps` array names `sourceBody`, `sourceFace`, output `body`,
`caps`, `sides` (`vertices` source pair and generated `faces`), and `segments`.
These identify surviving originally generated faces, not descendants produced
by later batch operations. Deleted output bodies are omitted; deleted faces are
removed while edge/segment slots remain. Instance-scoped results resolve body
IDs to scene IDs; `componentOperations[].sweeps` retains canonical definition
member IDs for definition clients. Mapping keys describe the original source
even if a later explicitly requested operation deletes it.

Read-only preview and atomic publication use the same command. Cancel publishes
nothing, and Undo removes only the generated body. Persistent output uses ordinary
native geometry; no new file format or dependency is needed. R053 remains open
until native interaction acceptance and prerequisite CI/merges are complete.

Native Follow Me uses Select to collect exactly one profile face and one connected
edge path in a single path body. Shift+F validates and previews it; Enter or click
publishes, Escape cancels, and Alt-drag orbits without losing the preview. Open
paths start at the endpoint in the profile plane nearest its center; closed
paths start at the nearest eligible station, with stable identity ordering for
ties. Branches, disconnected selections and missing plane-aligned starts reject.
Path geometry is sampled in world coordinates. Source/profile/path selection is
retained after Apply. The active shared-component scope follows ordinary native
editing rules; unique-instance edits remain available through the explicit command.
