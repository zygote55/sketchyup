# Sheet layout interface

Status: **Accepted (2026-10-14)**; the layers are implemented by R091.a–h. Roadmap [R091](../PR_ROADMAP.md#r091) (M10, scope
row [F01](../SCOPE.md)). This builds on [ADR 0163](0163-documentation-sheets.md)
(R090) and shapes [R092](../PR_ROADMAP.md#r092). The interaction design is the
owner-approved
[R091 UX design (canvas; private to the owner)](https://claude.ai/artifact/ByrL3RtnQihKV9zx5DiYyQ).
This ADR does not repeat that design. It decides how the code delivers it, names
where the design meets the code or [UX_DESIGN](../UX_DESIGN.md) awkwardly, and
splits the work into layers. The owner's choices are recorded under "Decisions".

## Context

R091 must "add multi-page layout tools, alignment/snapping, title blocks,
dimensions and annotations with keyboard access and undo". Its Verify line: "a
small drawing set can be laid out and revised; referenced dimensions update and
page editing never modifies source model geometry".

### What R090 provides

ADR 0163 and PRs #310–#313 are on `main`; R090.e, the acceptance record, is PR
#314.

- **Records.** `DrawingSheetRecord {id, name, position, paper}` and
  `DrawingViewRecord {id, name, sheet, source, frame, offsetMm, scaleDenominator,
  content, hiddenLines, accepted}` (`src/core/drawing_sheet_records.hpp`).
  - Both tables are siblings at the document root.
  - View IDs are global.
  - The file is schema 27 only when sheets exist (decision 2), and schema 27
    stays open on `main` until the F01 release (decision 6).
- **Edits.** The builders in `src/core/drawing_sheets.hpp` tag each edit with
  `drawingSheetScope`.
  - `Document::apply` enforces `isDrawingSheetOnlyEdit`.
  - `executeBatch` (`src/automation/commands.cpp:615-627`) refuses a batch that
    mixes sheet and model operations. It publishes one edit, using the optional
    `metadata.label` as the history label.
- **Evaluation.** `evaluateDrawingView` and `evaluateDrawingViewDrawing`
  (`src/io/drawing_view.hpp`) return state, reasons, `notToScale`, the
  projection and the `MeasuredDrawing`.
  - `DrawingViewStatusCache` is pure: it has no threads.
  - `drawingFingerprint` hashes frame-local geometry plus the page *size*, so
    translating a frame does not change it.
- **Commands.** `drawing_sheet.*`, `drawing_view.*` and `drawing_view.accept`
  (`src/automation/drawing_sheet_commands.cpp`).
  - `replaceView` evaluates the view *synchronously*, before and after, to apply
    the re-stamp rule.
- **Templates.** Still open from ADR 0163: `instantiateTemplate` drops sheet
  tables.

### What the desktop has today (surveyed for this ADR)

- **Window.** `Window : QMainWindow` (`src/app/window.cpp`, 1,437 lines; the
  `window*.cpp` set is about 4,800).
  - The central widget is a `QVBoxLayout`: header, recovery status, save
    banner, content row, footer.
  - The content row holds:
    - the `#toolRail` `QToolBar`;
    - `#modelTabs`, a `QTabWidget` with the GL viewport as tab 0 and render
      results as closable tabs;
    - `sideTabs_` (Tray | Assistant).
  - There is no workspace or mode concept and no segmented-control widget.
    `window_assistant.cpp:27` warns: "Reparent only the assistant, never the live
    GL viewport".
- **Viewport.** `Viewport : QOpenGLWidget` with GL 3.3 core. It paints QPainter
  overlays after native GL painting.
  - Picking is GPU colour-ID into an FBO, scaled by `devicePixelRatioF()`.
  - `renderMeasuredView` (`viewport_measured.cpp`) captures from the *editor*
    camera and live `doc_`. Its raster path calls `makeCurrent()` on the
    viewport's context.
- **Breadcrumb and banner.** `breadcrumb_` and `componentBanner_` are `QLabel`s
  parented to the viewport (`window.cpp:207-234`). Esc and Enter for contexts
  are handled in `Viewport::selectionKey`.
- **Tool rail.**
  - Tools are created by the `addTool` lambda (`window.cpp:458-468`). Each is a
    checkable public action with id `tool.<enum ordinal>` in one exclusive
    `QActionGroup`.
  - The rail is text-only, with no icons, groups or flyouts.
  - `installToolbarNavigation(toolbar, group)` gives one roving Tab stop.
- **Registry and shortcuts.**
  - `Window::action(id, …)` (`window.cpp:121-137`) throws "Duplicate public
    action identity or shortcut" on *any* shortcut collision.
  - `ShortcutBindings` (`src/app/shortcut_bindings.*`) rejects duplicate
    defaults and checks conflicts globally. Its only scope notion is
    `panelGuard` (ADR 0153).
  - Validation reserves Esc, Tab, Backtab, Return, Enter and the arrow keys
    (ADR 0148).
  - Plain and Shift-only keys become `Qt::WidgetShortcut` on the viewport
    (`window.cpp:860-867`, `window_shortcuts.cpp:68-86`).
  - Settings key: `shortcuts/v1`.
- **Focus and keys.**
  - F6 and Shift+F6 cycle `focusRegions_ = {search, tools, viewport, organization,
    assistant, measurements}` (`window.cpp:841`).
  - The viewport claims digits for the Measurements box through
    `ShortcutOverride`.
  - With Select, Tab and Backtab cycle entities.
- **Status bar.** The footer holds:
  - `status_`, the instruction text;
  - the render job chip;
  - `measurementUnits_`, which reads "Measurements · unit";
  - `measurements_`, the box.

  There is no active-tool label. Grammar: `parseMeasurements`, `parseLength`
  (`src/automation/measurements.*`). Re-entry: `ToolSession` with
  `Document::AmendStamp` and `executeAmend`.
- **Tray.** It is not a stack of collapsible sections.
  - `OrganizationPanel` is one `QTabWidget` with 12 tabs (Outliner … Images).
  - Its focus proxy maps hard-coded tab *indexes* to panels
    (`organization_panel.cpp:331-344`).
  - Panels are `QWidget`s that call the shared commands through
    `Viewport::commitCommands` and refresh from `Window::sync()`.
  - `ReportSheet` is a header-only `QDialog`.
- **Theme.** `ThemeColors` (`src/app/theme.hpp`) has nine tokens (surface, ink,
  border, selected, hover, input, accent, muted, canvas). Selection blue-violet
  (`#5c4fd4`, `#6354d4`), amber (`#b9762f`) and red (`#bc4343`) exist only as
  literals in viewport code. There is no hatch helper, no tabular-numeral font
  and no 28 px grid constant.
- **Model annotations** (ADRs 0078–0081).
  - Anchors: `AnnotationAnchor {kind point|vertex|edge|face, body, entity,
    parameter, vertices, weights, fallback, state}`.
  - Model edits remap anchors *inside the same edit*, through topology lineage
    (`resolveAnnotationEdit`, `model.cpp:1466`).
  - `resolveAnnotationAnchor` is a pure ID lookup with no lineage.
  - Dimensions are authored from the Annotations panel, not with a viewport
    tool.
  - Text is QPainter at a logical-pixel size. Export uses "DejaVu Sans" glyph
    outlines and counts `missingGlyphs`.
- **Export** (R078).
  - `paths(const MeasuredDrawing&)` is anonymous in `measured_export.cpp`.
  - PDF goes through `QPdfWriter` and QPainter in millimetres. SVG is written by
    hand.
  - `sketchyup-cli` links Qt Gui but not Qt Widgets.
- **Tests.**
  - About 50 native `*_input_tests` are built by `sketchyup_native_fixture()` and
    listed one by one in `.github/workflows/native.yml`:
    - X11 under `xvfb-run` at `QT_SCALE_FACTOR` 1 and 2;
    - Wayland under `scripts/test-wayland.sh` with `SKETCHYUP_TEST_SCALE` 1 and 2;
    - fractional 1.5 and 1.75 for 5 suites;
    - a us/de/fr keyboard-layout matrix.
  - Keyboard and accessibility suites: `keyboard_reachability_tests`,
    `native_accessibility_audit` (dialog cases), `visible_focus_input_tests`,
    `shortcut_input_tests`, and `tests/accessibility_bridge_test.py` (AT-SPI).

## 1. Page canvas widget

### Options

**(a) Custom `QWidget` + `QPainter`** (`SheetCanvas`). Paper millimetres are the
model space, and one transform maps millimetres to logical pixels.

- **HiDPI and fractional scaling (N04).** Exact. QPainter on a widget already
  paints in logical pixels with the device ratio applied, so vector paths stay
  sharp at 1.5× and 1.75×. Cached raster content is built at
  `devicePixelRatioF()` with `QPixmap::setDevicePixelRatio`. There is no GL
  surface format, so the Wayland RGB565 crash class of ADR 0157 does not apply.
- **Accessibility.** We write one `QAccessibleWidget` subclass. It gives role
  Canvas, the page as name, and the focused item as description, and it sends
  focus and description events on Tab cycling. This has the same reach as the
  viewport (ADR 0155), and the Tray tree is the full alternative (§4).
- **Hit-testing at zoom.** Done in millimetres. The tolerance is a fixed number of
  logical pixels divided by the current pixels per millimetre, so targets stay
  the same size at any zoom, as UX_DESIGN §2 and §4.4 require. A page has at most
  64 views and 512 items, so a linear pass over items is cheap. Snap candidates
  from view line geometry use a uniform grid built once per evaluated drawing.
- **Printing reuse (R092).** The page content is painted by a Qt-Gui-only
  compositor, `paintDrawingSheet(QPainter&, …)`. It works on any
  `QPaintDevice`: the widget, a `QImage`, or the existing `QPdfWriter`. R092 calls
  the same function per page, and the CLI can too, because it needs no Widgets.
  Screen-only decorations are painted by the canvas *after* the compositor: the
  "⟳ Changed" chip, selection, handles, grid, broken hatch and the dimmed
  edit-view surround. The margin frame is part of the drawing: it is the printed
  drawing border the title block attaches to, so the compositor paints it
  (`marginFrame`, on by default; R091.d). So the decision 4 rule that "the chip never prints"
  holds by construction.
- **Cost.** We write the item model, z-order, selection and handles ourselves.
  That code is small and is the same code the compositor and the commands need
  anyway.

**(b) `QGraphicsView` / `QGraphicsScene`.**

- Gains: free item hit-testing, a BSP index, rubber-band selection and zoom
  transforms.
- `QGraphicsItem`s are **not exposed to accessibility** at all, so it gives no
  accessibility benefit over (a).
- `QGraphicsScene::render` can paint to a `QPdfWriter`, but `QGraphicsScene`
  lives in Qt Widgets, so the export path would gain a Widgets dependency the
  CLI does not have.
- Records become mirrored scene items that must be kept in sync with immutable
  document records after every edit, undo and redo. That is a second state copy.
  Cosmetic pens and item caching at fractional DPR need care.
- Selection handles and snapping are custom anyway.

**(c) Reuse the GL viewport.**

- The viewport is built around a 3D camera, GPU colour-ID picking and the model
  editing tools. Paper is 2D, and text, dimensions and title blocks would all be
  QPainter overlays anyway.
- Printing would have to read back framebuffers, so there is no vector reuse.
- It re-imports the GL surface risks (ADR 0157, the sanitized Wayland hang
  tracking) into a workspace that does not need GL for vector content.
- It conflicts with "never reparent the live GL viewport".

### Decision

**Adopt (a): a custom `QWidget` canvas over a shared QPainter compositor.** GL
is used only where it is unavoidable: rendering raster-appearance and perspective
view content into an offscreen image (§2).

## 2. Rendering model views on paper

- **Vector content** (`technicalLines`, orthographic):
  - `captureMeasuredDrawing` gives a `MeasuredDrawing` in frame millimetres.
  - The anonymous `paths()` in `measured_export.cpp` becomes the public
    `measuredDrawingPaths(const MeasuredDrawing&)`. ADR 0163 planned this for
    R092; R091 needs it to draw views on screen, so it **moves into R091**.
  - The compositor translates the paths to the frame origin and clips them to
    the frame. Annotation text inside the view follows ADR 0163: it is clipped
    to the frame when painted.
- **Raster content** (`rasterAppearance`, and every `notToScale` perspective
  view):
  - A **scene-scoped raster capture** renders a `RenderSnapshot` from
    `captureDrawingView` into an offscreen `QImage`. ADR 0163 also put this in
    R092; it **moves into R091**.
  - It is a `Viewport` member (`renderDrawingViewRaster(snapshot, page or
    perspective camera, pixel size)`) because the GL renderer and texture cache
    live there. It runs on the GUI thread with the viewport's context and FBO,
    like `renderRaster`.
  - The viewport is created and shown in the Model workspace at startup and is
    never reparented, so its context exists.
  - Resolution: screen pixels for the frame at the current zoom, capped at 2048
    px per side for display. R092 asks for export DPI separately.
- **Caching.** A desktop `DrawingViewRenderCache` holds two maps:
  - `(documentIdentity, view, revision) → status`. This wraps the existing
    `DrawingViewStatusCache`.
  - `fingerprint (+ pixel size for raster) → prepared content`: the painter path
    set or the `QImage`.

  Content is keyed by fingerprint, so a revision-only change, an undo back to
  equal content, or moving a frame without resizing it reuses the cached paths.
  The cache is bounded (proposed: 64 MiB of images, least recently used first)
  and pruned when views disappear.
- **Background evaluation.**
  - `evaluateDrawingViewDrawing` is split into an owner-thread capture
    (`captureDrawingView` from `readSnapshot()`) and a pure worker half that
    takes the capture and the view record and returns status, fingerprint and
    `MeasuredDrawing`. The existing function becomes the composition of the two,
    so every command and query keeps its result.
  - The desktop runs the worker half on a `QThreadPool` and debounces revision
    changes (proposed: 150 ms).
  - It evaluates only the views on the visible page first, then the rest for
    the References and Pages counts.
  - Results whose revision is older than the document are dropped.
  - Until a result arrives, the canvas keeps drawing the last content for that
    view with an "Updating…" status in Entity info. It never shows an empty frame.
  - Typing and dragging never wait on a capture.
- **Commit-time cost.** The command layer still evaluates synchronously in
  `replaceView`. That is correct, and one capture per commit is bounded by
  R078. Two measures keep it rare:
  - (i) `drawsDifferently` treats a pure frame translation (same size, same
    offset, scale, content and hidden lines) as *not* changing what the view
    draws, because the fingerprint is frame-local. Moving a view on paper then
    needs no evaluation. A test asserts the fingerprint invariance this relies on.
  - (ii) Consecutive arrow nudges amend one history entry (§5).
- **States on paper** (design §6):
  - `current`: no badge.
  - `stale`: the amber "⟳ Changed" corner chip, with icon and text, never colour
    alone. It is screen only.
  - `broken`: a red border, a diagonal cross-hatch fill and a centred card, for
    example "Scene “South elevation” was deleted", with **Choose scene…**. The
    frame, scale and caption stay in place.
  - `failed`: like broken. The card shows "Could not draw this view" with the
    capture detail and no Choose scene.
  - `notToScale`: the caption reads "Not to scale"; it is not a state.

  The hatch is screen only. R092 decides how broken views print, via its
  preflight.

## 3. Page-item records (schema 27, still open)

### Options

- **Separate tables per kind** (texts, labels, dimensions, …). Each table needs
  its own allocator, change vector, floor and codec, and there is no shared z
  order across kinds.
- **One `drawingItems` table with a tagged `kind`** (chosen). It follows ADR
  0163's sibling-table pattern:
  - one allocator, `nextDrawingItemId`;
  - one `Edit` change vector, `drawingItems` with `nextDrawingItemFloor`;
  - one strict codec with per-kind fields;
  - a single per-sheet stacking order.
- **Items nested inside the sheet record.** Rejected for the same reason ADR
  0163 kept views out of sheets: identity and history must survive moving an
  item between pages.

### Records

The conventions are ADR 0163's:
- canonical nonzero decimal IDs;
- strict, fully required fields, with unknown fields rejected;
- allocators that never rewind;
- names of 1–1024 bytes where present.

The container keeps the `drawing-sheets-v1` feature and `json-v27`, and adds
`nextDrawingItemId` to `allocatorFloors`. The R090.b `drawing-sheets-v27`
writer fixture is regenerated, which decision 6 allows until a release writes
schema 27.

**Sheet record additions:**
- `number`: 0–32 bytes, no control characters, unique among sheets when
  nonempty. This holds the "A-101" in "A-101 Ground floor"; `name` stays
  "Ground floor".
- `marginMm`: 0–50, at most a quarter of the shorter paper side, default 10.
  It is the page-margin frame. Snapping and the title-block default use it, and
  R092 uses it as the printable-area hint.
- `dimensionFormat` (added by R091.g): `{unit: mm | cm | m | ftIn, decimals: 0–4, showUnit}`,
  default whole millimetres without a unit; see the R091.g conventions.

**Drawing item:** `id`, `sheet`, `z` (contiguous per sheet, 0 at the bottom),
`kind`, and `data`, whose fields depend on `kind`:

| `kind` | `data` fields |
| --- | --- |
| `text` | `frame {xMm, yMm, widthMm, heightMm}` (wrap box); `text` (1–4,096 bytes); `font`; `align` (`left`/`center`/`right`) |
| `label` | `text` (1–4,096 bytes); `font`; `textAt {xMm, yMm}`; `leader` (bool); `target`: `{kind:"paper", xMm, yMm}` or `{kind:"view", view, anchor, lastMm [x, y]}` |
| `dimension` | `view`; `anchors` [2]; `orientation` (`aligned`/`horizontal`/`vertical`, in paper axes); `offsetMm` (signed distance from the measured points to the dimension line); `prefix` (0–256 bytes); `font`; `lastMm` [[x, y], [x, y]] |
| `line` | `from [x, y]`, `to [x, y]`; `strokeMm` (0.1–2.0); `dash` (`solid`/`dashed`) |
| `rectangle` | `frame`; `strokeMm`; `dash` |
| `titleBlock` | `frame`; `font`; `fields {project, drawingTitle, date, revision}` (each 0–256 bytes) |

- **`font`** is `{family, sizePt}`.
  - `family` is 1–128 bytes and names a font family, never a file path.
  - `sizePt` is 4–72.
  - The default is "DejaVu Sans" 10 pt, matching R078 export.
- **Geometry.** Stored geometry must lie inside the paper. Derived geometry,
  such as dimension lines placed from the model, is not validated against the
  paper; it is clipped when painted.
- **Limits.** 4,096 items, 512 per sheet. Items count toward the 8 MiB
  drawing-sheet byte budget.
- **Derived title-block fields.** Never stored:
  - the sheet number, from the sheet's `number`;
  - the page "n of N", from `position` and the sheet count;
  - the scale:
    - "1:50 @ A3" when every measurable view on the page shares one scale and the
      paper matches a named size, otherwise "1:50";
    - "As indicated" when scales differ;
    - "Not to scale" when the page has only perspective views;
    - "—" when it has no views.
- **R092 hook for fonts and assets.**
  - R092's preflight resolves each `font.family` through `QFontDatabase`, reports
    substitutions, and adds them to the existing `missingFontGlyphs` count.
  - While schema 27 is open, a later layer may add an optional embedded-font
    reference (a document asset with a font media type) to `font`, and an
    `image` item kind with an `asset` ID for title-block logos. The asset table
    and its `missing` state already exist.
  - R091 does not add either.
- **Cascade rules.** Additive changes to R090.d semantics, in the same edit:
  - `drawing_sheet.delete` deletes the sheet's items.
  - `drawing_view.delete` deletes the dimensions bound to the view and the labels
    whose `target.view` is the view.
  - `drawing_view.move` moves those items to the new sheet and puts them on top
    of its z order.

  Undo restores each case as one entry. The record validator rejects any item
  whose `view` names a missing view or a view on another sheet.

### Referenced dimensions: binding and the broken state

- **Binding.**
  - A dimension names a `view` and two anchors that use the ADR 0080 anchor JSON.
  - R091 allows only `vertex` and `edge` kinds. Edge anchors are used for edge
    midpoints and for points on an edge.
  - At creation, `state` is `resolved` and `fallback` is the world point.
  - The canvas offers snap candidates from the evaluated `MeasuredDrawing`:
    - `MeasuredSource {body, edge}` identifies the model edge under each line;
    - a line endpoint that matches the projection of an edge endpoint within
      0.001 mm becomes a vertex anchor;
    - any other point on the line becomes an edge anchor at the projected
      parameter.
  - Section-cut lines (`MeasuredSource.section`) and points that are not on a
    model edge are refused with a named reason. R091 has no "paper-only"
    dimensions.
- **Evaluation.** `evaluateDrawingItem(doc, item)` lives in
  `src/io/drawing_items.{hpp,cpp}`. It is pure and cheap: it needs no capture,
  only the scene camera and `drawingViewProjection`.
  - Each anchor is resolved with `resolveAnnotationAnchor`, projected with
    `worldToSheet`, and the dimension geometry is placed from `orientation` and
    `offsetMm`.
  - **value = paper length × scaleDenominator / 1000** metres, formatted with the
    document's display units and precision (`displayLength`). It is the
    view-plane distance, which is what an orthographic drawing measures.
  - Moving geometry, changing the view's scale, offset or frame, or editing the
    scene camera therefore updates the dimension at once. That is the Verify
    line "referenced dimensions update".
- **Broken state.** Derived, never stored; same family as views.

  | Reason | Meaning |
  | --- | --- |
  | `referenceMissing` | An anchor's body or entity no longer exists |
  | `sceneMissing`, `sceneHasNoCamera` | The view cannot project |
  | `notToScale` | The view's scene became perspective |

  - A broken dimension paints in the failure red with the ADR 0081 cross markers
    at `lastMm`. That is its paper position at the last sheet edit that wrote
    it.
  - Its text reads "[Missing reference]", as `annotationText` already does.
  - Entity info offers **Reattach**, which runs the Dimension tool on that item
    and commits a `drawing_item.update`. For each broken anchor it proposes the
    nearest vertex or edge in the view at the old anchor's paper position
    (`lastMm`); the user confirms or picks another before anything is committed.
  - `drawing_items.query` lists broken items with reasons, for R092's preflight.
- **Why lazy resolution, not lineage remap.**
  - Model annotations survive splits and merges because each model edit rewrites
    their anchors. ADR 0163 forbids model edits from writing sheet records, and
    its reverse test requires sheet tables to be byte-identical across model
    edits.
  - Paper anchors are therefore resolved by ID at evaluation time:
    - vertex IDs survive moves, rotations, scales and most push/pull and split
      edits;
    - an edge anchor breaks when its edge is split;
    - anything breaks on cross-context consolidation (ADR 0078 already leaves
      that broken).
  - Per-body ID allocators are monotonic and history is linear, so a missing ID
    is never silently re-bound.
  - R091.c records which common edits keep paper dimensions associated. The
    owner chose this over a lineage remap (Decisions, item 2).
- **Leader labels with a view target** use the same binding and states. The
  label text stays put; only the leader tip follows or breaks.
- **Creation refusals.** A dimension cannot be created on a `notToScale`,
  `broken` or not-yet-evaluated view: `DRAWING_ITEM_NOT_MEASURABLE`.

### Commands and queries (additive)

- **Operations:**
  - `drawing_item.create|update|delete|reorder`, where `reorder` sets a sheet's
    z order;
  - `drawing_sheet.update` for `number` and `marginMm`;
  - `drawing_sheet.create` accepts optional `number` and `marginMm`.

  All operations stay inside the sheet-only batch rule.
- **Queries:**
  - `drawing_items.query`: paged per sheet, with state and reasons.
  - `drawing_item.describe`: the record, the derived paper geometry, the measured
    value and the state.
  - `drawing_sheet.describe` gains an item count.
- **Catalogue and schemas.** The generated files regenerate as in R090.d.
- **Invariant tests.** The pointer-identity and reverse tests extend to
  `drawingItems`.

### Implementation conventions (R091.c)

- **Operations.** `drawing_item.create {sheet, kind, data}`,
  `drawing_item.update {item, kind, data}` (kind must name the item's kind; omitted
  data fields keep their values; title-block `fields` merge one by one),
  `drawing_item.move {item, sheet}` (to another sheet, on top; items bound to a view
  move only with `drawing_view.move`), `drawing_item.reorder {sheet, order}`,
  `drawing_item.delete {item}` and `drawing_item.reattach {item, anchors}`.
  `drawing_sheet.update {sheet, number?, marginMm?}`; `drawing_sheet.create` takes
  optional `number` and `marginMm`. Batch results add `createdDrawingItems`; staging
  names `drawing_item` changes; `--inspect-native` adds `drawingItems`.
- **Command anchors** use the annotation command forms, `{kind: "vertex", body,
  vertex}` and `{kind: "edge", body, edge, fraction}`. The stored ADR 0080 anchor,
  its world `fallback` and `lastMm` are derived by the command. A dimension's view
  and anchors are set at creation and change only through `drawing_item.reattach`.
  Every `drawing_item.*` command that writes a bound item rewrites `lastMm` and the
  fallbacks when the anchors resolve and the view projects; a broken item keeps its
  old values.
- **`lastMm` follows the view.** A `drawing_view.update` or `drawing_view.move` that
  changes the view's source, frame, offset or scale (its world↔sheet mapping)
  rewrites `lastMm` for every item bound to the view in the same sheet-only edit.
  Each position goes through the old mapping's `sheetToWorld`, at the depth of the
  anchor's last resolved world point, and then the new mapping's `worldToSheet`. If
  either mapping is unavailable (broken or not to scale), `lastMm` stays as it is. A
  rename writes no item records. Undo restores the view and its items together, so
  Reattach always searches where a broken anchor is drawn now.
- **Not measurable.** Creating a dimension or a view-target label, or reattaching
  one, evaluates the view synchronously (one capture, as `replaceView` does) and is
  refused with `DRAWING_ITEM_NOT_MEASURABLE` when the view is broken, failed or not
  to scale.
- **Placement.** Aligned: the line is offset along n = (d.y, −d.x) for the
  measured direction d with paper y down, so a positive offset is above a
  left-to-right pair. Horizontal: a positive offset is above both points, a negative
  one below. Vertical: a positive offset is right of both points, a negative one
  left. The text point is the line's midpoint. Broken text is "[Missing reference]",
  followed by the prefix when there is one.
- **Title-block scale** names A0–A5, Letter, Legal and Tabloid within 0.5 mm in
  either orientation. Views whose scene is missing or has no camera do not count.
- **Queries.** `drawing_items.query {sheet?}` (paged, revision-bound, record plus
  `state` and `reasons`, no capture); `drawing_item.describe {item}` (record plus
  `evaluation`: anchors, `measuredMm`, `lineMm`, `textMm`, `tipMm`, `paperLengthMm`,
  `valueMetres`, `text`, `titleBlock`); `drawing_item.reattach_candidates {item}`
  (one capture). For each missing anchor, the query proposes the nearest model vertex or edge point
  drawn in the view (section-cut lines excluded) to `lastMm`. It prefers a vertex
  within 2 mm. Proposals use the command anchor form and are never applied.
- **Pure translation.** `drawsDifferently` compares the frame's size, not its
  position.
- **Amend.** `executeAmend` amends a sheet-scoped entry (for example, consecutive
  nudges labelled "Nudge"). An amendment cannot touch other items or model
  records.
- **Lineage table** (`drawing_dimension_tests`). The last column is a model
  annotation (ADR 0080) on the same edge points, for comparison:

  | Model edit | Vertex anchors | Edge anchors | Model annotation |
  | --- | --- | --- | --- |
  | Move, rotate or scale the body | kept | kept | kept |
  | Move one vertex or one edge | kept, value updates | kept, value updates | kept |
  | Push/pull the face | kept | kept | kept |
  | Group the body | kept | kept | kept |
  | Split the anchored edge (away from the anchor) | kept | broken | kept |
  | Erase the anchored edge | kept | broken | broken |
  | Erase the body | broken | broken | broken |
  | Make a component (consolidation) | broken | broken | broken |

  Undo of each edit restores the item. No model edit writes item records.
- **Known limitation: edge splits.** Edge splits are the only edits in the table
  where paper dimensions break and model annotations survive. Model annotations
  survive because `Document::apply` rewrites the annotation records inside the
  model edit (`resolveAnnotationEdit` → `remapAnnotationAnchor`), using the edit's
  `TopologyChanges` descendants. ADR 0163 forbids the same write to sheet records.
  That lineage exists only in the history entry. It is not persisted, and it is lost
  when history is trimmed or the file is reopened. Paper resolution that consulted it
  would therefore give different answers before and after a save, so R091.c does
  not use it.
  - Make component breaks model annotations as well. ADR 0078 leaves cross-context
    consolidation without published lineage. Paper dimensions are no worse there.
  - Rebinding by geometry (finding the edge that now contains the fallback point)
    would silently re-bind a missing ID, which §3 rules out.
  - **Follow-up.** A persisted, append-only lineage journal would fix this. Model
    edits would maintain it as a model record: retired edge ID → descendant edges
    with their parameter ranges, per body, bounded and compacted. It would also need
    a schema 27 field and a cross-context entry for make component (a new ADR 0078
    contract). Resolution would consult it read-only.
  - Estimate: about 600–900 lines plus tests, beyond R091.c. Until then, Reattach is
    the repair.

## 4. Keyboard model

- **Workspace-scoped shortcuts.**
  - Every public action gets a scope: `global`, `model` or `sheets`.
    - Existing actions are `global`, except the model tools and viewport-only
      actions, which are `model`.
    - Sheet tool ids are new and stable (`sheet.tool.select`, `sheet.tool.view`,
      …).
  - Conflict detection applies only to overlapping scopes: global with
    everything, model with model, sheets with sheets. This changes
    `Window::action`'s duplicate check, `ShortcutBindings` (defaults, `resolve`,
    `conflicts`, `assign`) and the `panelGuard`, which treats model panels as
    `model` scope.
  - Applying bindings:
    - On a workspace switch, the inactive workspace's actions are disabled.
    - Sheets' plain and Shift-only keys become `Qt::WidgetShortcut` on the canvas,
      as the model's do on the viewport.
  - The `shortcuts/v1` envelope is unchanged: IDs are the key, and scope comes
    from the code. The editor gains a Workspace column and search term.
  - This extends the rules of ADRs 0148 and 0153 and is recorded here.
- **Defaults** (design §2): Select `Space`, Model view `W`, Line `L`, Rectangle
  `R`, Text `X`, Label `K`, Dimension `D`, Move `M`, Pan `H`, Zoom `Z`, Fit page
  `Shift+Z`, Accept view `U`, Add page `Ctrl+Shift+N`.
  - `W`, `X`, `K`, `D` and `U` are unused in the model.
  - The rest collide only with `model`-scope actions.
  - `Ctrl+Shift+N` is also the Scenes and Sections panel "New…", but those
    panels are not in the Sheets Tray.
- **F6 regions in Sheets:** command search, sheet tool rail, canvas, page tabs,
  Tray (Sheets), Assistant, Measurements. `focusRegions_` becomes per-workspace.
- **Canvas keys** with the Select tool:
  - Tab and Shift+Tab cycle page items in paint order (views, then items by `z`),
    as the viewport cycles entities.
  - Arrows nudge 1 mm; Shift+Arrows nudge 10 mm.
  - Alt held during a drag ignores snapping.
  - Delete erases.
  - Enter on a view opens edit-view (§7 of the design). Enter on a selected
    text, label or title-block field edits it in place on the canvas; Esc or a
    click outside ends editing and commits one history entry. Typing alone never
    starts editing, because plain keys stay tool shortcuts (UX_DESIGN keyboard
    ownership rule). Entity info keeps the same text as a labelled field for
    screen readers.
  - Esc backs out one step: cancel the operation, then leave the tool, then
    leave edit-view, then clear the selection (UX_DESIGN §4.1).
  - Ctrl+PgUp and Ctrl+PgDn switch pages. These are new; no current action uses
    them.
- **Measurements box per tool.** Bare numbers are paper millimetres in the
  Sheets workspace; `cm` and `in` suffixes are accepted by `parseLength`. The
  footer's unit label reads "Paper units: mm". Dimension *values* use document
  units.

  | Tool or step | Label | Grammar |
  | --- | --- | --- |
  | Model view, after the first corner or before placing | Size | `w,h` frame size in mm; Enter places. Re-entry amends size |
  | Select, one view selected | Scale | `1:50` or `50` sets the scale; `w,h` sets the crop size. Re-entry amends |
  | Select, item(s) selected | Move | `<dx,dy>` relative, `[x,y]` absolute top-left in mm |
  | Line | Length | length along the preview direction, `<dx,dy>`, `[x,y]` |
  | Rectangle | Size | `w,h` |
  | Move | Distance | distance along the drag direction, `<dx,dy>` |
  | Dimension, after two anchors | Offset | offset in mm; re-entry amends |
  | Text, Label | Position | `[x,y]` places at a paper point |
  | Zoom | Zoom | `150%` or a factor |

- **Screen-reader alternative.** As the Outliner is for the model (ADRs 0154 and
  0155):
  - The Tray's **Pages** section is a `QTreeWidget`. Pages are top-level rows;
    each page expands to its views and items in paint order, with a state
    column (Current, Changed, Broken: reason).
  - Selection mirrors the canvas both ways.
  - F2 renames a page or view.
  - Alt+Up and Alt+Down move a page or reorder an item.
  - Delete erases.
  - Enter opens edit-view or the text field.
  - The canvas accessible description announces the focused item ("View 1 ·
    Ground floor plan · 1:50 · Changed since accepted").
  - Entity info exposes every value as a labelled, editable field.
  - Together with typed Measurements entry, a sheet can be laid out without a
    pointer.

## 5. Undo

- Every page operation is one `executeBatch` of sheet-only commands with a
  `metadata.label`. Each is one labelled entry in the single history (decision
  3). Labels include "Add page", "Place view", "Move 3 items", "Crop view", "Pan
  view", "Accept 2 views", "Add dimension", "Edit title block".
- Multi-selection operations put all their commands in one batch.
- Preview while dragging is local canvas state. Nothing is committed until
  release, Enter or a typed value. Esc cancels with no entry and no dirty state.
- Measurements re-entry and consecutive arrow nudges of the same selection use
  `ToolSession`'s amend stamp (`executeAmend`), so ten nudges are one "Nudge"
  entry. R091.c tests that `amendLast` handles sheet-scoped edits.
- Edit-view commits one entry per gesture ("Pan view", "Crop view"), so Undo
  works inside the mode. Done or Esc leaves the mode and commits nothing.
- Undo and Redo in Sheets are the existing `edit.undo` and `edit.redo`, which
  are global actions. Undo can therefore step back into a model edit (decision
  3). When the entry being undone is not sheet-scoped, the status bar names it
  ("Undid model edit: Push/pull").

## 6. Tests

- **Headless** (ctest, offscreen):
  - `drawing_item_record_tests`: kinds, bounds, limits, z contiguity, cascades,
    pointer identity of model records under every item edit and the reverse.
  - `drawing_sheet_io_tests`: extended for item round trip, strict fields,
    migration and fixture regeneration.
  - `drawing_item_command_tests`: create, update, delete and reorder; atomic
    batches; amend; paged queries; schema regeneration.
  - `drawing_dimension_tests`. Paper dimensions update when geometry moves, and
    when the view scale, offset, frame or scene camera changes. They become
    broken on vertex deletion and return on undo, and are refused on perspective
    views. The suite also records the lineage table (§3) and checks the
    pure-translation fingerprint invariance.
  - `drawing_sheet_paint_tests`. Compositor output to `QImage` and `QPdfWriter`
    is deterministic, a 2 m edge at 1:50 measures 40 mm, and no screen-only
    decoration appears.
- **Native** (`sketchyup_native_fixture`, `native.yml` X11 and Wayland at 1× and
  2×; `sheet_layout_input_tests` also at fractional 1.5):
  - `sheet_workspace_input_tests`: workspace toggle, page tabs, Fit page, zoom
    and pan, F6 regions, scoped shortcuts.
  - `sheet_layout_input_tests`: placing with `W` by drag and by typed `w,h`,
    snapping to the grid and to view edges, Alt, handles, nudge and amend, Tab
    cycling, edit-view, Accept and `U`, broken-view Choose scene, hit-testing at
    zoom.
  - `sheet_annotation_input_tests`: Line, Rectangle, Text, Label, Dimension,
    title block, z order, Reattach.
  - In every suite, the pointer identity of the model maps is asserted after each
    page operation.
- **Keyboard and accessibility extensions:**
  - `keyboard_reachability_tests`: one Tab stop for the sheet rail, F6 order in
    Sheets, page tabs, the Pages tree.
  - `shortcut_input_tests`: `L` for a sheet tool does not conflict with model
    Line, and global keys still conflict across workspaces.
  - `native_accessibility_audit`: new dialog cases (Choose scene…, page setup,
    Save as new scene…).
  - `visible_focus_input_tests`: canvas and Pages tree.
  - `theme_input_tests`: contrast of the new tokens.
  - `accessibility_bridge_test.py`: reads the Pages tree and the canvas
    description over AT-SPI.
- **Acceptance (R091.h).** A scripted "small drawing set":
  1. three pages and four views, including a perspective one;
  2. dimensions, labels, text and title blocks;
  3. a model edit;
  4. stale chips, and dimensions updating;
  5. Accept all;
  6. a scene deletion, giving a broken view, then Choose scene;
  7. save and reopen.

  It asserts that model records are pointer-identical across every page
  operation, and that the bytes are unchanged.

## 7. Workspace shell and theme

- **Construction.** One `QStackedWidget` replaces `#modelTabs` in the content
  row *at construction*, so the GL viewport is never reparented at runtime.
  - Page 0 is the existing `#modelTabs`.
  - Page 1 is the Sheets page: breadcrumb and paper line, the canvas, and the
    page tab bar with "+ Page".
  - The tool rail is a second `QStackedWidget` holding the model rail and the
    sheet rail, each with its own `installToolbarNavigation`.
  - The Tray swaps between the existing `OrganizationPanel` page and a new
    `SheetsTray`. `OrganizationPanel`'s index-based focus proxy is untouched.
- **Toggle.** The Model | Sheets toggle is two checkable buttons in an exclusive
  group in the header, backed by `view.workspace.model` and
  `view.workspace.sheets`. They also appear in the View menu and the palette.
  - A third, `global`-scope action, `view.workspace.toggle`, switches between
    Model and Sheets. Its default is `Ctrl+Alt+S`, and it is configurable like
    any other binding. No existing default binds `Ctrl+Alt+S`: the only
    `Ctrl+Alt` defaults are `Ctrl+Alt+T`, `Ctrl+Alt+N`, `Ctrl+Alt+F` and
    `Ctrl+Alt+Shift+T`, all panel actions, and no `QKeySequence` standard key
    the code uses maps to it.
  - The title reads "house · Sheets • Edited", following the code's existing "•".
- **Theme tokens.** `ThemeColors` gains `selectionStroke`, `warning`, `failure`,
  `paper` and `pasteboard`.
  - The first three start from today's literals (`#6354d4`, `#b9762f`,
    `#bc4343`) and are checked for contrast in both themes.
  - Paper is white in both themes because it represents print.
  - Existing viewport literals are not refactored in R091.
  - A small `CollapsibleSection` widget is added for the Sheets Tray. It uses
    28 px header rows, as the design asks.
- **Status bar.** In Sheets it adds an active-tool label before the
  instruction, as UX_DESIGN §3.1 describes. The model status bar is unchanged.
- **Hidden until complete.** Until R091.h, the workspace toggle is hidden unless
  `SKETCHYUP_SHEETS_WORKSPACE=1` is set. The native suites set it, so `main` stays
  releasable with no half-built UI. Documents with sheets still open, save and
  validate either way.

## Decision

Build R091 as:
- a QWidget/QPainter page canvas over a Gui-only compositor shared with R092;
- desktop worker evaluation keyed by revision and fingerprint;
- one `drawingItems` table in the open schema 27, with lazily resolved,
  view-bound dimension anchors and derived broken states;
- workspace-scoped shortcuts;
- one labelled history entry per page operation through the shared commands;
- the layers below.

## Consequences

- R091 takes two items ADR 0163 assigned to R092: the public
  `measuredDrawingPaths` and the scene-scoped raster capture. R092 keeps the
  multi-page writer, export DPI and the preflight.
- Schema 27 gains `drawingItems`, `nextDrawingItemId`, and the sheet `number` and
  `marginMm` fields. The v27 writer fixture is regenerated once, in R091.b.
  Sheet-free files are still schema 26.
- The R090.d commands gain cascades and the pure-translation shortcut. Their
  existing results do not change for documents without items.
- The shortcut system gains scopes. Saved bindings keep their meaning; an old
  saved binding that now conflicts only across workspaces becomes active again.
- Paper dimensions break in some edits where model dimensions survive (§3);
  Reattach repairs them.
- One more surface needs the native matrix: three new suites and extensions to
  five existing ones.

## Decisions

The owner decided these points on 2026-10-14. The interaction design is the
[R091 UX design (canvas; private to the owner)](https://claude.ai/artifact/ByrL3RtnQihKV9zx5DiYyQ).

1. **Canvas.** The QWidget + QPainter `SheetCanvas` (§1, option (a)), over a
   Qt-Gui-only compositor `paintDrawingSheet(QPainter&, …)` that R092 also uses
   to write PDF. Editor-only decorations (selection, handles, grid, the broken
   hatch and the "⟳ Changed" chip) are painted by the canvas after the
   compositor and never print.
2. **Dimension lineage.**
   - Lazy resolution by ID, plus a derived broken state with **Reattach** (§3).
   - Model edits never write sheet records; ADR 0163 stands.
   - Reattach offers the nearest vertex or edge in the view at the old anchor's
     paper position, and the user confirms.
3. **Shortcuts.** New per-workspace shortcut scopes in `Window::action` and
   `ShortcutBindings` (§4). The same key may bind in Model and in Sheets.
   - The workspace toggle is `Ctrl+Alt+S` (`view.workspace.toggle`, global
     scope). It switches between Model and Sheets and is configurable. It was
     checked against every existing default binding and conflicts with none
     (§7).
4. **Tray.** A separate `SheetsTray` is swapped in while Sheets is active.
5. **Theme tokens.** Warning (amber), failure (red) and selection (blue-violet)
   tokens are added to both themes, with contrast checked against WCAG AA. The
   Sheets status bar gains an active-tool label (§7).
6. **Measurements box.** A single box, where `1:N` sets the scale and `w,h` sets
   the size. Paper units are millimetres (§4).
7. **Pulled forward from R092.** The public `measuredDrawingPaths` and the
   scene-scoped raster capture move into R091 (§2).
8. **Save as new scene** is two history entries, a model edit then a sheet edit,
   because mixed model and sheet batches are refused.
9. **Show scene in model** applies the scene camera only, never a full recall.
10. **"Other model file…"** is omitted until the external-model feature exists
    (UX_DESIGN §11).
11. **Text editing.** Enter on a selected text, label or title-block field edits
    it in place. Esc or a click outside ends editing as one history entry.
    Typing never starts editing by itself, because plain keys stay tool
    shortcuts (UX_DESIGN keyboard ownership rule).
12. **Feature flag.** The UI stays behind `SKETCHYUP_SHEETS_WORKSPACE=1` until
    R091.h enables it.

The remaining proposals in the body stand as written and were not separately
decided: the Pages tree expands each page to its views and items (§4), leader
label text is static in R091, and templates carrying sheets stay out of R091
("+ Page" copies the current page's title block in the same edit).

## Layer plan for R091

Each layer merges on its own, and `main` stays releasable. The UI layers stay
behind `SKETCHYUP_SHEETS_WORKSPACE` until R091.h.

**R091.a: page-item records (core).** §3.
- Scope:
  - `DrawingItemRecord` and its kinds, validation and limits;
  - the sheet `number` and `marginMm` fields;
  - `Edit.drawingItems` and its floor;
  - history, `readSnapshot`, prepared-edit and memory accounting;
  - `isDrawingSheetOnlyEdit` extended;
  - the cascades in `eraseDrawingSheet`, `eraseDrawingView` and
    `moveDrawingView`.
- Files: `src/core/drawing_item_records.{hpp,cpp}`,
  `src/core/drawing_sheets.{hpp,cpp}`, `src/core/drawing_sheet_records.*`,
  `src/core/model.{hpp,cpp}`, `src/core/history.cpp`,
  `src/core/prepared_edit.cpp`.
- Tests: `drawing_item_record_tests`.

**R091.b: persistence.** §3.
- Scope:
  - the `drawingItems` and `nextDrawingItemId` root fields;
  - the floors;
  - the sheet fields in the codec;
  - the regenerated `drawing-sheets-v27` fixture;
  - fuzz seeds and fault-corpus counts;
  - `native-format-v1.json`;
  - an addendum to the ADR 0108 "Schema 27" section.
- Files: `src/io/drawing_items_io.{hpp,cpp}`, `src/io/drawing_sheets_io.cpp`,
  `src/io/document_io.cpp`, `src/io/container.cpp`.
- Tests: `drawing_sheet_io_tests`, `native_format_tests`,
  `parser_fault_corpus_tests`.

**R091.c: item evaluation, commands and queries.** §§3, 5.
- Scope:
  - `evaluateDrawingItem` (dimensions, labels and title-block fields);
  - `drawing_item.*`, `drawing_sheet.update`, and the new `drawing_sheet.create`
    fields;
  - `drawing_items.query` and `drawing_item.describe`;
  - the pure-translation rule in `drawsDifferently`;
  - amend for sheet batches;
  - the generated API files.
- Files: `src/io/drawing_items.{hpp,cpp}`,
  `src/automation/drawing_sheet_commands.*`, `catalog.cpp`, `inspection.cpp`,
  `commands.cpp`.
- Tests: `drawing_item_command_tests`, `drawing_dimension_tests` (with the
  lineage table), the API schema tests.

**R091.d: compositor and evaluation split.** §§1, 2.
- Scope:
  - public `measuredDrawingPaths`;
  - `paintDrawingSheet`;
  - the capture and worker split of `evaluateDrawingViewDrawing`.

  This layer has no UI.
- Files: `src/io/measured_export.{hpp,cpp}`,
  `src/io/drawing_sheet_paint.{hpp,cpp}`, `src/io/drawing_view.{hpp,cpp}`.
- Tests: `drawing_sheet_paint_tests`, plus `drawing_view_tests` showing equal
  results through both paths.

**R091.e: workspace shell, scoped shortcuts and read-only canvas.** §§1, 2, 4, 7;
design §§1, 2, 4, 6.
- Scope:
  - the stacked workspace and toggle (behind the flag);
  - action scopes in `Window::action` and `ShortcutBindings`, and the editor's
    Workspace column;
  - the sheet rail with Select, Pan, Zoom and Fit;
  - the per-workspace F6 regions;
  - `SheetCanvas`: paper, shadow, grid, margins, zoom and pan, the accessible
    interface;
  - the breadcrumb and paper line, page tabs and "+ Page";
  - the `SheetsTray`, with Pages (tree), References, History and a read-only
    Entity info;
  - `DrawingViewRenderCache` and worker wiring;
  - `Viewport::renderDrawingViewRaster`;
  - view state drawing (chip, broken card);
  - the theme tokens and `CollapsibleSection`;
  - the Sheets status bar;
  - page rename, reorder and delete.
- Files: `src/app/sheet_workspace.{hpp,cpp}`, `src/app/sheet_canvas.{hpp,cpp}`,
  `src/app/sheets_tray.{hpp,cpp}`, `src/app/sheet_evaluation.{hpp,cpp}`,
  `src/app/viewport_drawing_view.cpp`, `src/app/collapsible_section.hpp`,
  `src/app/theme.hpp`, `src/app/shortcut_bindings.*`, `src/app/window*.cpp`.
- Tests: `sheet_workspace_input_tests`; extensions to `shortcut_input_tests`,
  `shortcut_bindings_tests`, `keyboard_reachability_tests` and
  `theme_input_tests`.

**R091.f: model-view layout and editing.** Design §§3, 5, 7, 8.
- Scope:
  - the `W` tool and the "New model view" panel;
  - selection, handles, Move, crop and resize;
  - snapping to grid, margins, view edges and alignment guides, with hint text;
  - nudge and amend; Tab cycling;
  - Entity info · Model view as editable fields;
  - Accept, `U` and Accept all;
  - edit-view mode (pan = offset, zoom = crop, banner, Done);
  - Choose scene…, Save as new scene… and Show scene in model;
  - the Measurements grammar for Size, Scale and Move.
- Files: `src/app/sheet_tools.cpp`, `src/app/sheet_canvas.cpp`,
  `src/app/sheets_tray.cpp`.
- Tests: `sheet_layout_input_tests` (also at fractional 1.5); new
  `native_accessibility_audit` dialog cases.

**Implementation conventions (R091.f).**
- **Labels.** "Place view", "Move view", "Nudge", "Crop view", "Pan view", "Change scale",
  "Rename view", "Change style", "Accept 1 view" (`U`), "Accept N views" (Accept all),
  "Choose scene"; "Save as new scene" (model) then "Use new scene" (sheet).
- **Amending.** Measurements re-entry, consecutive nudges, consecutive arrow pans and consecutive
  wheel notches in edit-view amend the entry they continue (`executeAmend`) while nothing else
  changed; a new operation, selection or tool ends it. Amending a placement re-creates the view,
  so it gets a new ID (allocators never rewind).
- **Crop.** Dragging a handle or typing `w,h` changes the frame and adjusts `offsetMm` so the
  scene target stays on its paper point: the drawing stays put and the scale never changes.
- **Placement.** The natural frame is the visible geometry's extent at the scale plus a 10 mm
  border, rounded up to 5 mm, with the drawing centred by `offsetMm`; Enter or Place uses it at
  the pointer (else the margin corner). A perspective scene places 160 × 120 mm with rendered
  content. The title is the scene name, made unique ("South elevation 2").
- **Snapping.** 8 logical pixels at any zoom. Targets: the paper edges, the margins, and the
  other views' edges and centres (drawn as alignment guides). The 5 mm grid quantizes when it is
  shown and nothing nearer applies. Alt turns snapping off.
- **Edit-view** applies to drawable orthographic views; a perspective view fills its frame and
  is cropped with the handles instead. The wheel zooms the crop about the pointer (×1.1 per
  notch), Ctrl+wheel zooms the page, `+`/`-` zoom the crop and arrows pan 1 mm (Shift 10 mm).
  Esc, Done or a click outside the frame leaves.
- **Save as new scene** copies the view's scene with its camera target moved to the point drawn
  at the frame centre; the view then uses it with a zero offset and draws the same.
- **Tab** cycles the page's views; R091.g adds page items to the cycle with item selection.

**R091.g: page items.** Design §5; §§3, 4.
- Scope:
  - the Line, Rectangle, Text, Label and Dimension tools;
  - view-anchor snapping from `MeasuredSource`;
  - Reattach;
  - the title block (inserted with "+ Page" and from the Insert menu; field
    editing);
  - z order (bring forward and send backward);
  - items in the Pages tree;
  - the Measurements grammar for these tools.
- Files: `src/app/sheet_tools.cpp`, `src/app/sheet_items.cpp`,
  `src/app/sheets_tray.cpp`.
- Tests: `sheet_annotation_input_tests`; the `accessibility_bridge_test.py`
  extension.

**Implementation conventions (R091.g).**
- **Labels.** "Add line", "Add rectangle", "Add text", "Add label", "Add dimension", "Add title
  block", "Edit text", "Edit label", "Edit title block", "Move <kind>", "Nudge", "Delete <kind>",
  "Bring forward", "Send backward", "Reattach dimension", "Reattach label", "Change sheet number",
  and one label per Entity info field ("Change font", "Change orientation", …).
- **Tools.** Line and Rectangle draw by drag or click–move–click, snap like views (grid, margins,
  view edges) and take Shift for 45° steps or a square. Text places with a click (the box fits its
  words) or a drag (the drag sets the wrap width), then edits in place; a new item is created only
  when editing ends with text. Label picks the leader's point first: a model vertex or edge in a
  view drawn to scale binds it to the view, anything else is a paper point; the second click
  places the text. Dimension picks two model points in one view (8 px snap, a vertex preferred
  within it, from `nearestDrawingViewAnchor`, the same search Reattach uses), then the offset;
  new dimensions are aligned, and Entity info changes the orientation. Tools stay active after
  each item; a typed value right after one revises it (amend).
- **Measurements.** Line: a length along the preview, `<dx,dy>` or `[x,y]` (before the first
  click `[x,y]` sets the start). Rectangle: `w,h` (from the first corner towards the pointer, else
  at the pointer). Text and Label: `[x,y]`, and `12pt` sets the size of new text. Dimension: the
  offset after two points. Select with an item: `<dx,dy>`, `[x,y]` (top-left of its stored
  geometry) or `12pt`; a selected dimension takes its offset.
- **Moves.** An item moves its stored geometry, clamped to the paper; a dimension moves by its
  offset; a label bound to a view moves its text only, a paper label moves with its point.
- **In-place editing.** Esc, Ctrl+Enter, a click outside or focus leaving ends editing; Enter adds
  a line break in text and labels and ends a title-block field. Tab and Shift+Tab move between
  the title block's four stored fields within one entry.
- **Title block.** Insert › Title block places 160 × 30 mm (or what fits) in the bottom-right
  corner of the margin with empty stored fields. "+ Page" copies the current page's title blocks
  into the new page in the same entry. The compositor draws the design's grid: the project across
  the top, then Drawing and Sheet, then Scale, Date, Revision and Page, captions over bold values.
- **Dimension text** reads along its line (left to right, or top to bottom), centred 1 mm above it.
- **Dimension format.** Values follow the sheet's `dimensionFormat` (schema 27, required on
  every sheet record), never the model's display units or precision: `unit` `mm` | `cm` | `m`
  | `ftIn`, `decimals` 0–4 and `showUnit`; the default is whole millimetres without a unit
  ("6000", the design's "6000"; the page notes state the unit). Rounding is half away from
  zero. Feet and inches always show their marks, `19'-8"` in whole inches, or `19'-8 1/4"` to
  1/16 inch when decimals are above zero, so `showUnit` changes nothing there.
  `drawing_sheet.update {sheet, dimensionFormat: {unit?, decimals?, showUnit?}}` merges the
  given fields. Entity info · Page (shown when nothing is selected) edits it, one entry per
  field ("Change dimension format"). The prefix stays per dimension.
- **No dialogs.** R091.g opens none, so its commands carry no ellipsis: Reattach proposes on
  the canvas and Enter confirms (the dialog-keyboard inventory treats "…" as "opens a dialog").
- **Z order.** Bring forward `Ctrl+]` and Send backward `Ctrl+[` in a new Arrange menu (Sheets
  scope); Alt+Down and Alt+Up on an item row in Pages do the same.
- **Pages** has two columns, number and name, so names keep the narrow Tray's width (no
  sideways scrolling). State is a small leading glyph on the name: amber ⟳ changed, red ⊗
  broken, nothing for current; page rows show glyphs with counts ("⟳2 ⊗2"). The words
  ("Broken: reference missing", "1 changed · 2 broken") are the tooltip and the accessible
  description. Choosing a row selects the item, Delete erases it and Enter edits its text in
  place.

**R091.h: enable and accept.**
- Scope:
  - remove the flag;
  - add a Sheets section to UX_DESIGN and the shortcut table;
  - the `visible_focus` and audit sweeps;
  - the acceptance record `docs/verification/R091h-sheet-layout.md` (the §6
    script, the X11 and Wayland 1× and 2× matrix, installed smoke);
  - the roadmap status.

R091.a–d are headless and can be reviewed in parallel with design follow-ups.
R091.e depends on a–d. R091.f and R091.g both depend on e and touch the same
files, so they merge in order.

## R092 export

Added 2026-10-14 for [R092](../PR_ROADMAP.md#r092). R092 prints and exports the
drawing set through the compositor of §1 (decision 1). The approved preflight design
is the "Export sheets as PDF" dialog of the R091 UX design canvas.

### Decisions (owner, 2026-10-14)

1. **Real text.** PDF text is real text with embedded font subsets, so it can be
   selected and searched. Geometry stays vector. A font that cannot be embedded is
   drawn as outlines and reported by the preflight. Raster views are drawn at 300 dpi
   by default.
2. **Actual size.** Each page is its own paper size in millimetres, and one PDF may
   mix sizes. Measured scale holds on paper: a 2 m edge at 1:50 is 40 mm.
3. **One preflight.** The preflight is a pure function of the document and the export
   options. It returns structured findings, each with a severity (blocking, warning
   or info), a stable code, the affected sheet, view and item IDs, and a message. The
   export refuses to run while a blocking finding is unresolved. The caller's opt-ins
   turn specific blocking findings into warnings. The desktop dialog, the CLI and the
   automation query all call the same function.
4. **Reconciliation report.** Every export produces a JSON report. It lists the
   pages, and for each view its state, fingerprint, accepted stamp and scale. It also
   lists font substitutions, raster views and DPI, and the views exported as they are
   now (changed since accepted) or as empty frames. This is the roadmap's
   "model-update reconciliation report". It is attached to the PDF as
   `sheet-export-report.json` and returned to the caller.

### R092.a: the headless engine

- **Engine.** `src/io/sheet_export.{hpp,cpp}`:
  - `preflightSheetExport(doc, options)`;
  - `renderSheetsPdf(doc, options)`, which paints into memory;
  - `exportSheetsPdf(doc, options, path)`, which publishes through `publishNewFile`. It
    never replaces an existing file, and a failure leaves no partial file.
- **Pages.** Each page is painted by `paintDrawingSheet` into one `QPdfWriter` at 2540
  dpi, so one device unit is 0.01 mm. Each page gets its own `QPageLayout` before
  `newPage`. Qt writes page boxes in whole points, so a page is within 0.5 pt
  (0.18 mm) of its paper size; drawing coordinates, and so measured scale, are exact. Nothing edits the document; every read uses `captureDrawingSheet`
  snapshots.
- **Text.** The compositor's text primitives keep their shaped glyph runs as well as
  their outlines. `DrawingTextMode::Text` (a new paint option) draws the runs with
  `QPainter::drawGlyphRun`, so Qt's PDF engine embeds a TrueType subset with a
  ToUnicode map.
  - `drawingFontEmbeddable` refuses a font without TrueType outlines (`glyf`), and an
    OS/2 `fsType` of restricted-license, no-subsetting or bitmap-only. Text in such a
    font is filled as outlines instead.
  - Model annotations inside views (`measuredDrawingPaths`) keep their runs the same
    way.
  - Sheet text is shaped with contextual alternates and ligatures off (`calt`, `liga`,
    `clig`), so every glyph maps back to its character. Faces such as Inter swap ":"
    and "-" between digits for glyphs outside their character map, which would drop
    them from extracted text. The canvas uses the same shaping, so screen and paper
    agree.
  - The desktop canvas keeps the default, `Outlines`: it still fills the glyph paths.
  - R078's measured export is unchanged.
- **Options.**
  - `sheets`: empty means every sheet. Pages are always written in page order.
  - `allowBroken`: export broken or failed views, and raster views that cannot be
    rendered, as empty, labelled frames.
  - `allowChanged`: export views that changed since they were accepted as they are
    now.
  - `rasterDpi`: 72–1200, default 300. Each raster view is capped at 8192 px per side
    and 16 MP; the report gives the DPI actually used.
  - `rasterRenderer`: the desktop viewport's scene-scoped capture. Headless callers
    have none.
- **Page ranges.** `resolveSheetPageRange` accepts:
  - `all`;
  - a sheet number such as `A-101`, or a 1-based position when no sheet has that
    number;
  - a range `FROM..TO` (or `FROM–TO` with an en dash; a hyphen belongs to sheet
    numbers);
  - comma-separated lists of these.
- **Findings.** An opted-in finding keeps its code, becomes a warning, and has
  `optedIn` set.

  | Code | Severity | Meaning |
  | --- | --- | --- |
  | `viewBroken` | blocking; warning with `allowBroken` | One per broken or failed view (or one over the path budgets): "A-301, view 3 “Section B-B”: its scene was deleted." It prints as an empty, labelled frame |
  | `viewChanged` | blocking; warning with `allowChanged` | All views changed since accepted (`stale`), with their reasons, fingerprints and accepted stamps. ADR 0163 refuses to publish them unless acknowledged |
  | `rasterUnavailable` | blocking; warning with `allowBroken` | Raster views that this export cannot render because it has no viewport. They print as labelled frames |
  | `fontSubstituted` | warning | One per missing family, naming its substitute and the items and pages that use it |
  | `fontOutlined` | warning | A font that cannot be embedded. Its text is drawn as outlines and cannot be selected |
  | `missingGlyphs` | warning | Characters with no glyph in their font |
  | `itemBroken` | warning | Page items with a missing model reference. They print in red as "[Missing reference]"; Reattach fixes them |
  | `sceneReferencesMissing` | warning | A view whose scene names model entities that no longer exist |
  | `scaleSummary` | info | "7 views at measured scale: 6 at 1:50 and 1 at 1:20. Print at 100% to measure from paper." |
  | `rasterViews` | info | "1 view will be raster: “Garden view” (perspective) at 300 dpi. Everything else is vector." |

- **CLI.** `sketchyup-cli --export-sheets OUT.pdf --input MODEL [--pages RANGE]
  [--allow-broken] [--allow-changed] [--preflight-only]`.
  - Like `--export-view`, it is a standalone operation that starts the offscreen
    `QGuiApplication` font worker.
  - `--preflight-only` prints `{"status":"preflight","preflight":{…}}` and writes
    nothing.
  - An export prints `{"status":"exported","exportReport":{…}}`.
  - A blocked export exits 1 with `{"status":"failed","code":"EXPORT_BLOCKED",
    "preflight":{…}}` on standard error.
  - The sheet options without `--export-sheets` are `INVALID_REQUEST`.
- **Automation.** The inspection query `drawing_sheets.export_preflight {pages?,
  allowBroken?, allowChanged?, rasterDpi?, rasterCapture?}` returns the same findings.
  It appears in the session, headless MCP and native MCP catalogs.
  - `rasterCapture` describes an export that has the desktop's raster capture.
  - The query needs the font worker. In a process without a `QGuiApplication` (the
    headless `--session`, `--mcp` and `--inspect` modes, which keep `QCoreApplication`
    under ADR 0127) it returns `UNAVAILABLE_CONTEXT`.
  - The export itself is **not** an automation operation. ADR 0028 exposes no
    arbitrary-path tool, and the session writes only the destination bound at launch.
    A launcher-bound export destination would need its own decision.
- **Tests.** `sheet_export_tests` (ctest `sheet_export`; `sheet_export_real` with
  `SKETCHYUP_PDF_TEST=pdftoppm`, as `measured_export_real`). With Poppler, it
  rasterizes the PDF and measures the 2 m edge as 40 mm ± 0.1 mm, checks clipping to
  the view frame, extracts the dimension and title-block text with `pdftotext`, and
  checks embedded subsets with `pdffonts`.

### R092.b: desktop export and print (planned)

- **File › Export sheets…** opens the preflight dialog from the approved design. The
  dialog has:
  - the page choice: All, Current page, or a range such as A-101–A-301;
  - the output file;
  - the "Checked before publishing" list, built from `preflightSheetExport` findings
    and grouped by code.
- **Controls in the list.**
  - A broken view offers **Go to view** and "Export it as an empty, labelled frame"
    (`allowBroken`).
  - Changed views offer **Review and accept** and "Export them as they are now (named
    in the export report)" (`allowChanged`).
  - A substituted font offers **Choose another font**.
  - **Export PDF** stays disabled while a blocking finding is unresolved; the footer
    names what to fix.
- **Raster views.** The desktop passes `Viewport::renderDrawingViewRaster` as
  `rasterRenderer`, so raster views export at 300 dpi.
- **Print…** goes through `QPrintDialog` at 100% (no fit-to-page). It runs the same
  preflight and paints each page with `paintDrawingSheet` at actual size.
