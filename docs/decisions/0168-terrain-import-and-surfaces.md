# Terrain import and surfaces

Status: **Accepted (2026-10-18)** for the decisions below. Roadmap
[R095](../PR_ROADMAP.md#r095) (M10, scope row [F03](../SCOPE.md)); shapes
[R096](../PR_ROADMAP.md#r096). **R095.a (terrain core, persistence and site origin)
is implemented**; see "Implementation status". Each section gives the
options and trade-offs that were weighed; the owner chose the recommended option in
every case (see "Decisions"). The interaction design is the owner-approved
[R095 UX design (canvas; private to the owner)](https://claude.ai/artifact/Uhxtc3gzv7FDdMshM5FuXF).

## Context

F03 asks for "contour triangulation, terrain sculpting, drape/stamp and site
coordinates with large-coordinate precision tests". R095 is the first half. Its
Build line: contour/elevation ingestion, terrain triangulation and georeferenced
local origins with explicit coordinate conversion. Its Verify line: terrain
fixtures preserve elevations and units; far-from-origin data stays precise; invalid
contours generate bounded reports. R096 then adds scoped sculpt/smooth, contour
edit, projection/drape and a building-pad stamp with reversible previews, and must
keep contours and pad heights consistent while local edits leave distant terrain
untouched across undo and save. R095 requires R089 (the 1.0 gate) and R054
(context-scoped intersections, which drape and stamp will reuse). F03 ships in its
own gated minor release ([M10 acceptance](../M10_ACCEPTANCE.md),
[ADR 0162](0162-release-versioning.md)).

What the code gives us (surveyed at `327f35a3`):

- **Geometry.** `Body` (`src/core/body.hpp`) holds one `Surface` (double-precision
  `Vec3` vertices, faces as ordered ID loops with holes, wires) and a `Topology`
  (edge records). Faces must be planar within `tolerance = 1e-7` m and are
  tessellated by Clipper2 on a 1e-7 m grid in the face's own plane
  (`src/geometry/surface.cpp`). `checkPoint` refuses any coordinate beyond
  `coordinateLimit = 1e6` m. `BodyKind` is `Geometry`, `Group` or
  `ReferenceImage`; a body can carry an optional typed record (`textSource`,
  `referenceImage`), which is the pattern a terrain record would follow.
- **Edits.** Every change is one `Edit` applied by `Document::applyEdit` with a
  revision check; bodies are immutable `shared_ptr`s, so undo shares everything an
  edit does not replace. Retained history is capped at 64 MiB
  (`Document::historyLimit`).
- **Limits.** `Surface::validate` refuses a body over 100,000 vertices or faces.
  `DocumentLimits` caps unique geometry at 100,000 vertices, 100,000 faces and
  300,000 edges, and materialized geometry at 600,000 faces. The model JSON chunk is
  capped at 32 MiB (`NativeLimits::modelBytes`). The frozen
  [R082.w v2 memory budget](../verification/R082w-memory-budget-v2.md) is 1 GiB for
  the supported 100k families, one of which is "far".
- **Format.** Schema 28 is released in 1.2.0 and frozen. The writer emits the lowest
  schema the content needs ([ADR 0163](0163-documentation-sheets.md) decision 2).
  Vertices are JSON `[id, x, y, z]` doubles; Qt writes the shortest round-trip form.
- **Importers.** DXF (`src/io/dxf_source.cpp`) is bounded: 64 MiB, 1,000,001 lines,
  100,000 records per section, 100,000 expanded segments and 100,000 vertices per
  polyline, 128 distinct omission kinds. Units come from `$INSUNITS` (in, ft, mm,
  cm, m) or an explicit override; unitless input is refused. Today it imports world
  XY only and **skips** any LWPOLYLINE with an elevation (group 38) and any
  POLYLINE with non-zero Z as `widthElevationOrVertexFlags`: exactly the contour
  data R095 needs. Fatal problems throw one message; recoverable ones become
  counts in a JSON report (`omittedEntities`, `losses`, `sha256`) shown in the
  conversion sheet. Every importer opens a **new unsaved document**
  ([ADR 0111](0111-native-gltf-import-workflow.md)); none adds to the open model.
- **Large coordinates.** The model is double throughout. The viewport renders
  relative to a snapped 16 m origin ([ADR 0060](0060-camera-relative-rendering.md)),
  so rendering is not the problem. GLB export is float32 and documents the loss
  ([ADR 0032](0032-glb-snapshots.md)). `SolarSettings` stores latitude, longitude
  and a north angle; there is no site origin or CRS anywhere.
- **Dependencies.** Geometry libraries are vendored unmodified under `third_party/`
  with the upstream commit recorded, linked statically, and their licences
  installed to `share/licenses/sketchyup` (Clipper2 BSL-1.0, Manifold Apache-2.0,
  cgltf MIT). The PKGBUILD runtime depends only on Qt, OpenSSL, Wayland and fonts.
  Application code is MIT ([ADR 0001](0001-baseline.md)). `dependencies.json` lists
  Clipper2 but not Manifold or cgltf (a separate inventory gap, not fixed here).
- **Automation.** Commands are `noun.verb` specs in `src/automation/catalog.cpp`;
  binary inputs travel inline (`asset.import` takes base64 `data`, at most
  22,369,624 characters). `assistantVisible()` filters the in-app assistant's tools
  and `assistant_tool_surface_tests` pins that surface.

Measurements behind the numbers below are in the [appendix](#appendix-experiments).

## 1. Sources

### Options

| Source | Value | Cost and risk |
| --- | --- | --- |
| DXF contours: LWPOLYLINE with group 38 elevation; 2D POLYLINE with vertex Z; 3D POLYLINE (flag 8); LINE with equal end Z | The common survey and civil hand-off; reuses the bounded tokenizer and unit rules | New "terrain mode" in the reader; arcs (bulges) must be segmented |
| Point files: CSV / XYZ / PNEZD (delimited text) | Survey shots and thinned LiDAR | New small parser; column order and header detection need explicit user choice |
| Model edges and curves picked in the model | No parser; lets users draw breaklines and boundaries | Needs a selection-to-source conversion and a rule for edges whose Z varies |
| GeoJSON contours | Common from GIS portals | Usually geographic (lon/lat, EPSG:4326), so it needs a CRS transform to be useful |
| LandXML surfaces/breaklines | Civil exchange with explicit units and TINs | XML schema subset work; a second parser to fuzz |
| GeoTIFF / DEM rasters | National elevation data | Needs libtiff/GDAL; grids produce dense meshes (1 km at 1 m is 2M triangles) |
| LAS/LAZ point clouds | Raw LiDAR | Very large; needs thinning and classification; LAZ needs a decompressor |

### Recommendation

The first set is **DXF contours, CSV/XYZ points and picked model edges**. Everything
else goes to a later row. These three cover survey hand-offs and hand-drawn sites,
need no new dependency, and fit the existing bounded-parser and fuzzing pattern.

- **Roles.** Every polyline is assigned one role: *contour* (constant elevation),
  *breakline* (elevation varies along it), *boundary* (closed; outer limit) or
  *hole* (closed; excluded). Roles come from DXF layers chosen in the import dialog;
  by default every elevated polyline is a contour if its Z is constant and a
  breakline otherwise.
- **Terrain import adds to the open document** in one labelled edit, unlike the
  existing importers, because a site is normally placed under an existing model.
  The source file is never modified.
- **Units.** DXF follows the existing rule: `$INSUNITS` or an explicit choice;
  unitless input is refused. Point files have no units, so the user must choose.
  The unit list adds the **US survey foot** (1200/3937 m), which DXF cannot express
  and US state-plane data uses. Elevation always uses the horizontal unit (a
  separate Z unit is an owner option; not recommended). The report states
  `metresPerUnit` and the formula applied.

### Invalid input and bounded reports

A terrain import has three outcomes: **refused** (nothing changes), **imported with
notices**, or **imported cleanly**. The report extends the existing DXF report
shape (`apiVersion`, `format`, `metresPerUnit`, `sha256`, counts) with a
`terrain` object.

| Problem | Default handling |
| --- | --- |
| Non-finite number, unit missing, file or line over budget | Refuse (existing rules) |
| Two contours cross, or a contour crosses itself | Refuse; list the pairs |
| Same XY (within 1e-7 m after conversion) with different Z | Refuse; list the locations |
| Contour whose Z is not constant | Import as breakline; notice |
| Exact duplicate point or vertex | Merge; count |
| Zero-length segment, collinear spike | Drop; count |
| Arc segment (bulge) | Segment at the chosen chord tolerance; count |
| Point or contour outside the boundary | Clip or drop; count |
| Fewer than three non-collinear points | Refuse |
| Local extent beyond ±1e6 m after the origin is applied | Refuse; state the extent |
| Triangulation over the terrain limit (§2) | Refuse; state the count and the limit |
| Flat triangles left after mitigation (§3) | Import; count and area |

Bounds: at most 32 locations listed per problem kind and 64 kinds; each location
gives the source reference (DXF entity handle or line number; CSV line number;
model edge ID) and XY in source units; each message is at most 256 bytes and quotes
at most 64 bytes of user text, matching `DocumentLimits`; the whole report is at
most 64 KiB. Counts are always complete even when locations are truncated.
Whether crossing contours refuse or drop the offending contours is owner
decision 4.

Input budgets for R095: the DXF limits above; point files at most 64 MiB and
1,000,000 lines; at most 250,000 input vertices after merging and thinning. The
triangle limit is in §2.

## 2. Representation

### Options

**(a) Ordinary faces in a geometry body.** The triangulation becomes one body of
planar triangles with soft, smooth interior edges.

- For: every existing tool works on it unchanged (paint, intersect, section,
  measure, export, picking, inference).
- Against: it hits the current limits at realistic sizes. One body and the whole
  document allow 100,000 faces; a 100k-triangle terrain uses the entire document
  budget. In memory, Surface plus Topology measured about 0.5 KB per triangle
  (52 MB for 99,458 triangles). JSON is about 200 bytes per triangle (vertex, face,
  edge and edge-appearance entries), so the 32 MiB model chunk tops out near
  150k triangles. Full validation of one 100k body took 2.1 s, so every local edit
  would re-validate the whole terrain, and an undo entry would snapshot the whole
  body (~50 MB of a 64 MiB history).
- Nothing remembers the contours, so R096 contour editing and pad stamping would
  have to reverse-engineer them.

**(b) A new terrain body kind with its own mesh store.** A TIN record (indexed
vertex and triangle arrays, ~40 B per triangle) plus sources and parameters.

- For: scales to 1M triangles in tens of MB; regeneration and sculpting are array
  operations.
- Against: every consumer that reads `body->surface` (viewport caches, picking,
  inference, sections, R054 intersection, measurement, exports, inspection, the
  assistant's queries) needs an adapter, or terrain silently disappears from it.
  Undo needs its own copy-on-write chunking to stay within history. That is the
  largest integration surface of the three.

**(c) Hybrid: a terrain record plus tiled ordinary geometry (recommended).**

- A new `BodyKind::Terrain` container (it behaves like a group) carries a
  `TerrainRecord`: the sources (contours, breaklines, boundary, holes and points in
  local coordinates, each with its source reference), the triangulation parameters,
  the generator identity (`cdt-2.0.0/terrain-v1`), the import provenance (file
  SHA-256, units, `metresPerUnit`) and a digest of the generated mesh.
- Its children are ordinary geometry bodies, one per **tile**: triangles are
  bucketed by centroid into a square grid sized so a tile holds at most 2,048
  triangles. Tiles are a partition of the triangle set, not a geometric clip, so
  there are no seam constraints; a vertex on a tile border is duplicated in each
  tile and kept identical by the terrain operations.
- Tiles are ordinary bodies, so viewing, picking, paint, sections, exports,
  measurement and R054 intersection work with no adapter.
- A local R096 edit replaces only the tiles it touches: validation, render
  rebuild and the undo entry scale with the edit (a 2,048-triangle tile is about
  1 MB), not with the terrain.
- Generic topology tools (Push/Pull, Move on vertices, Erase, Intersect *into* the
  terrain) refuse terrain tiles with "Edit terrain with the terrain tools, or
  Explode it to ordinary geometry". Paint, tags, hide/lock and Intersect *with*
  terrain (as reference) are allowed. **Explode terrain** turns it into an ordinary
  group of geometry, drops the record, and is subject to the ordinary limits.
- If a tile is changed outside the terrain tools (for example by a future tool that
  forgets the rule), the mesh digest no longer matches and the terrain shows
  "Changed since generated" with Regenerate, the pattern of
  [ADR 0166](0166-configurable-component-interface.md) "Changed".

How (c) meets R096:

| R096 need | With (c) |
| --- | --- |
| Scoped sculpt and smooth | Change vertex Z inside a radius; replace only the affected tiles; distant tiles are pointer-identical, which the Verify line can test directly |
| Contour edit re-triangulates locally | Edit the source contour in the record; re-run CDT on the cavity of triangles touching the edited span plus a one-ring margin; replace those tiles |
| Drape | R054 intersection against tile bodies; no terrain adapter |
| Pad stamp keeps pad heights consistent | The pad outline becomes a breakline at the pad height plus a daylight slope; the pad region is re-triangulated with those as constraints, so pad vertices are exact by construction |
| Undo and save | Ordinary body changes; history cost proportional to touched tiles |

**Size limit.** With (c) the in-memory cost stays about 0.5 KB per triangle, so
1M triangles (~500 MB before render caches) does not fit the frozen 1 GiB budget
alongside a building model. The recommendation is a **terrain limit of 250,000
triangles per document**, counted in its own limit class so a terrain does not use
up the 100,000-face modelling budget. 250k covers a survey-grade site of several
hectares at 0.25–0.5 m contours. Going to 1M is possible later by giving tiles a
compact in-memory form, which is option (b) inside each tile; that is a follow-up,
not R095. The limit is owner decision 3.

### Persistence

- **Schema 29** with required feature `terrain-v1`. A document without terrain is
  still written as schema 28 (or lower), so 1.2.x keeps opening it; a document with
  terrain is refused by 1.2.x with its normal newer-format error.
- `TerrainRecord` and the tile bodies' ordinary attributes (materials, tags,
  per-face paint) go in the model JSON.
- Tile **meshes** go in a new binary container chunk `terrain-mesh` (little-endian
  float64 XYZ plus uint32 triangle indices per tile, with the chunk SHA-256 in the
  manifest), about 24 bytes per triangle. Vertex and face IDs are implicit (tile
  base + index); edge IDs come from `Topology::rebuild` on load, which is
  deterministic for a given surface; interior edge appearance (soft, smooth) is
  implied. Loading checks counts against the limits before allocating, like
  [ADR 0159](0159-instanced-component-storage.md) placements. JSON tiles (simpler,
  ~200 B per triangle, ~150k ceiling) are the alternative, and would make the
  250k limit unreachable.
- **The mesh is stored, not re-derived on load.** Re-deriving would make the file
  depend on the exact CDT version, compiler and floating-point flags; a library
  upgrade could then change every triangle and every ID that R096 edits, drapes
  and paint refer to. Sources are stored so Regenerate is always available, and a
  regenerate is an explicit, labelled, undoable edit.
- The new parsers (terrain DXF mode, point files) and the `terrain-mesh` decoder
  get libFuzzer targets and parser-fault corpus entries, as R083.e requires.

## 3. Triangulation

### Library options

| Candidate | Licence | Arch packaging | Fit and robustness | Measured |
| --- | --- | --- | --- | --- |
| **CDT** (artem-ogre) 2.0.0 | MPL-2.0 (file-level copyleft); bundled predicates BSD-2 (Lenthe), `portable_nth_element` Apache-2.0 with LLVM exception | Not in the Arch repos; vendor header-only under `third_party/cdt` | Constrained Delaunay with free points and open constraint edges; Shewchuk-style adaptive exact predicates; duplicate removal; detects crossing constraints with the edge pair; hole and outer removal by depth | 500k vertices / 999k triangles in 1.2–1.7 s, 112 MB peak; identical output on rerun |
| Clipper2 triangulation (already vendored, BSL-1.0) | BSL-1.0 | Vendored | CDT of **closed, non-intersecting polygons only**: no free points and no open breaklines, so contours cannot be constraints. Marked beta upstream | Not applicable |
| Triangle (Shewchuk) | Non-free: may not be sold or bundled in commercial products without permission | Not in Arch repos | Excellent quality and refinement | Not tried: incompatible with MIT distribution |
| CGAL 2D triangulations | GPL-3.0-or-later (commercial licence otherwise) | `extra/cgal` 6.2.1 (needs GMP/MPFR, Boost) | Most robust; exact kernels | Not tried: would make the shipped binary GPL and add heavy dependencies |
| poly2tri | BSD-3-Clause | Not in Arch repos | Needs one enclosing polygon; fails on duplicate and collinear input; non-exact predicates | Not tried |
| Delaunator (C++ ports) | MIT/ISC | No | Unconstrained only | Not applicable |
| In-house Bowyer–Watson plus constraint insertion | MIT | — | Full control; roughly 1.5–2k lines of the hardest robustness code in the product, with exact predicates still borrowed | Not built |

**Recommendation: vendor CDT 2.0.0** (commit `c888b30d`, tagged 2026-09-20)
unmodified under `third_party/cdt`, in header-only mode behind a small
`geometry/terrain_triangulation` adapter (as Clipper2 sits behind the surface
adapter). Install its `LICENSE`, `LICENSE-predicates.txt` and `LICENSE-LLVM.txt`;
add it and the missing Manifold and cgltf entries to `dependencies.json`. MPL-2.0
requires that the CDT source files stay available under MPL; shipping them
unmodified in the source tree satisfies that, and the MIT application code is
unaffected. Confirming this reading is owner decision 5.

### Flat triangles

A Delaunay triangulation connects three vertices of the **same** contour wherever a
contour bends back on itself (hilltops, ridges, valley floors), giving flat
triangles that are a known terrain artefact. Synthetic elongated contours gave 5.1%
flat triangles; round contours gave 1.0% (only the summit). Mitigations, in order of
cost:

1. **Flip.** For an interior non-constraint edge whose triangles are both flat, flip
   it when the flipped pair is not flat and the quad is convex. Cheap, local,
   deterministic; removes most of the valley/ridge artefacts.
2. **Steiner fill.** For remaining flat regions bounded by one contour, insert
   points along the region's medial axis with elevation interpolated from the
   neighbouring contour (half an interval up or down, by the direction of the
   adjacent slope). Gives plausible summits and valley floors, but invents data.
3. **Report only.** Count and area in the report; leave the user to add spot
   heights or breaklines.

Recommendation: **1 always, 3 for whatever remains; 2 offered as an explicit
option, off by default**, because filling invents elevations a survey did not
contain (owner decision 6).

### Boundaries and holes

- If a *boundary* is supplied, triangles outside it are removed (CDT depth-based
  removal) and the boundary is a constraint at its own elevations (interpolated
  from the TIN where it has none).
- Without one, the default is the convex hull trimmed by a **maximum edge length**:
  boundary triangles with an outer edge longer than the limit are removed
  repeatedly. The default limit is 3× the median contour spacing, shown and
  editable in the import dialog. A pure convex hull invents long, thin triangles
  across concave sites.
- *Hole* polylines (ponds, building footprints, voids) remove the triangles inside
  them; nested holes are refused in R095.

### Determinism

The same input must give the same triangles and the same IDs. Experiment: two runs
on the same input gave identical output, but **translating the same data from local
to UTM coordinates changed the triangulation** (co-circular ties resolve
differently). So the rule is:

- Triangulate in the terrain's local frame (model coordinates after the site
  origin), never in source coordinates.
- Feed vertices in canonical order: sources in file order (entity order, then
  vertex order), then points in line order, then picked edges by edge ID; after
  duplicate merging, keep the first occurrence.
- Assign IDs from the result, not from CDT's internal order: sort triangles by
  their vertex indices (smallest first, keeping orientation), bucket them into
  tiles, allocate tile body IDs in row-major tile order, and vertex/face IDs within
  each tile in that sorted order.
- Record the generator identity and pin build flags that affect floating point
  (no `-ffast-math`; `-ffp-contract=off` for the adapter). Store the mesh (§2), so
  determinism across compilers or library versions is not required for files to
  stay valid, only for Regenerate to be reproducible on one build.
- Verify with a golden fixture: the mesh digest of each terrain fixture is pinned
  in the tests, and a library or flag change that alters it fails CI and must
  update the generator identity.

## 4. Georeferencing and large coordinates

### Precision of the current core

The core is double everywhere; the one hard edge is `coordinateLimit = 1e6` m,
which **refuses raw UTM northings** (about 5,000,000 m) outright. Measured:

| Coordinate | double spacing | float32 spacing |
| --- | --- | --- |
| 1,000 m | 1.1e-13 m | 6.1e-5 m |
| 500,000 m | 5.8e-11 m | 0.031 m |
| 5,000,000 m | 9.3e-10 m | 0.5 m |

Computing the normal of 1 m terrain triangles at (500,000, 5,000,000) instead of
near the origin changes it by up to 2.0e-7, larger than the 1e-7 m geometry
tolerance; near the origin the error is zero. Raising the coordinate limit to
admit raw UTM would therefore make planarity and contact tests noisy across the
kernel. GLB export (float32) would lose 0.25 m at those northings. Text round trips
of doubles are exact.

### Options

1. **Raise `coordinateLimit` and store raw projected coordinates.** No conversion
   step; but kernel tolerances become unreliable (above), and every float32 path
   breaks.
2. **Document-level site origin** (local offset): model = source × unitScale −
   origin, with origin stored in the document; conversion is explicit at import and
   export. Geometry stays near the origin, within the existing limits and
   tolerances.
3. **Origin plus a CRS identifier, transforms by PROJ.** Accept geographic
   (lon/lat) or other-CRS input and reproject. PROJ is in Arch (`extra/proj`
   9.8.1, MIT) but brings its SQLite database, optional datum grids and network
   grid fetching, and would be the first system geometry dependency in the
   PKGBUILD.

**Recommendation: option 2 with a CRS label but without PROJ.** The user supplies
data already in a projected, metric-or-feet CRS; the document stores:

```
site: {
  origin: {easting, northing, elevation},   // metres, doubles; elevation default 0
  crs: "EPSG:32633",                        // optional label, ≤ 64 bytes, informational
  established: {source: "<file name>", sha256: "<hex>"}  // provenance of the first import
}
```

- The CRS is a label: it is never used to transform. A later import that names a
  different CRS (from a LandXML header or a user entry) warns and needs explicit
  confirmation. PROJ stays an option for a later GIS row (owner decision 8).
- The conversion is `local = source × metresPerUnit − origin` in double. With
  4-decimal source values near (512,345, 5,234,567) the measured error against
  exact arithmetic is ≤ 4.7e-10 m for metres, international feet and US survey
  feet, and export (`(local + origin) / metresPerUnit`) round-trips the source
  values exactly. Elevations are not offset by default, so model Z equals source
  elevation × unit scale and contour labels read true heights.
- `SolarSettings` latitude/longitude stay separate. Without PROJ the product
  cannot derive one from the other; the site panel shows both and says so.

### What "explicit coordinate conversion" means

At import, before anything changes, the dialog shows:

- source units (detected or chosen) and the source extents (min/max E, N, Z) in
  source units;
- the **proposed origin**: the centre of the source extents rounded to 100 m,
  in metres; editable;
- the resulting local extents in document display units, and the formula, e.g.
  "model = (source × 0.3048 m/ft) − (512,300 m E, 5,234,500 m N, 0 m)";
- an optional CRS label.

The user confirms with **Import**. If the document already has a site origin, it is
shown read-only and used; the dialog cannot change it. Data that would land beyond
±1e6 m local is refused with the extent named.

After import:

- **Site settings** shows origin, CRS label and provenance. Changing the origin is
  **Re-anchor**, one labelled undoable edit that moves all geometry by the
  difference so every world (site) coordinate is unchanged. A plain "set origin
  without moving geometry" is not offered, because it would silently re-locate the
  model in the world.
- Entity info and the Measurements box can show a vertex in site coordinates
  (E, N, Z) alongside model coordinates (UX decision).
- **DXF export** gains "Write site coordinates" (adds the origin back, in the chosen
  unit). Other exporters keep model coordinates in R095 and say so in their reports
  (owner decision 9).

## 5. Interaction surface (high level)

Commands follow the existing `noun.verb` catalogue; each is one labelled edit with
preview (dry run), revision checks and the shared error codes.

| Command | Purpose |
| --- | --- |
| `site.describe` | Origin, CRS label, provenance; read-only |
| `site.set` | Establish the origin when none exists |
| `site.reanchor` | Change the origin, moving geometry so site coordinates stay fixed |
| `site.convert` | Convert points between model and site coordinates; read-only |
| `terrain.inspect_source` | Parse a source (inline base64 `data`, format, units) and return extents, layers, roles, proposed origin and the bounded report, without changing anything |
| `terrain.import` | Parse, triangulate and add a terrain from a source with explicit units, origin, role mapping and options |
| `terrain.create_from_edges` | Build a terrain from picked edges and curves (roles per selection) |
| `terrain.describe` | Sources, parameters, triangle and tile counts, generator, "changed since generated" |
| `terrain.set_parameters` | Change boundary mode, max edge length, flat-triangle options; regenerates |
| `terrain.regenerate` | Rebuild the mesh from the stored sources |
| `terrain.explode` | Convert to an ordinary group of geometry, dropping the record |
| `terrain.elevation_at` | Elevation (and triangle) under XY points; bounded batch |

CLI: `sketchyup-cli --import-terrain FILE --terrain-units U [--origin E,N[,Z]]
[--output NEW.sketchyup]`, mirroring the other import flags, printing the same
report. MCP exposes all commands. The in-app assistant should see only
`site.describe`, `terrain.describe` and `terrain.elevation_at` at first (no
file-bearing commands, small schemas), pinned in the tool-surface baseline after a
corpus run measures the token cost (owner decision 10).

Desktop entry points: **File → Import terrain…** (DXF or point file), **Create
terrain from selection** (context menu on edges), **Site settings…** (File menu,
near Document units) and a **Terrain** section in Entity info.

Decisions the UX design must make:

1. Layout of the import dialog: unit choice, the conversion preview and how the
   proposed origin is presented and edited.
2. How layers map to roles (contour, breakline, boundary, hole, ignore), and how
   point-file columns are chosen (order, header, delimiter).
3. Whether a preview of contours and the triangulated surface appears before
   Import, and how invalid locations are highlighted in it.
4. The report presentation: refusals vs notices, jumping to a listed location,
   copy as text.
5. Terrain appearance: default material, contour-line display (derived, not
   geometry), elevation shading, edge visibility.
6. What the Terrain section of Entity info shows and edits, and where Regenerate
   and "Changed since generated" appear.
7. How tools refuse on terrain tiles, and how Explode is offered and confirmed.
8. Site coordinates in Entity info and the Measurements box, and how the site
   origin is marked in the viewport.
9. Keyboard access, accessible names and 1×/2× and text-size behaviour, as for all
   native work.

## 6. Verification plan

Fixtures (all in-repo and generated or hand-made, no third-party survey data):

- `terrain-contours-m.dxf`: 0.5 m contours of a synthetic hill and valley, metres,
  including a hairpin valley (flat-triangle case), a boundary and a pond hole.
- `terrain-contours-usft.dxf`: the same terrain in US survey feet, unitless header,
  imported with the explicit unit.
- `terrain-utm.dxf` and `terrain-utm.xyz`: the same terrain translated to
  (512,345.678, 5,234,567.891) with an 1,850 m datum.
- `terrain-invalid.dxf`: crossing contours, a self-crossing contour, a conflicting
  duplicate, a varying-Z "contour", zero-length segments, arcs, points outside the
  boundary, and more than 32 instances of one problem.
- Picked-edges fixture built by commands in the test.
- A 250,000-triangle generated terrain for budgets.

Checks for the Verify line:

| Verify clause | Check |
| --- | --- |
| Elevations preserved | Every source vertex appears in the mesh with Z equal to source Z × scale (exact for metres, ≤ 1e-9 m otherwise); `terrain.elevation_at` on every source vertex returns its elevation |
| Units preserved | The usft fixture equals the metre fixture within 1e-6 m after conversion; the report states `metresPerUnit = 1200/3937`; display units do not change geometry |
| Far-from-origin precision | The UTM fixture, imported with its proposed origin, matches the local fixture's mesh (same digest after translation) and every vertex is within 1e-9 m of `source × scale − origin`; DXF export with site coordinates reproduces the source coordinates exactly at the source's decimals; save, reopen and undo/redo preserve them bit for bit |
| Invalid contours give bounded reports | Each invalid kind is reported with its count and at most 32 locations with source references; refusals leave revision, history and records unchanged; messages ≤ 256 bytes; report ≤ 64 KiB |
| Determinism | Two imports of each fixture give identical records and IDs; mesh digests are pinned; a shuffled input order that is canonicalised gives the same mesh |
| Limits and refusal | 250,001 triangles refuse with the count and the limit; schema 28 readers refuse a terrain file; a terrain-free document still writes 28 |
| Parsers | Fuzz targets for terrain DXF mode, point files and the `terrain-mesh` chunk; corpus entries in `parser_fault_corpus_tests` |

Budgets (software rendering on the reference machine; to be confirmed by a
measurement layer before freezing):

| Operation (250k triangles) | Budget |
| --- | --- |
| Parse and triangulate | ≤ 3 s, with progress and cancel |
| Commit edit, tiles and validation | ≤ 2 s |
| Open and save the document | ≤ 2 s each above the same document without terrain |
| Added resident memory | ≤ 200 MB |
| File growth | ≤ 8 MB (binary mesh chunk) |
| One-tile replacement (R096 preview path) | p95 ≤ 100 ms, the existing routine-edit budget |
| Complete frame with the terrain visible | Within the M9 frame budget |

Native tests follow the established matrix: a `terrain_import_input_tests` suite on
X11 and Wayland at 1× and 2×, plus accessibility and keyboard-reachability
extensions for the dialog and site settings.

## 7. Out of scope

For R095: GeoJSON, LandXML, GeoTIFF/DEM and LAS/LAZ import; CRS transforms (PROJ);
geographic input; terrain export as LandXML; terrains over 250k triangles; nested
holes; multiple terrains merged into one; cut/fill volumes; Steiner refinement for
triangle quality (beyond the optional flat fill); deriving latitude/longitude from
the site origin.

Left to R096: sculpt and smooth, contour editing with local re-triangulation, drape
and projection, building-pad stamp with daylighting, their previews, and the
"distant terrain untouched" proof. R095 must leave them a record with stored
sources, local re-triangulation through the adapter (the cavity API), tiles, and
the Changed/Regenerate mechanism.

## 8. Risks

- **Representation integration.** Option (c) still requires every generic tool to
  refuse terrain tiles correctly; a missed tool can corrupt the source/mesh
  correspondence. The mesh digest catches it after the fact, not before.
- **Memory at the limit.** 0.5 KB per triangle is measured for Surface and Topology
  alone; render caches and inference indices add to it. The 250k limit needs a
  measurement layer before it is frozen.
- **CDT robustness at scale.** CDT throws on crossing constraints (we pre-check and
  report) and has had non-termination fixes in refinement (not used here). The
  vendored version must be fuzzed with adversarial contour sets.
- **Determinism.** Pinned by digests, but any toolchain change can alter Regenerate
  output; stored meshes keep files valid regardless.

## Decisions

The owner decided these points on 2026-10-18, each as recommended:

1. **First sources:** DXF contours, CSV/XYZ point files and picked model edges. GeoJSON, LandXML, GeoTIFF and LAS/LAZ are later rows.
2. **Representation:** hybrid (c): a `BodyKind::Terrain` container with a `TerrainRecord`, its mesh in ordinary geometry tiles of at most 2,048 triangles.
3. **Size limit:** 250,000 triangles per document, in its own limit class; 1M later through compact tiles.
4. **Crossing contours and conflicting duplicates:** refuse the import with a located, bounded report.
5. **Triangulation library:** CDT 2.0.0 (MPL-2.0, header-only), vendored behind an adapter.
6. **Flat triangles:** always flip, report what remains; Steiner fill is an explicit option, off by default.
7. **Persistence:** schema 29 with required feature `terrain-v1`; tile meshes in a binary `terrain-mesh` chunk, stored rather than re-derived on load. Documents without terrain keep their lower schema.
8. **Georeferencing:** a document site origin plus an informational CRS label; imports must already be projected; no PROJ dependency.
9. **Export conversion in R095:** DXF export gains "Write site coordinates"; other exporters write model coordinates and say so.
10. **Assistant visibility:** the in-app assistant sees only `site.describe`, `terrain.describe` and `terrain.elevation_at`, pinned after a corpus token measurement; MCP exposes every command.
11. **Import target:** terrain import adds to the open document in one labelled edit.

**Interaction design.** The owner approved the R095 UX design on 2026-10-18 (link in the
status line). It settles §5's list as follows:

- **Import dialog:** settings on the left (format, units, layer → role table, site
  coordinates block with source extents, the proposed origin rounded to 100 m and
  editable, an optional CRS label, the conversion formula and the local extents),
  a live preview on the right switching Sources / Surface with optional elevation
  shading, and a footer status ("Ready: N triangles · K notices") with Show report.
- **Point files:** delimiter, header row and a meaning per column above a sample
  table, with PNEZD, PENZD, ENZ and XYZ presets; description codes map to roles;
  units are required and Import stays disabled until they are chosen.
- **Report:** refusals, notices and merges grouped by kind with counts and at most 32
  locations each; choosing a location zooms the preview and rings it in the failure
  colour; Copy report as text and Save report. A refusal shows a banner that nothing
  was changed, and Import stays disabled.
- **Appearance:** default material "Terrain"; contour lines are derived display (not
  geometry) at a chosen interval with every fifth line as an index line; shading by
  material or by elevation bands; tile seams and interior edges are not drawn.
- **Entity info, Terrain section:** name, triangle and tile counts, source file and
  digest, sources, elevation range, generator and flat-area result; Display
  (contour lines, shading); Terrain settings… and Explode terrain…; an amber
  "Changed since generated" chip with Regenerate when the mesh digest no longer
  matches.
- **Tools on terrain:** generic topology tools show the not-allowed cursor over a
  tile and put "<Tool> does not edit terrain. Edit it with the terrain tools, or
  explode it to work on it as ordinary geometry." in the status bar, linking to
  Explode. Explode confirms with the effect on the modelling and terrain budgets
  (Cancel is the default) and is refused when the faces would exceed the modelling
  limit.
- **Site:** File → Site settings… shows the origin read-only, the CRS label and
  provenance, with Re-anchor… (one edit that moves geometry so site coordinates are
  unchanged). Entity info shows a vertex in model and site coordinates; in the
  Measurements box a leading `@` types an absolute site position (E, N[, Z]) when the
  document has a site origin. The viewport marks the origin with a labelled
  crosshair while a terrain or the site settings are selected, and View → Site
  origin toggles it.
- **Keyboard and scale:** every control is a native widget in the Tab order with an
  accessible name; the report list and preview follow the existing list and canvas
  keyboard patterns; 1×, 2×, fractional and 200 % text follow the existing rules.

## Implementation status

R095 is split into layers. R095.b (the CDT adapter in
`src/geometry/terrain_triangulation.*`) is built in parallel; importers, Regenerate and
the desktop are later slices.

**R095.a: terrain core, persistence and site origin.** On `feat/r095a-terrain-core`.
It takes an already-triangulated mesh (local XYZ vertices plus triangle indices); it
does not triangulate.

- **Records** (`src/core/terrain_records.hpp`, `src/core/terrain.*`). `BodyKind::Terrain`
  carries a `TerrainRecord` (sources with role and source reference, triangulation
  parameters, generator identity, import provenance, mesh digest). Its children are
  geometry bodies marked `terrainTile`: triangles bucketed by centroid into a square XY
  grid (cell sized for about 1,024 triangles, halved until no cell exceeds 2,048; a
  cluster the grid still cannot separate is split in triangle order), empty cells
  omitted, tiles in row-major order with body IDs allocated in that order, border
  vertices duplicated. Within a tile, vertices are numbered 1..n in first-use order and
  faces n+1..n+m, interior edges are soft and smooth, and the tile shares its terrain's
  frame (identity transform). `createTerrain` builds the record and tiles as one
  labelled edit ("Create terrain"). A terrain behaves like a group for selection
  (`enclosingGroup`, click targeting).
- **Limits.** `terrainTriangleLimit` (250,000 per document) in `document_limits`, checked
  on every edit and load. Tile triangles, vertices and edges are counted only there
  (`DocumentTotals::terrainTriangles`); tiles still count as body records. Refusals name
  the count and the limit ("… terrain triangles would be 250,001 of 250,000").
- **Generic tools refuse terrain tiles** with "Edit terrain with the terrain tools, or
  Explode it to ordinary geometry" (`UNSUPPORTED_TARGET`). The rule is enforced once in
  `Document::applyEdit`, next to the lock rule: a change to a tile's surface, topology,
  edge appearance, curves, guides, transform, parent or kind, or the deletion or
  creation of a tile, is refused unless the same edit changes that terrain's record (as
  only terrain operations do). Tools that run a kernel first refuse earlier, where they
  check their targets: push/pull, extrude, offset, erase face and edge, split edge,
  heal, merge coincident topology, insert edges, draw edge and curve, guides into a
  tile, move of vertices/edges/faces (`transformSelected`), intersect *into* a tile,
  and solid booleans. Also refused: moving, reparenting or deleting one tile, edge
  appearance, reverse/orient faces, group explode of a terrain (pointing to Explode
  terrain) and making a terrain part of a component. Allowed: paint, materials and
  texture mapping, tags, names and properties, hide/lock, moving or deleting the whole
  terrain, and intersect *with* terrain as a reference.
- **Mesh digest.** SHA-256 of the tile meshes in ID order (counts, f64 XYZ, u32
  indices; body IDs excluded, so a copy or a moved terrain keeps its digest).
  `terrainChangedSinceGenerated` compares it with the record.
- **Explode terrain** (`terrain.explode`): the terrain becomes an ordinary group of its
  tiles, the record is dropped, one edit. Refused (`LIMIT_EXCEEDED`) when the faces
  would exceed the 100,000-face modelling limit, naming the resulting count, the limit
  and the terrain's triangles.
- **Site origin.** `Document::site()`: easting, northing, elevation (metres, double,
  each within ±1e8 m), an informational CRS label (≤ 64 bytes) and provenance (source,
  SHA-256). `site.set` establishes it only when none exists. `site.reanchor` changes
  E/N/Z (CRS and provenance are kept) and, in the same edit, moves every top-level body,
  every model-frame section plane and every saved camera target and clip plane by the
  difference, so site coordinates are unchanged; Undo restores both. A locked top-level
  body refuses the re-anchor (locks are authoritative), as does any geometry that would
  pass the ±1e6 m coordinate limit. Model ↔ site helpers: `modelToSite`, `siteToModel`.
- **Persistence.** Schema 29 with required feature `terrain-v1`, written only when the
  document has a terrain or a site origin; other documents keep schema 26, 27 or 28
  unchanged. The model JSON holds the record (`terrain` on the body), the tile marker
  (`terrainTile: true`, with empty vertex, face and edge arrays), `site` and
  `terrainMesh` (null in a container; base64 of the chunk in the inline raw-JSON form).
  The `terrain-mesh` chunk (`f64-u32-le-v1`, after the asset chunks, SHA-256 in the
  manifest) is `SKTMESH1`, u32 tile count, per tile {u64 body ID, u32 vertices, u32
  triangles}, then per tile f64 XYZ and u32 x3 indices. The decoder checks the tile
  count (≤ 10,000), every tile (1–2,048 triangles, 3 to 3x vertices), the 250,000
  document limit and the exact length before allocating any tile. Edges are rebuilt by
  `Topology::rebuild`. `NativeLimits::containerBytes` grows by the chunk bound
  (21.2 MB).
- **Commands and queries.** Batch commands `site.set`, `site.reanchor`,
  `terrain.explode` (dry run, revision checks, shared codes); inspection queries
  `site.describe`, `site.convert`, `terrain.describe`, `terrain.elevation_at` (point
  batches of at most 200, which fit the 16 KiB request bound). MCP exposes all of them;
  `assistantVisible` hides every `site.*` and `terrain.*` name until the corpus
  measurement (decision 10), and the tool-surface baseline is unchanged.
- **Fuzzing.** `parser_fuzz_terrain_mesh` (format `terrain-mesh`) with valid and
  oversized-count seeds; `parser_fault_corpus_tests` adds a schema-29 container and the
  chunk decoder.

Left for later slices: importers (`terrain.inspect_source`, `terrain.import`,
`terrain.create_from_edges`), `terrain.set_parameters` and `terrain.regenerate`, the
desktop (Site settings, Entity info Terrain section, tool cursors), DXF "Write site
coordinates", and the assistant surface.

## Owner decisions

1. **First sources:** DXF contours, CSV/XYZ points and picked model edges; defer GeoJSON, LandXML, GeoTIFF and LAS. *Decided 2026-10-18: yes.*
2. **Representation:** hybrid (c), a terrain record plus tiled ordinary geometry, over plain faces (a) or a new mesh kind (b). *Decided 2026-10-18: (c).*
3. **Size limit:** 250,000 triangles per document in its own limit class; 1M later via compact tiles. *Decided 2026-10-18: 250k.*
4. **Crossing contours and conflicting duplicates:** refuse the import with a located report, rather than drop the offending contours. *Decided 2026-10-18: refuse.*
5. **Triangulation library:** vendor CDT 2.0.0 (MPL-2.0, header-only) behind an adapter; not Triangle, CGAL, poly2tri or in-house. *Decided 2026-10-18: CDT.*
6. **Flat triangles:** flip always, report the rest; Steiner fill as an explicit option, off by default. *Decided 2026-10-18: as stated.*
7. **Persistence:** schema 29 with `terrain-v1`; the mesh stored in a binary `terrain-mesh` chunk, not re-derived on load. *Decided 2026-10-18: yes.*
8. **Georeferencing:** document site origin plus an informational CRS label; user supplies projected coordinates; no PROJ. *Decided 2026-10-18: yes.*
9. **Export conversion in R095:** DXF export option "Write site coordinates" only; other exporters later. *Decided 2026-10-18: DXF only.*
10. **Assistant visibility:** only `site.describe`, `terrain.describe` and `terrain.elevation_at`, after a corpus token measurement; MCP gets everything. *Decided 2026-10-18: yes.*
11. **Import target:** terrain import adds to the open document in one labelled edit, unlike the other importers. *Decided 2026-10-18: add to the open document.*

## Appendix: experiments

Run on 2026-10-18 in a scratch directory (not committed), GCC `-O2`, x86_64.

- **Coordinate spacing and triangle error.** 100,000 random 1 m triangles at
  origins (0, 0), (500,000, 0), (1,000,000, 0) and with northing 5,000,000. Area
  error ≤ 5.2e-10 m²; normal error 0 at the origin, up to 2.0e-7 at northing
  5,000,000; float32 position error 0.25 m at northing 5,000,000; `%.17g` text
  round trip exact.
- **Conversion round trip.** 1,000,000 points within ±2 km of
  (512,345.678, 5,234,567.891) written with 4 decimals in m, ft and US survey ft;
  origin (512,000, 5,234,000). `source × scale − origin` vs exact
  (`long double`) ≤ 4.65e-10 m (ft) and 4.66e-10 m (usft), 0 for metres; export back
  to source units exact.
- **CDT 2.0.0** (`c888b30d`), header-only, synthetic concentric contours with every
  segment as a constraint, `IntersectingConstraintEdges::NotAllowed`:
  - 50 rings × 1,000 vertices: 98,998 triangles in 78–135 ms; 15 MB peak.
  - 500 rings × 1,000 vertices: 998,998 triangles in 1.2–1.7 s; 112 MB peak.
  - Reruns identical; the same data at UTM coordinates gives a different (also
    stable) triangulation.
  - Flat triangles: 1.0% for round contours, 5.1% for 8:1 elongated contours.
  - Deliberately crossing contours: CDT throws `IntersectingConstraintsError` naming
    the two edges.
- **Ordinary faces in the current core** (linked against the `dev` build's
  `libsketchyup_core.a`; grid terrain):
  - 99,458 triangles: Topology rebuild 0.47 s, `Surface::validate` + `Topology::validate`
    2.1 s, full tessellation 1.5 s, +49 MB resident.
  - 180,000 triangles: refused by `Surface::validate` ("Surface exceeds editing
    limits").
  - Adding faces one at a time through `Surface::vertex`/`addFaceIds` is quadratic
    (42.7 s for 100k) because `vertex()` scans for coincident points; a terrain
    builder must construct tiles directly.
