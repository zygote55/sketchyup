# R082.e — Unique, nested and distant model scenarios

2026-10-07. Benchmark source `b260fe3a42443b3c1755cdd42c6b62233ff9477c`
adds three explicitly defined scene families to the [fixture contract](../decisions/0141-real-model-performance-fixtures.md).
The original repeated fixture remains the default and retains canonical hash
`c874449f6143db01040c1b0df5f26dbc0f90daee66cd2635453eeac73c11376d` in the
[control run](R082e-amd-repeated-1000-1.json).

All **13 native AMD runs pass**: the repeated control, then a 25-placement smoke
and three 1,000-placement / 100,000-triangle runs for each new scenario. Every
report checks actual geometry hits, complete unclipped 1920 × 1080 framebuffer,
stationary cache/upload reuse, 100 edits with undo/redo, fixed body-cache capacity
and zero final GL error. Repeated full runs have identical per-scenario hashes.
[Complete artifact, environment and result manifest](R082e-performance-scenarios.json).

| Scenario | Actual content | Scene p95 range | Pick p95 range | Edit + readback p95 range |
| --- | --- | ---: | ---: | ---: |
| Unique | 1,000 independent prisms with distinct radii; no component bindings | 2.171–2.578 ms | 0.044–0.047 ms | 17.105–18.051 ms |
| Deep | Original repeated scene inside 32 enclosing groups | 2.379–2.491 ms | 0.156–0.179 ms | 56.038–56.662 ms |
| Far | Original repeated scene offset `(900000, -900000, 900000)` metres | 2.195–2.486 ms | 0.061–0.078 ms | 19.719–24.446 ms |

Full-run peak RSS is 444,026,880–452,448,256 bytes across the new scenarios.
The deeper hierarchy increases edit cost in these observations; it does not cause
an incorrect pick, lost history or growing body-cache capacity. These are
exploratory observations under the retained timing scope, not full frame feedback
or release-budget acceptance.

The executable is built from the completed package cache with a single benchmark
source overlay. All runtime, third-party and CMake inputs are unchanged from
`29fb31b`; the benchmark source hash is recorded, and the original cached source
is restored and compared afterward. Existing package artifacts are untouched.
The build uses the eight-CPU ceiling, two jobs, 4 GiB memory and persistent
temporary files. No second heavy local job runs concurrently.

The same CachyOS QEMU guest / AMD Navi 21 / Mesa 26.3.0-devel / Qt 6.11.2 system
is used, with the earlier board-name ambiguity retained. Each sequential remote
run verifies one CPU and affinity, 4 GiB memory, zero extra swap, 128 processes
and a 300-second deadline. Display-awake and accelerated-GL preflight succeeds.
Guest load is recorded; hypervisor activity remains unmeasured. No display wake,
desktop configuration change or package installation is required.

No public limits, initial budgets or existing fixture sizes are changed. The
unsupported 10,000-instance / 1,000,000-triangle fixture remains open. Large
textures, inference timing, complete frame feedback, full cache accounting,
current integrated-GPU reference runs and final release acceptance remain open.
