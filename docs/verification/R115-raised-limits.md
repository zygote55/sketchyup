# R115: raised model limits, measured on real hardware

Date: 2026-11-07. Branch `feat/r115-raise-limits`. The owner approved the raise on 2026-11-07:
"Forget about that limit - it was completely arbitrary." The product limits are the values measured in
[R114](R114-hardware-limits.md), not a memory ceiling. The 3.75 GiB and 8 GiB ceilings of earlier records
only protected the development laptop and do not apply (owner, 2026-11-07).

Status of this record: **the code and the CI changes are complete, and the review fixes of 2026-11-08 are in (see
"Review findings (2026-11-08)"). The at-limit acceptance ran on the GPU PC on 2026-11-08. Brief cases 1 to 5 passed;
case 6 passed except the Blender import of the GLB, which did not run; case 7 passed for OBJ and STL and failed for glTF
(refused by the component definition count); case 8 (STEP) did not run (see "Acceptance on the GPU PC").** Models are synthetic.

## Values as built

| Limit | Before (R112/R113) | As built (R115) | Source of the value |
| --- | ---: | ---: | --- |
| Unique bodies, curves, guides | 10,000 | 80,000 | R114 unique family |
| Unique vertices, faces, wires | 300,000 | 2,400,000 | R114: frame p95 18.5 ms, move 35 ms |
| Unique edges | 900,000 | 7,200,000 | R114 |
| Materialized placements | 20,000 | 80,000 | R114: frame p95 127 ms, move 345 ms (slow, works) |
| Materialized bodies, curves, guides | 40,000 | 320,000 | brief |
| Materialized vertices, faces, wires | 600,000 | 4,800,000 | R114 dense family: 4.75 million placed faces |
| Materialized edges | 900,000 | 7,200,000 | brief |
| One definition: vertices, faces, wires | 200,000 | 800,000 | no larger definition measured; R117 (merged) sets the definition count, not the size |
| One definition: edges | 600,000 | 2,400,000 | brief |
| One body: faces or vertices | 100,000 | 800,000 | R114 frame p95 2.5 ms; a move is slow |
| One body: edges | 300,000 | 2,400,000 | brief |
| Shading-normal corners per body (and diagnostics) | 2,000,000 | 16,000,000 | brief |
| Component definitions | 1,024 | 4,096 (set by R117, merged on main at fa41f744) | the count is R117's; R115 does not change it |
| Native file | 256 MiB | 2 GiB | brief |
| Model chunk | 96 MiB | 768 MiB | R114 wrote and read 258 MB; 226 B per face (D-099) |
| Raw JSON | 128 MiB | 1 GiB | brief (at least the model chunk) |
| Manifest | 4 MiB | 32 MiB | brief |
| One asset | 16 MiB | 64 MiB | a 64 MiB PNG holds an 8192 by 8192 image |
| Assets in total | 64 MiB | 512 MiB | R114 textures family |
| Texture dimension | 4096 | 8192 | the largest image a 64 MiB asset holds |
| Undo budget | 192 MiB | 4 GiB | D-104: refused definition edits inside the limits |
| Automation retained bytes (staging, transactions) | 128 MiB | 1 GiB | brief |
| Read-snapshot budget | 512 MiB | 4 GiB | at-limit export: see the results |
| GLB: visible faces | 600,000 | 4,800,000 | brief (each GLB bound times 8) |
| GLB: binary, JSON, manifest, references | 256 MiB, 16 MiB, 192 MiB, 2,000,000 | 2 GiB, 128 MiB, 1.5 GiB, 16,000,000 | brief |
| Render snapshot bodies and references | 20,000; 2,000,000 | 160,000; 16,000,000 | brief |
| Format byte bounds: exports (OBJ, STL, DXF, IFC) | 256 MiB | 2 GiB | brief (times 8) |
| Format byte bounds: OBJ, STL imports | 128 MiB | 1 GiB | brief (times 8) |
| Format byte bounds: DXF import | 256 MiB | 2 GiB | brief (times 8) |
| STEP output | 256 MiB | 512 MiB | brief: try 512 MiB inside the helper's memory limit |
| STEP triangles per element, in total, vertices per element | 100,000; 1,000,000; 300,000 | 1,600,000; 10,000,000; 2,400,000 | matches the IFC package bounds |
| Library bundle (catalog, template) | 130 MiB | 1040 MiB | a definition of 800,000 faces is about 180 MB |
| Terrain triangles | 250,000 | 250,000 (unchanged) | not measured |

Values changed by the review (2026-11-08; see its section):

