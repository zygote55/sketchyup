# SketchyUp

An independent native Linux 3D modeler in development. C++20, Qt 6 and OpenGL;
ordinary editing runs locally without an account or browser runtime. Own code
is MIT licensed. Bundled Clipper2 retains its Boost Software License 1.0 and
Manifold retains Apache License 2.0; their notices ship with the application.
Arch/Omarchy is the primary development environment.

## Current build

**Working native editor foundation, not a complete editor or release.** Draw lines,
freehand strokes, rectangles, polygons, circles, arcs and pie sectors on explicit or hovered planes, select
faces, push/pull planar regions, move/rotate/scale/flip selected geometry, copy and
color objects, undo/redo, and save/reopen native `.sketchyup` files. Faces retain
editable loops and holes. A headless driver uses the same core operations.

The [108-entry roadmap](docs/PR_ROADMAP.md) has passed its M0 feasibility and
M1 native foundation, M2 editable geometry, M3 manual drawing and M4 editing/recovery gates. M5 automation is in progress. Shared components now support native editing.
OpenAI and experimental loopback Ollama adapters, transactional previews and optional
Blender rendering are implemented. Live OpenAI acceptance is still pending; the
measured local CPU profile timed out on the initial modeling corpus. General
exchange formats remain planned. Curves retain analytic parameters alongside configurable segmented editing geometry. Surface topology and file format remain experimental.
Automatic recovery copies are enabled every 30 seconds; File → Recovery settings
changes the interval (5–3,600 seconds) or disables it. The recovery status shows
only the last verified copy and identifies newer edits still in memory. A write
failure pauses automatic attempts; File → Save recovery now retries.
Recovery never overwrites your saved file. Startup and File → Recover work let
you open a verified copy as Edited, open the last saved file, or discard selected
recovery data. Recovered work first saves to a new native path.
The CLI can inspect copies with `--recovery-list ROOT` and reopen one with
`--recover ROOT/SESSION --output copy.sketchyup`.
Save explicitly for durable named files: explicit saves use a
checksummed container and preserve the previous valid file as `.sketchyup.bak`.
Files and the containing directory are synced before showing Saved. Open a backup
through the file dialog's all-files filter or the CLI if you need the previous save. Light, Dark and System
themes are available in View. Groups and reusable components support nested, mirrored
and nonuniform transforms through both native tools and the headless command API.

Formline v1 files can be imported through File → Import Formline or
`sketchyup-cli --import-formline source.formline --output copy.sketchyup`.
Imports become editable boxes and 48-sided cylinders in a new unsaved model,
with names, colors, visibility and source identities retained. The report explains
conversion details; the original file stays unchanged. See the
[format decision](docs/decisions/0014-formline-import.md).

## Build and run

