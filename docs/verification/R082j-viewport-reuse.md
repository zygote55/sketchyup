# R082.j — Reuse unchanged viewport state

2026-10-07. All **40 hardware runs pass**, twenty on AMD and twenty on Intel.
Every measured 1,000-placement / 100,000-triangle run is within the initial
16.7 ms complete-frame and 100 ms edit/readback targets. The largest observed
p95 values are 2.619 / 32.186 ms on AMD and 16.528 / 70.048 ms on Intel.
This qualifies these measurements, not the whole performance or release gate.
[Raw reports, source/artifact hashes and limits](R082j-viewport-reuse.json).

## Changes and comparison

Immutable body identity avoids repeated topology and appearance comparisons.
Referenced global material records are still refreshed even when a body record is
unchanged, so material-only edits invalidate the cache. Empty asset and section
sets skip scans; an empty texture request still retires old CPU/GPU images.
Bodies without guides skip guide hierarchy traversal. Five shader uniforms shared
by all batches are set once at each primary shader binding, including selection.
Geometry, tolerances, scene hashes and supported document limits are unchanged.

The table compares medians of three full-size runs with R082.i. All figures are
milliseconds; each input is a run's p95, not a pooled percentile or confidence
interval. Both experiments use the same five frozen canonical scene hashes,
1920×1080 framebuffers, ten warmups, fifty frame/pick samples and one hundred
edit/undo/redo operations per run. Each family also has one 25-placement smoke.

| Machine | Scenario | Complete viewport p95, before → after | Edit/readback p95, before → after |
| --- | --- | ---: | ---: |
| AMD | repeated | 3.033 → 2.210 | 18.642 → 16.239 |
| AMD | unique | 2.658 → 2.152 | 16.552 → 17.215 |
| AMD | deep | 6.316 → 2.187 | 56.908 → 19.478 |
| AMD | far | 2.973 → 2.191 | 20.334 → 31.774 |
| AMD | textures | 2.937 → 2.179 | 18.808 → 16.077 |
| Intel | repeated | 16.137 → 13.065 | 68.199 → 47.487 |
| Intel | unique | 15.384 → 11.992 | 54.544 → 36.472 |
| Intel | deep | 26.941 → 12.814 | 173.078 → 68.731 |
| Intel | far | 16.087 → 14.311 | 75.673 → 48.224 |
| Intel | textures | 16.672 → 15.686 | 72.774 → 63.390 |

AMD's distant-coordinate edit median increased from 20.334 to 31.774 ms, and
unique-geometry edits increased slightly. These remain within the initial target;
the measurements do not establish that every operation improved. Intel's deeply
nested scene shows the largest reduction. Complete-frame timing includes painter
overlays and GPU completion, but excludes child widgets and compositor presentation.
Edit timing additionally includes framebuffer readback. Inference assertions and
zero-GL-error checks pass; the existing bounded-alternative truncation policy remains.

## Regression validation and provenance

There are **42 passing native checks**: eighteen Release checks cover viewport,
materials, textures, sections, styles, solar, selection, raster export and reference
images at Wayland scales 1 and 2. Four final Release checks cover viewport and guides
at both scales after the empty-guide shortcut. Twenty ASan/UBSan checks repeat all
ten fixtures at both scales on the final runtime. Native sanitizer leak detection
is disabled for the documented Wayland library issue; this is not leak qualification.
The linked machine-readable report identifies the three retained logs and their hashes.

The eighteen-check build uses source `a8c24a8`; the final four checks, all sanitizer
checks and all forty hardware measurements use runtime `cd1950e`. The later physical
output fixture changes only test reporting. Release builds overlay the four listed
viewport files and the earlier inference fix on the `29fb31b` package source cache;
the captured manifest verifies all four optimized viewport hashes. Sources and link
flags are restored afterward. The sanitizer build overlays these five runtime files
on the compatible existing native sanitizer cache and restores them afterward.
Package archives are untouched. This is cached-build evidence, not a clean rebuild.

Local work is serialized with at most eight CPUs, two compiler jobs, 4 GiB RAM,
zero extra swap and disk-backed temporary storage. The initial eighteen-check build
uses serial LTO per link; the final Release build uses two LTO workers per link,
still inside those CPU and memory bounds. AMD measurements pin one guest CPU;
Intel measurements allow eight distinct local cores. Both use 4 GiB and zero swap.
The environments and measurement caveats from R082.i still apply; this is not a
controlled comparison of GPU hardware alone. Displays were already awake and only
the benchmark's own window was made fullscreen.

The private million-triangle experiment does not widen production limits or qualify
10,000-instance support. Its first attempted optimized build omitted the four
viewport overlays; that run is excluded from optimized evidence and retained as a
diagnostic; the corrected private experiment is separate from this qualification.
Full memory accounting, large-scene
coverage, long sessions, cold native UI persistence, end-to-end input feedback and
reference/release acceptance remain open.