| Limit | Before | As built | Why |
| --- | ---: | ---: | --- |
| glTF import parse budgets: file | 1 GiB | 2 GiB | the export's binary bound |
| glTF import: JSON | 16 MiB | 128 MiB | the export's JSON bound |
| glTF import: JSON values | 1,000,000 | the JSON bound in bytes (134,217,728) | each value takes at least a byte |
| glTF import: accessor count, primitive elements | 8,000,000 | 43,200,000 | 3 × 14,400,000 corners (both sides) |
| glTF import: accessor elements | 16,000,000 | 129,600,000 | three accessors per corner |
| glTF import: nodes, meshes | 10,000 | 400,000 | materialized bodies plus placements |
| glTF import: primitives | 10,000 | 14,400,000 | one triangle at least per primitive |
| glTF import: materials, images, accessors, views | 1,024; 1,024; 65,536; 65,536 | 14,400,000; 14,400,000; 43,200,000; 43,200,000 | the export's own bounds (Budget bounds the memory) |
| IFC import: node bound | 40,000 | 800,000 | the package's bound (`IfcPackageLimits::nodes`) |
| IFC helper: triangles per element, in all | 1,600,000; 10,000,000 | the same, passed as flags | the package's bounds (were literals) |
| Library and catalog scan read budget | 256 MiB | 1,040 MiB (one bundle) | a 300 MiB template was refused by the budget |
| GLB binary bound (and the whole file) | 2 GiB | 2 GiB less one byte (INT_MAX) | the writer's int offsets must stay positive; it is the same bound at scale 1 apart from that byte |
| GLB binary, JSON and manifest bounds at a benchmark scale | 2 GiB × scale, … | capped at INT_MAX | the writer's int offsets and 32-bit chunk lengths |
| SKP helper address space | 4 GiB | 8 GiB | a 2 GiB input and 2 GiB package at once |
| SKP package total | 2 GiB (message: 512 MiB) | 2 GiB, checked before assembly | the message was wrong, and the copy was built first |
| SKP texture dimension | 4,096 | 8,192 | the product texture limit |
| Blender worker texture dimension | 4,096 | 8,192 | the GLB export embeds textures at full size |
| OBJ export manifest | 16 MiB | 128 MiB | a format byte bound, eight times R113 |

The format count limits (OBJ, STL, glTF, DXF) take the document limits through `checkFormatCount`,
`materializedCornerLimit` and `uniqueCornerLimit`, so they follow the table without a separate value.

Each limit that R114 scaled still multiplies by `DocumentLimits::scale`. The scale is 1 in every product build,
and the benchmark-only build defines it (R114). Every `static_assert` in `document_limits.hpp` holds.

## Sweep: every other literal that would refuse a model inside the new limits

The sweep ran `git grep -nE '100000|200000|300000|600000|900000|2000000|\* 1024 \* 1024'` over `src/`, and
a second grep for the digit-separator forms (`100'000` and similar), which the first missed (it found the STEP
export's own caps). Every hit that bounds model data was changed, or is listed as not changed with the reason.

Changed (a constant or a computed message):

- Core and document: `document_limits.hpp`, `model.hpp` (undo and read snapshot), `native_limits.hpp`,
  `asset_records.hpp` and `.cpp` (the asset message is computed), `texture_image.hpp` and `.cpp` (the message is
  computed), `surface.hpp`, `topology.hpp`, `diagnostics.hpp`, `shading_normals.cpp` (computed message),
  `render_snapshot.hpp` and `.cpp`, `automation/commands.cpp` (the byte fields are 64-bit; `int` overflowed past
  2 GiB), `automation/staging.*`, `automation/transactions.*`, `automation/transaction_catalog.cpp` (retained
  bytes; the outcome budget is 64-bit too, `outcomeStoreBytes` is 2 × `containerBytes` + 16 MiB).
- GLB and Blender: `integrations/glb_export.hpp` and `.cpp` (messages computed), `integrations/blender_worker.py`
  (named constants that follow the GLB bounds; the corner bound is 3 × 2 × 7,200,000), `integrations/blender_job.cpp`.
- Formats: `io/obj_import.cpp`, `io/obj_source.cpp`, `io/obj_export.cpp`, `io/stl_import.cpp`, `io/stl_source.cpp`,
  `io/stl_export.cpp`, `io/dxf_source.hpp`, `io/dxf_export.cpp`, `io/dxf_import.cpp`, `io/gltf_package.cpp`
  (file, image, image total, URI and element bounds; the image bounds are the asset bounds), `io/gltf_import.cpp`,
  `io/format_limits.hpp` (`mibBound`, used by messages).
- STEP: `io/step_export.hpp` (`stepOutputLimit` = 512 MiB), `io/step_export.cpp` (output, manifest, mesh chunk,
  nodes, meshes, triangles, vertices), `io/step_worker.cpp` and `.hpp` (input, helper output limit, triangles,
  entities; the messages are computed), `app/step_export_dialog.cpp` and `.hpp` (the note prints the limit; its
  threshold is 200,000 triangles, from 100,000).
- IFC helper (not built here, see Not run): `io/ifc_export.cpp`, `io/ifc_package.hpp`, `io/ifc_worker.cpp` and
  `.hpp`, `io/ifc_import.cpp` (per-body check uses `Surface::bodyLimit`), `src/ifc/export.cpp`,
  `src/ifc/export_package.cpp` and `.hpp`, `src/ifc/step_export_package.hpp`, `src/ifc/import.cpp` and `.hpp`,
  `src/ifc/package.cpp`, `src/ifc/step_import.cpp` and `.hpp`, `src/ifc/step_probe.cpp`.
- `.skp`: `io/skp_worker.cpp` and `.hpp` (`skpWorkerFileLimit`, 2 GiB; the messages are computed), `io/skp_package.cpp`
  (messages computed), `io/skp_package.hpp`, `skp/package.cpp`, `skp/package.hpp`, `skp/import.cpp` and `.hpp`,
  `skp/worker_main.cpp`, `app/ifc_sheets.cpp` (the message prints `skpWorkerFileLimit`).
- Library bundles and catalogs: `io/library_bundle.hpp` (1040 MiB), `io/library_bundle.cpp`, `io/catalog_io.cpp`
  (messages computed), `app/catalog_dialog.cpp` (the message prints the limit).
- Application messages: `app/style_dialogs.cpp` (the texture limit comes from `AssetPayload::limit`).
- Reads: a bounded file read reserved the whole bound (`QIODevice::read(limit + 1)` allocates `limit + 1`), which
  with the new bounds reserved 1.5 to 2 GiB for a small file, and one test failed under the address-space cap.
  `io/native_format.cpp`, `io/save.cpp`, `io/obj_import.cpp`, `io/step_export.cpp`, `io/dxf_import.cpp`,
  `io/stl_import.cpp`, `io/gltf_package.cpp`, `integrations/blender_job.cpp`, `integrations/render_store.cpp` now read
  `min(file size, limit) + 1`.

Not changed, with the reason:

- `Terrain` triangle and terrain edit limits (250,000 triangles, 4,096 edits, 8 MiB edit data), and
  `terrainHistoryLimit` (512 MiB): a separate class the brief leaves unchanged, not measured.
- `componentDefinitionCountLimit`: 4,096 from R117 (merged). R115 does not change it.
- The viewport's decoded texture budget (128 MiB) and its image count: R118. At the new texture size one 8192 image
  (256 MiB decoded) does not fit the budget, so such a texture shows its placeholder until R118.
- Push/pull: the 1,000-face context limit and the coplanar check (D-106) are R117's (merged).
- The edit path's work budgets (`1000000` in `model.cpp`, `geometry/*.cpp`, `io/dxf_import.cpp`, and the
  intersection and solid budgets): these bound work per operation, not a model's size. They were not reached by
  the cases that ran (see the results); a refusal in an at-limit case would be recorded there.
