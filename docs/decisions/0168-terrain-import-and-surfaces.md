# Terrain import and surfaces

Status: **Accepted (2026-10-18)** for the decisions below. Roadmap
[R095](../PR_ROADMAP.md#r095) (M10, scope row [F03](../SCOPE.md)); shapes
[R096](../PR_ROADMAP.md#r096). **R095.a (terrain core, persistence and site origin)
is implemented**, and so are R095.b (triangulation), R095.c (importers), R095.c2 (terrain
from edges, settings and Regenerate), R095.d (the desktop import dialog), R095.e1 (the
Entity info Terrain section, terrain settings, Explode and the tools on terrain) and R095.e2
(terrain display, Site settings and site coordinates); see "Implementation status". Each section gives the
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
  E/N/Z (CRS and provenance are kept) and, in the same edit, moves every stored world
  position by the difference: top-level bodies (with their guides, reference images and
  descendants), model-frame section planes, saved camera targets, clip planes and ground
  heights, the model ground height, and the world fallbacks of annotation anchors and of
  paper dimension and label anchors. Origin-independent and unchanged: body-local
  records, annotation text offsets (vectors), paper positions, drawing views (they show
  saved scenes), hosted attachments (host-local) and the sun study. Site coordinates are
  unchanged and Undo restores the exact encoding. A locked top-level
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

Left for later slices: a role picker for Create terrain
from selection (pending an owner decision), and the assistant surface. Terrain display (the
Terrain material, hidden seams and interior edges, contour lines, elevation shading),
`terrain.set_display`, Site settings with Re-anchor and Set origin, site coordinates in Entity
info and the Measurements box, and the origin crosshair with View → Site origin are R095.e2; see
"Implementation notes (R095.e2)". DXF "Write site coordinates" and the model-coordinates
notices on the other exporters are R095.e3; see "Implementation notes (R095.e3)". The Entity info
Terrain section, Terrain settings…, Regenerate, the Explode confirmation, the tool cursors and
Create terrain from selection are R095.e1; see "Implementation notes (R095.e1)". File → Import terrain… is R095.d; see "Implementation notes (R095.d)". The DXF and
point-file importers with `terrain.inspect_source` and `terrain.import` are R095.c; see
"Implementation notes (R095.c)". `terrain.create_from_edges`, `terrain.set_parameters` and
`terrain.regenerate` are R095.c2; see "Implementation notes (R095.c2)".

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

## Implementation notes (R095.b)

R095.b vendors CDT and adds the triangulation adapter. It adds no command, document field or
file format; R095.a (records) and the importer consume its output.

**Vendoring.** CDT 2.0.0, tag `2.0.0`, commit `c888b30d83312114d3ff332855a97fd95ce95f17`;
the GitHub source archive has SHA-256
`81ed0c7a8cbdc059b26948a160c3ce68c7a147976334093fe3175c8fa161a447`. The eleven headers of
`CDT/include` are copied byte for byte to `third_party/cdt/include/CDT/` with their MPL-2.0
notices; nothing else is vendored, and `CDT_USE_AS_COMPILED_LIBRARY` is never defined, so no
generated export header is needed. Two corrections to section 3: upstream has no
`LICENSE-predicates.txt` or `LICENSE-LLVM.txt`, and the predicates (Lenthe) are BSD-3-Clause,
not BSD-2. The package installs `third_party/cdt/LICENSE` (MPL-2.0) as `CDT-LICENSE` and the
BSD notice, extracted verbatim from `predicates.h` into `third_party/cdt/LICENSE-predicates.txt`,
as `CDT-predicates-LICENSE`. The Apache-2.0 with LLVM exception text in
`portable_nth_element.hpp` stays in that file; the exception covers compiled object code, so
no further notice ships. `dependencies.json`, the PKGBUILD `license` array and the Arch
README record it. The Manifold and cgltf inventory gap stays as noted in the context.

**API.** `src/geometry/terrain_triangulation.{hpp,cpp}`, a Qt-free library
`sketchyup_terrain_triangulation` (binary64, `-ffp-contract=off -fno-fast-math`). One call,
`triangulateTerrain(input, options)`, with local metres in and plain data out: vertices as
local XYZ, triangles as counter-clockwise index triples, and a report. The input holds
contours (one Z, open or closed), breaklines (a Z per vertex), points, an optional boundary
and holes (polygons without Z). Each polyline carries a caller `sourceId` that is echoed in
report locations. Options: flat mode (`Flip`, the default, or `ReportOnly`), Steiner fill
(off), maximum edge length (none), the merge tolerances and the limits.

**Method, in order.**

1. *Numbers.* A non-finite coordinate or one beyond the local bound (1e6 m, section 1)
   refuses; UTM-scale input is refused, not triangulated, so section 4's rule is enforced here.
2. *Merging.* Vertices within 1e-7 m in x and y are one vertex, numbered by first occurrence
   in the order contours, breaklines, points, then boundary and hole vertices that are not
   already present. Same place with a Z differing by more than 1e-9 m refuses
   (`ConflictingDuplicate`, located, both source references); otherwise it merges and counts.
   Zero-length segments are dropped and counted; a repeated segment is one constraint.
3. *Boundary and holes.* Sources outside the boundary or strictly inside a hole are dropped
   with their segments, and counted. A segment is not clipped: one with a dropped end is lost
   whole, so the importer should clip with Clipper2 first if it wants contours to run exactly
   to the boundary. A hole entirely outside the boundary is dropped and counted; a hole around
   the boundary, a hole inside a hole, and a polygon with fewer than three distinct or only
   collinear vertices refuse (`InvalidPolygon`).
4. *Constraint checks (exact predicates, not the library's).* A grid finds pairs of segments
   that properly cross and vertices lying inside a segment. Both refuse as
   `CrossingConstraints` with the crossing point and the two source references, except a
   vertex that lies on a boundary or hole edge, which splits that edge (data pre-clipped to
   the boundary ends exactly there). Collinear overlap shows as such a vertex and refuses. The
   count of crossing pairs is complete up to 1,000,000. This check is why the library's own
   `IntersectingConstraintsError` never fires; if it does, the adapter reports
   `CrossingConstraints` and the fuzz harness fails. A collinear spike (a segment that doubles
   back over its neighbour) is therefore refused as an overlap, where the table in section 1
   says drop; the importer cleans spikes before this call.
5. *Fewer than three non-collinear points* among the kept sources refuses
   (`InsufficientPoints`, up to 32 of the points listed).
6. *Polygon elevations.* A boundary or hole vertex that coincides with a source takes its
   elevation. Any other is interpolated barycentrically from a first triangulation of all
   sources (including those about to be dropped, constrained by the kept segments); outside
   that triangulation's hull it takes the nearest source's elevation and is counted as
   extrapolated.
7. *Triangulation.* CDT constrained Delaunay (`Auto` insertion order, `NotAllowed`
   intersections), every contour, breakline, boundary and hole segment a constraint edge,
   super triangle erased. The adapter then checks that every constraint is one library edge.
8. *Region.* Triangles outside the boundary and inside holes are removed by depth: crossing a
   boundary or hole edge adds one and odd depth is terrain, from the outside of the hull
   (the hull itself counts when there is no boundary, which is how holes work without one).
   This is done on the adapter's own mesh, not by the library's depth routine, so contour
   edges never change the depth.
9. *Maximum edge length.* Triangles with an outer edge longer than the limit are removed
   repeatedly. An edge that is a constraint is never the one that removes a triangle.
10. *Budget.* More than 250,000 triangles refuses with the count and the limit
    (`TriangleBudget`). It is checked again after Steiner fill.

**Flat triangles.** A triangle is flat when its three vertices are contour vertices at the
same elevation (within 1e-9 m); breakline and point vertices never make one. The flip pass
visits flat triangles in index order (a first-in first-out queue) and flips an interior edge
when it is not a constraint, the two triangles form a strictly convex quadrilateral
(exact `orient2d`), and the pair then holds fewer flat triangles than before. This is the
section 3 rule (flip when both are flat and the flipped pair is not) generalised to a flat
triangle beside a non-flat one, which is where a Delaunay edge joins two points of one
contour across a hairpin. Every flip lowers the flat count, so the pass ends, and a constraint
is never flipped. What remains is reported: count, planar area and up to 32 centroids.
`ReportOnly` skips the flips.

**Steiner fill (off by default).** For each flat triangle left after the flips, in index
order: take its longest edge that is not a constraint and whose neighbour's opposite vertex
has a different elevation; insert the edge's midpoint with elevation halfway between the
contour's and that vertex's; split the two triangles on the edge. Neither new triangle is
flat, only one flat triangle is touched per split, and the points are counted. It is the
"half an interval up or down toward the adjacent slope" of section 3 with the midpoint of the
longest free edge standing in for the medial axis. It does not resolve a region wholly
enclosed by one contour (a summit or a pit): every triangle there is flat or borders a
constraint, so it stays reported. Steiner fill never moves or removes an input vertex or edge.

**Determinism.** The same input in the same order gives identical vertices, triangles and
report bit for bit; tests repeat runs, and the fuzz harness runs every input twice. Input
order matters: it numbers the vertices, and it breaks co-circular ties. For points in general
position a permuted input gives the same triangles under a different numbering (tested), but
the adapter does not canonicalise; section 3's canonical source order is the caller's job.
Output is canonical in itself: triangles are rotated to start at their smallest index and
sorted, so the library's internal order does not show. The digests of the hill and UTM-origin
fixtures are pinned in `terrain_triangulation_tests`; the CDT insertion order uses the
library's fixed-seed SplitMix64 shuffle, so it is portable.

**Report bounds.** At most 32 locations per problem kind, messages at most 256 bytes, counts
complete (crossing pairs up to 1,000,000). Kinds: non-finite, extent exceeded, limit
exceeded, conflicting duplicate, crossing constraints, insufficient points, invalid polygon,
triangle budget, triangulation failed. Limits: 1,000,000 raw vertices, 250,000 merged,
20,000 boundary and hole vertices, 250,000 triangles.

**Timings.** `terrain_triangulation_benchmark`, Release (`-O3 -DNDEBUG`), GCC 16.2, one
thread, whole adapter call, best of three, on the development machine with other jobs
running (load average about 5):

| Terrain | 10k triangles | 100k | 250k |
| --- | --- | --- | --- |
| Scattered points | 9 ms | 103 ms | 279 ms |
| Concentric contours, every segment a constraint | 14 ms | 141 ms | 364 ms |
| 8:1 elongated contours | 11 ms | 131 ms | 378 ms |
| The same with Steiner fill | 11 ms | 132 ms | 403 ms |

(The contour rows have 9,498, 99,498 and 248,498 triangles. On the elongated terrain the flips take the flat triangles from 1,672 to 498 at 10k and from 2,468 to 498 at 100k and 250k.) Against the
3 s parse-and-triangulate budget of section 6 the adapter uses about an eighth at 250k
triangles; the remaining 498 flat triangles are the summit ring, which only the importer's
spot heights or a breakline can resolve.

**Verification.** `terrain_triangulation_tests` (dev and sanitize presets) covers simple
contours, a breakline, a boundary with a hole, crossing contours with locations (including
over 32), a conflicting duplicate, collinear input, the flat flip, Steiner fill, determinism,
the triangle budget at and over the limit, UTM-scale input converted to local coordinates
with the origin added back exactly, bounded reports, a boundary-area property over 300 random
inputs, and 3,000 generated inputs and seed mutations through the fuzz harness (more with
`--fuzz-count`). The libFuzzer target `parser_fuzz_terrain_triangulation` (seeds from
`terrain_triangulation_fuzz_seeds`) ran about 379,000 executions under ASan and UBSan without a
finding; see [PARSER_FUZZING.md](../PARSER_FUZZING.md).

## Implementation notes (R095.c)

R095.c adds the importers and the import commands. Modules: `src/io/terrain_source.*` (source
set, units, bounded report, point files), the terrain mode in `src/io/dxf_source.cpp`,
`src/io/terrain_import.*` (preparation, mapping of the adapter report, inspection), the commands
in `src/automation/terrain_commands.cpp`, `importTerrain` in `src/core/terrain.*`, and
`--import-terrain` in the CLI.

**Sources.** Parsing yields items in source units, each with its reference (DXF handle; the
1-based entity number when an entity has no handle; the 1-based line number of a point) and its
layer or description code.

- *DXF terrain mode* shares the planar reader's tokenizer, byte, line, record, layer and unit
  rules (the shared stage was factored out; the planar path and its tests are unchanged). It
  accepts LWPOLYLINE (group 38 elevation), 2D POLYLINE (each vertex's group 30, or the polyline
  elevation where a vertex has none or zero; spline frame points skipped), 3D POLYLINE (flag 8)
  and LINE with equal end Z (each its own two-point polyline). A LINE with different end Z is
  ignored as `LINE:varyingZ`; a non-default extrusion, a polyface or mesh and paper space are
  ignored and counted in `omittedEntities`. Bulges become chords whose sagitta is within the
  chord tolerance (default 0.05 m, given in model metres and converted to source units), Z
  linear along the arc, each arc a located `arcs-segmented` notice. At most 1,000,000 vertices
  after arc expansion and 100,000 per polyline; a value beyond 100,000 km after the unit refuses.
- *Point files*: at most 64 MiB, 1,000,000 lines and 4,096 bytes a line; UTF-8. Blank lines and
  lines starting with `#` are skipped. The delimiter is the first of comma, tab and semicolon
  that every one of the first 20 data lines contains, otherwise whitespace. Without explicit
  columns the first data line is a header when fewer than three of its fields are numbers (with
  columns, when an easting, northing or elevation field is not a number), and three fields mean
  ENZ, more mean PNEZD. The description code is the first word of the description (at most 64
  bytes, at most 1,024 codes). Non-numeric and non-finite values and missing columns are located
  refusals naming the line and the column and quoting the value.
- *Units*: mm, cm, m, in, ft and `usft` (1200/3937 m), elevation in the horizontal unit; DXF also
  `header`. The report gives `metresPerUnit` and `metresPerUnitExact` ("1200/3937").

**Preparation** (`prepareTerrainSources`), in order:

1. *Origin.* Proposed: the centre of all items' source extents in metres, E and N rounded to
   100 m, Z 0. Used: the document's site origin when it has one (an explicit origin that differs
   in any component refuses with INVALID_REQUEST), else the explicit origin, else the proposed
   one. `local = source x metresPerUnit - origin` for X, Y and Z.
2. *Roles* from the layer or code map (auto, contour, breakline, boundary, hole, ground,
   ignore). Auto: constant Z is a contour, varying Z a breakline; points are ground. A varying
   "contour" becomes a breakline with a `contour-not-constant` notice. Boundary and hole must be
   closed (or repeat their first vertex) or refuse (`open-polygon`); more than one boundary
   refuses. Codes mapped to a line role join their points in file order, one polyline per code.
3. *Local extent*: beyond +-1,000,000 m refuses (`extent-exceeded`, naming the axis and range).
4. *Cleaning*: consecutive vertices within 1e-7 m in XY and 1e-9 m in Z (zero-length segments)
   and collinear spikes (a vertex where the polyline doubles back exactly along itself, tested
   with the adapter's exact orientation predicate, `terrainOrientation`) are dropped and counted.
5. *Pre-clipping* to the boundary and holes: each contour and breakline is split at proper
   crossings with a boundary or hole edge, at polygon vertices lying on it and at its vertices
   lying on a polygon edge (exact predicates; a crossing point within 1e-7 m of an endpoint
   snaps to it). The crossing point gets Z interpolated along the segment and is also inserted
   into the polygon edge, so the data ends exactly at a shared boundary vertex. Runs between
   split points are kept when the midpoint of their first segment is strictly inside the region
   (crossing-number test with exact orientation); runs on a boundary edge are dropped. A closed
   contour's first and last runs are joined when its start is not a split point. Counts:
   `clippedPolylines`, `clippedPiecesKept`, `clippedPiecesDropped`, `droppedOutsidePolylines`.
   Clipper2 is not used here, a deviation from the brief: its open-path clipping snaps vertices
   to its integer grid and drops Z (kept source vertices would no longer be exact), and its
   `PointInPolygon` on doubles casts coordinates to integers in `CrossProductSign`, so points
   near an edge read as "on" it (this dropped whole valley contours in the fixture).
6. *Canonical order* (the rule that makes a shuffled source give the identical mesh): rings
   start at their lexicographically smallest (x, y, z) vertex and run counter-clockwise; open
   polylines start at the smaller endpoint; within each role polylines are sorted by their
   vertex sequence (lexicographic over (x, y, z) vertices), ties by closedness and then
   reference; ground points are sorted by (x, y, z), ties by reference, and grouped into one
   `Points` source per code (codes in order). References are carried along, never part of the
   key. Record order: contours, breaklines, boundary, holes, point groups.
7. *Records*: one `TerrainSource` per polyline (a closed contour or breakline repeats its first
   vertex), local metres, with its reference; points per code with the reference
   "points, code X". At most 100,000 sources and 250,000 source vertices, else `limit-exceeded`.

`terrainTriangulationInput(sources)` builds the adapter input from stored records only
(source i has sourceId i + 1), so R095.c2's Regenerate and `terrain.set_parameters` rebuild the
identical input from a `TerrainRecord`. The adapter's refusals are mapped back with the
sourceId as the reference (a point group is resolved to the point's line by its exact XY) and
XY in source units. Triangles with a 3D cross product below 1e-12 m^2 (twice the area) would be
refused by the document's face tolerance; they are refused, located, as `degenerate-triangle`
before any edit (none occur for valid adapter output).

**Report.** The importer's source report (`apiVersion`, `format`, `units`, `metresPerUnit`,
`metresPerUnitExact`, `sha256`, `bytes` and the format's counts) plus a `terrain` object:
`outcome` (`refused`, `importedWithNotices`, `imported`), `conversion` (for example
"model = (source × 1200/3937 m/usft) − (512300 m E, 5234600 m N, 0 m)"), `sourceExtents`,
`localExtents`, `proposedOrigin`, `origin`, `originSource`, `roles`, complete `counts`,
`flatTriangles` (initial, flips, Steiner points, remaining, area), `triangles`, `vertices`,
`tiles`, `parameters`, and `refusals` and `notices`: each kind with its complete `count`, a
message, at most 32 `locations` (`reference`, `reference2` for pairs, `xy` in source units,
`detail`) and `locationsOmitted`. At most 64 kinds (further kinds are counted in `kindsOmitted`
and `occurrencesInOmittedKinds`, and a refusal among them still refuses); messages and details at
most 256 bytes quoting at most 64 bytes of user text (control characters replaced, cut on a UTF-8
boundary); the whole report at most 64 KiB, reached by thinning locations evenly (32, 16, ... 0;
`locationsPerKind`, `locationsThinned`), never counts.

**Commands.** `terrain.inspect_source` runs alone in its batch and returns
`{"status": "inspected", "inspection": ...}` at the unchanged revision: layers or codes with
polyline, closed, constant-Z, varying-Z and point counts and a default role, extents, the
proposed and used origin, the conversion and the full report including the triangulation dry
run. `terrain.import` takes base64 `data`, `format`, `units`, optional `origin`, `crs`,
`sourceName`, `roles`, `columns`, `delimiter`, `header`, `flatMode`, `steinerFill`,
`maxEdgeLength`, `chordTolerance` and `name`; it applies `importTerrain`, ONE edit labelled
"Import terrain" that creates the terrain and, when the document has no site origin, establishes
it (CRS label, provenance: source name and SHA-256), so Undo removes both. The batch response
lists `terrainOperations` (terrain, tiles, `siteEstablished`, `importReport`). A refusal throws
INVALID_REQUEST (LIMIT_EXCEEDED for budget, extent and limit kinds) with
`details.importReport` and leaves revision, history and records unchanged. The record stores the
prepared sources, the parameters (boundary mode Boundary with a boundary, else MaxEdgeLength
when given, else ConvexHull), generator `cdt-2.0.0/terrain-v1` and provenance (SHA-256, unit
name, metresPerUnit). MCP reaches both through the command catalogue; `assistantVisible` still
hides every `site.*` and `terrain.*` name and the tool-surface baseline is unchanged.

**Data path bound.** `data` is canonical base64 of at most 16 MiB (22,369,624 characters, the
`asset.import` bound). Session, MCP, staging and transaction requests keep their 64 KiB request
bound, so through them a source is at most about 47 KiB; no global bound was raised. In-process
callers (the future desktop dialog) and the CLI, which reads the file, take the full 64 MiB
source budget.

**CLI.** `sketchyup-cli --import-terrain FILE [--terrain-units U] [--terrain-format dxf|points]
[--origin E,N[,Z]] [--terrain-crs LABEL] [--terrain-columns PRESET|m1,m2,...]
[--terrain-delimiter D] [--terrain-header yes|no] [--terrain-roles NAME=ROLE,...]
[--terrain-name NAME] [--input DOC] [--output NEW.sketchyup]`. The format defaults by extension
(`.dxf`, else points); units are required for point files (`header` for DXF follows `$INSUNITS`).
Without `--input` the terrain goes into a new model. It prints `{"status": "imported", ...,
"importReport"}`; a refusal exits 1 with the code and `details.importReport`.

**Kernel fix found on the way.** Validating a tile face sent Clipper2's (beta) Delaunay
triangulation into unbounded edge creation on a plain 50 m triangle: `SegsIntersect` multiplies
int64 grid coordinates in double, so beyond about 2^53 a shared endpoint can read as a crossing
and `RemoveIntersection` keeps adding edges (reproducer on the 1e-7 m grid: (0, 0),
(23000030, 0), (534575087, 70489124)). This was reachable on main through `createTerrain`. A
face with one three-vertex loop is now its own tessellation (`tessellate` in
`src/geometry/surface.cpp`); the zero-area refusal is unchanged. Larger faces still go through
Clipper2 and could in principle meet the same precision fault; the vendored source is not
patched. `terrain_tests` checks the triangle in a child process under a 2 GiB address-space cap.

**Verification.** `terrain_import_tests` (dev and sanitize presets) generates the section 6
fixtures in `tests/terrain_import_fixtures.hpp`: `terrain-contours-m.dxf` (16 rotated hill
ellipses, 8 hairpin valley contours clipped by the boundary, a 3D breakline, a LINE contour, a
boundary and a pond hole), `terrain-contours-usft.dxf` (6 decimals, unitless header),
`terrain-utm.dxf` and `terrain-utm.xyz` (translated by (512345.678, 5234567.891) with an 1850 m
datum, written exactly in decimal) and `terrain-invalid.dxf`. It checks: every kept source vertex
in the mesh with Z exactly the source value and `terrain.elevation_at` within 1e-9 m; the usft
fixture equals the metre fixture's triangles and is within 1e-6 m, stating 1200/3937; the UTM
fixture with its proposed origin (512300, 5234600) gives the same triangles, every vertex within
1e-9 m of `source x scale - origin`, the same micrometre-rounded digest after translation, and
save, reopen, Undo and Redo bit for bit; the invalid fixture refused with at least 41 crossing pairs (32
located), a conflicting duplicate and the notices, nothing changed, all bounds held; a crowded
report (100 kinds x 100 locations) within 64 KiB; two imports identical; a shuffled DXF and a
shuffled point file give the identical mesh; each preset, delimiter and header detection,
description-code breaklines, required units and line-numbered errors; inspection, dry run,
origins; and the CLI. The metre fixture's mesh digest is pinned
(`c093a02e...29584`, identical in Debug and Release). The UTM point file (no constraints) is
compared by vertices only: hull points that are nearly collinear may resolve differently under
the translation's 1e-10 m rounding, which is why section 3 pins triangulation to the local frame.
Fuzzing: `parser_fuzz_terrain_dxf` and `parser_fuzz_terrain_points` with seeds from
`parser_fuzz_seeds`, and two `parser_fault_corpus_tests` entries; see
[PARSER_FUZZING.md](../PARSER_FUZZING.md). The libFuzzer targets were not run in this slice (the
seed generator and harness were built and run with GCC).

**Timings.** `terrain_import_tests --timings`, Release, GCC 16.2, best of three, development
machine under other load; source parse plus preparation and triangulation (the document edit
is not included):

| Source | Size | Parse | Prepare and triangulate | Triangles |
| --- | --- | --- | --- | --- |
| 120,000 random points (XYZ) | 3.8 MB | 64 ms | 288 ms | 239,962 |
| 400 contour rings of 300 vertices (DXF) | 2.9 MB | 64 ms | 399 ms | 239,698 |

## Implementation notes (R095.c2)

R095.c2 adds `terrain.create_from_edges`, `terrain.set_parameters` and `terrain.regenerate`.
Modules: `regenerateTerrain`, `terrainTilePaint` and `requireUnlockedTerrain` in
`src/core/terrain.*`; `runTerrainRegenerate`, `terrainTriangulationOptions`,
`validateTerrainParameters` and `terrainEdgeSources` in `src/io/terrain_import.*` (the import's
triangulation stage is shared, so all four paths map the adapter report the same way); the
commands in `src/automation/terrain_commands.cpp`.

**Regenerate.** The mesh is rebuilt from the stored `TerrainRecord` through
`terrainTriangulationInput` and `triangulateTerrain`, with the options the parameters stand for:
Boundary mode uses the stored boundary source, the other modes leave it out of the
triangulation (it stays stored); the maximum edge length applies in MaxEdgeLength mode, and in
Boundary mode when set (as the import applies it), never in ConvexHull mode. The generator
identity is set to the current one. One labelled edit ("Regenerate terrain"; "Terrain
settings" for `terrain.set_parameters`) replaces the tiles and the record (parameters,
generator, mesh digest), so `Document::applyEdit`'s tile rule admits it. The terrain body
keeps its ID, name, parent, transform, tag, material, colour, visibility and lock. A locked
terrain, a locked ancestor or a locked tile refuses (INVALID_OPERATION, the document's lock
message) before triangulating. The document limit counts the other terrains: a mesh that
would pass 250,000 triangles in all is refused in the report (`triangle-budget`).

**Tile IDs.** A body's vertex, face and edge IDs never go back (the document refuses retired
IDs), so a tile body can only be kept when its mesh is unchanged; and the mesh digest reads
tiles in ID order, so IDs must ascend in row-major order. The rule: the leading new tiles
(row-major) whose meshes equal the old tiles' at the same position (old tiles in ascending ID
order) keep those IDs; from the first difference on, every new tile takes a new ID from the
document allocator in row-major order, and old tiles that are not kept are removed. Tile names
are "Terrain tile k" by row-major position, as at creation. The same mesh therefore
regenerates to the same IDs, and regenerating twice gives identical records; a regenerate in
a copy of the document gives identical records too.

**No-op.** A terrain whose mesh, record and tiles already equal what its sources give is left
alone: no edit, no history entry, revision unchanged (`changed: false` from the core call),
following the core operations that return without applying when nothing changes (for
example face texture mapping). Through the batch API such a regenerate is, like any other
command that changes nothing, a batch with no committed changes, which the batch refuses.
A `terrain.set_parameters` that restates the stored parameters is the same.

**Paint notice.** Tiles are rebuilt fresh, so paint on old tiles cannot follow a new mesh: a
tile face with its own colour, material or texture mapping (`face-paint-dropped`, counted in
faces) and a tile with its own material or colour (`tile-paint-dropped`, in tiles) are
reported as notices in the report of the operation record and of the dry run. The terrain's
own material and colour are kept. Paint is dropped even when the mesh is unchanged (the tile
then keeps its ID), so a regenerate of a painted, current terrain is an edit.

**Settings.** `terrain.set_parameters {terrain, boundary?, maxEdgeLength?, flipFlatTriangles?,
steinerFill?}` starts from the stored parameters. `boundary` is `convexHull`, `maxEdgeLength` or
`boundary`; `maxEdgeLength` is 0 (not set) or 0.001 to 2,000,000 m. Boundary mode without a
stored boundary source and MaxEdgeLength mode without a positive length refuse
(INVALID_REQUEST, no report: nothing was triangulated). Then it regenerates in the same edit.

**From edges.** `terrain.create_from_edges {edges, roles?: {contour?, breakline?}, boundary?,
holes?: [[...]], name?, flatMode?, steinerFill?, maxEdgeLength?}`. An edge is
`{body, edge}`, since edge IDs are body-local; at most 100,000 edges in all. `edges` take their
role from Z, `roles.contour` and `roles.breakline` force one, `boundary` is one closed loop and
each list of `holes` one or more closed loops. Checks, in order, before any work: the body
exists (NOT_FOUND), it is not a terrain or tile (UNSUPPORTED_TARGET with the terrain message),
the edge exists (NOT_FOUND), neither the body nor an ancestor is locked (INVALID_OPERATION), and
no edge is given twice (INVALID_REQUEST).

- *Chaining.* Each list is chained on its own. Endpoints are taken in world coordinates through
  the bodies' transforms, so edges inside (nested) groups work; endpoints of one body's vertex
  are one node, and endpoints within 1e-7 m on every axis are one node across bodies. Edges are
  visited in ascending (body, edge) order and nodes numbered by first use; polylines run from
  every node that does not have exactly two selected edges (ends and branch points, in node
  order), and the edges left form closed loops. An edge whose ends are one node is dropped
  (`zero-length-edges` notice). A polyline's reference is its smallest edge, "body B edge E"
  (`TerrainSourceRef::Kind::Edge`). The order only names and orients the polylines: the
  preparation's canonical order (section "Implementation notes (R095.c)", step 6) then sorts
  them, so a shuffled edge list gives the identical mesh (tested).
- *Preparation.* The chains become a source set (format `edges`, unit m, no SHA-256) with the
  layers `edges`, `contour`, `breakline`, `boundary` and `hole` mapped to the auto, contour,
  breakline, boundary and hole roles, and go through `runTerrainImport` with origin (0, 0, 0)
  and no site origin: the terrain frame is the model frame; the site origin is neither set nor
  required, and the report's `originSource` is `model` (no `origin` or `proposedOrigin`). Hence
  the import's rules: a varying-Z "contour" becomes a breakline with a notice; an open boundary or
  hole refuses (`open-polygon`, located by its edge and the XY of its first end); more than one
  boundary loop refuses; contours are pre-clipped to the boundary and holes.
- *Edit.* `createTerrain`, one "Create terrain" edit, at the top level with an identity
  transform. Provenance: empty SHA-256, units "m", 1 m per unit. The source edges stay in the
  model, untouched.

**Reports and refusals.** All three return the import's bounded report: the operation record is
`{command, terrain, tiles, changed, terrainReport}` in `terrainOperations`, and a refusal throws
INVALID_REQUEST (LIMIT_EXCEEDED for budget, extent and limit kinds) with
`details.terrainReport`, leaving revision, history and records unchanged. Regenerate's report
is `{apiVersion, format: "terrain-record", units: "m", metresPerUnit: 1, sources, terrain}` with
XY in the terrain's local metres, references from the stored sources and `outcome`
`regenerated`, `regeneratedWithNotices` or `refused`; from edges, the outcome reads `created`,
`createdWithNotices` or `refused`. The commands are in the shared catalogue with dry run and
revision checks; MCP exposes them and `assistantVisible` hides them (the tool-surface baseline
is unchanged).

**Verification.** `terrain_regenerate_tests` (dev and sanitize presets): regenerating the
imported metre fixture is a no-op with the pinned digest; a terrain whose tile was changed
behind its record shows "changed since generated", regenerates in one labelled edit to the
pinned digest and is current again, with the terrain body otherwise unchanged; Undo and Redo
restore the model exactly (except the ID allocator's high-water mark, which Undo keeps); save
and reopen are bit for bit; the tile-ID rule (new IDs for a changed mesh, removed old tiles,
leading unchanged tiles kept) and determinism across a copy and a second regenerate; each mode
change and each invalid combination of `terrain.set_parameters`, with its dry run; refusals
(crossing stored contours, the document budget, a locked terrain, a missing terrain) that
change nothing; the paint notice in the dry run and the change report with the terrain
colour kept; and from edges: contours and a forced-contour breakline, boundary and hole, the
open-boundary refusal, edges in nested groups, tile-edge, lock, missing, duplicate and
insufficient-point refusals, edge-order independence, and chaining of a T junction and a loop
split across two bodies.

Deviations from the sections above: section 5 lists roles "per selection"; the command takes
them as edge lists (`roles`, `boundary`, `holes`) rather than a selection, and section 3's
"picked edges by edge ID" order is the (body, edge) order before the import's canonical sort.

## Implementation notes (R095.d)

R095.d adds **File → Import terrain…** (after Import Formline…): a file chooser for DXF and
point files (`.dxf`, `.csv`, `.txt`, `.xyz`, `.pts`; at most 64 MiB, read off the UI thread with
the shared file-operation progress), then the import dialog. Modules:
`src/app/terrain_import_dialog.*`, `src/app/terrain_preview.*`, `src/app/window_terrain.cpp`;
`parseTerrainSourceRequest` and `applyTerrainImport` in `src/automation/terrain_commands.*`.

**One core path.** `importTerrainSource` (terrain.import and the CLI) is now
`parseTerrainSourceRequest` + `runTerrainImport` + `applyTerrainImport`. The dialog builds the
same terrain.import fields from its settings (`TerrainImportDialog::command()`), runs the first
two on a worker as the dry run, and Import hands the dry run's result to `applyTerrainImport`:
ONE "Import terrain" edit that creates the terrain and, when the document has none, the site
origin (provenance: the file name and SHA-256). Import is enabled only for a dry run of exactly
the current settings at the current document revision. One addition to the request: the dialog
sets `otherTerrainTriangles` to the document's terrain triangles, so a mesh that would pass the
250,000-triangle document limit is a located `triangle-budget` refusal in the preview instead
of an edit error after Import (R095.c2 added the option for Regenerate; terrain.import leaves it
zero and the edit refuses).

**Responsiveness and memory.** Every change re-runs the dry run after a debounce (0 ms for
choices, 250–400 ms for typed values) on a one-thread pool; a run that is overtaken is
discarded on arrival and one more run follows, so at most one runs and one result is kept. The
worker owns copies of its inputs (the implicitly shared source bytes, the request, the site
origin) and never reads the document; it also builds the preview geometry (`TerrainPreviewData`:
2D points, float elevations, triangle indices and unique edges, at most about 30 MB for the
largest import). The dialog follows document changes (`Window::sync`) and re-runs when the
revision moves.

**Preview.** A plain QPainter top-down view (`TerrainPreview`), not the GL viewport: it needs
no depth, lighting or picking, keeps the dialog independent of the main viewport's GL context,
and renders identically offscreen. Sources draws contours (ink), breaklines (information blue,
dashed), the boundary (accent), holes (amber, dashed) and ground points by role, with a legend;
Surface draws the dry-run mesh's edges; Elevation shading colours lines, points or triangles in
ten bands from green through sand to brown. The drawing is cached in an image per view, points
are drawn in colour batches and off-screen triangles and edges are culled, so 250,000 triangles
redraw only when the view changes. Drag or the arrow keys (Shift for larger steps) pan, the
wheel or + and − zoom, Shift+Z or Home fits (as Fit model and Fit page), and the canvas draws
the viewport's focus ring. The view refits to the data and the size until the user moves it.
Before units are chosen it shows a placeholder: a point file cannot be read without units.

**Settings choices the design left open.**
- *Format* is detected from the extension (`.dxf`, else points). *Units* start at "Choose
  units…"; a DXF whose `$INSUNITS` declares units offers and selects "From the file: <unit>"
  (the section 1 rule: `$INSUNITS` or an explicit choice). Without units the dialog still reads
  the file provisionally in metres to list its layers or codes and columns, but shows no preview
  or report and keeps Import disabled.
- *Roles:* a three-column table (layer or code, contents, role) and one **Role** combo below it
  that sets the role of the selected rows (Extended selection), rather than a combo in every
  cell: one Tab stop for up to 1,024 codes, and the table follows the list keyboard pattern.
  "Automatic" shows the inspection's default ("Automatic: contour"). Point codes also offer
  Ground points.
- *Boundary mode* lists Convex hull, Limit edge length and Boundary layer. Section 3's rule
  (a boundary source makes the mode Boundary) is kept visible rather than hidden: giving a layer
  the Boundary role selects Boundary layer, removing the last one returns to Convex hull, and a
  contradictory choice (Boundary layer without a boundary, or another mode with one, or Limit
  edge length without a length) is named in the footer and keeps Import disabled. Max edge
  length is in metres (the command's unit), "Not set" at 0.
- *Origin:* three spin boxes in metres, filled with the proposed origin from each dry run until
  edited; an edited origin is sent as the explicit origin and "Use proposed origin" returns to
  the proposal. With a document site origin the fields are replaced by the origin read-only (the
  dialog cannot change it); the core's refusal of a differing explicit origin would appear like
  any other refusal (banner, nothing changed). The CRS field starts with the document's label.
  Local extents use the document's display units; source extents are in source units.
- *Point files:* Preset (Detect, PNEZD, PENZD, ENZ, XYZ, Custom), Delimiter (Detect, comma, tab,
  semicolon, spaces) and Header row (Detect, header, none); Detect items name what the parser
  detected ("Detect (comma)"). Up to 16 column meaning combos (four per row) above a sample of
  the first eight data rows split as the parser splits them; changing a column chooses Custom
  and sends the list.
- *Footer:* "Ready: N triangles · K notices" counts notice *kinds* (each a report entry; merges
  included), since occurrence counts of merged duplicates can be large.

**Report.** Show report opens a panel under the preview: a tree with Refusals, Merges (notice
kinds `merged-*`) and Notices, each kind with its message and complete count ("(32 listed)" when
truncated) and at most 32 locations (reference, second reference, XY in source units, detail),
then "… N more not listed". Choosing a location (selection, arrow keys or Enter; Enter stays in
the list and never reaches Import) centres the preview on it, zooms to about an eighth of the
data extent (5–500 m) and rings it in the failure colour. Copy report puts a plain-text form on
the clipboard; Save report… writes that text, or the bounded JSON report when the name ends in
`.json`. A refusal (or a source that cannot be read) shows a failure banner — "Import refused.
Nothing was changed." with the first refusal and the totals — the footer turns to the failure
colour, and Import stays disabled.

**Keyboard and scale.** Every control is a native widget with an accessible name; Tab runs the
settings top to bottom, then the preview column (Sources/Surface as one radio group, shading,
Fit, the canvas, the report), then the footer; Import is the default button and Escape cancels.
Focus returns to the viewport. The dialog sizes to the screen (at most 1,120 × 780), the
settings scroll, and its title and headings follow the interface text size like the export
dialog.

**Verification.** `terrain_import_dialog_tests` (ctest `terrain_import_dialog`, offscreen):
units gate Import (a point file; Enter does nothing; usft conversion; proposed origin), point
presets (detection named, PENZD swaps E and N in the extents, XYZ, Custom columns, a code
ignored), role mapping reaching the stored sources (boundary, ignore, breakline) with Surface
and shading, a refusal (`terrainInvalid()`) showing the banner and leaving the encoded
document, revision and history unchanged, the report grouped, copied and saved, location
selection zooming and ringing, the preview keys, the dialog audit's Tab, Shift+Tab, naming and
default-button checks on two states, and File → Import terrain… importing one labelled,
undoable edit with the terrain selected. It passes at QT_SCALE_FACTOR 1, 1.5 and 2. The
dialog audit inventory (`tests/native_dialog_cases.hpp`) adds the file chooser, a point file
(Import disabled) and a DXF (Enter imports).

## Implementation notes (R095.e1)

R095.e1 adds the desktop surface for existing terrains. Modules: `src/app/terrain_section.*`
(the Entity info section), `src/app/terrain_actions.*` (the dialogs and actions),
`src/app/terrain_report_view.*` (the report tree and text, now shared with the import dialog),
`src/app/viewport_terrain.cpp` (tool hover and the context-menu request), the actions in
`src/app/window_terrain.cpp`, and `terrainExplodeBudget` in `src/core/terrain.*`.

**One core path.** Every action runs its command through the batch API outside any component
scope (`Viewport::commitTerrainCommand`): Terrain settings → `terrain.set_parameters` ("Terrain
settings"), Regenerate → `terrain.regenerate` ("Regenerate terrain"), Explode →
`terrain.explode` ("Explode terrain"), Create terrain from selection →
`terrain.create_from_edges` ("Create terrain"), each one history entry. A refusal arrives as the
batch error with `details.terrainReport` and changes nothing. The Explode confirmation reads
`terrainExplodeBudget`, which `explodeTerrain` now uses for its own refusal, so the dialog and
the command cannot disagree. The four actions are public window actions (command search),
enabled for a selected terrain or selected edges; the section's buttons trigger the same
actions, so the assistant's edit fence applies to both.

**Choices where the design was silent.**
- *When the section shows:* the single selected entity is a terrain, or the selection lies in
  one terrain's tiles (inside the opened terrain). It sits under Entity info and is cached by
  document, revision, terrain and display units.
- *Source file:* the record stores the digest, unit and scale but not a file name; the name is
  shown when the document's site provenance has the same SHA-256 (the import that set the
  origin), otherwise only the digest (first 12 hex digits; the full digest in the tooltip). A
  terrain from edges reads "Model edges (no source file)". Sources are counted by role; point
  groups count their points ("25 spot points").
- *Flat-area result:* not stored in the record, so it is recomputed from the mesh with the
  adapter's own definition (three contour vertices at one elevation within 1e-9 m) and its
  planar area. For a current mesh this is the triangulation's "remaining" count.
- *Display (contour lines, shading):* left out. They have no viewport rendering until R095.e2,
  and storing them needs a decision this slice cannot make (on the record, which changes the
  frozen `terrain-v1` encoding, or as view or scene state); controls with no visible effect
  would mislead. R095.e2 adds them with their rendering.
- *Chip:* "⚠ Changed since generated" with Regenerate in the warning colours of the parameter
  status strip; the section header's detail also reads "Changed". A tile's paint, material or
  colour does not enter the mesh digest (counts, XYZ, indices), so painting never shows the chip;
  a change of a tile's geometry does.
- *Regenerate confirmation:* only when paint would be dropped (`terrainTilePaint`), listing the
  face and tile counts; Cancel is the default, as for Explode.
- *Terrain settings:* the boundary modes are labelled as in the import dialog except "Boundary
  source" (a stored terrain has sources, not layers). Contradictory choices are named before any
  work ("Cannot apply: …", the command's own validation). Every change re-runs
  `runTerrainRegenerate` on a worker (debounced, stale results dropped, as in the import dialog)
  with the paint notices; the footer reads "Ready: N triangles in K tiles · M notices" over the
  report tree with Copy report. Apply is the default and is disabled while checking, on a
  refusal, and when the choices equal the stored parameters (Regenerate is the action for a
  terrain with its own settings, and a no-op batch would commit nothing).
- *Explode confirmation:* "Modelling faces: X now + N = Y of 100,000" and "Terrain triangles:
  T now − N = T′ of 250,000 (N freed)"; over the limit a failure banner explains it and Explode
  is disabled.
- *Create terrain from selection:* the selected edges (curves select their edges) go to the
  command's `edges` list, so roles come from Z (contours and breaklines). The approved design
  gives no way to mark a boundary or holes from a selection, so none is offered here; the command
  still takes them. On success the new terrain is selected; when the report has notices (for
  example remaining flat triangles) or on a refusal, the report opens with the R095.d tree and
  Copy report.
- *Context menu:* the viewport had none. A right click without a drag (under 4 px) opens it, so
  right-drag still pans; the Menu key and Shift+F10 open it at the pointer or the view's
  centre. It holds Create terrain from selection, enabled when the selection has edges.
- *Tools on terrain:* the hover check runs in `Viewport::mouseMoveEvent` (the tool session has no
  pointer path) and never changes the core rule. Push/pull, Offset, Follow Me, Solid tools and
  Face orientation refuse any tile under the pointer. The drawing tools, Tape measure,
  Protractor, Move, Rotate and Scale refuse only inside the opened terrain: from outside, a
  click on a terrain draws in the active context or moves the whole terrain, which is allowed.
  Select, Paint, the camera tools and Intersect (terrain as a reference) never refuse. The cursor
  is Qt's forbidden cursor; the status bar shows the design's sentence and an "Explode
  terrain…" button that opens the same confirmation. The refusal stays while the pointer leaves
  the view (so the button can be reached) and clears when a tool hovers something else or the
  tool changes, restoring the previous status text.

**Verification.** `terrain_desktop_tests` (ctest `terrain_desktop`, offscreen): the section's
fields for a terrain from edges and an imported one, its accessible names, the summit's two flat
triangles (400 m²); paint keeps the digest and shows no chip, a tile geometry change shows it;
Regenerate's paint confirmation (Cancel changes nothing), one "Regenerate terrain" edit, Undo
bringing the chip back, and no confirmation without paint; Terrain settings cancel, the two
invalid combinations, a valid change applied as one "Terrain settings" edit and undone; the
Explode confirmation within budget (Enter cancels; Explode is one edit leaving a group) and
over a test face limit (banner, Explode disabled); Create terrain from selection from the
context menu (Menu key) with its notices report, a crossing-contour refusal located by edges
that changes nothing, and edges in nested groups in world coordinates; the Push/pull cursor and
status sentence over a tile, the status link opening Explode, Paint, Select and Move allowed
from outside, and Move refused inside the opened terrain. It passes at QT_SCALE_FACTOR 1, 1.5
and 2 and at 200 % interface text. The dialog audit inventory adds a `terrain` group: Terrain
settings (Apply disabled until a change), Explode (Cancel default), Regenerate with paint
(Cancel default) and the Create terrain refusal report (Close default).

Deviation: the brief's "open-boundary refusal" from a selection cannot occur without a way to
mark a boundary (see above); the desktop test uses crossing contours, which refuse with located
pairs, and the open-boundary refusal stays covered by `terrain_regenerate_tests`.

## Implementation notes (R095.e2)

R095.e2 completes the desktop: terrain appearance and display settings, and the site UI.
Modules: `src/core/terrain_display.*` (the display edit, contour lines and their cache, the
surface data, the band colours), `src/app/site_dialogs.*` (Site settings, Re-anchor, Set
origin), the Display controls in `src/app/terrain_section.*`, the viewport parts in
`src/app/viewport_terrain.cpp` and `Viewport::rebuild`, the Site row in
`src/app/entity_info_panel.*`, and `@` in `Viewport::measurements` with `parseSitePosition`.

**Terrain material.** New terrains (`createTerrain`, `importTerrain`, so every import and
`terrain.create_from_edges`) give every tile the material "Terrain" (#8a9a6b, opaque) on both
sides, created in the same edit when the document has no material of that name (an existing one
is reused; with a full 1,024-material table the tiles stay plain). The Materials panel's local
library offers the same preset. Regenerate gives the new tiles the old tiles' shared default
material (so a terrain painted as a whole keeps its paint), and mixed tiles return to "Terrain";
`terrainTilePaint` therefore counts a tile only when its material differs from what the new
tiles get. The material is not in the mesh digest.

**Seams and normals.** Tiles stay ordinary geometry; the viewport draws each terrain as one
surface from per-terrain data (`terrainSurfaceDisplay`), rebuilt when the tile records change:
area-weighted normals by exact terrain-frame position over all tiles (a seam vertex's copies
share one normal, equal to the average over every triangle at that position, so shading is
continuous), the terrain's outer boundary edges, and its elevation range. A tile's interior edges
are soft and hidden as before; its other edges are drawn only when they are on the outer
boundary, so seams are neither drawn nor picked or snapped (`Viewport::visible`), and Show hidden
geometry shows them dashed like soft edges. The outer boundary stays drawn: it is neither a seam
nor an interior edge, and outlines the terrain.

**Contour lines** (`terrainContours`) are levels at site elevations: terrain-frame z plus the
terrain's world Z translation and the site origin's elevation, so they read true heights and
stay put through a re-anchor. A triangle meets level L = k × interval when min(e) < L ≤ max(e)
(a vertex at L counts as above it); a crossed edge is interpolated from its endpoints in a
canonical order, so neighbours give bit-identical points; a level that only touches a vertex
gives no segment. Index lines are k divisible by 5. Generation is O(triangles + segments) and
deterministic (tile ID order, triangle order). **Bound:** an upper bound of the segments is
counted first in O(triangles); over 500,000 segments (about 28 MB of segments; the GPU copy is
similar) the interval draws nothing and the section says "No contour lines: this interval needs
about N line segments, over the 500,000 limit". At 249,924 triangles the refusal takes about
0.1 s and 337,155 one-metre segments about 0.3 s (debug build). **Cache:** per terrain, keyed by
the mesh digest, interval and elevation offset; a lookup with the same tile records skips the
digest, a paint-only change recomputes the digest but keeps the lines; at most eight terrains,
least recently used first out, and removed terrains are pruned. The viewport and the Terrain
section share one cache. Contours are drawn after edges with the depth test (surfaces have a
polygon offset), never written to the pick buffers, and hidden with their terrain; index lines
are the line plus four one-pixel screen offsets (drivers limit GL lines to 1 px). A terrain
whose frame is rotated off level would get levels in its own frame; terrains are created level.

**Shading** by elevation bands colours each triangle by the band of its centroid: ten bands over
the terrain's elevation range, green through sand to brown (the import preview's ramp), replacing
the material colour and image; Monochrome style keeps its colours. Band edges therefore follow
triangle edges.

**Display settings storage.** On the terrain record (`TerrainRecord::display`), not a view or
style: the settings belong to one terrain (two terrains can differ), Entity info edits them per
terrain, they travel with copies, and they undo with the model. Model styles are document-wide and
scenes would have to name terrains. Schema 29 had not shipped, so the record gains an optional
`display` object with defaults omitted (ADR 0108, schema 29 addendum): existing schema-29
documents and digests are unchanged. `setTerrainDisplay` is ONE "Terrain display" edit of the
record only, so the mesh digest and "Changed since generated" do not change; equal settings
record nothing; a locked terrain refuses. `terrain.set_parameters` regenerates, so it is not the
place for display settings: **`terrain.set_display`** (`terrain`, optional `contours`,
`contourInterval` 0.01–1,000 m and `shading` `material` or `elevationBands`; omitted fields keep
their values) follows the catalogue, dry-run, revision and assistant-hidden rules of the other
terrain commands, and `terrain.describe` reports `display`.

**Display controls.** Under the Terrain section's fields: a "Display" heading, **Show contour
lines** (check box), **Interval** (spin box in metres, as Terrain settings' maximum edge length;
committed when editing finishes, not per keystroke) and **Shading** (Material, Elevation bands),
then a status line ("18 contour lines · 4 index lines", or why there are none). Each change is
one edit through the batch API; the controls follow Undo. Tab order: Regenerate (when shown),
Contour lines, Interval, Shading, Terrain settings…, Explode terrain….

**Site settings** (File → Site settings…, after Document units): a modal dialog. With an origin:
Easting, Northing, Elevation (metres, three decimals), CRS label, Source and SHA-256 (two lines of
two 16-digit groups; the whole digest in the tooltip and accessible description), all read-only
labels in the Tab order; a note on site = model + origin and the CRS being a label; Re-anchor…;
Close (default). The CRS label is read-only because the core sets it only with the origin
(`site.set` or the first import) and `site.reanchor` keeps it. Without an origin it explains that
the first terrain import establishes it (proposed as the data centre rounded to 100 m) and offers
**Set origin…** (`site.set`: E, N, Z and an optional CRS label; geometry does not move). The site
origin marker shows while the dialog is open.

**Re-anchor…** takes the new E, N, Z (spin boxes in metres, starting at the current origin),
explains that geometry moves so site coordinates are unchanged, shows the current origin and the
model move ("Geometry moves by X … · Y … · Z …"), and runs `site.reanchor` as one edit
(Re-anchor is the default button). It is checked before anything is applied: a locked top-level
body names itself in a failure banner ("Re-anchor is not available: “Fence” is locked, and
re-anchoring moves every top-level body. Unlock it first.") and the coordinate limit and other
refusals come from a dry run of the command on a copy of the document (debounced 200 ms);
Re-anchor stays disabled until the origin differs and while refused.

**Entity info** gains a **Site** row while the document has a site origin: E · N · Z of the
selection's world origin (the point the World-coordinates Origin row shows), and for an edge both
endpoints, each in site and model coordinates. It sits under Origin in every frame (site
coordinates are always world-based), rather than as another frame choice, because the design asks
for model and site coordinates together. Values use the document's display units.

**Measurements box:** `@E, N` or `@E, N, Z` (the coordinate separators of the locale, lengths in
display units unless suffixed) becomes the model point [E − origin E, N − origin N, Z − origin Z]
and is entered as `[x;y;z]` metres, so every tool that takes a point (the drawing tools, Tape
measure, Protractor, Move, Rotate, Scale) accepts it. Without Z the point is on the active
drawing plane above or below E, N. Site values are bounded at ±1e8 m (not the model length
bound); the model point is checked against the coordinate limit. Refusals: no site origin ("…
this document has no site origin. Import a terrain, or set one in File → Site settings…") and a
tool that takes no point.

**Origin marker:** a crosshair with a canvas-coloured halo at model (0, 0, 0), the site origin,
labelled "Site origin · E … · N … · Z …" in display units, drawn over the scene (not depth
tested; skipped behind the camera or far off screen). It shows while a terrain is selected or Site
settings is open; **View → Site origin** (checkable) shows it always; never without a site origin.
The toggle is persisted in the preferences (`siteOriginVisible`) like the navigation choice.

**Verification.** `terrain_display_tests` (ctest `terrain_display`): counts and index lines on a
ramp (40 segments over 10 levels with 2 index levels; 20 when levels pass through grid lines, no
zero-length segments; interval 2 gives 5 levels and 1 index), interval range refusals,
determinism (repeat runs and another document give identical segments; shared edges give
identical points), the cache (reuse without a digest, interval and elevation offset in the key,
paint keeps the lines, capacity and pruning), the bound at 249,924 triangles (dense refusal in
about 0.1 s, 337,155 segments in about 0.3 s, outer edges only), the display edit (one labelled
history entry, digest and "Changed since generated" unchanged, equal settings record nothing,
defaults omitted and only changed fields written, save and reopen, Undo restoring the document
JSON, decoder refusals, locked and missing terrains), `terrain.set_display` (report,
omitted fields kept, describe, refusals changing nothing, catalogue label), the Terrain material
(created in the terrain's edit, reused, undone, carried by Regenerate) and the surface data
(exactly the boundary drawn, seams present but hidden, normals equal to the global average within
1e-12, ten bands). `terrain_site_desktop_tests` (ctest `terrain_site_desktop`, offscreen): the
Display controls' names and Tab order, each change one edit with the digest and chip unchanged,
the status line, Undo, the dense interval; Site settings without an origin (explanation, Set
origin… as one `site.set` edit, the marker while open) and with one (provenance); Re-anchor
through the dialog (one edit, site coordinates of a tile vertex and a wire unchanged, model moved,
Undo), refused for a locked top-level body (banner, Re-anchor disabled, Enter inert, nothing
changed) and past the coordinate limit; `@` drawing a line from site positions with and without
Z, refused without an origin and for Select; the Entity info Site row for an edge and a body and
its absence without an origin; View → Site origin (contextual display, always when checked, the
saved preference read by a new window). It passes at QT_SCALE_FACTOR 1, 1.5 and 2 and at 200 %
interface text. The offscreen platform has no GL context, so the viewport does not render there
(as R095.e1 found); the same test's viewport part checks rendering whenever the renderer is ready,
and it was run on a Wayland session: only the 220 outer-boundary edges of a 6,000-triangle
multi-tile terrain are drawn, the drawn contour segments equal the computed ones, contours and
bands change the frame, a hidden terrain draws no contours. The dialog audit inventory adds Site
settings without and with an origin, Re-anchor (default disabled until a change) and Set origin.

Deviations: none from the decisions. Where the design was silent: the material colour, the
contour and band colours, the outer boundary staying drawn, per-triangle bands, the 500,000-segment
bound and its message, metres for the interval and the site fields, the marker shown by selection
or Site settings and always when View → Site origin is checked, and the Site row under Origin
rather than a new frame choice.

## Implementation notes (R095.e3)

R095.e3 adds owner decision 9: DXF export can write site coordinates. Modules:
`exportDxf(document, metresPerUnit, siteCoordinates)` in `src/io/dxf_export.*`, the CLI flag
`--dxf-site-coordinates` in `src/automation/main.cpp`, and the checkbox in `src/app/window_dxf.cpp`.

- **Conversion.** Each written X and Y is `(model + origin) / unitScale`, computed in double from
  the world-transformed model point, the inverse of the importer's `source x scale - origin`.
  Z is not written: ADR 0122's export is planar and writes Z as zero, and that stays. Arc and circle
  radii and angles are unchanged; only the centre moves. Off (the default) takes the old path, so
  bytes are identical, with or without a site origin.
- **Exactness.** The proposed origin is whole metres, so for metre output `model = source - origin`
  and `model + origin` are exact in double and give back the source double. Other units add at most
  about one ulp in the division. Site text is therefore not the 17-digit form (which would print
  `5234567.8910000001`): it is the fewest-decimal fixed text within four ulps of the value, which
  is the source's own decimals (`5234567.891`). A source finer than four ulps (below 4e-9 at UTM
  scale) is coarsened, which is below the double's own noise. A test imports the UTM fixture with its
  proposed origin, builds the model edges, exports in metres and compares every written X, Y text with
  the source decimals; millimetres and feet are checked to convert back to the same metre values.
- **Surfaces.** The CLI flag is export-only and refuses with INVALID_REQUEST and a clear message when
  the input has no site origin (the core function throws the same message). The desktop dialog's
  checkbox ("Write site coordinates", accessible name and description) sits after the units and before
  the buttons, is unchecked by default and disabled without an origin. There is no DXF export command
  in the batch or MCP catalogue or the assistant, so no catalogue, schema or tool-surface baseline
  changes. The report gains `siteCoordinates: true` and a notice when on; a model with an origin
  exported without it gets the notice "model coordinates".
- **Other exporters.** STL and OBJ reports add a notice that coordinates are model coordinates when
  the document has a site origin; their geometry, bytes and digests do not change. GLB, measured
  PDF/SVG and sheet exports have no coordinate report to extend.
- **Tests.** `dxf_export` (exactness, off by default, refusal, notices), `dxf_import` (CLI flag),
  `stl_export` and `obj_export` (notices), `dxf_input` (checkbox disabled and enabled) and the dialog
  audit case `dxf-export-options-site`.

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
