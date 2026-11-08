# R117: edit path, push/pull on large bodies, history charge, definition count

R117 found three defects on the GPU PC in [R114](R114-hardware-limits.md) and measured them again
here: every edit paid for every component definition (D-107), the undo budget charged each changed
record twice where the history keeps one copy (part of D-104), and push/pull refused any body above
1,000 faces and compared every pair of faces (D-106). It fixes all three. It also raises the component
definition count from 1,024 to 4,096, the largest tier measured within the 100 ms edit budget on the
GPU PC. Creating definitions is still superlinear, so that target is not met; the profile is below.

## Summary

| Defect or target | Result |
| --- | --- |
| D-107: edits pay for every definition | Fixed. At 1,024 definitions on the GPU PC a placement move takes 5.5 ms p95 (was 268 ms) and a definition edit 13.5 ms p95 (was 1,057 ms). At 4,096 the PC takes 22 ms and 59 ms p95. |
| Move a placement, one 176,000-face definition | Met on the GPU PC (scale 8): 14, 27 and 49 ms p95 over three runs at 3 placements; 15 ms p95 at 27 placements (was 258 ms). |
| Definition edit of the 176,000-face definition | Refused by the 192 MiB undo budget at 3 and 27 placements, as in R114. The charge fix does not admit it. See [what failed](#what-failed-and-how). |
| D-107: creation is superlinear | Not met. About 20 to 25 times faster at 1,024 definitions than before, but each doubling still costs 2.5 to 6 times the last. 4,096 take 118 s on the PC. |
| D-104: the history charge | The charge counts each changed record once. A definition edit at 5,000 placements is charged 88 MB (was 176 MB), against 85 MB of heap growth. The budget itself is unchanged. |
| D-106: push/pull on large bodies | Fixed. The 1,000-face and 10,000-vertex context limit is gone; the one-body limit (100,000 faces) is the only limit. A 99,000-face sphere pushes in 1.3 s on the laptop. |
| Definition count | 1,024 to 4,096. The PC measures a 4,096-definition product build at 22 ms move and 59 ms definition-edit p95, and 118 s to create. The next tier (8,192) was not built; see [the tiers](#definition-count-tiers). |

## Conditions

| Item | Laptop (profiles, document-level timings) | GPU PC (feedback including the frame) |
| --- | --- | --- |
| Machine | Linux, GCC 16.2.1, 22 cores, 30 GB RAM, shared with the other worker and with other processes | CachyOS guest, 12 vCPUs, RX 6800 XT, Mesa, Qt 6.12, X11, as in R114 |
| Build | RelWithDebInfo, headless core (the timed driver `edit_path_benchmark`). Product limits, except the 4,096 tier, which is built at scale 16 (`edit_path_benchmark_scaled`) because the product count limit was 2,048 when it was built | Release, the R114 harness (`limits_hardware_benchmark`). Two builds: scale 8 (R114 conditions, for the 176,000-face cases and a 4,096 cross-check; made when the count was 2,048, so 16,384 at scale 8, which these cases do not reach) and product scale (the at-limit confirmation, count 4,096) |
| Driver | `tests/edit_path_benchmark.cpp` (new, EXCLUDE_FROM_ALL). Timings cover the document API only, from the mutation to the return. The product build also times the native save and open | `limits_hardware_benchmark` (R114), unchanged |
| Samples | 20 per edit kind, each undone and redone; creation is one run per tier | 20 per edit kind, 100 frames; three runs at 1,024 and at 176,000 faces (3 placements), one run elsewhere |
| Profiler | `perf` is not installed. The profiles use an `LD_PRELOAD` timer sampler kept outside the repository and `addr2line` with inline chains. gdb cannot attach here (`ptrace_scope` is 1) | Not used |
| Load | Other workers and other processes shared the laptop. The laptop timings at 4,096 were taken on a shared machine and are higher than the PC's; the PC numbers are the budget evidence | Another user held the GPU at 99% busy during parts of the campaign. The runner waited for the GPU to fall to 10% before each case, and each case went through `flock` in its own 30-minute unit (see [GPU PC](#gpu-pc)) |

## Profiles (before)

Profiles were taken at 256 and 512 definitions, with product limits, before any fix.

- **Placement move.** 99% of the samples were inside `validateComponentDefinitions`. That function
  restores every definition as a canonical document (`Document::restore`), which validates every body and
  tessellates every face to check its topology. It ran on every commit, including a move that changed no
  definition. At 256 definitions a move cost 16 ms of CPU.
- **Definition edit.** 92% of the samples were inside the same validation, through the component draft:
  `editComponentDefinition` restores the whole document's definitions to build its draft. At 256
  definitions an edit cost about 120 ms of CPU.
- **Creation.** 95% of the samples were inside the validation, called from `placeComponent` and from the
  publish step of `createComponent`. Every creation validated every definition, so the total grew with the
  square of the count (1,024 definitions: 159.7 s, each doubling 3.6 to 4.1 times the last).
- **Push/pull.** Refused above 1,000 faces. Its coplanar check compared every pair of faces, so a body above
  about 1,414 faces would exceed its 1,000,000-pair budget even without the context limit.

## Fixes and their measured effect

### 1. Edits that change no definition skip definition validation (D-107)

A commit that changes no definition, tag, material or asset skips the definition check (`applyEdit`).
Its definitions were validated when they were stored, and nothing they depend on has changed. An edit that
changes a definition checks that definition and every definition that places it
(`validateComponentDefinitionChanges`). A tag, material or asset change, and the full-validation oracle, still
check every definition.

The check keeps its cheap parts for every definition (identity, sizes, nested references, so a reference to an
erased definition is refused). It adds the full geometry check only for changed definitions. The expansion
graph is checked for the changed definitions and their placers. A randomized test compares the scoped check
with the full one on 400 seeded candidates: they agree, and both outcomes occur.

Measured on the laptop (product limits, placement move, p95): 1,024 definitions 82 ms before, 2.2 ms after.

### 2. Placement and definition publishing validate only what they change (D-107)

`placeComponent` needs only the prototype's expansion sizes, and `publish` checks only the definitions the
candidate changes or erases. The parameter query's sizes are computed for the one definition it reports. Each
computes sizes by the same graph checks as before, without re-validating stored geometry.

### 3. The definition draft shares the stored definitions (D-107)

`editComponentDefinition` builds its draft from the document's definitions. Restoring them re-validated and
re-copied every definition, so the draft cost the document's whole geometry. The draft now uses a restore path
that shares them (`DocumentDraftAccess`, a friend of `Document`, used only by the draft). The draft's floors are
computed as before. The native loader and every other restore still validate every definition.

Measured effect at 1,024 definitions on the laptop, with fixes 1 to 3 together: a definition edit fell from
353 ms p95 to 11 ms p95.

### 4. Creation is still superlinear (D-107, not met)

| Definitions | Creation, before | Creation, after (laptop) | Creation, after (GPU PC, product) | Ratio to the previous tier, after (laptop) |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 2.4 s | 0.2 to 0.3 s | | |
| 256 | 9.3 s | 0.6 to 0.8 s | | 2.5 |
| 512 | 39.2 s | 1.7 to 2.2 s | | 2.8 to 3.0 |
| 1,024 | 159.7 s | 6.2 to 8.0 s | 4.8 s | 3.6 to 3.7 |
| 2,048 | not run before | 38 to 53 s | 22.5 s | 3.8 to 4.1 |
| 4,096 | not run before | 223 s (shared machine) | 118 s | 6.0 |

Creation at 1,024 is 20 to 25 times faster than before. The remaining cost is one pass over every definition and
placement per creation: the change check and its placer map, the record checks, the unique-geometry total and
the variant index, and the copies of the definition map. Each is linear, and together they make the total grow
faster than linearly. Removing them needs an incremental path for definition changes (today a definition change
takes the full commit scheme), a larger design change than this brief asks for. It is recorded as the next step.

### 5. The history charge counts each changed record once (D-104)

The undo budget charged a changed record its before copy and its after copy. The history keeps one of them: the
after copy while the change is applied, or the before copy while it is undone. The other is the live record, or
a copy an earlier edit already charged. A chain of edits therefore charged each record version twice. The charge
is now the copy the change creates (its after), or for an erasure the copy it removes (its before). Paint, terrain
and their special cases are unchanged, and so are the other record kinds (see
[contradictions](#contradictions-with-the-design-records)).

Measured against real heap growth (the process's heap in use, before and after one edit, with the undo entry
kept):

| Case | Charge before | Charge after | Heap growth | Ratio, charge to heap, before and after |
| --- | ---: | ---: | ---: | --- |
| Definition edit, 5,000 placements of a 28-face prism (140,000 faces) | 175.7 MB | 88.3 MB | 84.7 MB (80.8 MiB) | 2.07, then 1.04 |
| Body move, one sphere of 99,000 faces | 63.4 MB | 31.7 MB | 30.1 MB (28.8 MiB) | 2.11, then 1.05 |
| Definition of 99,000 faces, 3 placements (297,000 faces) | 253 MB (derived, not measured) | 126.7 MB | 120.4 MB (114.8 MiB) | 1.05 after |
| Definition of 99,000 faces, 5 placements (495,000 faces) | 380 MB (derived, not measured) | 190.1 MB, admitted | 180.7 MB (172.3 MiB) | 1.05 after |

The "before" cells for the two definitions are derived: the old charge was the sum of both copies, so twice the
after figure. Both were over the 192 MiB budget (201 MB), so those edits were refused before the change. A
definition of 176,000 faces (the R114 case) is one body only on the scale-8 harness; at product limits it needs at
least two bodies, since a body is capped at 100,000 faces and a definition at 200,000. On the scale-8 harness it
is still refused by the budget after this change (see the GPU table).

Undo leaves the heap about 28.7 MiB higher in the two definition cases above, at both 3 and 5 placements. That
amount does not grow with the placements. It is not charged to the history, and I did not profile it.

### 6. Push/pull on large bodies (D-106)

- **The context limit is gone.** `pushPull` checks only the one-body limit (`Surface::bodyLimit`, 100,000 faces or
  vertices, enforced by `Surface::validate`). The loop limit (10,000 vertices a loop) stays.
- **The coplanar check is a grid over bounding boxes.** Two faces can share a region of positive area only when
  their bounding boxes, grown by four times the tolerance, overlap. Faces are binned in a grid of cells about the
  three-quarter extent of the faces, and each overlapping pair is tested once, in the cell that holds the lower
  corner of its overlap. Large faces are tested against every face. The pair test is the old one: the first face's
  plane must hold the second within the tolerance, and the projected regions must overlap in an area of at least
  one unit. A projected region lies in the intersection of the two boxes, so a pair whose boxes overlap in less than
  one unit is skipped without changing the answer.
- **The cut tests only the vertices on its plane.** A cut segment lies on the destination plane, so only vertices
  within twice the tolerance of that plane can match. The match test itself is unchanged.
- **The candidate tests are bounded** by a budget of 64 tests per face plus one million, refused by name when
  exceeded, so a pathological body still fails closed.

Measured, sphere bodies, push of one face by 1 cm, undo included (laptop, product limits, 20 samples):

| Faces | Push p50 | Push p95 | Undo p95 | Before |
| ---: | ---: | ---: | ---: | --- |
| 1,000 | 9.6 ms | 10.3 ms | 0.07 ms | refused (context limit) |
| 10,000 | 109.5 ms | 110.9 ms | 0.77 ms | refused |
| 99,000 | 1,320 ms | 1,338 ms | 6.5 ms | refused |
| 100,000 | refused: the push adds 200 faces, and the result (100,200) exceeds the one-body limit | | | refused |

A push that adds faces to a 100,000-face body is refused by the body limit, so the largest sphere that can be
pushed is just under the limit. The 99,000-face sphere was measured instead. Time per face is 10 to 14
microseconds across the sizes. A profile at 10,000 faces attributes 45% of the samples to `pushPull` itself, of
which 19% is the coplanar check and 17% is the validation of the result, and 53% to the document's `applyEdit`
(its copies and topology comparison). Both grow linearly with the faces.

The randomized test compares the new push/pull with the pre-R117 implementation (kept in the test file, unchanged
apart from its namespace) on 80 random bodies and 480 pushes: results and refusal messages must match. Both
outcomes occur. A second test pushes the cap of a 1,000-sided prism, which the old code refused.

### 7. Definition count 1,024 to 4,096

`componentDefinitionCountLimit` is 4,096 (`src/core/component_records.hpp`). The native loader refused a document
with more than 1,024 definitions by a literal in `document_io.cpp`; it now follows the document limit. Without
that fix a saved document with more than 1,024 definitions could not be opened. The `.skp` import keeps 4,096
definitions and counts the rest. A test with injected counts accepts 4,096 definitions and refuses one more,
without building them.

The count is 4,096 because the GPU PC measured a product build at that count within both edit budgets (move 22 ms
p95, definition edit 59 ms p95, frame 10 ms p95), and an earlier product run at 2,048 was noisier (see the GPU
table). The laptop measured the same tier at 148 ms for the definition edit, on a shared machine; the PC is the
budget platform, as in R114.

## Definition-count tiers

Product limits unless stated. Laptop figures are document-level; GPU PC figures are feedback including the frame.

| Tier | Creation | Placement move p95 | Definition edit p95 | Undo p95 | Save, open (file) | Verdict |
| ---: | --- | --- | --- | ---: | --- | --- |
| 1,024 | laptop 6.2 to 8.0 s; PC 4.8 s | laptop 0.8 to 2.2 ms; PC 5.5 ms | laptop 11 to 17 ms; PC 13.5 ms | laptop 0.4 to 0.6 ms; PC 4.2 ms | laptop 0.38 s, 0.57 s (7.8 MB) | Within the budgets |
| 2,048 | laptop 38 to 53 s; PC 22.5 s | laptop 3.4 to 4.8 ms; PC 48.8 ms (one run, noisy: p50 8.7 ms) | laptop 38 to 53 ms; PC 85.5 ms (one run, noisy: p50 26.8 ms) | laptop 1.7 to 2.2 ms; PC 62 ms | laptop 1.68 s, 0.96 s (15.7 MB); PC 0.46 s, 1.75 s | Within the budgets, but the PC run was noisy |
| 4,096 | laptop 223 s (shared machine); PC 118 s | laptop 12.9 ms (shared); PC 21.7 ms | laptop 148 ms (shared); PC 58.8 ms | laptop 7.6 to 8.8 ms; PC 19 ms | PC 1.2 s, 4.1 s (31.3 MB), reopened with an empty history | Within the budgets on the PC. Chosen |
| 8,192 | not built | | | | | Not measured. Projected from the PC's 1,024 and 4,096 runs (definition edit about 2 times per doubling, so about 120 ms) |
| 16,384 | not built | | | | | Not run (see below) |

The 2,048 PC run was made while another user had the GPU at 99% (see [GPU PC](#gpu-pc)). Its move and edit p95
values are 5 to 10 times the same case's p50 and far above the 4,096 product run, which repeated the scale-8 4,096
run within a few percent. The decision uses the 4,096 runs.

**What stops the next tier.** A definition edit's cost roughly doubles with each doubling of the definitions
(14 ms at 1,024 to 59 ms at 4,096 on the PC), so 8,192 is projected at about 120 ms, over the 100 ms budget. The full
commit path still walks the whole document for an edit that changes a definition: the unique-geometry total, the
variant index and the copies of the body and instance maps. Creation at 4,096 takes 2 minutes on the PC, and
16,384 would take more than 15 minutes at the measured rate, plus a product build that could not hold it: 16,384
28-face prisms are 458,000 faces, above the product unique limit of 300,000.

**The material limit (1,024)** is not raised. The brief allows that only when it costs no more than a small
fixture, and this record does not measure it.

## GPU PC

Cases use the R114 harness. Each ran through `flock` in its own unit, with `MemoryMax=18G`,
`MemorySwapMax=0`, `RuntimeMaxSec=1800`, `LimitCORE=0`. The runner waited while the GPU was above 10% busy
before each case, so a case started on an idle GPU; during a case the GPU is busy from the harness's own
rendering. Another user's load returned during some campaign intervals.

| Case | Build | Frame p95 | Move feedback p95 | Definition-edit feedback p95 | Undo p95 | Refusals | R114 (PC), move / edit |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| 1,024 definitions, one placement each (3 runs) | scale 8 | 3.1 to 3.3 ms | 5.1 to 6.4 ms | 13.8 to 14.6 ms | 4.0 to 5.1 ms | none | 268 ms / 1,057 ms |
| 1,024 definitions, one placement each (1 run) | product | 3.1 ms | 5.5 ms | 13.5 ms | 4.2 ms | none | 268 ms / 1,057 ms |
| 2,048 definitions, one placement each (1 run) | product | 5.4 ms | 48.8 ms (p50 8.7) | 85.5 ms (p50 26.8) | 62 ms | none | not run |
| 4,096 definitions, one placement each (1 run) | product | 10.1 ms | 21.7 ms (p50 19.1) | 58.8 ms (p50 55.9) | 19 ms | none | not run |
| 4,096 definitions, one placement each (1 run) | scale 8 | 9.3 ms | 21.5 ms | 58.6 ms | 22 ms | none | not run |
| 176,000-face definition, 3 placements (3 runs) | scale 8 | 1.7 to 2.0 ms | 14.0, 27.0, 49.4 ms (p50 12.4 to 27.8) | refused | 12 to 41 ms | "Edit exceeds the 192 MiB history budget" | 258 ms / not measured |
| 176,000-face definition, 27 placements (1 run) | scale 8 | 6.6 ms | 15.4 ms (p50 14.9) | refused | 15.6 ms | same | 258 ms / not measured |

For the 176,000-face cases the move rows time one placement's move. The harness stops an edit kind at its first
refusal, so the definition-edit rows have no samples.

The 176,000-face definition at 3 and 27 placements (both at scale 8, the R114 conditions) is refused by the undo
budget. The refusal carries no figure. The 99,000-face measurement above gives a charge of 126.7 MB at 297,000
faces, so the 528,000 faces at 3 placements would be about 225 MB, over the budget. That estimate is not a
measurement.

Move feedback is the time from the document change to the first complete viewport frame, including the
framebuffer readback, as in R114.

## What failed and how

1. **A 100,000-face push/pull was refused** by the one-body limit, because the push adds 200 faces. The record
   measures 99,000 faces and states the limit.
2. **gdb could not attach** to a benchmark process (`ptrace_scope` is 1). The profiles use an `LD_PRELOAD` timer
   sampler kept outside the repository.
3. **The first draft fix was not enough.** Definition edits still spent 83% of their time in validation, through
   the draft's restore. The draft now shares the stored definitions.
4. **A literal 1,024 in the native loader** refused a saved document with more than 1,024 definitions. The save
   and open run at 2,048 found it. The loader now follows the document limit.
5. **Two tests assumed the old charge.** `scene_tests` expected 400 paints to exceed the budget, which they no
   longer do (each paint is charged once, about 0.28 MB); the test now uses 800 paints, with the same assertion.
   `unique_limits_acceptance_tests` read the newest history entry from `history()`, which returns the oldest 200
   entries by default; with more entries kept, it now asks for the newest by its offset.
6. **The push/pull volume check** compared 1,000-sided prisms with a 1e-6 tolerance. Clipper's 1e-7 integer grid
   gives about 1e-5 of rounding there, so the check uses 1e-4. It still checks the swept volume.
7. **The driver's arguments** for `placements` were read from the wrong position for one run; fixed, and the run
   repeated.
8. **The 176,000-face definition is refused by the budget.** The charge fix counts each record once, about half
   the old charge, but the old charge had already refused this edit by a wide margin. R114's refusal stands at
   scale 8. This is a finding for the budget's owners, not a change made here.
9. **The 2,048-definition PC run at product scale** was noisier than the 4,096 run that followed it, with its move
   and edit p95 values far above its p50 values, and another user's GPU load returned during the campaign. The
   decision uses the 4,096 runs, which agree with each other.
10. **Two laptop builds started while another worker's build was running.** One compiled the timing driver and
    the other the I/O library. Both were single-target builds in this worktree; no other worker's tree was
    touched. Later builds went through a wrapper that waits for an idle machine.

## Not run, and why

- **8,192 and 16,384 definitions.** Not built. 8,192 is projected above the budget (see the tiers). 16,384 would
  need more than 15 minutes of creation on the PC and more faces than the product unique limit allows for these
  prisms.
- **A quiet laptop run at 4,096 definitions.** The laptop was shared when the 4,096 timings were taken; the PC
  product run is the evidence.
- **The 176,000-face definition edit.** Refused by the budget at scale 8, so no timing was taken.
- **The materials limit (1,024).** Not raised and not measured.
- **The full CTest suite and the CI full tier.** Not run here. The 33 core and I/O test executables that the change
  can affect were built and run (the list is in `R117-edit-path.json`), and CI runs the fast tier on the pull
  request.

## Contradictions with the design records

- **ADR 0005, "History budget" and its R114 notes.** The charge counted both copies of a changed record, so a
  definition edit at 5,000 placements was 175.7 MB. R117 charges one copy. The R114 statement that the charge
  "considers the definition's faces only" is still true of the 200,000-face cap, and the placements' records are
  still in the charge. The R114 notes are superseded where they state the charge.
- **ADR 0005, "Implementation notes (R114: measured limits)", on push/pull.** It records the 1,000-face context
  limit and the 1,000,000-pair budget. Both are gone; the one-body limit is the only limit.
- **The undo charge for other records.** Scenes, sections, tags, materials, assets, annotations, drawing records and
  camera paths still charge both copies. The same overstatement applies to them; R117 did not change them, because
  the brief's defect was about bodies and component records.
- **UX.** The refusal message "Push/pull exceeds the 1000-face/10000-vertex context limit" no longer exists. A push
  that would exceed the one-body limit is refused by the body limit's message.

## Risks

- **Admissions change.** Edits the old budget refused are now admitted, for example a 99,000-face definition placed
  five times, and body moves of about twice the old face count. The measured memory is within 5% of the charge for
  these cases, but the budget now protects less of the history's other record kinds than the design assumed.
- **The scoped validation relies on an invariant:** a stored definition was validated when it was stored, and it
  changes only through an edit that names it. The randomized test, the fail-closed tests and the full-validation
  oracle in the incremental-validation and parameter tests cover it. A future edit path that changes a definition
  without naming it would bypass the check.
- **Creation is still superlinear.** A 4,096-definition creation takes about two minutes on the PC and longer on
  slower machines.
- **Undo heap growth** (about 29 MiB in the 99,000-face cases) is unexplained.
- **The count and the file.** A file saved with more than 1,024 definitions cannot be opened by builds before this
  change, and a file saved with more than 4,096 is refused by this one.

## Evidence

- [R117-edit-path.json](R117-edit-path.json): the summaries of the tables above, the tests, the GPU results and the
  cases not run.
- The raw timing outputs and the sampler profiles stay on the development laptop; the GPU samples stay on the GPU
  PC, as in R114.
- Driver: `tests/edit_path_benchmark.cpp` (headless, product and scale-16 builds).
- Tests: `tests/component_record_tests.cpp` (the scoped check, the count limit), `tests/push_pull_tests.cpp` (the
  reference comparison, the large body), `tests/scene_tests.cpp` (eviction, paints),
  `tests/unique_limits_acceptance_tests.cpp` (the at-limit commit's newest entry), `tests/skp_mapping_tests.cpp` (the
  import's count).