- JSON value and parse budgets (`gltf_package.cpp` 1,000,000 values, the IFC JSON 8,000,000 values): parse bounds,
  not sizes of geometry.
- The MTL file bounds (4 MiB, 100,000 lines) and the `.skp` package's definition count (100,000): R113 kept them.
- Reference images and orthophotos (16 MiB stored, 128 MiB GeoTIFF source): not model data, unchanged.
- Presentation, animation, video and render outputs (512 MiB video, 64 MiB frames, 2 GiB output): not model data.
- The automation and transaction budgets for requests, responses and recipes (1 MiB to 256 MiB): not model data.

## Review findings (2026-11-08)

A review of this pull request found nine items. Each was checked against the code, and each is confirmed. Finding 6
named a set of files; the sweep below covers more messages in the same files and in others that print a bound R115
changed.

| # | Finding | Outcome | What was done |
| --- | --- | --- | --- |
| 1 | The Blender worker refuses textures above 4096 | fixed | `src/integrations/blender_worker.py`: `TEXTURE_DIMENSION_LIMIT = 8192` (the product texture limit, `TextureImage::dimensionLimit`). The render-setting size check (4096) is a render output, not a texture, and is unchanged. Not run here: the laptop has no Blender. |
| 2 | IFC import stops at 40,000 elements | fixed | `ImportOptions::maxNodes` is 800,000, the package's `IfcPackageLimits::nodes`. The app passes `--max-nodes`, `--max-mesh-triangles` and `--max-total-triangles` from `IfcPackageLimits` (it passed none before). The helper is not built on this laptop (`SKETCHYUP_BUILD_IFC` is off; it needs IfcOpenShell), so its compile is for CI. |
| 3 | Library bundles above 256 MiB are saved but not listed | fixed | The library scan (`io/library_catalog.cpp`) and the catalog scan (`io/catalog_io.cpp`) read at most one bundle at the bundle limit, 1,040 MiB. A folder of two bundles of 600 MiB lists one, and says so. The owner can raise the budget. |
| 4 | Reads that reserve the whole bound up front | fixed | `io/recovery.cpp`, `io/ifc_export.cpp`, `io/library_bundle.cpp`, `io/library_catalog.cpp` (two reads), `io/assets.cpp`, `app/reference_images_panel.cpp`. The sweep found two more with R115 bounds: `automation/session.cpp` (a model file, 2 GiB) and `io/outcome_store.cpp` (a checkpoint, about 2.7 GiB). Each read is now `min(file size, bound) + 1`. Other bounded reads keep their R113 bounds (see the sweep list). |
| 5 | glTF round trip at the unique limit | fixed | The import's parse budgets follow the export's own bounds (the table above): a GLB the export writes for a model inside the unique limit imports, double-sided faces included. Checked by counts in `tests/gltf_package_tests.cpp` (`gltfParseBudgets`). A file of that size is not built in CI. The unique vertex bound is a parameter of `importGltf`, and the 300,001-vertex file is refused at 300,000 in CI. |
| 6 | Messages that print old numbers | fixed | Each named file now prints its constant (`mibOf`, `std::to_string(... / (1024 * 1024))`, `groupedCount`). The sweep list has every message changed. |
| 7 | Refusal tests turned into "accepted" checks | restored | Each refusal has a CI test at its boundary by a route that builds no large model: see "Tests restored" below. |
| 8 | The benchmark build's GLB overflows | fixed | `glbBinaryByteLimit`, the JSON bound and the manifest bound stop at `INT_MAX` at any benchmark scale. At scale 1 the binary bound is 2 GiB less one byte, the JSON and manifest bounds are unchanged. The writer's int offsets and 32-bit chunk lengths cannot wrap now. No CI test builds at scale 8; the cap is a `min` in the code, and the messages round up to MiB. |
| 9 | The SKP helper may not reach its 2 GiB input or package under 4 GiB | fixed (with a reason) | The helper's address space is 8 GiB (the app's `kSkpAddressSpaceBytes` too), and the package total (2 GiB) is checked before the package is assembled, not after the copy reached 4 GiB. The copy is still made, so the peak is input + chunks + package + string, about 6 GiB before the parser's own memory: 8 GiB leaves room for it. Not measured: a 2 GiB `.skp` cannot be made here, and the caps forbid a test that size. A model inside the R115 limits is estimated to be far below 2 GiB; that estimate is not measured. |

