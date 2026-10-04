# R023: lines, freehand, rectangles and polygons on drawing planes

Date: 2026-10-03. Local verification passed; PR merge pending.

The Line tool continues from the previous endpoint after pointer movement. Closing
a chain forms the same planar engine face as automation; Escape disconnects the
chain without removing committed edges. Numeric re-entry before starting the next
segment still amends the previous segment. Freehand samples pointer positions,
coalesces coincident consecutive points and commits one bounded polyline. Closing
near the initial screen point acquires that endpoint; modeling tolerance remains
unchanged. Escape discards the sampled stroke.

The Draw menu adds regular polygons and rotated rectangles. Polygons have a center,
radius and 3–256 sides, with `6s` style count entry and amendment. Rotated rectangles
use a first corner, baseline endpoint and perpendicular height; typed dimensions
use the same plane/axis geometry as pointer construction. Circles remain 48-sided
profiles pending the explicit curve records in R024.

Drawing planes can follow the first hovered face or be locked to ground, the
selected face, or a custom origin/normal/horizontal direction. The plane stays
fixed during an operation and camera movement. Typed coordinates must lie on it.
An existing hovered face supplies its body editing context, allowing drawn outlines
to subdivide that face. Detached strokes use new contexts. Automatic inference of
arbitrary loose-edge contexts remains later selection/inference work.

`geometry.rectangle`, `geometry.polygon` and `geometry.polyline` expose the same
construction parameters. Their optional local/world space is explicit. World-space
points convert through the context's inverse scene transform before planar insertion,
so a rotated rectangle retains its world dimensions inside a nonuniformly scaled
body. Plane directions normalize independently of their magnitudes. Polylines are
limited to 512 samples; polygon edges below modeling tolerance reject rather than
silently changing the requested side count. Existing arrangement budgets apply.

Validation:

- Sixteen development CTest suites and eleven ASan/UBSan suites pass, including
  tilted/reversed planes, polygon areas, degenerate axes, bounded samples, atomic
  nonplanar rejection and exact drawing undo.
- Command cases cover every catalog entry, world dimensions under nonuniform
  transforms, invalid space rejection and exact save/reopen.
- Native drawing tests pass on Wayland and pinned Arch/Xvfb: chained/disconnected
  lines, closed/canceled freehand, rotated rectangles, typed/pointer equivalence on
  a tilted plane, polygon count amendment and hovered-face subdivision.
- Numeric/lifecycle/interaction regressions pass. The tilted pointer fixture uses
  a front view so integer mouse coordinates resolve both plane axes; the default
  perspective is nearly edge-on to that particular plane.
- `examples/tilted-drawing.json` runs through the CLI as one atomic batch. The
  [native capture](R023-tilted.png) and [metadata](R023-tilted.json) show its reopened
  rectangle, polygon and sampled open polyline.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/drawing_input_tests
build/dev/sketchyup-cli --script examples/tilted-drawing.json --output /tmp/tilted.sketchyup
```

Freehand preview generation is synchronous and capped; this is not an unbounded
stroke recorder. Screen snapping remains the initial 0.1 m plane grid outside
freehand; indexed inference and directional locks are the next roadmap entries.
