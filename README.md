# SketchyUp

An independent native Linux 3D modeler in development. C++20, Qt 6 and OpenGL;
ordinary editing runs locally without an account or browser runtime. Own code
is MIT licensed. Arch/Omarchy is the primary development environment.

## Current build

**Working native editor foundation, not a complete editor or release.** Draw lines,
freehand strokes, rectangles, polygons, circles, arcs and pie sectors on explicit or hovered planes, select
faces, push/pull planar regions, move/rotate/scale/flip selected geometry, copy and
color objects, undo/redo, and save/reopen native `.sketchyup` files. Faces retain
editable loops and holes. A headless driver uses the same core operations.

The [108-entry roadmap](docs/PR_ROADMAP.md) has passed its M0 feasibility and
M1 native foundation, M2 editable geometry and M3 manual drawing gates; M4 modeling tools are in progress. Components,
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
- `T`: tape measure. Start on an edge or guide line for a parallel offset guide;
  start on a point for a distance and guide point. Enter `900mm` for an exact sill
  height. Draw → Protractor: center, baseline endpoint, then angle point or `45deg`.
  Numeric re-entry revises the last guide as one undoable operation.
- Tap `Ctrl` while measuring, or uncheck Draw → Create guides when measuring, to
  measure without editing the document. View → Show guides controls both display
  and snapping. Edit → Delete all guides clears them in one undo step.
- Guides appear as dotted overlays through faces and never form model faces.
  Guide points, lines and true 3D intersections can be acquired by drawing tools;
  guide lines also supply parallel/perpendicular references.
- `Space`: select. `P`: select a face, move to preview, then click/drag to finish
  or enter a signed world distance along its transformed local normal. Numeric
  re-entry revises the last operation. Double-click a face to repeat that distance
  as a new undo step. Tap `Ctrl` to retain the starting face while creating a new
  cap and sides; Draw exposes the same mode. Retaining intermediate faces can
  create non-manifold internal boundaries; turn it off to cut a through opening.
- With Select active, click faces/edges/guides; `Ctrl` adds and `Shift` toggles.
  Drag left-to-right to select fully enclosed visible entities, right-to-left to
  select touched visible entities. Occluded geometry is excluded in both modes.
  Double-click a face for its boundaries; triple-click for connected geometry.
- `Tab` / `Shift+Tab` traverses visible entities; `Ctrl+A` selects eligible geometry.
  The Outliner selects whole contexts and supports `Ctrl` multi-selection.
  `Enter` on a context opens it; `Escape` clears selection, then exits one level.
- Edit offers temporary hide/reveal and context lock/unlock. View → Show hidden
  geometry exposes hidden entities but does not bypass locks or the active context.
  These view states are session-only. Persistent group visibility and locks are separate.
- `M`: move; `Q`: rotate; `S`: scale. Select geometry, choose a pivot, then a
  destination (plus a baseline/reference for rotation or scale). Type exact
  displacement, angle or scale factors in Measurements. Re-entry revises the same
  undo item. With no selection, Move can target an inferred vertex.
- After a Move or Rotate copy, type `xN` for N new copies or `/N` for N equal
  intervals ending at the chosen destination. Counts are limited to 1–100;
  re-entering counts, distance or angle revises the same undo item.
- `Ctrl` toggles transform copy mode; Edit also provides copy/local-axis settings
  and X/Y/Z flips about the selection center. Raw copies stay in their editing
  context; whole-context copies duplicate the hierarchy. Local mode requires one
  context. `[x,y,z]` always denotes world coordinates. `B`: change context color.
  `Delete` erases the selected entities as one undo step. Mixed deletion is bounded
  to 100 subentities per operation; select a whole context for bulk deletion.
- Point inference acquires endpoints, midpoints, curve centers, intersections,
  edges and faces within 8 logical pixels. Marker shapes and labels identify the
  result; `Tab` cycles nearby alternatives. Drawing from loose geometry adopts
  its context. Locked planes exclude off-plane points. Index preparation runs in
  the background; the 0.1 m grid remains the fallback when no candidate is acquired.
- Middle drag or `O`: orbit. Right drag or `H`: pan. `Z`: drag to zoom.
  `Alt`+left drag temporarily orbits within a drawing tool; add `Shift` to pan.
  Wheel: zoom around the pointer on the camera target plane. `Shift+Z`: fit.
