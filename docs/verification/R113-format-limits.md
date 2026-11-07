# R113: the file formats at the published model limits

Evidence for [R113](../PR_ROADMAP.md#r113) and [D-102](R089a-defect-triage.md), measured on the R113
branch (`feat/r113-format-caps`) with synthetic models only. The owner's models were not used, and
no Blender runs here. The numbers are single runs on a shared machine, so the times are rough.

## What was measured

| Case | Result | Notes |
|---|---|---|
| OBJ import, 6,050 / 24,200 / 96,800 faces (grid) | before 0.22 / 1.52 / 12.16 s; after 0.47 / 1.16 / 4.44 s | Cause: body encoding inserted each face record into a `QJsonObject` in id order, which is quadratic. Fixed by sorting the keys first (`src/io/document_io.cpp`, `keyedObject`). The profile (gdb sampling) put every sample in `encodeBodies`. |
| OBJ import, 150,152 faces | before refused (`element expansion exceeds limits`); after 2 bodies | `bodySplits` lists it |
| STL import, 150,152 binary facets | before refused; after 2 bodies | |
| glTF import, one mesh of 49,928 / 50,562 / 150,152 triangles | before 49,928 accepted, 50,562 refused; after all accepted | the mesh was charged twice before |
| `.skp`, 1,100 definitions (synthetic, copies of a fixture's definition) | before `Invalid component definition allocator or count`; after 1,024 imported, 75 counted in `definitionLimit` | `tests/skp_mapping_tests.cpp` |
| `.skp`, 150,152 faces in one bucket (synthetic) | after 2 bodies, every face kept | |
| Acceptance: OBJ export of a model at the materialized limit (600,000 faces, 900,000 edges, 8 placements of two closed spheres through one definition, 4 per sphere) | 600,000 faces, 54 MB, 33 s | `tests/format_limits_acceptance_tests.cpp` |
| Acceptance: STL export at 600,000 facets | binary 30 MB; ASCII 183 MB | |
| Acceptance: GLB export at 600,000 faces | 10.8 MB | |
| Acceptance: DXF export of a planar model (29 placements of a 101 × 101 grid, 591,658 faces, 893,345 edges) | 893,345 segments, 68 MB, 37 s | the edge limit is 900,000, so a planar model at the face limit is not possible |
| Acceptance: OBJ import of 300,000 faces | 3 bodies, saved and reopened with every face | |
| Acceptance: STL import of 300,000 facets | 3 bodies | |
| Acceptance: glTF import of 300,000 triangles in two definitions | 2 definitions | |
| Acceptance: DXF import, one polyline of 250,000 vertices | 3 bodies, 249,999 segments | |
| One past each limit | OBJ `faces would be 300,001 of 300,000`; STL `facets would be 300,001 of 300,000`; glTF `faces would be 200,400 of 200,000` (a component definition) | each refused by name |
| The acceptance test under a 4 GiB address-space cap (`ulimit -v`) | passes | |

## STEP and IFC, in the pinned image

The helpers were built in `sketchyup-ci-pinned:r097a-ifc` (OpenCASCADE 7.9.3, IfcOpenShell 0.9.0)
with `sketchyup-cli` and `sketchyup-ifc-worker`, and run with `--memory 4g --memory-swap 4g`. The
STEP export runs in the IFC helper. The container peak is the cgroup's `memory.peak`, which counts
the page cache of the output file as well.

| Model | STEP | IFC |
|---|---|---|
| 150,000 placed faces (2 distinct spheres of 75,000, 1 placement each) | exported, 31 s, 167 MB, container peak 2.48 GB | exported, 7 s, 7.5 MB, peak 0.43 GB |
| 600,000 placed faces (2 distinct spheres, 4 placements each) | exported, 37 s, 167 MB, peak 2.55 GB | exported, 30 s, 30 MB, peak 0.66 GB |
| 300,000 distinct faces (4 distinct spheres, 1 placement each) at the 256 MiB bound | refused `outputTooLarge` after 127 s: the helper did not finish, and its output file had reached 256 MiB | |
| The same at a 512 MiB bound (tried, then reverted) | the helper stopped with a crash after 90 s, with the container peak at its 4 GiB cap; the cause was not confirmed | |
| 300,000 distinct faces | | exported, 15 s, 15 MB, peak 0.76 GB |

Placed instances are shared in a STEP file (one mesh, several instances), so a model placed many
times writes its distinct geometry once. A STEP file of distinct geometry is about 1.1 KB a triangle
for closed spheres (167 MB for 150,000 triangles). That is less than the 2.4 KB the STEP ADR records
for another model.

**Decision:** the STEP output bound stays at 256 MiB, as the brief says. The helper did not complete a
raised bound at 300,000 distinct faces. A model of distinct geometry above about 240,000 triangles
is refused by name and is recorded as a limit.

## What stays, and why

- The 192 MiB undo budget refuses a glTF import whose edit places 600,000 faces (four placements
  of the 300,000-face pair): `Edit exceeds the 192 MiB history budget`. It is named and it is a budget,
  which the brief keeps out of scope (R115).
- Literal bounds kept in `src/`: parse bounds of the MTL file (100,000 lines, 4 MiB), the `.skp`
  package's own bounds (100,000 definitions, 65,536 loop vertices, materials and tags), the
  terrain-only DXF bounds (1,000,000 vertices), the IFC and STEP helper bounds, the texture
  budget (R115) and the 1,024 material and texture-count bounds of the formats. None of these is a
  count of model geometry.

## Fuzz-regression replay

Each changed parser's libFuzzer harness was built with clang in the pinned image and run once over
its corpus (`-runs=0`): STL binary 81 inputs, STL ASCII 151, OBJ 484, DXF 465, terrain DXF 465,
GLB 8 and `.skp` package 17. Every run ended with status 0. The replay is libFuzzer only, without
ASan or UBSan, and the corpora are the R0xx runs' outputs plus synthetic `.skp` packages made from
the fixtures.
