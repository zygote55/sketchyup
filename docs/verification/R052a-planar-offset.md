# R052.a — Planar offset kernel fixtures

Date: 2026-10-05 UTC. Preparatory geometry slice; R052 is not delivered.
M5 CI/merges and the user-facing command/tool remain prerequisites for delivery.
[Numerical and collapse contract](../decisions/0043-planar-offset.md).

The immutable adapter produces native-valid loops for all surviving regions.
No document/history/schema or provider behavior changes in this slice. Clipper2
was already vendored; no new third-party dependency is introduced.

`offset_tests` checks independently calculated areas and coordinates for a
rectangle, a concave L, a holed rectangle, a neck splitting into two islands,
merging holes and complete region/hole collapse. It checks zero-distance exact
identity, unchanged source records/allocator, reversed front orientation,
canonical results after loop rotation/hole reordering, redundant collinear
vertices, and squared acute corners instead of unbounded miter spikes.

Precision fixtures cover a micrometer square and 48 seeded continuous inset
distances on an oblique plane near the coordinate limit. Invalid IDs, NaN and
infinite/sub-tolerance distances, nonplanar or crossed boundaries, a touching
hole, out-of-range output and oversized input are rejected with specific codes.

Validation on the development host:

```sh
cmake --build --preset dev --target offset_tests topology_tests planar_tests tolerance_tests geometry_fuzz_tests --parallel 2
ctest --preset dev -R '^(offset|topology|planar|tolerance|geometry_fuzz)$'
cmake --build build/inspection-sanitize --target offset_tests --parallel 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/inspection-sanitize/offset_tests
```

All five selected CTest suites pass (3.02 s); offset also passes address,
undefined-behavior and leak checks. Native UI and live-provider trials are not
claimed for this numerical slice. Those belong to the subsequent command/tool
integration and R052 acceptance evidence.