- View → Navigation → Trackpad enables two-finger scrolling to pan,
  `Alt`+scroll to orbit and `Ctrl`+scroll to zoom. Native pinch/pan/rotate events
  also navigate while preserving an anchored tool. Navigation mode is remembered.
- `1/2/3` with Select active: perspective/top/front. View adds orthographic,
  right/back/left/bottom/isometric presets and a remembered 5–120° vertical FOV.
  Projection changes preserve target-plane scale; fit accounts for FOV and aspect.
  `Escape`: cancel drawing.
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
./build/dev/sketchyup-cli --script examples/transforms.json --output /tmp/transforms.sketchyup
./build/dev/sketchyup-cli --script examples/copy-arrays.json --output /tmp/arrays.sketchyup
./build/dev/sketchyup-cli --script examples/components.json --output /tmp/components.sketchyup
./build/dev/sketchyup-cli --input /tmp/curves.sketchyup --query-file examples/inference-query.json
```

`geometry.transform_selection` accepts typed context/face/edge/vertex/guide
entities, a column-major affine `matrix`, an optional `pivot`, `space` (`world`
or `local`) and boolean `copy`. Shared vertices move once; incident geometry
stays attached, and invalid nonplanar or collapsed results reject atomically.
Raw copies receive fresh geometry IDs inside their existing context; whole-context
copies duplicate the hierarchy. The result includes typed copy mappings. Preview
geometry includes world transforms, including children moved by a parent.
The [transform contract](docs/verification/R030a-scoped-transforms.md) records
limits; the [native tool evidence](docs/verification/R030b-native-transforms.md)
describes pointer input, numeric amendment and frame semantics.

Guide points and infinite guide lines are available through `guide.point`,
`guide.line`, `guide.angle`, `guide.offset`, `guide.erase` and `guide.clear`.
Guides remain separate from faces/edges. `guide.clear` with body `"0"` removes
all guides in one undo step; a body ID scopes cleanup. Creation accepts local or
world coordinates, angles use radians, and offsets use meters in the selected
space. `geometry.measure_distance` and `geometry.measure_angle` are read-only
queries for world-coordinate points. `geometry.infer` accepts `includeGuides`
(default true) and an optional direction reference with `body` plus `guide`
in place of `edge`; results identify both sources of mixed intersections.
The current file schema is version 8. It stores canonical component definitions
and stable instance-member bindings; versions 1–7 migrate with no invented
components. Version 7 face colors and earlier group/guide records retain their
previous migration behavior.

`component.create` converts a geometry/group root into a reusable definition;
`component.instance` places it with a parent-local affine matrix. `component.edit`
runs a nested command batch using canonical member IDs from `component.inspect`
and propagates the change to every instance in one undo step. Its `world` space
means definition coordinates. New model-root records created inside this scope
become children of the definition root. Editing resolved member geometry outside
this explicit scope rejects; a locked affected instance rejects the whole edit.
`component.make_unique` isolates a placement; for nested placements it also clones
the ancestor ownership path. `component.replace` keeps placement while replacing
members with fresh scene IDs. `component.axes` changes a definition's local frame
while preserving world geometry in all placements. Whole-context copies/arrays
continue sharing definitions; explode removes the selected binding and retains
geometry. See the [command contract](docs/verification/R033b-component-operations.md).
Native component controls and the definition-scope banner remain under development.

`group.create` groups sibling context IDs while preserving their geometry, colors
and world placement. `group.explode` removes one group boundary and merges eligible
promoted raw geometry; `merge: false` requests boundary removal alone;
`scene.reparent` moves a record to a group (or parent `"0"`) while preserving its
world transform. `scene.state` stores boolean `hidden` and `locked` flags, with
inherited visibility and authoritative protection of locked descendants. See
[the group recipe](examples/groups.json). Select faces, edges, guides or whole groups and use `Ctrl+G` to make a group.
Double-click or Enter opens it; Esc closes one level. The viewport breadcrumb
links to parent contexts. `Ctrl+Shift+G` explodes selected groups. The Edit menu
also exposes persistent group hide/lock and document-wide reveal/unlock.
Drawing and deletion stay in the active context. The Edit menu can merge raw
geometry in that context through `geometry.merge_context`, with coincident seam
welding and per-face colors preserved. Nested groups, locked records and hidden
geometry stay isolated. This operation does not boolean intersecting faces. See
[consolidation semantics and evidence](docs/verification/R032c-context-consolidation.md).

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