Also fixed while checking finding 9: `src/skp/package.cpp` passed two `json.str()` temporaries to one `insert` (iterators
from different objects, which is undefined behaviour). It now uses one string.

### Tests restored

Each test below checks a refusal at its boundary, by the route named. Product values are the default; a test passes a
bound.

| Refusal | Test | Route |
| --- | --- | --- |
| IFC helper: element triangles (1,600,000) and total (10,000,000) | `tests/ifc_import_worker_tests.py` | `--max-mesh-triangles` and `--max-total-triangles` on 162- and 324-triangle grids: at the bound imports, one under it refuses |
| IFC conversion: unique bodies | `tests/ifc_convert_tests.cpp` | `IfcImportOptions::unique`: 10,002 bodies with the bound at 10,001 refuse the last by name |
| IFC conversion: a body's faces (`bodyLimit`) | `tests/ifc_convert_tests.cpp` | `IfcImportOptions::bodyLimit` = 100,000 refuses a 100,001-face body |
| IFC conversion: unique faces (`documentLimit:faces`) | `tests/ifc_convert_tests.cpp` | `IfcImportOptions::unique.faces` = 19,600 with three bodies of 9,800 faces |
| Render snapshot body bound (160,000) | `tests/glb_export_tests.cpp` | `requireSnapshotBodies(count, limit)`: one over and exact |
| GLB manifest bound (1.5 GiB) | `tests/glb_export_tests.cpp` | `writeGlbExport(..., manifestLimit)`: one byte over refused by name, exact written |
| GLB decoded texture budget | `tests/glb_export_tests.cpp` | `exportGlb(..., decodedTextureBudget)`: a 256-byte decode, budget 255 refused, 256 exact |
| glTF decoded-image budget (2 GiB) | `tests/gltf_import_tests.cpp` | `importGltf(package, budget)`: one byte under the first 4096-square image refused |
| glTF unique vertices | `tests/gltf_import_tests.cpp` | `importGltf(package, budget, uniqueBounds)`: 300,001 vertices refused at 300,000 |
| STL input bound (1 GiB) | `tests/stl_source_tests.cpp` | `parseStl(bytes, options, inputLimit)`: one byte under the file refused, its size accepted |
| SKP body split (800,000 faces) | `tests/skp_mapping_tests.cpp` | `SkpImportOptions::bodyLimit` = 100,000, passed to `BodyBudget::limit`: the 150,152-face bucket splits into two bodies, as before R115 |
| glTF parse budgets (finding 5) | `tests/gltf_package_tests.cpp` | `gltfParseBudgets()`: counts, not a file |

No refusal case was added to `tests/limits_r115_acceptance.cpp`: each one above has a cheap route.

### Tests changed for cost

- `glb_export_tests`: the 20,001-body capture is no longer built (`requireSnapshotBodies` checks the bound); the
  19,998-body capture still is. The decoded-texture test builds one 8 by 8 image.
- `gltf_import_tests`: the vertex refusal imports the same 300,001-vertex file once (the old test did too), and the
  decoded-image refusal decodes one 4096-square image before it refuses (the five-image load is unchanged).
- `stl_source_tests`: the over-count fixture is unchanged from R115 (2,400,001 facets, 120 MB of zeros, rejected before
  any facet is read). The byte-bound test adds two parse calls on the existing ASCII fixture.
- `skp_mapping_tests`: the body split runs as before (150,152 faces, two bodies), with the bound lowered.

### Sweep additions (2026-11-08)

