# R082.ff — Validate component placement incrementally

2026-10-10. Base `809bdbcfdcee0c9b8ba0481beb06c01b91a2313e`. No production limit,
error message, ID allocation, canonical document or history behavior changes.
This is construction-cost evidence only; it does not accept R082 or M9.

## What was quadratic

R082.u expanded only the new placement, but every `Document::apply` still revalidated
and recopied the whole document. A placement therefore cost O(document), and building
*n* placements cost O(*n*²). On base `809bdbcf` the per-apply whole-document work was:

| Location (base) | Work per apply |
| --- | --- |
| `src/core/components.cpp:349` | `placeComponent` preflight summed every body for the limit check |
| `src/core/model.cpp:1030`, `:821`, `:889` | full copies of the body, binding and allocator-floor maps |
| `src/core/model.cpp:118`, called at `:1037` | recount of every body plus a parent-chain walk (one `std::set` allocation per body) |
| `src/core/model.cpp:1091`, `:1103`, `:1104` | tag, material and reference-image scans of every body |
| `src/core/component_records.cpp:196` (via `model.cpp:1107`) | every binding re-checked; unbound-child scan of every body |
| `src/core/model.cpp:1140` | scan of every body for locks |
| `src/core/model.cpp:1156` | reachability set over all bodies and every retained undo entry |

## Incremental scheme

`Document` now maintains these values:

- `DocumentTotals` (records, vertices, faces, wires, edges, curves, guides), exposed
  as `materializedTotals()`. `placeComponent` adds the definition size to these totals
  instead of re-summing. The comparison and the message are unchanged.
- the set of locked body IDs;
- the set of allocator-floor keys with no live body ("detached floors");
- per-ID counts of the retained undo entries that name each body.

`apply` first inspects the edit and then picks one of two schemes. The incremental
scheme is used when the edit meets all of these conditions:

- It changes no definition, tag, material, asset, scene or section record.
- No annotations, active sections or hosted records exist.
- Each modified body keeps its parent, transform and reference-image ownership, is
  unbound and is not erased.
- Each binding change inserts a binding whose root and targets are all inserted bodies.

Placement, loose drawing and in-place geometry edits on unbound bodies qualify.
Every other edit uses the original full scheme, unchanged.

Under the incremental scheme:

- Every precondition, record, floor and topology check runs exactly as before.
- Only changed bodies get the parent-chain, reference-image, world-bound and assignment
  checks. They are checked in ID order, so the first error is the one the full scan
  would report.
- Limits are checked against the running totals.
- Only the inserted bindings are validated, using the same per-binding routine.
  `componentBound` walks the bounded ancestor chain to decide whether a changed body's
  parent is bound.
- The lock check visits only locked bodies.
- Floor pruning considers only detached floors, and the history counts answer whether
  an undo entry still names each one.
- The commit splices the staged nodes into the live maps, so the document is never
  copied. Every check in this list is unchanged from the full scheme.

These checks are equivalent because the current document already satisfies every
invariant. Unchanged records therefore keep their parent chains, world transforms and
ownership. Inserted records have fresh IDs, so no existing record can refer to them.

## Keeping the totals correct

Each operation updates the totals as follows:

- **apply** computes `after − before` for the frozen changed records and commits the
  result only after every check passes. A rejected edit, including a failed or
  partially failed batch, commits nothing.
- **undo and redo** apply the same delta in reverse or forward inside `update`.
- **Locks, detached floors and history counts** are staged before the commit and then
  spliced or erased, so no step after the commit point can fail.
- **History** counts change on push, undo, redo, amendment pruning and entry-limit
  pruning.
- **Amendment and navigation** run on a private document copy, which carries its own
  totals.
- **`readSnapshot`** copies the totals; a snapshot keeps no history counts.
- **`restore` and native load** recount everything and are still fully validated.

## Equivalence oracle

`Document::setFullValidationOracle(true)` turns the oracle on. Setting
`SKETCHYUP_FULL_VALIDATION_ORACLE=1` does the same; it is off by default. With the
oracle on, every apply also runs the full scheme on a private copy, including the
original whole-map reachability pruning. The oracle then requires an identical
accept/reject decision, error text, change report and resulting state: bodies,
definitions, bindings, all allocator floors and next IDs, history sizes and bytes,
`readSnapshotBytes` and every incremental index. Apply, undo and redo also recount
all bookkeeping. Any disagreement throws `ValidationOracleMismatch`.

