# R115: raised model limits, measured on real hardware

Date: 2026-11-07. Branch `feat/r115-raise-limits`. The owner approved the raise on 2026-11-07:
"Forget about that limit - it was completely arbitrary." The product limits are the values measured in
[R114](R114-hardware-limits.md), not a memory ceiling. The 3.75 GiB and 8 GiB ceilings of earlier records
only protected the development laptop and do not apply (owner, 2026-11-07).

Status of this record: **the code and the CI changes are complete and tested locally. The at-limit acceptance
on the GPU PC is in the results section below; any case not run there says why.** Models are synthetic.

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
| One definition: vertices, faces, wires | 200,000 | 800,000 | no larger definition measured; R117 may raise it |
| One definition: edges | 600,000 | 2,400,000 | brief |
| One body: faces or vertices | 100,000 | 800,000 | R114 frame p95 2.5 ms; a move is slow |
| One body: edges | 300,000 | 2,400,000 | brief |
| Shading-normal corners per body (and diagnostics) | 2,000,000 | 16,000,000 | brief |
| Component definitions | 1,024 | 1,024 (unchanged) | R117 owns it |
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
- `componentDefinitionCountLimit` (1,024): R117 owns it.
- The viewport's decoded texture budget (128 MiB) and its image count: R118. At the new texture size one 8192 image
  (256 MiB decoded) does not fit the budget, so such a texture shows its placeholder until R118.
- Push/pull: the 1,000-face context limit and the 1,000,000-pair coplanar check (D-106): R117.
- The edit path's work budgets (`1000000` in `model.cpp`, `geometry/*.cpp`, `io/dxf_import.cpp`, and the
  intersection and solid budgets): these bound work per operation, not a model's size. They were not reached by
  the cases that ran (see the results); a refusal in an at-limit case would be recorded there.
- JSON value and parse budgets (`gltf_package.cpp` 1,000,000 values, the IFC JSON 8,000,000 values): parse bounds,
  not sizes of geometry.
- The MTL file bounds (4 MiB, 100,000 lines) and the `.skp` package's definition count (100,000): R113 kept them.
- Reference images and orthophotos (16 MiB stored, 128 MiB GeoTIFF source): not model data, unchanged.
- Presentation, animation, video and render outputs (512 MiB video, 64 MiB frames, 2 GiB output): not model data.
- The automation and transaction budgets for requests, responses and recipes (1 MiB to 256 MiB): not model data.

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

The three acceptance binaries are built with `EXCLUDE_FROM_ALL` and have no CTest entry. The path map
(`.github/ci-path-map.json`) drops them from `slow-core` and the test seconds, and re-times the three tests that
stay. The nine tests together took about 20 minutes before and take about 1.7 minutes after. No CI test builds more
geometry than it did before. Two tests build a little more memory: `asset_io` (64 MiB assets) takes about one second
more; the CI total rises by about one second.

## Acceptance on the GPU PC

Results are in the section that follows the code. A case that did not run says why.

Status at the end of this pull request: **no case has run on the GPU PC.** The owner's Blender job (`blender -b`
on the owner's own project, PID 2605 on `sketchyup-gpu`) was running from 20:08 on 2026-11-07 and held the GPU at
97 to 99% busy at each check through 02:19 on 2026-11-08. The brief says to pause while the owner uses the machine, and
the owner's process was not stopped. The runner and the binary are in place, so the cases run with one command per case:

    ssh sketchyup-gpu 'bash -lc "~/sketchyup-verification/r115-20261107/run-case.sh unique"'

The laptop smoke run (`--smoke`, every case, small sizes) passed on the development laptop: twelve cases, every step
ok. Smoke sizes prove the code paths, not the limits, and no at-limit size ran on the laptop.

