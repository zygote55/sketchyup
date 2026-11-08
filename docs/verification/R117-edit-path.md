# R117: edit path, push/pull on large bodies, history charge, definition count

R117 found three defects on the GPU PC in [R114](R114-hardware-limits.md) and measured them again
here: every edit paid for every component definition (D-107), the undo budget charged a copy of
each changed record that the history does not keep (part of D-104), and push/pull refused any body
above 1,000 faces and compared every pair of faces (D-106). It fixes all three, and it raises the
component definition count from 1,024 to 2,048, the largest tier measured within the 100 ms edit
budget. Creating definitions is still superlinear, so that target is not met; the profile is below.

## Summary

| Defect | Result |
| --- | --- |
| D-107: edits pay for every definition | Fixed. A placement move at 1,024 definitions takes 5 ms p95 on the GPU PC (was 268 ms). A definition edit takes 14 ms p95 (was 1,057 ms). The laptop's document-level times are in the tables below. |
| D-107: creation is superlinear | Improved about 25 times at 1,024 definitions (160 s to 6 s on the laptop), but still superlinear: each doubling from 1,024 costs 4 to 6 times more. Not met; see the profile. |
| D-104: the history charge | Fixed. A changed record is charged once, as the copy the change creates. A definition edit at 5,000 placements is charged 88 MB (was 176 MB), against 85 MB of real memory. |
| D-106: push/pull on large bodies | Fixed. The 1,000-face and 10,000-vertex context limit is gone; the one-body limit (100,000 faces) is the only limit. A 99,000-face body pushes in 1.3 s. The coplanar check is near-linear. |
| Definition count | 1,024 to 2,048, the largest measured tier inside the edit budget. 4,096 is measured (creation 223 s, definition edit 148 ms p95) and stops the next tier. |

## Conditions

