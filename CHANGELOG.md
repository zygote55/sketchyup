# Changelog

## 1.5.0 — unreleased

SketchyUp 1.5.0 is a minor release. It carries the 1.5 follow-ups that the 1.4.0 owner decisions
left for 1.5, the first GeoTIFF elevation import (R107.b, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md))
and the source credits in exports. Additive: a document with no GeoTIFF terrain is written as 1.4.0
writes it, and a GeoTIFF terrain is written at schema 34, which 1.4.0 refuses. The summary is in the
[1.5.0 release notes](docs/RELEASE_NOTES.md), and the release is gated through its [M10 record](docs/M10_ACCEPTANCE.md#the-150-release).

- Large component edits and orphaned definitions (D-075, [ADR 0005](docs/decisions/0005-document-protocol.md), [PR #540](https://github.com/zygote55/sketchyup/pull/540)): a component edit on a definition that holds more than half the unique-geometry limit now succeeds in the CLI and on the desktop, and exploding a placement or making it unique no longer counts the replaced definition twice; the removed definition is listed in the batch response as `removedDefinitions`.
- User guide link on the GeoTIFF page (R107.b4 link, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md), [PR #538](https://github.com/zygote55/sketchyup/pull/538)): the terrain import dialog's GeoTIFF page has an "Open the user guide" button that opens the installed guide at the download section; nothing is fetched.
- Visible source credits (R107.c2, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): a PDF sheet page, a measured PDF or SVG drawing and a presentation frame that show GeoTIFF terrain print its source notice, a "Sources" line in the page margin or a burned-in note on the frame.
- GeoTIFF elevation in File > Import terrain… (R107.b4, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md), [PR #536](https://github.com/zygote55/sketchyup/pull/536)): a `.tif` file opens a GeoTIFF page in the terrain import dialog, with the 3DEP or GLO-30 product, a window in UTM or latitude and longitude, Fit to the file, the vertical reference and the offset and note for a mismatch, the report, the located voids and the Credit under Source in Entity info; the user guide page for it is R107.e.
- Downloaded elevation files (R107.e, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): the user guide page "Terrain from a downloaded elevation file" and the support-matrix row for importing a USGS 3DEP or Copernicus GLO-30 GeoTIFF, with the download pages, the credits and the non-affiliation wording; documentation only.
- Source credits in exports (R107.c1, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): a terrain made from a GeoTIFF elevation file adds its notice to the GLB `asset.copyright`, the OBJ comment lines, the IFC site's property set, the STL export report and the presentation batch manifest; a DXF export carries it only for a terrain with every height at zero.
- GeoTIFF elevation import (R107.b1, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): `sketchyup-cli --import-terrain` reads a user-downloaded USGS 3DEP or Copernicus GLO-30 GeoTIFF in a UTM window (`--dem-source`, `--dem-window`, `--dem-zone`), labels its datum and vertical reference, and stores the source record with the terrain (schema 34, `terrain-dem-v1`; a 1.4.0 reader refuses it). The desktop dialog is R107.b4.
- GeoTIFF LZW (R107.b1.1, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): the GeoTIFF reader decodes LZW-compressed rasters, which USGS uses for 3DEP. Overview images are skipped by R107.b1.2.
- GeoTIFF overviews (R107.b1.2, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): the GeoTIFF reader skips the reduced-resolution overviews of a 3DEP tile and reads its full-resolution image, so a tile as USGS distributes it imports; a second full-resolution image, a transparency mask, a loop, or more than 16 further images are still refused by name.
- GeoTIFF import engine for the desktop dialog (R107.b3, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md), [PR #534](https://github.com/zygote55/sketchyup/pull/534)): a path-based GeoTIFF dry run and import with no 64 MiB limit, the file's facts, spacing that takes the model's room, a zero offset refused, the site's geographic record written when a GeoTIFF establishes the origin, the digest bound to the read, stable refusal codes with plain sentences, and the void posts listed in the refusal.
- Render limits for terrains (R099.f): a 249,218-triangle terrain and a 20,000-body capture now export and render; a terrain counts against its own 250,000-triangle class, and the manifest bound is 128 MiB. The 20,000-body limit is unchanged: a 20,000-body capture prepares in 1.70 s (p50), over the 1 s per-frame budget, and the 10,000-placement capture (30,257 bodies) is still refused by name ([PR #526](https://github.com/zygote55/sketchyup/pull/526)).
- STEP import entity cap (R098.k): a STEP file whose reader holds more than 2,000,000 entities is refused before any transfer, with the count and the limit named; STEP keeps one tessellation thread, because parallel meshing gave no speed-up on the 100-sphere model ([PR #525](https://github.com/zygote55/sketchyup/pull/525); 1.5 follow-up 7; [ADR 0172](docs/decisions/0172-step-exchange-subset.md)).
- STEP conversion speed (R098.j): importing the 100-sphere model with a new native file takes 10.17 s (was 14.70 s; still over its 10 s budget, a documented limit), and without a file 6.87 s (met); exporting the 90,954-face model takes 9.96 s (was 16.56 s; budget 5 s, a documented limit) and writes 100.6 MB, not 224 MB, with no parametric curves on the edges ([PR #524](https://github.com/zygote55/sketchyup/pull/524)).
- STEP import restart-and-exclude (R098.i, [PR #522](https://github.com/zygote55/sketchyup/pull/522)): a STEP file whose tessellation crashes the helper or overruns its limits is restarted without the part it stopped on (at most three times), and the import is partial and names that part; a sandbox kill, or a stop before the tessellation, still refuses the file whole (1.5 follow-up 4; [ADR 0172](docs/decisions/0172-step-exchange-subset.md)).
- Asset catalogs insert (R100.i, [PR #521](https://github.com/zygote55/sketchyup/pull/521)): a large catalog's insert runs off the UI thread; a progress sheet opens after a second. A Cancel before the edit is composed leaves the model and the library folder unchanged; a Cancel after it is too late, and the insert commits.
- Styles and asset catalogs (R100.h, [PR #520](https://github.com/zygote55/sketchyup/pull/520)): the asset catalog report has **Save report…**, its accounting bar is solid, hatched and cross-hatched with a legend, and the Styles refusals open with "Nothing was saved." or "Nothing was applied." (1.5 follow-up 3; [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md)).

- Defects fixed since 1.4.0 (from the [open-defect triage](docs/verification/R089a-defect-triage.md)):
  - D-068 and D-069 ([PR #527](https://github.com/zygote55/sketchyup/pull/527)): the Quantities Tags popover no longer prints a Qt Wayland popup warning, and the desktop build's compiler warnings are fixed.
  - D-070 ([PR #519](https://github.com/zygote55/sketchyup/pull/519)): the `.skp` import dialog test waits for the progress step before it checks it, so it no longer misses the step on Wayland at 2×.
  - D-071: the 1.4.0 release notes now say to remove `sketchyup-ifc` before a rollback to 1.3.0 ([PR #518](https://github.com/zygote55/sketchyup/pull/518)).
  - D-072 ([PR #523](https://github.com/zygote55/sketchyup/pull/523)): the sanitized native MCP step no longer leaks a 96-byte Wayland proxy at exit.
  - D-073 ([PR #528](https://github.com/zygote55/sketchyup/pull/528)): a catalog insert that finishes at once can no longer wait forever. It was introduced with R100.i, after 1.4.0.
  - D-074 ([PR #534](https://github.com/zygote55/sketchyup/pull/534)): a GeoTIFF terrain import whose window is narrower than its spacing no longer loops forever. It was introduced with R107.b1 and was never in a release.

## 1.4.0 — 2026-10-29

Tag `v1.4.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.4.0).

A minor release that adds the 1.4.0 group of Extended rows: IFC and STEP exchange (scope row F04),
camera paths and batch presentations (F05), custom styles and asset catalogs (F06), quantities,
reports and schedules (F07), `.skp` import (C01, import only), offline geographic coordinates (C06,
offline only) and the helper process sandbox (R109). Additive: existing commands, queries and
schemas keep their shapes; a document that uses the new features is written at schema 30 to 33.
The summary is in the [1.4.0 release notes](docs/RELEASE_NOTES.md). The group is released only
through its [M10 gate](docs/M10_ACCEPTANCE.md).

- IFC import and export (R097, F04, [ADR 0171](docs/decisions/0171-ifc-exchange-subset.md)): **File → Import IFC…**
  reads `IFC2X3`, `IFC4` and `IFC4X3_ADD2` into a new unsaved model, with a report; **File → Export IFC…**
  writes `IFC4` only, tessellated. `sketchyup-cli --import-ifc` and `--export-ifc` do the same work. The
  acceptance record is partial: the owner's buildingSMART hosted validation has not been run, so the export
  claim is conditional on it, and the 10,000-body import takes about 12 s against its 10 s budget, an accepted limit for 1.4.0 (2026-10-28) ([R097.f](docs/verification/R097f-ifc-exchange.md)).
- STEP import and export (R098, F04, [ADR 0172](docs/decisions/0172-step-exchange-subset.md)): **File → Import STEP…**
  reads AP203 and AP214, and AP242 on a best-effort basis; **File → Export STEP…** writes AP214 (the default) or AP203,
  and never AP242. `sketchyup-cli --import-step` and `--export-step`. Three budgets are missed; the owner accepted them as limits for 1.4.0 on 2026-10-28, and speeding up the conversion is planned for 1.5 ([R098.d](docs/verification/R098d-step-exchange.md)).
- `.skp` import (R102, C01, [ADR 0176](docs/decisions/0176-skp-import.md)): **File → Import .skp file…** and
  `sketchyup-cli --import-skp` read a SketchUp model file as a new, unsaved model, through the MIT-licensed
  OpenSKP reader in a sandboxed helper. Export is not supported.
- Geographic site coordinates (R107.a, C06, [ADR 0181](docs/decisions/0181-maps-geolocation-feasibility.md)): the
  site can carry a WGS84 or NAD83 position, in UTM, as schema 32 (`geographic-site-v1`); the sun panel's **Use site
  location** applies it as one undo step. Offline only: there is no lookup, no elevation import and no attribution.
- Quantities panel and automation names (R101): **View → Quantities** shows the counts, areas and volumes of the
  model in a panel; `report.run` and `report.schedule` are available over MCP and the session. The in-app
  assistant cannot run them.
- Presentations and batch output (R099.b to R099.d): **Camera → Presentations…** edits the camera paths and render
  presets; **Export presentation batch…** (command search) renders a path through up to four presets to a new
  folder, with an optional H.264 MP4 for each preset.

- Asset catalogs dialog (R100.g F-1 and F-3, [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md)): a
  catalog opened with Open catalog file… is checked on each selection without its bytes being sent through the check.
  On a 128 MiB catalog a selection takes about 0.1 s, against 2 to 5 s before, and the process peaks at about 0.5 GB,
  against 1.5 GB. The check and the insert are the engine's, as before.
- Helper process sandbox, socket calls (R109 S1, S3, S4 and S6, [ADR 0183](docs/decisions/0183-helper-process-sandbox.md)):
  the `.skp`, text and extension helpers now refuse a socket call with an error (EPERM), as the IFC helper already
  does, instead of ending. A text helper whose Qt logger opens a socket for a missing locale carries on and logs
  nothing for it.
- Sandbox check in CI (R109 S6, [ADR 0183](docs/decisions/0183-helper-process-sandbox.md)): the fast tier runs
  `sandbox_available`, a required check. It fails when the runner has no Landlock, no `PR_SET_NO_NEW_PRIVS` or no
  seccomp filter, and nothing skips it. Its job log records the runner's kernel and Landlock ABI. This change
  closes R109, the helper process sandbox.

- Presentation batch, changed Blender (R099.e2, [ADR 0173](docs/decisions/0173-camera-paths-batch-output.md)): if
  you change the Blender after opening the batch dialog and press Start, Start checks the video encoder again and
  says so, instead of refusing with the H.264 message. The batch starts with video when that Blender has H.264. If
  it has not, or its check does not finish, Start shows that reason.
- Presentation frames (R099.e2): each rendered PNG loses Blender's text chunks (the date, the time, the render time
  and Cycles' statistics), so the same frame renders to the same bytes. The pixels are unchanged. Single renders
  lose the same chunks.
- `sketchyup-cli --export-presentation`: a request over 16 KiB is refused with `LIMIT_EXCEEDED`, not `INPUT_ERROR`.
- Presentation frames, rendered camera (R099.e3, [ADR 0173](docs/decisions/0173-camera-paths-batch-output.md) §6):
  each frame's record now holds the camera its Blender rendered, in model coordinates, not only the camera the job
  was given. The batch checks it against the planned pose to 1 µm, with the stand-in and with real Blender.
- Presentation batch, MPEG-4 refusal (R099.e3): when a batch that chose MPEG-4 Part 2 is refused for its codec, the
  refusal names MPEG-4 Part 2, not H.264.
- Render queue (R099.e3, [ADR 0100](docs/decisions/0100-render-queue-lifecycle.md)): asking for the result of a render
  before it finishes is refused, and the render keeps its state. It no longer shows as Failed in the jobs list.

- Blender job sandbox (R109 S5, [ADR 0183](docs/decisions/0183-helper-process-sandbox.md)): the Blender
  render, preview, encode and handoff run under the helper sandbox. Blender may read its own install, the
  system libraries and its data, and may write only in the job's attempt directory. It has no network
  (sockets are refused), no audio server (`ALSOFT_DRIVERS=null`), a private home and temporary directory,
  and `--offline-mode`. It may execute only Blender. A sandbox violation is reported as such and is not
  retried. A sandbox that cannot be installed refuses the render, named, and Blender does not start.
- Helper process sandbox, text and extension (R109 S3 and S4, [ADR 0183](docs/decisions/0183-helper-process-sandbox.md)):
  the text helper that outlines glyphs and the extension worker that resolves an extension's action now run
  under the sandbox. They can read only the system files they need (fonts and their configuration for the text
  helper, the locale for both) and write nothing; they cannot open a socket or run a program. A call outside the
  allow-list ends the helper, which the application reports as a sandbox violation and does not restart. A helper
  that cannot install the sandbox refuses and runs nothing. The text helper reads your font folders
  (`$XDG_DATA_HOME/fonts`, `~/.local/share/fonts` and `~/.fonts`) read-only, so a font installed there is found
  as before, and it reads no other file of yours. The helper's locale is C.UTF-8 whatever yours is.

- Outliner rebuilds (D-048): the Outliner no longer rebuilds on edits that do not change what it shows.
  Its rows and the Tags rows are rebuilt only when a shown name, tag, hierarchy position, hidden, locked or
  context state, or the viewport's selection, changes. A placement move on a 100,000-face model no longer
  rebuilds them: its synchronous work drops from about 148 ms to about 37 ms (Release, p50), and the tree
  keeps its items, selection and scroll across such an edit.

- Edge stroke drawing (R100.f1, [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md)):
  `document.style` takes the optional edge stroke (width, opacity, dash and texture), one labelled
  edit per control. While it is on, the profiles draw with its width, opacity and dash, and with its
  texture tiled along each edge. A missing texture draws hatched in the warning colour, with a status
  chip that opens the Styles tab. The Styles tab controls follow (R100.f2). A model saved with an edge
  stroke is schema 31 and cannot be opened by SketchyUp 1.3.0.
- Styles tab and style library (R100.f2, [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md), [UX §18](docs/UX_DESIGN.md#18-styles-edge-strokes-and-asset-catalogs)):
  the Styles tab gains an Edge stroke section (use, width, opacity, dash and texture, each one undo
  step) and a Style library section (a folder you can choose, search, Apply to model, Save to library).
  File > Import style… and File > Export style… read and write `.sketchystyle` files, with the refusals
  of §18.6: a missing texture is named and nothing is applied or written, and an existing file is never
  replaced. A dotted edge costs about 91 bytes of vertex data per visible pixel; the frame-time numbers
  are software-rendered and recorded in the ADR.

- Sandboxed IFC and STEP helper (R109 S2, [ADR 0183](docs/decisions/0183-helper-process-sandbox.md)): every mode of
  `sketchyup-ifc-worker` that reads or writes a file (the import, the check, the export, the STEP import and export,
  and the developer probes) now runs under a Landlock ruleset and a seccomp allow-list, with `PR_SET_NO_NEW_PRIVS`,
  before it opens the file. A helper that a call outside its policy stops is reported as `sandboxViolation` and is
  not restarted. A layer that cannot be installed refuses the import, the check or the export with
  `sandboxUnavailable`, and nothing is read or written. The STEP export's AP203 header no longer names the machine's
  address: it names `0.0.0.0`, because the name lookup that wrote it is refused under the sandbox.

- Dynamic Components in .skp files (R104.a, R104.c, R104.d, [ADR 0178](docs/decisions/0178-dynamic-live-component-feasibility.md)):
  a Dynamic Component's `dynamic_attributes` values are read as read-only properties
  `skp.dynamic_attributes.<key>`, at instance and definition level (a definition's values reach each of its
  placements, and the placement's own value wins). Lengths are stored in metres. Formulas (`_<name>_formula`,
  `_material_formula`) and `onClick` are kept as text, capped at 2,048 bytes, and never evaluated or run.
  `Copies` imports as one placement. The report has a `dynamicComponents` section that balances per level.
  Live Components are not identified: the file does not mark them, so the report does not label them
  service-dependent; the import makes no call to any Trimble service, and a test checks that it needs no
  network. Formula mapping is deferred indefinitely (R104.b). Dynamic Components and Live Components are
  Trimble's terms; SketchyUp is not affiliated with or endorsed by Trimble.
- Downloads from online libraries (R105.a, [ADR 0179](docs/decisions/0179-hosted-asset-access-feasibility.md), [PR #476](https://github.com/zygote55/sketchyup/pull/476)):
  the user guide's page "Getting models from online libraries" describes the download steps. A ZIP archive,
  COLLADA (`.dae`), USDZ or FBX file is refused with the reason. SketchyUp makes no network request for them.
- Textured material presets (R100.b, [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md) Q1):
  the New material dialog's Local library gains a **Textured** group of fifteen presets, nine CC0
  textures from Poly Haven and six generated ones. Creating a preset adds its texture and its material
  in one undo step, and an identical texture already in the model is reused. Each preset shows its
  source and licence. The six colour presets are unchanged. The Poly Haven notice is in
  `THIRD_PARTY_NOTICES`.
- Material texture size (R100.b2, [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md) section 3):
  a material can store the real-world size, in metres, that one texture repeat covers. Each textured
  preset stores its signed-off tile size, and the New and Edit material dialogs have a **Set size**
  field. A material without a size repeats every metre, as before. The material commands take
  `textureSize`, `materials.describe` reports it, and catalogs and the `.skp` import carry it. A
  document with a sized material is schema 33 with the conditional feature `material-texture-size-v1`;
  a document without one is written as before. A geographic site record is schema 32 (R107.a), and the
  two features are independent. The schema-33 file is refused by SketchyUp 1.3.0 and 1.2.0 as schema 31 is.

- Asset catalogs dialog (R100.f3, [ADR 0174](docs/decisions/0174-custom-styles-asset-authoring.md) §8,
  [UX_DESIGN §18](docs/UX_DESIGN.md#18-styles-edge-strokes-and-asset-catalogs)): File → **Asset catalogs…**
  lists the catalogs in the library folder (Choose folder… is remembered) and any catalog opened with
  Open catalog file…, with search and a Kind filter. Insert places the selected entries at the model's
  origin, as one undo step, after the engine's dry run. Templates and entries whose textures are missing
  or do not match their checksum are not inserted, and a refused insert opens a report that names the
  dependency. File → **Export asset catalog…** writes the chosen components, materials and style to a
  new `.sketchycat` file. Choose folder… does not change the default folder that MCP and the command line
  read. Save report… is not yet available.

- Quantities report engine (R101.a, [ADR 0175](docs/decisions/0175-quantities-reports-schedules.md)):
  a pure counting pass over the materialized document, with the counting rules for shared
  components, nested placements, hidden content, mirrored transforms, invalid solids and terrain.
  It is served by the bounded `report.run` session query (counts, areas, volumes, the invalid
  table and terrain, in the document's units, stamped with the document identity and revision).
  The assistant does not see it. No CSV, CLI, saved schedules, MCP names or panel yet (R101.b to
  R101.e).

- Quantities report files (R101.b, [ADR 0175](docs/decisions/0175-quantities-reports-schedules.md)):
  `sketchyup-cli --input MODEL --report FILE.csv|FILE.json` writes a report of counts, areas,
  volumes, the invalid table and terrain to a new file, with the metrics, grouping, visibility and
  filters of the session query. The CSV is the file of record (UTF-8 without a BOM, LF line endings,
  a `#key=value` metadata block and one tidy row per group and metric). The JSON is the session's
  `report.run` data, byte for byte. An existing file is refused, and the model is never changed.
  The saved-schedule flags, the MCP names and the desktop panel are not in this release (R101.c to
  R101.e).

- Saved report schedules (R101.c, [ADR 0175](docs/decisions/0175-quantities-reports-schedules.md)):
  a named report request is saved for one document in a user-level file, beside the presentation
  jobs, and never in the model. `sketchyup-cli --input MODEL --save-schedule NAME [report flags]`,
  `--list-schedules` and `--delete-schedule NAME` manage them, and `--report FILE --schedule NAME`
  runs one (its CSV says `#schedule=NAME`). A schedule runs only on the document it was saved for,
  and a schedule whose tags, materials or definitions were removed refuses, naming them. A save leaves
  the model file byte-identical. The session gains `report.schedule`. The MCP names and the desktop
  panel are not in this release (R101.d and R101.e).

- Camera paths and render presets (R099.a, [ADR 0173](docs/decisions/0173-camera-paths-batch-output.md)):
  named camera paths that keyframe saved scenes (2 to 32 keyframes, at most 240 planned frames,
  up to 64 per document) and named render presets (up to 16 per document). Both are saved with
  the document as schema 30 (`presentation-v1`); a document without them keeps its lower schema.
  SketchyUp 1.3.0 refuses schema-30 files unchanged. No command, UI or rendering yet (R099.b to
  R099.d).
- STEP import fixes (PRs #510 and #511, [ADR 0172](docs/decisions/0172-step-exchange-subset.md)): a bad solid's
  surfaces are no longer imported into another part's body; problems are located in the import report; PMI and
  GD&T are counted and reported; and parts nested too deeply are counted.
- `.skp` import fixes (R102.f, [ADR 0176](docs/decisions/0176-skp-import.md)): a helper run with its output on
  `/dev/null` is no longer ended by its own sandbox. The "newer than tested" label applies to SketchUp 2025 files,
  as the documentation says; it applied only from 2026. Hashing a large input in the helper took quadratic time, so
  a large `.skp` could exceed the helper's 120 s deadline.

## 1.3.0 — 2026-10-22

A minor release that adds terrain and site tools (scope row F03). Additive: existing
commands, queries and schemas keep their shapes. The summary is in the
[1.3.0 release notes](docs/RELEASE_NOTES.md), which also list the fixes since 1.2.0 that
have no entry below (ADR 0167 recoverable replies, D-038, D-039, D-023, D-032, `anyOf`
validation and the 88 px tool rail). Also: a ChatGPT-plan usage limit is now reported as such,
and other provider stream errors are named with their code and message (D-046). The entries under "Details" are the other changes on
`main` since 1.2.0.

- Terrain: import from DXF contours and point files, or from model edges, as a tiled
  constrained Delaunay surface (up to 250,000 triangles); a site origin with site
  coordinates in the tools and DXF export; Sculpt, Smooth, Edit contours, Stamp pad and
  Drape, each kept in the terrain's edit list and replayed by Regenerate.
- API: new `site.*` and `terrain.*` operations and queries over MCP and the headless
  session; CLI `--import-terrain` and `--dxf-site-coordinates`. The in-app assistant sees
  only `site.describe`, `terrain.describe` and `terrain.elevation_at`.
- Assistant: well-formed provider replies that break a host rule are refused with tool
  errors instead of stopping the task (ADR 0167); 1.3.0 is evaluated against provider
  corpus v5.
- Format: schema 29 (`terrain-v1`) is written only for documents with a terrain or a site
  origin. SketchyUp 1.2.0 refuses those files unchanged.
- Package: Arch `sketchyup` 1.3.0-1, upgrading from 1.2.0-1.

Tag `v1.3.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.3.0).

Details:

- Terrain core (R095.a, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md)):
  a terrain body with its sources and tiled triangle mesh (at most 2,048 triangles a
  tile, 250,000 a document, outside the 100,000-face modelling budget), a document site
  origin with an informational CRS label, schema 29 with a binary `terrain-mesh` chunk,
  and the `site.set`, `site.reanchor`, `terrain.explode` commands and `site.describe`,
  `site.convert`, `terrain.describe`, `terrain.elevation_at` queries in automation
  sessions and MCP (the in-app assistant sees only the three queries listed under R095.g). Generic topology tools refuse terrain tiles;
  Explode terrain turns a terrain into ordinary geometry. No importer or desktop UI yet.

- Terrain edits list (R096.f2, [UX_DESIGN §15.6](docs/UX_DESIGN.md#156-terrain-edits),
  [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096f2)): with a
  terrain selected, Info shows its **Terrain edits**: each change with its kind, title, values and
  number, the selected one outlined in the view (double-click frames it), **Remove**, **Edit pad…**,
  **Restamp** (unavailable with the reason when the building is gone) and **Reset edits…**. Removing,
  editing or resetting replays the list in the background with progress and **Cancel** in the status
  bar, as one undoable edit. Changes a replay skips stay listed, marked with the reason, and the last
  change's or replay's notices show with **Show report**.

- Terrain tools on the desktop, part 3: Stamp pad and Drape (R096.f2, [UX_DESIGN §15.4–15.5](docs/UX_DESIGN.md#154-stamp-pad),
  [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096f2)):
  **Terrain → Stamp pad** takes a selected or clicked face, group or component (a group's lowest
  horizontal face, highlighted first) or a closed loop, sets the pad's elevation by moving up or down
  or typing (less a remembered pad offset) and previews daylight or fixed-width grading with cut and
  fill tinted and patterned, the slope rays and the dashed daylight line; Enter or a click stamps it,
  and a typed elevation or slope afterwards revises the same edit. **Terrain → Drape** (or **Drape
  onto terrain** in the context menu) drapes the selected edges, curves and faces: imprinted as
  breaklines with optional region paint (the terrain selected afterwards), or as a lifted overlay
  group (selected afterwards), previewed on the surface with clipped parts in red. Refusals are
  marked at the place with an × and change nothing.

- Terrain tools on the desktop, part 2: Edit contours (R096.f2, [UX_DESIGN §15.3](docs/UX_DESIGN.md#153-edit-contours),
  [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096f2)):
  **Terrain → Edit contours** (Contours in the strip) draws a terrain's source contours solid,
  breaklines dash-dot and spot points as crosses over faded display contours, names the source under
  the pointer and selects it with square vertex and round midpoint handles. Dragging a handle moves
  or inserts a vertex with the re-triangulated surface previewed in place, planned on a background
  worker so the view stays responsive (the newest pointer position wins); a move that would cross
  another source, leave the outline or enter a pad is marked at the pointer in red with an × and its
  place in site coordinates, and releasing it changes nothing. A typed number sets the selected
  source's elevation, Delete and Shift+Delete remove a vertex or the source, the strip switches its
  role, and Add contour, Add breakline, Add spot and Promote add sources. Each change is one undoable
  edit ("Edit contour", "Add contour", "Remove contour"), and a keyboard-only path selects a source by
  a typed position, Tabs between its handles and moves them by typed positions.

- Faster Terrain section (R096.f1c, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096f1c)):
  with a terrain selected, Entity Info's Terrain section updates its triangle count, elevation
  range and flat areas from the tiles an edit changed instead of rescanning the whole terrain,
  with the same values, so sculpting a selected terrain is no slower than an unselected one.

- Faster terrain commits (R096.f1b, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096f1b)):
  a one-tile sculpt commit no longer hashes the whole terrain before the frame (the mesh digest
  is computed when first read, or in the background, with the same value as before), and the
  view redraws only the changed tiles' contour rows and recomputes the lighting of the tiles
  around them instead of rebuilding them. At 250,000 triangles the work before the frame drops
  from about 130 ms to 51–64 ms.

- Terrain tools on the desktop, part 1 (R096.f1, [UX_DESIGN §15](docs/UX_DESIGN.md#15-terrain-editing),
  [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096f1)):
  a **Terrain** menu between Draw and Camera (the tools, then Terrain settings…, Regenerate
  terrain, Reset edits… and Explode terrain… for the selected terrain), a **Terrain** slot on the
  tool rail after Tape, terrain entries in the viewport's context menu, and the **Sculpt**
  (Shift+S) and **Smooth** tools with a tool options strip at the top of the view. A brush on the
  surface shows the vertices that will move; press and drag paints a swept stroke, a click then a
  move up or down sets a one-spot amount, Ctrl lowers and Shift smooths, `[` and `]` change the
  radius, and region scope uses a selected face or loop or a clicked polygon. The change is drawn
  in place of the tiles it replaces before it is applied as one undoable edit, and Escape
  withdraws it without changing the model. A typed value after an edit revises it. The brush can
  be placed by typing `[x, y]` or `@E, N`. `terrain.sculpt` now uses the plan its dry run (or the
  desktop preview) made instead of planning the change twice.

- Projection and drape (R096.e, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096e)):
  a new `terrain.drape` command (automation sessions and MCP; hidden from the in-app assistant)
  projects edges, curves and faces in any context vertically onto a terrain. Imprint splits only
  the triangles the lines cross, each within itself, so the terrain's shape does not change; the
  lines join its sources as breaklines, contours they cross gain the crossing points at their
  elevations, and parts outside the terrain are clipped and reported. With region paint, each
  face's material paints every terrain triangle inside it, on every later edit and Regenerate.
  The drape is one undoable edit ("Drape on terrain") recorded in the terrain's edit list; it
  may cross a pad's slopes but not the pad. Overlay instead makes ordinary geometry lying on the
  terrain (lifted 0.01 m by default) in a new group, leaving the terrain unchanged.
  `terrain.describe` lists the paint regions.

- Building-pad stamp (R096.d, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096d)):
  a new `terrain.stamp_pad` command (automation sessions and MCP; hidden from the in-app
  assistant) flattens a footprint (a horizontal face, the lowest horizontal face of a group or
  component, a closed edge loop or a polygon) to a pad elevation and grades the ground around it
  by cut and fill slopes to where they meet the ground (default 1:2, within a 100 m maximum distance by default) or by a band of
  fixed width. The pad, its slopes and the daylight line are one undoable edit ("Stamp pad")
  recorded in the terrain's edit list, so Regenerate reproduces it; grading rays that cross at
  tight corners are dropped and reported. Pads are protected: sculpt and smooth leave them, and
  contour edits and other stamps that would enter one are refused with its location.
  `terrain.update_edit` changes a pad's elevation, slopes or mode, or re-reads its footprint from
  its source body (Restamp), replaying the later edits ("Edit terrain change"); `terrain.describe`
  lists the pads with their elevation and extent.

- Sculpt and smooth (R096.b, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096b)):
  a new `terrain.sculpt` command (automation sessions and MCP; hidden from the in-app
  assistant) raises, lowers or smooths a terrain inside a brush stroke (1 to 256 points, a
  radius of 0.01 to 1,000 m, a smooth, linear or constant falloff) or a closed region with a
  falloff width. Only elevations change; the triangles are kept. A stroke moves each vertex
  once, by its distance to the whole stroke, so the result does not depend on how the stroke
  was sampled. Contour, breakline and spot-point vertices under the change become edited
  ground and leave the current sources; a contour cut in two keeps its id for one part and
  gives the other a new id. Each change is one undoable edit ("Sculpt terrain" or "Smooth
  terrain") recorded in the terrain's edit list, so Regenerate reproduces it; out-of-range
  parameters and a brush that reaches no vertex are refused and change nothing.

- Contour edit (R096.c, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096c)):
  new `terrain.edit_source`, `terrain.add_source` and `terrain.remove_source` commands
  (automation sessions and MCP; hidden from the in-app assistant) move, reshape, re-elevate, add
  and remove a terrain's contours, breaklines and spot points, change a contour to a breakline
  and back, and promote the surface's contour line at an elevation to a source contour. Only the
  triangles around the change are re-triangulated; a change of elevation alone moves the
  vertices without re-triangulating. Each is one undoable edit recorded in the terrain's edit
  list, so Regenerate reproduces it. Edits that would cross another contour or breakline, leave
  the terrain's outline, enter a hole or put two elevations at one point are refused with their
  locations, and change nothing. `terrain.describe` lists the current sources with their ids.
  Imported source coordinates of −0.0 are stored as 0.0, so a terrain saved and reopened
  regenerates to the same mesh.

- Terrain edit core, part 2 (R096.a, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#part-2)):
  Regenerate and Terrain settings rebuild a terrain from its sources and then replay its list of
  edits, so a Regenerate after edits changes nothing. New `terrain.reset_edits` and
  `terrain.remove_edit` commands (automation sessions and MCP; hidden from the in-app assistant)
  empty the list or remove one change and rebuild, as one undoable edit each; a change that no
  longer applies is skipped, reported and kept. `terrain.describe` lists a terrain's edits. A
  replay reports its progress and can be cancelled without changing anything. After an edit the
  viewport recomputes shading normals and contour lines only for the tiles the edit touched.
  No edit operation exists yet (they follow in R096.b–e), so existing terrains behave as before.

- Terrain edit core, part 1 (R096.a, [ADR 0169](docs/decisions/0169-terrain-editing-drape-stamp.md#implementation-notes-r096a)):
  the groundwork for terrain editing, with no new command yet. Terrains now store their tile
  grid, a key for each tile and an id for each source, and can hold a list of edits; an edit
  rebuilds only the tiles it touches and leaves every other tile untouched. Regenerate keeps the
  ID of every tile whose mesh did not change, wherever the change is. `terrain.elevation_at`
  looks points up through a grid index instead of scanning every tile, with the same results.
  Terrain edits are charged to the undo history by what they change, not by the whole terrain
  record. The unreleased schema-29 format changes in place (ADR 0108 addendum); terrain
  documents saved by earlier development builds still open and are saved in the new form.

- Progress and cancel for terrain import (D-044, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-d-044)):
  File → Import terrain… and the roles step of Create terrain from selection show what their
  check is doing (reading the source, preparing the sources, triangulating, tiling) with a
  progress bar, instead of only "Checking…". Cancel, Escape or closing the dialog stops the
  check at once, and changing a setting stops the outdated check before starting the new one.
  `sketchyup-cli --import-terrain` shows the same stages on stderr when stderr is a terminal;
  its stdout and JSON output are unchanged. Imported terrains are unchanged, bit for bit.

- Roles for **Create terrain from selection** (R095.h, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095h)):
  when the selected edges include a closed loop or lie under more than one tag, a roles step
  lists each tag (Untagged included) with Automatic (contours and breaklines by Z, as before),
  Contour, Breakline, Boundary, Hole or Ignore, in the same role table as File → Import terrain….
  A boundary must be one closed loop and holes closed loops; problems are named by tag and count,
  and the command's own dry run shows its refusals and notices, before Create is enabled. Create
  is one undoable "Create terrain" edit and Regenerate keeps the boundary and holes. Other
  selections still create a terrain in one click.

- Terrain queries for the in-app assistant (R095.g, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095g)):
  the assistant can now call `site.describe`, `terrain.describe` and `terrain.elevation_at`,
  with descriptions that give units and say which coordinates are site and which are model
  coordinates. It still cannot see or run any other site or terrain command; MCP exposes all of
  them. Each assistant turn sends about 2.4–3.8 KB more tool schema.

- DXF export option **Write site coordinates** (R095.e3, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095e3)):
  File → Export 2D DXF… and `sketchyup-cli --export-dxf … --dxf-site-coordinates` add the
  site origin back to X (easting) and Y (northing) in the chosen unit, so an imported
  survey-scale drawing round-trips at its own decimals. It is off by default (output bytes
  unchanged), disabled in the dialog and refused with INVALID_REQUEST when the model has no site
  origin, and Z is still not written. STL and OBJ export reports now say that coordinates are
  model coordinates when the model has a site origin; their geometry is unchanged.

- Terrain display and site settings (R095.e2, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095e2)):
  new terrains use a built-in **Terrain** material and draw as one smooth surface, with tile
  seams and interior edges hidden and normals continuous across seams. The Terrain section's
  **Display** shows contour lines at a chosen interval (every fifth an index line, drawn
  heavier; derived display, never geometry, picking or snapping) and shades by material or by
  ten elevation bands; each change is one undoable edit that keeps the mesh digest, stored on
  the terrain record with defaults omitted, and `terrain.set_display` exposes it to automation
  and MCP. **File → Site settings…** shows the site origin, CRS label and provenance, with
  **Re-anchor…** (one edit that moves geometry so site coordinates are unchanged; refused for a
  locked top-level entity or the coordinate limit) and, without an origin, **Set origin…**.
  Entity info shows site coordinates, `@E, N[, Z]` in the Measurements box types a site
  position, and a labelled crosshair marks the site origin (always with **View → Site
  origin**, a remembered choice).
- Terrain in Entity info and the tools (R095.e1, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095e1)):
  a Terrain section in the Info tab (name, triangle and tile counts, source file and digest,
  sources by role, elevation range, generator and flat areas) with **Terrain settings…** (a
  dry-run report before one undoable "Terrain settings" edit), **Explode terrain…** (a
  confirmation with the modelling and terrain budgets; Cancel is the default and Explode is
  disabled over the 100,000-face limit) and an amber "Changed since generated" chip with
  **Regenerate** (one undoable edit, confirmed first when tile paint would be dropped). The
  viewport gains a context menu (right click without dragging, Menu key or Shift+F10) with
  **Create terrain from selection** for selected edges and curves, also inside groups; a
  refusal or notices show the bounded report. Push/pull, Offset, Follow Me, Solid tools, Face
  orientation and, inside an open terrain, the drawing, guide and transform tools show the
  not-allowed cursor over a terrain tile and a status-bar message with an Explode link.
- File → Import terrain… (R095.d, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095d)):
  the desktop dialog for terrain import. Choose a DXF or point file, then the units (required;
  a DXF's declared units are offered), layer or description-code roles, point-file columns
  (PNEZD, PENZD, ENZ and XYZ presets, delimiter and header row over a sample), the site origin
  (proposed, rounded to 100 m, editable; the document's origin when it has one), an optional
  CRS label and the triangulation options, with the conversion formula and extents shown. A
  live preview switches between the sources by role and the triangulated surface, with
  elevation shading, pan, zoom and fit. The footer reports triangles and notices; the report
  groups refusals, merges and notices with their locations, and choosing a location zooms the
  preview to it. Copy or save the report. A refused import changes nothing and Import stays
  disabled; Import adds the terrain to the open model as one undoable edit.
- Terrain from model edges, terrain settings and regenerate (R095.c2, [ADR 0168](docs/decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095c2)):
  `terrain.create_from_edges` builds a terrain from picked model edges (inside groups too,
  in world coordinates), chained into contours (constant Z) and breaklines (varying Z), with
  an optional closed boundary and holes; the terrain uses the model frame and needs no site
  origin. `terrain.regenerate` rebuilds a terrain's mesh from its stored sources (for example
  when it shows "changed since generated"), and `terrain.set_parameters` changes the boundary
  mode, maximum edge length and flat-triangle options and regenerates, each as one undoable
  edit with a dry run. Paint on individual tiles cannot follow a new mesh; the report says how
  many faces lost it. Refusals change nothing and return the import's bounded report.
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

- Fixed (D-043): a terrain over about 151,000 triangles could not be imported or created, and
  Regenerate, Terrain settings and Explode failed above about 76,000, with "Edit exceeds the
  64 MiB history budget". Terrain edits now have their own 512 MiB undo history budget, so
  every terrain up to the 250,000-triangle document limit applies as one undoable edit. When
  terrain edits fill that budget the oldest undo steps are dropped, as for other history.
  Undo history for other edits is unchanged.
- Fixed (D-042): faces larger than about 10 m (quads, L-shapes, faces with holes) and section
  caps of similar size could send face tessellation into unbounded memory growth, hanging an
  edit, a load or a render until the system killed SketchyUp. The vendored Clipper2
  triangulation now tests segment crossings exactly and bounds its work, refusing a face it
  cannot triangulate instead of growing; see `third_party/clipper2/PATCHES.md`. Faces that
  tessellated before produce the same triangles.
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
