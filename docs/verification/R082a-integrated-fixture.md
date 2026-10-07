# R082.a — Integrated performance fixture

2026-10-07. Integration `7ffaab9`; fixture source `b4652b9`.
Parent: [PR #202](https://github.com/zygote55/sketchyup/pull/202).
This is overlapping benchmark infrastructure under the roadmap rule, not R082 or M9 acceptance.

The desktop, benchmark and viewport-regression targets build. A deliberately small
[Debug/software integration run](R082a-integration-smoke.json) passes with **25
instances / 2,500 triangles**, including exact 1920 × 1080 framebuffer size,
geometry-hit checks, stable stationary mesh/upload caches and 100 edit/undo/redo
operations. It is a functional smoke fixture; its timing is not compared against
release budgets or substituted for the required 100k/1M workloads.

The actual Wayland 2× viewport regression also passes: concavity/holes, topology IDs,
clipping, occlusion, depth, transparency, cache reuse, context recreation, hide/show,
resize and pixel-delta zoom. Timing instrumentation is opt-in; normal viewport work
retains its existing rendering path.

The earlier [Release exploratory baseline](R082a-real-model-baseline.md) retains
its exact sources, fixture hash, integrated/software hardware results and caveats.
The requested 1M-triangle fixture still rejects at the existing component-placement
limit. No bound or acceptance budget has been raised. Larger-document/instancing
work, controlled reference measurements, complete timing/memory accounting and
required discrete hardware remain open.

The [installed smoke](R082a-final-installed-smoke.json) verifies the desktop/CLI,
ten catalogs and forty-six contracts plus existing exchange workflows. The
[source package](R082a-final-source-package.json) contains **200 byte-exact installed
inputs**, SHA-256 `e7e5f5220f476f339856eeaa96c3ce32062dd993b40a21e681e31b5db5a3093d`.
Complete remote CI, ordered merges and prior milestone acceptance remain required.
