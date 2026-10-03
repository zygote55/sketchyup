# R011: incremental viewport snapshots and navigation

Date: 2026-10-03. Pending PR merge; M1 still requires durable save and packaging.

Each body now owns a cache of local triangles/edges, transformed world triangles,
and opaque/edge GPU buffers. Immutable body records and composed world matrices
identify what changed. A color/selection edit rebuilds only that body's appearance;
a transform reuses local triangulation; ancestor transforms update descendants;
geometry edits retriangulate the affected body. Removed bodies release their
buffers without uploading survivors. Grid/axis buffers update independently.

Transparent faces still share the depth-sorted buffer because ordering crosses
object boundaries. It uploads when affected transparent geometry/appearance or
the camera changes, not for an unrelated opaque-body edit. Intersecting
transparency remains the documented ordering limitation.

Context cleanup destroys every per-body GPU buffer with its context current.
CPU meshes survive context recreation and are reuploaded without retriangulation.
Renderer exceptions report an unavailable viewport rather than unwinding through
Qt's paint callback. Very small transformed triangles use scale-independent
normalization for shading. Pixel-delta wheel events support smooth zoom without
editing content or uploading body geometry.

`viewport_tests` now verifies:

- Painting one body uploads exactly that body and does not triangulate again.
- Moving one body changes only its world/GPU caches; moving a parent updates
  parent plus descendant and reuses local meshes.
- Adding one body builds only that mesh/cache; deletion releases it and leaves
  survivor uploads unchanged.
- Smooth pixel-delta zoom changes projection without body uploads.
- Context recreation retains CPU meshes while restoring rendered pixels and
  picking. Existing transparency, clipping, DPI and lifecycle checks still run.
- Tiny transformed faces leave the renderer operational.

Observed: native Wayland and X11 suites pass. Physical-output placement passes on
eDP-1 (effective 2.0) and DP-7 (1.6), including six GL context generations. The
suite prints cache counters, checked outputs and effective scales.

```sh
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=wayland timeout 40s build/dev/viewport_tests --screens
QT_QPA_PLATFORM=xcb timeout 40s build/dev/viewport_tests
ctest --preset dev
```

Picking remains a linear triangle scan; scene aggregation still traverses body
caches. These are not claims of accelerated picking or M9 large-model latency.
Physical suspend/resume and hardware trackpad acceptance remain the later device
matrix; this change covers context loss/recreation, hide/show and pixel events.
