# R085.g — Current-source AMD GPU measurements

2026-10-07. The Release benchmark from source
`29fb31bba53e0b6dd2c0d5a73ffbc7fa1b44903e` passes a 25-instance smoke and three
1,000-instance / 100,000-triangle runs on the available CachyOS AMD system.
Unlike [R085.f](R085f-discrete-gpu.md), this executable includes the subsequent
component and file-worker changes. Its stripped executable SHA-256 is
`7a3a73da0e6d71eaf977ea62a391a44cb1f3d3fc28489184d5d5c9492deed5ff`;
the transferred bytes match. [Build/environment record](R085g-current-discrete-gpu.json).

All full runs reproduce canonical fixture SHA-256
`c874449f6143db01040c1b0df5f26dbc0f90daee66cd2635453eeac73c11376d`.
The complete 1920 × 1080 framebuffer is unclipped. Geometry-hit picking,
stationary mesh/upload reuse, 100 edits with undo/redo, stable body-cache capacity
and final zero GL error pass. [Smoke report](R085g-amd-smoke.json).

| Run | Scene GPU-complete p95 | Pick p95 | Edit + readback p95 | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| [1](R085g-amd-1000-1.json) | 2.386 ms | 0.064 ms | 19.329 ms | 451,842,048 B |
| [2](R085g-amd-1000-2.json) | 2.302 ms | 0.060 ms | 18.228 ms | 451,776,512 B |
| [3](R085g-amd-1000-3.json) | 2.220 ms | 0.054 ms | 18.663 ms | 451,792,896 B |

Preflight again confirms accelerated AMD Navi 21 OpenGL, Mesa 26.3.0-devel
`8ea6e9dd41`, Qt 6.11.2 and running CachyOS kernel 7.2.7. The
[earlier hardware inventory](R085f-discrete-hardware.json) records the QEMU guest,
Ryzen 9 9900X presentation, 16 GiB VRAM, and conflicting RX 6900 XT PCI subsystem
versus RX 6800 XT renderer names. No more precise board identity is inferred.
Guest load averages were 0.00/0.00/0.00 at preflight; hypervisor load remains
unmeasured. The display was awake; no desktop configuration or wake was needed.

The benchmark was explicitly built from the completed package cache under the
owner-authorized eight-CPU ceiling, two build jobs and 4 GiB limit. Measurements
retain the earlier remote conditions: one CPU quota and affinity, 4 GiB memory,
zero additional swap, 128 processes and a five-minute deadline per run. Actual
kernel limits were verified inside each worker. Temporary/cache/evidence files
stay on persistent disk. All four transient remote services finish with exit zero.

These remain exploratory measurements. Scene timing includes `glFinish` and
excludes painter overlays/compositor presentation; edit timing includes readback.
The earlier baseline uses a different build/toolchain configuration, so the lower
edit measurements do not establish a controlled optimization speedup. Partial
body-cache accounting is not total GPU memory. The known unsupported 10,000-instance
fixture was not rerun or replaced. Full fixture coverage, integrated current-source
reference runs, memory budgets, platform acceptance, CI and ordered merges remain
open. No release acceptance is claimed.
