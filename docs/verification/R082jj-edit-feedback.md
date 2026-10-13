# R082.jj — Edit feedback at 10,000 placements (D-024)

2026-10-12. Branch `perf/r082jj-edit-feedback`, based on `origin/main` `fb3c9b7e`
(its code is identical to `db68cd14`, the source of the
[R082.ii](R082ii-amd-hardware-session.md) AMD binaries). Raw figures, hashes and
per-phase profiles are in [R082jj-edit-feedback.json](R082jj-edit-feedback.json);
test logs are in [R082jj-regressions.txt](R082jj-regressions.txt). This fixes D-024. It
is not release or M9 acceptance, and no limit, budget, fixture or check changes.

## Result

On the RX 6800 XT (X11, one CPU, public Release build, two campaigns of
`scripts/session-benchmark-campaign.sh amd capacity-10k`), the p95 from an edit to the
first GPU-complete viewport frame at 10,000 placements is now:

| Family | Before (R082.ii) | After, campaign 1 | After, campaign 2 | Edit + readback, before → after |
| --- | ---: | ---: | ---: | --- |
| deep | 209.3 ms | 40.3 ms | 37.6 ms | 229.4 → 58.9 / 56.6 ms |
| textures | 108.9 ms | 39.2 ms | 40.6 ms | 128.8 → 59.2 / 61.1 ms |
| repeated (3 runs) | 67.2–68.9 ms | 36.9–37.9 ms | 36.1–36.8 ms | 86.3–87.2 → 55.2–56.3 ms |
| far | 73.8 ms | 39.1 ms | 38.3 ms | 93.1 → 57.8 / 58.1 ms |

All twelve cases exit 0 and are below the 100 ms routine-edit target
([BUILD_PLAN](../BUILD_PLAN.md#initial-performance-budgets)). The inference update p95
falls from 48.2 ms (deep) and 11.1–12.5 ms (others) to 7.1–8.2 ms. Complete-frame p95
(17.4–20.3 ms after, 17.3–21.7 ms before) and GPU-complete frame p95 are unchanged within
run-to-run noise; they stay below the 33.3 ms target for the million-triangle class.

## Root cause

The benchmark's history loop moves one placement (`Document::transform` with its
parent, `tests/real_model_benchmark.cpp:387`) and times the edit, `Viewport::refresh`
and the first complete frame. Each of those steps did work proportional to the whole
document, and in `deep` proportional to documents × 33 hierarchy levels. Temporary
thread-CPU-time scopes in an instrumented Release build (container, one CPU, 100
edits; slower than the AMD CPU, so the absolute values are larger) located it:

| Mean CPU per edit at 10,000 placements | deep before | deep after | textures before | textures after | repeated before | repeated after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `Document::apply` (validation, copies, history) | 334.1 ms | 0.7 ms | 120.8 ms | 0.6 ms | 132.0 ms | 0.5 ms |
| `Viewport::refresh` (selection sync, worker snapshot) | 39.3 ms | 32.8 ms | 33.4 ms | 28.1 ms | 39.0 ms | 29.7 ms |
| `Viewport::rebuild` | 176.6 ms | 55.6 ms | 260.6 ms | 56.2 ms | 66.0 ms | 42.8 ms |
| … of which `syncTextures` | 0.04 ms | 0.04 ms | 182.3 ms | 10.1 ms | 0.04 ms | 0.03 ms |
| Inference worker thread (same CPU) | 141.5 ms | 44.2 ms | 50.4 ms | 34.7 ms | 50.5 ms | 39.4 ms |

GPU rendering is excluded: llvmpipe dominates software frames and is not hardware
evidence. File and line references are to the base.

1. **Core validation of a moved placement was whole-document.** R082.ff's incremental
   scheme refused any record whose parent or transform changed, or that is bound to a
   component (`src/core/model.cpp:1072–1076`), so every placement move took the full
   scheme. It copied the body and binding maps (`:1327`, `:1095`), recounted every
   record and walked every record's parent chain (`validateDocumentSize`, `:306`,
   called at `:1334`) and revalidated all 10,000 bindings plus an unbound-child scan of
   every record (`validateInstances`, `src/core/component_records.cpp:283`, `:295`).
2. **Every world-transform walk allocated.** `worldTransformIn`
   (`src/core/model.cpp:117`) kept a `std::set` of visited IDs, one allocation per
   level. In `deep` each of 20,032 records walks 33 levels, so each document-wide pass
   cost about 660,000 allocations and matrix products; validation, the viewport and the
   inference index each made such a pass per edit.
