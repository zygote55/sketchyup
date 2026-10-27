# Custom styles and asset authoring

Status: **Accepted (2026-10-23)**: owner decisions 1 to 12 approved as recommended on 2026-10-23; question Q1 (the preset list and texture licences) answered on 2026-10-27 (procedural or CC0 only; the 15-preset list signed off by the owner on 2026-10-27). Roadmap [R100](../PR_ROADMAP.md#r100) (M10, scope row
[F06](../SCOPE.md)). R100 requires [R089](../PR_ROADMAP.md#r089) (the 1.0 gate,
accepted; 1.3.0 is the current release), [R062](../PR_ROADMAP.md#r062) (viewport
styles and display controls, delivered) and [R079](../PR_ROADMAP.md#r079)
(templates and the local component library, delivered). Each section gives the
options that were weighed and the recommendation. Every owner decision below
carries a recommended default and a one-line reason, so the owner can "approve
all". Facts about this repository were read at `34910469` (origin/main);
anything not verified in the repository is marked as such.

## Context

F06 asks for "custom edge styles, expanded material authoring, packaged content
catalogs and asset dependency management". R100's Build line: "Add packaged edge
styles, expanded material presets and authored asset catalogs with dependencies
and distribution metadata". Its Verify line: "A style/catalog relocates cleanly,
preserves appearance and reports missing dependencies; assets are never
substituted silently." R100 ships in its own gated M10 minor release
([M10 acceptance](../M10_ACCEPTANCE.md), [ADR 0162](0162-release-versioning.md))
once R089, R062 and R079 have all merged with every CI job passing.

What the repository gives us (surveyed at `34910469`):

- **One document-owned style record.** `ModelStyle` (`src/core/model_style.hpp`)
  is a value type with exactly 14 fields: a display `mode` (`textured`,
  `shaded`, `monochrome`, `wireframe`, `xray`); `background`, `ground`, `front`,
  `back` and `edge` RGB colours; `groundVisible`, `groundHeight` (±1e6 m),
  `gridVisible`, `axesVisible`, `edgesVisible`; `profiles` (on/off) and
  `profileWidth` (1–8 logical pixels); and `xrayOpacity` (0.01–0.95). It is
  persisted as a required root `style` object in JSON schema 17 with the
  `model-style-v1` container feature ([ADR 0068](0068-model-styles.md));
  [R062](../PR_ROADMAP.md#r062) delivered it and all five native display modes
  ([ADR 0069](0069-native-model-styles.md)). **There is no packaged, named,
  importable or exportable style today:** the style is one record inside the
  document, and there is no style file, style library folder or `style` command
  family beyond `document.style` and `document.describe`
  (`src/automation/commands.cpp`).
- **Edges are a flat colour at a fixed profile width.** Profiles are drawn in
  `Viewport::drawStyleProfiles` (`src/app/viewport_styles.cpp`) as screen-space
  triangles at `profileWidth` logical pixels in the style's `edge` colour; soft
  edges are skipped. There is **no** edge opacity, dash/line pattern, edge
  texture, or hand-drawn edge rendering (no jitter, wobble or noise line code
  exists in `src/`). `EdgeAppearance` (`src/core/edge_appearance.hpp`) is only
  per-edge `hidden`/`soft`/`smooth`.
- **Materials are document swatches that may reference one managed asset.**
  `MaterialRecord` (`src/core/material_records.hpp`) has a unique nonempty name
  (≤ 1,024 bytes), RGB in [0,1], opacity in [0,1] and one optional `asset` id;
  at most 1,024 swatches ([ADR 0010](0010-material-records.md)). Faces carry a
  front/back material pair with sparse per-face overrides; front and back render
  in separate passes ([ADR 0012](0012-material-rendering.md)). The "New
  material" dialog's **local library is six colour-only presets** — White
  `#e6e6e6`, Concrete `#b0ada6`, Terracotta `#c45f42`, Steel `#7c8a96`, Glass
  `#b7d9e5` (35 % opacity), Terrain `#8a9a6b` (`src/app/materials_panel.cpp`,
  [ADR 0013](0013-native-material-controls.md)). They are flat colours with no
  texture and no external dependency.
- **Managed assets are immutable embedded resources with an explicit missing
  state.** `AssetRecord` (`src/core/asset_records.hpp`) has a name (≤ 1,024
  bytes, names may repeat), a `mediaType` (≤ 128 bytes, lowercase
  type/subtype) and an `AssetPayload` of 1 byte to 16 MiB, or a null payload
  that "explicitly means missing, never an external path". The document allows
  1,024 assets and 64 MiB of payloads. The native envelope carries
  `assets-v1`/`json-v11` with logical keys `assets/<canonical ID>.bin` and a
  manifest entry per asset of `{id, name, mediaType, missing, bytes, path,
  sha256}` (null hash when missing, `src/io/assets.cpp::assetManifest`);
  "corruption is an error; it is never converted into a fabricated missing
  record" ([ADR 0011](0011-managed-assets.md)). `materials.describe` and
  `material.sample` report `assetStatus` as `none`/`present`/`missing`, and
  `document.describe`/`inspectNativeFile` count `missingAssets`
  (`src/automation/commands.cpp`, `src/io/native_format.cpp`).
- **A "never silently substitute" precedent already exists.** On library
  insertion, "asset reuse requires equal media type and byte-for-byte equal
  embedded payload. **SHA-256 is only an indexing aid; a hash match alone never
  substitutes a resource.** Materials reuse only an exactly matching name, color,
  opacity and remapped asset. **Missing referenced assets reject.**"
  ([ADR 0131](0131-atomic-library-insertion.md)); "missing referenced assets
  reject before publication" ([ADR 0129](0129-self-contained-template-bundles.md),
  [ADR 0130](0130-contained-component-bundles.md)). This is the invariant R100
  generalises to styles and catalogs.
- **The library is a flat folder of self-contained single-file bundles.**
  `.sketchylib` has a 24-byte header (`SKYLIB\0\1` magic, then little-endian
  manifest, model and thumbnail lengths), a manifest of at most 32 KiB, a native
  model of at most 128 MiB and a thumbnail of at most 4 MiB, 130 MiB combined
  (`src/io/library_bundle.cpp`); model and thumbnail SHA-256 and sizes are
  verified before use. Manifest version 1 has `bundleVersion`, `kind`
  (`template` or `component`), `name` (≤ 256), `description` (≤ 4,096), up to 32
  `labels` of 64 bytes, `defaultScene`/`definition`, `model` and `thumbnail`
  ([ADR 0129](0129-self-contained-template-bundles.md),
  [ADR 0130](0130-contained-component-bundles.md)). A library is a
  user-selected flat folder of `.sketchylib` files, scanned with bounded limits
  (4,096 directory entries, 128 candidates, 256 MiB read, 16 MiB thumbnails) and
  fully decoded before an entry becomes selectable
  ([ADR 0132](0132-bounded-local-library-catalog.md),
  [ADR 0133](0133-native-template-and-library-workflows.md)); "relocation
  requires only the one bundle file" (ADR 0129). `captureLibraryComponent`
  captures only the selected definition's closure and used resources (ADR 0130).
- **The native format is schema 29, container 2, and the writer emits the
  lowest schema the content needs.** `nativeDocumentVersion = 29`,
  `nativeContainerVersion = 2` (`src/io/native_format.hpp`); the ladder is 26
  (SketchyUp 1.0), 27 (1.1), 28 (1.2), 29 (1.3), selected by
  `nativeDocumentVersionFor` (`src/io/drawing_sheets_io.cpp`), and the container
  manifest lists `requiredFeatures` — a fixed set plus the conditional
  `drawing-sheets-v1`, `component-parameters-v1` and `terrain-v1`
  (`src/io/container.cpp`). Older readers reject a required feature "instead of
  silently discarding" it (ADR 0068); SketchyUp 1.2 refuses schema 29 with its
  newer-format error (ADR 0168 §2). Versions 1–29 read
  (`nativeFormatCapabilities`).
- **CLI and assistant.** `src/automation/main.cpp` exposes importers/exporters
  (`--import-gltf`, `--import-obj`, `--import-ifc`, `--import-terrain`,
  `--import-formline`, `--export-*`), `--build-assembly` (build an assembly
  source into a `.sketchylib`), `--inspect-native`, `--validate-native`,
  `--capabilities`, `--query`, `--recipe` and `--mcp`. The in-app assistant is
  gated by `assistantVisible` (`src/automation/assistant.cpp`), whose result the
  `assistant_tool_surface_tests` pin; every visible tool costs tokens on every
  turn, and MCP exposes the full catalogue (the standing position of
  [ADR 0168](0168-terrain-import-and-surfaces.md) and
  [ADR 0169](0169-terrain-editing-drape-stamp.md) decision 9).
- **UX.** `docs/UX_DESIGN.md` has sections 1–15, and §16 is the IFC design (PR #420) and §17 the camera-path design (ADR 0173); the R100
  interface design would be **§18** and is to be commissioned and approved before the
  desktop slice (the R062 and R096 pattern).

## 1. Packaged edge styles (decision 1)

A packaged style is a **named, relocatable presentation snapshot**: the complete
`ModelStyle` record plus the extended edge-stroke fields of §2, plus any sketch
edge textures as embedded managed assets. It is a snapshot of *appearance only* —
applying it never changes geometry, materials, image assets, component
definitions, identity or history except for the one labelled style edit
(the ADR 0068 contract).

### 1.1 What a style contains

| Part | Source | Notes |
| --- | --- | --- |
| Display mode and the 14 `ModelStyle` fields | `src/core/model_style.hpp` | Unchanged; the style carries the current record |
| Edge stroke: colour, width, **opacity**, **dash pattern**, **edge-texture reference** | New (decision 2) | Colour and width exist today (`edge`, `profileWidth`); opacity, dash and texture are added |
| Profile: on/off, width | `ModelStyle` | `profiles`, `profileWidth` |
| Sketch edge texture (optional) | A managed asset reference | An image asset tiled along visible edges; absent means "no texture" |

A style **does not** carry geometry, material swatch definitions, tags or
component definitions. Those travel in catalogs (section 4), not in a style.

### 1.2 Format

| Option | Verdict |
| --- | --- |
| **A. A SketchyUp-native single-file style, kind `style` in the existing `.sketchylib` envelope (recommended)** | Reuses the hash-verified 24-byte envelope, the SHA-256 size checks, the atomic new-file publisher and the library-folder relocation precedent. Self-contained: any edge texture is an embedded asset, so relocation needs only the one file. |
| B. A folder of style settings plus side files | More parts to move and to keep in sync; no precedent; weaker integrity story. |
| C. SketchUp `.style` file compatibility | **Not claimed.** The `.style` format is proprietary and was not verified; claiming it would need a fixture corpus and a licence review that do not exist. |

The style manifest is the existing v1 manifest with `kind = "style"` and, in
place of `defaultScene`/`definition`, a `style` object (the extended record of
§2). Template and component readers keep rejecting the other kinds (ADR 0130);
style readers reject the other kinds, and a style file cannot contain a model.
No scripts or executable payloads.

### 1.3 Import and export

- **Export** captures the current document style (with the extended fields when
  present) and its referenced edge-texture assets into a `.sketchystyle`,
  published by the existing atomic new-file publisher (never overwriting a
  destination, ADR 0133).
- **Import** offers two dispositions: **apply** (replace the document style in
  one undoable edit, no-op when identical, per ADR 0068) or **save to the style
  library** (drop the file into the user's style folder, the same flat-folder
  pattern as R079, remembered only by folder in preferences). Importing a style
  whose edge texture asset is missing **reports** the missing asset and does not
  fall back to a flat colour (section 5). A style never mutates the library
  file; re-importing is idempotent by name.
- Applying a style reuses `document.style`'s strict single-record validation; a
  style whose fields fail validation is refused with the field named, and the
  document is left untouched.

## 2. The extended style record and schema (decision 2)

The packaged style needs edge-stroke fields the 14-field record does not have.
The recommendation is to **add an optional sub-record, not a new top-level
record**, so documents that do not use it stay at their current schema:

- A new optional `edge` sub-record on the style: `width` (reusing
  `profileWidth` semantics, 1–8 logical pixels), `opacity` (0.01–1), a bounded
  `dash` (a small fixed set of named patterns plus "solid", or a bounded
  on/off run list) and `texture` (an asset id, zero when none).
- The container gains an **optional feature `edge-styles-v1`**, present only
  when the `edge` sub-record is used, alongside the existing conditional
  `drawing-sheets-v1`, `component-parameters-v1` and `terrain-v1`
  (`src/io/container.cpp`).
- A document using it is written as **schema 31**; the writer keeps emitting the
  lowest schema the content needs (`nativeDocumentVersionFor`), so a style that
  uses only the current 14 fields stays schema ≤ 29. SketchyUp 1.3.x and 1.2.x
  **refuse** schema 31 with the newer-format error (the terrain-v1 precedent,
  ADR 0168 §2); versions 1–29 keep reading, and schema-30 documents migrate to
  the current record with the `edge` fields dropped and a notice (the migration
  path of ADR 0068 for schemas 1–16).
- The edge texture is a **managed asset reference** exactly like a material's
  asset binding; it is decoded, decoded-bounds-checked and rendered by the
  existing image path. A missing or hash-mismatched texture **reports** and does
  not fall back (section 5).

Consequences: `docs/api/native-format-v1.json`, the ADR 0108 addendum, the
fixtures and a fuzz seed for the `edge` sub-record decoder are updated; the
decoders check every bound before allocating.

## 3. Expanded material presets (decision 3)

- **The six existing colour presets are kept unchanged** (White, Concrete,
  Terracotta, Steel, Glass, Terrain) so the "no external dependency" contract of
  ADR 0013 holds for them.
- **Textured presets are added** as a bounded set (the exact list is an owner
  decision — decision 3 below). A textured preset creates a material whose
  colour is a tint and whose texture is an **embedded managed asset**
  (`AssetRecord`), so using a preset introduces no external dependency (the
  ADR 0013 position). The face mapping uses the existing implicit/explicit
  texture mapping of `src/core/face_textures.hpp`.
- **Where the textures come from.** Two acceptable sources, both owner decisions:
  (a) **generated or procedural** textures (no third-party bytes, no licence
  review needed); or (b) **CC0 / public-domain** textures whose licence and
  source are recorded per asset and collected into `THIRD_PARTY_NOTICES`
  (the release-artifact notices step, [ADR 0171](0171-ifc-exchange-subset.md)
  §1.3 pattern). **Textures whose licence is not verified are not bundled.**
  The specific preset list and the provenance and licence of every bundled
  texture are **not verified in this repository** and are an owner decision;
  they must be confirmed (with counsel if you wish) before packaging.

## 4. Authored asset catalogs (decision 4)

A catalog is an **authored, self-contained single-file package**
(`.sketchycat`), an extension of the same hash-verified `.sketchylib` envelope
with `kind = "catalog"`. It is relocatable: its manifest holds internal
offsets, never absolute paths, so moving it to another folder or machine
resolves without external resolution.

### 4.1 Manifest

Version 1 `catalog` manifests contain:

- **Identity and distribution metadata:** `bundleVersion`, `kind`, `name`,
  `description`, `labels`, `author`, `license` (a text plus a licence
  identifier), `version` (a bounded version string) and `catalogId`.
- **Entries:** an ordered list, each with a `kind` (`component`, `template`,
  `style` or `material` preset), a bounded `name`/`description`, its embedded
  native payload (for component/template) or its style/preset record, and a
  SHA-256.
- **Shared assets:** a deduplicated list of embedded managed assets (media type,
  bytes, SHA-256); entries reference them by id, so a texture or a nested
  component definition is stored once and referenced by every entry that uses
  it.
- **Integrity:** SHA-256 per entry and per asset, verified before any entry is
  selectable or importable (the ADR 0129/0131 position).
- **Dependencies:** an entry may depend on shared assets (textures), nested
  component definitions and styles, all embedded in the file. A dependency that
  is absent, or whose recorded hash does not match its bytes, makes that entry
  **not importable**, with the entry and the missing dependency id reported —
  it is never substituted (section 5).

Limits follow the envelope: 32 KiB manifest, 128 MiB model, 4 MiB thumbnail,
130 MiB combined; entry and asset counts are bounded and the manifest stays
within 32 KiB.

### 4.2 Relocation

Because the catalog is one self-contained file with internal offsets (no
absolute paths, no side files, no download), "relocation requires only the one
catalog file" (the ADR 0129 position). The verification in section 6 moves the
file to a different directory and (in the acceptance record) a different mount
and re-scans: every entry resolves, hashes match, and appearance is preserved.

## 5. Missing dependencies: reported, never substituted (decision 5; the Verify line)

R100 generalises the standing invariant (ADR 0111, 0129, 0130, 0131) to styles
and catalogs:

- A style import whose edge-texture asset is missing or whose hash mismatches
  its bytes **reports** the missing asset (by id and name) and does not fall
  back to a flat colour or a default texture.
- A catalog entry whose referenced texture, nested component definition or style
  is missing, or whose recorded hash does not match its bytes, is **not
  imported or inserted**; the report names the entry and the missing
  dependency id and the reason. No default substitution, and **a hash match
  alone never substitutes a resource** (ADR 0131).
- Inserting a catalog entry copies its records into the destination with fresh,
  remapped identities (the ADR 0131 atomic insertion contract); a failed
  placement, resource, allocation or budget check leaves the destination and its
  history unchanged.
- Reports reuse the existing `assetStatus` `none`/`present`/`missing` states and
  the `missingAssets` count, extended to name the offending entry.

## 6. Persistence and schema impact (decision 6)

- **Schema 31** (schema 30 is taken by R099's `presentation-v1`, which ships first in F05; if the order changes, the next free number) is introduced for the optional `edge-styles-v1` feature (the
  `edge` sub-record). Container stays at 2. The writer continues to emit the
  lowest schema the content needs, so style-free, preset-free documents are
  unchanged and remain open by 1.0/1.1/1.2.
- **Material presets** and **catalog import** reuse existing records
  (`materials-v1`, `assets-v1`, `components-v1`) and add no schema.
- **Catalog provenance is not persisted in the document.** Importing a catalog
  entry copies its records in (ADR 0131); the document does not store "which
  catalog" or a catalog path, so relocating or deleting the catalog breaks
  nothing (the R079 position).
- Read range grows to 1–30; `nativeFormatCapabilities` and
  `docs/api/native-format-v1.json` are updated; a migration fixture covers a
  schema-30 document opening in a schema-29 reader (the `edge` fields dropped,
  notice recorded) and a 1.2 refusal of a schema-30 file.

## 7. CLI and assistant tool surface (decision 7)

- **CLI:** `--import-style`, `--export-style`, `--import-catalog`
  (list or import named entries into the open or a new document),
  `--export-catalog` and `--list-catalog`, alongside `--build-assembly`. Each is
  one bounded workflow that publishes nothing on a failed check, following the
  importer/exporter shape of `src/automation/main.cpp`.
- **Commands:** `style.apply`, `style.export`, `catalog.list`, `catalog.import`
  and `catalog.export` (transaction/query commands with the shared dry run,
  revision checks and error codes).
- **Assistant:** the in-app assistant gets **no new tools** (visual/spatial
  work, token cost, ADR 0168/0169 position); MCP exposes every new command;
  `assistant_tool_surface_tests` stays pinned. Exposing a catalog command to the
  assistant later, after a corpus measurement, is the obvious first candidate.

## 8. Desktop UI (decision 8; UX_DESIGN §18 to be designed later)

The owner approves a UX design separately, as with R062 and R096. The design
(§18 of `docs/UX_DESIGN.md`) must decide:

1. Where the **style library** lives (the R062 Styles tab in the model panel,
   a Styles menu, a context menu), its names and any default shortcuts
   (conflicts checked).
2. The **style import/export** flow: folder choice, refresh, previews, and how
  "apply" versus "save to library" is chosen and confirmed.
3. The **extended edge stroke** controls: width, opacity, dash and texture
   selection, and how a missing texture is shown at the pointer.
4. The **Materials tab local library** extension: how textured presets are
  presented, how a preset's texture is shown, and how a preset's asset is bound.
5. The **asset catalog** dialog: folder selection, refresh, search, entry list
  with `kind`, metadata and errors, Insert/Apply, and the missing-dependency
  report tree.
6. Keyboard, accessibility and 1×/2×/fractional/200 % text for all of the
   above (the R096 §8 pattern).

## 9. Budgets (decision 9)

Proposed rows, to be confirmed by measurement before they are frozen (the
ADR 0171 decision 14 / ADR 0169 decision 10 position):

| Operation | Budget |
| --- | --- |
| Apply a style (one labelled style edit) | No measurable cost beyond the existing style edit (appearance rebuild, no retriangulation, ADR 0069) |
| Decode and validate one 130 MiB `.sketchystyle`/`.sketchycat` | Bounded by the 130 MiB envelope; reported |
| Catalog scan (one file, full decode) | Reported; within the existing 256 MiB read allowance |
| Import one catalog entry (one composed edit) | ≤ 3 s with progress and cancel for a 130 MiB source (*proposed*) |
| Style/catalog relocation re-scan | Reported |
| Added resident memory (decoded catalog) | Within the existing memory budget rows (R082) |

## 10. Verification plan

Fixtures (in-repo, generated, no third-party data, the ADR 0169 §9 position):

- a generated style with a sketch edge texture (procedural);
- a catalog with a component entry, a style entry and a shared embedded texture;
- a catalog with a deliberately **missing** texture asset and one with a
  **tampered** asset hash;
- a textured material preset fixture (procedural texture).

| Verify clause | Check |
| --- | --- |
| A style relocates cleanly, preserves appearance | Move the style file to a different directory (and, in the acceptance record, a different mount); re-scan and apply; the style record and the edge-texture asset hash match the original bit for bit |
| A catalog relocates cleanly, preserves appearance | Move the catalog file; re-scan; every entry resolves, hashes match, and an inserted entry's records and their assets are identical to a decode in place |
| Reports missing dependencies | The missing-asset and tampered-hash fixtures: the entry is not imported, the report names the entry and the missing dependency id, and the destination document is unchanged |
| Assets are never substituted silently | A fixture with an equal-hash, different-payload asset is refused (ADR 0131); no fallback colour or default texture is emitted for a missing style texture; `assetStatus`/`missingAssets` reflect the state |
| Refusals leave state unchanged | Each refusal (invalid style field, missing dependency, hash mismatch, over limit) leaves document, history, revision and encoding unchanged |
| Undo / save | Each import is one undo step; save and reopen is bit for bit including the `edge` sub-record and its assets |
| Determinism | The same import on two copies gives identical ids, records and hashes; hashes pinned per fixture |
| Fuzzing | A libFuzzer target for the `edge` sub-record decoder and for the catalog manifest/entry decoder, run twice per input for determinism |

Native tests follow the established matrix: a `style_catalog_input_tests` suite
on X11 and Wayland at 1× and 2× with accessibility, keyboard-reachability and
text-size extensions for the style, preset and catalog workflows.

## 11. Out of scope

For R100:

- SketchUp `.style` / `.skp` file compatibility (proprietary; not verified).
- Hand-drawn (jittered/wobbled) edge rendering as a user operation (a later
  style option, not part of the format).
- Live links from a document back to a catalog or library file (catalogs are
  side libraries, as in R079).
- Remote or hosted catalogs, authentication or automatic synchronization (C04
  territory).
- Per-face edge styles, edge patterns driven by scene or selection, and
  edge-texture animation.
- Growing the six existing colour presets or changing their colours.

## 12. Risks

- **Schema forward-refusals.** Schema 31 is refused by 1.3.x and 1.2. A user
  who applies a style with edge opacity/dash/texture cannot open the result in
  those releases; the refusal and the migration notice must be stated in the
  report and the support matrix.
- **Texture licensing.** Bundling a texture with an unverified licence would
  break the MIT package's distribution story. Mitigation: only
  generated/CC0-verified textures, licence and source recorded per asset,
  collected into `THIRD_PARTY_NOTICES`.
- **Silent substitution regression.** The "report, never substitute" rule must
  hold across style and catalog import; a regression would be a data-integrity
  defect. Mitigation: the refusal fixtures above, the fuzz decoders, and the
  `assetStatus`/`missingAssets` assertions.
- **Catalog decode cost.** A 130 MiB decode before selection can dominate the
  scan; the bounded-scan limits (ADR 0132) and a decode budget with progress and
  cancel bound it.
- **Style snapshot drift.** A style is a snapshot; if `ModelStyle` grows, a
  stored style must round-trip the current 14+ fields without inventing values
  (the ADR 0068 validation contract).

## 13. Reconsideration triggers

- A supported SketchUp `.style` fixture corpus and licence review appear:
  reconsider option C of section 1.
- The owner wants folder-based catalogs for distribution workflows: reconsider
  option B of decision 4.
- Edge-stroke fields prove insufficient for the intended looks (e.g. per-face
  edge styles are needed): the `edge` sub-record widens and the schema bump is
  revisited.

## Implementation split

Each slice is an independently testable review unit. Slices keep documents
without the new content at their current schema (≤ 29) and do not touch
modelling limits.

- **R100.a: The extended style record, schema 31 and the `.sketchystyle`
  format.** The optional `edge` sub-record and `edge-styles-v1` feature,
  `nativeDocumentVersionFor`, the ADR 0108 addendum, `docs/api/native-format-v1.json`,
  the style manifest, the style library folder, `style.apply`/`style.export`,
  and the migration/refusal fixtures and the `edge` fuzz seed. **Verify:** the
  schema-30 round trip, the 1.2 refusal, the migration notice, the
  no-fallback check for a missing texture, and the "no external dependency"
  check for a style export.
- **R100.b: Expanded material presets.** The bounded textured preset set, its
  texture source and per-asset licence/source metadata, the "New material"
  local-library extension, and the licence/notice checks. **Verify:** each
  preset's asset is present and licensed or procedural, no external dependency,
  and the six existing presets are byte-unchanged.
- **R100.c: The `.sketchycat` catalog format.** The manifest of 4.1, shared
  asset deduplication, per-entry and per-asset hashes, the bounded decoder and
  its fuzz target, and `catalog.list`. **Verify:** a relocating catalog
  resolves, a tampered hash is refused, and a missing dependency is reported
  with the entry named.
- **R100.d: Catalog import and insertion.** `catalog.import` through the
  atomic insertion contract (ADR 0131), missing-dependency reporting, and
  `catalog.export` (a capture of the destination's records, the ADR 0130
  capture position). **Verify:** the Verify line (relocation, preserved
  appearance, missing dependencies reported, no silent substitution), one undo
  step, save/reopen, and the refusal-state checks.
- **R100.e: CLI.** `--import-style`, `--export-style`, `--import-catalog`,
  `--export-catalog`, `--list-catalog`. **Verify:** each CLI path, the
  no-publish-on-failure rule, and the `--capabilities`/`--describe-command`
  schemas.
- **R100.f: Desktop tools.** Requires the approved UX design (§18). The style
  library, the extended edge-stroke controls, the textured preset library and
  the catalog dialog, with `style_catalog_input_tests` in the native matrix.
- **R100.g: Acceptance record.** `docs/verification/R100g-custom-styles-assets.md`
  with a JSON summary: the Verify line and every check of section 10, the
  budgets measured and frozen, the native matrix on X11 and Wayland at 1× and
  2×, sanitizers and the fuzz campaigns, the support-matrix row, the
  user-guide section and the exchange-boundaries entry for F06. F06 is not
  delivered until the M10 gate accepts the release.

## Decisions

The owner has not yet decided these points. Each carries the recommendation.

1. **Packaged style format:** a SketchyUp-native single-file `.sketchystyle`,
   kind `style` in the `.sketchylib` envelope, self-contained and relocatable;
   no SketchUp `.style` compatibility claim.
2. **Schema impact:** an optional `edge` sub-record and `edge-styles-v1`
   feature; a document using it is schema 31, refused by 1.3.x/1.2.x with the
   newer-format error; the lowest-schema writer is kept.
3. **Edge texture:** a managed asset reference like a material's asset; a
   missing or hash-mismatched texture reports and never falls back to a flat
   colour.
4. **Material presets:** the six existing colour presets are kept; a bounded set
  of textured presets is added from generated or CC0-verified sources, with no
  external dependency and unverified licences not bundled.
5. **Catalog format:** a self-contained single-file `.sketchycat`, kind
   `catalog`, with distribution metadata, embedded deduplicated assets and
   per-entry/per-asset integrity hashes; a folder-based catalog is the
   alternative.
6. **Missing dependencies:** reported with the entry and dependency id, never
   substituted; a hash match alone never substitutes a resource; a missing
   referenced asset refuses the import.
7. **Catalog provenance:** not persisted in the document; import copies records
  in and stores no catalog path.
8. **CLI:** `--import/--export/--list` for style and catalog, beside
   `--build-assembly`.
9. **Assistant:** no new in-app assistant tools; MCP exposes every new command;
   the tool-surface baseline is re-pinned only if a tool is added.
10. **Budgets:** adopt section 9, to be confirmed by measurement before they
    are frozen.
11. **Desktop UX:** commission and approve a UX design (§18) before R100.f.
12. **Style is a presentation snapshot:** applying a style changes only the
    document style; a style or catalog file is never updated by model edits.

## Owner decisions

Each carries a recommended default and a one-line reason.

1. **Packaged style format:** a SketchyUp-native single-file `.sketchystyle`
   (kind `style`), self-contained and relocatable; no SketchUp `.style` claim.
   *Recommended default: option A — it reuses the hash-verified envelope and the
   "relocation needs one file" precedent, and there is no verified `.style`
   corpus to claim compatibility.*
2. **Schema impact:** an optional `edge` sub-record and `edge-styles-v1`;
   documents using it are schema 31 and refused by 1.3.x/1.2.x; the
   lowest-schema writer is kept. *Recommended default: option 2 — it matches
   the terrain-v1 feature-gating pattern and leaves style-free documents
   unchanged.*
3. **Edge texture:** a managed asset reference; a missing or hash-mismatched
   texture reports and never falls back to a flat colour. *Recommended default:
   yes — it is the Verify line and the ADR 0131 invariant.*
4. **Material presets:** keep the six colour presets; add a bounded set of
   textured presets from generated or CC0-verified sources; do not bundle
   unverified-licence textures. *Recommended default: keep + add textured,
   verified only — it preserves the "no external dependency" contract and an
   honest licence story. (The specific preset list and every bundled texture's
   provenance/licence are **not verified** and are an owner decision.)*
5. **Catalog format:** a self-contained single-file `.sketchycat` (kind
   `catalog`) with distribution metadata and integrity hashes; folder-based is
   the alternative. *Recommended default: single-file — relocation is one file
   and it is consistent with the R079 library pattern.*
6. **Missing dependencies:** reported with the entry and dependency id, never
   substituted; a hash match alone never substitutes a resource. *Recommended
   default: yes — it is the Verify line.*
7. **Catalog provenance:** not persisted in the document; import copies records
  in and stores no catalog path. *Recommended default: not persisted — it keeps
   relocation working and avoids stale references.*
8. **CLI:** `--import/--export/--list` for style and catalog, beside
   `--build-assembly`. *Recommended default: yes — it matches the existing
   importer/exporter CLI shape.*
9. **Assistant:** no new in-app assistant tools; MCP exposes every new command;
   `assistant_tool_surface_tests` stays pinned. *Recommended default: no new
   tools — token cost and the visual/spatial nature of the work, the
   ADR 0168/0169 position.*
10. **Budgets:** adopt section 9, confirmed by measurement before they are
    frozen. *Recommended default: adopt, then confirm — the standing position.*
11. **Desktop UX:** commission and approve a UX design (§18) before R100.f.
    *Recommended default: yes — the R062 and R096 pattern.*
12. **Style is a presentation snapshot:** applying a style changes only the
    document style; a style or catalog file is never updated by model edits.
    *Recommended default: yes — the ADR 0068 and ADR 0133 contract.*

## Questions for the owner

- **Q1. Preset list and texture licences. *Answered by the owner on 2026-10-27:*** only procedural textures or CC0 textures (Poly Haven, ambientCG); a worker drafts the list with each source and licence, and the owner signs it off before R100.b ships. The original question: The exact set of new
  textured presets, and the provenance and licence of every bundled texture,
  are not verified in this repository. The owner is asked to confirm the preset
  list and, with counsel if you wish, that every bundled texture's licence is
  distributable, with attribution where the licence
  requires it.

## Q1 preset list (proposed for owner sign-off, 2026-10-27)

**Status: signed off by the owner on 2026-10-27, not yet bundled.** The owner approved the 15 presets and the size budget below, with one change: R100.b makes the floor tile's grout clearly visible (it is nearly invisible in the preview). The owner also accepted ambientCG's site-wide CC0 statement as sufficient for future presets (no per-asset licence line needed there); no ambientCG asset is in this list. Originally: drafted, not signed off, not bundled. This list was drafted on 2026-10-27 under the owner's Q1 answer (procedural textures, or CC0 textures from Poly Haven or ambientCG). The owner signs the list off before R100.b bundles anything. This section commits no texture bytes. The downloads and the embedded files stay in `.local/r100b-textures/`, outside git. The one image committed is the review contact sheet, [`mockups/presets/contact-sheet.png`](../mockups/presets/contact-sheet.png) (110 KiB). It is derived from the nine listed Poly Haven assets (credited below) and from throwaway procedural previews. It is not the generator's output.

### The list

Fifteen textured presets, in the order the Materials library should show them under **Textured**. Nine are CC0 from Poly Haven and six are procedural. The tile size is the real-world size one texture repeat covers. `src/core/face_textures.hpp` gives implicit mapping one metre per repeat, and each preset sets its own size. The embedded size is the file the preset would bundle after the resize in "CC0 presets: files" below. Procedural sizes are estimates from a preview, not final numbers.

| # | Name (UI) | Source | Tile (m) | Tint | Opacity % | Embedded size (bytes) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Wood, light | CC0, Poly Haven `ash_veneer` | 1.00 × 1.00 | #ffffff | 100 | 30,728 |
| 2 | Wood, dark | CC0, Poly Haven `black_oak_veneer` | 1.00 × 1.00 | #ffffff | 100 | 32,898 |
| 3 | Brick, running bond | Procedural | 0.90 × 0.90 | #9c4a36 | 100 | ≈16,258 |
| 4 | Concrete, cast | Procedural | 2.00 × 2.00 | #b0ada6 | 100 | ≈62,466 |
| 5 | Stone, rubble wall | CC0, Poly Haven `rustic_stone_wall_02` | 1.50 × 1.50 | #ffffff | 100 | 63,050 |
| 6 | Floor tile, 600 mm | Procedural | 0.60 × 0.60 | #d6d1c7 | 100 | ≈5,315 |
| 7 | Plaster, fine | Procedural | 2.00 × 2.00 | #f2efe8 | 100 | ≈4,954 |
| 8 | Metal, brushed | Procedural | 1.00 × 1.00 | #8c96a0 | 100 | ≈15,510 |
| 9 | Metal, corrugated | CC0, Poly Haven `corrugated_iron` | 1.12 × 1.12 | #ffffff | 100 | 53,376 |
| 10 | Glass, reeded | Procedural | 0.30 × 0.30 | #b7d9e5 | 35 | ≈13,826 |
| 11 | Grass | CC0, Poly Haven `leafy_grass` | 2.00 × 2.00 | #ffffff | 100 | 89,503 |
| 12 | Gravel | CC0, Poly Haven `bicolour_gravel` | 1.90 × 1.90 | #ffffff | 100 | 120,454 |
| 13 | Asphalt | CC0, Poly Haven `clean_asphalt` | 2.10 × 2.10 | #ffffff | 100 | 25,760 |
| 14 | Roof tiles, clay | CC0, Poly Haven `clay_roof_tiles_02` | 2.50 × 2.50 | #ffffff | 100 | 71,553 |
| 15 | Fabric, wool | CC0, Poly Haven `poly_wool_herringbone` | 0.27 × 0.28 | #ffffff | 100 | 56,842 |

Tints are sRGB. For a CC0 preset the tint is white, so the albedo keeps its own colours. For a procedural preset the texture is a neutral luminance map and the tint carries the colour. ADR 0065 multiplies the linear tint by the sampled texture RGB, so this works with the existing path. Opacity 35 for Glass copies the flat "Glass" preset. UX_DESIGN §18.4 does not list opacity, so this is a proposal (see open questions).

### Sources and how each was checked

- **Poly Haven (used).** Each asset's own page, `https://polyhaven.com/a/<id>`, was read on 2026-10-27. In each one the data block carries `"license":"https://creativecommons.org/publicdomain/zero/1.0/"` and the `copyrightNotice` quoted in the table below. Each page's keywords include `seamless` and `tileable`. The file list at `https://api.polyhaven.com/files/<id>` gives the 1K diffuse JPEG. The MD5 in that list matched the downloaded file for all nine. The info JSON at `https://api.polyhaven.com/info/<id>` gives the `dimensions`. Poly Haven's licence page, `https://polyhaven.com/license` (read 2026-10-27), says "Our assets are all licensed as CC0" and "You do not need to give credit or attribution when using them (although it is appreciated)". Credit is listed below as a courtesy.
- **Unit of `dimensions`.** The OpenAPI spec at `https://api.polyhaven.com/api-docs/swagger.json` (read 2026-10-27) describes the field as "An array with the dimensions of this asset on each axis in millimeters". The info JSON itself gives no unit. The tile sizes above are therefore the Poly Haven values divided by 1000.
- **ambientCG (not used).** The asset pages read on 2026-10-27, `https://ambientcg.com/view?id=Bricks059`, `Concrete034`, `Wood066` and `Metal049A`, carry the site-wide FAQ sentence "All assets are released under the Creative Commons CC0 license", with a link to `https://docs.ambientcg.com/license/`. They have no licence field for the asset itself. The brief asks for the asset's own page to state CC0, so no ambientCG asset is in the list. This is an owner decision, listed below.

### CC0 presets: licence on the asset's own page

| # | Name (UI) | Poly Haven asset | Creator (credit) | Licence line, quoted from the asset's page | Checked on the asset's own page |
| --- | --- | --- | --- | --- | --- |
| 1 | Wood, light | [ash_veneer](https://polyhaven.com/a/ash_veneer) | Jenelle van Heerden | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 2 | Wood, dark | [black_oak_veneer](https://polyhaven.com/a/black_oak_veneer) | Jenelle van Heerden | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 5 | Stone, rubble wall | [rustic_stone_wall_02](https://polyhaven.com/a/rustic_stone_wall_02) | Amal Kumar | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 9 | Metal, corrugated | [corrugated_iron](https://polyhaven.com/a/corrugated_iron) | Jenelle van Heerden, Dimitrios Savva | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 11 | Grass | [leafy_grass](https://polyhaven.com/a/leafy_grass) | Charlotte Baglioni | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 12 | Gravel | [bicolour_gravel](https://polyhaven.com/a/bicolour_gravel) | Dimitrios Savva | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 13 | Asphalt | [clean_asphalt](https://polyhaven.com/a/clean_asphalt) | Dimitrios Savva | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 14 | Roof tiles, clay | [clay_roof_tiles_02](https://polyhaven.com/a/clay_roof_tiles_02) | Amal Kumar | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |
| 15 | Fabric, wool | [poly_wool_herringbone](https://polyhaven.com/a/poly_wool_herringbone) | colormass, Rico Cilliers | `CC0 1.0 Universal - public domain dedication, no attribution required` | yes, read 2026-10-27 |

### CC0 presets: files

The source is the Poly Haven 1K diffuse JPEG, kept in `.local/r100b-textures/polyhaven/<id>/` and not committed. The embedded file is that source resized and re-encoded, kept in `.local/r100b-textures/embedded/` and not committed. The recipe is `magick SOURCE -strip -colorspace sRGB -resize '512x512>' -sampling-factor 4:2:0 -interlace none -quality 85 OUT.jpg`, with ImageMagick 7.1.2-34 (Q16-HDRI). Two runs produced identical bytes. R100.b must reproduce the same SHA-256, or record the new one with the ImageMagick version.

| # | Name (UI) | Source (downloaded, not committed) | Embedded file (not committed) | Embedded SHA-256 | Tile (m) | Tint |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Wood, light | Diffuse 1K JPEG, 1024×1024, `13fd87c8b0a5cc1d4e408fb75b3eb629417ea6b1aaf1672b5adf993640486673` | `wood-light-ash.jpg`, 512×512, 30728 | `91a0846b4578f6d333a7fa950ac72c87b661eb0d23b45e7a015e89310e4a6b5e` | 1.00 × 1.00 | #ffffff |
| 2 | Wood, dark | Diffuse 1K JPEG, 1024×1024, `7893f9bfeda626364f0a803dea6327c9f3a7b2f2fe330f6b4f326f6b06c0c265` | `wood-dark-oak.jpg`, 512×512, 32898 | `ff787eabf5b8b2638bae2ad4336dcc9f59fa03775a1e5320be8870009cda917d` | 1.00 × 1.00 | #ffffff |
| 5 | Stone, rubble wall | Diffuse 1K JPEG, 1024×1024, `a091e04d73ddf7485d1b99a66d6cfe270e9ba36cdc26f98dbc17934427e43bc9` | `stone-rubble-wall.jpg`, 512×512, 63050 | `6874d83108366863e409de5d23147a4f0602cfd36c4c42bf0232331e302ffe24` | 1.50 × 1.50 | #ffffff |
| 9 | Metal, corrugated | Diffuse 1K JPEG, 1024×1024, `7f3649dd0d056a55a910146f09b1d5298370696a90f511a255fb4c5715bb4b58` | `metal-corrugated.jpg`, 512×512, 53376 | `1e1a5803ac390860b34830eb64c2191b0684c758e0bbf0e6852dc18dd6c64d55` | 1.12 × 1.12 | #ffffff |
| 11 | Grass | Diffuse 1K JPEG, 1024×1024, `cfa40bc9d9417d1852db8753a8d5917f110c40101179f63543c382e39bc05e4a` | `grass.jpg`, 512×512, 89503 | `b46a499a537948bf457533d6e522e545536e83d3a71f7e3cd4276191ca85c3a0` | 2.00 × 2.00 | #ffffff |
| 12 | Gravel | Diffuse 1K JPEG, 1024×1024, `95301f134ad5dcc51442d23f901220743bba63d17e306b5c4131668d4d87d82f` | `gravel.jpg`, 512×512, 120454 | `489e8fb30eb98b8f67158f9a677ebe3a239c8f88b0d6695e706d1b211a47dba8` | 1.90 × 1.90 | #ffffff |
| 13 | Asphalt | Diffuse 1K JPEG, 1024×1024, `ddf9224e9cfa3eb2998d9097e4cd34bd3630c85e0e6798e5a0a000415a684083` | `asphalt.jpg`, 512×512, 25760 | `d0b08e7ac43f3a6ff5e118a0261b3d496f607c37be8731e1bccc95a6790aef8d` | 2.10 × 2.10 | #ffffff |
| 14 | Roof tiles, clay | Diffuse 1K JPEG, 1024×1024, `227a062b38b170e02c27eaa513dae45dc8bc893a62cbd8d0c08e2c32b381fc25` | `roof-tiles-clay.jpg`, 512×512, 71553 | `e6a214840c55dd99e83daf8331931322985bc8c37ea074ebf14fad3945caf013` | 2.50 × 2.50 | #ffffff |
| 15 | Fabric, wool | Diffuse 1K JPEG, 1024×1045, `fd889a46f617efd331e522e36b8ad2548e16e1cb018c001872f17537d7666af5` | `fabric-wool-herringbone.jpg`, 502×512, 56842 | `6d18bc27c52f70dc73e5e53144f80c62b2cf281ea6be7ae49c562d71d051e04d` | 0.27 × 0.28 | #ffffff |

Credits, for `THIRD_PARTY_NOTICES` as a courtesy (CC0 needs none):

- Wood, light: Jenelle van Heerden, [ash_veneer](https://polyhaven.com/a/ash_veneer)
- Wood, dark: Jenelle van Heerden, [black_oak_veneer](https://polyhaven.com/a/black_oak_veneer)
- Stone, rubble wall: Amal Kumar, [rustic_stone_wall_02](https://polyhaven.com/a/rustic_stone_wall_02)
- Metal, corrugated: Jenelle van Heerden, Dimitrios Savva, [corrugated_iron](https://polyhaven.com/a/corrugated_iron)
- Grass: Charlotte Baglioni, [leafy_grass](https://polyhaven.com/a/leafy_grass)
- Gravel: Dimitrios Savva, [bicolour_gravel](https://polyhaven.com/a/bicolour_gravel)
- Asphalt: Dimitrios Savva, [clean_asphalt](https://polyhaven.com/a/clean_asphalt)
- Roof tiles, clay: Amal Kumar, [clay_roof_tiles_02](https://polyhaven.com/a/clay_roof_tiles_02)
- Fabric, wool: colormass, Rico Cilliers, [poly_wool_herringbone](https://polyhaven.com/a/poly_wool_herringbone)

### Procedural presets: generator descriptions (no code yet)

Shared rules for all six:

- **Output.** 512 × 512 px. One luminance value per pixel, written to R, G and B in sRGB, encoded as JPEG at quality 85 through Qt, with no metadata. The generator makes only neutral luminance. The tint carries the colour.
- **Periodic.** The tile repeats with no seam. Value noise uses a lattice that wraps, and 1-D streaks wrap around.
- **Deterministic.** Each preset has a fixed seed. The PRNG is a documented integer generator (SplitMix64 is proposed). Arithmetic is double. Values are quantised to 8 bits with round-half-up. R100.b pins each preset's output bytes with a fixture SHA-256, which is the "SHA-256 of the exact file" for a procedural preset.
- **Inputs.** The tile size, the seed and the constants below. No file reads and no network.

The seeds and preview bytes below come from a throwaway NumPy preview (`PCG64`, seeds 11 to 16). The preview is only for the contact sheet and for a rough size. It is not the generator, and R100.b's bytes will differ.

| # | Name (UI) | Generator | Tile (m) | Tint | Opacity % | Seed (preview) | Preview JPEG (bytes) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 3 | Brick, running bond | Brick bond | 0.90 × 0.90 | #9c4a36 | 100 | 11 | 16,258 |
| 4 | Concrete, cast | fBm noise with pits | 2.00 × 2.00 | #b0ada6 | 100 | 14 | 62,466 |
| 6 | Floor tile, 600 mm | Tile grid | 0.60 × 0.60 | #d6d1c7 | 100 | 12 | 5,315 |
| 7 | Plaster, fine | fBm noise | 2.00 × 2.00 | #f2efe8 | 100 | 13 | 4,954 |
| 8 | Metal, brushed | Row streaks | 1.00 × 1.00 | #8c96a0 | 100 | 15 | 15,510 |
| 10 | Glass, reeded | Vertical ribs | 0.30 × 0.30 | #b7d9e5 | 35 | 16 | 13,826 |

- **Brick, running bond (3).** Brick 215 × 65 mm, mortar joint 10 mm, course 75 mm. Twelve courses, four bricks per course at a 225 mm pitch. Odd courses are offset by half a pitch. The tile is 0.90 m square, which is 12 × 75 mm and 4 × 225 mm, so it repeats seamlessly. Brick face luminance is 0.96, with a per-brick offset of ±0.04 from the seed and fine noise. Mortar is 0.76.
- **Concrete, cast (4).** Tile 2.00 m square. Periodic fBm value noise, 4 octaves, base 8 cells per tile, luminance 0.85 to 1.00. Add 1,200 seeded pits of radius 1 to 3 px that darken to 0.65, and a speckle with σ 0.01. The tint equals the flat "Concrete" colour, `#b0ada6`.
- **Floor tile, 600 mm (6).** Tile 0.60 m square, one tile per repeat. The 3 mm grout is drawn at least 3 px wide at the edges, at luminance 0.70. The field is 0.95 with ±0.015 of 4-octave fBm.
- **Plaster, fine (7).** Tile 2.00 m square. Periodic fBm, 4 octaves, base 4 cells, luminance 0.88 to 1.00.
- **Metal, brushed (8).** Tile 1.00 m square. One random value per row, smoothed by a 5-tap periodic kernel, gives horizontal streaks of ±0.03. An anisotropic fBm, 3 octaves on 8 × 64 base cells, adds ±0.02. Luminance is 0.85 ± 0.06.
- **Glass, reeded (10).** Tile 0.30 m square with 12 vertical ribs at a 25 mm pitch. The profile is the square root of (0.5 + 0.5 cos φ), which gives luminance 0.82 to 1.00. A per-rib offset of ±0.02 comes from the seed. Opacity 35 follows the flat "Glass" preset.

### Size budget (proposed; measured where a file exists)

| Item | Proposed budget | Measured now |
| --- | --- | --- |
| One embedded file | ≤ 512 px on the long edge; JPEG q85; sRGB; no metadata; ≤ 200 KiB | CC0: 25,760 to 120,454 bytes; the largest is 117.6 KiB. Procedural: 4,954 to 62,466 bytes (preview estimate) |
| The 15-preset set | ≤ 1 MiB | 662,493 bytes (647.0 KiB): CC0 544,164 bytes (531.4 KiB); procedural preview estimate 118,329 bytes (115.6 KiB) |
| Document impact | One asset per placed preset; identical bytes reused (ADR 0131). Decoded RGBA is 1 MiB per preset (512 × 512 × 4), well inside the 4096 px and 64 MiB decoder limits (`TextureImage`) | Largest preset 117.6 KiB, so 0.12 MiB or less per material. The whole set is 0.99% of the 64 MiB payload limit (`assetTotalLimit`) and 15 of 1,024 asset slots |
| Package impact | ≤ 1 MiB added to the uncompressed package. JPEG does not recompress under zstd, so the package grows by about this much | About 0.63 MiB. R100.b measures the real figure with its chosen install path (Qt resource or installed file) |

Procedural sizes must be re-measured with the Qt encoder in R100.b. The owner signs off the budget with the list.

### Notices (proposed text)

If the list is signed off, R100.b commits a notice file `third_party/textures/PolyHaven-CC0.txt` and adds a line to `CMakeLists.txt`, so that `scripts/release-artifacts.sh` collects it into `THIRD_PARTY_NOTICES`:

```cmake
install(FILES third_party/textures/PolyHaven-CC0.txt DESTINATION share/licenses/sketchyup RENAME Textures-PolyHaven-CC0)
```

Proposed content of that file:

```text
Textures in the material presets (ADR 0174 Q1; R100.b)

Wood, light; Wood, dark; Stone, rubble wall; Metal, corrugated; Grass; Gravel; Asphalt;
Roof tiles, clay; Fabric, wool are derived from Poly Haven assets (https://polyhaven.com).
Each asset is licensed CC0 1.0 Universal (https://creativecommons.org/publicdomain/zero/1.0/),
as stated on the asset's own page, checked 2026-10-27. No attribution is required.
Credits are given as a courtesy:
  Ash Veneer, Jenelle van Heerden: https://polyhaven.com/a/ash_veneer
  Black Oak Veneer, Jenelle van Heerden: https://polyhaven.com/a/black_oak_veneer
  Rustic Stone Wall 02, Amal Kumar: https://polyhaven.com/a/rustic_stone_wall_02
  Corrugated Iron, Jenelle van Heerden and Dimitrios Savva: https://polyhaven.com/a/corrugated_iron
  Leafy Grass, Charlotte Baglioni: https://polyhaven.com/a/leafy_grass
  Bicolour Gravel, Dimitrios Savva: https://polyhaven.com/a/bicolour_gravel
  Clean Asphalt, Dimitrios Savva: https://polyhaven.com/a/clean_asphalt
  Clay Roof Tiles 02, Amal Kumar: https://polyhaven.com/a/clay_roof_tiles_02
  Poly Wool Herringbone, colormass and Rico Cilliers: https://polyhaven.com/a/poly_wool_herringbone

Brick, running bond; Concrete, cast; Floor tile, 600 mm; Plaster, fine; Metal, brushed;
Glass, reeded are generated by SketchyUp and need no notice.
```

### Open questions for the owner

1. **ambientCG.** The asset pages carry only the site-wide CC0 sentence, not a per-asset licence. Is the site-wide sentence enough? If yes, the next step is to add ambientCG alternates, verified the same way. No ambientCG bytes have been downloaded.
2. **One tint per preset.** The brick mortar and the floor-tile grout take a darker shade of the preset tint. A separate mortar or grout colour would need a second colour per preset, which the design does not have.
3. **Glass opacity.** The list gives Glass 35 %, copied from the flat preset. UX_DESIGN §18.4 does not list opacity. Please confirm that a textured preset sets opacity.
4. **Tile sizes.** The sizes are Poly Haven's millimetre figures, divided by 1000. R100.b should check the scale on screen before the list is frozen.
5. **No mipmaps (ADR 0065).** Viewed at a distance, a 512 px tile can alias. R100.b should measure this at 1× and 2×, and decide whether 512 px is the size to keep.

### Corrections made with this section

- The status line at the top of this ADR said the list was "signed off by the owner". It was not. The line now says the drafted list awaits sign-off.
- The R100 row in `docs/PR_ROADMAP.md` says the list is drafted and awaits sign-off, with nothing bundled.
- No code was changed, and the six colour presets in `materials_panel.cpp` are untouched. The mapping and limit facts cited here (one metre per implicit repeat in `face_textures.hpp`; 16 MiB per asset payload and 64 MiB per document in `asset_records.hpp`; 4096 px and 64 MiB RGBA in `texture_image.hpp`) were read at origin/main `607cd670`.

## Implementation notes (R100.a)

Recorded 2026-10-26 for branch `feat/r100a-edge-styles`, on `de83e219`.

- **Schema numbering (the reading the brief sets).** Schema 30 is R099.a's `presentation-v1`, so
  this slice is schema 31 with `edge-styles-v1`. Read the ADR's "schema-30" sentences as schema
  31: the round trip is at 31, a document without an edge stroke writes what it wrote before
  (26 to 30 by content), and a document with one writes 31. Cumulative features follow R099.a: a
  schema-31 container also carries `presentation-v1` and `terrain-v1`, and its payload is
  `json-v31`. `nativeDocumentVersionFor` returns 31 for an edge stroke on the document style or on
  a saved scene's style, 30 for presentation records, and the earlier ladder otherwise.
  `nativePresentationDocumentVersion` names the 30 that the presentation branch returns.
- **The `edge` sub-record is stored as `edgeStroke`.** §2 calls it `edge`, but the style already
  has an `edge` key, the RGB edge colour. The two cannot share one object. The sub-record is the
  optional `edgeStroke` field of the style, with `width` (1 to 8, `profileWidth` semantics),
  `opacity` (0.01 to 1), `dash` and `texture` (a canonical decimal asset id, `"0"` for none).
  `ModelStyle::edgeStroke` is an `std::optional`, so an absent stroke leaves the 14-field record
  and its bytes unchanged. **Owner: confirm the key name** (`edgeStroke`) or choose another.
- **Dash patterns.** §2 offers named patterns or a bounded on/off run list. This slice takes the
  named set: `solid`, `dashed`, `dotted` and `dashDot`. The run list is not built.
- **Missing or mismatched textures.** A texture asset must exist in the document (an unknown id
  refuses the edit, as for materials). The same rule covers a saved scene's style, so the asset
  cannot be deleted under a scene that names it. A missing asset is a valid state and is not
  replaced.
  `style.apply` refuses a style whose texture is missing or hash-mismatched and names it by id and
  name in the error; the document, its revision and its history are unchanged. No fallback colour
  or texture is produced.
- **The `.sketchystyle` envelope (§1.2, a contradiction with the ADR text).** §1.2 says a style file
  is the existing v1 manifest with `kind` `style`, and also that a style file cannot contain a
  model. The `.sketchylib` envelope requires a model and a thumbnail, so both cannot hold. This
  slice keeps the 24-byte header and the manifest, and drops `model` and `thumbnail` for styles.
  The header's payload length is the texture's byte count (zero when there is none), and its
  thumbnail length must be zero. The manifest carries `name`, `description`, `labels`, the `style`
  record and a `texture` descriptor (`id`, `name`, `mediaType`, `missing`, `bytes`, `sha256`), or
  `null`. The texture must be a PNG or JPEG that decodes as a complete supported image.
  **Owner: confirm this layout.**
- **Export publishes nothing in this slice (§1.3 and §7).** The style export returns the file as
  base64 in `data`. `writeStyleBundle` publishes a file with the atomic new-file publisher, and
  the CLI that calls it is R100.e.
- **Commands (§7).** `style.apply` is a command. `style.export` is a query (`inspection`
  registry), because the batch engine refuses a batch with no committed change, and an export
  changes nothing. Neither reads or writes a path; the bytes travel as canonical base64 (the
  `asset.import` rule). `style.apply` replaces the document style and adds its texture (or reuses
  an asset with the same media type and byte-equal payload, ADR 0131) in one labelled edit. A
  second apply of the same style changes nothing, so the batch is refused as every no-op batch is.
- **Assistant (decision 9).** Every `style.` name is hidden from the in-app assistant, and the
  `assistant_tool_surface_tests` authorization set excludes it. The assistant's baseline is
  unchanged. The `document.style` and saved-scene schemas are also unchanged, so the edge stroke
  cannot be set through a command. Only `style.apply` sets it in R100.a. **Owner: decide whether
  the edge stroke joins the `document.style` schema now, which changes the assistant's pinned
  tool bytes and so needs a re-pin under decision 9, or waits for R100.f.**
- **Rendering is not in this slice.** The viewport still draws edges at the flat colour and
  `profileWidth`. Edge opacity, dash and texture are stored and validated, but nothing draws them.
  ADR 0174 §2 expects the texture to be drawn by the existing image path; that belongs to the
  desktop slice.
- **Released-reader refusal (measured, not assumed).** Both released CLIs refuse
  `tests/fixtures/edge-styles-v31.sketchyup` with exit 1 and the same message as their refusal of
  schema 30 in R099.a: `{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}`.
  The file is byte-identical afterwards (`cmp`).
  - `sketchyup-cli 1.3.0`, from `sketchyup-1.3.0-1-x86_64.pkg.tar.zst` (sha256
    `5f5c0cf8…a833b`, from the release `SHA256SUMS`), downloaded into a scratch folder and
    extracted there. It was not installed.
  - `sketchyup-cli 1.2.0`, from `sketchyup-1.2.0-1-x86_64.pkg.tar.zst` (sha256 `b95fc189…31295d`).
  - The in-repo test `style_bundle_tests` runs the 1.3.0 CLI when `SKETCHYUP_PRIOR_CLI` is set.
  - A simulation of the package prior-release loop over `tests/fixtures`: 1.2.0 (reads 28) and
    1.3.0 (reads 29) each refuse both newer fixtures, `presentation-v30` and `edge-styles-v31`,
    with their bytes unchanged. The full package acceptance (`verify-package.sh` in its container)
    was not run here.
- **The refusal wording differs from §2 and §6 (contradiction).** §2 says 1.3.x and 1.2.x refuse
  schema 31 "with the newer-format error (the terrain-v1 precedent, ADR 0168 §2)". Measured, they
  refuse with `Unsupported container field`, which is the container's field check. That is the same
  finding R099.a recorded for schema 30. The check still refuses and leaves the file unchanged.
  `verify-package.sh` already checks by exit status and `status == failed`, so it needs no change.
  Its prior stays `1.2.0-1` in CI. Adding `1.3.0-1` to `released_packages` is safe at the next
  format release, as R099.a's note said.
- **Contradictions for the owner.**
  1. §2 and §6 say that a schema-30 document "migrates to the current record with the `edge`
     fields dropped and a notice". A schema-30 document has no edge fields, because they arrive
     in schema 31, so there is nothing to drop. No migration notice and no migration fixture were
     built. A schema-29 reader refuses schema 30 at the container (measured above for 1.3.0).
  2. §6 says the read range grows to "1–30". It is 1–31 under the numbering this slice takes.
  3. §2 says a style that uses only the 14 fields "stays schema ≤ 29". It is ≤ 30 now that
     presentation-v1 is schema 30; the R099.a numbering covers this.
  4. §5 says the report reuses `assetStatus` and `missingAssets`, extended to name the entry.
     Neither changed. `style.apply` names the missing texture in its refusal, and
     `document.describe` does not report the edge texture's status yet.
- **Not measured.** The §9 budgets (apply cost, decode of a 16 MiB texture, the import budget) are
  not measured in this slice. A 4 x 4 texture is the only fixture, so the decode limits were not
  stressed. No frame or screenshot evidence exists, because nothing draws the stroke yet.
- **Fuzzing.** `parser_fuzz_edge_style` (format `edge-style`) decodes each input twice and requires
  equal results. An accepted stroke is within its bounds and re-encodes canonically. Its seeds are
  four accepted strokes (defaults, both bounds with a texture, dashed, dotted with the largest id)
  and eight rejected inputs (a width of 0.5 and 8.5, an opacity of 0 and 1.5, an unknown dash, a
  non-canonical id, a negative id and an unknown field). `parser_fuzz_seeds` was built and run
  locally: all twelve seeds validate as the harness expects, and the harness compiles with
  `-fsyntax-only`. The libFuzzer target was not built, because it needs clang and the fuzz
  configuration.
- **Tests.** `style_bundle_tests` (new): schema selection and the lowest-schema writer; the
  schema-31 round trip, bit for bit, and the checked-in fixture; the decoder bounds; the style file
  round trip, the header layout and the refusals (kind, thumbnail, trailing data, GIF, undecodable
  bytes, a texture the file does not carry); missing and hash-mismatched textures (apply and
  export); apply as one undoable edit with reuse by bytes; the export query; relocation to a new
  folder with no external dependency; the style folder scan; the command path; and the 1.3.0
  refusal. The existing sweeps (`native_format_tests`, `presentation_io_tests`,
  `drawing_sheet_io_tests`, `component_parameter_io_tests`) name schema 31 and exempt its fixture.
  `command_tests` and `inspection_tests` gained a case each, because each published command and
  query must have one.
- **Schema validation.** `docs/api/native-format-v1.json` names 31 and reads 1–31. The discovery
  schemas were regenerated with `scripts/generate-api-schemas.py` and `generate-tool-reference.py`;
  both `--check` runs pass against the built binaries.
- **Not run here.** The Qt 6.8 reference compile, the sanitizer job, the native matrix and the
  package acceptance run only in CI.

## Implementation notes (R100.c)

Recorded when the `.sketchycat` manifest, writer, verifying reader, `catalog.list`, fuzz
target and fixtures were implemented (branch `feat/r100c-catalog-format`).

**Format.** A catalog is the `.sketchylib` envelope with `kind = "catalog"`, the same 24-byte
header and the same magic. The manifest is JSON of at most 32 KiB. The payload region holds
every shared asset, then every entry payload, in id order and contiguous, and each record
addresses its bytes by an offset from the region start. The envelope's thumbnail length must be
zero, because a catalog has no thumbnail. Nothing is stored by absolute path, so a moved file
resolves. The shared envelope helpers (header, hash, field and text checks) moved into
`src/io/library_envelope.hpp`; the template and component readers use them unchanged.

**Entries.** The four kinds are `component` (a native container with a captured definition),
`template` (a native container), `style` (the record that `encodeModelStyle` writes, and
`decodeModelStyle` reads, so R100.a's `edge` sub-record is carried once it merges) and `material`
(a JSON record: name, colour, opacity and an asset id, with zero for none). Each entry carries a
SHA-256 of its payload and a `dependencies` list whose kinds are `asset` and `entry`.

**Bounds chosen** (`CatalogLimits` in `src/io/catalog_io.hpp`): manifest 32 KiB; payload region
128 MiB; file 130 MiB; a style or material record 16 KiB; 128 entries; 256 assets; 64 assets total
16 MiB each and 64 MiB together; 64 dependencies per entry; 32 labels of 64 bytes; names 256 bytes;
descriptions 4 KiB; the author 256 bytes; the licence text 4 KiB and its identifier 64 bytes; the
version 64 bytes; asset names 1 KiB; media types 128 bytes in lowercase `type/subtype` form; the
catalog id 1 to 64 letters, digits, dots, underscores or hyphens. Every count and length is checked
before its allocation, and the reader checks each record against the envelope before it keeps the
record.

**Decisions made here that the ADR did not settle:**

- *Canonical layout.* Ids run from 1 in order, and offsets are contiguous. A layout fault refuses
  the whole file.
- *Unreferenced assets are refused* (a whole-file fault), so no padding can ride along.
- *Dependencies name earlier entries only.* An entry dependency on itself or a later entry is not
  importable, which rules out cycles without a recursion limit.
- *A material declares exactly its asset.* Its payload asset id must match its one asset
  dependency, or the entry is not importable.
- *A material's asset is a dependency*, so the writer adds it and the reader requires it.
- *The writer refuses output its own reader would not list as importable.*
- *Deduplication* compares media type and bytes; the SHA-256 is only the index (ADR 0131). The
  first name is kept.

**Verification on read.** The reader checks each asset hash, then each entry hash, then each
declared dependency (an asset that is absent or mismatched; an entry that is absent, not earlier,
or not importable), then each payload decode. The first failure gives the entry's reason, and the
entry keeps its name and the dependency it names. Component and template payloads are fully
decoded, and the decoded document is then discarded. R100.d decodes again on import, so the
reader keeps no document in memory. Nothing is substituted: a hash match never replaces a
resource.

**`catalog.list`.** A read-only inspection query. It takes the catalog as unpadded base64url
in `catalog`, so the query reads no path (the MCP scope is "no arbitrary paths"). It is listed
beside the other queries and is hidden from the in-app assistant, which the assistant
tool-surface baseline pins. Its output lists each entry's kind, name, importable state, reason
(when not importable) and dependency status, with `assetStatus` per asset and a `missingAssets`
count. The inspection layer caps a request at 16 KiB and a response at 256 KiB, so the query
lists catalogs of about 12 KiB or less. Larger catalogs need the file-based CLI surface of R100.e.

**MCP bound (review).** `catalog.list` is the 64th MCP tool. The `McpServer` bound was a sanity
check from the first MCP slice, not a client limit, and §7 exposes every new command on MCP, so the
review raised it from 64 to 128. **Open for R100.d:** an inline catalog is capped near 12 KiB by
the inspection request size, so `catalog.import` (and a useful `catalog.list`) over MCP need a
catalog reference that is not inline bytes and not an arbitrary path, for example a name in the
style/catalog library folder of §1. R100.d decides it.

**Measured.** The generated `basic` fixture (one shared texture, a style, two materials that
share the texture, a template and a component) is 10,246 bytes. The fuzz determinism test ran
626 inputs twice each with the same result: 8 seeds, 106 truncations and 512 byte flips (102 accepted,
524 rejected).

**Gaps against the ADR:**

- *Component definitions are not deduplicated at catalog level.* A component's container embeds
  its own assets and definitions, so a texture used by two components is stored twice. The ADR's
  "a nested component definition is stored once" is not met yet. Textures are shared only through
  asset dependencies.
- *No libFuzzer smoke step in CI.* `parser_fuzz_catalog` builds under
  `SKETCHYUP_BUILD_FUZZERS` with Clang, as the other parser targets do. The sanitize job is GCC, and
  the only libFuzzer job is the SKP job, which has no Qt. The determinism test runs in ctest and
  under the sanitizers in CI.
- *`catalog.list` is size-limited* as described above.
- *Style edge fields* (R100.a) are not in the style record yet, so catalog style entries carry the
  current 14 fields.

## Implementation notes (R100.d)

Recorded for branch `feat/r100d-catalog-import`, on `a8ef9eef` (origin/main).

**Reference decision (the MCP bound left open by R100.c).** A catalog is named, never given
as bytes or a path. `catalog.list` and `catalog.import` take `{"library": "<name>"}`, or the
inline `catalog` form as before; exactly one is required. The library is one fixed user folder,
`<AppDataLocation>/catalogs` (on Linux, `$XDG_DATA_HOME/SketchyUp/SketchyUp/catalogs`). A request
cannot change the folder. A name is 1 to 64 characters: letters, digits, spaces, dots,
underscores and hyphens, starting and ending with a letter, digit, underscore or hyphen. It is
the file's base name without `.sketchycat`. The resolved file must be a regular file, not a link
or a folder, and it is read through the bounded loader (130 MiB at most). The name check uses
`\A`/`\z`, because `$` also matches before a final newline.

Why this form: inline bytes are capped near 12 KiB by the inspection request, and a transaction
request is capped at 64 KiB, so inline cannot carry real catalogs. A path breaks the MCP "no
arbitrary paths" scope. The brief asked the catalog folder to mirror the style folder of R100.a,
but R100.a has no fixed style folder: `scanStyleDirectory` takes a caller path, and no command
names one. The catalog folder is therefore the first fixed user folder that MCP can name. R100.f
should decide whether styles get the same reference.

**Listing (added beyond the brief's list).** `catalog.library` lists the folder, so a name can be
found over MCP. It is read-only, returns no paths, and scans at most 4096 folder entries and 128
catalogs. Each catalog is fully decoded, within the 256 MiB read budget that `scanStyleDirectory`
uses. A file that cannot be read, or whose name cannot be referenced, is listed with its error.

**Import (ADR 0131).** `importCatalogEntries` matches each name to exactly one entry. A name that
matches none, or several, is refused. An entry's entry dependencies come with it. A dependency
always names an earlier entry, so the walk ends. Every entry in the closure is checked before the
first change. A non-importable entry is refused with the reason, which names the missing or
mismatched dependency id. A template is refused. The work runs on a snapshot and publishes one
composed edit, so a refusal changes neither the document, its revision nor its history. A
component goes through `insertLibraryComponent`, which gives fresh definition identities and
places the component at the root or under `parent`. A material is reused only for the same name,
colour, opacity and asset. An asset is reused only for the same media type and equal bytes. A
clashing name takes the ADR 0131 suffix. A style replaces the document style, as `style.apply`
does, and changes nothing when it is equal (ADR 0068). `catalog.import` is a transaction command
and runs through the batch engine, so MCP and the session share its dry run, revision guard and
error codes.

Decisions the ADR did not settle:

- *A refused request imports nothing.* If one named entry is refused, the whole request is
  refused, so no partial state needs undoing. The brief's "refuses that entry only" is met by the
  other entries of the same catalog staying importable, and by a second request for them.
- *Templates are refused.* The brief said "a component or template inserted". A template is a
  whole model: inserting its free geometry, scenes and tags into an open model is a separate
  design. **Gap:** a template entry cannot be imported. It opens as a new model, as before.
- *Placement.* Components are placed at the identity transform. No transform parameter exists yet.
- *No-op requests.* A request that changes nothing (a style equal to the current one, or a
  material that already exists) is refused with "Batch has no committed changes", the rule every
  no-op batch follows.

**Export.** `catalog.export` is a query, so it changes nothing and publishes nothing. It captures
chosen components by definition id (the ADR 0130 closure, through `CatalogBuilder::addComponent`),
chosen materials with their textures, and the document style with its edge texture. A texture
shared by a material and by the style is stored once, by `CatalogBuilder`'s byte-equality rule.
Entry names must be unique. A missing record, an asset with no bytes (never left out), a repeated
name or an empty selection refuses the export and names what failed. The catalog id is derived
from the name. The licence identifier defaults to `LicenseRef-Unspecified`, and the version to
`1.0.0`. Both are defaults the owner may want changed. The catalog is returned as unpadded
base64url in `data`, which is the form `catalog.list` accepts. The query response caps it at 192
KiB. Writing an export to the library is not in this slice: no MCP path writes a file, so the
file write belongs to R100.e (CLI) or R100.f (UI).

**Reader change (the edge stroke).** R100.c's reader called `decodeModelStyle` without the
edge-stroke flag, so no catalog style could carry an edge stroke. The reader now accepts it, and
requires the edge texture to be a declared asset dependency of that style entry. Otherwise the
entry is not importable, with the reason "style edge texture asset N is not a dependency". The
layout is unchanged: the committed R100.c fixtures decode to the same entries. The R100.c
`catalog_tests` pass against the committed bytes.

**Component-definition deduplication: not done.** This is the one scope item the slice does not
meet, and the brief asked for an explanation. A component entry's payload is a complete native
container from `encodeContainer`. Its definitions sit in one JSON section, and the native reader
requires the complete reference graph (ADR 0130). So a nested definition shared by two entries is
stored in each container. Storing it once needs a partial container form that names definitions
held by other entries, plus an import-time merge before the native reader runs. That changes the
native container's definition encoding and its reader, which is a native-format decision (a
schema question and an ADR addendum), not a catalog-layer change. The R100.c layout is unchanged,
so this slice sets no `full-ci` label. Textures are shared, as R100.c already shares them.

**Undo and save.** One undo removes every record an import adds. The test checks the records. It
does not check the allocator floors (the next asset, material and definition ids). Those stay
advanced after undo, as after any edit, so an id is never reused. Save and reopen gives the same
container bytes, bit for bit.

**Assistant and surface.** Every `catalog.` name stays hidden from the in-app assistant
(`assistantVisible`). The pinned assistant baseline is unchanged. `assistant_tool_surface_tests`
keeps its own copy of the hide rule, which drops `style.` from the "every command" set. It needed a
`catalog.` line, and its baseline assertions are untouched. The batch response gained a
`catalogOperations` array beside `styleOperations`. The generated API documents and
`TOOL_REFERENCE` were regenerated (see the Validation note below).

**Tests.** `catalog_import_tests` runs in 0.23 s with 34 MB peak resident memory. It covers the
reference (name rules, traversal, links, folders, malformed files, the scan's bounds, and the
query surface). It covers export then import, with materials, textures, style fields including
`edgeStroke`, and one shared texture. It covers save and reopen, one undo step, relocation (a
moved file read inline, and a library rename), and missing and tampered dependencies, where only
the affected entry is refused and named. It also covers an undeclared style texture, a template,
unknown and ambiguous names, and eight refusal kinds that leave the document unchanged, and export
refusals. Every test writes only to a temporary home, redirected with `XDG_DATA_HOME` and `HOME`.
The new fixture, `tests/fixtures/catalog/edge-texture.sketchycat` (5,869 bytes), comes from
`catalog_fixtures.hpp` with fixed identities, and `catalog_tests` checks it byte for byte.

**Not measured or not run here.** The §9 budgets (an import of one 130 MiB entry within 3 s, and
resident memory) were not measured. The fixtures are small. The local toolchain is Qt 6.12 with
GCC 16, not the CI image's Qt 6.8, so the code was checked against 6.12 only. The sanitizer job,
the native matrix and the package acceptance run only in CI. The fuzz target
`parser_fuzz_catalog` was not extended for edge-stroke style entries, and the libFuzzer target was
not built. The style decode path now takes the edge flag. `catalog_fuzz_tests`, which runs the same
seeds and mutations under ctest, passes.

## Implementation notes (R100.e)

Recorded for branch `feat/r100e-style-catalog-cli`, on origin/main `fdd18797`.

**Shape.** The five flags are standalone operations in `src/automation/style_catalog_cli.cpp`.
Each takes only the options it names, and refuses the rest by name. `--input` is read and never
written. Each operation publishes at most one new file, through `publishNewFile`, after every
check has passed. Their options are listed once, in `styleCatalogOptions()`. The same list
registers the parser options, fills the `styleCatalog` section of `--capabilities`, and answers
`--describe-command` for each operation. No command is added, so the command catalogue, the MCP
surface and `assistant_tool_surface_tests` are unchanged.

**Output precedent.** The import operations write their output with `createNativeFile`, as
`--import-skp`, `--import-ifc`, `--import-stl` and `--import-obj` do: a new file only. The generic
`--script ... --output` path overwrites an existing file (after a `.bak` copy), so it is not the
precedent here. The five flags refuse an existing path, as the brief requires.

**Errors.** Every refusal exits 1 with the usual `{status, code, error}` object on stderr.
`INVALID_REQUEST`: a conflicting or missing flag, an existing or link path, a wrong suffix, an
unknown or ambiguous entry name, an empty selection, a malformed ID, an invalid library name.
`INPUT_ERROR`: an unreadable or malformed file, a missing or hash-mismatched texture (named by
name and asset ID), an entry refused by the catalog reader or the insertion check (named by entry,
with the reason, which names the dependency ID), and a template. `NOT_FOUND`: a catalog file or a
library name that does not exist.

**Decisions the ADR did not settle.**

1. `--entries` repeats (`--entries A --entries B`). Entry names are free text, so one flag per name
   is unambiguous.
2. A catalog named on the command line is a file when its value contains a slash or ends in
   `.sketchycat`. Any other value is a library name, under the R100.d name rule. An export to a
   path must end in `.sketchycat`, and a style export to a path must end in `.sketchystyle`.
3. `--to-library` writes `NAME.sketchycat` into the catalog library folder. R100.c and R100.d left
   this write to R100.e. The folder is created on first use, after the bytes are built, and an
   existing name is refused. A failed write can leave the empty folder, never a file.
4. Metadata: `--name` (default: the file's base name), `--description`, `--label` (repeatable),
   `--catalog-version` (default `1.0.0`) and `--catalog-license` (default
   `LicenseRef-Unspecified`). The name `--version` belongs to Qt's parser.
5. Selection follows `catalog.export`: `--catalog-component ID` and `--catalog-material ID`
   (repeatable), and `--catalog-style`. The `catalog-` prefix keeps the flags apart from the
   report's `--material` filter, which the same parser also holds.
6. An import into a model that already has the style is not refused. The output is written, and
   the style reports `unchanged`. The command refuses a no-op batch; the CLI writes a copy.
7. `--import-catalog` has no `--parent`. Components are placed at the root, as the command does by
   default.
8. There is no style library option. R100.a has no fixed style folder, and R100.d left that choice
   to R100.f.

**Contradictions and gaps against the ADR.**

- Section 7 says `--import-catalog` lists or imports "into the open or a new document". The CLI
  imports into `--input` and writes a new file. Listing is `--list-catalog`. Importing a template
  as a new model is not built: R100.d refuses templates, and that stands.
- No file format changed. No schema, fixture or decoder changed. The damaged style files that the
  test needs (a missing texture, a flipped texture byte) are made inside the test from the R100.a
  fixture, so no new fixture was added.
- The new files are written with mode `0600`. That is what `publishNewFile` does for every native
  output, and the new flags do not change it.

**Tests.** `style_catalog_cli_tests` is new. It runs the built `sketchyup-cli` and is registered
under `SKETCHYUP_BUILD_CLI`. It skips with exit 77 off Linux, as `report_schedule_cli` does. It
covers:

- discovery: `--capabilities` and `--describe-command` list all five operations and every option,
  and agree with each other;
- every flag on success, and the refusals listed above, each checked with its code;
- for every refusal, the folder's file names before and after, so a partial file would show;
- that `--input` and the catalog are unchanged (SHA-256);
- round trips: a style exported and imported again, and the document style carried in a catalog,
  both keep their appearance, texture bytes included;
- the library: a listing by name, an import by name, and `--to-library` with its refusals.

`HOME` and `XDG_DATA_HOME` point into a `QTemporaryDir`, so the library the test writes to is its
own. The test runs in 0.7 s locally (`style_catalog_cli: 1` in the path map's `test_seconds`).

**Checks run here.** Local build: Debug, `SKETCHYUP_BUILD_CLI=ON`, `SKETCHYUP_BUILD_DESKTOP=OFF`,
GCC 16 with Qt 6.12. Passed: `style_catalog_cli_tests`, `command_tests`, `catalog_import_tests`,
`report_cli_tests`, `report_schedule_cli_tests`, `inspection_tests`, `mcp_tests` and
`assistant_tool_surface_tests`. `generate-tool-reference.py --check` passes against the new CLI.
`ci-plan.py check` and `ci-line-inventory.py check` pass.

**Not run here.** The sanitizer job, the Qt 6.8 compile, and the package acceptance (they run in
CI). `generate-api-schemas.py --check` needs the desktop binary, which this build leaves out. It
reads only the existing `--capabilities` sections, and none of them changed. The `presentation_cli`
test was not run; it reads only the presentation section, which did not change.

**Not measured.** The section 9 budgets. The fixtures are small, and no CLI import of a 130 MiB
catalog entry was timed.

## Implementation notes (R100.f1)

Recorded 2026-10-27 for branch `feat/r100f1-edge-stroke-rendering`, on `origin/main` at `fba4e54f`
(R099.d1, #473). This is the first desktop slice of [UX_DESIGN §18](../UX_DESIGN.md#18-styles-edge-strokes-and-asset-catalogs):
the edge stroke's engine edits and its viewport drawing. The Styles tab controls (R100.f2) and the
catalog dialog (R100.f3) are not in it.

- **Edit surface (owner choice 3).** `document.style` takes the optional `edgeStroke` record
  (`width` 1 to 8, `opacity` 0.01 to 1, `dash` as one of `solid`, `dashed`, `dotted`, `dashDot`, and
  `texture` as a canonical asset id, `"0"` for none). The shared style schema (`modelStyleSchema()`)
  carries it, so MCP, the transaction schemas and the saved-scene snapshot take it too; the
  regenerated `docs/api` files and `tool-reference-v1.json` add only that object. The top-level
  `required` lists are unchanged.
- **Labelled edits.** `turnOnEdgeStroke`, `turnOffEdgeStroke`, `setEdgeStrokeWidth`,
  `setEdgeStrokeOpacity`, `setEdgeStrokeDash` and `setEdgeStrokeTexture` (`src/io/model_style_io.hpp`)
  each return the whole style with one control changed and the history label, so one
  `document.style` batch with `history.label` is one undo step. The labels are "Turn on edge
  stroke", "Turn off edge stroke", "Set edge width 2.5 px", "Set edge opacity 60 %", "Set edge dash
  Dashed" (Solid, Dashed, Dotted, Dash-dot), "Set edge texture “Name”" and "Clear edge texture".
  Turning the stroke on copies the profile width. A field edit on a style whose stroke is off is
  refused, and so is a width outside 1 to 8.
- **The assistant keeps the edge stroke off its schemas (decision 9, the preferred option).** The
  assistant's `transaction.apply` builds its command schemas from the shared catalogue, so
  `withoutEdgeStroke` removes the `edgeStroke` property from them, as `transaction.begin` already
  drops `history`. The assistant's pinned surface does not move: every `toolsSha256` in
  `tests/assistant-tool-surface-baseline.json` is unchanged, and `assistant_tool_surface_tests`
  checks byte for byte that the assistant's tool list has no `edgeStroke`. No re-pin, no corpus
  version and no token measurement were needed, because the assistant's bytes did not change. The
  engine still accepts the field from any caller that sends it. The assistant cannot set the stroke
  unless the owner asks for that later, and then the re-pin costs the measurement this record
  describes for R095.g and R099.c.
- **The viewport draws the stroke (UX_DESIGN §18.3).** `drawStyleProfiles` builds the profiles from
  `profileStroke()`: while the stroke is on, its width replaces Profile width, its opacity is the
  vertex alpha, and its dash and texture apply. With it off, the quads are the ones drawn before
  R100.f1, and the pixels are the same: a stroke equal to Profile width (opaque, solid, untextured)
  renders the same frame (`edge_stroke_viewport_tests`, check 1).
  - **Dash.** `edgeDashRuns` (`src/core/model_style.hpp`) gives the on runs in logical pixels: dashed
    8 on and 4 off, dotted 2 and 3, dash-dot 8, 3, 2, 3. Each run is one quad. Its world ends come
    from the screen fraction σ of the visible span: t = σ·w0 / (w1 − σ·(w1 − w0)), where w0 and w1
    are the clip-space w of the span's ends. Screen position is linear in σ, not in t, so this is
    what makes the runs exact on screen. The pattern starts at the visible start of each edge (see
    the gaps below).
  - **Texture.** The edge texture is bound as the back image of every stroke vertex. Each vertex
    carries its distance along the edge and its distance across the stroke, in logical pixels, in a
    `noperspective` varying. The fragment shader divides by the texture's own size, so the texture
    repeats at its own size in logical pixels and shows only the rows the stroke covers. The texel
    colour is the edge colour (`toSrgb(texel)`, not multiplied by the edge colour), and its alpha is
    the stroke opacity times the texel alpha.
  - **Missing and refused textures.** A named asset with no bytes is hatched, and so is one the
    decoder refused. The hatch is a 45° pattern (2 logical pixels on and 2 off along the stroke's
    diagonal) in the theme's warning colour (`colors_.warning`, which is `#7a4f00` on a light model
    background). A texture still decoding or uploading draws nothing, never a substitute colour.
    No plain colour, default texture or other substitute is drawn anywhere.
  - **Opacity.** Blending is on, with the framebuffer alpha kept as the transparent pass keeps it,
    and depth writes are off, only while the stroke is translucent (opacity below 1, or a texture).
    Opaque strokes draw exactly as before.
  - **Chip.** The footer shows "⚠ Edge texture missing · Show in Styles" while the model's stroke
    names a missing asset (`Viewport::edgeTextureMissing`). It opens the Styles tab through the View
    action. The chip's text is the design's; the window's chip wiring has no test (see the gaps).
  - **Exports.** The image export (`Viewport::exportRaster`), the measured PDF and SVG export (which
    embeds the page raster) and the drawing-view rasters call `renderRaster`, and the sheet export's drawing-view rasters come from the same
    viewport (`capturedRasters`, `window_sheet_export.cpp`). They all run `paintScene`, so they inherit
    the stroke. The vector geometry path of the measured export has no profiles, as before, and no
    vector writer reads them. None of these exports was run in this slice.
- **Precedence of the two width fields (UX §18.8 item 2).** The stroke's width is the one drawn while
  the stroke is on. `profileWidth` is drawn when it is off and is unchanged in the record. The
  Colors and details control that dims it is the Styles tab's, which R100.f2 builds.
- **Measured numbers.** The cost is one quad per visible dash run: 6 vertices of 76 bytes each. A dotted
  edge costs about 91 bytes of vertex data for each visible pixel of its length, and dash-dot about 57.
  The stats gained `profileSegments` (also in `sketchyup --benchmark` output). Nothing was timed at
  scale, so the section 9 budgets remain unmeasured.
- **Tests.**
  - `model_style_command_tests`: each control is one labelled `document.style` edit, one undo
    step, redone by Redo; turning the stroke off removes the sub-record; an unknown texture is
    refused by name; the field edits refuse an off stroke.
  - `model_style_tests`: the dash runs as pure geometry, including the clipped end and the
    published lengths.
  - `assistant_tool_surface_tests`: the assistant's surfaces equal the baseline, and no
    `edgeStroke` appears in the assistant's tool list; MCP's `document.style` accepts it.
  - `edge_stroke_viewport_tests` (new, native): a stroke equal to Profile width draws the same
    frame; half opacity blends black over white to grey; dashed runs land where the proposed
    lengths put them, on the screen, along each edge; the texture repeats every 8 logical pixels
    and shows no row below its width; a missing texture hatches with no edge colour; the chip state.
    Run locally offscreen (Mesa `swrast`) at device pixel ratio 1 and 2 (`QT_SCALE_FACTOR`): both
    pass. The X11 and Wayland jobs run it at 1x and 2x.
  - The existing `style_viewport_tests`, `command_tests`, `inspection_tests`,
    `transaction_sequence_tests`, `transaction_dispatch_tests`, `automation_session_tests` and
    `mcp_tests` pass locally.
- **Gaps (honest).**
  - The stroke-off path was not compared against a pre-change image. Its quads and shader branches
    are the same as before by construction, and the equality check above covers the new code only.
  - The dash and texture phase starts at each edge's visible start, after clipping at the screen
    edge. An edge that is partly off screen therefore moves its pattern when the view pans. Anchoring
    to the unclipped edge needs the projective relation over the whole edge, and an edge that crosses
    the camera plane has no such relation.
  - A texture that is present but refused by the decoder draws hatched. The status chip does not
    show for it, which the design does not ask for. The viewport's "Some images are unavailable"
    summary covers it.
  - No window-level test covers the chip's visibility or its click.
  - The Qt 6.8 reference build, the sanitizer job and the Wayland runs run only in CI.
  - The §9 budget for dash runs is unmeasured, as above.
