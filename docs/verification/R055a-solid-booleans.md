# R055.a — Solid Boolean kernel evidence

Date: 2026-10-05 UTC. Depends on R054.c; CI/dependency merges remain pending.
The contract and precision policy are [0046](../decisions/0046-solid-booleans.md).
This layer provides immutable geometry and source-face provenance. Shared command
consumption/material transactions and native interaction remain subsequent work.

Manifold 3.4.1 is pinned at `31afd71d17c7a94cfaeada83f657d7b42628ead6`.
All 42 vendored src/include files match upstream byte for byte. The Apache-2.0
license hashes to
`c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`.
The local static serial CMake target performs no downloads and installs no
upstream library or headers. A temporary-prefix install verifies the retained
Manifold license and AUTHORS byte for byte. Own-code MIT licensing is unchanged.

`boolean_tests` checks:

- Two overlapping 2 m cubes: union 12 m³, subtraction/intersection 4 m³.
- Disjoint, identical, face-touching, edge-touching and corner-touching solids;
  empty intersections and complete subtraction; disconnected union/trim parts.
- A through-cut with 6 m³ volume and two actual native polygon holes, then reuse
  of that holed result as a later operand.
- Every output face's source operand/face and front/back reversal; cutting faces
  reverse correctly, and repeated calls retain deterministic geometry/provenance.
- All three operations for eight oblique rotations near (800000,-700000,600000),
  plus a rotated mirrored/nonuniform frame with analytical 16.2/5.4/5.4 m³ volumes.
- Small 1 cm operands, a 16-sided 2 cm-radius cylinder, near-coplanar overlap,
  and a native solid with an already split edge.
- An open operand rejects with its operand index and boundary-edge diagnostics;
  oversized input, excessive precision extent, surviving sub-tolerance output
  and enclosed cavity shells reject explicitly. Inputs remain immutable.

Each positive output part independently passes native surface, topology and solid
validation. Analytical expected volumes are checked separately from the adapter's
reported volume. Native loops retain meaningful face boundaries and holes rather
than exposing interchange tessellation triangles as editing faces.

The seven targeted CTest suites (booleans, solids, tolerance, geometry fuzz,
intersections, sweep and topology) pass in 3.80 s. The full Boolean fixture
corpus also passes ASan, UBSan and leak detection with the vendored adapter
instrumented. CI and dependency merges remain pending.

The large-coordinate and mirrored fixtures exposed adapter provenance slivers.
The final contract includes a stable face-aligned frame, 12.5 nm input grid,
25 nm output normalization, removal of zero-width backtracking boundary spikes,
and propagation of shared collinear vertices. Native solid classification and
volume checks remain mandatory after that normalization. No exact-predicate or
arbitrary-precision claim is made. Enclosed cavity shells remain explicitly
unsupported until native containment handling is implemented.
