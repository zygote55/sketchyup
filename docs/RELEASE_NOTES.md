# SketchyUp release notes

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
  their substitute. Each page is written at its own paper size and actual size; text
  is real, selectable text with embedded font subsets, lines stay vector, and
  perspective and rendered views are drawn as images at 300 dpi. The export writes a
  new file, never replaces one, and leaves no file if it fails. Every export produces
  a reconciliation report, attached to the PDF as `sheet-export-report.json`.
- **Print.** File → **Print…** in Sheets runs the same checks. Every page prints at
  100% from the paper's top-left corner and is never scaled to fit; the dialog warns
  when a page is larger than the paper.

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
