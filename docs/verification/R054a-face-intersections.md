# R054.a — Face intersection kernel fixtures

Date: 2026-10-05 UTC. Preparatory kernel slice; R054 is not delivered.
[Numerical contract](../decisions/0045-face-intersections.md).

`intersection_tests` verifies independently calculated endpoints/lengths for
perpendicular faces, holes splitting a line into two intervals, concave gaps,
coplanar overlap/containment, a face inside a hole, a partial shared edge,
point-only contact, parallel separation and opposite front orientations.
Coincident faces and redundant collinear vertices retain a single nonduplicated
boundary. Operand order retains the same crossing geometry.

Sixteen rigid oblique transforms near a large origin retain the tested crossing
endpoints within 20 nm and the coplanar common perimeter within 100 nm. A
submillimeter fixture retains its expected intersection length. These are
measured fixture bounds, not universal guarantees for arbitrary conditioning.
Near-parallel crossing faces, tiny uncertain plane separation, sub-tolerance
common-region edges, invalid/nonplanar faces and excessive input reject with
specific codes. Inputs and allocators remain exactly unchanged.

Five targeted suites (`intersections`, `planar`, `topology`, `tolerance`,
`geometry_fuzz`) pass in 2.88 s. Subsequent added precision-boundary fixtures also
pass in the intersection suite. Address/undefined-behavior/leak checks cover the
same kernel. Document commands, native interactions and topology insertion are
not claimed by this numerical slice; CI/dependency merges remain required.