Requires CMake 3.25+, Ninja, a C++20 compiler, Qt 6.8+ base/Wayland development
packages, OpenSSL 3 development headers and OpenGL 3.3. Verified locally with Qt 6.11.2 on Wayland and X11.
The pinned geometry dependency is included in `third_party/`; CMake does not
download code. See [measured evidence and limitations](docs/verification/native-spike.md).

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev
./build/dev/sketchyup --demo
```

On Arch, build dependencies are `cmake ninja gcc pkgconf qt6-base qt6-wayland wayland openssl`.
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
- `Ctrl+G`: make a group; `Ctrl+Shift+G`: explode selected groups/components.
  Enter or double-click opens a context; Escape or an outside click closes one level.
  The breadcrumb returns to an ancestor. `G` opens Make component from the viewport.
  Edit offers Make component, Place component,
  Replace component, Change component axes and Make unique. Raw face/edge/guide
  selections can become components in one undo step.
- A persistent banner names the active shared definition and counts its instances.
  Drawing, transforms, grouping, paint and deletion inside that scope update all
  instances. Make unique isolates the active placement, including nested ownership.
  A locked affected instance rejects the entire shared edit. Placement dialogs use
  world coordinates; the axes dialog uses component coordinates and preserves
  world geometry. The [native component evidence](docs/verification/R033c-native-components.md)
  records tested workflows and limits.
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

## Assistant and rendering

Press `Ctrl+J` or choose View → Assistant. In Preferences, choose OpenAI and
**ChatGPT subscription**, then **Continue with ChatGPT**. Finish browser sign-in,
choose an account-specific model, and Save. Eligible requests use your ChatGPT
plan; Manage ChatGPT usage opens its usage/access settings. You can select saved
accounts or add another account/workspace. Sign out removes the selected session.

Alternatively, select **API key (separate API billing)**, enter an explicit model
ID and store an API key. Both connections require `secret-tool` (Arch package
`libsecret`) and an available, unlocked Secret Service on Linux. Credentials are
never stored in preferences or native files. Subscription sessions are separate
from API keys; SketchyUp does not read Codex's credential files.
The first request asks you to approve sending model context to OpenAI. “What is
sent” explains attachments and the bounded document inspection tools available
to the provider. Keys do not belong in chat, terminal arguments or model files.

Preview first is the default. Review the hatched geometry, changed objects and
host-measured bounds/area, then Apply (`Ctrl+Return`) or Discard. Apply adds one
undo entry. Refine / re-plan starts a fresh request on the current model. A manual
edit makes an older proposal stale. Direct mode uses the same validated commit
path. Deletion and shared-definition edits require the corresponding per-request
checkbox. If an outcome is unknown, use Reconcile before editing or saving.
Escape returns focus to the model. Without a configured provider, ordinary
modeling, saving and rendering remain available.

The optional local provider uses an explicitly configured numeric loopback
endpoint and model. The measured Ollama 0.35.1 / `qwen3:4b-instruct` CPU profile is
experimental: all four live corpus tasks reached the five-minute limit. It has
no automatic download or cloud fallback. See [provider evidence](docs/verification/R045-local-provider.md).

Choose Camera → Render to set an explicit Blender executable, camera, resolution,
samples and CPU or supported GPU device. Blender 5.2 LTS is supported; CPU is the
default. The job renders an immutable snapshot while editing continues. Results
identify the source revision, show when the model has changed, and can be saved
as PNG. Blender is optional. See [render evidence](docs/verification/R050-native-render-ui.md).

The [M5 acceptance procedure](docs/M5_ACCEPTANCE.md) describes the reproducible
room/window workflow, retained evidence, live provider corpus and package setup.
The [M5 gate is complete](docs/verification/M5.md); M6 advanced modeling is in progress.
The [M6 acceptance procedure](docs/M6_ACCEPTANCE.md) reproduces the integrated
roof/stair/joinery/furniture study, site placement and live assistant checks.

## Headless commands

Versioned bounded inspection is available through `--inspect document.describe --input MODEL`
for summary discovery, or `--inspect-file REQUEST.json --input MODEL` for typed
hierarchy, topology and measurement requests. The in-process inspection session
also supports bounded, expiring snapshots; see the [snapshot contract](docs/decisions/0020-inspection-snapshots.md). Pages are limited to 100 rows and
256 KiB; stale references fail explicitly. File-only queries do not claim access
to desktop selection. See the [inspection contract and schemas](docs/decisions/0019-bounded-inspection.md).
Native in-process inspection reads the actual window selection and captures a
bounded PNG with camera metadata. `sketchyup --inspection-capabilities` prints
its registry; see the [desktop inspection contract](docs/decisions/0021-desktop-inspection.md).
Private transaction preparation and durable outcome reconciliation are implemented
in-process; see the [coordinator contract](docs/decisions/0024-transaction-coordinator.md).
Incremental begin/apply/inspect/preview/commit dispatch uses versioned schemas,
immutable commit identities and cached append receipts; see the
[transaction contract](docs/decisions/0025-transaction-dispatch.md).
Persistent headless sessions use `--session --input MODEL --outcomes DIRECTORY`,
with optional `--output MODEL` for an explicitly bound save destination. Create a
new baseline with `--session --new --output MODEL --outcomes DIRECTORY`.
`--session-capabilities` prints the shared schemas. Requests and replies use bounded
JSON lines; see the [session protocol](docs/decisions/0026-headless-session.md).
Use `assert.measurement` inside an edit batch or transaction to require a final
body measurement to match an explicit expected value and absolute tolerance.
It supports local/world dimensions, bounds, edge length, face area and validated
solid volume. A failed assertion rolls back the whole edit; later steps in the
same draft must keep earlier assertions true. See the
[asserted-solid example](examples/asserted-solid.json) and
[measurement contract](docs/decisions/0055-geometric-assertions.md).

Versioned recipes pass typed results between those same requests:
`--recipe examples/transaction-face-recipe.json --new --output /tmp/face.sketchyup --outcomes /tmp/face-outcomes`.
The [room/window recipe](docs/decisions/0031-room-window-recipes.md) creates a 6 × 4 m
room and widens one window from 1.2 to 1.4 m while preserving its sibling and frame
thickness:

```sh
sketchyup-cli --recipe examples/room-window-resize-recipe-v1.json --new \
  --output /tmp/recipe-room.sketchyup --outcomes /tmp/recipe-room-outcomes
