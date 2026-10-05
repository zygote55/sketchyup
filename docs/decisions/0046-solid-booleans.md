# 0046 — Bounded solid Boolean adapter

Status: kernel, shared command and native acceptance pass locally; CI/dependency merges pending.

R055 uses [Manifold 3.4.1](https://github.com/elalish/manifold/tree/v3.4.1), pinned
at `31afd71d17c7a94cfaeada83f657d7b42628ead6`, as a replaceable solid operation
adapter. Native Surface loops, topology and stable IDs remain the document
representation, as required by the R003/R007 decision. Manifold's Apache-2.0
license and author list are retained and installed; SketchyUp's own code stays
MIT licensed. Its unmodified source/header trees are vendored for offline builds.
A local static serial target disables optional cross-section, TBB, bindings and
downloads, retaining upstream's floating-point contraction/precision flags.

The immutable adapter accepts two native, validated material solids in a
common coordinate frame. Native solid classification rejects open boundaries,
loose/non-manifold geometry, inconsistent winding, self-intersection, analysis
limits and disconnected material components before running a Boolean. Enclosed
inward cavity boundaries belong to their surrounding outer material shell. Invalid operand errors
retain the operand index and native defect face/edge/vertex IDs. A consistently
inward-wound input is normalized to outward adapter winding without changing the
source. Double-precision meshes are recentered at the joint bounds and oriented to an
orthonormal frame from the first target face. Coordinates round to a 12.5 nm
grid (one eighth of native tolerance), with a 12.5 nm adapter simplification
tolerance. This keeps nominally coplanar transformed inputs consistent at native
resolution and avoids a large world origin enlarging the numerical tolerance.
Operand extents whose adapter tolerance exceeds 25 nm reject as a precision
limit. Features inside the rounding grid can become contacts; surviving
sub-tolerance output boundaries reject.
Native tessellation corners recover their source identities within tolerance;
ambiguous or unrepresentable tessellation rejects instead of welding nearby
sheets. Manifold performs its own closed-mesh validation as an additional check.

Union, subtraction and intersection return zero or more disconnected material
parts. Empty and exactly zero-volume contacts are valid empty kernel results.
R056.b attaches each enclosed negative boundary to its smallest enclosing positive
shell using [native containment](0047-shell-containment.md). Native polygon and
source-face identities are remapped without welding independent boundaries.
Each material part is independently revalidated; material islands inside a cavity
are separate parts. Independent positive point/edge contacts keep separate bodies.

Candidate bounds only exclude impossible parents. Every remaining positive/cavity
pair runs native manifold, intersection, winding and containment analysis. The
cavity must be an immediate enclosed boundary in that pair. Candidate-pair work
has a conservative four-million budget, charged by squared combined triangle count
plus four times that count before each analysis. Every assembled body and aggregate
output volume are independently checked. Input winding normalization reverses the
entire material boundary together, preserving relative cavity orientation. A hollow
result can be reused as an operand; it is never silently filled.

Each source operand receives an adapter original ID and each input triangle its
native source face ID. Reconstruction follows [Manifold's face provenance
contract](https://manifoldcad.org/docs/html/structmanifold_1_1_mesh_g_l_p.html):
triangles with the same operand, source face and orientation are grouped, interior
halfedges cancel, and the remaining loops become native polygon faces with holes.
Triangles are an adapter interchange representation, not the published editing
faces. Explicit merge indices preserve adapter vertex identity; independent
shells touching at a point remain independent. Output simplification runs at
25 nm. Only connected edges shorter than 25 nm can collapse, and every merged
vertex must remain within that displacement budget. Boundary traversal pairs
material-left edges in the source plane. Zero-width backtracking spikes within
25 nm of their replacement segment are removed; shared collinear boundary
vertices then propagate through every incident native face. These steps remove
provenance slivers without turning separate nearby sheets into one surface.
Open, ambiguous, non-manifold or surviving sub-tolerance boundaries reject. Each output face retains source operand,
source face and reversal, allowing the command layer to preserve front/back
appearance on cutting faces. Reversal comes from input winding and operation
semantics, rather than ill-conditioned normals of tiny triangles. Source-plane
distances and well-conditioned triangle orientation provide additional checks.

Native face/topology validation and independent solid classification run on every
reconstructed part. Reconstructed volume must match the adapter within the larger
of four native tolerances times surface area and 1e-9 relative volume. Part order
uses world bounds and volume, not globally reserved adapter IDs. Inputs are
limited to 4,096 vertices/2,048 faces and 8,192 triangles each; pair triangle
products, corner searches and output boundary normalization each have a
four-million budget. Output is limited to
16,384 vertices, 32,768 triangles and 64 disconnected shells. Native validation
adds its own limits. These are bounded floating-point operations, not exact
predicates or unrestricted solid guarantees.

## Shared command

`geometry.boolean` requires `body` (target), `tool`, `context`, `operation`
(`union`, `subtract`, `intersection`) and boolean `keepOperands`. Both operands
must be different editable, visible raw geometry bodies in the explicit context;
enter group containers first. Surfaces are evaluated in world space, including
mirrored/nonuniform placements. Baking reflected source placements into world
geometry and results back to the target frame reverses loop order, preserving
physical front/back consistently with rendering and consolidation. Provenance
therefore describes physical material-side reversal even when only one operand
is reflected. Each disconnected output becomes a sibling body
in the target's local frame, inheriting its tag and default appearance. Each face
inherits its source color and material sides, swapping front/back when reversed.
Recipe properties are not copied onto newly generated solids.

Keeping operands retains their exact records. Consuming them deletes both in the
same atomic history item as the generated bodies. Empty results with retention
produce no change and the ordinary batch reports that fact; empty results with
consumption explicitly remove both sources. Failures, stale revisions and later
batch errors leave the model and allocators unchanged. Preview shares the same
geometry, appearance and provenance with commit. Undo/Redo and native persistence
retain the complete operation.

The `booleans` receipt contains source/tool bodies, operation, retention choice and
parts. Each part has its body ID, operation-time world `generatedVolume` and face
mappings (`face`, `sourceBody`, `sourceFace`, `reversed`). Later batch edits prune
deleted parts/faces; volume is a generation measurement, not a later-state claim.
Scoped component commands map both operands. Definition receipts use canonical
IDs; explicit instance receipts resolve original sources before consumption and
results after publication to scene IDs. Unique instance edits preserve the source
definition and siblings. The command is published consistently through CLI,
transaction/session/MCP discovery and the assistant command allowlist.

## Native controls

Solid Boolean (Shift+B) accepts two editable raw bodies, or selected faces from
two raw bodies, in the active context. The initial operation is union and Keep
originals starts enabled. Draw → Solid Boolean options selects union, target
minus tool, or intersection, toggles retention, and swaps target/tool. Initial
order follows stable body IDs; viewport and status text identify both by name
and ID. Swapping reverses that order and recomputes the preview. Choosing another
operation or retention policy also recomputes through the shared command.

Enter or click publishes the validated ToolSession preview; Escape leaves source
records, selection, allocators and history intact. Alt/right/middle navigation
retains the preview. Manual document changes invalidate it. Invalid solids report
operand and defect identities, and retained empty results explain the no-change
outcome. Empty consumption explicitly states that both originals will be removed.
Generated parts are selected after publication (none for an empty result).
Shared component editing uses the established scope path and scene mappings;
Make Unique remains the existing explicit route to independent instance edits.
Native display acceptance is recorded separately in R055.c.
