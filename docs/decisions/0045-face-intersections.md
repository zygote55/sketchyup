# 0045 — Bounded face intersection geometry

Status: immutable numerical kernel, shared command and native interaction verified locally;
CI/dependency acceptance remains pending.

R054 begins with two validated planar faces in a common coordinate frame. AABB
and signed-plane ranges reject disjoint candidates. For crossing planes, solve
their common line near the first face's bounds, clip it against each face's
ordered boundary loops, and intersect the retained intervals. Midpoint material
classification preserves concave gaps and holes. Boundaries belong to the closed
face region; point-only or sub-tolerance line contacts create no zero-length edge.

For coplanar faces, project into a stable orthonormal frame near the first face's
bounds and intersect normalized outer/hole loops with the existing Clipper2
adapter at a 10 nm grid. Return the complete common-region boundary, including
holes. Separately retain finite collinear shared-boundary contacts, since a
zero-area polygon intersection has no region boundary. Merge overlapping and
contiguous collinear segments within the native 100 nm tolerance, avoiding
duplicate output where a source edge has extra collinear vertices. A surviving
common region with sub-tolerance boundary edges rejects atomically instead of
silently losing an island or emitting an incomplete region boundary.

Coplanar admission requires all vertices within the native plane tolerance and
normal cross-product magnitude below 1e-8. Nearly parallel intersecting planes
outside that plane tolerance reject as unstable. Distinctly oriented faces whose
entire separation lies below the modeling tolerance also reject; a tiny pair of
perpendicular faces must not be flattened into a supposed coplanar region.

The immutable adapter admits at most 1,024 vertices and 64 loops per face, 4,096
candidate output edges and four million normalization comparisons. Coplanar
boundary contact comparisons are bounded by the input vertex limits. Existing
surface validation checks finite coordinates, planarity, self-crossing boundaries
and strictly contained holes. The input surfaces and allocators never change.
Errors distinguish invalid faces, bounded-work limits, unstable intersections
and sub-tolerance common regions. Floating-point and grid rounding remain part
of the contract; no exact-predicate or unrestricted geometry guarantee is made.

The shared `geometry.intersect` command takes explicit `entities` (each with
`body` and `face`), an editable `context` (including `"0"` for the model root),
and `mode`: `selected`, `context` or `model`. Selected mode compares the target
faces with each other. Context reads faces in that editable raw-geometry context,
respecting group boundaries. Model reads persistently visible scene faces across
groups; hidden/tag-hidden references are excluded. Locked reference geometry may
be read, but locked/hidden/out-of-context targets reject.

Only bodies containing explicit targets are edited. All pair calculations finish
before mutation; candidate bounds prune pairs, world coordinates establish the
intersection and each result returns to its target's local frame. Collected edges
pass through native planar arrangements. Incident boundary splits may update
other faces in the same body, and coplanar arrangement partitions retain material
coverage under the existing hole/appearance rules. Contexts and groups are never
merged. Ambiguous appearance merges and native arrangement limits reject rather
than discard unrelated faces or materials.

The command bounds targets to 128 faces, references to 2,048 faces/16,384 boundary
vertices, candidate pairs to 100,000, aggregate pair vertex products to four
million, and output to 8,192 edges with at most 1,024 per target plane. The existing
planar arrangement adds its own source-plus-output limits. An existing seam or
point-only result is a core no-op; unchanged automation batches retain the usual
no-committed-change rejection and do not add history.

Face descendant maps compose across every affected plane, appearance follows
those descendants, and all changed bodies publish as one edit. Preview uses the
same command and stays read-only. Undo/Redo and ordinary persistence retain native
identities. Within an explicit component instance scope, model references combine
the current definition draft with an immutable read of the outer scene, excluding
the chosen instance's old geometry. Siblings and outside references are never
edited by a unique-instance request. Definition-only model mode requires an
instance scope because a definition alone has no scene placement.

Native Intersect (I) previews the current face selection through the same command
and ToolSession. Draw's reference submenu selects Selected faces, Active context,
or Model; changing it rebuilds the active preview. Enter/click commits one edit,
Escape cancels, and orbit retains the preview. Selected source faces map to their
surviving descendants, including scene IDs inside component scopes. Intervening
manual changes invalidate pending work. Unchanged seams have a native no-new-edges
message. Viewport-only temporary reference hiding is not a persistent visibility
filter for this command.

No new dependency or file format is introduced. R054 remains open until
prerequisite CI/merges complete.