```

Window dimensions are outer-frame dimensions. The room-only variant is
`examples/room-recipe-v1.json`; installed recipes live under
`/usr/share/doc/sketchyup/examples/`.

The [gable roof recipe](docs/decisions/0056-gable-roof-recipe.md) adds an editable
closed slab with explicit pitch in degrees, overhang and vertical thickness.
`assembly.roof` preserves existing room/window content and verifies its volume,
area and bounds before publication. Run `examples/roof-recipe-v1.json` to create
and save the default room with its roof; native before/after files are also shipped.

The [straight stair recipe](docs/decisions/0057-straight-stair-recipe.md) creates a
filled flight with equal risers and explicit tread depth, width and final elevation.
`examples/stair-recipe-v1.json` places the default flight beside the room and roof;
each tread, the closed volume and preservation of the scene are verified.

The [table and cabinet recipes](docs/decisions/0058-furniture-recipes.md) build
measured solid members with shared component legs or panels. Their examples are
`examples/table-recipe-v1.json` and `examples/cabinet-recipe-v1.json`. Member dimensions,
clearances and volumes are verified; ordinary shared and instance-only edits remain
available afterward.

The [site placement recipe](docs/decisions/0059-site-placement-recipe.md) moves an
existing assembly with explicit position units, a world or parent frame and a yaw
delta. Local geometry, materials, shared components and complete hosted assemblies
are preserved. `examples/site-recipe-v1.json` builds and places the full study in
one transaction; adopted native before/after fixtures are also included.

See the [recipe contract](docs/decisions/0027-transaction-recipes.md) and
`--recipe-capabilities`. A local MCP stdio client can use `--mcp` with the same
explicit document/outcome options. It targets protocol 2026-07-28; see the
[MCP contract](docs/decisions/0028-local-mcp.md) and `--mcp-capabilities`.
For actual desktop selection and camera inspection, launch an explicit native
model with `--mcp-inspection-socket` and connect using
`sketchyup-cli --mcp-connect SOCKET`; see the
[native MCP binding](docs/decisions/0029-native-mcp.md).
Export a fixed render snapshot with
`sketchyup-cli --input MODEL --export-glb NEW_DIRECTORY`, optionally adding
`--render-settings examples/render-settings-v1.json`. The directory contains
`scene.glb` and its revision/settings/hash manifest. Export works without Blender;
Distinct front/back colors, opacity and PNG/JPEG textures are preserved in GLB and
Cycles, including independent projections, reversed faces and mirrored instances.
Original image bytes remain packaged alongside normalized render images. Missing,
invalid or unsupported images retain the swatch color and report their status;
see [textured transfer](docs/decisions/0064-textured-glb-export.md),
[two-sided transfer](docs/decisions/0050-two-sided-export.md) and the
[GLB subset and loss policy](docs/decisions/0032-glb-snapshots.md).
The headless binding reports desktop selection as unavailable. The native binding
exposes inspection only; staged mutation tools are currently available headlessly.


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
./build/dev/sketchyup-cli --script examples/tags.json --output /tmp/tags.sketchyup
./build/dev/sketchyup-cli --input /tmp/tags.sketchyup --query tags.describe
./build/dev/sketchyup-cli --script examples/entity-info.json --output /tmp/entity-info.sketchyup
./build/dev/sketchyup-cli --input /tmp/entity-info.sketchyup --query entity.inspect --context 1
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
The current file schema is version 15. It stores hosted component attachments,
uncut geometry and stable opening identities, explicit component glue-face
references and cutting behavior, persistent edge appearance flags,
display units, managed assets, material swatches and front/back
assignments, tag folders and assignments, canonical component definitions and
stable instance-member bindings. Versions 1–14 acquire no invented host attachments;
versions 1–13 acquire no invented glue behavior.
Versions 1–10 acquire no invented assets; versions 1–9 preserve legacy colors without
inventing swatches; versions 1–8 migrate with all entities Untagged, and versions
1–7 acquire no invented components. Earlier group and guide migrations remain supported.

`tag.create`, `tag.edit`, `tag.delete` and `tag.assign` manage tags separately
from scene ownership. Tag `"0"` means Untagged. Folder visibility affects its
descendant tags; hiding a tag preserves geometry and transforms. `tags.describe`
reports local and effective visibility. Component placement tags are local;
member assignments use shared component scope. `scene.rename` names an entity.
The Model panel offers Outliner, Tags, Info and Materials tabs. Search filters the entity
hierarchy while retaining matching ancestors. Enter/double-click opens a context;
Escape closes it. F2 renames, Space toggles visibility, Ctrl+Shift+L toggles an
entity lock, Ctrl+Shift+M opens Move to, and Ctrl+Alt+T assigns a tag. Buttons
provide the same operations. Drag onto a group/folder to reparent, or onto empty
space to move to the root; world placement is preserved. Tags offers folder and
tag creation, visibility checkboxes, rename, move, assignment and unused deletion.
Hidden/locked rows remain available for inspection and reveal/unlock. Shared
member edits still require opening their component context. See the
[native organization evidence](docs/verification/R034b-native-organization.md).

`material.create`, `material.edit` and `material.delete` manage named in-model
RGB/opacity swatches. `material.assign` targets a face or a record's own faces,
with `front`, `back` or `both` sides; material `"0"` restores the legacy color.
Splits, extrusion, copies, grouping and mirrored consolidation retain both sides.
Shared component member assignments use explicit component scope; swatch table
edits remain document-wide. `materials.describe` lists swatches and
`material.sample` reports both sides without editing. See the
[material contract](docs/decisions/0010-material-records.md) and
[recipe](examples/materials.json). The viewport renders independent front/back
colors and opacity, including mirrored placements. Fully transparent sides pass
face picking through; topology edges remain visible. Editing a swatch refreshes
only dependent appearance buffers. Intersecting transparent surfaces retain the
centroid-sorting limitation. The Materials tab edits named swatches and offers local presets through New.
Choose Front, Back or Both sides; Apply paints selected faces. B activates Paint;
Alt-click samples the visible side. Open a group/component before painting its
contents. Swatch edits affect every use; assignments in an opened component
remain shared. See the [rendering contract](docs/decisions/0012-material-rendering.md).

`asset.import` stores canonical base64 bytes; `asset.missing` retains an explicit
missing-resource record. `asset.replace` resolves/replaces bytes under the same
identity, or accepts null data to mark a resource missing. `asset.delete` rejects
resources still referenced by a swatch. Material create/edit accepts `asset`
(`"0"` clears it). `assets.describe` returns a checksummed manifest without raw
payloads; material queries expose missing/present status. Native containers own
their bytes and survive relocation without the source files. Asset paths are
fixed logical keys, never filesystem extraction paths. Limits are 16 MiB per
asset, 64 MiB per document and 1,024 records. See the
[asset contract](docs/decisions/0011-managed-assets.md). Schema 16 retains independent
front/back affine UV projections through modeling edits, Undo, shared components
and relocation ([mapping contract](docs/decisions/0062-face-texture-records.md)).
The image backend decodes bounded static PNG/JPEG assets into consistent sRGB
and straight-alpha pixels ([image contract](docs/decisions/0063-texture-image-decoding.md)).
GLB/Blender export includes those images and independent UVs. Native texture-authoring
controls and textured viewport drawing remain pending.
Materials offers Attach file, Replace file
(including missing-resource resolution), Detach file and undoable Clean files
for unused resources. New/edit dialogs can also bind an existing stored file.

`entity.inspect` reports world, parent and intrinsic bounds, lengths and areas.
Volume is present only after bounded material-solid validation, including enclosed
cavities. Open, invalid, multiple-record or disconnected material geometry returns
null volume with a reason.
`entity.position` and `entity.dimensions` edit real placement/geometry in world or
parent coordinates. Intrinsic measurements remain separate from placement scale.
`entity.properties` stores typed semantic values for recipes. See the
[measurement contract](docs/decisions/0009-entity-measurements.md). The Info tab
shows the selected entity in an explicit coordinate frame. Edit entity (F2)
accepts metric/imperial or locale-aware origin/dimension values and edits name
and tag in the same undo step. Invalid inputs stay in the dialog for correction;
Cancel changes nothing. Resizing uses the bounds minimum; an explicitly entered
origin is applied afterward. Closed components edit their placement, while an
opened component's members use shared scope. Inspect problem geometry selects
available offending edges/faces without modifying the model. See the
[native Entity info evidence](docs/verification/R035b-native-entity-info.md).

`component.create` converts a geometry/group root into a reusable definition;
`component.selection` converts typed selected faces, edges, guides or contexts;
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
For native adapters, `component.edit` also accepts an `instance` matching the
specified definition. Inner commands then use that placement's scene IDs and
world coordinate frame. Geometry stays canonical; the temporary editing frame
is removed before publication. The operation reports created members for the
initiating placement, while topology changes still cover every affected instance.
`scene.state` with body `"0"` and false flags reveals/unlocks the whole document,
including canonical component members, in one undo step; global hide/lock rejects.

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

Hosted components use an explicit canonical glue face and an ordinary geometry
host. `component.glue` sets or clears the definition's member/face, local anchor,
tangent and cutting flag. `component.attach` places an instance on a host-local
anchor; optional tangent, signed scale, rotation in radians and normal inset
control placement. `component.bind` preserves its current pose and requires the
explicit host-local inset. `component.detach` restores its opening;
`component.bake_host` keeps the cut geometry and releases all attachments on that
host. Moves and shared-definition edits automatically regenerate attached openings
with surviving reveal identities and paint preserved. Copying or arraying an attached
whole instance retains its host and creates independent openings. Copy the host with
all its attachments (or their enclosing group) for an independent assembly; partial
attachment subsets are rejected. A host-only copy keeps baked cut geometry. Native
Ctrl-copy, `xN`, `/N` and exact spacing revisions share one Undo step. See the
[executable example](examples/hosted-component.json) and
[hosted-component contract](docs/decisions/0054-hosted-components.md).

Use `entity.describe` to discover a materialized face's canonical member binding,
and `component.instances` to inspect the definition's glue and paged attachments.
The assistant advertises attach/bind/detach as routine operations, glue edits with
shared-definition permission, and baking with destructive-edit permission.

For native placement, enter a component, select its alignment face and choose
**Edit → Set glue face**. Set its local anchor/direction and explicitly enable
opening cutting when wanted. Leave component editing, select the whole component
and Ctrl-click one host face, then press **Shift+H**. Move on that face to preview;
click or Enter applies, and Escape cancels. Measurements accepts three host-local
anchor coordinates or one signed inset length; repeated numeric input revises the
same Undo item. **Attachment placement options** sets rotation in degrees, signed
scale and inset; restart placement after closing it. **Bind component at current
pose** uses the explicit inset while retaining position. Edit also offers detach,
clear shared glue, and bake host openings.

To upgrade a validated room recipe, select the whole room and choose
**Edit → Adopt recipe window attachments**. This gives both windows general hosted
behavior in one Undo step, retaining their poses, IDs and paint. Existing files
keep their original relationships until explicitly upgraded. The shared command
is `assembly.room.adopt_hosted` with the room's `body` ID; the
[executable example](examples/hosted-room-recipe-v1.json) creates and adopts a room,
then widens one window. Definitions used outside the room must first be made unique.
The authored instance-only resize remains available after adoption, while ordinary
Move and Delete update openings through the general host system.

The topology query exposes stable context-scoped edges, oriented loops and radial
adjacency, plus analytic curve parameters and ordered derived-edge references.
Curve command angles use radians; segment counts are bounded to 256. Curve
metadata survives edge splits and retires when an edit breaks its outline. The wire recipe demonstrates a loose edge and a propagated edge split;
these operations currently have command paths while direct edge tools are in progress.
The planar-grid recipe forms four editable faces from finite segments. Planar insertion
handles intersections, overlaps and holes within the documented arrangement limits.
Intersect (**I**) inserts crossing or coplanar intersection edges into selected
faces. Use Select and Ctrl-click to choose targets, then I to preview; Enter or
click applies, Escape cancels, and Alt-drag orbits. Draw → Intersection references
chooses Selected faces, Active context, or Model. Context and Model retain
unselected reference bodies; only target bodies change, with target descendants
selected and one Undo item. Context stops at group boundaries; Model reads
persistently visible scene geometry across groups, including from an explicit
component edit. Existing seams produce an actionable no-change message. The same
operation is available as `geometry.intersect`; see the
[intersection contract](docs/decisions/0045-face-intersections.md).

Solid tools (**Shift+B**) previews the two selected solids; selecting a face
from each is enough. Draw → Solid operation options chooses Union, Subtract,
Intersection, Trim, Split or Outer Shell. The viewport identifies target and tool by name and
ID; Swap target and tool reverses subtraction order. Keep originals is enabled
initially. Turning it off consumes both in the same Undo item. Trim instead
shows **Keep target (tool always retained)** and only replaces the target.
Split creates target-only, tool-only and overlap regions; Outer Shell fills
enclosed cavities while preserving through-holes. All generated regions are
selected together. Enter or click applies, Escape cancels, and Alt-drag orbits
while preserving the preview. Empty results explain whether the originals
will be retained or removed. Shared component edits update all instances; use
Make Unique first for an independent edit.

`geometry.boolean` combines two editable raw solids in one context using `union`,
`subtract` (target minus tool), or `intersection`. The required `keepOperands`
choice retains the exact sources or consumes them in the same Undo item. Outputs
inherit source face colors and correctly oriented front/back materials; receipts
identify the generated bodies and each source face. Enter group containers before
choosing their raw solids. Enclosed cavity boundaries stay with their surrounding
material solid; islands inside cavities become separate output bodies. See the
[Boolean contract](docs/decisions/0046-solid-booleans.md) and
[executable example](examples/solid-boolean.json). The
[enclosed-cavity example](examples/enclosed-cavity.json) produces a 7 m³ hollow
solid from an 8 m³ cube and a 1 m³ internal cutter.

`geometry.trim` subtracts the tool while always retaining it; `keepTarget` selects
whether the target also survives. `geometry.split` returns separate target-only,
tool-only and overlap regions. `geometry.outer_shell` fills enclosed cavities of
the union while preserving exterior through-holes. Split and Outer Shell require
`keepOperands`. All three require `body`, `tool` and `context`, publish one Undo
item and return region/provenance records in `solidOperations`. See the installed
[Trim](examples/solid-trim.json), [Split](examples/solid-split.json) and
[Outer Shell](examples/solid-outer-shell.json) examples and the
[publication contract](docs/decisions/0048-split-outer-shell.md).

Face orientation (**Shift+O**) previews selected faces with arrows showing their
new front direction. Draw → Face orientation options chooses **Reverse selected
faces** or **Orient connected faces to selected reference**. Orient requires one
reference face and keeps its direction. Enter or click applies, Escape cancels,
and Alt-drag orbits. Physical front/back appearance and selection are retained;
shared component edits affect all instances. Use Make Unique for an isolated edit.

`geometry.reverse_faces` reverses explicitly selected faces while preserving the
appearance of their physical sides, including front/back materials and opacity.
`geometry.orient_faces` preserves a reference face and makes its edge-connected
surface consistent. Both require an explicit `context`; non-manifold connections
and contradictory cycles reject with diagnostics. Preview, component editing and
Undo use the shared command path. See the [orientation example](examples/face-orientation.json)
and [contract](docs/decisions/0049-face-orientation.md).


`geometry.edge_appearance` sets independent `hidden`, `soft` and `smooth` flags
on explicit `entities` (`body`/`edge` pairs) in an explicit `context`. Supply at
least one boolean flag; omitted flags retain their current value. Flags persist
and participate in shared component edits, preview and Undo. Edge detail and
paged topology inspection expose them. Smooth flags now drive shared corner
normals in the viewport, GLB export and Blender renders, including mirrored
components. **Edit → Edge appearance** changes flags on selected edges.
Use **View → Show hidden geometry** to display hidden/softened edges as dashed
strokes and select them for Reveal or Harden. These actions keep other flags
intact and support Undo. Locks and group/component editing boundaries still apply. See the [edge appearance example](examples/edge-appearance.json)
and [contract](docs/decisions/0051-edge-appearance.md).

**Edit → Geometry diagnostics** opens findings for selected geometry or the active
editing context. Select or frame listed entities, refresh after edits, and preview
eligible orientation repairs. Connected orientation requires an explicit reference
face. Enter applies the preview; Escape cancels; Undo restores the edit. Shared
components follow their usual editing scope. Truncated samples cannot reverse a
whole shell, and diagnostics never delete unrelated geometry automatically.

`geometry.diagnose` inspects one explicit body record and returns bounded findings
for open boundaries, non-manifold geometry, inconsistent or inverted orientation,
loose geometry and near-degenerate faces. Counts distinguish complete scans from
lower bounds; omitted reference samples and incomplete analysis are explicit.
Diagnosis reads hidden geometry too, uses local coordinates, and excludes child
records. It never edits the model. The same query works through CLI inspection,
snapshots, MCP and the assistant. See the [diagnostic contract](docs/decisions/0053-geometry-diagnostics.md).

Follow Me (**Shift+F**) sweeps a selected profile face along selected connected
path edges. Use Select and Ctrl-click to add the face and path, then Shift+F to
preview; Enter or click applies, and Escape cancels. Alt-drag can orbit during
preview. The source profile, path, and selection are retained; the result is a
separate editable solid with one Undo item. Open and closed paths use bounded
miter joins and explicit frame transport. Branches, crossings, consumed short
segments and twisted closures reject with an explanation. Closed paths with
holed profiles are currently unsupported. `geometry.sweep` exposes the same
operation with local/world coordinates and generated face mappings; see the
[sweep contract](docs/decisions/0044-profile-sweep.md).

Offset (**F**) draws parallel boundaries for a selected planar face, including
concave outlines and holes. Click a face and move across its nearest edge to
preview, then click or finish a drag; Escape cancels. Measurements accepts an
exact signed distance: positive outward, negative inward. Select a face first
for keyboard-only entry. Re-enter the distance to revise the last eligible offset
as one Undo item. The tool uses world distance even in scaled components.

The same `geometry.offset` command accepts `body`, `face`, `distance` and optional
`space` (`local` by default or `world`). All surviving islands contribute
boundaries; complete collapse rejects without changing the source. Original
faces and holes are retained: insets partition coverage, outsets can add exterior
faces, and contracted hole outlines remain wires inside the original void. The
tool does not automatically heal those holes. See
[the contract](docs/decisions/0043-planar-offset.md) and
[CLI example](examples/planar-offset.json).

`geometry.push_pull` supports isolated profiles, complete prism caps, recessed face
regions and push-to-opposite-face openings. Unsupported intersections reject before
committing. Use `--preview --script recipe.json` to inspect prospective geometry and
lineage without changing the input document; preview cannot save an output file.

A script is a local JSON array, validated and committed as one batch. The in-process
API checks document identity and revision; failed batches change nothing. This is
an experimental local driver, not yet the durable AI/MCP protocol. The example's
IDs are specific to its empty-document fixture. Query live IDs before editing an
existing document; do not reuse them across documents.

The `document.units` command sets per-document display/entry preferences to `m`,
`mm` or `ft-in`. Coordinate arguments remain meters. Changing units is undoable
and survives save/reopen and recovery; it never resizes geometry. The
[units contract](docs/decisions/0018-document-units.md) describes schema 12 and
migration of older models. First run asks for default units; **File → Document
units** changes the current model and can set the default for new documents.
Bare lengths use that choice (feet for feet/inches); explicit unit suffixes still
override it. Measurements, tape readouts and Entity info show the document units.

The headless `history.describe` query exposes bounded labeled undo/redo pages.
A batch may attach a human label and task/request metadata; this remains local
session history and is not stored in model files. `--history-position N` navigates
the retained cursor after a CLI recipe. See the
[history contract](docs/decisions/0017-labeled-history.md). Open **View → History**
to browse labeled steps, saved markers and attached task requests. Click a step,
or select it and press Enter, to move through the same stack as Undo/Redo. The
panel pages large histories and marks the retained baseline when old steps have
been pruned. History navigation returns keyboard focus to the model.

The [M4 room study](examples/m4-room-study.sketchyup) is an editable native model
with two window assemblies, nested frame groups, materials and millimeter units.
The [integrated workflow record](docs/verification/M4.md) documents its construction,
shared/unique edits, History navigation, save/reopen and simulated crash recovery.

## Viewport verification

The [viewport follow-up](docs/verification/R002-viewport.md) records native pixel,
picking, GPU-cache and context-recreation checks, including both physical display
scales. Run `build/dev/viewport_tests` in a graphical session. The `--benchmark`
option now measures independent triangle buffers; add `--instanced` for the
original repeated-triangle comparison. Front/back material opacity is available in the Materials panel; clipping remains
a renderer test API pending section tools.

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

### Optional Blender rendering

In the native editor, choose **Camera → Render…**, select an installed Blender
5.2 LTS executable (or leave the path blank to discover `blender`), and check its
devices. Choose the current camera or fit the visible model, resolution and sample
count, then Render. CPU works without a GPU backend; an explicitly selected GPU
can retry once on CPU. Blender is optional for editing and saving native models.

Keep modeling while the captured snapshot renders. The status chip opens progress,
cancellation and diagnostics. A verified image opens beside the Model tab, with
its original revision and a warning if the model has changed. **Save image as…**
writes its PNG independently of model Save. Two recent results are kept in memory.
The initial Studio preset has the [GLB material/geometry limitations](docs/decisions/0032-glb-snapshots.md).

The optional local assistant adapter uses a measured Ollama 0.35.1 profile on a
numeric loopback endpoint. It sends no credentials or images, checks runtime and
model capabilities before document transfer, and rejects context overflow rather
than silently dropping history. See the [local profile and trial evidence](docs/verification/R045-local-provider.md)
for the exact model digest, CPU limits, known failures and opt-in corpus runner.
Native assistant setup is still under development; ordinary modeling does not
require a provider.
