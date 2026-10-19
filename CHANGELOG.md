# Changelog

## Unreleased

Changes on `main` since 1.2.0. R095.a adds native schema 29 (`terrain-v1`) and new
site and terrain commands and queries; documents without a terrain or site origin keep
their 1.2.0 schema.

- Terrain core (R095.a, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md)):
  a terrain body with its sources and tiled triangle mesh (at most 2,048 triangles a
  tile, 250,000 a document, outside the 100,000-face modelling budget), a document site
  origin with an informational CRS label, schema 29 with a binary `terrain-mesh` chunk,
  and the `site.set`, `site.reanchor`, `terrain.explode` commands and `site.describe`,
  `site.convert`, `terrain.describe`, `terrain.elevation_at` queries in automation
  sessions and MCP (not yet in the in-app assistant). Generic topology tools refuse terrain tiles;
  Explode terrain turns a terrain into ordinary geometry. No importer or desktop UI yet.

- Terrain import (R095.c, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095c)):
  `terrain.import` adds a terrain from a DXF (contours, breaklines, boundary and holes from
  LWPOLYLINE, 2D and 3D POLYLINE and LINE, arcs segmented) or a CSV/XYZ/PNEZD point file
  (delimiter, header and columns detected or chosen; description codes as ground points or
  breaklines) to the open document in one undoable edit, establishing the site origin from
  the proposed origin (data centre rounded to 100 m) when the document has none. Units
  include the US survey foot (1200/3937 m); conversion is explicit and reported.
  `terrain.inspect_source` previews a source without changing anything. Crossing contours,
  conflicting duplicates and other invalid input refuse the import with a bounded, located
  report (at most 32 locations per kind, 64 KiB). The CLI gains `--import-terrain` with
  `--terrain-units`, `--origin` and the point-file and role options. Also fixes a hang in
  face tessellation: a large triangle face could send Clipper2's Delaunay triangulation into
  unbounded memory growth; triangle faces are now their own tessellation.
- Terrain: the triangulation adapter for terrain import (R095.b, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095b)):
  contours, breaklines, points, a boundary and holes in local metres become a constrained
  Delaunay mesh, with flat triangles flipped and the rest reported, optional Steiner fill, and
  bounded refusals that name crossing constraints, conflicting duplicates and too few points.
  Vendors CDT 2.0.0 (MPL-2.0, header-only) under `third_party/cdt`. Nothing user-visible yet:
  no command, file or format uses it until the terrain records land.
