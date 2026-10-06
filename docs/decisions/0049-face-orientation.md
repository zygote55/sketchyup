# 0049 — Face orientation follows an explicit reference

Status: immutable kernel fixtures and sanitizers pass locally; CI/dependency
merges and downstream publication/native acceptance are pending. Roadmap R057.a, following R056.e.

Reverse changes every ordered loop of each explicitly selected face, including
hole loops. Orient keeps one explicit seed face's winding and traverses only its
edge-connected component. Shared two-face edges constrain neighboring loop
incidences to opposite directions. A breadth-first parity assignment computes
all reversals before copying and changing the surface. IDs, vertices, wires,
face/edge counts, topology records and allocators are unchanged. Results name the
changed faces and the selected or traversed component in stable ID order.

Orient supports open sheets and closed material/cavity boundaries. It does not
guess an outward direction, bridge a wire or vertex-only contact, or flip another
shell merely because it is geometrically contained. A consistently inward seed
keeps its inward component; the user can Reverse the reference first. Enclosed
cavities therefore retain their intended material-side convention. An already
consistent component returns no reversed faces.

A traversed edge with more than two incident faces rejects as
`ORIENTATION_NON_MANIFOLD`; inconsistent cycle constraints reject as
`ORIENTATION_CONFLICT`. Both identify a native face and edge. A non-manifold edge
in an unrelated component does not block an independent sheet. Explicit Reverse
is still valid on selected faces of a non-manifold surface because it does not
claim to repair or orient the whole component.

Limits apply before validation/traversal: 1–16,384 faces, 65,536 vertices, 131,072
edges, 131,072 loose wires, 4,096 boundary uses per face, 131,072 total boundary
uses and four million summed squared face-boundary uses for validation work.
Traversal is linear in the bounded face/edge incidence graph. Inputs are immutable;
no guessed welding, deletions, triangulation changes or partial repair is published.

The following document layer must swap front/back materials on exactly the
reversed faces so that existing physical sides retain their appearance. It must
also preserve curves, metadata and identity lineage, enforce editing scope,
support component publication, preview and one Undo item. Native controls and
edge softening/smoothing/hiding are later R057 layers; this kernel does not claim
the complete roadmap entry.


R057.b publishes `geometry.reverse_faces` (`context`, `entities` containing
`body`/`face`) and `geometry.orient_faces` (`context`, `body`, reference `face`).
Reverse accepts 1–4,096 explicit faces across at most 128 raw bodies. Every target
must be visible, unlocked and editable in that context. Duplicate face entries
reject. Orient traverses only the selected reference body's connected surface;
an already consistent result becomes the batch's usual no-change rejection.

One staged edit replaces affected body records and swaps material sides on the
reported reversed faces. Body defaults, colors, opacity/assets, metadata,
placements, curve bindings and authoritative topology stay intact. An override is
removed when the swapped sides equal the unchanged body defaults. Material zero
keeps its legacy color fallback on the same physical side. This convention also
holds under reflected/nonuniform placements. Ordinary change receipts retain
stable face IDs and report changed incidence on affected edges.

Both commands participate in catalog/schema publication, assistant policy,
component-scoped ID mapping, preview, stale-revision checking, batch rollback,
Undo/Redo and native-container persistence. Native controls and exporter-side
front/back handling are separate acceptance layers; this command slice does not
remove the existing GLB back-material loss report.
