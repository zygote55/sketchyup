# R082.b — Reuse unchanged leaf component projections

2026-10-07. Component placement previously reconstructed the materialized bodies
of every existing instance, including unchanged leaf definitions. The projector
now reuses immutable bodies only when the leaf definition, binding, placement
and every seeded member retain their validated identity. Nested definitions,
edited definitions and changed placement/member records follow full projection.
Document validation and all complexity limits remain unchanged.

The opt-in `component_projection_benchmark` constructs the same canonical
1,000-instance, 100,000-triangle prism fixture as R082.a through public document
operations. It measures construction before serialization or viewport work and
records process peak RSS at that point. It is not a timing-based CTest gate.

| Debug construction run | Elapsed | Peak process RSS |
| --- | ---: | ---: |
| [Before](R082b-construction-before.json), `df3f658` | 240,110.499 ms | 55,824,384 B |
| [After](R082b-construction-after.json), `f606124` | 199,975.817 ms | 38,338,560 B |

Both produce 2,000 bodies, 100,000 triangles and a 7,021,923-byte native document
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

The larger million-triangle fixture still exceeds existing document limits.
This change does not introduce real viewport instancing, raise limits, reduce
the required fixture, change performance budgets or accept R082/M9. Controlled
reference runs, remaining performance scenarios, final integration, remote CI
and ordered merges remain open.
