# R082.g — Inference self-occlusion fix and native scene measurements

2026-10-07. Source `88f0a750760eee5966b439b020e5aceceff40759` fixes a real
acquisition failure exposed by the unique-prism fixture. The viewport supplies
float-derived camera matrices. Reprojecting a surface candidate through their
approximate inverse can displace the visibility ray enough for the candidate's
own face to appear in front of it. Visibility now aims from the same eye position
toward the actual candidate. Geometry tolerance and acquisition radius are unchanged.

A minimal regression preserves the captured camera and one affected prism. It
fails before the fix (`a31f7e5`) and passes afterward. Adding a genuine foreground
face still hides that prism and exposes the foreground face. The complete
[normal](R082g-normal-ctest.txt) and [ASan/UBSan](R082g-sanitize-ctest.txt) inference,
constraint and guide suites pass **3/3 each**.

The native benchmark now measures production-index preparation, ten warmup and
fifty cursor queries across the fixture, and one hundred incremental updates.
Queries use the actual viewport camera and eight logical pixels; candidates must
include geometry found by viewport picking. Stationary queries/sync rebuild
nothing. Each transform rebuilds only its placement: one body for unique geometry,
two for a component. Undo/redo restores the cached records without another rebuild.
A final query validates the current post-history state.

All **20 bounded AMD runs pass**: a 25-placement smoke and three 1,000-placement /
100,000-triangle runs for each of five scenarios. Every frozen canonical fixture
hash remains unchanged. [Complete reports, build provenance and environment](R082g-inference.json).

| Scenario | Initial index build | Query p95 | Incremental update p95 | Truncated probes |
| --- | ---: | ---: | ---: | ---: |
| repeated | 81.122–81.551 ms | 0.249–0.293 ms | 0.917–1.007 ms | 61/61 |
| unique | 98.862–104.752 ms | 0.274–0.315 ms | 0.545–0.554 ms | 61/61 |
| deep | 86.313–88.276 ms | 0.242–0.251 ms | 3.604–3.767 ms | 61/61 |
| far | 82.171–84.858 ms | 0.231–0.303 ms | 0.994–1.071 ms | 61/61 |
| textures | 82.180–83.759 ms | 0.228–0.244 ms | 0.919–0.960 ms | 61/61 |

Truncation is the existing bounded-query policy, not an error or an exhaustive
result. Counts include ten warmups, fifty measured queries and the final current-
state query. Early qualification incorrectly rejected legitimate truncation and
expected a redundant post-history rebuild; those benchmark/controller assumptions
were corrected before final qualification. The subsequent empty unique-scene
result reproduced in a focused regression and required the production fix above.

Index timings exclude worker queueing, editor eligibility/context filtering and
displayed feedback. The benchmark queries all editing contexts and holds a separate
index in addition to the viewport's own worker, which increases measured RSS.
Existing GL scene timings still exclude painter overlays and compositor presentation.
These observations do not establish end-to-end inference response or release budgets.

Compilation overlays only the benchmark and changed inference source on the
completed Release package cache; all other runtime, third-party and CMake inputs
match `29fb31b`. Both originals are restored and compared afterward. Package
archives are untouched. Local builds use at most eight CPUs, two compiler jobs,
4 GiB RAM, no extra swap and persistent temporary storage. Each sequential remote
run verifies one CPU, 4 GiB, zero swap and a 300-second deadline on the documented
CachyOS QEMU/AMD system. Guest load is recorded; hypervisor activity is unmeasured.

The separate unsupported 10,000-instance fixture, complete frame feedback, full
memory accounting, current integrated reference measurements and final R082/M9
acceptance remain open. No public limits or performance budgets change here.
