# R082o — Edit timing through the first complete viewport frame

The benchmark now separately records the interval from a synchronous document
edit through the first complete viewport frame and GPU completion wait. Its older
`editAndReadbackP95Ms` remains unchanged. That older interval can also include a
second paint plus framebuffer readback, so it does not isolate the first frame
produced after an edit. This change adds evidence; it does not alter production
rendering, document limits or performance budgets.

## Timing boundary

The edit loop copies the same monotonic start timer into the instrumented
viewport immediately before `Document::transform`. The first subsequent
`paintGL` finishes all viewport QPainter overlays, calls `glFinish`, and records
the elapsed interval once. Later paints cannot overwrite it. Each of 100 edits
asserts that a completed frame was observed and that its timestamp precedes the
enclosing readback completion. The previous complete-frame, scene-only,
readback, picking, inference and retained-memory fields remain present.

`editToViewportGpuCompleteP95Ms` includes document mutation, refresh and queued
events before that frame. It excludes the input event queue, child widgets,
subsequent readback/repaint and compositor presentation. It is not a measurement
of what reached the physical display. Undo/redo still exercises bounded history;
this new timer covers the 100 transform edits, not 100 rendered undo/redo steps.

## Validation and observed values

All 10 software smoke cases pass: five fixture families at native Wayland scales 1
and 2. All 20 hardware cases also pass: a 25-placement smoke and one 1,000-placement
run for each family on AMD and Intel, using the same stripped artifact and
unchanged canonical fixtures. Each full run contains 100,000 triangles, 50 frame
samples after 10 warmups, 100 edits, and the existing 100 undo/redo operations.

The table reports p95 milliseconds from each single full-count run. It is not a
cross-run median or a controlled before/after performance comparison.

| GPU | Scenario | Complete viewport | Edit → first complete viewport | Edit + readback |
| --- | --- | ---: | ---: | ---: |
| AMD | repeated | 2.170 | 6.436 | 16.504 |
| AMD | unique | 2.206 | 3.774 | 16.499 |
| AMD | deep | 2.323 | 16.460 | 19.608 |
| AMD | far | 2.166 | 6.898 | 32.226 |
| AMD | textures | 2.231 | 10.259 | 16.257 |
| INTEL | repeated | 10.273 | 24.957 | 41.976 |
| INTEL | unique | 9.815 | 16.133 | 32.712 |
| INTEL | deep | 10.774 | 52.308 | 70.525 |
| INTEL | far | 10.718 | 28.649 | 46.918 |
| INTEL | textures | 12.593 | 42.935 | 61.327 |

All full-count frame and edit p95 results remain below the initial 16.7 ms / 100 ms
p95 targets. The million-triangle profile, input-to-present feedback, controlled
reference-hardware acceptance and full R082/release qualification remain open.

## Provenance and limits

Measured source: `bbe6e58b786d13bd8301579ae7423e15f9bd697f`. Artifact SHA-256:
`86644f569db8f5c498bdd7ac3acee6015f5de37a61c657623f8415c1d3aa6da0`. The Release build reuses package cache `29fb31b`, with nine captured
overlay files verified against the measured commit; it is not a clean build of
the whole branch. Cache source files and original link flags are restored.

The build uses two compiler jobs, two LTO workers per link, at most eight CPUs,
4 GiB RAM, no additional swap and disk-backed temporary files. Hardware workers
have the same 4 GiB/no-swap limit. The AMD worker uses CPU 11 and X11; the Intel
worker uses eight affinity slots and its own fullscreen Wayland window. Neither
worker wakes displays or changes desktop settings. GPU provider evaluation was
stopped before these measurements and the next model trial waited for both
hardware matrices. These different host/CPU/platform profiles are not a
GPU-only comparison.

[The evidence manifest](R082o-edit-frame-timing.json) records all raw hashes,
source inputs and limits. It retains the raw software logs and all 20 hardware
reports. This layer changes only the benchmark and evidence.
