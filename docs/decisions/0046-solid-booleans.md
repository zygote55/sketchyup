# 0046 — Bounded solid Boolean adapter

Status: immutable kernel fixtures and sanitizers pass locally; CI/dependency acceptance pending.

R055 uses [Manifold 3.4.1](https://github.com/elalish/manifold/tree/v3.4.1), pinned
at `31afd71d17c7a94cfaeada83f657d7b42628ead6`, as a replaceable solid operation
adapter. Native Surface loops, topology and stable IDs remain the document
representation, as required by the R003/R007 decision. Manifold's Apache-2.0
license and author list are retained and installed; SketchyUp's own code stays
MIT licensed. Its unmodified source/header trees are vendored for offline builds.
A local static serial target disables optional cross-section, TBB, bindings and
downloads, retaining upstream's floating-point contraction/precision flags.

The immutable adapter accepts two native, validated single-shell solids in a
common coordinate frame. Native solid classification rejects open boundaries,
loose/non-manifold geometry, inconsistent winding, self-intersection, analysis
limits and multiple shells before running a Boolean. Invalid operand errors
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

Union, subtraction and intersection return zero or more disconnected positive
shells. Empty and exactly zero-volume contacts are valid empty kernel results. Enclosed negative cavity shells currently
reject with `BOOLEAN_CAVITY`; they must not be published as filled positive solids.
Native multiple-shell containment remains required before that case is enabled.

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

Shared command behavior, operand consumption, native UI, Undo/persistence and
material acceptance belong to the following R055 layers.
