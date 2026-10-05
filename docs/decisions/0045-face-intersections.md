# 0045 — Bounded face intersection geometry

Status: immutable numerical kernel; context-scoped document integration pending.

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

This slice introduces no dependency or user-facing command. Follow-up integration
must choose explicit editable targets and selected/context/model reference sets,
apply world/local transforms, insert intersection edges through native planar
arrangements, preserve group boundaries/materials/lineage and verify preview,
Undo/Redo and persistence. R054 remains open until that behavior is accepted.
