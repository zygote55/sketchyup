# SketchyUp

An independent native Linux 3D modeler in development. C++20, Qt 6 and OpenGL;
ordinary editing runs locally without an account or browser runtime. Own code
is MIT licensed. Arch/Omarchy is the primary development environment.

## Current build

**Working native editor foundation, not a complete editor or release.** Draw lines,
freehand strokes, rectangles, polygons, circles, arcs and pie sectors on explicit or hovered planes, select
faces, push/pull planar regions, translate and
color objects, undo/redo, and save/reopen native `.sketchyup` files. Faces retain
editable loops and holes. A headless driver uses the same core operations.

The [108-entry roadmap](docs/PR_ROADMAP.md) has passed its M0 feasibility and
M1 native foundation and M2 editable geometry gates; drawing interaction is in progress. Components,
recovery, AI providers,
Blender integration and exchange formats are not implemented. Curves retain analytic parameters alongside configurable segmented editing geometry. Surface topology and file format remain experimental.
Save explicitly: there is no autosave or recovery journal. Explicit saves use a
checksummed container and preserve the previous valid file as `.sketchyup.bak`.
Files and the containing directory are synced before showing Saved. Open a backup
through the file dialog's all-files filter or the CLI if you need the previous save. Light, Dark and System
themes are available in View. The experimental scene records support nested/mirrored
transforms through the headless command API; component editing UI remains planned.

## Build and run

Requires CMake 3.25+, Ninja, a C++20 compiler, Qt 6.8+ base/Wayland development
packages and OpenGL 3.3. Verified locally with Qt 6.11.2 on Wayland and X11.
The pinned geometry dependency is included in `third_party/`; CMake does not
download code. See [measured evidence and limitations](docs/verification/native-spike.md).

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev
./build/dev/sketchyup --demo
```

On Arch, build dependencies are `cmake ninja gcc qt6-base qt6-wayland`.
For the core alone, with no Qt or graphical session:

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
```

## Modeling

- `L`: line. Click–move–click or press–drag–release; movement continues a chain.
  Escape disconnects it while keeping committed edges. Closed chains form faces.
- `R`: rectangle. Click the first corner, then the second; or enter `width, depth`
  in Measurements and press Enter. Values default to meters; `mm`, `cm`, `ft`, `in`,
  feet/inches and fractions override units. Unconstrained grid fallback is 0.1 m; acquired directions use continuous coordinates.
- Hover a point or edge for 450 ms to arm a reference. Move away for axis,
  parallel, perpendicular, analytic tangent, or from-point alignment. `Tab`
  cycles point and direction alternatives. `Shift` holds the current inference;
  release it to unlock. Right/Left/Up toggle world X/Y/Z locks; Down cycles
  parallel/perpendicular/off for the reference edge. Planar shapes reject axes
  outside their construction plane; Line can follow Z out of the starting plane.
  Numeric lengths honor locks. Focus transfer releases held Shift; arrow locks
  persist until toggled, committed, canceled, or the document changes.
- `C`: circle. Click center, then radius; or enter the radius. `24s` sets segments.
- Draw menu: Freehand, Regular polygon (`6s` sets sides), and Rotated rectangle
  (first corner, baseline endpoint, height).
- `A`: center arc. Draw → Center arc / Pie: center, radius point, end direction; or enter
  `radius, angle` such as `2m,90deg`. With a radius point set, a single value is
  the angle. Angles default to degrees; `rad` is supported. Pointer sweeps go
  counterclockwise around the plane normal; type a negative angle to reverse.
- Draw → Two-point arc: endpoints, then bulge point; or type a signed bulge.
  Three-point arc: start, a point on the arc, then end. Coordinates work at each
  phase. `24s` changes segmentation for all curves, including immediate re-entry.
- Draw → Drawing plane: automatic from the first hovered face, ground, selected
  face or custom origin/normal/horizontal direction. Drawing on an existing face
  subdivides its editing context.
- `Space`: select. `P`: select a face, move to preview, then click/drag to finish or enter a signed distance along its local normal.
- `M`: translate selected object numerically. `B`: change its color. `Delete`: remove it.
- Point inference acquires endpoints, midpoints, curve centers, intersections,
  edges and faces within 8 logical pixels. Marker shapes and labels identify the
  result; `Tab` cycles nearby alternatives. Drawing from loose geometry adopts
  its context. Locked planes exclude off-plane points. Index preparation runs in
  the background; the 0.1 m grid remains the fallback when no candidate is acquired.
- Middle drag or `O`: orbit. Right drag or `H`: pan. Wheel: zoom. `Shift+Z`: fit.
- `1/2/3` with Select active: perspective/top/front. `Escape`: cancel drawing.
- `Ctrl+Z` / `Ctrl+Shift+Z`: undo/redo. `Ctrl+K`: commands, objects and recent files.
- `F6` / `Shift+F6`: move between window regions. `Ctrl+Shift+T`: toggle the Model panel.
- `Ctrl+O` / `Ctrl+S`: native open/save dialogs. Unsaved changes prompt before replacement or close.

