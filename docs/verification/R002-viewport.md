# R002.b: viewport rendering and lifecycle evidence

Date: 2026-10-03. Status: implementation and local verification complete; pending
PR review/merge. Parent R002 and M0 are not marked Verified by this slice.

## Changes and regressions covered

- Separate persistent GPU buffers hold opaque faces, edges, transparent faces and
  benchmark geometry. Ordinary camera navigation no longer uploads opaque geometry
  every frame. Document identity/revision and view changes invalidate the right
  caches; picking sees edits even before a queued repaint.
- GPU resources are explicitly released with their context current, both on
  `aboutToBeDestroyed` and destruction, then rebuilt by `initializeGL`. The test
  reparents the viewport between top-level windows, confirms the old context was
  destroyed, and checks both rendered pixels and picking in the new context.
  This follows [Qt's context lifecycle guidance](https://doc.qt.io/qt-6/qopenglwidget.html#resource-initialization-and-cleanup).
- The adapter supports view-only body opacity and one clipping plane. Transparent
  layers are sorted back to front; opaque geometry writes depth first. Independent
  alpha blend factors preserve the opaque framebuffer alpha required by Qt's
  composition. A pixel-level test caught the initial alpha-blending defect.
- The same clipping half-space applies to drawing and ray hits. Removed fragments
  cannot intercept selection. Fully invisible bodies are not picked; visible
  transparent faces remain selectable. None of these view operations edits or
  saves materials, geometry or history.
- New `viewport_tests` compares actual framebuffer colors for depth occlusion,
  opaque/transparent blending, reversed insertion order, and clipping. It also
  verifies stationary buffer reuse, camera-only transparency updates, invalid
  plane rejection, document immutability, hide/show, context recreation, logical
  resize, and edits/opacity changes before repaint. CI runs it with software GL.

These are renderer feasibility APIs, not finished material/section UI. Intersecting
transparent surfaces need a more general ordering solution. Clipping has no caps
or section export. Edges remain opaque. Changes still rebuild a whole document's
mesh; per-body incremental caches and accelerated picking remain later work.

## Reproduction

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev
QT_QPA_PLATFORM=wayland build/dev/viewport_tests
QT_QPA_PLATFORM=xcb build/dev/viewport_tests
QT_QPA_PLATFORM=wayland build/dev/interaction_tests
# Optional hardware check: briefly shows a test window fullscreen on each output.
QT_QPA_PLATFORM=wayland build/dev/viewport_tests --screens
QT_QPA_PLATFORM=wayland build/dev/sketchyup --benchmark 100000
QT_QPA_PLATFORM=wayland build/dev/sketchyup --benchmark 1000000
# The original instancing experiment remains available for comparison.
QT_QPA_PLATFORM=wayland build/dev/sketchyup --benchmark 1000000 --instanced
```

## Observed host results

Same host/toolchain as [the initial native spike](native-spike.md): Intel Arc MTL,
Mesa 26.2.2, Qt 6.11.2, native Wayland, Omarchy. All core/persistence, existing
interaction and new viewport tests pass. Viewport tests also pass on X11/XWayland.
No OpenGL errors were reported.

- Effective Wayland scales 1, 1.5, 1.6 and 2 passed the pixel/picking/lifecycle suite.
  `QT_SCALE_FACTOR=0.625/0.9375/1.25` exercised 1/1.5/2 on the native 1.6-scale output.
- Physical-output test passed on **eDP-1 at 2.0** and **DP-7 at 1.6**, with six GL
  context generations across the complete run. Each transition checked the actual
  Qt window output, effective window scale, pixels and picking. Output selection
  must happen after adding the GL widget: adding it can recreate the native window.
  No monitor configuration or window rules were modified. This is controlled
  reparent/fullscreen placement, not a manual window-drag acceptance test.
- Logical viewport widths 640, 900, 1200 and 1600 retained picking alignment.
- Final independent-triangle runs: **100,000: 1.13 ms**, **1,000,000: 7.24 ms** mean
  per GPU-complete draw. The million-triangle buffer uploads about 84 MB once;
  subsequent frames reuse it. A comparative million-instance run measured 2.30 ms.
  Five fresh rendered samples warm up the path, followed by 20 fresh samples;
  the sampler does not count repeated timer reads of an unchanged frame.

Timings cover colored triangle submission plus `glFinish`, excluding compositor
presentation, geometry generation, initial upload, edge rendering and document
editing. The independent triangles form a synthetic height-varying grid. These
results are stronger than the original single-triangle instancing measurement,
but do not close the full-document performance budget or M9 hardware matrix.

## Remaining R002 acceptance

Native Open/Save dialog interaction and pointer capture across window boundaries
still need desktop acceptance evidence (R002.c). End-to-end manual movement
between monitors, additional drivers and sleep/resume remain platform follow-ups.
The chosen adapter has passed the rendering experiments here; R002 and M0 retain
their parent gate requirements and review/merge rules.

## Prototype retirement

At the owner's request, the untracked Electron/Three.js application was removed
from the original checkout: `desktop/`, `dist/`, `package.json`, old vendor script,
JavaScript test, Electron workflow and ignore file. The old README was replaced
by the native branch README. No prototype runtime was copied into the native app.
The main workspace now checks out the native follow-up branch. Supplied design
mockups match the committed versions; the earlier UX text is preserved in a
local Git stash named `Preserve original supplied UX design before retiring Formline`.
The later Formline migration scope remains planned using original synthetic
fixtures; removing the old runtime does not remove that scope item.
