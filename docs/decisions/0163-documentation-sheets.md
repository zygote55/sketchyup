# Documentation sheets and linked views

Status: **Accepted (2026-10-13)** for the decision; the layers are implemented by
R090.a–e. Roadmap [R090](../PR_ROADMAP.md#r090) (M10, scope row
[F01](../SCOPE.md)); shapes [R091](../PR_ROADMAP.md#r091) and
[R092](../PR_ROADMAP.md#r092). The owner's choices are recorded under "Decisions".
The R091 interaction design is the
[R091 UX design (canvas; private to the owner)](https://claude.ai/artifact/ByrL3RtnQihKV9zx5DiYyQ).

Implementation: R090.a–e are complete. PRs
[#310](https://github.com/zygote55/sketchyup/pull/310) (R090.a),
[#311](https://github.com/zygote55/sketchyup/pull/311) (R090.b),
[#312](https://github.com/zygote55/sketchyup/pull/312) (R090.c),
[#313](https://github.com/zygote55/sketchyup/pull/313) (R090.d) and the R090.e pull
request; acceptance in [R090.e](../verification/R090e-drawing-sheets.md).

## Context

F01 asks for LayOut-style documentation: multi-page sheets, title blocks, scaled
linked views, dimensions, annotations, print/PDF output and model-update
propagation, with regression fixtures. R090 is the first layer. It builds the sheet
model for pages, scales, view references and update tracking. It keeps links to
specific model scenes and exposes stale and broken reference states. Its acceptance
test: page and view records round trip, and changing model geometry updates a linked
view or marks it stale without changing its declared scale. [BUILD_PLAN](../BUILD_PLAN.md)
ships M10 work in separately gated releases. SketchyUp 1.0.0 is released, so this is
the first change to the public format since it was frozen.

What the code and contracts give us:

- **Format.** [ADR 0108](0108-public-native-format.md) freezes `SKUPDOC` container
  v2. The current document schema is v26 (`nativeDocumentVersion = 26` in
  `src/io/native_format.hpp`; `docs/api/native-format-v1.json`). `decodeContainer`
  (`src/io/container.cpp`) accepts only the exact historical `requiredFeatures`
  sets. Any other set is refused: because a newer set is not recognised, the
  manifest-field check runs first and 1.0.0 reports "Unsupported container field"
  (ADR 0108, schema 27) before its "Unsupported required document features"
  check. Unknown
  root fields, record fields and chunk kinds are rejected. The encoder always writes
  the current schema. `tests/fixtures` keeps writer output for v24 and v25 but has
  **no retained v26 (1.0.0) writer fixture**.
- **Versioning.** [ADR 0162](0162-release-versioning.md): a minor version may add
  commands, schemas or format features. A newer minor may write documents that older
  readers reject through `requiredFeatures`, but it keeps reading every format the
  previous minor read. A patch release never changes the written format.
- **Save and recovery.** `saveSnapshot` (`src/io/save.cpp`) writes the file
  atomically through `QSaveFile`. It keeps a `.bak` copy of the previous container
  if that copy decodes, and it fsyncs the directory. `RecoveryWriter`
  (`src/io/recovery.hpp`) journals whole-document bytes, keyed by `documentId`.
  New files go through `publishNewFile`, which never replaces an existing path.
- **Document and history.** `Document` (`src/core/model.hpp`) holds immutable
  records shared by `shared_ptr` (bodies, scenes, sections, annotations, and so on).
  It has one linear history of `Edit` values, and each record family has its own
  change vector and allocator floor (`scenes`/`nextSceneFloor`, ...). The revision
  is monotonic. Every apply, undo and redo raises it, so undoing back to identical
  content still produces a new revision.
- **Window.** `MainWindow` owns exactly one `Document doc_` and one `path_`
  (`src/app/window.hpp`). Its central area is a `QTabWidget` named "Model and render
  views", with the model viewport as tab 0 and render results as closable tabs
  (`src/app/window.cpp`). [UX_DESIGN](../UX_DESIGN.md) §2 rules out satellite
  windows. The codebase and UX docs already use "sheet" for modal dialogs
  (`ReportSheet`, "a small sheet"), so the identifiers below say *drawing sheet*.
- **Scenes.** Scenes ([ADR 0070](0070-saved-scene-records.md),
  [0071](0071-scene-workflows.md)) have stable, never-reused IDs and selective
  snapshots. The camera, visibility, style, section and solar properties are each
  optional. Later model edits never rewrite a snapshot, and missing references are
  kept and diagnosed. `RenderSnapshot::captureSavedScene`
  (`src/integrations/render_snapshot.cpp`) already turns a scene into an immutable,
  worker-safe snapshot. It recalls the persistent model state on a `readSnapshot()`
  copy and records `sourceIdentity`, `sourceRevision` and `sourceScene`. It
  *rejects* scenes that have missing references, a temporary clip plane, show-hidden
  enabled, or no camera. It honours only hidden body and face entities, not hidden
  edges.
- **Measured export (R078,** ADRs [0124](0124-measured-orthographic-hidden-lines.md)–[0128](0128-native-measured-export.md)**).**
  - `MeasuredPage` (`src/geometry/measured_view.hpp`) holds the paper width, height
    and margin in millimetres, the scale denominator and an orthonormal frame.
  - `measuredHiddenLines` is the bounded, Qt-free hidden-line engine.
  - `captureMeasuredDrawing(const RenderSnapshot&, MeasuredPage)`
    (`src/io/measured_drawing.hpp`) requires an orthographic camera, puts the camera
    target at the page centre, and keeps the declared scale independent of camera
    zoom. It returns a `MeasuredDrawing`: lines, sources, annotations with
    associative and broken state, units, and a report.
  - `exportMeasuredDrawing` (`src/io/measured_export.hpp`) writes exactly **one**
    page, as SVG or PDF (`QPdfWriter` at 2540 DPI). Painter path generation is the
    anonymous-namespace `paths(const MeasuredDrawing&)` in `measured_export.cpp`.
  - Raster appearance comes only from `Viewport::renderMeasuredView`
    (`src/app/viewport_measured.cpp`). It uses the *editor* camera, the live `doc_`
    and the GL framebuffer.

## Options considered

### (a) Sheet records inside the `.sketchyup` document (schema 27)

Add drawing-sheet and view records to the document root. The schema becomes 27,
with required feature `drawing-sheets-v1`. Schemas 1–26 migrate to empty sheet
tables.

- **Compatibility.** ADR 0162 explicitly allows this in a minor release (1.1.0).
  A 1.0.0 build rejects any v27 file at `decodeContainer`, because the feature set
  is unknown. It reports "Unsupported container field" (its manifest check runs
  before the feature check) and does not touch the file, so no data is silently
  dropped. Under decision 2 the writer emits the lowest schema the
  content needs, so a file without drawing sheets stays schema 26 and 1.0.0 still
  opens it.
- **Atomic save and recovery.** No new code. Sheets travel in the document chunk,
  so `saveSnapshot`, `.bak`, `RecoveryWriter`, `--migrate-native` and the
  round-trip verification in `createNativeFile` cover them. Title-block images
  reuse the document asset table and its asset chunks (including `missing` state).
- **Multi-model sets.** Not supported. One sheet set documents one model. A later
  minor can add an external model-reference record (see (c)) under a new feature
  without changing the view record.
- **Relinking.** Not needed. Scene IDs are document-local, stable and never reused.
  Undoing a scene deletion relinks the view automatically.
- **Undo.** Sheet edits are ordinary `Edit`s, one history entry per command, on
  the document's single stack, as scene edits already are. The cost: Undo while
  looking at a sheet can undo an earlier model edit (decision 3).
- **"Page editing never modifies source geometry".** Enforced in code, not by file
  separation: sheet commands may only fill the new sheet fields of `Edit`, and a
  sheet-only predicate rejects anything else (see Decision). Model edits never
  write sheet records either. Staleness is derived, not stored.
- **Live updates.** Views evaluate against the in-memory document, so an unsaved
  model edit shows on the sheet at once. No file watching is needed.
- **Fit.** One document and one window. A drawing-sheet tab can sit beside the
  model tab later (R091 UX). It reuses the fuzz corpus, the parser fault corpus,
  migration and the inspection and transaction machinery.

### (b) Separate sheet-set file that references model files by path and identity

A new `.sketchysheets` file stores pages and views. Each model reference holds a
relative path, the last absolute path, `documentId` and the last accepted
fingerprint. Referenced models are loaded read-only with `loadDocument`.

- **Compatibility.** The model files stay schema 26, so 1.0.0 still opens them.
  1.0.0 cannot open the sheet file, which is a new parser and a new public format
  needing its own versioning, fuzz seeds and limits.
- **Save and recovery.** Needs a second `Document`-like owner with its own history,
  dirty state, save stamp, `.bak` policy and recovery journal. That duplicates much
  of `src/core/history.cpp`, `prepared_edit.cpp` and `recovery.cpp`.
- **Multi-model.** Yes.
- **Relinking.** Required. Resolve the relative path, then the absolute path,
  verify `documentId`, and on a mismatch or missing file offer a relink chooser.
  `documentId` plus revision is not a safe identity: two copies of one file share
  the ID and can reach equal revisions with different content. Only the content
  fingerprint is reliable.
- **Host paths and sandboxing.** This would be the first SketchyUp format that
  resolves host paths, which ADR 0108 deliberately avoids. UX_DESIGN §2 also
  requires portal file access, and portals grant per-file access, so sibling
  model paths may be unreadable under a sandbox.
- **Undo.** Separate stacks, which keeps them clean.
- **Enforcement.** Strongest: the models are read-only by construction.
- **Live updates.** Only saved model bytes are visible. Edits in another window are
  invisible until saved. Without cached content, a missing model makes the view
  unprintable.
- **Fit.** A second document kind per window, or a second window. That conflicts
  with the single-window model.

### (c) Self-contained sheet-set bundle with embedded model snapshots (LayOut-style)

Like (b), but the bundle (on the pattern of the `.sketchylib` bundle,
[ADR 0129](0129-self-contained-template-bundles.md)) embeds a verified copy of each
referenced model container. Links are by path and identity, and an explicit
**Update reference** replaces the embedded copy.

- **Multi-model.** Yes.
- **Missing models.** The sheet still prints from its embedded copy, and missing
  models are reported rather than fatal.
- **Portals and relocation.** Problems mostly avoided: the bundle is portable, and
  the path is used only to look for updates.
- **Enforcement.** Embedded models are read-only by construction.
- **Cost.** Everything in (b) except fatal path resolution, plus file size
  (embedded models of up to 128 MiB each), a new bundle parser and fuzz family,
  and still a second document kind with its own history and recovery. "Stale"
  means only "the embedded copy differs from the file on disk". It never reflects
  unsaved edits.

## Decision

**Adopt (a): drawing-sheet records inside the native document, schema 27.** Design
the view's source as a field, so that (c)-style external model references can be
added later as a separate required feature if multi-model sets become a
requirement.

Main reason: R090's acceptance is about live linkage. Changing geometry must update
the view or mark it stale, without changing the declared scale. Option (a) gets this
from the in-memory document, with stable scene IDs, and reuses the existing atomic
save, recovery, assets, history, migration, fuzz and inspection machinery instead
of building a second document kind. F01 does not require multi-model sets.

### Records (schema 27)

The root adds `drawingSheets`, `nextDrawingSheetId`, `drawingViews` and
`nextDrawingViewId`. Records follow the scene conventions:

- IDs are canonical nonzero decimal strings.
- Names have 1–1024 UTF-8 bytes, no control characters and no leading or trailing
  spaces, and are unique per table.
- Strict fields: every field is required, unknown fields are rejected, and
  allocators never rewind on undo.

View IDs are document-global, so R091 can move a view between pages without
changing its identity. The tables are siblings, not nested, so a view keeps its
identity and history when its page changes.

- **Drawing sheet:** `id`, `name`, `position` (contiguous order), and
  `paper {widthMm, heightMm}` in the same 10..2,000 mm range as `MeasuredPage`.
  Named paper sizes are a UI convenience and are not stored.
- **Drawing view:**
  - `id`, `name`, `sheet` (a drawing-sheet ID).
  - `source`: in R090 always `{"kind":"scene","scene":"<id>"}`. A future
    `external` kind gets its own feature.
  - `frame {xMm, yMm, widthMm, heightMm}`: the viewport rectangle on the paper.
    It is also the crop and must lie inside the paper.
  - `offsetMm [x, y]`: a paper-space pan of the scene target inside the frame.
  - `scaleDenominator`: declared, in `MeasuredPage` bounds, and never derived from
    camera zoom or geometry.
  - `content`: `technicalLines` or `rasterAppearance`.
  - `hiddenLines`: boolean.
  - `accepted`: `null` or `{revision, fingerprint}`, where `fingerprint` is a
    64-character lowercase SHA-256.

Limits follow scenes: 256 sheets, 1,024 views, 64 views per sheet, and 8 MiB
charged for sheet records in history and snapshot accounting. The container adds
the `drawing-sheets-v1` feature, encoding `json-v27`, and the two allocator floors
in `allocatorFloors`. Validation keeps a view whose scene was deleted (a diagnosed
broken view, as with annotations); creating or updating a view requires its scene
to exist.

### Linked views: what is stored and what is derived

Stored: the scene link, the frame/crop, the pan offset, the declared scale, the
content mode, the hidden-line flag and the accepted stamp. Not stored: the camera,
visibility, style and sections. Those come from the scene at evaluation time. An
omitted scene property takes the document's current persistent state, which is what
`captureSavedScene` already does through `recallSceneModel`. A view therefore has no
camera override. To change the projection, the user updates the scene, which marks
the view stale.

States are derived and never persisted:

| State | Meaning |
| --- | --- |
| `current` | Accepted fingerprint equals the fingerprint at the current revision |
| `stale` | Accepted fingerprint differs: the model or scene changed what the view draws (reason `changedSinceAccepted`), or the view has never been accepted (`accepted` is `null`, reason `notAccepted`) |
| `broken` | Scene missing, scene has no camera, temporary clip plane, or show-hidden enabled; each is a named reason |
| `failed` | Capture threw, typically because it exceeded the R078 work bounds. Every capture exception gives reason `captureFailed` with the message as detail; the record is unchanged |

`notToScale` is a condition alongside the state, not a state of its own (decision 5).
It holds when the scene camera is perspective. Such a view is still `current` or
`stale` like any other, using the illustration fingerprint below. It draws as an
unscaled raster illustration, has no measured mapping (no `MeasuredPage`, no
world↔sheet projection), Dimension refuses to measure it, and the scale field reads
"Not to scale".

Missing references *inside* a scene, such as a deleted body that the scene hid, are
a warning count, not `broken`.

**Fingerprint.** SHA-256 over a canonical serialization of the view's
`MeasuredDrawing`, which covers:

- the page parameters;
- line segments in frame millimetres, quantized to 0.001 mm, with hidden flags,
  sorted;
- annotation text, measured values, broken flags, units and precision.

Quantization keeps the hash stable across builds and the normal/sanitizer
configurations. For `rasterAppearance` views the hash also covers a digest of the
appearance inputs: the style, the materials and texture asset SHA-256s of visible
bodies, and the reference-image assets.

Implementation conventions (R090.c):

- **Offset.** `offsetMm` is in paper millimetres, x right and y down, like the frame.
  The scene camera target is drawn at the frame centre plus `offsetMm`. The page
  origin, the world point at the frame centre, is therefore
  `target − right × offsetMm[0] × scale/1000 + up × offsetMm[1] × scale/1000`.
- **Annotations.** The fingerprint counts only annotations whose bounds (anchors and
  text point, in frame millimetres) reach the frame. `captureMeasuredDrawing` does
  not crop annotations; clipping their rendering to the frame is R092's compositor
  work.
- **Perspective views.** These have no measured drawing. They hash the captured
  snapshot inputs the illustration depends on, never rendered pixels:
  - the camera and the frame size;
  - the model style;
  - for every visible face, its world triangles quantized to 0.001 mm, colour,
    opacity and texture-asset SHA-256;
  - every visible edge, with its soft, smooth and wire flags;
  - every section cut and visible reference image.

  The items are sorted, so IDs and map order do not matter. The whole scene counts,
  not only what falls inside the frame. The hash is bounded at 200,000 items, and an
  overrun gives `failed`.

**Cheap detection.** An in-memory cache maps `(view, revision)` to a fingerprint.
At an unchanged revision, status costs nothing. When the revision changes, views
are re-evaluated lazily from a `RenderSnapshot` on a worker, within the existing
R078 bounds. A revision-only change, such as an unrelated rename or an undo back to
the same content, stays `current`, because the fingerprint is equal. Edits outside
a view's crop or behind its section cut also stay `current`. Any command that
changes a view's frame, offset, scale or mode re-stamps `accepted`, because the user
made that change. Model edits never re-stamp. **Accept** (`drawing_view.accept`) is
an undoable sheet edit.

Semantics (decision 4):

- Display and export always draw the *current* model (live update). No cached
  drawings are stored in the file.
- `stale` means "changed since accepted" and is shown until the user accepts the
  view (`U`, or Accept all).
- The R092 export preflight lists unaccepted views; R092 refuses to publish stale,
  broken or failed views unless the user accepts them or explicitly acknowledges
  them in the reconciliation report.

Implementation conventions (R090.d):

- **Accepted stamp.** `drawing_view.accept` stamps `{revision, fingerprint}`. The
  revision is the batch's base revision, the document revision whose model content
  was evaluated. Accepting a view that is already `current` changes nothing and is
  reported as a no-op.
- **Re-stamp rule.** `drawing_view.update` and `drawing_view.move` re-stamp a view
  only when it was `current` just before the change and the change alters what it
  draws (source, frame, offset, scale, content or hidden lines). A view that was
  `stale`, `broken`, `failed` or never accepted keeps its stamp unchanged, so a frame
  edit never silently accepts an unreviewed model change (decision 4). If the changed
  view cannot be evaluated (`broken` or `failed`), the old stamp is kept and the
  state reports why. A name change or a move without a new frame keeps the stamp.
  A new view starts with `accepted: null`; create and accept can share one batch.
- **Broken views.** Accepting a named `broken` or `failed` view is refused with
  `DRAWING_VIEW_NOT_ACCEPTABLE`, and the whole batch rolls back.
  `drawing_view.accept` with `sheet` or `all: true` (Accept all) skips such views;
  they remain listed by `drawing_views.status`.
- **Batches.** A batch that contains any `drawing_sheet.*` or `drawing_view.*`
  operation must contain only those operations; it publishes one edit tagged with
  the sheet scope. A mixed batch is refused before any work, because one history
  entry cannot both change the model and the sheets. Batch results add
  `createdDrawingSheets` and `createdDrawingViews` beside `createdScenes`.
- **Queries.** Besides `drawing_sheets.query`, `drawing_sheet.describe` and
  `drawing_views.status`, R090.d adds `drawing_view.describe` (the record, its
  status and, for a measurable view, the world↔sheet projection). Status rows are
  evaluated lazily, only for the requested page. `--inspect-native` adds
  `drawingSheets` and `drawingViews` counts; `document.describe` is unchanged.

### Enforcing "page editing never modifies source model geometry"

- `Edit` gains `drawingSheets`/`drawingViews` change vectors and floors.
- A core predicate `isDrawingSheetOnlyEdit(const Edit&)` requires every other
  change vector and optional field to be empty.
- The command dispatcher applies the predicate to every `drawing_sheet.*` and
  `drawing_view.*` operation, and `Document::apply` asserts it for edits tagged
  with the sheet scope. Component edit scopes reject sheet operations, as they
  reject scene operations today.
- Tests check, for every sheet operation, that the body, definition, instance, tag,
  material, asset, scene, section and annotation maps are *pointer-identical*
  before and after the edit. Shared immutable records make this exact and cheap.
- The reverse test: geometry and scene edits leave the sheet tables
  byte-identical.

### Rendering and export reuse

- **Reuse unchanged:** `MeasuredPage`, `measuredHiddenLines` and
  `captureMeasuredDrawing`. A view maps to `MeasuredPage{widthMm = frame.width,
  heightMm = frame.height, marginMm = 0, scaleDenominator, includeHidden =
  hiddenLines}`, with the frame origin at the scene camera target shifted by
  `offsetMm × scale / 1000` along the page right/up axes. A 2 m edge at 1:50
  measures 40 mm inside the frame, as in ADR 0125.
- **New in R090:** `RenderSnapshot::captureDrawingView` (a variant of
  `captureSavedScene`). It tolerates missing scene references (and counts them),
  maps temporary clip planes and show-hidden to `broken` reasons instead of
  throwing, and also honours hidden *edges* so that `edgeVisible` matches scene
  recall. It also adds world↔sheet projection helpers built on
  `MeasuredPage::project`, which R091 dimensions need.
- **Refactor in R092, not R090:**
  - Expose `paths(const MeasuredDrawing&)` as a public `measuredDrawingPaths`, so
    a compositor can translate and clip several viewports onto one painter.
  - Add a multi-page writer: `QPdfWriter::newPage`, and one SVG per page or a
    PDF-only batch.
  - Add a scene-scoped raster capture. `Viewport::renderMeasuredView` uses the
    editor camera and live `doc_`, so it needs a variant driven by a
    `RenderSnapshot` from `captureDrawingView`.
  - `exportMeasuredDrawing`'s report, `missingFontGlyphs`, raster disclosure and
    `publishNewFile` carry over directly.

### Automation surface

R090 includes the shared command and inspection path, following the scene precedent
(R063.b put the commands and queries before any native panel work). R091's UI and
tests should drive the same commands. All additions are additive, as ADR 0162
allows in a minor version:

- **New transaction operations:** `drawing_sheet.create|rename|resize|reorder|delete`
  and `drawing_view.create|update|move|delete|accept`.
- **New inspection queries:** `drawing_sheets.query` (paged summaries),
  `drawing_sheet.describe`, and `drawing_views.status` (paged, revision-bound,
  with state and reasons).
- **Updated files:** the generated catalogue flows into `transactions-v1.json`,
  `inspection*-v1.json`, `headless-session-v1.json`, the `mcp-*` files and
  `tool-reference-v1.json` (ADR 0138). Existing response shapes are not changed.
  `document.describe` does not grow a field, and the new data is reachable only
  through the new queries.
- `docs/api/native-format-v1.json` changes its `documentVersion`, `publicSchema` and
  `readDocumentVersions` values, as R082.cc did.

### Consequences

- The public schema advances once, to `sketchyup-document-v27`, and ADR 0108 gets a
  "Schema 27" section. Files that contain drawing sheets cannot be opened by 1.0.0.
  The open fails cleanly and the file is not modified. Files without sheets stay
  schema 26 (decision 2).
- The frozen provider corpus `inputSha256` values do not change. Its inputs have no
  drawing sheets, so under decision 2 they are still written as schema 26 with an
  unchanged writer string (R090.b verified this; nothing was regenerated).
- A document that returns to schema 26 (every sheet deleted) does not store the
  drawing-sheet allocators, so they restart at 1 when the file is reopened. No
  record or link can refer to a retired sheet or view ID by then.
- Multi-model sheet sets are out of scope until an external-reference feature is
  approved.
- Sheet edits share the model's undo stack and dirty state. Saving the model saves
  its sheets.
- Live evaluation costs a bounded capture per view per changed revision. That work
  runs on a worker from immutable snapshots, never on the editing thread.

## Decisions

The owner decided these points on 2026-10-13.

1. **One model per sheet set.** Sheets are records inside the model's `.sketchyup`
   (option (a)). The view `source` stays a tagged field, so external models can
   become a later feature.
2. **Downgrade window.** A document with **no** drawing sheets keeps the 1.0
   format: schema 26, without the `drawing-sheets-v1` feature. The first sheet makes
   the file schema 27, and 1.0.0 then refuses it with its normal error for a newer
   format ("Unsupported container field"). This is new writer policy: the writer emits the lowest schema the content
   needs. R090.b tests three things:
   - a sheet-free document saved by the new build is accepted by the 1.0.0 reader
     (the `release-1.0.0-v26` fixture rules);
   - adding a sheet writes 27;
   - deleting every sheet writes 26 again.

   The writer string still changes with the version: that happens at the next
   version bump, not with schema 27. A sheet-free save from a 1.0.x build has the
   same writer string as 1.0.0.
3. **One shared undo history.** Sheet edits are labeled entries in the document's
   single history, matching scenes.
4. **Live views with an accepted stamp,** as proposed above. A view always draws the
   current model. "Changed since accepted" (the derived `stale` state) is shown until
   the user accepts it (`U` / Accept all). The export preflight lists unaccepted
   views. There are no cached drawings in the file.
5. **Perspective scenes are allowed** as unscaled views. They give the derived
   `notToScale` condition rather than `broken`: they draw as raster illustrations,
   Dimension refuses to measure them, and the scale field reads "Not to scale".
6. **Schema 27 stays open on `main` until the F01 release.** R091 may add page-item
   records to it, and no release writes it before then. The retained-fixture rule
   applies from the first release that writes it.
7. **Naming.** "Drawing sheet" in code and the file format, to avoid the existing
   dialog-"sheet" term. The UI says "sheet"/"page" per the
   [R091 UX design (canvas; private to the owner)](https://claude.ai/artifact/ByrL3RtnQihKV9zx5DiYyQ).

## Layer plan for R090

**R090.a: core drawing-sheet records.**
- Scope:
  - Types, validation, limits and allocators.
  - `Edit` change vectors and floors; history, readSnapshot, prepared-edit, amend
    and memory accounting.
  - The sheet-only predicate and the component-scope rejection.
- No persistence.
- Files: `src/core/drawing_sheet_records.{hpp,cpp}`,
  `src/core/drawing_sheets.{hpp,cpp}` (edit builders), `src/core/model.{hpp,cpp}`,
  `src/core/history.cpp`, `src/core/prepared_edit.cpp`, `CMakeLists.txt`.
- Tests: `tests/drawing_sheet_record_tests.cpp`:
  - bounds, names, frame inside paper, global view IDs, moving a view between
    sheets;
  - no allocator rewind on undo;
  - a scene deletion keeps the view record;
  - pointer identity of model records under every sheet edit, and the reverse;
  - full-validation oracle on.

**R090.b: schema 27 persistence and migration.**
- First commit: retain unmodified schema-26 writer output from the `v1.0.0` tag
  build as `tests/fixtures/release-1.0.0-v26.sketchyup` (the README records the
  command and commit).
- Then:
  - `src/io/drawing_sheets_io.{hpp,cpp}`;
  - root fields in `src/io/document_io.cpp`;
  - the feature, encoding and floors in `src/io/container.cpp`;
  - `nativeDocumentVersion = 27`, capabilities, and
    `docs/api/native-format-v1.json`;
  - the lowest-schema writer policy of decision 2 (26 without sheets, 27 with);
  - the ADR 0108 "Schema 27" section;
  - a new `drawing-sheets-v27.sketchyup` writer fixture.
- Tests: `tests/drawing_sheet_io_tests.cpp` and `native_format_tests.cpp`:
  - round trip of all fields;
  - migration from every v1–26 fixture to empty tables, with the originals
    byte-compared;
  - unknown, missing or noncanonical fields and allocator disagreement are
    rejected;
  - a broken scene link survives the round trip;
  - decision 2: a sheet-free document saved by the new build is accepted by the
    1.0.0 reader (the `release-1.0.0-v26` fixture rules), adding a sheet writes 27,
    and deleting every sheet writes 26 again;
  - save, `.bak` and recovery round trip with sheets.
- Fuzz: seed documents with sheets in `tests/fuzz/parser_seeds.cpp`, with
  `parser_fault_corpus_tests` counts updated.
- Package acceptance: the previous (1.0.0-1) CLI rejects the v27 fixture through
  `--validate-native` and leaves it unchanged. The real release package is
  downloaded, checked against its pinned SHA-256 and unpacked beside the package
  lifecycle (`SKETCHYUP_PRIOR_RELEASE_PACKAGE`); it must also accept a sheet-free
  save from the new package.
- The provider corpus hashes stay unchanged (sheet-free inputs remain schema 26).

**R090.c: linked-view evaluation and staleness.**
- Scope:
  - `RenderSnapshot::captureDrawingView`;
  - the view→`MeasuredPage` mapping and world↔sheet projection;
  - the canonical drawing fingerprint;
  - `evaluateDrawingView`, which returns state, reasons and the warning count;
  - a pure revision-keyed status cache (desktop worker wiring is R091).
- Files: `src/integrations/render_snapshot.{hpp,cpp}`,
  `src/io/drawing_view.{hpp,cpp}`.
- Tests: `tests/drawing_view_tests.cpp`:
  - moving geometry makes the view `stale` while the declared scale is unchanged;
  - a revision-only change and an undo back to equal content stay `current`;
  - an edit outside the crop stays `current`;
  - a scene update makes the view `stale`;
  - deleting the scene gives `broken` (`sceneMissing`), and undo returns it to
    `current`;
  - a perspective scene gives `notToScale`, not `broken`;
  - show-hidden and temporary-plane `broken` reasons;
  - work limits give `failed`;
  - 40 mm for 2 m at 1:50 at a frame offset;
  - fingerprints equal under normal and sanitizer builds;
  - hidden edges honoured.

**R090.d: shared commands and inspection.**
- Scope: the operations and queries above. The command layer computes the
  `accepted` stamps.
- Files:
  - `src/automation/drawing_sheet_commands.{hpp,cpp}`;
  - `catalog.cpp`, `inspection.cpp`, `component_scope.cpp`;
  - the generated `docs/api` files.
- Tests: `tests/drawing_sheet_command_tests.cpp` and the existing API schema tests:
  - atomic batches, no-ops and stale-revision rejection;
  - accept and undo;
  - every operation passes the sheet-only predicate;
  - paged status;
  - component scope rejection;
  - schemas regenerate exactly.

**R090.e: acceptance record.**
- `docs/verification/R090e-drawing-sheets.md`.
- Installed smoke, plus the normal and sanitized Wayland/X11 matrix.
- Roadmap status and the F01 evidence link.
- The ADR's implementation note records that R090.a–e are complete.

## What R091 and R092 need from R090

- **R091.**
  - Global view IDs and `drawing_view.move` for multi-page layout.
  - Frame and offset edits for cropping and alignment.
  - World↔sheet projection, so paper-space dimensions can anchor to model entities
    through a view (anchors as in [ADR 0078](0078-annotation-anchors.md)) and update
    when the referenced geometry changes.
  - A page-item table (title blocks with field text, text, dimensions, images that
    reference document assets) added to schema 27 under decision 6.
  - The single history, for keyboard-accessible undo.
  - Open question: should templates carry drawing sheets? `instantiateTemplate`
    (`src/io/library_bundle.cpp`) restores a new document without the sheet tables,
    so a template saved from a document with sheets is schema 27 but its sheets are
    dropped on instantiation. A title-block template workflow would need them.
- **R092.**
  - `measuredDrawingPaths` and a multi-page writer.
  - Scene-scoped raster capture.
  - `drawing_views.status` as the reconciliation report.
  - A preflight that lists stale, broken and failed views, missing assets and
    font substitutions (`QFontDatabase` availability plus the existing
    `missingFontGlyphs`) before `publishNewFile`.
