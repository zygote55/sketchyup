# R053.a — Profile sweep kernel fixtures

Date: 2026-10-05 UTC. Preparatory kernel slice; R053 is not delivered.
[Frame and rejection contract](../decisions/0044-profile-sweep.md).

`sweep_tests` independently checks volume and mappings for straight and reversed
paths, a right-angle miter, a 16-segment quarter-circle, a closed rectangle, an
open hollow elbow, reversed profile orientation, a concave profile and a spatial
path. The miter vertices satisfy their analytical bisector plane. Twelve oblique
orientations near a large origin retain corresponding corner positions within
20 nm and volume within 2 × 10⁻⁶ m³ for the tested elbow. A submillimeter straight
profile retains its expected volume. These fixture tolerances are not universal
error bounds for arbitrary geometry.

Twisted closure, direction reversal, global crossing, consumed short segments,
duplicate stations, misaligned start, invalid/nonplanar profile, nonfinite path,
out-of-range output and admission limits reject with classified errors. Closed
holed profiles explicitly reject pending multiple-shell support. Success and
failure preserve the exact source surface and allocator.

The solid regression includes a coplanar folded tetrahedron whose overlapping
interiors still reject after the shared-edge rounding correction. Existing
crossing, faceted-cylinder, winding, non-manifold, near-coordinate-limit and
analysis-budget fixtures remain passing.

Validation:

```sh
cmake --build --preset dev --target sweep_tests solid_tests entity_measure_tests glb_export_tests topology_tests tolerance_tests geometry_fuzz_tests --parallel 2
ctest --preset dev -R '^(sweep|solids|entity_measure|glb_export|topology|tolerance|geometry_fuzz)$'
cmake --build build/inspection-sanitize --target sweep_tests solid_tests --parallel 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/inspection-sanitize/sweep_tests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/inspection-sanitize/solid_tests
```

All seven selected suites pass (3.66 s). Sweep and solid fixtures also pass
address, undefined-behavior and leak checks. CI and dependency merges remain
pending. No document/native interaction or live-provider acceptance is claimed
for this kernel slice.
