# SketchyUp release notes

## 1.3.0

SketchyUp 1.3.0 is a minor release. It adds terrain and site tools, Extended
[scope row F03](SCOPE.md), in its own gated release ([M10 acceptance](M10_ACCEPTANCE.md)).
Existing commands, queries and schemas keep their shapes; everything new is additive,
as a minor version allows ([ADR 0162](decisions/0162-release-versioning.md)). The release
record, with the release-candidate evidence, is [verification/F03-1.3.0.md](verification/F03-1.3.0.md).
The [support matrix](SUPPORT_MATRIX.md) states what is supported.

### Terrain import and surfaces

A terrain is a surface triangulated from survey sources and kept with them: its contours,
breaklines, spot points, boundary and holes. It is stored in tiles of at most 2,048
triangles, up to 250,000 triangles a document, outside the 100,000-face modelling budget.
The [user guide](USER_GUIDE.md#terrain-and-site-tools) walks through the workflow;
[ADR 0168](decisions/0168-terrain-import-and-surfaces.md) defines the record, the sources and
the limits.

- **File → Import terrain…** reads a DXF with contours (LWPOLYLINE with elevation, 2D and
  3D POLYLINE, LINE; arcs are segmented) or a CSV, TXT, XYZ or PTS point file (PNEZD,
  PENZD, ENZ and XYZ presets, or a meaning per column; description codes as ground points
  or breaklines). Choose the units first: Import stays disabled until you do, and a DXF that
  declares its units offers them. The US survey foot is 1200/3937 m, and the conversion is
  shown and reported. Each layer or code gets a role: contour, breakline, boundary, hole,
  ground points or ignore. A live preview shows the sources or the triangulated surface. The
  check shows its stages with a progress bar, and Cancel stops it at once.
- **Bounded, located reports.** Crossing contours, conflicting duplicates and other invalid
  input refuse the import and change nothing. The report counts every problem and locates
  up to 32 of each kind (at most 64 KiB); choosing a location zooms the preview to it. Copy
  or save the report.
- **Create terrain from selection.** Select drawn edges or curves (inside groups too) and
  choose it from the viewport's context menu (right click, Menu key or Shift+F10).
  Constant-height edges become contours and the others breaklines. When the selection has a
  closed loop or spans several tags, a roles step gives each tag Automatic, Contour,
  Breakline, Boundary, Hole or Ignore, with the command's own check shown before Create.
- **Working with a terrain.** The Info tab's **Terrain** section shows the triangles and
  tiles, the source and its digest, the sources by role, the elevation range and the flat
  triangles. **Terrain settings…** changes the boundary mode, maximum edge length,
  flat-triangle flips and Steiner fill, with the result shown before it applies.
  **Regenerate** rebuilds a terrain whose tiles no longer match its sources. **Explode
  terrain…** turns it into ordinary geometry when the faces fit the modelling budget.
  Push/pull, Offset, Follow Me, the Solid tools and Face orientation (and, inside an open
  terrain, the drawing, guide and transform tools) show a not-allowed cursor over a terrain,
  with Explode offered in the status bar; Select, Paint, tags,
  hiding, locking and moving the whole terrain work as usual. A new **Terrain** menu,
  between Draw and Camera, holds the terrain tools and actions.
- **Display.** A new terrain uses the built-in **Terrain** material and draws as one smooth
  surface, without tile seams. **Show contour lines** draws contours at a chosen interval
  (every fifth an index line), and **Shading** can colour the surface by ten elevation
  bands. These are display only: not geometry, and selection and snapping ignore them.

### Site coordinates

- **A site origin.** The first terrain import sets the model's site origin: the easting,
  northing and elevation of the model origin, in metres, proposed as the data centre rounded
  to 100 m, with an informational CRS label. **File → Site settings…** shows it and where it
  came from, and **Re-anchor…** moves it without changing any site coordinate.
- **Large coordinates stay exact.** Geometry is stored relative to the origin, so survey
  coordinates keep their precision: on the UTM-scale test terrain every vertex is within
  1e-9 m of its source ([R095.f](verification/R095f-terrain.md)).
- **Site positions in the tools.** Entity info shows site coordinates, `@E, N[, Z]` in the
  Measurements box types a site position for any tool that takes a point, and **View → Site
  origin** marks the origin with a labelled crosshair.
- **DXF export with site coordinates.** File → Export 2D DXF… offers **Write site
  coordinates** (`--dxf-site-coordinates` on the command line). It adds the origin back to X
  and Y, so an imported survey drawing round-trips at its own decimals. It is off by default
  and needs a site origin; Z is not written. Other exports keep model coordinates, and their
  reports say so.

### Terrain editing, drape and stamp

Every change to a terrain's shape, sources or paint is one undoable edit, kept in the
terrain's list of edits (up to 4,096 entries or 8 MiB), so Regenerate and Terrain settings rebuild the terrain from its sources and replay
the list. An edit replaces only the tiles it touches and leaves the rest of the terrain
untouched. [ADR 0169](decisions/0169-terrain-editing-drape-stamp.md) defines the edits and
their limits.

- **Sculpt and Smooth** (**Terrain → Sculpt**, **Shift+S**, or the rail's **Terrain**
  slot). A brush on the surface marks every vertex that will move. Press and drag to paint
  a stroke, or click and move up or down to set an amount. **Ctrl** lowers, **Shift**
  smooths, and **[** and **]** change the radius. Region scope uses a selected face or loop
  or a clicked polygon with a falloff width. The change is shown in place before it is
  applied, and Escape withdraws it. Typing a value after an edit revises it.
- **Edit contours.** Draws the terrain's source contours, breaklines and spot points over
  the faded display contours. Drag a vertex handle to move or insert a vertex, type an
  elevation, or remove a vertex or a source. Add contour, Add breakline and Add spot add
  sources, and **Promote** turns a display contour into a source contour. Only the
  triangles around the change are re-triangulated. A change that would cross another
  source, leave the outline or enter a pad is marked in red at the pointer, with its place
  in site coordinates, and changes nothing.
- **Stamp pad.** Flattens a footprint (a horizontal face, a group's or component's lowest
  horizontal face, a closed loop or a clicked polygon) to a pad elevation. It grades the
  ground around it by cut and fill slopes to daylight (default 1:2, with a 100 m maximum
  distance by default) or by a
  band of fixed width. The preview tints cut and fill and draws the slope rays and the
  daylight line. A remembered **Pad offset** sets the pad below the footprint. Pads are
  protected: sculpting leaves them, and contour edits and other pads that would enter one
  are refused.
- **Drape.** **Terrain → Drape** (or **Drape onto terrain** in the context menu) projects
  selected edges, curves and faces vertically onto the terrain. **Imprint** adds them as
  breaklines without changing the terrain's shape, and can paint each face's region with
  its material on every rebuild. **Overlay** makes ordinary geometry lying on the terrain in
  a new group instead. Parts outside the terrain are clipped and reported.
- **Terrain edits.** With a terrain selected, the Info tab lists its changes. Select one to
  outline it in the view, then **Remove** it, **Edit pad…**, **Restamp** a pad from its
  building, or **Reset edits…**. The terrain is then replayed in the background with
  progress and **Cancel**. A change that no longer applies is kept, marked **Skipped** with
  the reason.
- **Keyboard paths.** The brush, a source or a footprint can be placed by typing `[x, y]` (model
  metres) or `@E, N` (site coordinates); F6 moves between the view, the tool strip and the
  Measurements box.

### Command line and automation

- **New transaction operations:** `site.set`, `site.reanchor`, `terrain.import`,
  `terrain.inspect_source` (a preview that changes nothing), `terrain.create_from_edges`,
  `terrain.set_parameters`, `terrain.regenerate`, `terrain.set_display`, `terrain.explode`,
  `terrain.sculpt`, `terrain.edit_source`, `terrain.add_source`, `terrain.remove_source`,
  `terrain.stamp_pad`, `terrain.drape`, `terrain.update_edit`, `terrain.remove_edit` and
  `terrain.reset_edits`.
- **New inspection queries:** `site.describe`, `site.convert`, `terrain.describe` (which
  lists a terrain's sources, edits, pads and paint regions) and `terrain.elevation_at`. MCP
  and the headless session expose all of them.
- **CLI:** `sketchyup-cli --import-terrain FILE` imports a terrain into `--input` (or a new
  model), with `--terrain-units`, `--origin`, `--terrain-crs`, `--terrain-roles` and the
  point-file options. On a terminal it shows the check's stages on stderr; its stdout and
  JSON output do not change. `--export-dxf` gains `--dxf-site-coordinates`.
- **The in-app assistant can read terrain but not change it.** It now sees `site.describe`,
  `terrain.describe` and `terrain.elevation_at`, and no other site or terrain command. This
  adds about 2.4–3.8 KB of tool schema to each assistant turn.
- The schema files keep their names and versions (`transactions-v1`, `inspection-v1`, MCP
  2026-07-28 and the others listed under 1.0.0). They gain these entries, and no existing
  response shape changes.

### Assistant

When the model sends a complete reply that breaks one of the host's rules, the assistant
now refuses it with a tool error that names the rule. Before, the whole task stopped
([ADR 0167](decisions/0167-recoverable-provider-replies.md)). The model reads the error on
its next turn and can try again. Unusable replies (transport errors, invalid or incomplete
responses) and the task's budgets still stop the task. The ChatGPT-plan limits and the
instructions are unchanged from 1.2.0. Because the assistant's tool lists changed, 1.3.0
is evaluated against provider corpus **version 5** at the release candidate: the version 4
tasks, prompts, limits and instructions with the new tool lists
([ADR 0168, R095.g notes](decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095g)).

### Other changes

- **Face tessellation no longer hangs (D-042).** Faces larger than about 10 m (quads,
  L-shapes, faces with holes) and section caps of similar size could send tessellation into
  unbounded memory growth. That hung an edit, a load or a render until the system stopped
  SketchyUp. The vendored Clipper2 triangulation now tests crossings exactly and bounds its
  work, refusing a face it cannot triangulate instead of growing. Faces that tessellated
  before give the same triangles.
- **Annotations follow geometry.** Paper dimensions and leader labels on sheets survive
  edge splits and Make component, and model dimensions and labels survive Make component
  and Make group. Ambiguous cases still break, and **Reattach** repairs them. This lifts the
  limitation disclosed in 1.1.0 and 1.2.0.
- **Configurable components.** Painting inside a definition no longer marks its drivers
  Changed (D-038). The unique-geometry check at many variant placements is faster (D-039).
- **Tool rail.** Below 800 px the Model rail shows one group of tools behind a group button.
  The rail is 88 px wide, with the short labels Tape and New face.
- **Focus rings** keep their contrast next to the canvas, scroll bars and overflowing
  controls (D-023), and snap to whole device pixels at fractional scale factors (D-032).
- **Request validation.** The automation schema validator now checks `anyOf`, so an empty
  `reference_image.update` or `text.update` is refused at the schema layer. Before, the
  command refused it later with its own message. Published schemas are unchanged.

### File format

1.3.0 writes schema `sketchyup-document-v29` (container version 2, required feature
`terrain-v1` after `component-parameters-v1`) **only when the document has a terrain or a
site origin**. Otherwise it writes schema 28 (with component parameters), 27 (with sheets)
or 26, as 1.2.0 does; these differ from a 1.2.0 save only in the writer string. A terrain's
mesh is stored in a binary `terrain-mesh` chunk. SketchyUp 1.2.0 refuses a schema-29 file
with "Unsupported container field" and leaves it unchanged (checked against the published
1.2.0-1 package; [R095.f](verification/R095f-terrain.md)). SketchyUp 1.1.x and 1.0.x read
at most schema 27 and 26 and refuse it too. A terrain made from model edges needs no site origin, so
exploding it returns the document to its lower schema; a document with a site origin
stays at schema 29. 1.3.0 reads document versions 1 through 29. Terrain documents saved
by development builds before 1.3.0 may need to be re-created, because schema 29 could
still change until this release
([ADR 0108](decisions/0108-public-native-format.md#schema-29-terrain-and-site-origin-r095a)).

### Upgrade from 1.2.0-1

Run `pacman -U sketchyup-1.3.0-1-x86_64.pkg.tar.zst`. pacman orders 1.3.0-1 after
1.2.0-1. Package acceptance upgrades a 1.2.0-1 package built from the release source to
1.3.0-1 and checks that preferences survive. In the package check, the published 1.2.0-1
CLI must refuse a schema-29 file unchanged and open schema-28 files written by 1.3.0.

To roll back, install `sketchyup-1.2.0-1-x86_64.pkg.tar.zst` with `pacman -U`. Documents
without a terrain or a site origin open in 1.2.0; documents with one do not. When 1.3.0
saves over an existing document, the previous bytes are kept as `NAME.sketchyup.bak`, which
holds only one previous version. Copy any document you may need in 1.2.0 before you
import a terrain into it.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.3.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.3.0-1, x86_64 |
| Native document | Schema `sketchyup-document-v29` in SKUPDOC container version 2 for documents with a terrain or a site origin; v28 for documents with component parameters; v27 for documents with sheets; v26 otherwise. Reads versions 1 through 29. Saves record the writer `SketchyUp/1.3.0` |
| MCP, inspection, transactions and other public schemas | Same versions as 1.0.0 (below), with the additive sheet (1.1.0), component parameter (1.2.0) and site and terrain (1.3.0) operations and queries |
| Assistant provider | OpenAI `gpt-6-astra` over a ChatGPT-plan sign-in, with the 1.2.0 limits. Evaluated at the release candidate against corpus v5; the results are in the release record |
| New third-party code | CDT 2.0.0 (MPL-2.0, header-only, with bundled BSD-3-Clause and Apache-2.0-with-LLVM-exception files) for terrain triangulation. Its licenses install to `/usr/share/licenses/sketchyup` and are in the release's `THIRD_PARTY_NOTICES` |

Qt, OpenGL and Blender requirements are unchanged from 1.0.0.

### Known limitations

The 1.2.0, 1.1.0 and 1.0.0 limitations below still apply, except that F01, F02 and F03 are
now delivered and these are lifted (see "Other changes"): the sheet-annotation limitation of
1.1.0 and 1.2.0, the focus-ring contrast residuals (D-023, 1.0.0), the fractional-scale focus
ring (D-032, 1.1.0) and the extra cost of moving variant placements (D-039, 1.2.0). For terrain
and site tools:

- **Sources.** DXF contours, point files and model edges only. No LandXML, GeoJSON,
  GeoTIFF/DEM or LAS/LAZ import, and no LandXML export. The CRS label is informational:
  there is no coordinate transform and no latitude/longitude input.
- **One terrain, one outline.** Terrains are not merged, holes are not nested, and a
  terrain cannot grow past its outline or gain base sources after it is created.
- **Editing.** Sculpting moves existing vertices only, so sparse contour meshes look
  faceted; there is no Flip Edge or Add Detail. Projection is vertical and onto terrain
  only. A pad has one cut and one fill slope and no holes. There are no corridors,
  retaining walls or cut and fill volumes, and a pad does not follow its building (use
  **Restamp**).
- **Paint on individual tiles is lost when they are rebuilt.** Regenerate asks first;
  Drape's region paint is the way to keep paint across edits.
- **Limits:** 250,000 terrain triangles a document, 2,048 a tile and 4,096 edits (8 MiB) a
  terrain. The proof that an edit leaves distant terrain untouched was run on terrains of up
  to 60,000 triangles, not at the 250,000 limit.
  Terrain edits have their own 512 MiB undo history; when it fills, the oldest terrain
  undo steps are dropped. A whole-terrain region paint keeps about 140–157 MB in memory
  while it can be undone.
- **Performance is measured on one AMD discrete GPU.** On an RX 6800 XT every one-tile
  commit is within 100 ms (sculpt 55.6–74.0 ms, contour edit 90.2–92.3 ms, stamp 70.2–71.6
  ms, drape 51.6–51.8 ms) and every one-tile preview within 21.9 ms
  ([R096.g](verification/R096g-terrain-editing.md#budgets)). The contour-edit commit has only
  8–10 ms of headroom. Larger edits take longer and have no budget: a sculpt that touches nine
  tiles previews in 39.2–63.3 ms and commits in 236.6–406.0 ms. Under software rendering the
  sculpt, contour-edit and stamp commits take longer than 100 ms. Intel graphics are untested
  (waived, [ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)).
- **The in-app assistant cannot import, edit or explode a terrain**; MCP and the
  transaction APIs can.

The open-defect list is [R089.a](verification/R089a-defect-triage.md).
Extended rows F04–F07 and the Investigate rows remain outside this release.

## 1.2.0

SketchyUp 1.2.0 is a minor release. It adds configurable components, Extended
[scope row F02](SCOPE.md), in its own gated release ([M10 acceptance](M10_ACCEPTANCE.md)).
Existing commands, queries and schemas keep their shapes; everything new is additive,
as a minor version allows ([ADR 0162](decisions/0162-release-versioning.md)). The release
record, with the release-candidate evidence, is [verification/F02-1.2.0.md](verification/F02-1.2.0.md).
The [support matrix](SUPPORT_MATRIX.md) states what is supported. 1.2.0 also contains the
1.1.1 fix (D-036).

### Configurable components

A component definition can have named parameters (a width, a shelf count, a hinge
side) and drivers that tie them to its parts. Each placement keeps its own values, and
its geometry is regenerated from the definition. Formulas are data: a closed
expression language with units, checked before anything changes, that cannot run
programs or read files. The [user guide](USER_GUIDE.md#configurable-components) walks
through the workflow; [ADR 0165](decisions/0165-constrained-component-parameters.md)
defines the parameters, formulas and limits and
[ADR 0166](decisions/0166-configurable-component-interface.md) the interface.

- **Parameters.** Types: length, area, angle, number, count, yes/no and choice. An
  input parameter has a default that placements may override, with optional minimum,
  maximum and step; a derived parameter is a formula over the others (for example
  `width - 60 mm`). A value outside its bounds is refused, never clamped. Formulas
  take `mm`, `cm`, `m`, `in`, `ft`, `deg` and `rad`, arithmetic, comparisons,
  `and`/`or`/`not`, `if`, `min`, `max`, `clamp`, rounding and trigonometric functions,
  and only the definition's own parameter names. Units are checked: a length cannot be
  added to a plain number.
- **Drivers.** Stretch (parts of a piece past a plane move by the change in value),
  Position, Rotation, Visibility, Repeat (copies along an axis, up to 1,000) and Child
  parameter (sets a nested component's value). At the default values the definition
  shows exactly its own geometry.
- **Parameters in Entity info.** Select one placement: **Parameters · this placement**
  in the Tray's Info tab lists its values. Type a value in the Measurements box syntax
  (`1.2m`, `3'11"`); it is checked as you type and applied with Enter or when focus
  leaves. Each change is one Undo step. **↺** or **Ctrl+Backspace** resets one value
  and **Reset all** all of them. With the Select tool, the Measurements box sets the
  last focused parameter (else the first input).
- **Definition authoring.** In a component's context, **Parameters · definition** adds,
  edits, renames, reorders and removes parameters, and **Drivers** lists drivers with
  their state. **Add driver** (**Shift+D**, the Component menu or **+ Driver**) offers
  only the kinds the selected part accepts. The stretch plane is dragged in the
  viewport or typed. Formula fields are checked as you type, point at the character
  in error, and offer a unit fix ("Use 120 mm"). The Preview strip under the banner
  shows the regenerated shape as a ghost for other values without changing the model.
  A default, formula or driver change regenerates every placement in the same edit,
  and keeps scene IDs, materials, the open context and the selection.
- **A placement that has its own values.** Opening it asks how: **Edit definition at
  defaults** (the default) shows the definition at its defaults while you edit and
  restores the placement's values when you close it; **Make unique and edit** makes a
  copy whose defaults are this placement's values.
- **Errors at the control.** Every refusal is shown under the field, form, strip or
  dialog that caused it, in words ("2.6 m is above the maximum, 2.4 m. Nothing was
  changed."), never by colour alone. A refused change leaves the model unchanged. When
  an edit changes a part that a driver acts on, an amber strip says the driver now
  starts from a changed shape and offers **Rebind** and **Undo**; the Drivers list
  keeps the **Changed** state.
- **Assemblies.** Six configurable assemblies ship with SketchyUp: interior door,
  casement window, dining table, bookshelf, base cabinet and straight stair. The new
  **Components** tab in the Tray (View → Components) lists them; choose one, set its
  values and click to place it, following the status bar. The door and the window snap
  to the wall face under the pointer, moving in whole millimetres, and cut their opening
  in the wall in the same step; with a face selected, Enter places them at its centre.
  Placing an assembly that is already in the model reuses its definition. The assemblies are authored by the project, MIT-licensed, and contain no
  third-party models, textures or fonts.
- **Instanced variants.** Placements with equal values share one regenerated geometry,
  so 1,000 placements of one non-default variant cost the same body-cache and GPU
  payload as 1,000 default placements
  ([R094.g](verification/R094g-variant-instancing.md)).
- **SketchUp Dynamic Components are not imported or converted.** SketchyUp neither
  reads nor writes Dynamic Component attributes, and its formulas are not compatible
  with theirs.

### Resizable sidebar

Drag the sidebar's left edge, or the handle between Tray and Assistant, to change its
width. **Ctrl+Alt+]** (View → Widen sidebar) and **Ctrl+Alt+[** (View → Narrow sidebar)
step 40 px; double-clicking a handle or View → Reset sidebar width restores the
default. Each column keeps 240–720 px, scaled by the interface text size, and the
viewport keeps at least 400 px. The width is remembered across launches, in Model and
Sheets alike. Screen readers announce each handle as a split pane; with Qt 6.11 or
later they also report that it is vertical.

### Command line and automation

- **New transaction operations:** `component_parameter.define`, `.update`, `.rename`,
  `.remove`, `.drive`, `.undrive`, `.rebind`, `.override`, `.reset` and `.promote`.
- **New inspection queries:** `component_parameters.describe`,
  `component_parameters.values` and `component_parameters.evaluate` (a read-only dry
  run). MCP, the headless session, `--inspect` and the CLI expose all of them.
- **Additive result and error fields.** Batch results add `createdComponentDrivers`,
  `regenerated {placements, variants}` and `parameterWarnings`; `component.edit_instance`
  reports `parametersPromoted` and `component.replace` `droppedOverrides`. Errors from
  the parameter commands carry the `PARAMETER_*` codes and a `details` object; errors
  without details keep their shape.
- **`--inspect-native`** adds `componentParameters`, `componentDrivers` and
  `componentParameterOverrides` counts.
- **CLI:** `sketchyup-cli --build-assembly SOURCE OUTPUT.sketchylib` builds an assembly
  bundle from its source; the output must be a new path. Two builds of a source are
  byte-identical.
- **In-app assistant: it cannot change component parameters.** Its tool list is
  unchanged from 1.0.x and 1.1.x; it does not see the `component_parameter*` operations
  and queries, which are available over MCP and the transaction APIs. Its existing
  commands follow the parameter rules when they touch a configurable placement (for
  example, editing a placement alone makes its values the copy's defaults).
- The schema files keep their names and versions (`transactions-v1`, `inspection-v1`,
  MCP 2026-07-28 and the others listed under 1.0.0). They gain these entries, and no
  existing response shape changes.

### Assistant

The ChatGPT-plan connection now allows 524,288 reported tokens per task, up from
262,144, still in 16 turns, with no per-reply output cap and 15 minutes per task in
the Assistant panel. Its instructions now guide batched modeling and inspection
([ADR 0041](decisions/0041-chatgpt-plan.md)). Because the provider profile and the
instructions changed, 1.2.0 is evaluated against a new provider corpus,
[version 4](verification/R086e-corpus-v4.md), at the release candidate. Version 4 runs
the same eight tasks through the Assistant panel's native limits, including the
15-minute task limit; the public headless session keeps its 300-second ceiling. The
version 2 results are 1.1.x evidence only.

### File format

1.2.0 writes schema `sketchyup-document-v28` (container version 2, required feature
`component-parameters-v1` after `drawing-sheets-v1`) **only when the document has a
component with parameters**. Otherwise it writes schema 27 (with sheets) or 26
(without), which differ from a 1.1.x save only in the writer string, so SketchyUp 1.1.x
opens them, and 1.0.x opens schema 26. SketchyUp 1.1.x and 1.0.x refuse a schema-28 file
with "Unsupported container field" and leave it unchanged (checked against the
published 1.1.0-1 and 1.0.1-1 packages,
[R093.e](verification/R093e-component-parameters.md#previous-releases)). Removing every
parameter and driver saves the model as schema 27 or 26 again. Component bundles and
templates that contain parameters are schema 28 too. 1.2.0 reads document versions 1
through 28. Files with parameters saved by development builds before 1.2.0 may need to
be re-created, because schema 28 could still change until this release
([ADR 0108](decisions/0108-public-native-format.md#schema-28-component-parameters-r093c)).

### Upgrade from 1.1.1-1

Run `pacman -U sketchyup-1.2.0-1-x86_64.pkg.tar.zst`. pacman orders 1.2.0-1 after
1.1.1-1. Package acceptance upgrades a 1.1.1-1 package built from the release source to
1.2.0-1 and checks that preferences survive. It also checks that the published 1.1.1-1
CLI refuses schema-28 files unchanged and opens a parameter-free file saved by 1.2.0.

To roll back, install `sketchyup-1.1.1-1-x86_64.pkg.tar.zst` with `pacman -U`. Documents
without parameters open in 1.1.1; documents with parameters do not. When 1.2.0 saves
over an existing document, the previous bytes are kept as `NAME.sketchyup.bak`, which
holds only one previous version, so copy any document you may need in 1.1.x before
adding parameters to it.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.2.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.2.0-1, x86_64 |
| Native document | Schema `sketchyup-document-v28` in SKUPDOC container version 2 for documents with component parameters; v27 for documents with sheets and no parameters; v26 otherwise. Reads versions 1 through 28. Saves record the writer `SketchyUp/1.2.0` |
| MCP, inspection, transactions and other public schemas | Same versions as 1.0.0 (below), with the additive sheet (1.1.0) and component parameter operations and queries |
| Assistant provider | OpenAI `gpt-6-astra` over a ChatGPT-plan sign-in, with the limits above. Evaluated at the release candidate against [corpus v4](verification/R086e-corpus-v4.md) (native limits, 15 minutes per task); the results are in the release record |

Qt, OpenGL and Blender requirements are unchanged from 1.0.0.

### Known limitations

The 1.1.0 and 1.0.0 limitations below still apply, except that F01 and F02 are now
delivered. For configurable components:

- **One-way formulas only.** No geometric constraints or solving; editing geometry
  never changes a parameter. A formula reads only its own definition's parameters, not
  another component's, a parent's or a sibling's. There are no per-copy formulas and no
  material, text or tag drivers.
- **Nested placements have no values of their own.** Their values come from their
  defaults or the parent's Child parameter drivers; Make unique and edit is the way to
  change one.
- **Curves and stretch planes.** A circle, arc or pie must lie wholly on one side of
  every stretch plane of its part; a driver or edit that would cross one is refused.
- **Re-baselining is not byte-exact.** Changing a default and changing it back
  (900 → 1200 → 900 mm) may leave last-bit differences in the shape; Undo restores the
  exact records.
- **Edit definition at defaults leaves two History entries** ("Edit *Definition* at
  defaults" and "Restore *placement* values") when something was edited, and none when
  nothing was. Enter or a double-click in the Outliner asks the same question as the
  viewport.
- **The Select tool's digits stay view shortcuts.** Reach the Measurements box with F6,
  Tab or a click to type a parameter value.
- **Limits:** 128 parameters and 256 drivers per definition, 1,024 configurable
  definitions and 4,096 distinct variants per document, 1,024-byte formulas, 8 levels of
  configurable nesting, and a document evaluation budget
  ([ADR 0165 §3](decisions/0165-constrained-component-parameters.md#3-dependency-graph-and-limits)).
- **Performance is measured in software only.** At 10,000 placements of one variant,
  frame and edit times are within run-to-run noise of default placements on llvmpipe,
  which meets neither hardware budget for either kind. Applying a placement move takes
  1.1–1.5 ms with variant placements against 0.4–0.6 ms with default placements. No
  hardware run was made (waived,
  [ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md);
  [R094.g](verification/R094g-variant-instancing.md)).

The open-defect list is [R089.a](verification/R089a-defect-triage.md).
Extended rows F03–F07 and the Investigate rows remain outside this release.

## 1.1.1

SketchyUp 1.1.1 is a patch release for 1.1.0. It fixes one defect and changes no
format, schema or API ([ADR 0162](decisions/0162-release-versioning.md)). The release
check is [F01.a](verification/F01a-1.1.1-patch.md).

### Fix

- **D-036: malformed PDF document information on Qt 6.8.0 and 6.8.1.** `QPdfWriter` in
  Qt 6.8.0 and 6.8.1 ends the PDF document information dictionary with `2` instead of
  `>>`, and Qt 6.8.0 writes the dates as `D:00000000-1-1-1Z` when it cannot determine
  the system time zone. On those Qt versions every sheet PDF (**Export sheets…**,
  `sketchyup-cli --export-sheets`) and every measured PDF export had a malformed
  Info dictionary: PDF readers reported syntax errors and could not read the title,
  creator or dates, although the pages, text, fonts and the attached report were
  intact. SketchyUp now repairs exactly those two patterns in the written file, in
  place and at the same length, before it hashes the file, so the export report's
  SHA-256 still matches. A PDF without the defect, including every PDF written on a
  fixed Qt, is left byte for byte unchanged
  ([R089.a](verification/R089a-defect-triage.md)).

Limits that remain:

- The desktop **Print…** path to a PDF printer goes through `QPrinter`, which uses the
  same Qt engine, and is not repaired. Use **Export sheets…** for a PDF.
- On Qt 6.8.0 with no system time zone (no `TZ` and no `/etc/localtime`), the XMP
  `CreateDate` and `ModifyDate` stay empty. The Info dictionary dates are filled in
  with the export time in UTC.

### Upgrade from 1.1.0-1

Run `pacman -U sketchyup-1.1.1-1-x86_64.pkg.tar.zst`. pacman orders 1.1.1-1 after
1.1.0-1. Preferences, data and cache are kept. The native document format is
unchanged: 1.1.1 writes schema `sketchyup-document-v27` for documents with sheets and
`sketchyup-document-v26` for documents without, and 1.1.0 and 1.1.1 open each other's
documents. The only byte difference in a saved document is the writer string
`SketchyUp/1.1.1`. To roll back, install `sketchyup-1.1.0-1-x86_64.pkg.tar.zst` with
`pacman -U`; the D-036 defect returns on Qt 6.8.0 and 6.8.1.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.1.1 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.1.1-1, x86_64 |
| Native document | Unchanged from 1.1.0. Saves record the writer `SketchyUp/1.1.1` |
| MCP, inspection, transactions and other public schemas | Unchanged from 1.1.0 (below) |

Everything else in the 1.1.0 notes below applies to 1.1.1.

## 1.1.0

SketchyUp 1.1.0 is a minor release. It adds documentation sheets, the first Extended
workflow ([scope row F01](SCOPE.md)), in its own gated release
([M10 acceptance](M10_ACCEPTANCE.md)). Existing commands, queries and schemas keep
their shapes; everything new is additive, as a minor version allows
([ADR 0162](decisions/0162-release-versioning.md)). The release record, with the
release-candidate evidence, is `docs/verification/F01-1.1.0.md`. The
[support matrix](SUPPORT_MATRIX.md) states what is supported.

### Documentation sheets

A sheet is a drawing page stored in the model's own `.sketchyup` file, with views of
the model's saved scenes, dimensions, labels, text and a title block. Editing a page
never changes the model's geometry. The [user guide](USER_GUIDE.md#documentation-sheets)
walks through the workflow; [ADR 0163](decisions/0163-documentation-sheets.md) and
[ADR 0164](decisions/0164-sheet-layout-interface.md) define the records and the
interface.

- **The Sheets workspace.** The **Model | Sheets** switch in the header, View → Sheets
  workspace, or **Ctrl+Alt+S**. Pages have a paper size (A3 landscape by default), a
  sheet number and a margin, and the Pages section of the Tray lists them. Every page
  operation is one undo step in the model's history, and saving the model saves its
  pages.
- **Linked views.** A view shows a saved scene at a declared scale (for example 1:50),
  cropped by its frame. The scale is never derived from the camera or the geometry. A
  view always draws the current model. When the model changes, the view shows
  **Changed** ("changed since accepted") until you accept it (**U**, or **Accept
  all**). A view whose scene was deleted is **Broken**; **Choose scene…** relinks it
  and keeps its frame and scale. A perspective scene gives an illustration that is not
  to scale.
- **Page items.** Lines, rectangles, text, labels with or without a leader, and
  dimensions measured from the model at the view's scale. A dimension stays bound to
  its model points, so it updates when the geometry moves.
- **Title blocks.** Insert → Title block. Project, drawing title, date and revision are
  typed; the sheet number, the page "n of N" and the scale fill in by themselves.
- **Reattach.** A dimension or label whose model point no longer exists turns red and
  reads "[Missing reference]". **Reattach** in Entity info proposes the nearest point
  in the view, and you confirm it or pick another.
- **Dimension format per sheet.** With nothing selected, Entity info sets the page's
  dimension unit (millimetres, centimetres, metres or feet and inches), its decimals
  (0–4) and whether the unit is shown. The default is whole millimetres without a unit.
- **PDF export with preflight.** File → **Export sheets…** (in Model too, once the
  model has pages). **Checked before publishing** lists the findings before anything is
  written: a broken view or a view changed since it was accepted blocks the export
  until you fix it or choose to export it anyway, and missing fonts are named with
  their substitute (**Choose another font**). **Go to view** and **Review and accept**
  take you to the view to fix. Each page is written at its own paper size and actual
  size; text is real, selectable text with embedded font subsets, and lines stay
  vector. On the desktop, perspective and rendered views are drawn from the viewport
  as images at 300 dpi. The export runs behind the file-operation progress surface,
  writes a new file, never replaces one, and leaves no file if it fails.
- **Export report.** After an export, the report sheet lists the file, the pages,
  any views exported as they are now or as empty frames, the raster views with their
  DPI and the font substitutions. **Show the reconciliation report (JSON)** shows the
  full record, which is also attached to the PDF as `sheet-export-report.json`.
- **Print.** File → **Print…** in Sheets opens the same dialog and runs the same
  checks; **Choose…** opens the system print dialog for the printer and paper. Every
  page prints at 100% from the paper's top-left corner and is never scaled to fit.
  When a page is larger than the paper, the dialog warns before you print, and the
  page is cut off at the paper's edge.

Limits: 256 sheets, 1,024 views (64 per sheet) and 4,096 page items (512 per sheet)
per document.

### Command line and automation

- **CLI:** `sketchyup-cli --export-sheets OUT.pdf --input MODEL [--pages RANGE]
  [--allow-broken] [--allow-changed] [--preflight-only]`. `--preflight-only` prints
  the findings and writes nothing. A blocked export exits 1 with `EXPORT_BLOCKED`.
- **New transaction operations:** `drawing_sheet.create`, `.rename`, `.resize`,
  `.reorder`, `.update` and `.delete`; `drawing_view.create`, `.update`, `.move`,
  `.delete` and `.accept`; `drawing_item.create`, `.update`, `.move`, `.reorder`,
  `.reattach` and `.delete`.
- **New inspection queries:** `drawing_sheets.query`, `drawing_sheet.describe`,
  `drawing_views.status`, `drawing_view.describe`, `drawing_items.query`,
  `drawing_item.describe`, `drawing_item.reattach_candidates` and
  `drawing_sheets.export_preflight`. The export preflight query needs the font worker,
  so the headless `--session`, `--mcp` and `--inspect` modes return
  `UNAVAILABLE_CONTEXT` for it. Exporting a PDF is not an automation operation.
- **In-app assistant:** its tool list is unchanged from 1.0.x. The assistant does not
  see or run the sheet operations and queries; they are available over MCP and the
  transaction APIs, so external agents can still work with sheets.
- The schema files keep their names and versions (`transactions-v1`,
  `inspection-v1`, MCP 2026-07-28 and the others listed under 1.0.0). They gain these
  entries, and no existing response shape changes.

### File format

1.1.0 writes schema `sketchyup-document-v27` (container version 2, required feature
`drawing-sheets-v1`) **only when the document has sheets**. A document without sheets
is still written as schema 26 and differs from a 1.0.x save only in its writer string,
so SketchyUp 1.0.x opens it. SketchyUp 1.0.x refuses a schema-27 file with
"Unsupported container field" and leaves it unchanged; it does not open the model
without its sheets. Deleting every page saves the model as schema 26 again, so to share
a model with someone on 1.0.x, save a copy, delete its pages and save the copy. 1.1.0
reads document versions 1 through 27. Sheet files saved by development builds before
1.1.0 may need to be re-created, because schema 27 could still change until this
release.

### Upgrade from 1.0.1-1

Run `pacman -U sketchyup-1.1.0-1-x86_64.pkg.tar.zst`. pacman orders 1.1.0-1 after
1.0.1-1. Package acceptance seeds preferences, upgrades a 1.0.1-1 package built from
the release source to 1.1.0-1, and checks that the preferences survive. It also checks
that the published 1.0.1-1 CLI refuses schema-27 files unchanged and opens a sheet-free
file saved by 1.1.0.

To roll back, install `sketchyup-1.0.1-1-x86_64.pkg.tar.zst` with `pacman -U`.
Documents without sheets open in 1.0.1; documents with sheets do not. When 1.1.0 saves
over an existing document, the previous bytes are kept as `NAME.sketchyup.bak`, which
holds only one previous version, so copy any document you may need in 1.0.x before
adding pages to it.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.1.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.1.0-1, x86_64 |
| Native document | Schema `sketchyup-document-v27` in SKUPDOC container version 2 for documents with sheets; `sketchyup-document-v26` for documents without. Reads versions 1 through 27. Saves record the writer `SketchyUp/1.1.0` |
| MCP, inspection, transactions and other public schemas | Same versions as 1.0.0 (below), with the additive sheet operations and queries |
| Assistant provider | Unchanged from 1.0.0. The provider corpus is evaluated at the release candidate; the results are in the release record |

Qt, OpenGL and Blender requirements are unchanged from 1.0.0.

### Known limitations

The 1.0.0 limitations below still apply, except that F01 is now delivered. For
sheets:

- **Edge splits and Make component break paper dimensions.** Paper dimensions and
  labels resolve their model points by ID, and model edits never rewrite sheet
  records, so splitting an anchored edge or making the geometry into a component
  breaks them where a model annotation would survive. **Reattach** repairs them
  ([R091.h](verification/R091h-sheet-layout.md#known-limitations)).
- **Raster views use the viewport's visibility.** Perspective and rendered views are
  captured with the editor's current tag and hidden-object visibility, not the
  scene's saved visibility.
- **The CLI cannot render raster views.** It has no viewport, so `--export-sheets`
  reports perspective and rendered views as `rasterUnavailable`, which blocks the
  export; with `--allow-broken` they are written as empty, labelled frames. Export
  them from the desktop.
- **The PDF page box is rounded to whole points.** A page is within 0.5 pt (0.18 mm)
  of its paper size; drawing coordinates, and so the measured scale, are exact.
- **One model per sheet set.** Sheets live inside one model's file and show only that
  model's scenes.
- **D-031:** `keyboard_reachability_tests` once exited on Wayland without a check
  message, with a Weston "finalizing a layer with views still on it" line (test
  infrastructure, unverified).
- **D-032:** at a fractional 1.5× scale, focus rings fall under the 3:1 contrast
  threshold; the ring is still drawn, keyboard focus works, and the 1× and 2× scales
  pass.

The open-defect list is [R089.a](verification/R089a-defect-triage.md).
Extended rows F02–F07 and the Investigate rows remain outside this release.

## 1.0.1

SketchyUp 1.0.1 is a patch release for 1.0.0. It fixes one defect and changes no
format, schema or API. The release check is
[R089.g](https://github.com/zygote55/sketchyup/blob/release/1.0.x/docs/verification/R089g-1.0.1-patch.md).

### Fix

- **D-030: `annotation.create` through `transaction.apply`.** In 1.0.0, every
  `annotation.create` sent through MCP `transaction.apply`, a headless `--session` or
  a recipe step failed with `INVALID_REQUEST` "Parameter must match exactly one
  supported schema". The shared request validator ignored `properties`, `required`
  and bounds in a schema without `type`, and both `oneOf` choices of
  `annotation.create` (distance and label) are such schemas, so both matched every
  request. A schema without `type` now applies its keywords to the value it meets.
  The desktop Annotations panel and `--script` batches did not use this path and were
  not affected. Invalid annotation requests are still rejected
  ([R089.a](verification/R089a-defect-triage.md)).

### Upgrade from 1.0.0-1

Run `pacman -U sketchyup-1.0.1-1-x86_64.pkg.tar.zst`. pacman orders 1.0.1-1 after
1.0.0-1. Preferences, data and cache are kept. The native document format is
unchanged: 1.0.1 still writes schema `sketchyup-document-v26` in container version 2,
and 1.0.0 and 1.0.1 open each other's documents. The only byte difference in a saved
document is the writer string `SketchyUp/1.0.1`. To roll back, install
`sketchyup-1.0.0-1-x86_64.pkg.tar.zst` with `pacman -U`; the D-030 defect returns.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.0.1 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.0.1-1, x86_64 |
| Native document | Schema `sketchyup-document-v26` in SKUPDOC container version 2, unchanged. Saves record the writer `SketchyUp/1.0.1` |
| MCP, inspection, transactions and other public schemas | Unchanged from 1.0.0 (below) |

Everything else in the 1.0.0 notes below applies to 1.0.1.

## 1.0.0

SketchyUp 1.0.0 is the first release. Every earlier build was the 0.1.0 development
version ([ADR 0162](decisions/0162-release-versioning.md)). The release gate record,
with the release-candidate evidence, is [verification/M9.md](verification/M9.md).
The [support matrix](SUPPORT_MATRIX.md) states what is supported and what is untested.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.0.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.0.0-1, x86_64 |
| Qt | Build requires Qt 6.8 or later. CI and the package build use Qt 6.11.2. The package runs on clean Arch machines with Qt 6.8.0 through 6.12.0 ([R087.u](verification/R087u-clean-machine-packages.md)) |
| OpenGL | 3.3 core profile |
| Native document | Schema `sketchyup-document-v26` in SKUPDOC container version 2 ([ADR 0108](decisions/0108-public-native-format.md)). Saves record the writer `SketchyUp/1.0.0` |
| MCP | Protocol version 2026-07-28 (`docs/api/mcp-2026-07-28.json`, `docs/api/mcp-native-2026-07-28.json`) |
| Inspection | v1 (`inspection-v1.json`, `inspection-session-v1.json`, `inspection-desktop-v1.json`) |
| Transactions | v1 (`transactions-v1.json`) |
| Other public schemas | `recipe-v1`, `headless-session-v1`, `extensions-v1`, `tool-reference-v1`, `native-format-v1` |
| Blender (optional) | 5.2 LTS family; the worker rejects other versions. CI installs Arch `blender` 17:5.2.2-2 (Blender 5.2.2) |
| Assistant provider | Supported: OpenAI `gpt-6-astra` through a ChatGPT-plan sign-in. Experimental: local Ollama ([ADR 0160](decisions/0160-provider-scope-for-1-0.md)) |

The schemas are installed under `/usr/share/doc/sketchyup/api`. After 1.0.0, a
major version may drop reading support for a native format or break one of these
schemas; minor and patch versions do not ([ADR 0162](decisions/0162-release-versioning.md)).

### What 1.0 does

SketchyUp is a native Linux face modeler built with Qt Widgets and OpenGL. It needs
no network, account, browser runtime or Blender for ordinary modeling.

- Draw, measure and edit faces and solids with typed measurements, inference,
  groups, components, tags, materials, textures, sections, annotations, text,
  scenes and styles.
- Save `.sketchyup` documents with undo, automatic recovery and a backup of the
  previous save. Validate and migrate older documents from the CLI.
- Exchange through GLB/glTF, OBJ/MTL, STL, a DXF subset, measured PDF/SVG,
  PNG, Formline import and one-way Blender handoff. Each conversion reports its
  losses.
- Drive the model through the MCP server, recipes, transactions and inspection
  APIs, and through the optional assistant panel.

The 59 Core rows of the [scope matrix](SCOPE.md) are the 1.0 scope. The
[user guide](USER_GUIDE.md) describes the workflows. Extended and Investigate rows
are not part of 1.0, and 1.0 does not claim full SketchUp parity.

### Install and upgrade

Install the package with `pacman -U sketchyup-1.0.0-1-x86_64.pkg.tar.zst`. pacman
orders 1.0.0-1 after the development package 0.1.0-3, so the same command upgrades
an existing 0.1.0-3 installation.

- **Preferences are kept.** The package installs only under `/usr` and does not
  touch the settings file (`~/.config/SketchyUp/SketchyUp.conf`). Package
  acceptance seeds preferences, upgrades a 0.1.0-3 package built from the release
  source to 1.0.0-1, and checks that the preferences survive. The upgrade across
  application revisions, including shortcut bindings and unknown settings, was last
  checked between development packages ([R087.u](verification/R087u-clean-machine-packages.md)).
- **Older documents open.** 1.0.0 reads document versions 1 through 26. Saving
  writes version 26. To keep the original file unchanged, migrate it to a new path:

  ```
  sketchyup-cli --validate-native old.sketchyup
  sketchyup-cli --migrate-native old.sketchyup --output migrated.sketchyup
  ```

  Migration refuses an existing destination and never converts in place.
- **Match the package to your system.** The PKGBUILD's dependencies carry no
  versions. pacman therefore installs a package built against newer Qt or glibc on
  an older system, where it fails to start ([R087.u](verification/R087u-clean-machine-packages.md)).
  Install a package built for your repository snapshot.

### Rollback

No earlier release exists, so a rollback goes to a 0.1.0 development build.

- A build that predates schema 26 (development builds of `main` before PR #284,
  merged 2026-10-10) rejects every document saved by 1.0.0 as an unsupported format
  and leaves the file unchanged. Readers never load a newer format on a best-effort
  basis ([ADR 0108](decisions/0108-public-native-format.md)).
- When 1.0.0 saves over an existing valid document, it first copies the previous
  bytes unchanged to `NAME.sketchyup.bak`. A document last saved by an older build
  therefore keeps its older format there. The next save replaces that backup, so it
  holds only one previous version. Copy any document you may need in an older build
  before saving it in 1.0.0.
- Removing the package leaves documents, preferences, data and cache in place.

### Known limitations

No known defect is release-blocking. The open-defect list is
[R089.a](verification/R089a-defect-triage.md); the release-candidate triage is in
[verification/M9.md](verification/M9.md).

- **Untested for 1.0** by owner waiver ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)):
  Intel graphics campaigns, the sharing-off memory control on hardware, X11 output
  hotplug, suspend/resume, a physical 150% output, physical pointer capture, input
  methods, D-020 on Hyprland, automatic X11 output placement and an interactive Orca
  session. These are neither claimed to work nor known to fail. Wayland hotplug is
  owner-attested ([R085.r](verification/R085r-hotplug-owner-attested.md)).
- **D-004:** the Intel integrated GPU missed the 16.7 ms complete-frame target on
  100k-triangle scenes under one CPU (p95 17.3–19.6 ms) and was not remeasured.
- **D-005:** a 186–282 ms progress gap while opening on Intel/Wayland was not
  remeasured after the fix that brought AMD/X11 gaps below 50 ms.
- **Performance evidence** at the current revision is from an AMD discrete GPU only
  ([R082.ii](verification/R082ii-amd-hardware-session.md)).
- **Assistant:** Scope row A05 is met by the remote OpenAI profile only. The local
  Ollama adapter is experimental: the measured profile fails the frozen corpus
  (D-001), and it carries no quality guarantee, automatic download or cloud
  fallback. Remote model output is probabilistic, and acceptance covers one
  configuration (D-018).
- **Exchange** is conversion, not native identity. There is no native SKP or DWG
  support, and the DXF subset is not DWG compatibility (D-015). Blender handoff is
  one-way, and animation exports PNG frames without movie encoding (D-016).
- **Geometry** has finite planar and sweep limits; retained internal faces may be
  non-manifold (D-017).
- **External defects:** a Wayland 1.26 client proxy leak (D-010), a Qt Wayland
  decoration framebuffer leak (D-011), a compositor crash with RGB565 surfaces that
  the eight-bit surface policy avoids (D-012), a Qt crash with no installed fonts,
  avoided by the `ttf-dejavu` dependency (D-013), and a Weston 15.0.1 crash on a
  progress popup for a minimized window (D-020).
- **Recovery** evidence uses injected faults, not physical power loss (D-014).
- **Minor defects:** D-006 (intermittent X11 viewport assertion), D-019 (an
  unreachable Wayland re-show path), D-023 (focus-ring contrast next to the 3D
  canvas and native scroll bars), and test-infrastructure items D-007, D-008, D-009,
  D-021 and D-022.
- **Not in 1.0:** the Extended rows F01–F07 (LayOut-style documents, configurable
  components, terrain, IFC/STEP, more animation output, style and asset authoring,
  reports) and the Investigate rows C01–C07 (native SKP, Ruby extensions, Dynamic
  Components, 3D Warehouse, DWG, online maps, collaboration). See the
  [scope matrix](SCOPE.md).