3. **The viewport rebuilt its state for every body.** `Viewport::rebuild`
   (`src/app/viewport.cpp:968–972`) recomposed each body's world transform and rebuilt
   its material, image and section inputs, although only the moved placement's records
   changed. The shared placement meshes and per-instance fallbacks were not
   invalidated: mesh builds, uploads and tessellations are identical before and after.
4. **Texture requests tested visibility per face.** `syncTextures`
   (`src/app/viewport_textures.cpp:16`) called `visible()`, which walks the hierarchy,
   for every face of every body (560,000 calls in `textures`) on each rebuild.
5. **The inference index recomposed every placement.** `InferenceIndex::sync`
   (`src/geometry/inference.cpp:314`) called `Document::worldTransform` for all 20,000
   records to find the two that moved, and the worker received a full `Document`
   copy, including undo history (`src/app/inference_worker.cpp:19`). Under the
   one-CPU control the worker competes with the frame on the same CPU.

Not dominant: texture uploads (`syncTextures` uploads only images that are new to the
viewport, and none are during the edit loop), transparency sorting (0.02 ms per
frame), the selection overlay and profile passes (under 0.1 ms per frame) and history
(each edit retains 3,273 bytes).

## Changes

- **Incremental validation of moved placements** (`src/core/model.cpp`,
  `src/core/component_records.cpp`). A placement root that no enclosing placement binds
  may change, including its parent and transform, under the incremental scheme. Its
  descendants are exactly the records its binding and nested bindings own
  (`collectPlacementDescendants`), so the changed records and those descendants get the
  parent-chain, reference-image and world-bound checks, merged in ID order so the
  first failure is the full scan's. The changed root's binding is revalidated with the
  inserted bindings, in root ID order, and the unbound-child check covers changed
  records as before. Nested placement roots, bound members and every other moved
  record still use the full scheme. The full-validation oracle is unchanged.
- **Allocation-free `worldTransformIn`.** It counts steps instead of keeping a visited
  set. Chains longer than 128 records were already rejected, and a cycle never reaches
  a missing record, so every cycle still fails with "Cyclic or excessively deep
  hierarchy" and every other chain gives the same result and error.
- **Unchanged chains keep their world transform** (`src/core/hierarchy_changes.hpp`).
  `changedChains` marks, in one ID-ordered pass, each record whose own or any
  ancestor's record differs from the one a cached world transform was composed from.
  The viewport and the inference index recompose only those chains; records are
  immutable, so a cached transform of an unchanged chain is bit-identical.
- **Unchanged bodies skip the viewport pass.** When a body's chain is unchanged and the
  document-wide mesh inputs of the last complete rebuild still hold (materials, decoded
  images, no active sections, unchanged placement membership) and its opacity,
  presentation revision, buffers and render origin are unchanged, the full pass would
  rebuild, upload and replace nothing; the fast path collects only its totals and
  transparent triangles. A rebuild that throws invalidates the snapshot.
- **Texture requests** test visibility only for a face (or reference image) that would
  add an asset not yet requested; the requested set is the same.
- **Inference worker** copies `Document::readSnapshot()` (no undo history).
- `syncSelection` and cache pruning walk the ID-ordered maps together instead of
  searching per record.

## Behavior is unchanged

- **AMD:** in all twelve cases the fixture, warm-frame and final-frame hashes, body mesh
  builds, mesh uploads, tessellations, placement meshes, shared placements, cache bytes,
  retained history and all inference counters (final body builds, visited primitives,
  intersection pairs, minimum candidates, truncated queries) equal R082.ii's. Peak RSS
  differs by −0.7 to +3.5 MB (at most +0.5%).
- **Software:** in all twelve runs below, fixture and frame hashes, mesh builds and
  uploads and the inference counters match between the base and fixed builds.