| Item | Laptop (profiles and most timings) | GPU PC (frames) |
| --- | --- | --- |
| Machine | Linux, GCC 16.2.1, 22 cores, 30 GB RAM, shared with the other worker, which built at the same time for some runs | CachyOS guest, RX 6800 XT, Mesa, Qt 6.12, X11, as in R114 |
| Build | `RelWithDebInfo` for the profiles, `RelWithDebInfo` for the timed driver (`edit_path_benchmark`, headless) | `Release`, the R114 harness. Scale 8 (`SKETCHYUP_BENCHMARK_LIMIT_SCALE=8`) for the 176,000-face cases, as R114 measured them. Product scale for the count tier |
| Driver | `tests/edit_path_benchmark.cpp` (new, not run in CI). Timings cover the document API only, from the mutation to the return; the product build also times the native save and open | `limits_hardware_benchmark` (R114), unchanged except for the code under test |
| Samples | 20 per edit kind, each undone and redone; creation is one run per tier | 20 per edit kind, 100 frames |
| Profiler | `perf` is not installed. A sampling profile used an `LD_PRELOAD` timer sampler kept outside the repository, and inline-aware `addr2line`. gdb cannot attach here (`ptrace_scope` is 1) | Not used |
| Runs | One per tier unless stated. Other workers used the machine during some runs, so the laptop creation times vary by about 25 percent between runs; the ranges are given | Three runs at 1,024 definitions; one run for the other cases. The GPU was busy with another user before some cases, so the runner waited (see [Not run](#not-run-and-why)) |

## Profiles (before)

Profiles were taken at 256 and 512 definitions, with product limits, before any fix.

- **Placement move.** 99% of the samples were inside `validateComponentDefinitions`. That
  function restores every definition as a canonical document (`Document::restore`), which validates
  every body and tessellates every face to check its topology. It ran on every commit, including a
  move that changed no definition. At 256 definitions a move cost 16 ms of CPU.
- **Definition edit.** 92% of the samples were inside the same validation, through the component
  draft: `editComponentDefinition` restores the whole document's definitions to build its draft.
  At 256 definitions an edit cost about 120 ms of CPU.
- **Creation.** 95% of the samples were inside the validation, called from `placeComponent` and
  from the publish step of `createComponent`. Every creation validated every definition, so the
  total grew with the square of the count (1,024 definitions: 159.7 s, with each doubling 3.6 to 4.1
  times the last).
- **Push/pull.** Refused above 1,000 faces. Its coplanar check compared every pair of faces, so a
  body above about 1,414 faces would exceed its 1,000,000-pair budget even without the context limit.

## Fixes and their measured effect

### 1. Edits that change no definition skip definition validation (D-107)

A commit that changes no definition, tag, material or asset skips the definition check
(`applyEdit`). Its definitions were validated when they were stored, and nothing they depend on has
changed. An edit that changes a definition checks that definition and every definition that places
it (`validateComponentDefinitionChanges`). A tag, material or asset change, and the full-validation
oracle, still check every definition.

The check keeps its cheap parts for every definition (identity, sizes, nested references, so a
reference to an erased definition is refused). It adds the full geometry check only for changed
definitions. The expansion graph is checked for the changed definitions and their placers. A new
randomized test compares the scoped check with the full one on 400 seeded candidates: they agree,
and both outcomes occur.

Measured on the laptop (product limits, placement move, p95): 1,024 definitions 82 ms before, 2.2 ms
after; 2,048 definitions 4.8 ms after.

### 2. Placement and definition publishing validate only what they change (D-107)

`placeComponent` needs only the prototype's expansion sizes, and `publish` checks only the
definitions the candidate changes or erases. The parameter query's sizes are computed for the one
definition it reports. Each computes sizes by the same graph checks as before, without
re-validating stored geometry.

### 3. The definition draft shares the stored definitions (D-107)

`editComponentDefinition` builds its draft from the document's definitions. Restoring them re-validated
and re-copied every definition, so the draft cost the document's whole geometry. The draft now uses
a restore path that shares them (`DocumentDraftAccess`, a friend of `Document`, used only by the
draft). The draft's floors are computed as before. The native loader and every other restore still
validate every definition.

Measured effect at 1,024 definitions, on the laptop, with fixes 1 to 3 together: a definition edit fell
from 353 ms p95 to 11 ms p95.

### 4. Creation is still superlinear (D-107, not met)

| Definitions | Creation, before (s) | Creation, after (s) | Ratio to the previous tier, after |
| --- | ---: | ---: | ---: |
| 128 | 2.4 | 0.2 to 0.3 | |
| 256 | 9.3 | 0.6 to 0.8 | 2.5 |
| 512 | 39.2 | 1.7 to 2.2 | 2.8 to 3.0 |
| 1,024 | 159.7 | 6.2 to 8.0 | 3.6 to 3.7 |
| 2,048 | not run before | 38 to 53 | 3.8 to 4.1 |
| 4,096 | not run before | 223 | 6.0 |

Creation at 1,024 is 20 to 25 times faster than before, and 4,096 takes 3.7 minutes. The remaining cost is one pass
over every definition and placement per creation: the change check and its placer map, the record
checks, the unique-geometry total and the variant index, and the copies of the definition map. Each
is linear, and together they make the total grow faster than linearly. Removing them needs an
incremental path for definition changes (today a definition change takes the full commit scheme),
which is a larger design change than this brief asks for. It is recorded as the next step.

### 5. The history charge counts each changed record once (D-104)

The undo budget charged a changed record its before copy and its after copy. The history keeps
one of them: the after copy when the change is applied, or the before copy when it is undone. The
other is the live record, or a copy an earlier edit already charged. A chain of edits therefore
charged each record version twice. The charge is now the copy the change creates (its after), or
for an erasure the copy it removes (its before). Paint, terrain and their special cases are
unchanged, and so are the other record kinds (see [Contradictions](#contradictions-with-the-design-records)).

Measured against real heap growth (the process's heap in use, before and after one edit, with
the undo entry kept):

| Case | Charge before | Charge after | Heap growth | Ratio, charge to heap, before and after |
| --- | ---: | ---: | ---: | --- |
| Definition edit, 5,000 placements of a 28-face prism (140,000 faces) | 175.7 MB | 88.3 MB | 84.7 MB (80.8 MiB) | 2.07, then 1.04 |
| Body move, one sphere of 99,000 faces | 63.4 MB | 31.7 MB | 30.1 MB (28.8 MiB) | 2.11, then 1.05 |
| Definition of 99,000 faces, 3 placements (297,000 faces) | not measured (see the note) | 126.7 MB | 120.4 MB (114.8 MiB) | 1.05 |
| Definition of 99,000 faces, 5 placements (495,000 faces) | refused (the note) | 190.1 MB, admitted | 180.7 MB (172.3 MiB) | 1.05 |

Note on the "before" cells of the last two rows: the change is the sum of both copies, so the
charge before the change is twice the after figure (253 MB and 380 MB). Both are over the 192 MiB
budget, so those edits were refused. The before values are derived from the measured after values,
not measured. A definition of 176,000 faces, the R114 case, cannot exist at product limits (one body
is capped at 100,000 faces), so the 99,000-face definition stands in for it. Scale 8 keeps the R114
case: see the GPU table.

Undo leaves the heap about 28.7 MiB higher in the two definition cases above, at both 3 and 5
placements. That amount does not grow with the placements. It is not charged to the history, and I
did not profile it.

### 6. Push/pull on large bodies (D-106)

- **The context limit is gone.** `pushPull` checks only the one-body limit (`Surface::bodyLimit`,
  100,000 faces or vertices, enforced by `Surface::validate`). The loop limit (10,000 vertices a loop)
  stays.
- **The coplanar check is a grid over bounding boxes.** Two faces can share a region of positive area
  only when their bounding boxes, grown by four times the tolerance, overlap. Faces are binned in a
  grid of cells about the three-quarter extent of the faces, and each overlapping pair is tested once,
  in the cell that holds the lower corner of its overlap. Large faces are tested against every face.
  The pair test is the old one: the first face's plane must hold the second within the tolerance, and
  the projected regions must overlap in an area of at least one unit. A projected region lies in the
  intersection of the two boxes, so a pair whose boxes overlap in less than one unit is skipped
  without changing the answer.
- **The cut tests only the vertices on its plane.** A cut segment lies on the destination plane, so only
  vertices within twice the tolerance of that plane can match. The match test itself is unchanged.
- **The candidate tests are bounded** by a budget of 64 tests per face plus one million (refused
  by name when exceeded), so a pathological body still fails closed.

Measured, sphere bodies, push of one face by 1 cm, undo (laptop, product limits, 20 samples):

| Faces | Push p50 | Push p95 | Undo p95 | Before |
| ---: | ---: | ---: | ---: | --- |
| 1,000 | 9.6 ms | 10.3 ms | 0.07 ms | refused (context limit) |
| 10,000 | 109.5 ms | 110.9 ms | 0.77 ms | refused |
| 99,000 | 1,320 ms | 1,338 ms | 6.5 ms | refused |
| 100,000 | refused: the push adds 200 faces, and the result (100,200) exceeds the one-body limit | | | refused |

A push of one 100,000-face body is refused by the body limit because the push adds faces; the
largest body that can be pushed on a sphere is therefore just under the limit. The 99,000-face
sphere was measured instead. The time per face is 10 to 14 microseconds across the sizes. A profile
at 10,000 faces attributes 45% of the samples to `pushPull` itself, of which 19% is the coplanar check
and 17% is the validation of the result, and 53% to the document's `applyEdit` (its copies and topology
comparison). Both grow linearly with the faces.

The randomized test compares the new push/pull with the pre-R117 implementation (kept in the test
file, unchanged apart from its namespace) on 80 random bodies and 480 pushes: results and refusal
messages must match. Both outcomes occur. A second test pushes the cap of a 1,000-sided prism,
which the old code refused.

### 7. Definition count 1,024 to 2,048

`componentDefinitionCountLimit` is 2,048 (`src/core/component_records.hpp`). The native loader
refused a document with more than 1,024 definitions by a literal in `document_io.cpp`; it now follows
the document limit. Without that fix a saved 2,048-definition document could not be opened. The
`.skp` import keeps 2,048 definitions and counts the rest. A test with injected counts accepts 2,048
definitions and refuses one more, without building them.

## Definition-count tiers

Creation, placements and definition edits are measured on the laptop, product limits, except the
4,096 tier, which is built at scale 16 (`edit_path_benchmark_scaled`) because the product limit
refuses it. Frames are on the GPU PC (below).

| Tier | Creation | Placement move p95 | Definition edit p95 | Undo p95 | Save, open (file) | Verdict |
| ---: | --- | ---: | ---: | ---: | --- | --- |
| 1,024 | 6.2 to 8.0 s | 0.8 to 2.2 ms (laptop), 5.1 to 6.4 ms (PC feedback) | 11 to 17 ms (laptop), 13.8 to 14.6 ms (PC feedback) | 0.4 to 0.6 ms | 0.38 s, 0.57 s (7.8 MB) | Within the budgets |
| 2,048 | 38 to 53 s | 3.4 to 4.8 ms | 38 to 53 ms | 1.7 to 2.2 ms | 1.68 s, 0.96 s (15.7 MB) | Within the budgets. Chosen |
| 4,096 | 223 s | 12.9 ms | 148 ms | 7.6 to 8.8 ms | not measured | Over the edit budget: stops the next tier |
| 16,384 | not built | | | | | Not run (see below) |

Frames are for the GPU PC, scale 8, at 1,024 definitions: frame p95 3.1 to 3.3 ms; placement move feedback p95
5.1 to 6.4 ms; definition-edit feedback p95 13.8 to 14.6 ms; undo p95 4.0 to 5.1 ms. GPU results for 2,048 and 4,096 are in the
GPU table below when the campaign has run them.

**What stops the next tier.** A definition edit at 4,096 definitions takes 148 ms p95, over the
100 ms budget, and the time grows faster than linearly with the count. The full commit path
still walks the whole document for an edit that changes a definition (the unique-geometry total,
the variant index, the copies of the body and instance maps). Creating 4,096 definitions takes
3.7 minutes, and 16,384 would take hours at the measured rate, so 16,384 was not built.

**The material limit (1,024)** is not raised. The brief allows that only when it costs no more than
a small fixture, and this record does not measure it.

## GPU PC

Cases use the R114 harness and limits. Each case ran through `flock` in its own unit, with
`MemoryMax=18G`, `MemorySwapMax=0`, a 30-minute limit and no core dumps. The runner waited while the
GPU was busy with another user, as the brief asks; the results below are for cases that ran.

| Case | Build | Frame p95 | Move feedback p95 | Definition-edit feedback p95 | Undo p95 | R114 (PC), move / edit |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 1,024 definitions, one placement each (3 runs) | scale 8 | 3.1 to 3.3 ms | 5.1 to 6.4 ms | 13.8 to 14.6 ms | 4.0 to 5.1 ms | 268 ms / 1,057 ms |

Move feedback is the time from the document change to the first complete viewport frame, including
the framebuffer readback, as in R114. The tables above give the document-only times on the laptop.

The 176,000-face definition (R114's dense-definition family, 3 and 27 placements) and the product-scale
2,048 and 1,024 cases are pending; see [Not run](#not-run-and-why).

## What failed and how

1. **A 100,000-face push/pull was refused** by the one-body limit, because the push adds 200 faces.
   The record measures 99,000 faces instead and states the limit.
2. **gdb could not attach** to a benchmark process (`ptrace_scope` is 1). The profiles use an
   `LD_PRELOAD` timer sampler kept outside the repository.
3. **The first draft fix was not enough.** Definition edits still spent 83% of their time in
   validation, through the draft's restore. The draft now shares the stored definitions.
4. **A literal 1,024 in the native loader** refused a saved document with 2,048 definitions. The
   save and open run found it. The loader now follows the document limit.
5. **Two tests assumed the old charge.** `scene_tests` expected 400 paints to exceed the budget,
   which they no longer do (each paint is charged once, about 0.28 MB); the test now uses 800 paints,
   with the same assertion. `unique_limits_acceptance_tests` read the newest history entry from
   `history()`, which returns the oldest 200 entries by default; with more entries kept, it now asks for
   the newest by its offset.
6. **The push/pull volume check** compared 1,000-sided prisms with a 1e-6 tolerance. Clipper's
   1e-7 integer grid gives about 1e-5 of rounding there, so the check uses 1e-4. It still checks the
   swept volume.
7. **The driver's arguments** for `placements` were read from the wrong position for one run; fixed,
   and the run repeated.
8. **Two laptop builds started while another worker's build was running.** One compiled the timing
   driver and the other the I/O library. Both were single-target builds in this worktree; no other
   worker's tree was touched. The rule is now kept by a wrapper that waits for an idle machine.

## Not run, and why

- **16,384 definitions.** Not built. Creation at 4,096 takes 223 s and grows 4 to 6 times per
  doubling, so a 16,384 fixture takes hours. The 4,096 tier already misses the definition-edit budget,
  so 16,384 cannot be the limit.
- **Save and open at 4,096.** The product limit refuses 4,096 definitions, and the scaled build
  would need an I/O library built at the same scale.
- **GPU frames for the 176,000-face definition (3 and 27 placements), for 2,048 definitions at
  product scale, and for 4,096 definitions.** Queued on the GPU PC. The runner waited for the GPU,
  which another user held at 99% busy, so these cases were not measured when this record was written.
- **Push/pull at 100,000 faces.** Refused by the body limit (above).
- **The materials limit (1,024).** Not raised and not measured.
- **The full CTest suite and the CI full tier.** Not run here. The 33 core and I/O test executables that
  the change can affect were built and run (the list is in `R117-edit-path.json`), and CI runs the fast
  tier on the pull request.

## Contradictions with the design records

- **ADR 0005, "History budget" and its R114 notes.** The charge counted both copies of a changed
  record, so a definition edit at 5,000 placements was 175.7 MB, and the ADR's "every definition's
  edit fits the budget" reasoning was about the definition's faces only. R117 charges one copy. The
  R114 notes are superseded where they state the charge.
- **ADR 0005, "Implementation notes (R114: measured limits)", on push/pull.** It records the
  1,000-face context limit and the 1,000,000-pair budget. Both are gone; the one-body limit is the only
  limit.
- **The undo charge for other records.** Scenes, sections, tags, materials, assets, annotations,
  drawing records and camera paths still charge both copies. The same overstatement applies to them;
  R117 did not change them, because the brief's defect was about bodies and component records.
- **UX.** The refusal message "Push/pull exceeds the 1000-face/10000-vertex context limit" no longer
  exists. A push that would exceed the one-body limit is refused by the body limit's message.

## Risks

- **Admissions change.** Edits the old budget refused are now admitted, for example a 99,000-face
  definition placed five times, and body moves of about twice the old face count. The measured
  memory is within 5% of the charge for these cases, but the budget now protects less of the
  history's other record kinds than the design assumed (see the note above).
- **The scoped validation relies on an invariant:** a stored definition was validated when it was
  stored, and it changes only through an edit that names it. The randomized test, the
  fail-closed tests and the full-validation oracle in the incremental-validation and parameter tests
  cover it. A future edit path that changes a definition without naming it would bypass the check.
- **Creation is still superlinear**, so a 2,048-definition creation takes about 40 to 50 seconds on
  the laptop and longer on slower machines.
- **Undo heap growth** (about 29 MiB in the 99,000-face cases) is unexplained.

## Evidence

- [R117-edit-path.json](R117-edit-path.json): the summaries of the tables above, with the
  per-tier values, and the tests and their results.
- The raw timing outputs and the sampler profiles stay on the development laptop; the GPU samples
  stay on the GPU PC, as in R114.
- Driver: `tests/edit_path_benchmark.cpp` (headless, product and scale-16 builds).
- Tests: `tests/component_record_tests.cpp` (the scoped check, the count limit), `tests/push_pull_tests.cpp`
  (the reference comparison, the large body), `tests/scene_tests.cpp` (eviction, paints),
  `tests/unique_limits_acceptance_tests.cpp` (the at-limit commit's newest entry), `tests/skp_mapping_tests.cpp`
  (the import's count).
