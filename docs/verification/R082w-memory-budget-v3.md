# R082.w v3: memory budget at the R110 unique limits (arithmetic only, not measured)

Version three of the [memory calibration](R082w-memory-budget-v2.md), for the 1.7.0 unique
limits of [ADR 0005](../decisions/0005-document-protocol.md#raised-unique-geometry-limits-r110-170).
Versions one and two stay as they are. Its machine-readable record is
[R082w-memory-budget-v3.json](R082w-memory-budget-v3.json).

**Status: not measured.** The calibration harness did not run inside the 4 GiB container within
the 90-minute limit that R110 set, so this file contains no measured figure. It gives only the
modeled worst case, computed from v2's table, and the measurement that is still needed.

## Why the harness did not run

The harness is `real_model_benchmark` with `inference_index_benchmark`. v2 measured them with
software rendering (llvmpipe) in `sketchyup-native-fonts-check:20261008`, with one CPU and 4 GiB.
`real_model_benchmark` links the whole desktop library. That image has no build tree, so the
Release build would start from source. On this host, the normal fast-tree build of the same
sources (1,211 steps at `-j2`) reached 116 steps in about 13 minutes, and its desktop sources
were the slow part. A one-CPU container would take far longer than 90 minutes. R110 stopped
there, as its brief directs.

## What was measured, and what that means

Only one measurement exists for the new limits, and it is not calibration. The local normal
build (Debug with `-g0` and shared libraries, not Release, not llvmpipe, not one CPU in a
container) built a model of 300,000 unique vertices (150 prisms of 1,000 sides) and reached a peak
RSS of 985 MiB in `unique_limits_acceptance_tests`. Its document chunk is 35.0 MiB. It is a
single data point, and it does not replace the software figures that v2 uses.

## Modeled worst case at the new limits

The worst case is v2's table ([R082w-memory-budget-v2.md](R082w-memory-budget-v2.md#modeled-worst-case))
with one term changed: the unique-vertex inference index (917 bytes per unique vertex) rises from
100,000 to 300,000 vertices. Nothing else in the table depends on the unique limits, because the
materialized terms are unchanged.

| Term | v2 (bytes) | v3 (bytes) |
| --- | ---: | ---: |
| Base, including 1,000 shared placements (repeated 1k, five cycles) | 288,276,480 | 288,276,480 |
| 19,000 more placements × 36,138 B | 686,622,000 | 686,622,000 |
| 600,000 materialized vertices × 2,542 B unshared viewport payload | 1,525,200,000 | 1,525,200,000 |
| Unique vertices × 917 B inference index (100,000 in v2, 300,000 in v3) | 91,700,000 | 275,100,000 |
| Maximum texture fixture (textures 1k − repeated 1k) | 273,436,672 | 273,436,672 |
| **Total** | **2,865,235,152 (2.67 GiB)** | **3,048,635,152 (2.84 GiB)** |

The ceiling is 3 GiB (3,221,225,472 bytes). The v3 total is 172,590,320 bytes below it. This is
arithmetic only. It assumes that no per-unique-vertex cost beyond the inference index is missing
from v2's terms. No measurement has shown that, and so the result is provisional.

## Measurements still needed

1. The unique-geometry family at about 300,000 unique vertices with no placements: peak RSS, load
   and save time, frame time if the harness reports it, and the inference-index build and query time.
2. The v2 unique 100k case on the same build, for comparison.
3. Any per-unique-vertex cost that the measurements show and v2's materialized terms do not cover.
   It is added to the worst case. If the total goes over 3 GiB, the numbers are reported and no
   other limit changes.

The smallest measurement that would stand in for the full harness is item 1 and item 2, run once
with `inference_index_benchmark` and the unique family of `real_model_benchmark` in the v2 image,
with the same software rendering, one CPU and 4 GiB. The desktop link is the cost to weigh. If the
link is too slow, the parent can decide between a Release build of the whole tree and a core-only
probe of the document memory, which would not measure the viewport or inference terms.

Hardware acceptance stays under the [ADR 0161 waiver](../decisions/0161-m9-hardware-evidence-waiver.md),
as it did for v2. The fresh Intel and AMD reruns in [M9_HARDWARE_SESSION.md](../M9_HARDWARE_SESSION.md),
section 1, remain required before any ceiling is accepted.