- **Incremental-validation oracle:** `incremental_validation` runs every edit under the
  full-validation oracle. The three randomized seeds now validate 1,024 edits
  incrementally (987 in R082.gg), and reach the unique limit 12, 14 and 16 times, as in
  R082.gg. A new section moves, hides
  and re-parents placement roots (into a group, back to the model, out of bounds, into
  another placement's member, under its own nested placement) and moves a nested root;
  each must agree with the full scheme on the decision, the message and the state.
- **Inference equivalence, picking and the placement viewport A/B fixture** pass
  unchanged (below), as do the benchmark's own assertions, including "Inference
  transform rebuilds only the changed placement" and the fixed-capacity cache checks.

## Software measurements

Container `sketchyup-native-fonts-check:20261008` (one CPU, 4 GiB, no swap,
`xvfb-run`, xcb, llvmpipe) through `scripts/run-container-check.sh`, as R082.gg. Base is
a Release build of `origin/main` (`499bacc1…`), after is this branch's P2 Release build
(`76929d0d…`). Each run renders the 100 timed edits; undo and redo are not rendered (the
rendered-history argument is omitted to keep the twelve runs within the shared container
slot). All twelve runs exit 0. "Hashes" compares fixture, warm-frame and final-frame
hashes, body mesh builds, mesh uploads and the inference counters between the two builds.

| Placements, family | Edit → GPU-complete p95, before → after | Edit + readback p95 | Inference update p95 | Complete frame p95 | Hashes |
| --- | --- | --- | --- | --- | --- |
| 1,000 deep | 264.5 → 218.3 ms | 525.3 → 479.2 ms | 16.3 → 3.9 ms | 209.0 → 205.0 ms | equal |
| 1,000 textures | 411.2 → 360.1 ms | 834.7 → 762.7 ms | 5.1 → 3.5 ms | 377.6 → 372.5 ms | equal |
| 1,000 repeated | 227.5 → 214.0 ms | 487.3 → 470.7 ms | 4.8 → 3.5 ms | 204.1 → 200.5 ms | equal |
| 10,000 deep | 2,057.1 → 1,561.7 ms | 3,288.5 → 2,974.8 ms | 221.2 → 67.6 ms | 1,210.7 → 1,416.6 ms | equal |
| 10,000 textures | 1,996.6 → 1,641.5 ms | 3,525.8 → 3,191.0 ms | 73.3 → 52.7 ms | 1,487.4 → 1,493.0 ms | equal |
| 10,000 repeated | 1,506.0 → 1,321.8 ms | 2,742.5 → 2,556.8 ms | 76.4 → 59.1 ms | 1,181.7 → 1,182.7 ms | equal |

These are software figures, not hardware evidence. In llvmpipe on one CPU the
million-triangle frame itself takes 1.2–1.5 s, so the edit metrics fall by the CPU work
removed (about 0.2–0.5 s at 10,000 placements) rather than by the AMD ratio. The
inference update falls from 221.2 to 67.6 ms for deep at 10,000 placements. Complete-frame
times are single-run llvmpipe figures and vary by up to about 0.2 s between runs; the
changes do not touch frame rendering.

## Verification

- `ctest --preset dev -j3`: 190 of 190 pass; the 11 optional `*_real` consumer tests
  skip as usual. This includes `incremental_validation` (138 s), `inference`,
  `inference_equivalence`, `inference_instanced_io`, `instanced_storage`, components,
  component records, selection, transform selection, groups, history and
  `document_limits`.
- `ctest --preset headless -j3`: 62 of 62 pass.
- `ctest --preset sanitize -j3` (ASan/UBSan): 62 of 62 pass with no sanitizer report,
  including `incremental_validation` (sanitized mode, 76 s), `inference_equivalence`
  and `document_limits`.
- Native, in the container (`build/dev`, llvmpipe), 22 of 22 pass: on X11 at 1×
  `placement_viewport_tests`, `viewport_tests`, `viewport_precision_tests`,
  `inference_input_tests`, `selection_input_tests`, `component_input_tests`,
  `group_input_tests`, `transform_input_tests`, `texture_viewport_tests`,
  `material_viewport_tests`, `section_viewport_tests`, `style_viewport_tests`,
  `smooth_viewport_tests`, `guide_input_tests`, `constraint_input_tests` and
  `drawing_input_tests`; on Wayland at 1× (`scripts/test-wayland.sh`)
  `placement_viewport_tests`, `viewport_tests`, `inference_input_tests`,
  `selection_input_tests`, `component_input_tests` and `texture_viewport_tests`.
  The placement A/B fixture asserts identical picking between shared and per-body
  meshes; it also reports per-state pixel differences, which it does not assert.
- AMD binaries: built with the P2 host command (eight CPUs, 4 GiB, `-j2`), copied to a
  new directory `~/sketchyup-verification/r082jj-20261012/bin` on the AMD machine and
  verified with `sha256sum -c` (`real_model_benchmark`
  `76929d0d9d8e19d9cded9d5813ae235d5232325eeaaceaec0cdcf2d4fdd7c157`).

## Remaining work and limits

Each edit still makes a few linear passes with small constants: `changedChains` in the
viewport and in the inference index, the `syncSelection` walk, the worker's read
snapshot and index copy, and the inference index's top-level tree rebuild. On the AMD
CPU they total well under the target; removing them would need change lists from the
Core or persistent maps. The incremental scheme still uses full validation for moves of
loose groups, nested placement roots and bound members, which have no cheap descendant
list; those edits are as fast as before. Intel was not measured (waived,
[ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)).
