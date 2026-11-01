# R082.w v3: memory budget at the R110 unique limits (measured in software; over the 3 GiB ceiling)

Version three of the [memory calibration](R082w-memory-budget-v2.md), for the 1.7.0 unique
limits of [ADR 0005](../decisions/0005-document-protocol.md#raised-unique-geometry-limits-r110-170).
Versions one and two stay as they are. Its machine-readable record is
[R082w-memory-budget-v3.json](R082w-memory-budget-v3.json).

**Status: measured, in software only. The modeled worst case is over the 3 GiB ceiling.** The
worst case with the retained-history row at the 192 MiB undo budget is 3.03 GiB. With the
per-unique-vertex cost that the measurements show, it is 3.58 GiB. Under the R110 stop rule, no other
limit changed, and the unique-limit change is not accepted for 1.7.0. The owner must decide how to
resolve the ceiling (see [Decision needed](#decision-needed)).

## How it was measured

- **Harness.** `real_model_benchmark` (viewport, frame and edit times) and `inference_index_benchmark`
  (the shared-after index), built Release and static with the desktop on and the CLI, IFC and
  fixtures off. The new `unique_limits_benchmark` (core and I/O only, `EXCLUDE_FROM_ALL`) measured the
  file, load, densest-shape and history cases, which need no viewport.
- **Image and graphics.** `sketchyup-native-fonts-check:20261008`: Qt 6.11.2, llvmpipe (LLVM 23.1.1,
  Mesa 26.2.3), xcb, 1920 by 1080, scale 1. The kernel was 7.2.8-9-omarchy.
- **Containers.** The build ran with `--cpus=2 --memory=4g --memory-swap=4g` and `-j2`, and took
  about 17 minutes for its 453 steps. Every measurement ran with `--cpus=1 --memory=4g
  --memory-swap=4g`, as v2 did.
- **Differences from v2.** v2 records the same image, Release, llvmpipe, xcb, one CPU and 4 GiB, but
  not its Mesa or LLVM version, so the graphics stack may differ. The source revision also differs:
  it includes the R082 to R110 changes. Peak RSS is the process peak (`getrusage`).
- **Not from the image.** The local host build (Debug, `-g0`, shared libraries) is not calibration. It
  gave the same charged history bytes (the accounting is structural), but its load and inference
  timings were four to ten times slower.

## Results

Peak RSS is the process peak. Times are wall clock. The viewport figures are software (llvmpipe).

| Case | Shape | Peak RSS | Load and save | Frame p95 | Inference build, query p95 |
| --- | --- | ---: | --- | ---: | --- |
| (a) v2's unique 100k family | 1,000 loose prisms, 100,000 triangles, 52,000 vertices | 607,469,568 B (579 MiB); v2 560,132,096 B | load 0.22 s, save 0.24 s (core) | 187 ms | 215 ms, 0.51 ms |
| (b) unique family at 300k | 5,769 loose prisms, 576,900 triangles, 299,988 unique vertices, no placements | 1,915,072,512 B (1,826 MiB) | load 1.24 s, save 1.44 s (core) | 493 ms | 1,029 ms, 1.21 ms |
| (b2) densest shape | 3 closed 100,000-face spheres in one component: 150,006 vertices, 300,000 faces, 450,000 edges | 974 MiB | load 2.52 s, save 1.23 s (core) | not measured (core) | 1,269 ms, 0.64 ms |
| (c) file of 89 MiB | 5,769 prisms, 299,988 vertices, 220 bodies with 128 properties of 2,040 bytes; document chunk 89.1 MiB (bound 96 MiB), container 89.3 MiB | 905 MiB after load; 1,391 MiB at the end | read and load 1.51 s, save 1.53 s (core, fresh process) | not measured (core) | 964 ms, 0.86 ms |
| (d) whole-document edit in history | 5,700 of 5,769 bodies moved in one edit, kept in history: charged 191.0 MiB | 112 MiB before, 297 MiB kept; undo 0.01 s, redo 0.01 s | | not measured (core) | |

The inference build and query figures are the production index on the real-model fixture (b, a) and
on the core-only families (b2, c). `inference_index_benchmark` (shared-after) gives 275,212,900 bytes
of index for the 300,000-vertex family, and v2 modeled 917 bytes per unique vertex (275,100,000).
The index therefore matches the model.

**Refused edit (d).** Moving all 5,769 bodies in one edit is refused with `Edit exceeds the 192 MiB
history budget`. Scaled from 4,000 bodies (134.0 MiB charged), the whole family would charge about
193 MiB. It was not measured, because the edit is refused.

**The loose unique shape costs more than v2's terms.** The (b) peak of 1.92 GB is 589 MB above the v2
terms for the same shape (base, 300,000 materialized vertices at 2,542 B each, and 300,000 unique
vertices at 917 B each). That is 1,964 B per unique vertex. The slope between (a) and (b) gives about
544 MB at 300,000 vertices. Either way, the shape costs more than v2's modeled terms. The brief's rule
is to add a cost like this to the worst case, so it is added below.

## Modeled worst case at the R110 limits

The table is [v2's table](R082w-memory-budget-v2.md#modeled-worst-case), with the unique-vertex
inference term at 300,000 vertices, and a retained-history row at the 192 MiB undo budget.

| Term | v2 (bytes) | v3 (bytes) |
| --- | ---: | ---: |
| Base, including 1,000 shared placements (repeated 1k, five cycles) | 288,276,480 | 288,276,480 |
| 19,000 more placements × 36,138 B | 686,622,000 | 686,622,000 |
| 600,000 materialized vertices × 2,542 B unshared viewport payload | 1,525,200,000 | 1,525,200,000 |
| Unique vertices × 917 B inference index (100,000 in v2, 300,000 in v3) | 91,700,000 | 275,100,000 |
| Maximum texture fixture (textures 1k − repeated 1k) | 273,436,672 | 273,436,672 |
| Retained undo history at the 192 MiB budget (new) | — | 201,326,592 |
| **Total** | **2,865,235,152 (2.67 GiB)** | **3,249,961,744 (3.03 GiB)** |

The ceiling is 3 GiB (3,221,225,472 bytes). **The v3 total is over it by 28,736,272 bytes (27.4 MiB).**
Without the history row the total is 3,048,635,152 bytes (2.84 GiB), which leaves 164.6 MiB of
margin. The history row alone uses more than that margin.

Adding the measured excess for the unique shape (589,096,032 bytes, from (b)) gives
3,839,057,776 bytes, or 3.58 GiB, which is 0.58 GiB over the ceiling and still under the 4 GiB
operational job cap.

## Decision needed

The brief's stop rule applies, and R110 does not change any other limit. The owner decides how to
resolve the ceiling. Three options, none chosen here:

1. Keep the unique limits and raise the 3 GiB modeled ceiling to match the measured worst case (3.03
   GiB, or 3.58 GiB with the unique-shape excess). The 4 GiB operational job cap still holds.
2. Lower the unique vertex limit, or the undo budget, until the measured worst case fits 3 GiB. The
   unique vertex limit sets the inference and viewport terms, and the undo budget sets the history row.
3. Hold 1.7.0 at the 1.6.1 unique limits, and keep this record as the measured evidence for the
   change.

The measured figures do not show a failure of the shape. The decision is whether the ceiling is a
product claim that the new limits must keep.

## Not measured

- Frame time for (c) and (d), and a viewport frame time for the densest shape (b2). The core-only
  harness has no viewport. A viewport measurement of a file-loaded document would need a file option
  in `real_model_benchmark`.
- The charged history of the 300,000-face component edit. It is refused at 192 MiB; the figure of
  about 275 MiB is a linear estimate from the two-sphere edit (183.1 MiB), not a measurement.
- Hardware. Acceptance stays under the [ADR 0161 waiver](../decisions/0161-m9-hardware-evidence-waiver.md),
  as it did for v2. The fresh Intel and AMD reruns in [M9_HARDWARE_SESSION.md](../M9_HARDWARE_SESSION.md),
  section 1, remain required before any ceiling is accepted.

## Relation to earlier versions

v1 and v2 stay as they are. v2's 64 MiB retained-history row is its own sampled residual growth
(6.6 MB), a different quantity from the undo budget. v3 adds the undo budget as a row of the modeled
worst case, at 192 MiB.
