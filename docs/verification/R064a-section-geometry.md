# R064.a — Derived section geometry

2026-10-06, implementation `c9ad9fc`. This layer is the bounded geometry adapter;
document persistence, section controls, viewport picking/rendering and export
integration remain subsequent R064 work. The M6 gate and dependency checks still
apply. [Contract](../decisions/0072-section-geometry.md).

All **113 CTest suites passed in 110.85 seconds**, including the real Blender
smoke test. The new geometry suite also passed ASan/UBSan with leak detection in
**0.34 seconds** and no suppressions. Logs:
`build/r064a-complete-{build,ctest}.log` and
`build/r064a-sanitize-{build,ctest}.log`. Neither final build emitted warnings.

Geometry fixtures cover analytic half-cube and three-plane volumes/cap areas,
vertex-aligned tetrahedron cuts, tangent/coincident faces, fully removed inputs,
hollow solids, nested islands, disconnected contours, open/nonmanifold boundaries,
reflection/shear/rotation/translation, millimetre features and coordinates near
the model bound. Invalid identities, coefficients, point values and input/plane
budgets reject explicitly. Point and wire clipping agree with the retained side.

Eighty deterministic oblique samples compare complementary volume conservation,
cap area and two-plane order independence. They exposed a seam caused by restoring
cap vertices from rounded contour coordinates. The final implementation reuses
exact source boundary positions; all samples now close and pass. For every native
clipped vertex, independent reconstruction from original-triangle barycentric
weights verifies texture interpolation and source face identity. Cap winding and
typed generated identities are checked separately.

The installed decision document matches its source. The source archive contains
all **109 explicit install inputs byte-for-byte**, without build output or Git
metadata. [Package results](R064a-source-package.json).