- Messages printing a bound, now computed from its constant: `io/native_format.cpp`, `io/container.cpp` (manifest and
  raw JSON), `io/document_io.cpp` (file and model bounds), `io/library_bundle.cpp`, `io/dxf_import.cpp`,
  `io/obj_export.cpp` (text and manifest), `io/obj_import.cpp` (images), `io/obj_source.cpp` (text, 1 GiB, printed 64),
  `io/step_export.cpp` (output 512 MiB, printed 256; manifest 256 MiB), `io/ifc_worker.cpp` (file 1 GiB, printed 128),
  `io/ifc_export.cpp` (package bounds 1 GiB and 256 MiB, printed 128 and 32; output), `ifc/export.cpp` (output),
  `ifc/package.cpp` (mesh and manifest bounds, printed 128 and 32), `ifc/step_import.cpp` (input 1 GiB, printed 128),
  `io/ifc_package.cpp` (manifest, mesh and triangles), `io/skp_package.cpp` (manifest), `io/assets.cpp` (asset bytes,
  printed 16), `io/image_refusal.cpp` (stored size 64 MiB, printed 16; image side 8,192, printed 4,096),
  `app/reference_images_panel.cpp` (16 and 4,096), `app/ifc_sheets.cpp` (1,600,000 triangles in one body, printed
  1,000,000), `automation/style_commands.cpp` (style file bound, printed 16), and `io/catalog_io.cpp`: the asset size message
  printed 16 MiB (the bound is 64 MiB), the asset total printed 64 MiB (512 MiB), and the payload messages printed 256 MiB
  (2 GiB). The asset count (256) is unchanged and is now computed too.
- `src/skp/worker_main.cpp`: the help text (8 GiB address space, 8 GiB output) and the header comment.
- `src/skp/package.cpp`: "512 MiB total limit" now prints 2,048 MiB, the bound the code checks.

Checked, not changed (bounds R115 did not change, or work budgets rather than model size):

- Bounds stated in messages that match their constants: `io/obj_import.cpp` (MTL libraries 16 MiB, MTL file 4 MiB),
  `io/obj_material.cpp` (4 MiB, 64 KiB lines), `io/geotiff.cpp` (8 MiB values), `io/orthophoto_import.cpp` (128 MiB
  GeoTIFF, 4,096-pixel georeferenced image), `core/reference_image.hpp` (`imageGeoPixelLimit` 4,096, a georeferenced image),
  `app/window_terrain.cpp` and `automation/main.cpp` (64 MiB terrain source), `io/terrain_source.cpp` (64 MiB point file),
  `core/terrain*` (8 MiB edit list, 250,000 vertices), `integrations/render_environment.cpp` (40 MiB HDR), the automation
  request and response bounds (16 KiB to 256 MiB), `io/formline.cpp` (32 MiB), `io/library_catalog.cpp` (16 MiB thumbnail
  cache), `io/style_bundle.cpp` (style scan 256 MiB; one style file fits it), `integrations/animation_export.cpp`
  (2 GiB output), `integrations/presentation_batch.cpp` and the report bounds (256 MiB).
- Work budgets, not model size: `core/intersection_edit.cpp` (100,000 candidates), `geometry/measured_view.cpp` and
  `io/measured_drawing.cpp` (20,000 edges, 10,000 body records in a measured drawing), `io/drawing_view.cpp`
  (200,000 fingerprint items), `core/component_parameters.cpp` (evaluation budget), the boolean and intersection
  budgets (4,000,000), `io/hosted_components_io.cpp` (10,000 vertices of a hosted component).
- `io/library_catalog.cpp`, `io/catalog_io.cpp` (the thumbnail and bundle bounds): the bundle bound is 1,040 MiB and
  binds before the catalog's 2 GiB payload bound (see the deferred list).
- `io/ifc_import.cpp` (8 MiB of property text, 32 KiB a property) and `io/geotiff.cpp` (1,000,000 posts): unchanged.

### Found by the at-limit run on the GPU PC (2026-11-08)

The first pass of the acceptance on the GPU PC (the binary built before the fixes below) ran the cases one after another.
Eight passed: `unique`, `materialized`, `one-body`, `chunk`, `textures`, `export-glb`, `export-obj` and `export-stl` (the
results are in the acceptance table). Four did not, and each was a real defect or a fixture error:

| Case | First result | Cause | Fix |
| --- | --- | --- | --- |
| `import-obj` | refused: "too many object/group/material states" at line 3,274,721 | `obj_source.cpp` capped the states at a literal 10,000 (an R110-era cap that the sweep did not match). A unique-limit model has 12,000 bodies. | the cap is the unique face count, through `checkFormatCount` (the message names the quantity and the limit). |
| `import-gltf` | refused: "vertices would be 2,400,001 of 2,400,000" (then, after the weld, "Invalid component definition allocator or count": one definition per mesh, see Deferred) | The export writes every triangle corner as its own accessor entry (no indices), and the importer counted each corner as a vertex (`definedGeometry(1, 0, 0)` per glTF index). A GLB the export writes for a unique-limit model could not import, which breaks finding 5's rule. | The importer welds a body's corners by position (`Piece::welded`). Covered by `gltf_import_tests` (the Y-up test now looks the vertex up by position, not by glTF index). |
| `import-stl` | refused: "vertices would be 2,400,001 of 2,400,000" | The STL and GLB exporters took their triangles from `Surface::triangulate`, which returns grid corners that differ by up to 1.6e-8 m between faces (the smoke file has 48,240 distinct corner positions for 12,240 model vertices, and the ring vertices sit at z = -1.6e-8). The importer's weld could not join them. | The STL, GLB and OBJ exporters use `Surface::triangulateExact` (the model's own vertex positions). |
| `export-dxf` | the document load refused "vertices would be 2,404,134 of 2,400,000" | The acceptance program built six separate flat grids: over the unique limit (vertices and faces), and only the materialized limit was in the comment. | One flat grid of 798,848 faces, placed six times (4,793,088 placed faces, the materialized family). |

