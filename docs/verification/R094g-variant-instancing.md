# R094.g — Instanced rendering of component parameter variants

2026-10-16. Branch `build/r094g-variant-instancing`. The code is `6ed9cb12`, the suite
is `84937092`, the benchmark family is `e9855c63` and `bf3e2fa9`, and both merges of
`origin/f02/integration` are included (the second at `64a29e01`). The design is in
[ADR 0166 §7](../decisions/0166-configurable-component-interface.md#7-instanced-rendering-of-variants)
and its "Implementation notes (R094.g)". This record covers the R094.g Verify line.
It is not M9 acceptance.

## Result

| Check | Result |
| --- | --- |
| Framebuffer and picking hashes, sharing on and off | Equal in all 10 suite states, at X11 and Wayland, 1× and 2× |
| 1,000 placements of one variant against 1,000 default placements | Body-cache and GPU payloads equal (ratio 1.00; the criterion is within 10 %) |
| 10,000 all-overridden placements, software, Release, one CPU | With the 1 mm override, within run-to-run noise of the repeated family for complete-frame and edit-to-feedback p95 (two interleaved rounds, three runs per family); a 2.5 m round was 20–43 % slower (raster area, see below) |
| Absolute M9 budgets in software (33.3 ms frame, 100 ms edit) | **Not met by either family**: one llvmpipe CPU takes 0.9–4.3 s per frame at a million triangles. These are hardware budgets, and the AMD session is waived ([ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)) |
| Documents without parameters | Identical fixture and frame hashes, counters and payloads against the base build; `placement_viewport_tests` output identical in all 18 states and 4 cells; no existing test changed |
| `variant_instancing_input_tests`, X11 and Wayland, 1× and 2× | 4 of 4 pass |

## Sharing, picking and framebuffers

`variant_instancing_input_tests` draws a window component with stretch, position,
repeat and visibility drivers. It compares placement sharing on and off in every state
and asserts that both the framebuffer SHA-256 and a hash of 63 grid picks are equal.
The picks combine `pick`, `pickEdge` and a `selectionAt` subset.

The states are:

- defaults;
- variant A;
- three equal-value placements of variant A;
- variant B (sill hidden) with a mirrored variant-A placement;
- X-ray;
- monochrome with profiles;
- an entered context;
- show hidden;
- a move and a reset;
- undo and redo.

The coordinates are dyadic, so the shared path and the per-body path round identically.

| State | Placement meshes (expected) | Shared bodies (expected) |
| --- | ---: | ---: |
| Defaults: 8 placements, one mirrored | 10 (10) | 40 (40) |
| One placement at variant A | 14 (14): +4 driven records (frame, handle, two repeat copies) | 42 (42) |
| Three placements at variant A | 14: no mesh, upload or tessellation added | 46 (46) |
| Variant B and a mirrored variant-A placement | 19 (19) | 48 (48) |

A copied variant placement costs exactly what a copied default placement costs: one
upload and one tessellation (its root), and no local inference index beyond the
default copy's. Inference equals an expanded brute-force search in 120 queries. On the
code before `6ed9cb12` the suite fails with "Variant A adds exactly its four driven
meshes".

## Payloads

| Fixture (Debug, llvmpipe) | Body-cache vectors B | GPU payload B | Placement meshes | Shared bodies |
| --- | ---: | ---: | ---: | ---: |
| Suite window, 1,000 default placements | 52,600 | 22,800 | 5 | 5,000 |
| Suite window, 1,000 placements of width 1.5 m | 52,600 | 22,800 | 5 | 5,000 |
| Benchmark repeated, 1,000 and 10,000 | 89,266 | 34,656 | 1 | 1,000 / 10,000 |
| Benchmark variant (2.5 m), 1,000 and 10,000 | 89,266 | 34,656 | 1 | 1,000 / 10,000 |
| Benchmark variant (2.001 m), 10,000, Release | 89,266 | 34,656 | 1 | 10,000 |

Inference also indexes both 1,000-placement suite families identically: 6 local
builds, 260 indexed primitives and 2,620,696 B.

## Documents without parameters

The base is `origin/f02/integration` `ddf7016e`, built in the same image. The repeated
family at 1,000 and 10,000 placements gives the same results on base and branch:

- fixture hashes: `3eba027d…` at 1,000 and `2a02cd2b…` at 10,000;
- warm and final framebuffer hashes;
- placement meshes, shared placements, tessellations, mesh builds and uploads;
- payloads and all inference counters.

`placement_viewport_tests` per-state output (pixel differences, picks, meshes) is
identical on base and branch at X11 1× and 2× and Wayland 1× and 2×.
`tests/real_model_benchmark.cpp` only adds the `variant` family; no other test changed.

## Timing at 10,000 placements

Release build in the pinned CI image, with R082.jj's controls: one CPU (pinned core),
4 GiB, no swap, xvfb 1920×1080×24, xcb and llvmpipe. Each run renders 100 timed edits.
The families are interleaved, and the host load average (1 min) is given before and
after each run. The host is shared with other build workers, so wall-clock p95 varies
up to 4× between runs of the same family.

**Round 1: variant at 2.5 m** (commit `4f2a79fc`).

| Run | Family | Complete frame p95 ms | Edit → feedback p95 ms | Edit + readback p95 ms | Inference update p95 ms | Load |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 1 | variant | 2,949 | 2,851 | 5,487 | 50.4 | 16.47 → 5.11 |
| 2 | repeated | 1,399 | 1,540 | 3,069 | 36.7 | 5.11 → 3.84 |
| 3 | variant | 1,663 | 1,848 | 3,552 | 39.6 | 3.84 → 6.06 |
| 4 | repeated | 1,291 | 1,775 | 3,550 | 38.8 | 6.06 → 4.42 |
| 5 | variant | 1,852 | 1,362 | 2,695 | 34.1 | 4.42 → 3.22 |
| 6 | repeated | 885 | 1,458 | 2,976 | 34.5 | 3.22 → 3.74 |

**Round 2: variant at 2.001 m** (commit `22a5b6e8`).

| Run | Family | Complete frame p95 ms | Edit → feedback p95 ms | Edit + readback p95 ms | Inference update p95 ms | Load |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 1 | variant | 2,127 | 2,716 | 5,497 | 52.6 | 0.72 → 11.66 |
| 2 | repeated | 2,651 | 1,973 | 3,946 | 43.4 | 11.66 → 5.76 |
| 3 | variant | 2,781 | 1,489 | 3,022 | 35.1 | 5.76 → 4.66 |
| 4 | repeated | 3,681 | 3,850 | 7,956 | 71.7 | 4.66 → 17.40 |
| 5 | variant | 4,306 | 4,063 | 8,290 | 73.7 | 17.40 → 17.53 |
| 6 | repeated | 1,922 | 3,834 | 7,398 | 70.5 | 17.53 → 37.76 |

**Round 3: 2.001 m, with temporary per-phase CPU instrumentation.** The
instrumentation is uncommitted, as in R082.jj. It records the thread CPU time of
`Document::transform` (apply) and `Viewport::refresh` per edit, and the process CPU
time of each stationary frame across all threads, llvmpipe included. Each phase
column is a mean over 100 edits or 50 frames.

| Run | Family | Frame p95 ms | Edit → feedback p95 ms | Load | Apply CPU ms | Refresh CPU ms | Frame CPU ms |
| ---: | --- | ---: | ---: | --- | ---: | ---: | ---: |
| 1 | variant | 2,249 | 3,289 | 27.58 → 17.75 | 1.50 | 20.3 | 1,666 |
| 2 | repeated | 1,731 | 1,520 | 17.75 → 3.53 | 0.44 | 15.9 | 1,281 |
| 3 | variant | 889 | 2,332 | 3.53 → 8.27 | 1.08 | 17.1 | 876 |
| 4 | repeated | 2,590 | 2,598 | 8.27 → 5.01 | 0.56 | 18.9 | 1,885 |
| 5 | variant | 2,430 | 2,212 | 5.01 → 9.29 | 1.14 | 17.5 | 1,818 |
| 6 | repeated | 2,046 | 2,394 | 9.29 → 7.83 | 0.61 | 18.7 | 1,801 |

Medians of the three runs per family and round:

| Round | Frame p95 (variant / repeated) | Edit → feedback p95 (variant / repeated) |
| --- | --- | --- |
| Round 1 (2.5 m) | 1,852 / 1,291 ms | 1,848 / 1,540 ms |
| Round 2 (2.001 m) | 2,781 / 2,651 ms | 2,716 / 3,834 ms |
| Round 3 (2.001 m) | 2,249 / 2,046 ms | 2,332 / 2,394 ms |

Runs of one family spread up to 2.9× (repeated frame p95 885–3,681 ms), with host
load. With the 1 mm override (rounds 2 and 3), the variant medians are within 10 % of
the repeated medians for the complete frame (+5 %, +10 %). For edit-to-feedback they
are lower or equal (−29 %, −3 %). Both are inside the spread of the repeated runs.

With the 2.5 m override (round 1), the variant medians are 43 % (frame) and 20 % (edit)
higher. That round draws prisms 25 % wider, so llvmpipe rasterizes more area per frame.
That is consistent with the gap, but the noise is too large to prove it. The benchmark
therefore uses the 1 mm override, which isolates the instancing path. Deterministic
counters, cache bytes and draw work are identical between the families in every round.

### The 2.2× edit-feedback gap in the first report

The earlier Debug, 4-CPU run 2 (variant 2,976 ms against repeated 1,371 ms) ran while
the host load rose from 13 to 23 during the variant run. In Release with a single CPU
and interleaved runs, no edit gap above noise remains (table above).

Per-phase CPU time does show one real difference, well below the budget:

- `Document::apply` of a placement move costs 1.1–1.5 ms in the variant family against
  0.4–0.6 ms in the repeated family at 10,000 placements.
- At 1,000 placements it costs 0.37–0.53 ms against 0.33 ms (two runs each).
- The cost grows with the number of variant placements. The cause is
  `checkUniqueGeometry` in `src/core/model.cpp` (from R093.b `74f6d509`), which every
  apply runs. While any placement shows a variant, it loops over
  `ComponentVariantIndex::placements` and calls `variantDirect`, a walk of the
  variant's member records, once per variant placement. That is
  O(variant placements × members per variant) per edit.
- R094.g does not change it. Keeping per-variant placement counts in the variant index
  would make it O(distinct variants).
- `Viewport::refresh` (16–20 ms) and the inference update are equal within noise. The
  frame is llvmpipe-bound for both families.

## Native results

All lines are the exact `native.yml` invocations. They ran in
`sketchyup-ci-pinned:r094a`: the pinned `archlinux:base-devel@sha256:51dd3d24…` with
the 2026-10-02 archive mirror and the CI package set (Qt 6.11.2, Mesa 26.2.3,
Weston 15.0.1). Each line had a fresh container with 4 CPUs, 16 GiB, SYS_PTRACE and
`QT_FORCE_STDERR_LOGGING=1`, on a Debug `cmake --preset dev` tree built in the same
image. `sketchyup-x11-repro` and `sketchyup-wayland-repro` were not used: they are
built on Arch 20261011, not on the pinned digest.

Merged tree `22a5b6e8`. Container wall times include startup and Weston, whose fixed
limit is 60 s.

| Suite | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
| --- | --- | --- | --- | --- |
| `variant_instancing_input_tests` | pass (37 s; 16 s) | pass (43 s; 22 s) | pass (50 s; 18 s) | pass (57 s; 15 s) |
| `placement_viewport_tests` (R082.dd) | pass | pass | pass ×3, 1 timeout | pass, 1 timeout |
| `component_input_tests` | pass | pass | pass | pass |
| `viewport_precision_tests` | pass | pass | pass | pass |
| `inference_input_tests` | pass | pass | — | — |
| `viewport_tests` | pass | — | — | — |

A dash marks a cell `native.yml` does not run. The two timings for
`variant_instancing_input_tests` are a run at host load 23–31 and a rerun at load
11–18.

`placement_viewport_tests` timed out twice at Weston's 60 s limit while the host load
was 22–31: once at Wayland 2× in the first pass and once at Wayland 1× in the retry.
The logs end in Weston's "caught signal 15" with no test failure. Both cells then
passed. Back-to-back Wayland 1× runs of branch and base took 23 s against 25 s and
22 s against 22 s. That fixture has no parameters, so it takes the unchanged path.
R082.dd recorded the same load-dependent Weston limit. The pre-merge tree passed all
24 cells in one pass.

The suite reports, in every cell:

- equal framebuffer and pick hashes in all 10 states;
- a body-cache ratio of 1.00 and a GPU payload ratio of 1.00;
- the same mesh and body counts as the table above.

On the host, `ctest --preset dev -j6` on the merged tree passes 208 of 208. The 12
`*_real` consumer tests skip as usual.

## Commands

Build (Debug tree for native tests; the Release tree adds `-DCMAKE_BUILD_TYPE=Release`
and builds only `real_model_benchmark`):

```sh
docker run --rm --cpus=6 --memory=12g -v "$WORKTREE":/w -v "$BUILD":/w/build -w /w \
  -e CCACHE_DIR=/ccache -e CMAKE_C_COMPILER_LAUNCHER=ccache -e CMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -v "$CCACHE":/ccache sketchyup-ci-pinned:r094a \
  bash -c 'cmake --preset dev && cmake --build --preset dev --parallel 6'
```

Native line (one container per `native.yml` line, verbatim):

```sh
docker run --rm --cpus=4 --memory=16g --cap-add=SYS_PTRACE -e QT_FORCE_STDERR_LOGGING=1 \
  -v "$WORKTREE":/w -v "$BUILD":/w/build -w /w sketchyup-ci-pinned:r094a bash -c "$LINE"
# e.g. LINE='SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/variant_instancing_input_tests'
```

Benchmark (R082.jj controls; `FAMILY` is `variant` or `repeated`, alternated):

```sh
core=$(python3 -c 'import os; print(max(os.sched_getaffinity(0)))')
docker run --rm --init --cpus=1 --cpuset-cpus="$core" --memory=4g --memory-swap=4g \
  --pids-limit=256 -v "$WORKTREE":/w:ro -v "$RELEASE_BUILD":/w/build:ro -w /w \
  sketchyup-ci-pinned:r094a xvfb-run -a -s '-screen 0 1920x1080x24' \
  env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 build/dev/real_model_benchmark 10000 "$FAMILY"
```

Payload and base comparisons used the Debug tree with `--cpus=4 --memory=8g` and
the same xvfb line. The host suite was `ctest --preset dev -j6`.

## Caveats

- Timings come from a shared host (load average 0.7–38 during the runs). They support
  parity between the families, not absolute values.
- No hardware run. The AMD session is waived (ADR 0161).
- The suite's Wayland 2× run takes about 49 s of a container's wall time against
  Weston's fixed 60 s limit on this loaded host.
- The per-phase instrumentation is not committed.