The new `incremental_validation` test runs with the oracle on throughout:

- **Boundary:** 49 placements of a 2,000-vertex definition reach exactly 100,000
  vertices. The 50th is rejected with `Component placement exceeds document editing
  limits`, and a loose face is rejected with `Document complexity exceeds editing
  limits`. Undo frees one placement, and placing it again fills the limit. Six
  triangle placements reach exactly 10,000 bodies, and one more is rejected with the
  same message. Rejected edits change nothing.
- **Randomized:** three fixed seeds (`0x82ff`, `0x1234`, `0xbeef`) × 220 steps.
  Steps include placements (nested, inside groups and into bound groups), make-unique,
  replace, erase, move, paint, lock/unlock, undo/redo, history navigation, amendment
  and batches, some with injected failures. A preloaded loose geometry block keeps the
  document near the limit. Each run covered 7–34 limit rejections and 9–19 failed
  batches. 856 edits used the incremental scheme, and every step agreed with a full
  recount.

## Verification

- **Dev (Debug):** `cmake --preset dev && cmake --build --preset dev --parallel 3`.
  - The 23 selected core, component, history, transaction and automation suites pass.
  - The same 22 existing suites [pass again](R082ff-oracle-regressions.txt) with
    `SKETCHYUP_FULL_VALIDATION_ORACLE=1`, which cross-checks all of their edits.
  - The [full dev ctest](R082ff-full-dev-ctest.txt) passes 183/183, with 11
    real-tool cases skipped as usual.
- **Sanitizer:** `cmake --preset sanitize && cmake --build --preset sanitize --parallel 3`.
  All 60 headless core suites [pass under ASan/UBSan](R082ff-sanitize-ctest.txt). They
  include `incremental_validation` (533.5 s), components, history, groups, copy-array,
  hosted and core.

## Timings

All timings are from Release builds with `-g -fno-omit-frame-pointer` on a shared
host, load average 10–12. They are recorded for evidence and are not a ctest gate.

The unchanged `component_projection_benchmark` produces identical canonical native
hashes before and after at 250, 500 and 1,000 instances. At 1,000 instances
(`c874449f…`) construction falls from 9,876.6 ms to 703.0 ms;
[raw data](R082ff-projection-benchmark.json).

The new opt-in `component_placement_scaling` harness uses only API that already
existed. At production limits, 1,000 instances take 4,437.8 ms before and 1,067.9 ms
after, with identical document digest `cd2e24f5916f5640`
([before](R082ff-before-default-1000.json), [after](R082ff-after-default-1000.json)).

The 26-sided prism reaches the production vertex limit at 1,923 instances, so growth
beyond that point uses `component_placement_scaling_raised_limits`. That target links
a private core copy compiled with `SKETCHYUP_BENCHMARK_LIMIT_SCALE=8`. The macro is
defined only for this benchmark-only target (`EXCLUDE_FROM_ALL`). Every product and
test target keeps scale 1, so no public default changes. Both runs end with the same
10,000-instance digest `924513c52e3380c3`
([before](R082ff-before-raised-10000.json), [after](R082ff-after-raised-10000.json)).

| Instances | Before cumulative ms | After cumulative ms | Before ×/doubling | After ×/doubling |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 4,195.4 | 755.7 | — | — |
| 2,000 | 21,924.9 | 1,499.6 | 5.2 | 1.98 |
| 4,000 | 107,505.0 | 3,043.8 | 4.9 | 2.03 |
| 8,000 | 469,814.3 | 6,351.2 | 4.4 | 2.09 |
| 10,000 | 770,787.8 | 7,949.9 | — | — |

Each additional 1,000 placements now costs 744–846 ms, which is near-linear. The
remaining constant per-placement cost comes mostly from revalidating the canonical
definitions. That cost scales with the number and size of definitions, not with
placements, and is left to later layers. Splitting the limits into unique geometry
and placements remains R082.gg.