Also found by reading (the at-limit run did not reach it): `io/scenes_io.cpp` decoded a scene's body visibility flags with a
bound of 10,000, while the record bounds them by the materialized bodies (320,000). A scene of more than 10,000 bodies
could be saved and not opened. The decoder now uses the record's bound. No CI test covers it yet.

### Deferred (found in the review and the at-limit run, not changed)

- **glTF import, one definition per mesh (found by the at-limit run, 2026-11-08).** The importer makes a component
  definition for every glTF mesh and an instance for every node, so a GLB of 12,000 meshes (a unique-limit model of 12,000
  bodies, which the app's own export writes) is refused: "Invalid component definition allocator or count", the count
  limit (1,024; R117 raises it to 4,096). The fix is for the importer to import a mesh that one node uses as plain
  bodies, not definitions. That changes the imported structure, and the tests pin it (`Shared mesh instances retain
  nested hierarchy`), so it is the owner's decision. Until then `import-gltf` does not pass.
- A catalog file is bounded by the bundle limit (1,040 MiB), so its payload bound (`CatalogLimits::payloadBytes`, 2 GiB)
  is never reached. One largest native container is about 1.3 GiB (16 bytes + 32 MiB manifest + 768 MiB model + 512 MiB
  assets + the terrain mesh chunk), so one such container does not fit a catalog or a template library bundle. The R110
  rule that "a catalog holds one largest container" no longer holds at R115 values. This is a decision for the owner: raise
  the bundle bound, or keep it and say so.
- A template or component library bundle with imported UVs at the unique limit can exceed 1,040 MiB (about 528 bytes a
  textured face, 2,400,000 faces: about 1.2 GB), and is refused by name at save (`Template bundle exceeds 1,040 MiB`).
  Models without UVs (about 226 bytes a face) fit.
- The IFC helper and the SKP helper's 2 GiB input were not measured at their bounds on this laptop.
- The measured-drawing and hosted-component budgets above are not raised (not model size).

## CI test times

Local, serial (`ctest -j1`), the `build/ci-fast` configuration (Debug, `-g0`, shared libraries, default linker),
`ulimit -v 8000000`. The baseline is origin/main `0038456f`.

| Test | Before (s) | After (s) | What changed |
| --- | ---: | ---: | --- |
| document_limits | 152.27 | 46.01 | boundaries by counts; the at-limit build is gone; `publicPlacements(10000)` kept |
| document_limits_io | 29.46 | 0.21 | counts and small fixtures; the 1 GiB raw JSON buffer is not built |
| incremental_validation | 66.94 | 54.05 | boundaries by counts; full-mode randomized steps 220 to 120 |
| asset_io | 0.15 | 1.37 | the asset tests now build 64 MiB assets (the new per-asset limit) |
| history | 0.98 | 0.87 | unchanged code |
| component_placement | 0.01 | 0.02 | unchanged code |
| unique_limits_acceptance | 215.48 | not in CI | manual at-limit acceptance (GPU PC) |
| snapshot_glb_limits_acceptance | 289.18 | not in CI | manual at-limit acceptance (GPU PC) |
| format_limits_acceptance | 255.50 | not in CI | manual at-limit acceptance (GPU PC) |

Review (2026-11-08), serial (`ctest -j1`), after the review fixes, on the same laptop. The "R115" column is the table
above for the same test (a different load, so compare with care):

| Test | R115 (s) | After the review (s) | What changed in the review |
| --- | ---: | ---: | --- |
| document_limits | 46.01 | 29.96 | no change in the test |
| incremental_validation | 54.05 | 44.25 | no change in the test |
| ifc_convert | (not timed) | 27.33 | restored refusals at lowered bounds (three small conversions added) |
| glb_export | (not timed) | 10.62 | the 20,001-body capture is no longer built; the manifest, body and decoded-texture bounds are checked by passing them in |
| skp_mapping | (not timed) | 7.00 | body split at a lowered bound, as before R115 |
| gltf_import | (not timed) | 2.65 | the decoded refusal decodes one 4096-square image more; the vertex refusal is one import |
| asset_io | 1.37 | 1.06 | no change in the test |
| document_limits_io | 0.21 | 0.16 | no change in the test |
| history | 0.87 | 0.58 | no change in the test |
| stl_source | (not timed) | 0.06 | the input bound is a parameter (two parse calls added) |
| gltf_package | (not timed) | 0.02 | the parse budgets are checked by counts |

The three acceptance binaries are built with `EXCLUDE_FROM_ALL` and have no CTest entry. The path map
(`.github/ci-path-map.json`) drops them from `slow-core` and the test seconds, and re-times the three tests that
stay. The nine tests together took about 20 minutes before and take about 1.7 minutes after. No CI test builds more
geometry than it did before. Two tests build a little more memory: `asset_io` (64 MiB assets) takes about one second
more; the CI total rises by about one second.

## Acceptance on the GPU PC

**The at-limit acceptance ran on the GPU PC on 2026-11-08, on the merged code (R115 with R117, `1f061e90`), with the
final binary (`libsketchyup_io.so` `1da882a8…`, `libsketchyup_core.so` `a93d3ebd…`; the executable is `b20edf56…`).** The
GPU PC was idle at 04:03 (no Blender job; GPU 3 to 10% busy), and the cases ran one after another through
`run-case.sh`, which takes `flock -w 3600 ~/sketchyup-verification/gpu.lock` and then `systemd-run --user --wait` with
`MemoryMax=18G`, `MemorySwapMax=0`, `RuntimeMaxSec=1800` and no core dumps. The results are in
`~/sketchyup-verification/r115-20261107/runs` (the final pass) and, for the history of this record, `runs-pass1`,
`runs-pass2` and `runs-pass3` on the GPU PC. Earlier passes ran before the fixes and the merge; their failures are listed
under "Found by the at-limit run" above.

Result: brief cases 1 to 6 passed, except the Blender import of the GLB, which did not run. Case 7 passed for OBJ and STL
and failed for glTF. Case 8 (STEP) did not run.

| Case (brief) | Program case | Result | Measured | Notes |
| --- | --- | --- | --- | --- |
| 1. unique: 2,400,000 faces; save, close, open; move, undo, redo | `unique` | passed | build 22 s; save 7.7 s, 238,803,918 bytes; open 20 s; peak RSS 4.4 GiB | 12,000 bodies of 200 faces; move one body 0.0 s, history 67,695 bytes |
| 2. materialized: 4,800,000 placed faces from an 800,000-face definition; move a placement; edit the definition (D-104); undo, redo | `materialized` | passed | placed build 102 s; save 2.3 s, 98,203,894 bytes; open 42.5 s; definition edit 100 s | the history charge of the definition edit is **1,792,016,139 bytes (1.67 GiB)**, inside the 4 GiB budget (it was 3.34 GiB before R117's charge); RSS growth **2,326 MiB**; peak RSS 7.5 GiB; undo and redo 0.1 to 0.2 s |
| 3. one body of 800,000 faces: move, undo | `one-body` | passed | move 10.9 s | history 256,003,695 bytes; undo and redo 0 s |
| 4. model chunk near 768 MiB with imported UVs (D-099): save and open | `chunk` | passed | save 23.1 s; open 18.4 s | 595 bodies, 1,190,000 textured faces; model chunk 796,135,248 bytes (759 MiB); peak RSS 7.9 GiB |
| 5. 512 MiB of stored textures, one 64 MiB asset: save and open | `textures` | passed | build 87 s; save 0.5 s, 527,246,051 bytes; open 0.5 s | 33 assets (32 PNGs of 4096 by 4096, one 64 MiB asset); the program states that 31 of 33 would show a viewport placeholder (the viewport count was not measured) |
| 6. exports at the materialized limit: GLB (and Blender import), OBJ, binary STL, DXF | `export-glb`, `export-obj`, `export-stl`, `export-dxf` | GLB, OBJ, STL and DXF passed | GLB export 98 s, manifest 574,980,899 bytes (under 1.5 GiB), GLB 57,603,152 bytes (the six placements share one mesh); OBJ 77 s, 383,405,885 bytes; STL 39 s, 240,000,084 bytes; DXF 59 s, 914,788,283 bytes | **Not run: the Blender import of the GLB with the Blender worker.** The DXF model is the planar family (one flat grid of 798,848 faces placed six times) |
| 7. imports at the unique limit: OBJ, STL, glTF (D-103) | `import-obj`, `import-stl`, `import-gltf` | OBJ passed; STL passed; **glTF failed** | OBJ import 112 s, peak RSS 8.8 GiB, faces 2,400,000, 24,001 bodies; STL import 189 s, facets 2,400,000, 3 bodies | **glTF:** "Invalid component definition allocator or count" (after 54 s). The importer makes one component definition per glTF mesh, and the export writes one mesh per body, so 12,000 bodies are 12,000 definitions, over the count of 4,096 (R117). The vertex refusal that came first is fixed (corners are welded by position). |
| 8. STEP export at 512 MiB (D-103) | none | not run | none | The STEP helper (`sketchyup-ifc-worker`) is not built on the GPU PC, and building it was not attempted (the vendored IfcOpenShell fetch is a network step, and the brief forbids installs). D-103 stays open. |

Notes: the OBJ import reports 24,001 bodies for 12,000 exported bipyramids (the writer and reader count objects and
groups, two per body, plus one); the limit (80,000 bodies) holds. The GLB export's manifest is 575 MB because each of the
4,800,000 visible faces has a manifest record; the GLB itself is small because the six placements share one mesh.

Expected results, stated so that they can be checked: the GLB binary at 4,800,000 faces is about 0.7 to 1 GB when no mesh is
shared, and its manifest about 1.4 GB (about 306 bytes a face); the chunk case measured 759 MiB at 595 bodies.

The laptop smoke run (`--smoke`, every case, small sizes) passed before the review, and again on the final binary (2026-11-08):
twelve cases, every step ok. Smoke sizes prove the code paths, not the limits, and no at-limit size ran on the laptop.

### GPU PC state

- 2026-11-08 02:53 (during review): a Blender job not started by this worker (PID 2605 on `sketchyup-gpu`) held the GPU at
  97 to 99% busy; the acceptance was not run and the job was not stopped.
- 2026-11-08 04:03 (before the acceptance): no Blender process; GPU 3 to 10% busy; load 0.3. The acceptance ran in
  passes (see above); the last pass is on the merged code, and it started at 07:04 (GPU time).
- Each case's `runs/<case>/state.txt` records `uptime` and `gpu_busy_percent` before and after.

## Records and checks

- Local build: `build/ci-fast` (all default targets, 314 steps, and the harness target), exit 0. The review fixes
  changed `core/document_limits.hpp` (an overload), which rebuilt the whole project: the full build after the fixes was
  912 steps and exited 0, with incremental builds after it (0 errors). `limits_r115_acceptance` is built with
  `--target` (it is `EXCLUDE_FROM_ALL`), so `cmake --build` alone does not rebuild it.
- After the merge with R117 (`1f061e90`), the full fast tier (283 registered tests, `ctest -j2`, under `ulimit -v 8000000`) passed
  100%, in 445 s. The six `*_real` tests report skipped, as before.
- Review check, local (final): the fast-tier tests that the review's changes can affect (58 registered tests: the limit,
  format, document, scene, persistence, texture, reference-image, recovery and SKP tests) under `ulimit -v 8000000`,
  `ctest -j2`: all 58 passed. Six of them are the `*_real` tests, which need Blender, pdftoppm or the CI environment
  variables, and they report skipped. Earlier runs found and fixed two failures: the SKP body split (the product
  bound in `BodyBudget`) and the glTF vertex test (it looked up a glTF index).
- Review check, local: `limits_r115_acceptance --smoke` (every case, small sizes) passes all twelve cases on the laptop.
  Smoke sizes prove the code paths, not the limits. No at-limit case ran here.
- The IFC helper (`sketchyup-ifc-worker`, `SKETCHYUP_BUILD_IFC=ON`) and the Blender worker are not built or run on this
  laptop. The `src/ifc/` changes and the Python changes were checked by reading and by type (the CI image builds the
  helper). `tests/ifc_import_worker_tests.py` (the IFC helper's triangle bounds) therefore did not run here.
- Fast-tier CTest: every registered test was run after the last source edit that affects it; the skipped tests
  are the real-tool tests that need Blender, pdftoppm and similar, which CI supplies (`SKETCHYUP_*_TEST`).
- `sketchyup-cli --capabilities`: the `limits` block reports the new values. The byte fields are 64-bit.
  Before: 1,658,514 bytes; after: 1,658,532 bytes (the new values are longer).
- Schemas regenerated with `scripts/generate-api-schemas.py` and `scripts/generate-tool-reference.py`
  (`docs/api/transactions-v1.json`, `headless-session-v1.json`, `mcp-2026-07-28.json`, `native-format-v1.json`,
  `tool-reference-v1.json`); the diffs change only limit values.
- `tests/assistant-tool-surface-baseline.json`: the tool surface grows by two bytes (the transaction retained
  bytes), so the byte counts and one SHA-256 change.
- `docs/verification/R115-raised-limits.json`: the machine, the binary hashes, the acceptance status and the local test
  times in machine-readable form. It parses.
- `git diff --check`, JSON parsing and the relative links are checked before the pull request.

## Binaries and machine

- Laptop: `build/ci-fast/limits_r115_acceptance`, executable SHA-256 `b20edf56cde32d408645d65b629deea8e35318049c743bd0401cfd424d2e71f7`. It is dynamically linked, so the shared libraries carry the changes. The final libraries (merged code, `1f061e90`): `libsketchyup_io.so` `1da882a8c435577600f638abbdf4318af655acc6f1fe45dcde9954fce1941164`, `libsketchyup_core.so` `a93d3ebd05d0d6b4843917067ca5036c511abadacc53ec7a4bece2c0ee5cc2fa`. Earlier: `f8be4868…` (io, before the merge), the first review build `c6fe485b…`, and the pre-review `33194787…`.
- GPU PC: the final executable and libraries are in `~/sketchyup-verification/r115-20261107/bin` (the merged binary copied 2026-11-08 07:00). The earlier passes are in `runs-pass1`, `runs-pass2` and `runs-pass3`; the final pass is in `runs`.
- GPU PC: `sketchyup-gpu` (CachyOS QEMU guest, 12 vCPUs, 22 GiB RAM, RX 6800 XT, Qt 6.12, X11). Cases run one per
  `systemd-run --user` unit with `MemoryMax=18G`, `MemorySwapMax=0`, `RuntimeMaxSec=1800` and no core dumps, through
  `flock -w 3600 ~/sketchyup-verification/gpu.lock`. The runner is `~/sketchyup-verification/r115-20261107/run-case.sh`.
  Each case records `uptime` and `gpu_busy_percent` before and after (`runs/<case>/state.txt`).