| Case (brief) | Program case | GPU PC result | Notes |
| --- | --- | --- | --- |
| 1. unique: 2,400,000 faces; save, close, open; move, undo, redo | `unique` | not run | 12,000 distinct 200-face bipyramids |
| 2. materialized: 4,800,000 placed faces from an 800,000-face definition; move a placement; edit the definition (D-104); undo, redo; history charge and RSS growth | `materialized` | not run | six placements of one definition |
| 3. one body of 800,000 faces: move, undo | `one-body` | not run | |
| 4. model chunk near 768 MiB with imported UVs (D-099): save and open | `chunk` | not run | 595 bodies of 2,000 textured faces, full-precision UVs |
| 5. 512 MiB of stored textures, one 64 MiB asset: save and open | `textures` | not run | 32 PNGs of 4096 by 4096 and one 64 MiB asset; the viewport count is not measured here |
| 6. exports at the materialized limit: GLB (and Blender import), OBJ, binary STL, DXF | `export-glb`, `export-obj`, `export-stl`, `export-dxf` | not run | DXF uses six flat grids (planar) |
| 7. imports at the unique limit: OBJ, STL, glTF (D-103) | `import-obj`, `import-stl`, `import-gltf` | not run | the OBJ import is checked against the 10-minute rule |
| 8. STEP export at 512 MiB (D-103) | none | not run | the STEP helper (`sketchyup-ifc-worker`) is not built on the GPU PC; building it was not attempted |

Expected results, stated so that they can be checked (not measured): the GLB binary at 4,800,000 faces is about 0.7 to
1 GB and its manifest about 1.4 GB (about 306 bytes a face); the chunk case has about 670 bytes a face on the
laptop smoke run, which gives a chunk near 760 MiB at 595 bodies.

The owner's Blender job did not run into any of the laptop's checks; the GPU PC's Arch and sanitized CI jobs are separate.

The Blender import of the GLB (case 6) also needs the worker on the GPU PC; `scripts/verify-blender-glb.py` expects fixture
layouts, so a direct import of the exported `scene.glb` is the plan, not yet run.


## Records and checks

- Local build: `build/ci-fast` (all default targets, 314 steps, and the harness target), exit 0.
- Fast-tier CTest: every registered test was run after the last source edit that affects it; the skipped tests
  are the real-tool tests that need Blender, pdftoppm and similar, which CI supplies (`SKETCHYUP_*_TEST`).
- `sketchyup-cli --capabilities`: the `limits` block reports the new values. The byte fields are 64-bit.
  Before: 1,658,514 bytes; after: 1,658,532 bytes (the new values are longer).
- Schemas regenerated with `scripts/generate-api-schemas.py` and `scripts/generate-tool-reference.py`
  (`docs/api/transactions-v1.json`, `headless-session-v1.json`, `mcp-2026-07-28.json`, `native-format-v1.json`,
  `tool-reference-v1.json`); the diffs change only limit values.
- `tests/assistant-tool-surface-baseline.json`: the tool surface grows by two bytes (the transaction retained
  bytes), so the byte counts and one SHA-256 change.
- `git diff --check`, JSON parsing and the relative links are checked before the pull request.

## Binaries and machine

- Laptop: `build/ci-fast/limits_r115_acceptance` (SHA-256 `33194787c9d488352cc35ebe7c6f0d8230e17a921195b370ec9bee1fb1db7dd8`)
  and its shared libraries, copied to `~/sketchyup-verification/r115-20261107/bin` on the GPU PC.
- GPU PC: `sketchyup-gpu` (CachyOS QEMU guest, 12 vCPUs, 22 GiB RAM, RX 6800 XT, Qt 6.12, X11). Cases run one per
  `systemd-run --user` unit with `MemoryMax=18G`, `MemorySwapMax=0`, `RuntimeMaxSec=1800` and no core dumps, through
  `flock -w 3600 ~/sketchyup-verification/gpu.lock`. The runner is `~/sketchyup-verification/r115-20261107/run-case.sh`.
  Each case records `uptime` and `gpu_busy_percent` before and after (`runs/<case>/state.txt`).
