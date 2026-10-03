# R003.b verification

Date: 2026-10-03. Host: native-spike baseline. Source fixtures and assertions:
`tests/arrangement_tests.cpp`; representation decision: [ADR 0004](../decisions/0004-topology.md).

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev
build/dev/arrangement_tests
cmake --preset sanitize
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
```

Observed: core, arrangement and persistence suites pass. ASan/UBSan core and
arrangement suites pass. The 100-cut arrangement corpus plus all other fixtures
runs in approximately 20 ms in the development build and 170 ms under sanitizers
on the baseline machine. These are whole-corpus timings, not editing latency or
a large-document performance claim. The executable prints the actual face/edge
split maps for reproducible inspection.

This closes the isolated arrangement/identity feasibility experiment. It does
not deliver a public split command, persistent edge schema, adjacent-face
propagation or robust arbitrary near-coplanar intersections. Those remain the
explicit M2 implementation boundaries in ADR 0004.