With a drawing tool active, typing a digit or `[` sends input to Measurements.
`[x,y,z]` sets an absolute point; `<x,y,z>` sets a relative point. Comma-decimal
locales use semicolons between dimensions/coordinates. The selected drawing plane constrains the point. Entering another value immediately after completion revises that operation
as one undo item; an intervening edit or undo/redo invalidates re-entry. Escape in
Measurements returns focus to the viewport. Invalid input remains selected and
marked with an explanation.

## Headless commands

```sh
./build/dev/sketchyup-cli --capabilities
./build/dev/sketchyup-cli --describe-command geometry.translate
./build/dev/sketchyup-cli --script examples/room-shell.json --output /tmp/room.sketchyup
./build/dev/sketchyup-cli --input /tmp/room.sketchyup
./build/dev/sketchyup /tmp/room.sketchyup
./build/dev/sketchyup-cli --input /tmp/room.sketchyup --query geometry.inspect --context 1
./build/dev/sketchyup-cli --script examples/split-wire.json --output /tmp/wire.sketchyup
./build/dev/sketchyup-cli --script examples/planar-grid.json --output /tmp/grid.sketchyup
./build/dev/sketchyup-cli --preview --script examples/through-opening.json
./build/dev/sketchyup-cli --script examples/tilted-drawing.json --output /tmp/tilted.sketchyup
./build/dev/sketchyup-cli --script examples/curves.json --output /tmp/curves.sketchyup
./build/dev/sketchyup-cli --script examples/guides.json --output /tmp/guides.sketchyup
./build/dev/sketchyup-cli --input /tmp/curves.sketchyup --query-file examples/inference-query.json
```

Guide points and infinite guide lines are available through `guide.point`,
`guide.line`, `guide.angle`, `guide.offset`, `guide.erase` and `guide.clear`.
Guides remain separate from faces/edges. `guide.clear` with body `"0"` removes
all guides in one undo step; a body ID scopes cleanup. Creation accepts local or
world coordinates, angles use radians, and offsets use meters in the selected
space. `geometry.measure_distance` and `geometry.measure_angle` are read-only
queries for world-coordinate points. Native guide display and tape/protractor
interaction are the next R027 slice. The current file schema is version 5;
versions 1–4 migrate without adding guide data.

The topology query exposes stable context-scoped edges, oriented loops and radial
adjacency, plus analytic curve parameters and ordered derived-edge references.
Curve command angles use radians; segment counts are bounded to 256. Curve
metadata survives edge splits and retires when an edit breaks its outline. The wire recipe demonstrates a loose edge and a propagated edge split;
these operations currently have command paths while direct edge tools are in progress.
The planar-grid recipe forms four editable faces from finite segments. Planar insertion
handles intersections, overlaps and holes within the documented arrangement limits.
`geometry.push_pull` supports isolated profiles, complete prism caps, recessed face
regions and push-to-opposite-face openings. Unsupported intersections reject before
committing. Use `--preview --script recipe.json` to inspect prospective geometry and
lineage without changing the input document; preview cannot save an output file.

A script is a local JSON array, validated and committed as one batch. The in-process
API checks document identity and revision; failed batches change nothing. This is
an experimental local driver, not yet the durable AI/MCP protocol. The example's
IDs are specific to its empty-document fixture. Query live IDs before editing an
existing document; do not reuse them across documents.

## Viewport verification

The [viewport follow-up](docs/verification/R002-viewport.md) records native pixel,
picking, GPU-cache and context-recreation checks, including both physical display
scales. Run `build/dev/viewport_tests` in a graphical session. The `--benchmark`
option now measures independent triangle buffers; add `--instanced` for the
original repeated-triangle comparison. Transparency and clipping are currently
renderer test APIs, not finished material or section tools.

## Development package

```sh
./scripts/package-source.sh /tmp/sketchyup-package
cd /tmp/sketchyup-package
makepkg -s
```

The script creates a source archive and writes its checksum into the generated
PKGBUILD. Package output contains the native app, CLI, desktop entry, icon, MIME
definition and license notices. [Clean Arch acceptance](docs/verification/R014-package.md)
covers install, desktop launch, upgrade/reopen and removal. See the
[package workflow](packaging/arch/README.md) for reproduction. This remains an
experimental development package.

## Design and delivery

- [Build plan](docs/BUILD_PLAN.md) and [scope matrix](docs/SCOPE.md)
- [PR roadmap](docs/PR_ROADMAP.md)
- [UX design and mockups](docs/UX_DESIGN.md)
- [AI modeling contract](docs/AI_MODELING.md) — proposed future behavior
- [Baseline/license decision](docs/decisions/0001-baseline.md)
- [Native architecture experiments](docs/decisions/0002-native-spikes.md)
- [Compatibility gaps](docs/decisions/0003-compatibility-gaps.md)

The original local Formline/Electron prototype was removed at the owner’s request.
The native application now lives in the main project checkout; supplied UX design
references remain preserved.
SketchyUp is not affiliated with SketchUp or Trimble.
