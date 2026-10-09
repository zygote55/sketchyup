# R087.c — Bound local verification resources

2026-10-07. Earlier local acceptance work placed multi-gigabyte native binaries in
the host's RAM-backed `/tmp` and ran concurrent builds without container resource
limits. The project owner reported laptop crashes. The earlier observation was
about 14 GiB used on a 16 GiB tmpfs; after the interruption it was already down to
21 MiB. This record does not claim that cleanup by the agent caused that reduction,
or establish a crash root cause without system evidence.

The owned jobs were stopped. The old graphics/package containers are retired;
they must not be restarted with their old mounts or unbounded settings. Missing
temporary captures are not described as retained, while persistent source,
verification logs, manifests and already archived binaries remain on disk.
The interrupted fresh package attempt is not accepted as a completed clean build.

`scripts/run-container-check.sh` is the local verification entry point. Its shared
worktree lock and active-container check permit one job. The kernel enforces one
CPU including affinity, 4 GiB memory, no additional swap and 256 processes. Build
parallelism is one, and a preflight requires 6 GiB available host memory. Checkout
input is read-only; capture/config/data/cache/package files and container `/tmp`
use persistent project storage. Memory-backed paths and host `/tmp` are refused.
The runner never pulls an image, overwrites a run directory or enables restart.
No desktop or system-wide settings change.

[Runtime proof](R087c-bounded-runner.json) verifies actual cgroup memory/swap limits,
one visible CPU, disk-backed container `/tmp`, lock refusal, forbidden temporary
package storage, existing-run refusal, success/failure exit propagation and TERM
cleanup of the owned container. No stress-to-OOM test was run. Host `/tmp` remained
at 21 MiB through these lightweight checks. After an uncatchable interruption,
a still-running labeled job blocks a second invocation until inspected/stopped.

The first bounded package retry exposed a separate nested-worker issue: Ninja ran
with `-j1`, but GCC's inherited `-flto=auto` invoked `make -j22` despite one-CPU
affinity. The cgroup CPU/memory ceilings remained in force (observed 1.3 GiB of
4 GiB), and host `/tmp` was 22 MiB. That attempt was stopped deliberately before
testing; exit 143 is not a package pass or a product crash. Its persistent logs
remain under `build/local-checks/r087d-package-retry`.

PKGBUILD now explicitly appends `-flto=1` to executable/shared link flags when LTO
is enabled, preserving compile flags, optimization, and reusable build objects.
This follows [GCC's documented explicit link worker count](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-flto).
CPU affinity alone is not evidence that every tool will create one worker.
A [13-translation-unit compiler probe](R087c-serial-lto.json) executes the actual
PKGBUILD build function with forced maximum partitioning and inherited automatic
compile LTO. GCC explicitly reports serial compilation of 13 LTRANS jobs; the
linked program runs successfully. This lightweight probe passed under the same
container ceilings and does not substitute for the package lifecycle check.

Package defaults also become one outer build job. The lifecycle procedure uses this
bounded runner. These are local safety constraints, not reference-performance
measurements or release acceptance. The next package attempt must retain its
actual source, cache/interruption provenance and test results separately.
