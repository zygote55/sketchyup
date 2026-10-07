# R082.b — Reuse unchanged component projections and validated geometry

2026-10-07. Component placement previously reconstructed the materialized bodies
of every existing instance, including unchanged leaf definitions. The projector
now reuses immutable bodies only when the leaf definition, binding, placement
and every seeded member retain their validated identity. Nested definitions,
edited definitions and changed placement/member records follow full projection.
Document validation requirements and all complexity limits remain unchanged.

The opt-in `component_projection_benchmark` constructs the same canonical
1,000-instance, 100,000-triangle prism fixture as R082.a through public document
operations. It measures construction before serialization or viewport work and
records process peak RSS at that point. It is not a timing-based CTest gate.

| Debug construction run | Elapsed | Peak process RSS |
| --- | ---: | ---: |
| [Before](R082b-construction-before.json), `df3f658` | 240,110.499 ms | 55,824,384 B |
| [Leaf projection reuse](R082b-construction-after.json), `f606124` | 199,975.817 ms | 38,338,560 B |
| [Validation reuse](R082b-construction-validation-reuse.json), `3cbe259` | 106,158.497 ms | 37,388,288 B |

All three produce 2,000 bodies, 100,000 triangles and a 7,021,923-byte native document
with SHA-256 `c874449f6143db01040c1b0df5f26dbc0f90daee66cd2635453eeac73c11376d`.
Other builds and CPU-only provider evaluation ran concurrently. These single
Debug measurements are exploratory, not a controlled speedup guarantee or
viewport, release-performance or whole-session memory acceptance.

The ten selected regressions cover component records, shared edits, nested
scopes, placement/gluing, hosted components/copies, arrays and native persistence.
The [initial normal run](R082b-normal-ctest.txt) retains nine passing checks and
one new-fixture failure: its transform omitted the member parent and correctly
triggered the shared-scope guard. The fixture now preserves its parent;
the [component rerun](R082b-components-recheck.txt) passes, including shared
definition edits beside unchanged locked geometry, subsequent placement and
undo/redo. This fixture correction changes no product behavior.

ASan/UBSan with leak detection and halt-on-error likewise retains the
[initial nine passes and fixture failure](R082b-sanitize-ctest.txt), followed by
the [passing corrected component check](R082b-sanitize-components-recheck.txt).
All ten selected regressions therefore pass in both configurations after the
fixture correction; the initial failures are not removed from the evidence.
The [source archive check](R082b-source-package.json) includes all 218 installed
inputs byte for byte and excludes build products and Git metadata.

The second refinement (`3cbe259511e7fc866c549d0df3eb65bd9e07dfde`) reuses
canonical-body validation only for unchanged immutable leaf definitions,
instance bindings and scene records from a valid `Document`. Ownership,
member-map, nested-binding and unbound-child checks still run across the whole
candidate. World-bound geometry scans are reused only when the body and every
ancestor retain their immutable identity; parent-chain and aggregate size checks
still run. Restore/import always performs full validation. Nested or changed
records follow the full comparison path.

New regressions reject a changed shared definition whose peer projection was
omitted, and reject an empty parent's transform that puts its unchanged child's
vertices outside the world bounds. Both failures preserve the document
atomically. Valid parent transforms and undo remain covered. The expanded
[normal set passes 21/21 in 51.08 seconds](R082b-validation-normal-ctest.txt);
[ASan/UBSan passes the same 21/21 in 330.11 seconds](R082b-validation-sanitize-ctest.txt)
with leak detection and halt-on-error. These results cover components, scene
hierarchy, guides/reference images, materials/tags, history, groups, native
persistence and transaction/geometry sequences. The ancestor's full 181-test
result predates this core optimization and is not represented as validation of
these new edits. The final merged source will run its own CI.

The second construction run also overlapped build/provider work; package load
changed during the run. It preserves the exact canonical document hash, but
is still not a controlled speedup measurement.

The [final merged source archive](R082c-source-package.json) verifies all 218
installed inputs after the package and guide changes.

The larger million-triangle fixture still exceeds existing document limits.
This change does not introduce real viewport instancing, raise limits, reduce
the required fixture, change performance budgets or accept R082/M9. Controlled
reference runs, remaining performance scenarios, final integration, remote CI
and ordered merges remain open.


## Broader current-source regression

After the package/guide parent integration, source
`0c921a5` passes [all 178 non-desktop registered tests](R082b-full-headless-ctest.txt),
including every configured real Blender, DXF and PDF consumer with no skips.
The desktop build remains covered by its earlier baseline and current-head CI;
this result does not relabel the 178-test headless configuration as 181 desktop
checks or accept physical hardware/performance gates.
