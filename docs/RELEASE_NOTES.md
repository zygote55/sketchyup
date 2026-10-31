# SketchyUp release notes

## 1.6.0

SketchyUp 1.6.0 is a minor release. It places a georeferenced orthophoto as a reference image (R107.b2, part of
offline geographic work under C06), credits a DEM terrain in a frame or a view only when the terrain is in the
camera's view (R107.c3 and R107.c3.1), and adds an in-app viewer for the user guide, with the guide text for these
features (R107.e2). It also carries the CI flake work of the 1.6.0 scope. That work fixed no defect by evidence, and
its triage is in the [open-defect triage](verification/R089a-defect-triage.md). The release is gated through its
[M10 acceptance](M10_ACCEPTANCE.md#the-160-release); its record, `docs/verification/G-1.6.0.md`, is written at the
release candidate. The [support matrix](SUPPORT_MATRIX.md) states what is supported, and the
[user guide](USER_GUIDE.md) describes each workflow. C06 stays in the Investigate class until its R107.d geocoding
is accepted. R107.d is held and is not in this release, so this release does not claim C06 as delivered.

Existing commands, queries and schemas keep their shapes, and a document with no georeferenced image is written
exactly as 1.5.0 writes it. One behaviour changes. A DEM terrain's source notice is no longer burned into a frame,
a still, an animation frame or a perspective sheet view that does not show the terrain.

### Georeferenced orthophoto (R107.b2, part of C06)

- **Desktop.** **View → Reference images**, then **Import reference image…**. A `.tif` or `.tiff` file, or a PNG or
  JPEG with its world file beside it, opens **Place georeferenced image**. The dialog reads the file on a worker and
  shows the zone, datum, corners, elevation, credit and name before **Import georeferenced image** places it as one
  undo step. A PNG or JPEG without a world file opens the plain reference-image dialog, as before
  ([R107.b2.2](decisions/0181-maps-geolocation-feasibility.md), [UX §21](UX_DESIGN.md#21-orthophoto-as-a-georeferenced-reference-image-desktop)).
- **Command line.** `sketchyup-cli --import-reference-image PATH`, with `--reference-zone` (needed for a world file when
  the site has no geographic record), `--reference-datum`, `--reference-credit`, `--reference-elevation` and `--output`
  for a new native file ([R107.b2.1](decisions/0181-maps-geolocation-feasibility.md)).
- **Inputs.** A PNG or JPEG with a north-up world file (`.pgw`, `.pngw`, `.jgw`, `.jpgw` or `.wld`), or an 8-bit RGB or
  RGBA GeoTIFF, uncompressed, LZW or Deflate, in WGS84 UTM (zones 32601 to 32660 and 32701 to 32760) or NAD83 UTM
  (zones 26901 to 26923). NAD83 is a labelled approximation, taken as WGS84 with a notice. A JPEG-compressed GeoTIFF,
  a BigTIFF, 16-bit or float pixels, a rotated or flipped world file, and a geographic-coordinate GeoTIFF are refused,
  each with a sentence that names the cause.
- **Limits.** At most 4096 by 4096 pixels, and at most 16 MiB as stored. A larger image is refused with the advice to
  crop it, and nothing is resampled. Each side is at most 1,000 km. Each corner must lie within 3° of its zone's
  central meridian and between 80° S and 84° N. An image cannot cross zones, so a GeoTIFF in a zone other than the
  site's record is refused.
- **Viewport only.** The image is drawn in the viewport. It is not drawn in renders, exports or presentation frames,
  and its credit reaches none of them. The credit (optional, at most 256 bytes) is stored with the image and shown in
  the panel and in the import report.
- **Placement.** The image lies flat at the elevation given, and is not draped onto terrain. A site with no origin
  takes one at the image's centre, rounded to 100 m. A change of the site's zone is refused while a georeferenced
  image uses the old zone, and a re-anchor that would leave the image's zone is refused.
- **Measured** on the pinned image, with the host shared. A 4096 by 4096 RGBA GeoTIFF of 64 MiB, uncompressed,
  imports through the CLI in 2.3 s; the process peak was about 167 MiB, of which about 121 MiB is above the process
  baseline. The desktop dialog reached its Ready state for the same file in 0.79 s and 1.05 s, with a process peak of
  about 408 MiB that includes the whole application. A 4096 by 4096 RGB GeoTIFF with Deflate imports in 2.5 s. A real
  photograph was not measured, and a photograph of that size may exceed 16 MiB as stored.
- **Not in this release.** Drawing the image in exports and frames (the owner decided that the orthophoto stays
  viewport-only in 1.6.0), calibrating a georeferenced image (refused by name), any online lookup, and MCP or assistant
  import of an orthophoto.

### Frame credit in the camera view (R107.c3, R107.c3.1)

- **The rule.** A DEM terrain's source notice is burned into a presentation frame, a still, an animation frame or a
  perspective sheet view only when the terrain's bounding box meets that camera's view volume. The test is
  conservative and has no occlusion test, so a terrain behind a building still counts. Before 1.6.0 a frame showed the
  notice of every terrain its scene showed. To-scale and measured sheet views keep their line rule
  ([R107.c3](decisions/0181-maps-geolocation-feasibility.md), [R107.c3.1](decisions/0181-maps-geolocation-feasibility.md)).
- **The batch checkbox.** **Burn source credits into frames** is in the batch dialog. It is checked by default, and
  the dialog checks it again each time it opens. When it is off, no frame carries a burned note. The batch manifest
  still lists the notices of the terrain its scenes show, and each frame's record lists the notices in its view,
  whichever way the box is set. `sketchyup-cli --export-presentation` takes `burnCredits` (a boolean, default true).
- **Measured** in real Blender, on a 512 by 384 frame with the 3DEP fixture. The note showed in 379 text-bright
  pixels of the top 24 rows when the terrain was in view and the box was on, and in none when the box was off or the
  camera looked away. The cost of the view test was not measured, so the per-frame budget of R099.f was not re-measured
  for 1.6.0.

### User guide viewer and guide text (R107.e2)

- **Viewer.** The GeoTIFF page's **Open the user guide** button opens the installed guide in a read-only window, scrolled
  to "Terrain from a downloaded elevation file". It replaces the system opener of 1.5.0, which might not scroll to the
  section. Links in the guide are shown as text and are not followed. Nothing is fetched. If the guide is missing, the
  window says so and names the folder it looked in ([UX §22](UX_DESIGN.md#22-user-guide-viewer)).
- **Guide text.** The user guide gains "Orthophoto from a downloaded image file", a paragraph on the batch checkbox and
  the in-view credit rule. The support matrix gains the orthophoto row. The guide text is in
  [ADR 0181](decisions/0181-maps-geolocation-feasibility.md) (Implementation notes, R107.e2 (guide text)).

### CI flake work (1.6.0 scope)

No flake was fixed by evidence, so no row of the [triage](verification/R089a-defect-triage.md) is marked Fixed and the
changelog has no line for it.

- **D-019** (a Qt Wayland protocol error when a window hidden during a file operation is shown again) is decided as a
  Limitation. Its probe hides the window for a whole file operation, which no product flow does.
- **D-007** (a dialog reported active before it had focus) has its race removed from ten dialog waits, which now wait
  for keyboard focus. **D-008** waits for the end of its scene transition, not a fixed time.
- **D-040** (a Weston crash in a shell animation) is further mitigated: the remaining shell animations are off in the
  test runs. **D-031** has its cause placed, not reproduced, and **D-034** is diagnosable: a recurrence would now name the
  frame that timed out.

The triage holds 22 open defects: none is release-blocking, none is Major, 7 are Minor and 15 are Limitations.

### Defects fixed

From the [open-defect triage](verification/R089a-defect-triage.md). Each row there states its cause and its test.

- **D-082** (Major, found by a peer session's report on 2026-10-31): on a large model, every MCP and assistant transaction failed with `STAGE_LIMIT`, and every `snapshot.begin` with `SNAPSHOT_LIMIT`, although the model was within the document limits. A proposal or a read snapshot is now charged only for the records it does not share with the live model, and the charge is checked when it is admitted. The 128 MiB staging, 32 MiB snapshot and 64 MiB durable outcome budgets are unchanged, so a durable commit still stores the before and after models within the 64 MiB outcome budget.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.6.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch packages | `sketchyup` 1.6.0-1, x86_64; `sketchyup-ifc` 1.6.0-1 (optional, IFC and STEP helper) |
| Native document | Schema 35 at most, in SKUPDOC container version 2; reads versions 1 through 35. Saves record the writer `SketchyUp/1.6.0`. The public schema changed only for the native document (`sketchyup-document-v35`) |
| MCP, inspection, transactions, recipes and other public schemas | Unchanged from 1.5.0 |
| Assistant provider | OpenAI `gpt-6-astra` over a ChatGPT-plan sign-in, with the 1.2.0 limits, as in 1.5.0. Corpus v6, unchanged in content; its `inputSha256` values are re-pinned for the 1.6.0 writer. The assistant's tool surface did not change since 1.4.0. Evaluated at the release candidate; the results are in the release record |
| New third-party code | None. The orthophoto reader uses the GeoTIFF reader's inflater and the texture decoders that 1.5.0 already ships |

Qt, OpenGL and Blender requirements are unchanged from 1.5.0.

### File format

1.6.0 reads document versions 1 through 35 and writes the schema each document needs:

| Schema | Feature | Written when |
| --- | --- | --- |
| 35 | `reference-geo-v1` | a reference image has a georeference (a world file or a GeoTIFF placed on the site) |
| 34 | `terrain-dem-v1` | the terrain was made from a GeoTIFF file (its source record) |
| 33 | `material-texture-size-v1` | a material has a texture size |
| 32 | `geographic-site-v1` | the site has a geographic record |
| 31 | `edge-styles-v1` | the document style has an edge stroke |
| 30 | `presentation-v1` | the document has a camera path or a render preset |
| 29 | `terrain-v1` | the document has a terrain or a site origin |
| 28, 27, 26 | component parameters, sheets, none | as in 1.5.0 |

A document with more than one feature is written at the highest schema it needs.

SketchyUp 1.5.0 reads schemas 1 through 34 and refuses schema 35. The released 1.5.0-1 CLI refuses a schema-35 file
with a geographic site record with `{"code":"INVALID_OPERATION","error":"Invalid required features","status":"failed"}`, and
a schema-35 file with a site but no geographic record with `"error":"Unsupported container field"` in the same form. Both
exit with status 1, and the file is unchanged (checked on the published package, 2026-10-31). The package acceptance runs the same check, and
[ADR 0162](decisions/0162-release-versioning.md) records it.

### Upgrade from 1.5.0-1

Run `pacman -U sketchyup-1.6.0-1-x86_64.pkg.tar.zst`. For the IFC and STEP helper, also install
`sketchyup-ifc-1.6.0-1-x86_64.pkg.tar.zst`; the helper is optional. Package acceptance upgrades a 1.5.0-1 package built
from the release source to 1.6.0-1. It checks that the 1.5.0-1 CLI refuses the schema-35 fixture unchanged and opens
the schema 27 to 34 fixtures unchanged.

### Rollback

To roll back, first remove `sketchyup-ifc` if it is installed (`pacman -R sketchyup-ifc`). It requires the exact
`sketchyup` version, so pacman refuses the rollback while it is installed and changes nothing (D-071). Then install
`sketchyup-1.5.0-1-x86_64.pkg.tar.zst` with `pacman -U`. A document that holds a georeferenced reference image is
schema 35 and does not open in 1.5.0, so copy such a document before you open it in 1.6.0 if you may need 1.5.0. A
document without one is written at its usual schema and opens in 1.5.0.

### Budgets

- **No new budget was missed in the measurements above, and no owner acceptance of a 1.6.0 budget is recorded.** The
  orthophoto has no time budget in ADR 0181 §9. Its CLI memory increase of about 121 MiB is inside the proposed 256 MiB.
  The desktop peak includes the whole application, so it is not compared with that figure.
- **Carried from 1.5.0, accepted there.** The STEP speed budgets (an import with a native file takes 10.17 s against
  10 s; an export takes 9.96 s against 5 s), and the render limits: a 19,998-body capture prepares in 1.70 s, over the
  1 s per-frame budget, and a 10,000-placement capture is refused by name.
- **Not yet measured.** The view test of R107.c3 runs once for each frame. Its cost is not measured, so the per-frame
  budget has not been checked against it.

### Known limitations

The 1.5.0 limitations still apply where a row above does not lift them. In particular:

- **Orthophoto:** viewport only, so its credit reaches no export or frame. The 16 MiB stored limit applies, and a real
  photograph was not measured. Only north-up images are read. NAD83 is an approximation. The read cannot be stopped part
  way: Cancel closes the dialog at once and discards the result. The dialog's layout at 200% text was not checked, and
  it was not run on Qt 6.8, the declared minimum (the pinned image has Qt 6.11.2). Its fuzz and sanitizer runs belong to
  the R083 campaign.
- **Frame credit:** no occlusion test, so a terrain behind a building still counts. A render from 1.5.0, re-rendered from
  its files, keeps its 1.5.0 stamp, and its manifest has no view test, so a terrain outside the camera's view may still
  carry the note. The cost of the view test is not measured.
- **Guide:** links are not followed, and the viewer was not run on Qt 6.8.
- **Defects:** the triage lists 22 open defects, none release-blocking and none Major (7 Minor, 15 Limitations). The
  record gives the count at the release candidate.
- **Everything in the 1.5.0 list** that no row above changes, including the IFC and STEP limits, the GeoTIFF elevation
  limits, the source-credit limits and the geographic coordinate limits.

### Not in this release

- R107.d (geocoding, held). C06 stays Investigate until R107.d is accepted.
- The orthophoto in exports and presentation frames, and any online source for an image.
- The buildingSMART hosted validation of the IFC export. That is the owner's own task and is not a gate for this release.
- Rows the owner deferred: R103 (2026-10-27), R106 and R108, and R104.b (formula mapping). R105.b to R105.d are design
  only, and Sketchfab (R105.d) is deferred.
- The 1.5.0 list still holds: Ruby extensions (C02), DWG (C05), collaboration (C07), `.skp` export, IFC2X3 and IFC4X3
  export, STEP AP242 export, and any import format beyond those listed.

## 1.5.0

SketchyUp 1.5.0 is a minor release. It carries the five follow-ups that the 1.4.0 owner decisions left for 1.5
(STEP restart-and-exclude, STEP speed, the STEP entity cap, render limits for terrains and large captures, and the
Styles and catalog dialogs), the first elevation import from files the user downloads (R107.b, part of offline
geographic work under C06), the source credits that GeoTIFF terrains add to exports (R107.c), the documentation of
that import (R107.e) and the defect fixes D-068 to D-081. The release is gated through its
[M10 acceptance](M10_ACCEPTANCE.md#the-150-release); its record is [verification/G-1.5.0.md](verification/G-1.5.0.md),
written at the release candidate. The [support matrix](SUPPORT_MATRIX.md) states what is supported, and the
[user guide](USER_GUIDE.md) describes each workflow. C06 stays in the Investigate class until its R107.d
geocoding is accepted, so this release does not claim C06 as delivered.

Existing commands, queries and schemas keep their shapes; everything new is additive, as a minor version allows
([ADR 0162](decisions/0162-release-versioning.md)). A document that has no GeoTIFF terrain is written exactly as
1.4.0 writes it.

### Elevation from a downloaded GeoTIFF (R107.b, part of C06)

- **Desktop.** **File → Import terrain…** accepts a `.tif` or `.tiff` file and opens a GeoTIFF page. The page
  takes the product (USGS 3DEP or Copernicus GLO-30), the window in UTM or in latitude and longitude, a Fit to the
  file action, the vertical reference, and, for a mismatch, the offset and a note. It shows the import report, the
  located voids and the credit under Source in Entity info ([R107.b4](decisions/0181-maps-geolocation-feasibility.md),
  [UX §20](UX_DESIGN.md#20-terrain-from-a-downloaded-elevation-file-desktop), [PR #536](https://github.com/zygote55/sketchyup/pull/536)).
- **Command line.** `sketchyup-cli --import-terrain` reads a GeoTIFF in a UTM window with `--dem-source`,
  `--dem-window`, `--dem-zone`, `--dem-vertical`, `--dem-vertical-offset` and `--dem-vertical-note`. It labels the
  datum and the vertical reference and stores the source record with the terrain ([R107.b1](decisions/0181-maps-geolocation-feasibility.md),
  [PR #529](https://github.com/zygote55/sketchyup/pull/529); [R107.b3](https://github.com/zygote55/sketchyup/pull/534)).
- **Products.** USGS 3DEP at 1/3 or 1 arc-second, and Copernicus GLO-30 at 1 arc-second north to south and 1 or
  1.5 arc-second east to west, as pixel-is-point posts. Other posts are refused by name.
- **File format.** GeoTIFF, classic TIFF only (BigTIFF is refused), one band, int16 or float32 heights, no
  compression, LZW or Deflate, stripped or tiled. LZW is read because USGS distributes 3DEP with it
  ([R107.b1.1](https://github.com/zygote55/sketchyup/pull/530)). The reduced-resolution overview images of a 3DEP
  tile are skipped, so a tile as USGS distributes it imports ([R107.b1.2](https://github.com/zygote55/sketchyup/pull/531)).
  Other inputs are refused by name, such as a second full-resolution image or a transparency mask.
- **Tested.** One real 3DEP 1 arc-second tile (USGS `n35w119`) imports as downloaded. One real 1/3 arc-second tile
  was tested only with its overview links removed, not as downloaded. GLO-30 was tested with synthetic files
  shaped like the product, and one real tile's header and one block were decoded in memory, not imported.
- **Datum and vertical reference.** WGS84 is taken as typed. NAD83 is a labelled approximation, with a notice.
  ETRS89 and OSGB36 are refused by name. The files carry no vertical reference, so the user chooses NAVD88, EGM2008
  or EGM96. There is no geoid model. A mismatch between the file and the site needs a non-zero offset and a note,
  and a zero offset is refused.
- **Voids.** A window with a void (no-data) post is refused, not filled. The located list covers the first 4,096
  void posts.
- **Limits.** Every post must lie inside the file, in one UTM zone. A model's terrains together may hold 250,000
  triangles. A window whose grid would pass that limit is decimated to fit, with a notice.
- **Measured** on the record's synthetic windows. A 1 km window at 1 arc-second imports in 18 ms, against a 2 s
  budget. A 12 km window, decimated to fit, imports in 0.36 s, holds at most 13.1 MiB of raster at a time (budget
  64 MiB), and peaks at 97 MiB in the process (budget 1 GiB). The desktop GeoTIFF path is not read whole, so the 64 MiB
  file limit that applies to DXF and point files does not apply to it ([R107.b1](decisions/0181-maps-geolocation-feasibility.md), [R107.b3](decisions/0181-maps-geolocation-feasibility.md)).
- **Not in this release.** Orthophoto placement (R107.b2, optional), geocoding (R107.d, held), any online lookup, and
  MCP or assistant import: `terrain.import` takes DXF and point files only.
- **Downloads.** SketchyUp fetches no elevation data. Download steps, the credit each product asks for, and the
  non-affiliation wording are in the user guide, under [Terrain from a downloaded elevation file](USER_GUIDE.md#terrain-from-a-downloaded-elevation-file)
  ([R107.e](https://github.com/zygote55/sketchyup/pull/537)).

### Source credits in exports (R107.c)

- **Data exports.** A terrain made from a GeoTIFF file adds its source notice to the GLB `asset.copyright`, the OBJ
  comment lines, the IFC site's property set `SketchyUp_SourceCredits` (only when the site origin came from the
  file), the STL export report and the presentation batch manifest's `credits` ([R107.c1](https://github.com/zygote55/sketchyup/pull/533)).
- **DXF.** A DXF export carries the notice only for a terrain whose every height is zero. DXF export refuses a real
  elevation terrain, since it writes world XY only.
- **STEP.** No notice is written. This was decided, not omitted.
- **Documents with no GeoTIFF source** write no credit keys, and the notices cannot be removed from an export of a
  GeoTIFF terrain.
- **Visible credits** ([R107.c2](https://github.com/zygote55/sketchyup/pull/535)). A PDF sheet page, and a measured PDF
  or SVG drawing, print a "Sources" line in the bottom margin, and export is blocked when the margin has no room. A
  presentation frame of a GeoTIFF terrain carries a burned-in note, and a note over 700 bytes is refused.

### STEP restart-and-exclude (R098.i)

- A STEP file whose tessellation crashes the helper, or overruns its limits, is restarted without the part it stopped
  on, up to three times. The import is then partial, and the report names the part that was left out
  ([PR #522](https://github.com/zygote55/sketchyup/pull/522)).
- A sandbox kill, or a stop before tessellation, still refuses the whole file, and the model is unchanged.
- Limit: no committed STEP fixture makes the pinned helper crash. The restarts were tested with stand-in helpers, and
  the mutation checks were not run.

### STEP speed and entity cap (R098.j, R098.k)

- **Speed.** Measured on the release machine, against the 1.4.0 numbers:

  | Case | 1.5.0 | Budget | 1.4.0 |
  | --- | --- | --- | --- |
  | Import of the 100-sphere model, with a new native file | 10.17 s (max 10.23 s) | 10 s, missed | 14.70 s |
  | Import of the same model, with no output file | 6.87 s (max 6.96 s) | 10 s, met | 10.32 s |
  | Export of the 90,954-face model | 9.96 s (max 9.96 s) | 5 s, missed | 16.56 s |

  The export writes 100,551,450 bytes, against 223,950,330. Peak memory is 700 MiB for the import, against 724 MiB,
  and 1,034 MiB for the export, against 1,926 MiB ([R098.j](verification/R098d-step-exchange.md#14-r098j-step-conversion-speed-2026-10-29),
  [PR #524](https://github.com/zygote55/sketchyup/pull/524)). The two misses are the ones the owner accepted as limits
  for 1.4.0 (decision 8, 2026-10-28), and they remain accepted limits in 1.5.0.
- **Entity cap.** A STEP file whose reader holds more than 2,000,000 entities is refused before any transfer, with
  the count and the limit (`--step-max-entities`) ([R098.k](https://github.com/zygote55/sketchyup/pull/525)).
- **Threads.** STEP keeps one tessellation thread. On the 100-sphere model, parallel meshing gave identical output and
  no speed-up (3.97 s at four threads, 3.96 s at one). A body with many faces was not measured, and that is the one
  case where the decision could change.

### Render limits for terrains and large captures (R099.f)

- **Terrains.** A terrain of 249,218 triangles exports and renders, with a 75.36 MB manifest and a median preparation
  of 3.69 s. A terrain counts against its own 250,000-triangle class, not the 100,000-face modelling budget.
- **Large captures.** A capture of 19,998 bodies (79,992 faces) exports and renders, with a 29.12 MB manifest and a
  median preparation of 1.70 s.
- **Manifest bound.** The bound rises from 16 MiB to 128 MiB, which is the largest manifest the admitted limits can
  produce.
- **Still refused.** A 10,000-placement capture (30,257 bodies, 119.16 MB) is refused by name. The GLB modelling
  limit of 100,000 faces and the render snapshot's limit of 20,000 bodies are unchanged.
- **Budget.** The per-frame budget is 1 s at the 95th percentile. The terrain and the 19,998-body capture are over it;
  the record gives only their medians. The 3,700-body model prepares in 303 ms (95th percentile 304 ms)
  ([R099.f](verification/R099e-presentation.md#12-r099f-render-limits-for-terrains-and-large-captures),
  [PR #526](https://github.com/zygote55/sketchyup/pull/526)).

### Catalog progress and Cancel (R100.i) and the Styles dialog (R100.h)

- **Insert.** An asset catalog insert runs off the interface thread. A progress sheet names the phases (read, verify,
  insert) and the catalog, and opens after one second, so a sub-second insert shows none
  ([PR #521](https://github.com/zygote55/sketchyup/pull/521)).
- **Cancel.** Cancel before the edit is composed changes nothing: the undo history, definitions, materials, assets,
  style and library folder stay as they were. A Cancel after the edit is composed comes too late, and the insert
  commits. Measured on a 128 MiB catalog, one entry takes 0.59 to 0.61 s in the command line, and 342 to 357 ms in
  the worker. The longest interface stop was 11.6 to 16.5 ms, and a Cancel clicked at once took 4.6 to 4.8 ms.
- **Styles.** **Save report…** writes the asset catalog report as a text file, with an overwrite prompt. The accounting
  bar is solid, hatched and cross-hatched, with a legend, and refusals open with "Nothing was saved." or "Nothing was
  applied." ([R100.h](decisions/0174-custom-styles-asset-authoring.md), [PR #520](https://github.com/zygote55/sketchyup/pull/520)).
  The IFC import, IFC export and STEP export reports keep a colour-only bar, and the IFC import report has no
  **Save report…**.

### Defects fixed since 1.4.0

From the [open-defect triage](verification/R089a-defect-triage.md). Each row there states its cause and its test. D-076 to D-080 were fixed before this release and were never in a release; D-081 shipped in 1.4.0.

- **D-068 and D-069** ([PR #527](https://github.com/zygote55/sketchyup/pull/527)): the Quantities Tags popover no
  longer prints a Qt Wayland popup warning, and the desktop build's compiler warnings are fixed.
- **D-070** ([PR #519](https://github.com/zygote55/sketchyup/pull/519)): a test harness fix. The `.skp` import dialog
  test holds its import stand-ins until the driver sees the progress step.
- **D-071**: the 1.4.0 release notes' rollback steps now say to remove `sketchyup-ifc` first
  ([PR #518](https://github.com/zygote55/sketchyup/pull/518)).
- **D-072** ([PR #523](https://github.com/zygote55/sketchyup/pull/523)): a 96-byte Wayland proxy that the sanitized
  native MCP step leaked at exit. A test fix.
- **D-073** ([PR #528](https://github.com/zygote55/sketchyup/pull/528)): a catalog insert that finished at once could
  wait forever. It came with R100.i, after 1.4.0, and it was fixed before 1.5.0.
- **D-074** ([PR #534](https://github.com/zygote55/sketchyup/pull/534)): a GeoTIFF import whose window was narrower
  than its spacing never ended. It came with R107.b1 and was never in a release.
- **D-075** ([PR #540](https://github.com/zygote55/sketchyup/pull/540)): editing a component whose definition holds
  more than half the unique-geometry limit was refused, because the edit counted that definition twice. An explode or
  make-unique that left a definition with no placement also kept counting it. The edit now counts the definition once,
  and an explode or make-unique removes a definition that nothing uses any more, in the same edit, so one undo brings
  it back. A batch response lists such definitions in `removedDefinitions`.
- **D-076** ([PR #530](https://github.com/zygote55/sketchyup/pull/530)): a real USGS 1/3 arc-second GeoTIFF tile was refused by a too-tight post-step tolerance.
- **D-077** ([PR #534](https://github.com/zygote55/sketchyup/pull/534)): a GeoTIFF import into a model with other terrains chose its spacing without their room, so a window could be refused.
- **D-078** ([PR #534](https://github.com/zygote55/sketchyup/pull/534)): a GeoTIFF import set a coordinate-system label with no geographic record, so the zone and vertical checks did not run.
- **D-079** ([PR #534](https://github.com/zygote55/sketchyup/pull/534)): the file's SHA-256 was taken after the read, so a file changed during the read could be recorded with the wrong digest.
- **D-080** ([PR #536](https://github.com/zygote55/sketchyup/pull/536)): a test-only fix to the keyboard audit's handling of radio groups.
- **D-081** ([PR #524](https://github.com/zygote55/sketchyup/pull/524)): a STEP export of a 90,954-face model could not be read back, because its 224 MB file exceeded the 128 MiB input limit. Shipped in 1.4.0.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.5.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch packages | `sketchyup` 1.5.0-1, x86_64; `sketchyup-ifc` 1.5.0-1 (optional, IFC and STEP helper) |
| Native document | Schema 34 at most, in SKUPDOC container version 2; reads versions 1 through 34. Saves record the writer `SketchyUp/1.5.0`. The public schema changed only for the native document (`sketchyup-document-v34`) |
| MCP, inspection, transactions, recipes and other public schemas | Unchanged from 1.4.0 |
| Assistant provider | OpenAI `gpt-6-astra` over a ChatGPT-plan sign-in, with the 1.2.0 limits, as in 1.4.0. Corpus v6, unchanged in content; its `inputSha256` values are re-pinned for the 1.5.0 writer. Evaluated at the release candidate; the results are in the release record |
| New third-party code | None. The GeoTIFF reader uses the inflater of miniz, which 1.4.0 already ships (MIT, in `sketchyup`). The elevation data are the user's own downloads |

Qt, OpenGL and Blender requirements are unchanged from 1.4.0.

### File format

1.5.0 reads document versions 1 through 34 and writes the schema each document needs:

| Schema | Feature | Written when |
| --- | --- | --- |
| 34 | `terrain-dem-v1` | the terrain was made from a GeoTIFF file (its source record) |
| 33 | `material-texture-size-v1` | a material has a texture size |
| 32 | `geographic-site-v1` | the site has a geographic record |
| 31 | `edge-styles-v1` | the document style has an edge stroke |
| 30 | `presentation-v1` | the document has a camera path or a render preset |
| 29 | `terrain-v1` | the document has a terrain or a site origin |
| 28, 27, 26 | component parameters, sheets, none | as in 1.4.0 |

A document with more than one feature is written at the highest schema it needs.

SketchyUp 1.4.0 reads schemas 1 through 33 and refuses schema 34 with "Invalid required features", leaving the
file unchanged. The package acceptance runs the same checks: the 1.4.0-1 CLI must refuse the schema-34 fixture and open the
schema 27 to 33 fixtures unchanged ([ADR 0162](decisions/0162-release-versioning.md)).

### Upgrade from 1.4.0-1

Run `pacman -U sketchyup-1.5.0-1-x86_64.pkg.tar.zst`. For the IFC and STEP helper, also install
`sketchyup-ifc-1.5.0-1-x86_64.pkg.tar.zst`; the helper is optional. Package acceptance upgrades a 1.4.0-1 package built
from the release source to 1.5.0-1, and checks that the 1.4.0-1 CLI refuses the schema-34 file unchanged and opens
the schema 27 to 33 files unchanged.

### Rollback

To roll back, first remove `sketchyup-ifc` if it is installed (`pacman -R sketchyup-ifc`). It requires the exact
`sketchyup` version, so pacman refuses the rollback while it is installed and changes nothing (D-071). Then install
`sketchyup-1.4.0-1-x86_64.pkg.tar.zst` with `pacman -U`. A document with a GeoTIFF terrain does not open in 1.4.0, so
copy such a document before you open it in 1.5.0 if you may need 1.4.0.

### Known limitations

The 1.4.0 limitations still apply where a row above does not lift them. In particular:

- **IFC** is unchanged from 1.4.0 and accepted with two documented limits, as [G-1.4.0](verification/G-1.4.0.md) records: a 10,000-body import takes about 12 s against 10 s (the owner accepted that as a limit on 2026-10-28, decision 9), and the export passes the buildingSMART rules run locally and `ifcopenshell.validate` but was not checked by buildingSMART's hosted service (decision 10).
- **STEP** is partly met. Its speed budgets are missed, as above, and they are accepted limits. A 1,000,000-triangle
  export has not been measured since R098.j. The 1.4.0 record shows a crash at 21.6 s under the 4 GiB cap, and says
  the model cannot be built in one document. The other STEP limits of 1.4.0 hold: 128 MiB input, 1,000,000 triangles
  and 100,000 per element, a document of 100,000 faces of unique geometry, and an export refused above 256 MiB.
  Cancel latency on the real helper was not measured.
- **Render limits:** the 10,000-placement capture is refused, and the terrain and the 19,998-body capture are over the
  1 s per-frame budget at their medians. The batch projection (960 frames, about 3.4 hours) is from a 48-frame run,
  and the full run was not made. An EEVEE preset with no finished frame cannot resume.
- **Styles and catalogs:** the IFC and STEP report bars are colour-only, and the IFC import report has no
  **Save report…**. A catalog component is placed at the origin only, and a template in a catalog cannot be opened as
  a model. Selecting an entry in a catalog opened from a file still runs its check on the interface thread, about
  0.1 s on a 128 MiB catalog.
- **GeoTIFF elevation:**
  - Heights are not checked against control points. The R083 sanitized failure matrix was not run for this reader;
    its fuzz target ran, and the others did not.
  - The 1/3 arc-second 3DEP tile was tested only with its overview links removed. GLO-30 was tested with synthetic files.
  - WGS84 and NAD83 only. NAD83 is an approximation. There is no geoid model.
  - A window with a void is refused, not filled.
  - A model's terrains together hold at most 250,000 triangles, and a larger window is decimated, with a notice.
  - There is no MCP or assistant import.
- **Source credits:** STL has a report line, not a field. DXF refuses a real elevation terrain. The IFC property set is
  written only when the site origin came from the file, and STEP writes none.
- **Geographic coordinates** (R107.a): unchanged from 1.4.0. There is no online lookup.
- **Desktop link:** the GeoTIFF page's **Open the user guide** button ([PR #538](https://github.com/zygote55/sketchyup/pull/538))
  opens the installed guide (`/usr/share/doc/sketchyup/USER_GUIDE.md`) at its download section with the system's
  default viewer. A Markdown viewer may open the file without scrolling to the section. Nothing is fetched.
- **Defects:** the triage lists 22 open defects at the 1.5.0 preparation, none release-blocking and none Major. The
  record gives the count at the release candidate.
- **Quantities, the helper sandbox, online libraries, `.skp` import, camera paths and the assistant:** as in 1.4.0.

### Not in this release

- R107.b2 (orthophoto placement, optional) and R107.d (geocoding, held). C06 stays Investigate until R107.d is accepted.
- The buildingSMART hosted validation of the IFC export. That is the owner's own task and is not a gate for this release.
- Rows the owner deferred: R103 (2026-10-27), R106 and R108, and R104.b (formula mapping). R105.b to R105.d are design
  only, and Sketchfab (R105.d) is deferred.
- The 1.4.0 list still holds: Ruby extensions (C02), DWG (C05), collaboration (C07), `.skp` export, IFC2X3 and IFC4X3
  export, STEP AP242 export, and any import format beyond those listed.

## 1.4.0

SketchyUp 1.4.0 is a minor release. It adds one named group of Extended rows, the group that
[M10 acceptance](M10_ACCEPTANCE.md) calls the 1.4.0 group: IFC and STEP exchange (scope row F04),
camera paths and batch presentations (F05), custom styles and asset catalogs (F06), quantities,
reports and schedules (F07), `.skp` import (C01, import only), offline geographic coordinates (C06,
offline only) and the helper process sandbox (R109). The group is gated as one release, and it is
not claimed until its record is accepted. The release record, with the release-candidate evidence,
is [verification/G-1.4.0.md](verification/G-1.4.0.md). The [support matrix](SUPPORT_MATRIX.md)
states what is supported, and the [user guide](USER_GUIDE.md) describes each workflow.

Existing commands, queries and schemas keep their shapes; everything new is additive, as a minor
version allows ([ADR 0162](decisions/0162-release-versioning.md)). A document that uses none of the
new features is written exactly as 1.3.0 writes it.

### IFC import and export

IFC support covers three schemas for import and IFC4 for export ([ADR 0171](decisions/0171-ifc-exchange-subset.md)).

- **File → Import IFC…** reads `IFC2X3`, `IFC4` (IFC4 ADD2 TC1) and `IFC4X3_ADD2` (the building
  subset of IFC 4.3.2.0) into a new, unsaved model. It keeps the spatial structure (project, site,
  building, storey and space), the elements with a body, their properties and quantities, and their
  materials and colours. The report lists everything it did not import. The source file is never
  changed.
- **File → Export IFC…** writes `IFC4` only, tessellated, with no view definition and no
  certification claim. It never replaces an existing file. It passes the buildingSMART rules run
  locally and `ifcopenshell.validate`; it has not been checked by buildingSMART's hosted validation
  service.
- The command line does the same work: `sketchyup-cli --import-ifc` and `--export-ifc`. The helper,
  `sketchyup-ifc-worker`, is in the optional `sketchyup-ifc` package.
- A map conversion with no rotation sets the site origin. A rotation keeps the project axes unless
  `--ifc-georef grid` is given. A non-metre map unit, a scale, several conversions or a non-identity
  world coordinate system is reported, and the last is refused.

### STEP import and export

STEP support reads AP203 and AP214, and AP242 on a best-effort basis; it writes AP214 (the default)
or AP203 ([ADR 0172](decisions/0172-step-exchange-subset.md)).

- **File → Import STEP…** and `sketchyup-cli --import-step` read a STEP file into a new, unsaved model.
  A declared unit converts and is reported. An undeclared unit is refused unless you choose one; the
  unit is never guessed.
- **File → Export STEP…** and `sketchyup-cli --export-step` write a new file. AP242 is never written:
  a request for it is refused.
- Assemblies, instances, names, flat colours, `step.Layer` and hidden parts come across in both
  directions. A layer's own on/off switch, material names, opacity and textures do not.
- Curved surfaces arrive as meshes of flat faces. They are not editable as curves, and an export
  writes the mesh only.
- Fixes in this release (PRs #510 and #511): a bad solid's surfaces are no longer imported into another
  part's body; problems are located in the import report; PMI and GD&T are counted and reported; and parts
  nested too deeply are counted.

### `.skp` import

**File → Import .skp file…** and `sketchyup-cli --import-skp` read a SketchUp model file (`.skp`) as a
new, unsaved SketchyUp model ([ADR 0176](decisions/0176-skp-import.md)). The file is read by OpenSKP,
an MIT-licensed reader that runs in a sandboxed helper. There is no `.skp` export.

- Imported: faces with their holes, edges with their flags, components, tags, materials with their
  textures (tested with synthetic PNG files only; the JPEG path and the conversion of other formats are
  not tested), names and attributes. A Dynamic Component's attributes come across as read-only
  properties, tested on synthetic files only.
- Not imported: scenes, styles, section planes, text, dimensions, guides and the camera. A group
  becomes a component used once, not a group, as [ADR 0176](decisions/0176-skp-import.md) §4 notes as
  a gap.
- SketchUp 2024 files import. One 2024 model was tested; other 2021-and-later versions use the same
  container and are untested. A file from SketchUp 2025 or later is read and labelled "newer than
  tested" in the report.
- A file from before 2013 is refused with advice to save it again. A 2013 to 2020 file is read on a
  best-effort basis and labelled as such in the report. These files are tested only with synthetic files.
- Fixes in this release (R102.f, PR #513): a helper run with its output on `/dev/null` is no longer
  ended by its own sandbox, and a large file is hashed in linear time, so it no longer risks the
  helper's 120 s deadline. The "newer than tested" label now starts at SketchUp 2025 files, as the documentation says.

### Camera paths and batch presentations

- **Camera → Presentations…** edits named camera paths of two to 32 saved scenes, and named render
  presets (settings only). A path plans at most 240 frames, and a document holds at most 64 paths and
  16 presets ([ADR 0173](decisions/0173-camera-paths-batch-output.md)).
- **Export presentation batch…** (in command search) renders one capture through up to four presets
  to a new folder: PNG frames with a JSON record for each frame, a batch `manifest.json`, and an
  optional H.264 MP4 for each preset. Blender 5.2 LTS renders the frames. A batch is refused before any
  frame renders when the chosen codec is missing from that Blender. MPEG-4 Part 2 is offered only as an
  explicit choice when H.264 is missing.
- `sketchyup-cli --export-presentation` runs the same batch from a request file of at most 16 KiB.
  Ctrl-C (SIGINT) cancels at the next frame boundary and keeps the finished frames.
- Camera paths and render presets save with the document as schema 30 (`presentation-v1`).

### Custom styles and asset catalogs

- **Edge strokes** in the document style: width, opacity, dash and an optional texture. A missing
  texture draws hatched, with a status chip that opens the Styles tab. Saved as schema 31
  (`edge-styles-v1`) ([ADR 0174](decisions/0174-custom-styles-asset-authoring.md)).
- **Textured material presets:** fifteen under **Textured** in the New material dialog, nine CC0
  textures from Poly Haven and six generated ones. Each preset shows its source and licence. The Poly
  Haven notice is in `THIRD_PARTY_NOTICES`.
- **Material texture size:** a material can store the metres that one texture repeat covers, in
  **Set size** in the New and Edit material dialogs. Saved as schema 33 (`material-texture-size-v1`).
- **Styles** (`.sketchystyle`): **File → Import style…** and **File → Export style…**, and a Style
  library section in the Styles tab. A missing texture is named and nothing is applied or written.
- **Asset catalogs** (`.sketchycat`): **File → Asset catalogs…** lists the catalogs in the library
  folder and any catalog you open, with search and a Kind filter, and inserts the chosen entries at the
  model's origin as one undo step. **File → Export asset catalog…** writes components, materials and a
  style to a new file. A tampered or incomplete entry is refused and named; it is never replaced with a
  default. The command line has `--import-style`, `--export-style`, `--list-catalog`, `--import-catalog`
  and `--export-catalog`.

### Quantities, reports and schedules

- **View → Quantities** shows the counts, areas and volumes of the model, grouped by kind, tag, material
  or definition. Hidden content is included unless you choose Visible only. CSV is the file of record
  and JSON is also available; both write a new file only ([ADR 0175](decisions/0175-quantities-reports-schedules.md)).
- A solid counts toward volume only when it validates. Other solids are listed with their reason, and
  their area still counts. Sheets, views and items appear only in the file's metadata.
- `sketchyup-cli --report` writes a report file. `--save-schedule`, `--list-schedules`,
  `--delete-schedule` and `--schedule` manage saved schedules, which are kept on the computer, not in
  the model. Over MCP and the session, `report.run` and `report.schedule` are available. The in-app
  assistant cannot run either.

### Geographic site coordinates

- A site can carry a geographic record: a WGS84 or NAD83 position, converted to UTM with an in-house
  series, with no network, grid file or PROJ library ([ADR 0181](decisions/0181-maps-geolocation-feasibility.md)).
  A site with the record is schema 32 (`geographic-site-v1`); a site without it keeps its schema.
- The sun panel's **Use site location** applies the position as one undo step. The site and solar
  describe operations report and warn when the two locations disagree.

### Dynamic Components in `.skp` files (read-only, R104.a)

A Dynamic Component's `dynamic_attributes` values come across as read-only properties, at instance and
definition level, with lengths in metres. Formulas (`_<name>_formula`, `_material_formula`) and
`onClick` are kept as text and are never evaluated or run. A Live Component is not identified, because
the file does not mark it ([ADR 0178](decisions/0178-dynamic-live-component-feasibility.md)). These are
tested on synthetic files written by OpenSKP's writer; no SketchUp file with Dynamic Components has been
tested. Formula mapping (R104.b) is deferred.

### Helper process sandbox

The `.skp`, IFC and STEP, text, extension and Blender helpers run under a sandbox: `PR_SET_NO_NEW_PRIVS`,
a Landlock ruleset that confines each helper to its input and a private directory, and a seccomp-bpf
allow-list with a kill default. A helper cannot open a socket or run another program, and it reads only the files
its policy names: its input, a private directory, and for the text helper the font folders and the locale. A call outside its policy ends the helper and is reported as a sandbox violation; it is not
restarted. A helper whose sandbox layer cannot be installed refuses, naming the layer, and reads
nothing ([ADR 0183](decisions/0183-helper-process-sandbox.md)). The `sandbox_available` check is required
in CI.

### Other changes

- **Online libraries:** SketchyUp does not search, sign in to or download from any online library. You
  download a file yourself and import it. A ZIP archive, COLLADA (`.dae`), USDZ and FBX file is refused with
  the reason ([ADR 0179](decisions/0179-hosted-asset-access-feasibility.md)). The user guide's page
  "Getting models from online libraries" describes the download steps (R105.a).
- The asset catalogs dialog no longer stalls the window on a large catalog. Selecting an entry in a
  catalog opened from a file takes about 0.1 s on a 128 MiB catalog, against about 2 s before.
- The Outliner rebuilds only when what it shows changes. A placement move on a 100,000-face model
  takes about 37 ms of its work, against about 148 ms before (Release, p50).
- The render queue refuses a request for a result before the render finishes, and keeps the render's
  state. Presentation frames no longer carry Blender's date, time and statistics text chunks, so a
  frame renders to the same bytes.

### File format

1.4.0 reads document versions 1 through 33 and writes the schema each document needs:

| Schema | Feature | Written when |
| --- | --- | --- |
| 33 | `material-texture-size-v1` | a material has a texture size |
| 32 | `geographic-site-v1` | the site has a geographic record |
| 31 | `edge-styles-v1` | the document style has an edge stroke |
| 30 | `presentation-v1` | the document has a camera path or a render preset |
| 29 | `terrain-v1` | the document has a terrain or a site origin |
| 28, 27, 26 | component parameters, sheets, none | as in 1.3.0 |

A document with more than one feature is written at the highest schema it needs.

SketchyUp 1.3.0 refuses schemas 30 to 33 with "Unsupported container field" and leaves the file
unchanged. The published 1.3.0-1 package was checked against each of these files, and the
package acceptance runs the same checks. Files at schema 29 and below open in 1.3.0.

### Upgrade from 1.3.0-1

Run `pacman -U sketchyup-1.4.0-1-x86_64.pkg.tar.zst`. For the IFC and STEP helper, also install
`sketchyup-ifc-1.4.0-1-x86_64.pkg.tar.zst`; the helper is optional. Package acceptance upgrades a
1.3.0-1 package built from the release source to 1.4.0-1, and checks that the 1.3.0-1 CLI refuses
each newer file unchanged.

To roll back, install `sketchyup-1.3.0-1-x86_64.pkg.tar.zst` with `pacman -U`. If `sketchyup-ifc` is
installed, remove it first with `pacman -R sketchyup-ifc`: it requires `sketchyup` 1.4.0, so pacman
refuses the rollback while it is installed and changes nothing (D-071). A document with a
camera path or preset, an edge stroke, a geographic site or a material texture size does not open
in 1.3.0. Copy such a document before you open it in 1.4.0 if you may need 1.3.0. When 1.4.0 saves
over an existing document, the previous bytes are kept as `NAME.sketchyup.bak`.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.4.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch packages | `sketchyup` 1.4.0-1, x86_64; `sketchyup-ifc` 1.4.0-1 (optional, IFC and STEP helper) |
| Native document | Schema 33 at most, in SKUPDOC container version 2; reads versions 1 through 33. Saves record the writer `SketchyUp/1.4.0` |
| MCP, inspection, transactions and other public schemas | Same versions as 1.0.0, with the additive sheet (1.1.0), component parameter (1.2.0), site and terrain (1.3.0) and presentation, style, catalog, report and geographic operations and queries (1.4.0) |
| Assistant provider | OpenAI `gpt-6-astra` over a ChatGPT-plan sign-in, with the 1.2.0 limits. Evaluated at the release candidate against corpus v6; the results are in the release record |
| New third-party code | OpenSKP (MIT, `.skp` import, in `sketchyup`), IfcOpenShell 0.9.0 with the OpenCASCADE libraries it needs (LGPL, IFC and STEP helper, in `sketchyup-ifc`), and the Poly Haven CC0 textures. Their notices are in the release's `THIRD_PARTY_NOTICES` |

Qt, OpenGL and Blender requirements are unchanged from 1.0.0. The camera paths, batches and textured
presets need Blender 5.2 LTS.

### Known limitations

The 1.3.0 limitations still apply where a row above does not lift them. For the 1.4.0 group:

- **IFC is not yet accepted.** Its acceptance record is partial: the owner's
  buildingSMART hosted validation has not been run, so the IFC4 export claim is conditional on it, and
  the native matrix runs on the pull request. A model of about 10,000 bodies imports in about 12 s,
  against a 10 s budget. The owner accepted that as a limit for 1.4.0 (2026-10-28). A damaged-file run of 5,120 mutations found one crash in the IFC library,
  which is not fixed.
- **IFC limits:** a file over 128 MiB or 2,000,000 entities is refused. A model over the document limits
  (100,000 vertices or faces, 10,000 bodies) imports partly, and the report names what is left out. IFC
  4.3 infrastructure, advanced breps, NURBS, CSG, swept solids, revolutions and parametric profiles are
  not imported. `IFC2X3` and `IFC4X3` are not exported. `.ifczip`, `.ifcXML`, `.ifcJSON` and IFC5 are not
  read. Two IFC classes, `IfcGrid` and `IfcRelNests`, are omitted, and `IfcAnnotation` and
  `IfcVirtualElement` are reported as unsupported.
- **STEP is not yet accepted.** Its record is partly met: three of eight checks pass in full, five are
  partial, and three budgets are missed. On 2026-10-28 the owner accepted the missed budgets as limits for 1.4.0.
  The end-to-end import of a 100-sphere model takes about 14 s (13.8 to 15.8 s measured) against a 10 s budget. Exporting the 90,954-face model imported
  from it takes about 17 s (16.6 to 16.8 s measured) against 5 s. Speeding up the conversion step is planned for 1.5.
- **STEP import limits:** 128 MiB input, 1,000,000 triangles and 100,000 triangles per element. A document holds
  100,000 faces of unique geometry, so a curved model stops at about 100,000 triangles and the rest is
  left out and named in the report. A STEP export is refused above 256 MiB, which is reached at about
  110,000 triangle faces of curved imports. A limit breach fails the import. A STEP file that crashes the
  helper is refused whole, and the model is unchanged. Restart-and-exclude is planned for 1.5. Cancel
  latency on the real helper was not measured.
- **STEP export budget:** the largest model a document can hold, five placements of a 100,000-triangle grid, exports in about
  19 s, within its 23 s budget (R098.h). The export
  helper keeps its 4 GiB address-space cap.
- **`.skp` import is tested on one model.** The release tested one 2024 model and 15 synthetic files. SketchUp 2024 files import; other 2021-and-later versions use the same container and are untested. 2013 to 2020 files are best effort, tested only with synthetic files. Some 2013 to 2020 files fail:
  one cause, in a text label, is known (OpenSKP issue 442), and another, in files from 2017 to 2020, is not
  yet understood. Dynamic Component formulas are not run, and Live Components are not identified. Dynamic Components and textures are tested on synthetic files only. There is
  no export.
- **Camera paths and batches:** a 250,000-triangle terrain cannot be rendered or batched; the GLB export
  refuses more than 100,000 visible faces, and the render snapshot refuses more than 20,000 bodies. Both limits stay in 1.4.0, and raising them is planned for 1.5. A
  full 960-frame batch (240 frames for each of four presets, at 1024 × 768 and 32 samples) is projected at
  about 3.4 hours on the test machine. The owner accepted that projection, which comes from a measured 48-frame run; the full run was not made. A batch has progress, and it cancels at frame boundaries. An Eevee
  preset with no finished frame cannot resume. There is no HDR environment in version 1.
- **Styles and catalogs:** a catalog import has no progress and no Cancel, and both are planned for 1.5; a 128 MiB catalog imports one
  entry in about 0.6 s. Catalog components are not deduplicated, templates in a catalog cannot be opened
  as a model, and placement is at the origin only. Three style-dialog differences from the design are accepted for 1.4.0 and planned for a fix in 1.5: there
  is no **Save report…** button (**Copy report** is there), the usage bar is in three colours rather than
  hatched, and a refused save uses the wording "Save refused:", which the design does not give. The in-app
  assistant cannot change styles or catalogs.
- **Quantities:** a solid that fails validation has no volume, and terrain has area only. A material filter
  applies to area rows only. A schedule keeps its precision, not its display units. Cancel acts between
  records, so one very large body cannot be interrupted. On a model of about 100,000 faces, an edit holds the
  UI for 0.3 to 0.4 s locally and 0.85 s in CI; this is outside the report code.
- **Geographic coordinates:** WGS84 and NAD83 only. NAD83 is an approximation and is labelled as one. OSGB36
  and ETRS89 are refused by name. The UTM grid covers 80° S to 84° N, and Norway and Svalbard are refused by
  name. There is no online lookup, no elevation import and no attribution.
- **Helper sandbox:** Linux only. It needs Landlock and seccomp. Unavailable layers refuse the import with a
  named reason. The kernels of 24.04 and 26.04 were not measured. Access to `chmod`, `/proc` reads and device
  `ioctl` calls is not limited.
- **Online libraries:** SketchyUp makes no network request for them. A ZIP archive, COLLADA (`.dae`), USDZ and
  FBX file is refused with the reason; a ZIP archive must be extracted first.
- **Assistant:** the in-app assistant cannot run the report, the schedule or the presentation commands, nor
  change styles, catalogs or parameters.

The open-defect list is [R089.a](verification/R089a-defect-triage.md).

### Not in this release

- The Investigate rows C02 (Ruby extensions), C05 (DWG), C07 (collaboration) and the Dynamic and Live
  Component formula mapping (R104.b). Each was deferred or found not viable ([ADR 0177](decisions/0177-ruby-extension-feasibility.md),
  [ADR 0180](decisions/0180-dwg-feasibility.md), [ADR 0182](decisions/0182-collaboration-feasibility.md)).
- Geographic elevation import (DEM and GeoTIFF from user downloads), credits in exports, geocoding and the
  geographic documentation (R107.b to R107.e), and orthophoto placement.
- Hosted-asset adapters (R105.b to R105.d). Only user-mediated downloads are in this release.
- `.skp` export, IFC2X3 and IFC4X3 export, STEP AP242 export, and any import format beyond the ones above.
- Ruby extension compatibility, native `.skp` support beyond import, and full SketchUp parity.
- Planned for 1.5, from the 1.4.0 owner decisions: a higher render and batch limit for 250,000-triangle
  terrains; catalog import progress and Cancel; the Styles dialog fixes; STEP restart-and-exclude; faster
  STEP conversion; and buildingSMART's hosted validation of the IFC export. The roadmap lists them.

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
responses) and the task's budgets still stop the task. When the ChatGPT plan's usage limit is
reached, the panel now says so and points to Manage ChatGPT usage; 1.2.0 reported "Provider
returned an incomplete or invalid response" instead (D-046). Any other error the service reports
in its reply ends the task with "Provider returned an error:" and the service's code and message,
bounded and with anything shaped like a credential removed
([ADR 0034 addendum](decisions/0034-openai-provider.md#addendum-2026-10-21-stream-error-fields-d-046)). The ChatGPT-plan limits and the
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
