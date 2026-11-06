# R082.w v4: memory at the R112 limits (measured in software; within the owner's 8 GiB ceiling)

Version four of the [memory calibration](R082w-memory-budget-v3.md), for the R112 limits of
[ADR 0005](../decisions/0005-document-protocol.md#r112-snapshot-and-glb-limits-180): the read-snapshot
budget at 512 MiB (from 256 MiB) and the GLB visible-face cap at 600,000 (from 100,000), with the
manifest bound at 192 MiB (from 128 MiB). Versions one to three stay as they are. Its machine-readable
record is [R082w-memory-budget-v4.json](R082w-memory-budget-v4.json).

**Status: measured in software only. The modeled worst case fits the 8 GiB ceiling that the owner set on
2026-11-06.** Every measured peak is under the 4 GiB job cap, and the snapshot budget and the cap do what R112
says. The modeled worst case with a GLB export on top of a loaded viewport is **3.99 GiB**. That was over the
earlier 3.75 GiB ceiling by about 242 MiB, so the work stopped until the owner raised the ceiling (see
[The owner's decision](#the-owners-decision)). **Hardware evidence stays open:** D-091 is not closed by
this record, and the [ADR 0161 waiver](../decisions/0161-m9-hardware-evidence-waiver.md) still applies.

## What R112 changes in the modeled worst case

R112 adds no resident memory to the model in [v3](R082w-memory-budget-v3.md). The snapshot budget is an
admission check: a read snapshot shares the document's records, so raising the budget changes which
documents are accepted, not what they hold. The GLB cap changes what an export may write. v3 modeled the
viewport, the history and the placements, but not an export, because a 100,000-face cap made a 600,000-face
export impossible. At 600,000 faces an export allocates a manifest string and its tree, and that transient
now stands on top of the worst case.

| Term | v3 (bytes) | v4 (bytes) |
| --- | ---: | ---: |
| v3 total, with the measured excess and the 150 MB hardware delta | 3,989,057,776 (3.72 GiB) | 3,989,057,776 |
| GLB export at 600,000 visible faces, above the loaded model (measured, [below](#export-at-the-cap)) | not modeled | 278 MiB (291,504,128 B) |
| **Total** | 3,989,057,776 (3.72 GiB) | **4,280,561,904 (3.99 GiB; 4,082 MiB)** |
| Owner's modeled ceiling | 4,026,531,840 (3.75 GiB) | 8,589,934,592 (8 GiB) |
| Margin to the ceiling | 37,474,064 B (35.7 MiB) | 4,309,372,688 B (about 4.01 GiB) |
| Margin to the 4 GiB job cap | 305,909,520 B | about 14 MiB |

Without the hardware delta (the software-only sum of v3, 3,839,057,776 B, plus the same 278 MiB) the total
is 3,939 MiB, or 3.85 GiB.

The earlier 3.75 GiB ceiling left 35.7 MiB for an export, with the hardware delta. The measured transient is
about 7.8 times that, which is why the ceiling had to change or the export had to be limited.

**Does an export run on top of a loaded viewport in the desktop app?** Yes, through the render path. The
Blender job (`src/integrations/blender_job.cpp`) calls `exportGlb` and `writeGlbExport` on the document
while the viewport keeps its payload (v3's viewport terms). The desktop build is not compiled here
(`SKETCHYUP_BUILD_DESKTOP=OFF`), so the viewport's own resident memory was not measured. The transient was
measured on the command-line export, which runs the same `exportGlb` and `writeGlbExport` code. The render
path also reads the 175 MiB manifest back to hash it, one step after the write, so its peak is about the
same size as the write's, not added to it.

## How it was measured

- **Image and build.** `sketchyup-ci-pinned:r097a-ifc` (Qt 6.11.2, GCC 16.2.1, CMake 4.4.3, Ninja 1.13.2),
  `CMAKE_BUILD_TYPE=Release`, `SKETCHYUP_BUILD_DESKTOP=OFF`, `SKETCHYUP_BUILD_CLI=ON`,
  `SKETCHYUP_BUILD_IFC=OFF`, `SKETCHYUP_SHARED_LIBS=OFF`, `SKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF`. The build
  ran with `--cpus=2 --memory=4g --memory-swap=4g` and `-j2`. Every measurement ran with
  `--cpus=1 --memory=4g --memory-swap=4g`, so the 4 GiB job cap was the cap under which each peak was taken.
- **Source.** The R112 branch, `feat/r112-snapshot-glb-limits`, from `44c7ab96` (main), with the commit that
  adds these measurements.
- **Harness.** `snapshot_glb_limits_benchmark` (new, `EXCLUDE_FROM_ALL`, core and I/O only) builds the
  synthetic shapes, prints `readSnapshotBytes()` before and after the 64 MiB of assets, writes a container,
  and measures export resident memory before and after the write (`export` mode). The CLI measures catalog
  import, validation, save, reopen and GLB export. Peaks are `ru_maxrss` of the process (the child, for CLI
  runs), in MiB (1 MiB = 1,048,576 bytes). Times are wall clock.
- **Synthetic only.** No owner or peer model was read, and the models are built in the harness.
- **Not Blender.** No Blender ran on this machine. The render path's Blender step was not run.

## The shapes

The published limits are: unique 300,000 vertices, faces and wires, 900,000 edges; materialized 600,000
vertices, faces and wires, 900,000 edges; 200,000 vertices, faces and wires and 600,000 edges for one
definition; a 96 MiB model chunk; 64 MiB of assets in total.

- **`spheres-at-cap`.** Three closed 100,000-face spheres, each placed twice. Unique 300,000 faces, 150,006
  vertices and 450,000 edges; materialized 600,000 faces, 300,012 vertices and 900,000 edges. Every face is
  visible, so the export reaches the GLB cap exactly.
- **`strips-at-limits`.** The largest smooth charge found. Strips have one vertex per face and two edges per
  face, so the 900,000 materialized edges bind before the 600,000 materialized faces. Three strip definitions
  (two of 99,998 triangles, one of 98,004) make unique 300,000 faces and 299,008 vertices. A 2,000-face sphere
  placed 100 times fills the remaining edges: materialized 498,000 faces, 398,206 vertices and 896,003 edges.
  The published 300,000 unique vertices and faces, and 600,000 materialized faces, cannot all be reached with
  smooth edges, because each of those figures needs an edge count the 900,000 limit does not allow.

Both shapes are smooth (every edge soft and smooth, one appearance record each) and carry 64 MiB of assets.

## Measured snapshot charges (`readSnapshotBytes()`)

| Shape and variant | Geometry (bytes) | With 64 MiB of assets (bytes) | With assets (MiB) | Against 512 MiB | Model chunk (MiB, bound 96) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `strips-at-limits`, smooth (the worst smooth case) | 420,352,838 | **487,462,730** | **464.9** | **within**, 47.1 MiB under | 69.5 |
| `spheres-at-cap`, smooth | 389,287,368 | 456,397,260 | 435.3 | within, 76.8 MiB under | 59.8 |
| `strips-at-limits` with every face coloured | 487,384,838 | 554,494,730 | 528.8 | **over**, by 16.8 MiB | 76.1 |
| `strips-at-limits` with textures: all 200,000 materialized sphere faces and 50,000 strip faces | 500,080,838 | 567,190,730 | 540.9 | **over**, by 28.9 MiB | 77.4 |

The textured variant holds 52,000 unique textured faces and 250,000 charged copies of them, so the
measured charge is 79,728,000 bytes over 302,000 copies. That is **264.0 bytes per copy**, the figure
in the model code. A painted face costs **84 bytes per copy** (67,032,000 bytes over 798,000 copies: 300,000
unique and 498,000 materialized). Both are charged to the definition and to each placement, so a component
placed many times multiplies its cost.

The headroom under the smooth worst case is 49,408,182 bytes. That is enough for about 588,000 coloured face
copies or about 187,000 textured face copies, and any mix that adds up to more refuses the export.

**Keep 512 MiB, and say which documents exceed it.** The smooth worst case fits: 464.9 MiB. The
case with textures or colours does not, and it is inside the 96 MiB chunk bound. Those documents open, edit
and save, but their exports refuse by name. This is the record of
[D-100](R089a-defect-triage.md). The budget stays at the 512 MiB the owner approved.

## Export at the cap

Measured on the saved models, one process at a time, with the CLI and with the in-process `export` mode.

| Step | Model | Wall (s) | Peak (MiB) | Notes |
| --- | --- | ---: | ---: | --- |
| Validate (`--validate-native`) | `spheres-at-cap` | 7.4 | 1,231 | valid |
| Save (`--input`, `--output`) | `spheres-at-cap` | 65.8 | 1,453 | output is byte-identical to the input (`cmp`) |
| Reopen (validate the saved file) | `spheres-at-cap` | 7.0 | 1,231 | same validation hash |
| GLB export (`--export-glb`) | `spheres-at-cap` | 22.1 | 1,326 | 600,000 visible triangles; manifest 183,684,206 B (175.2 MiB); `scene.glb` 7,203,224 B; JSON chunk 3,196 B; one mesh shared by three definitions |
| Validate | `strips-at-limits` | 5.1 | 1,427 | valid |
| GLB export | `strips-at-limits` | 11.6 | 1,464 | 498,000 visible triangles; manifest 145.0 MiB; `scene.glb` 14.4 MB |
| GLB export, textured variant | textured `strips-at-limits` | 6.8 | 1,649 | refused: `Render snapshot exceeds the read-snapshot budget`; nothing written |
| Catalog import (`--import-catalog`) | two spheres placed twice (400,000 materialized) plus a one-sphere entry | 201.2 | 2,042 | 500,000 materialized, 300,000 unique; `valid` |

Resident memory of a loaded model, and the export on top of it (in-process, `export` mode):

| Model | Resident after load (MiB) | Peak during load (MiB) | Resident after export (MiB) | Peak, export included (MiB) | Export adds above the resident model |
| --- | ---: | ---: | ---: | ---: | ---: |
| `spheres-at-cap` | 1,047 | 1,229 | 1,070 | 1,326 (CLI) | **278 MiB** |
| `strips-at-limits` | 1,196 | 1,426 | 1,207 | 1,464 (CLI) | **268 MiB** |

The manifest is the largest part of the transient: 175.2 MiB of indented JSON at 600,000 faces, about 306
bytes a face (the bound is 192 MiB). Under R112 the bound is 192 MiB, so the manifest write fits; the cost of
that write is the 278 MiB above.

**Times.** The catalog import (201 s) and the save (66 s) are the slow steps; the GLB export takes 12 to 22 s.

Build and checks with the same image: `snapshot_glb_limits_benchmark` built the `spheres-at-cap` model in
75.5 s and peaked at 1,457 MiB while building (1,709 MiB with the container written); `strips-at-limits`
took 336.9 s and peaked at 1,728 MiB (1,993 MiB with the container written).

## Acceptance and regression runs

- `snapshot_glb_limits_acceptance` (new): 291,994 faces in 29 definitions, charge 285.3 MiB, peak 1,539 MiB,
  78 s in total (build 19 s, import 20 s, save and reopen 9 s, export 5 s). It passes.
- `unique_limits_acceptance` (R110): passes, 56.5 s, peak 1,864 MiB.
- `glb_export_tests`, `document_snapshot_tests`, `library_bundle_tests`, `catalog_import_tests`,
  `catalog_tests`, `stl_export_tests`, `obj_export_tests`, `dxf_export_tests`, `section_export_tests`,
  `texture_export_tests`, `smooth_export_tests`, `document_limits_tests`, `document_limits_io_tests`,
  `assistant_tool_surface_tests`, `staging_tests`, `transaction_dispatch_tests`, `render_store_tests`,
  `render_queue_tests`, `blender_handoff_tests`, `blender_job_tests`, `inspection_session_tests`: all pass.
- `m8_exchange_tests` aborts without a display (`QGuiApplication`) and passes with
  `QT_QPA_PLATFORM=offscreen`. That is the environment, not R112.
- `scripts/generate-tool-reference.py --check`: passes (2 artifacts). `scripts/generate-api-schemas.py`:
  the eight CLI artifacts match. The two desktop artifacts need the desktop binary, which is not built here,
  and their keys do not carry `limits`.
- `assistant_tool_surface_tests` passes. `tests/assistant-tool-surface-baseline.json` and
  `tests/provider-release-corpus-v6.json` are not changed: the capability output carries no tool surface.

## Not measured

- The desktop viewport, its frame times and its resident memory (no desktop build here).
- Hardware (D-091 stays open).
- The Blender render path, and its 175 MiB manifest read-back.
- Paired (two-sided, distinct) faces at 600,000 faces: the 1,000,000-triangle cap would refuse them, as the
  triangle count doubles. Not built.
- A 600,000-face model made of quads: its 2,400,000 face-vertex references exceed the 2,000,000 cap, so such
  an export is refused. Not built.
- Binary size at the triangle cap: about 72 bytes a triangle for positions and normals, so 1,000,000 triangles
  come to about 72 MB, under the 256 MiB binary bound. Estimated, not measured.

## The owner's decision

The export at the cap went over the ceiling of the time, 3.75 GiB, so the work stopped and the owner was asked.
The options offered were 600,000 faces on the command line only, 600,000 everywhere with a higher ceiling, or
cutting the export's memory first. On 2026-11-06 the owner answered: "Raise the memory ceiling to 8GiB".

- **The modeled worst-case ceiling is 8 GiB** (8,589,934,592 bytes), from 3.75 GiB. The worst case with an export
  is 3.99 GiB, about 4.01 GiB under it.
- **The 600,000-face cap applies to every GLB export,** including renders started from the desktop app.
- **The 4 GiB cap on test and measurement jobs is a separate operational limit and is unchanged.** Every measured
  peak is well under it (the largest is 2,255 MiB). The modeled worst case is about 14 MiB under it, so a change
  that adds to that worst case must be re-measured against this record.
- **The higher ceiling raises no model limit.** The published vertex, face, edge, placement and byte limits stay
  as they are.

## Relation to earlier versions

v1, v2 and v3 stay as they are. v3's modeled worst case, with its hardware delta, is 3.72 GiB. v4 adds one term,
the GLB export transient at 600,000 faces, which v3 did not model. The v3 term for the undo budget
(192 MiB), the placements and the viewport is unchanged.