- Sheets: paper dimensions and leader labels now survive edge splits (including face
  splits that split their edge) and make component, as model annotations survive edge
  splits: the model edit moves their anchors to the split piece, the new vertex or the
  component's geometry in the same Undo step. Where that is ambiguous they still show as
  broken and **Reattach** repairs them. This lifts the known limitation disclosed in 1.1.0
  and 1.2.0 ([ADR 0164 addendum](docs/decisions/0164-sheet-layout-interface.md#addendum-2026-10-17-paper-anchor-lineage)).
- Annotations: model dimensions and labels now survive make component and make group or
  component from a selection. Their vertex, edge and face anchors follow the geometry into
  its new context, in the same Undo step, by the geometry-transfer rule paper anchors use;
  where that is ambiguous they still break as before
  ([ADR 0164 addendum](docs/decisions/0164-sheet-layout-interface.md#addendum-2026-10-17-paper-anchor-lineage)).

- Tool rail: below 800 px the Model rail shows one group of tools (Select, Draw, Modify,
  Measure or Camera) behind a group button, the first item on the rail. Choosing a group
  does not change the active tool; picking a tool by shortcut, menu or command search
  switches to its group. From 800 px the rail is as before.

## 1.2.0 — 2026-10-17

A minor release that adds configurable components (scope row F02). Additive: existing
commands, queries and schemas keep their shapes.

- Configurable components: typed parameters with bounds and unit-checked formulas,
  and Stretch, Position, Rotation, Visibility, Repeat and Child parameter drivers.
  Placements keep their own values; equal values share one instanced variant.
- Desktop: placement parameters in Entity info, definition authoring with the
  parameter and driver lists, Add driver (Shift+D), a dragged or typed stretch plane,
  formula fields checked as you type and a Preview ghost; the "has its own values"
  dialog (Edit definition at defaults, Make unique and edit); refusals shown at their
  control; Rebind for drivers whose part changed.
- Six bundled assemblies (interior door, casement window, dining table, bookshelf, base
  cabinet, straight stair) in a new Components tab (View → Components), placed with
  values; doors and windows cut their opening when placed, and placing one already in
  the model reuses its definition. CLI: `sketchyup-cli --build-assembly`.
- Resizable sidebar: drag the handle, Ctrl+Alt+] / Ctrl+Alt+[ or View → Reset sidebar
  width; 240–720 px per column, remembered.
- API: new `component_parameter.*` transaction operations and
  `component_parameters.*` inspection queries over MCP, the headless session and the
  CLI. The in-app assistant cannot change component parameters.
- Assistant: the ChatGPT-plan budget is 524,288 tokens per task with no per-reply
  output cap; 1.2.0 is evaluated against provider corpus v4.
- Format: schema 28 (`component-parameters-v1`) is written only for documents with
  component parameters. SketchyUp 1.1.x and 1.0.x refuse those files unchanged and
  still open parameter-free ones.
- SketchUp Dynamic Components are not imported or converted.

Tag `v1.2.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.2.0).
- Package: Arch `sketchyup` 1.2.0-1, upgrading from 1.1.1-1.

## 1.1.1 — 2026-10-16

Patch release from `release/1.1.x` ([GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.1.1)).
No format, schema or API change.

- Fixed D-036: on Qt 6.8.0 and 6.8.1, sheet PDF exports (desktop and
  `sketchyup-cli --export-sheets`) and measured PDF exports had a malformed document
  information dictionary, because `QPdfWriter` closes it with `2` instead of `>>`
  (and Qt 6.8.0 writes invalid dates without a system time zone). SketchyUp now
  repairs exactly those patterns in place, before the export report's SHA-256 is
  computed; other PDFs are unchanged. The desktop Print path through `QPrinter` is not
  repaired, and on Qt 6.8.0 without a time zone the XMP dates stay empty.
- Package: Arch `sketchyup` 1.1.1-1, upgrading from 1.1.0-1.
- The same fix reached `main` in #334.

## 1.1.0 — 2026-10-15

A minor release that adds documentation sheets (scope row F01). Additive: existing
commands, queries and schemas keep their shapes.

- Documentation sheets: the Sheets workspace with pages, linked views of saved scenes at
  a declared scale and a "changed since accepted" state, lines, rectangles, text,
  labels, referenced dimensions with Reattach, title blocks and a per-sheet dimension
  format.
- PDF export of sheets with a preflight and a reconciliation report, and printing at
  100%. CLI: `sketchyup-cli --export-sheets`.
- API: new `drawing_sheet.*`, `drawing_view.*` and `drawing_item.*` transaction
  operations and drawing-sheet inspection queries, including
  `drawing_sheets.export_preflight`.
- Format: schema 27 (`drawing-sheets-v1`) is written only for documents with sheets.
  SketchyUp 1.0.x refuses those files unchanged and still opens sheet-free ones.
- Package: Arch `sketchyup` 1.1.0-1, upgrading from 1.0.1-1.

Tag `v1.1.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.1.0).

## 1.0.1 — 2026-10-14

Patch release from `release/1.0.x` ([GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.0.1)).

- Fixes D-030: `annotation.create` sent through MCP `transaction.apply`, a headless
  `--session` or a recipe step was always rejected, because the request validator ignored
  the rules of `oneOf` choices without a `type`. No format, schema or API change; the
  package upgrades from 1.0.0-1 and keeps preferences.
- The same fix reached `main` in R090.e (#314).

## 1.0.0 — 2026-10-13

The first release of SketchyUp, a native Linux face modeler. Earlier builds were
the 0.1.0 development version and Arch development packages 0.1.0-1 through 0.1.0-3.

- Native modeling: faces and solids with typed measurements, inference, groups,
  components, tags, materials, textures, sections, annotations, text, scenes and
  styles; undo, automatic recovery and a backup of the previous save.
- Native document schema 26 in container version 2. The CLI validates and migrates
  older documents to a new path.
- Exchange: GLB/glTF, OBJ/MTL, STL, a DXF subset, measured PDF/SVG, PNG, Formline
  import and one-way Blender 5.2 LTS handoff.
- Automation: MCP (protocol 2026-07-28), recipes, transactions and inspection v1.
- Assistant: OpenAI through a ChatGPT-plan sign-in is supported; local Ollama is
  experimental.
- Package: Arch `sketchyup` 1.0.0-1, upgrading from 0.1.0-3.

Tag `v1.0.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.0.0).

Versions, upgrade and rollback notes and known limitations:
[release notes](docs/RELEASE_NOTES.md). Support boundaries:
[support matrix](docs/SUPPORT_MATRIX.md).
