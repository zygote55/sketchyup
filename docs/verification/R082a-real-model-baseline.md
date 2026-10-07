# R082.a — Exploratory real-model performance baseline

2026-10-07. Benchmark source `b4652b9`; [contract](../decisions/0141-real-model-performance-fixtures.md).
This fixture/profiling spike overlaps M8 work under the roadmap rule. It does not
accept M9 or close R082. No performance budget has been changed.

The Release benchmark passes geometry-hit, stationary mesh/upload reuse, exact
1920 × 1080 framebuffer, 100-edit/undo/redo history and body-cache capacity checks.
The canonical 1,000-instance / 100,000-triangle fixture hash is
`c874449f6143db01040c1b0df5f26dbc0f90daee66cd2635453eeac73c11376d`.

| Exploratory run | Scene GPU-complete p95 | Pick p95 | Edit + framebuffer readback p95 | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| [Intel MTL native Wayland](R082a-intel-final-1000.json) | 10.168 ms | 0.838 ms | 88.182 ms | 491,077,632 B |
| [llvmpipe software X11](R082a-software-1000.json) | 83.167 ms | 1.057 ms | 151.145 ms | 631,975,936 B |

The native run used [this host](R082a-local-hardware.json) at fractional scale 1.6.
The compositor clipped part of the host window; the full child framebuffer was
rendered and read back at the asserted physical dimensions. Both runs used 50 frames
after 10 warmups. Body vector capacities were 101,456,000 B, GPU vertex payload
34,656,000 B and retained history 326,500 B. These are explicitly partial cache
accounting, separate from process RSS. Software results used the earlier `e6ea11d`
measurement implementation; native `b4652b9` added strict physical-size checks.
An earlier native run at 1890 × 2080 was discarded from this table.

Another worktree was compiling during these exploratory measurements. Scene timing
excludes painter overlays and compositor presentation, and edit timing includes
readback. Consequently even numbers below the initial budgets are not release
acceptance. Repeated controlled-load reference runs, discrete hardware, the larger
fixture and the remaining performance scenarios are still required.
