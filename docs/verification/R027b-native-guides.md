# R027.b: native tape, protractor and guide inference

Date: 2026-10-03. Local validation passed; CI/merge pending.

Tape starts from an edge/guide line to construct a signed parallel offset, or
from a point to measure a distance and create a guide point. Protractor uses a
center, baseline endpoint and angle point; numeric angles default to degrees and
accept radians. Both use the active drawing plane, world-space inference locks,
private command previews and the existing guarded amendment path. Numeric
re-entry replaces the last guide without adding undo steps. A standalone Ctrl
release toggles creation versus measurement only, also exposed as a checked Draw
menu action. Ctrl shortcuts do not toggle the mode. Measurement-only distance
and angle operations publish no document changes and preserve history/dirty state.

Guides render as dotted overlays through model faces. Pure guide snapping follows
that visibility convention; mixed guide/model intersections still honor model
occlusion. Infinite lines are bounded only by the editable world cube for indexing
and viewport clipping, with no artificial endpoints or midpoints. Acquisition
projects the original world line analytically, avoiding near/far clipping roundoff.
True 3D intersections share the existing modeling tolerance. Coincident results
retain both typed sources rather than collapsing guide/model namespace collisions.
Direction references carry source types and transform guide directions to world
space. Hiding guides excludes them from acquisition and clears armed references.

Starting on a guide over a face in the same editing context retains that face's
plane. Edit → Delete all guides uses one undoable cleanup and preserves modeled
geometry and editing contexts. Saved guides are immediately eligible for indexing
after reopening; this slice makes no additional persistence schema change.

Validation:

- All 20 development suites and 15 ASan/UBSan suites pass.
- Core fixtures cover logical-pixel acquisition across zooms, guide points,
  guide/guide and guide/model intersections with typed identity, false projected
  crossings, perspective accuracy, visibility and hidden-guide filtering.
- Direction fixtures cover mirrored/nonuniform world transforms and distinct
  edge/guide lock identities. CLI tests cover guide references, visibility and
  strict boolean validation.
- Native sill fixture passed on Wayland at DPR 1.6 and pinned Xvfb/Mesa at DPR 1
  and 2: create 900 mm sill, revise to 120 cm and back, undo once, reuse the guide
  for a 400 × 300 mm rectangle, and confirm only drawn model edges form faces.
- The same fixture verifies pointer-only tape/protractor phases, a 45-degree
  wall protractor, standalone Ctrl versus Ctrl-shortcut behavior, measurement-only 3 m
  and −90-degree inputs, an 18-inch Z-locked point, save/reopen acquisition,
  framebuffer visibility, cleanup/undo and no OpenGL errors.

- The full Xvfb native regression set passes: direction constraints, inference,
  circles/arcs, line/rectangle/freehand drawing, numeric input, tool lifecycle,
  interaction, shell, dialogs and topology viewport. The application smoke check
  passes with no OpenGL errors. The final guide fixture passes again on Wayland
  and both software-rendering scales after the shortcut and pointer-phase checks.

[Wayland guide capture](R027b-guides-wayland.png): the 0.9 m sill, 45-degree line
and 18-inch guide point are visually distinct from the wall's model edges. The
capture was inspected by the implementation agent; no independent human review
or additional physical display coverage is claimed.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/guide_input_tests
# CI also runs guide_input_tests with Xvfb/Mesa at logical scales 1 and 2.
```
